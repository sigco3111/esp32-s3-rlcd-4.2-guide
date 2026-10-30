/*
 * USB CDC-NCM 네트워킹 — Wi-Fi 없이 USB 로 통신
 *
 * 배경:
 *   이 보드가 붙어 있는 네트워크에는 2.4GHz Wi-Fi 가 없다.
 *   (UBOB 는 5GHz 전용 / 게이트웨이는 Zyxel USG FLEX 700H AP 컨트롤러)
 *   ESP32-S3 는 2.4GHz 전용이라 STA 로 붙을 수 없다.
 *
 * 해결:
 *   USB 를 이더넷 NIC 로 노출한다 (TinyUSB CDC-NCM / Network Control Model).
 *   Mac 은 이 USB 인터페이스를 일반 유선이더넷처럼 인식한다.
 *   Mac 에서 보드 HTTP 서버로 바로 접근 가능하고,
 *   Mac 의 인터넷 공유(NAT)를 빌리면 보드도 외부 통신이 가능하다.
 *
 * 구조:
 *   [Mac] --USB(CDC-NCM, L2 이더넷 프레임)--> [ESP32-S3 lwIP netif 'usb0']
 *                                                 └─ HTTP 서버(:80)
 *
 * 구현 노트 (ESP-IDF 5.5 lwIP):
 *   · netif_output_fn 시그니처는 3인자: (netif, pbuf, ipaddr)
 *   · IP 타입은 ip4_addr_t (lwip/ip4_addr.h)
 *   · tcpip_input(pbuf, netif) 순서
 *   · IP 문자열 길이는 IP4ADDR_STRLEN_MAX
 *   · esp_netif 공개 API 로는 외부 netif 를 붙일 수 없어 lwIP 를 직접 다룬다.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

static const char *TAG = "USB_NCM";

#define TX_QUEUE_LEN 16
#define HTTP_PORT    80

static struct netif s_usb_netif;
static bool     s_usb_link_up = false;
static QueueHandle_t s_tx_queue;

/* ── lwIP netif → USB ────────────────────────────────────── */
static err_t usb_netif_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr)
{
    if (!s_usb_link_up) {
        return ERR_IF;
    }
    struct pbuf *copy = pbuf_alloc(PBUF_RAW, p->tot_len, PBUF_POOL);
    if (!copy) {
        return ERR_MEM;
    }
    if (pbuf_copy(copy, p) != ERR_OK) {
        pbuf_free(copy);
        return ERR_MEM;
    }
    if (xQueueSend(s_tx_queue, &copy, 0) != pdTRUE) {
        pbuf_free(copy);
        return ERR_MEM;
    }
    return ERR_OK;
}

static err_t usb_netif_init(struct netif *netif)
{
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output = usb_netif_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

/* ── USB → lwIP netif ────────────────────────────────────── */
static esp_err_t usb_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (!p) {
        return ESP_FAIL;
    }
    if (pbuf_take(p, buffer, len) != ERR_OK) {
        pbuf_free(p);
        return ESP_FAIL;
    }
    if (s_usb_netif.input) {
        /* lwIP 의 netif_input_fn 시그니처는 (pbuf, netif) 순서 */
        s_usb_netif.input(p, &s_usb_netif);
    } else {
        pbuf_free(p);
    }
    return ESP_OK;
}

/* ── lwIP → USB 실제 전송 태스크 ──────────────────────────── */
static void usb_tx_task(void *arg)
{
    while (1) {
        struct pbuf *p = NULL;
        if (xQueueReceive(s_tx_queue, &p, portMAX_DELAY) == pdTRUE && p) {
            tinyusb_net_send_sync(p->payload, p->tot_len, NULL, pdMS_TO_TICKS(100));
            pbuf_free(p);
        }
    }
}

static void usb_netif_ip_str(char *out, size_t len)
{
    /* netif_get_addr 가 없는 lwIP 이므로 netif->ip_addr 에서 직접 읽는다 */
    ip4_addr_t *ip = &s_usb_netif.ip_addr.u_addr.ip4;
    ip4addr_ntoa_r(ip, out, (int)len);
}

