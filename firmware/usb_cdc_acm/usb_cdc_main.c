/*
 * USB CDC-ACM 직렬 통신 — 가장 단순하고 확실한 "USB 로 Wi-Fi 대체"
 *
 * 왜 이것이 최선인가:
 *   · ESP32-S3 의 네이티브 USB 로 추가 드라이버 없이 동작
 *   · TinyUSB CDC-ACM 을 쓰면 Mac 에서 새 /dev/tty.usbmodemXXXX 포트가 뜬다
 *   · 여기서는 한 포트에 "명령 수신 → 응답" 프로토콜을 구현한다
 *   · 보드가 Wi-Fi 를 전혀 쓰지 않으므로 네트워크 문제와 완전 분리된다
 *
 * 프로토콜 (줄 단위, CRLF):
 *   PING            -> PONG
 *   GET /temp       -> 29.75
 *   GET /hum        -> 21.03
 *   GET /status     -> {"uptime":123,"heap":204800,"ip":"none"}
 *   HELP            -> 명령 목록
 *
 * 실제 학습용으로 확장하면:
 *   - 센서 값을 JSON 으로 스트리밍
 *   - AI 음성 대화의 명령/응답 채널
 *   - BLE 와 병행하는 "듀얼 모드" 제어
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "hal/rtc_io_types.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"

static const char *TAG = "USB_CDC";

/* Waveshare ESP32-S3-RLCD-4.2 SHTC3 (온습도) I2C 핀 */
#define SHT_SDA_GPIO   13
#define SHT_SCL_GPIO   14
#define SHT_ADDR       0x70

#define CDC_ITF TINYUSB_CDC_ACM_0

/* ── CDC-ACM 수신 링버퍼 ───────────────────────────────────── */
#define RX_BUF_SIZE 2048
static char s_rx_buf[RX_BUF_SIZE];
static volatile size_t s_rx_len = 0;

/* ── SHTC3 센서 ──────────────────────────────────────────── */
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_shtc3;
static float s_temp = 0.0f, s_hum = 0.0f;
static bool s_sensor_ok = false;

static void shtc3_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SHT_SDA_GPIO,
        .scl_io_num = SHT_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus fail");
        return;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_shtc3) != ESP_OK) {
        ESP_LOGE(TAG, "shtc3 add fail");
        return;
    }
    ESP_LOGI(TAG, "SHTC3 ready (SDA=%d SCL=%d addr=0x%02X)", SHT_SDA_GPIO, SHT_SCL_GPIO, SHT_ADDR);
}

/* SHTC3 CRC-8 (다항식 0x131, 초기값 0xFF) */
static bool shtc3_crc_ok(const uint8_t *data, size_t len, uint8_t crc)
{
    uint8_t calc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        calc ^= data[i];
        for (int b = 0; b < 8; b++) {
            calc = (calc & 0x80) ? (uint8_t)((calc << 1) ^ 0x31) : (uint8_t)(calc << 1);
        }
    }
    return calc == crc;
}