/* ── lwIP BSD socket HTTP 서버 ───────────────────────────── */
static void http_task(void *arg)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket() failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HTTP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    if (listen(listen_fd, 4) < 0) {
        ESP_LOGE(TAG, "listen() failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "HTTP server listening on :%d (usb0)", HTTP_PORT);

    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int fd = accept(listen_fd, (struct sockaddr *)&client, &clen);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char req[512];
        int r = recv(fd, req, sizeof(req) - 1, 0);
        if (r > 0) {
            req[r] = '\0';
            char ipstr[IP4ADDR_STRLEN_MAX];
            usb_netif_ip_str(ipstr, sizeof(ipstr));

            char body[640];
            snprintf(body, sizeof(body),
                     "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta http-equiv='refresh' content='5'>"
                     "<title>ESP32-S3 USB-NCM</title></head><body>"
                     "<h1>ESP32-S3 &mdash; USB CDC-NCM</h1>"
                     "<p>Wi-Fi 없이 USB 로 통신 중입니다.</p>"
                     "<ul>"
                     "<li>보드 IP: <code>%s</code></li>"
                     "<li>링크: <code>%s</code></li>"
                     "<li>uptime: <code>%" PRIu64 " ms</code></li>"
                     "<li>free heap: <code>%u</code></li>"
                     "</ul><p>5초마다 갱신</p></body></html>",
                     ipstr, s_usb_link_up ? "UP" : "DOWN",
                     (uint64_t)(esp_timer_get_time() / 1000),
                     (unsigned)esp_get_free_heap_size());

            char hdr[256];
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n\r\n", (int)strlen(body));
            send(fd, hdr, hn, 0);
            send(fd, body, strlen(body), 0);
            ESP_LOGI(TAG, "HTTP served (board ip=%s)", ipstr);
        }
        close(fd);
    }
}

/* ── 상태 폴링 태스크 ────────────────────────────────────── */
static void status_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        char ipstr[IP4ADDR_STRLEN_MAX];
        usb_netif_ip_str(ipstr, sizeof(ipstr));
        ESP_LOGI(TAG, "usb0 IP=%s link=%s up=%" PRIu64 "ms",
                 ipstr, s_usb_link_up ? "UP" : "DOWN",
                 (uint64_t)(esp_timer_get_time() / 1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== USB CDC-NCM 시작 ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(struct pbuf *));
    ESP_ERROR_CHECK(s_tx_queue ? ESP_OK : ESP_ERR_NO_MEM);

    /* lwIP netif 직접 생성 */
    ip4_addr_t ip, mask, gw;
    ip4addr_aton("192.168.7.2", &ip);
    ip4addr_aton("255.255.255.0", &mask);
    ip4addr_aton("192.168.7.1", &gw);

    if (netif_add(&s_usb_netif, &ip, &mask, &gw, NULL, usb_netif_init, tcpip_input) == NULL) {
        ESP_LOGE(TAG, "netif_add failed");
        return;
    }
    netif_set_up(&s_usb_netif);
    netif_set_default(&s_usb_netif);
    ESP_LOGI(TAG, "lwIP netif 'usb0' ready (192.168.7.2/24, gw 192.168.7.1)");

    /* TinyUSB NCM 시작 */
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_net_config_t net_config = {
        .on_recv_callback = usb_recv_callback,
        .free_tx_buffer = NULL,
        .user_context = NULL,
    };
    esp_read_mac(net_config.mac_addr, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "USB NIC MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             net_config.mac_addr[0], net_config.mac_addr[1], net_config.mac_addr[2],
             net_config.mac_addr[3], net_config.mac_addr[4], net_config.mac_addr[5]);
    ESP_ERROR_CHECK(tinyusb_net_init(&net_config));

    xTaskCreate(usb_tx_task, "usb_tx", 4096, NULL, 5, NULL);
    xTaskCreate(http_task, "http", 6144, NULL, 4, NULL);
    xTaskCreate(status_task, "stat", 4096, NULL, 3, NULL);

    while (1) {
        if (!s_usb_link_up) {
            s_usb_link_up = true;
            ESP_LOGI(TAG, "USB network link UP — Mac 에서 http://192.168.7.2 로 접속 가능");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