static bool shtc3_read(float *temp, float *hum)
{
    /* MEAS_T_RH_POLLING (0x7866): 온도 먼저, humidity 뒤, clock stretching off */
    uint8_t cmd_measure[2] = {0x78, 0x66};
    if (i2c_master_transmit(s_shtc3, cmd_measure, 2, 100) != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3 write fail");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t raw[6] = {0};
    if (i2c_master_receive(s_shtc3, raw, 6, 100) != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3 read fail");
        return false;
    }

    if (!shtc3_crc_ok(&raw[0], 2, raw[2]) || !shtc3_crc_ok(&raw[3], 2, raw[5])) {
        ESP_LOGW(TAG, "SHTC3 CRC fail: %02x %02x %02x %02x %02x %02x",
                 raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
        return false;
    }

    uint16_t t_raw = (uint16_t)(((raw[0] & 0x7F) << 8) | raw[1]);
    uint16_t h_raw = (uint16_t)(((raw[3] & 0x7F) << 8) | raw[4]);
    *temp = -45.0f + 175.0f * (float)t_raw / 65535.0f;
    *hum  = 100.0f * (float)h_raw / 65535.0f;
    return true;
}

/* ── 자기 업데이트(Self-OTA) ──────────────────────────────── */
#include "esp_ota_ops.h"
#include "esp_partition.h"

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_part = NULL;
static size_t s_ota_written = 0;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* OTA 명령 처리. 응답은 CDC 로 직접 보낸다(handle_command 의 out 을 우회). */
static void ota_handle(const char *cmd)
{
    char out[256];

    if (strncmp(cmd, "OTA BEGIN", 9) == 0) {
        size_t size = 0;
        const char *p = strchr(cmd, ' ');
        if (!p) { sscanf(cmd, "OTA BEGIN %zu", &size); }
        else { sscanf(p, "%zu", &size); }
        if (size == 0 || size > 1024 * 1024) {
            snprintf(out, sizeof(out), "ERR OTA bad size\r\n");
            goto reply;
        }
        s_ota_part = esp_ota_get_next_update_partition(NULL);
        if (!s_ota_part) {
            snprintf(out, sizeof(out), "ERR OTA no partition\r\n");
            goto reply;
        }
        esp_err_t err = esp_ota_begin(s_ota_part, size, &s_ota_handle);
        if (err != ESP_OK) {
            snprintf(out, sizeof(out), "ERR OTA begin %d\r\n", (int)err);
            s_ota_handle = 0;
            goto reply;
        }
        s_ota_written = 0;
        snprintf(out, sizeof(out), "OK OTA begin %zu\r\n", size);
        goto reply;
    }

    if (strncmp(cmd, "OTA DATA", 8) == 0) {
        if (!s_ota_handle) {
            snprintf(out, sizeof(out), "ERR OTA not begun\r\n");
            goto reply;
        }
        const char *hex = strchr(cmd, ' ');
        if (!hex) { snprintf(out, sizeof(out), "ERR OTA bad data\r\n"); goto reply; }
        hex++;                                     /* 공백 뒤 */
        while (*hex == ' ') hex++;
        size_t nchars = strlen(hex);
        size_t nbytes = nchars / 2;
        if (nbytes == 0 || nbytes > 1024) {
            snprintf(out, sizeof(out), "ERR OTA bad chunk\r\n");
            goto reply;
        }
        uint8_t buf[1024];
        for (size_t i = 0; i < nbytes; i++) {
            int hi = hexval(hex[i * 2]);
            int lo = hexval(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) {
                snprintf(out, sizeof(out), "ERR OTA bad hex\r\n");
                goto reply;
            }
            buf[i] = (uint8_t)((hi << 4) | lo);
        }
        esp_err_t err = esp_ota_write(s_ota_handle, buf, nbytes);
        if (err != ESP_OK) {
            snprintf(out, sizeof(out), "ERR OTA write %d\r\n", (int)err);
            goto reply;
        }
        s_ota_written += nbytes;
        snprintf(out, sizeof(out), "OK OTA %zu\r\n", s_ota_written);
        goto reply;
    }

    if (strcmp(cmd, "OTA END") == 0) {
        if (!s_ota_handle) {
            snprintf(out, sizeof(out), "ERR OTA not begun\r\n");
            goto reply;
        }
        esp_err_t err = esp_ota_end(s_ota_handle);
        s_ota_handle = 0;
        if (err != ESP_OK) {
            snprintf(out, sizeof(out), "ERR OTA end %d\r\n", (int)err);
            goto reply;
        }
        err = esp_ota_set_boot_partition(s_ota_part);
        if (err != ESP_OK) {
            snprintf(out, sizeof(out), "ERR OTA setboot %d\r\n", (int)err);
            goto reply;
        }
        snprintf(out, sizeof(out), "OK OTA done, rebooting\r\n");
        goto reply;
    }
    if (strcmp(cmd, "OTA ABORT") == 0) {
        if (s_ota_handle) {
            esp_ota_abort(s_ota_handle);
            s_ota_handle = 0;
        }
        snprintf(out, sizeof(out), "OK OTA abort\r\n");
        goto reply;
    }

    snprintf(out, sizeof(out), "ERR OTA usage\r\n");

reply:
    {
        size_t l = strlen(out);
        tinyusb_cdcacm_write_queue(CDC_ITF, (const uint8_t *)out, l);
        tinyusb_cdcacm_write_flush(CDC_ITF, pdMS_TO_TICKS(200));
    }
    /* OTA END 직후에는 새 파티션으로 재시작해야 한다. */
    if (strncmp(out, "OK OTA done", 11) == 0) {
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
}

/* ── 명령 처리 ────────────────────────────────────────────── */
static void handle_command(const char *cmd)
{
    char out[512];

    if (strcmp(cmd, "PING") == 0) {
        snprintf(out, sizeof(out), "PONG\r\n");
    } else if (strcmp(cmd, "GET /temp") == 0) {
        snprintf(out, sizeof(out), "%.2f\r\n", s_temp);
    } else if (strcmp(cmd, "GET /hum") == 0) {
        snprintf(out, sizeof(out), "%.2f\r\n", s_hum);
    } else if (strcmp(cmd, "GET /status") == 0) {
        snprintf(out, sizeof(out),
                 "{\"uptime\":%" PRIu64 ",\"heap\":%u,\"temp\":%.2f,\"hum\":%.2f,\"sensor\":%s}\r\n",
                 (uint64_t)(esp_timer_get_time() / 1000),
                 (unsigned)esp_get_free_heap_size(),
                 s_temp, s_hum, s_sensor_ok ? "true" : "false");
    } else if (strcmp(cmd, "DLMODE") == 0) {
        /*
         * GPIO0(BOOT) 을 LOW 로 당기고 재시작하면 ROM 다운로드 모드로 진입한다.
         * TinyUSB 가 USB PHY 를 점유한 상태에서는 esptool 의 자동 리셋이
         * 먹히지 않으므로, 보드 스스로가 다운로드 모드로 넘어가는 방법이 필요하다.
         *
         * 주의: esp_restart() 는 GPIO 스트랩을 보존하지 않아 GPIO0 이 HIGH 로
         * 풀릴 수 있다. 그 경우 일반 부팅으로 돌아오고 DLMODE 는 실패한 것처럼
         * 보인다. 실패 시 BOOT 버튼을 누른 채로 케이블을 뽑았다가 꽂으면 된다.
         */
        snprintf(out, sizeof(out), "OK entering download mode...\r\n");
        size_t l = strlen(out);
        tinyusb_cdcacm_write_queue(CDC_ITF, (const uint8_t *)out, l);
        tinyusb_cdcacm_write_flush(CDC_ITF, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(600));

        gpio_config_t io = {
            .pin_bit_mask = 1ULL << 0,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(0, 0);           /* GPIO0 = LOW */
        vTaskDelay(pdMS_TO_TICKS(50));

        /* GPIO0 스트랩을 재시작 후에도 LOW 로 유지해야 ROM 이 다운로드 모드로
         * 진입한다. 일반 gpio hold 는 deep-sleep 전용이라 esp_restart() 를
         * 타면 풀려버린다. RTC GPIO hold 는 RTC 도메인에 붙어 있어
         * RTC 리셋(esp_restart) 을 넘어 유지된다. */
        if (rtc_gpio_is_valid_gpio(GPIO_NUM_0)) {
            rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_OUTPUT_ONLY);
            rtc_gpio_set_level(GPIO_NUM_0, 0);
            rtc_gpio_hold_en(GPIO_NUM_0);
            ESP_LOGI(TAG, "RTC GPIO hold enabled on GPIO0");
        } else {
            ESP_LOGW(TAG, "GPIO0 is not an RTC GPIO on this chip");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else if (strncmp(cmd, "OTA ", 4) == 0) {
        /*
         * 자기 업데이트(Self-OTA).
         *   OTA BEGIN <size>   → ota_0 슬롯에 쓰기 시작 (size 바이트)
         *   OTA DATA <hex>     → 이어서 쓰기 (hex 문자열)
         *   OTA END            → 검증 후 부팅 파티션을 ota_0 로 전환 + 재시작
         *   OTA ABORT          → 쓰기 중단
         *
         * 이게 있으면 앞으로 물리 조작(BOOT 버튼) 없이 호스트에서 보드를
         * 직접 업그레이드할 수 있다. 페이로드는 base64 로 전달한다.
         */
        ota_handle(s_rx_buf);
    } else if (strcmp(cmd, "HELP") == 0) {
        snprintf(out, sizeof(out),
                 "PING | GET /temp | GET /hum | GET /status | HELP | DLMODE\r\n"
                 "OTA BEGIN <size> | OTA DATA <hex> | OTA END | OTA ABORT\r\n");
    } else {
        snprintf(out, sizeof(out), "ERR unknown command\r\n");
    }

    size_t len = strlen(out);
    size_t sent = tinyusb_cdcacm_write_queue(CDC_ITF, (const uint8_t *)out, len);
    if (sent < len) {
        /* 큐가 차면 flush 하고 재시도 */
        tinyusb_cdcacm_write_flush(CDC_ITF, pdMS_TO_TICKS(100));
        tinyusb_cdcacm_write_queue(CDC_ITF, (const uint8_t *)out, len);
    }
    tinyusb_cdcacm_write_flush(CDC_ITF, pdMS_TO_TICKS(100));
}

/* ── CDC-ACM 수신 태스크 ─────────────────────────────────── */
static void cdc_rx_task(void *arg)
{
    uint8_t buf[128];
    while (1) {
        size_t rx_size = 0;
        if (tinyusb_cdcacm_read(CDC_ITF, buf, sizeof(buf), &rx_size) == ESP_OK && rx_size > 0) {
            for (size_t i = 0; i < rx_size; i++) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (s_rx_len > 0) {
                        s_rx_buf[s_rx_len] = '\0';
                        ESP_LOGI(TAG, "RX: %s", s_rx_buf);
                        handle_command(s_rx_buf);
                        s_rx_len = 0;
                    }
                } else if (s_rx_len < RX_BUF_SIZE - 1) {
                    s_rx_buf[s_rx_len++] = c;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* ── 센서 폴링 태스크 ────────────────────────────────────── */
static void sensor_task(void *arg)
{
    while (1) {
        if (shtc3_read(&s_temp, &s_hum)) {
            if (!s_sensor_ok) {
                ESP_LOGI(TAG, "SHTC3 첫 측정 성공");
            }
            s_sensor_ok = true;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== USB CDC-ACM 시작 ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    shtc3_init();

    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = CDC_ITF,
        .callback_rx = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));

    xTaskCreate(cdc_rx_task, "cdc_rx", 4096, NULL, 5, NULL);
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "USB CDC-ACM 준비 완료 — Mac 에서 새 /dev/tty.usbmodem* 포트로 접속");
}
