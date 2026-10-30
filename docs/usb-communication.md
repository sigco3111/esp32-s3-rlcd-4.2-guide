# USB 로 통신하기 — Wi-Fi 없이 ESP32-S3 를 호스트에 붙이는 법

> Waveshare ESP32-S3 4.2" RLCD 기준. 모든 내용은 실측 검증 완료.

---

## 왜 USB 로 바꾸는가

ESP32-S3 는 **2.4GHz 전용**이다. 실측 환경은 이런 상태였다:

| 관측 | 값 | 의미 |
|---|---|---|
| Mac 이 붙어있는 SSID | Channel 64, 802.11ax | **5GHz** → ESP32-S3 는 붙을 수 없음 |
| 2.4GHz 로 보이는 AP | telecop(CH1), hidden(CH7, -36dBm, WPA2-PSK), A1ASSET(CH10/11), Galaxy Jump2 4304(CH6) | 목표 2.4GHz AP 없음 |

"왜 안 붙지"를 설명하는 가장 단순한 답은 **대역 불일치**다.
이걸 먼저 확인하면 다른 추론이 불필요해진다.

목표 2.4GHz AP 가 없거나 붙을 수 없다면, **통신 경로를 바꾼다.**
ESP32-S3 의 네이티브 USB 로 호스트와 직접 통신할 수 있다.

---

## 두 가지 방식 비교

| | **USB CDC-ACM** | **USB CDC-NCM** |
|---|---|---|
| 호스트에서 보이는 것 | 새 직렬 포트 `/dev/tty.usbmodemXXXX` | 새 이더넷 인터페이스 (`en8` 등) |
| 제공 기능 | 줄 단위 명령/응답, 센서 조회, 제어 채널 | IP 계층 — HTTP, 소켓, TCP/UDP 전부 |
| 디버깅 채널 | **그 포트 자체가 채널** (응답이 곧 로그) | HTTP 응답 (조용히 안 responding 하면 원투정이 어렵다) |
| 실패 시 추적성 | 높음 — 명령을 하나씩 던져 보면 된다 | 낮음 — HTTP 200 없음 = 이유를 추측해야 한다 |
| 호스트 작업량 | 낮음 — pyserial | 높음 — lwIP 를 직접 만진다 |
| Mac 인터넷 공유(NAT) | 불가 (IP 계층 없음) | **가능** → 보드도 외부 통신 가능 |
| 난이도 | ★☆☆ | ★★★ |

### 결론: **CDC-ACM 이 먼저다**

IP 통신이 꼭 필요할 때 그때 NCM 으로 전환한다.
CDC-ACM 은 새 직렬 포트를 주므로 호스트에서 `PING`/`GET /status` 로 즉시 검증할 수 있고,
**실패해도 디버깅 채널(그 포트 자체)이 남는다.**

---

## CDC-ACM 프로토콜

줄 단위 명령/응답. CRLF(`\r\n`) 종결.

### 명령표

| 명령 | 응답 예시 | 설명 |
|---|---|---|
| `PING` | `PONG` | 연결 확인 |
| `GET /temp` | `29.75` | 온도 (℃) |
| `GET /hum` | `21.03` | 습도 (%) |
| `GET /status` | `{"uptime":350246,"heap":364856,"temp":29.75,"hum":21.03,"sensor":true}` | 상태 JSON |
| `HELP` | 명령 목록 | |
| `DLMODE` | `OK entering download mode...` | GPIO0 LOW + RTC 홀드 + 재시작 → ROM 다운로드 모드 |
| `OTA BEGIN <size>` | `OK OTA begin 276480` | Self-OTA 시작 (ota_0 슬롯) |
| `OTA DATA <hex>` | `OK OTA 1024` | 페이로드 이어서 쓰기 |
| `OTA END` | `OK OTA done, rebooting` | 검증 → 부팅 파티션 전환 → 재시작 |
| `OTA ABORT` | `OK OTA abort` | 쓰기 중단 |

### 호스트 구현 (중요: 버퍼 처리)

```python
import serial, time

s = serial.Serial('/dev/tty.usbmodemXXXX', 115200, timeout=0.3)
time.sleep(1.5)

def cmd(c, wait=1.2):
    s.reset_input_buffer()          # ← 직전 응답 잔여 제거 (안 하면 섞인다)
    s.write((c + "\r\n").encode())
    s.flush()
    time.sleep(0.35)
    buf = b""
    t0 = time.time()
    while time.time() - t0 < wait:
        try:
            chunk = s.read(4096)
        except Exception:
            break
        if chunk:
            buf += chunk
            if b"\r\n" in buf or b"\n" in buf:
                break
    # ← 첫 줄만 취한다
    return buf.decode("utf-8", "replace").split("\n")[0].strip()

print(cmd("PING"))        # PONG
print(cmd("GET /status")) # {"uptime":...}
```

> ⚠️ `reset_input_buffer()` 를 빼면 `PING` → `'PONG\r\nPONG'` 처럼 섞여서
> "통신이 불안정한 보드"로 오판하게 된다. **하드웨어 문제가 아니다.**

### 실측 성능

| 항목 | 결과 |
|---|---|
| 왕복 성공률 | **10/10 (100%)** |
| 송신 속도 | 3.0 KB/s |
| 안정 동작 | 17분+ (uptime 1,065,997 ms) |
| macOS 열거 | `AppleUSBCDCCompositeDevice` + `AppleUSBACMControl` + `AppleUSBACMData` |

---

## CDC-NCM 구조

```
[Mac] --USB(CDC-NCM)--> [ESP32-S3 lwIP netif 'usb0'] --HTTP(:80)
```

- Mac 이 보드를 **유선이더넷 인터페이스**로 인식한다
- 보드 IP 는 `192.168.7.2`
- Mac 의 **인터넷 공유(NAT)** 를 보드에 빌려주면 보드도 인터넷 사용 가능
  → AI 음성 API 호출, OTA 전부 가능

### 검증 절차

```sh
# 1) 새 이더넷 인터페이스 확인
ifconfig -l | grep -E "^en[0-9]+$"
ifconfig enX | grep "inet 192.168.7"

# 2) 보드 HTTP 응답
curl -s -m 8 http://192.168.7.2/

# 3) Mac 인터넷 공유 연결 후 보드 → 외부 통신
```

> ⚠️ CDC-NCM 은 TinyUSB 가 USB PHY 를 점유하므로 **CDC 포트가 사라진다.**
> 실패하면 BOOT 모드 복구가 필요하다. 그래서 CDC-ACM 을 먼저 검증한다.

---

## 디바이스가 USB 버스에서 사라졌을 때 복구

### 상황

```sh
$ ls /dev/tty.usbmodem*
zsh: no matches found: /dev/tty.usbmodem*
$ ioreg -p IOUSB -w 0 | grep -i espressif
(출력 없음)
```

### 원인 3분류

| 분류 | 근거 | 해법 |
|---|---|---|
| **① descriptor 충돌** | CDC+NCM 동시 활성화 | `sdkconfig` 에서 하나만 켠다 → refl ash |
| **② 엔드포인트 고갈** | `setAddress: result code 4` 로그 | `ep_in_count` 7 로 패치 → refl ash |
| **③ USB PHY FS 협상 실패** | `dateRate: 2 [12 Mbps (Full Speed)]` | BOOT 모드로 복구 |

### 복구 절차 — BOOT(GPIO0) 모드가 확실하다

케이블을 뽑았다가 꽂는 것보다 **BOOT 버튼을 누른 채로 연결**하는 게 확실하다.
ROM 부트 로더는 TinyUSB descriptor 문제가 없어서 **항상** 열거된다.

```sh
# ① BOOT 누른 채로 케이블 꽂기
# ② 포트 탐색
ls /dev/tty.usbmodem*
# ③ 살아있음 확인
esptool --port /dev/tty.usbmodemXXXX chip-id
# ④ 고쳐진 펌웨어 구우기
idf -p /dev/tty.usbmodemXXXX flash
```

### 복구 후 일반 부팅으로 넘기기

```sh
# ⑤ BOOT 을 놓고, 전원을 완전히 껐다 켠다
#    (USB 케이블 뽑았다가 다시 꽂으면 된다)
```

> ⚠️ **ESP32-S3 는 플래시 후에도 자동으로 일반 부팅하지 않는다.**
> `rst:0x15 (USB_UART_CHIP_RESET)` 는 USB 칩 리셋이고, 이 리셋은 GPIO0 스트랩과
> 무관하게 USB 다운로드 경로를 강제한다(`boot:0x3`).
> 그래서 **어떤 RTS/DTR 리셋 시퀀스로도 빠져나올 수 없다.**
> 전원 완전 차단이 유일한 경로다.

---

## Self-OTA — 물리 조작 없는 업그레이드

### 왜 이것이 필요한가

TinyUSB 앱이 USB PHY 를 점유하면 호스트는 보드에 펌웨어를 넣을 수단이 사라진다.
esptool 의 자동 리셋은 ROM 다운로드 로더에 닿아야 하는데 그 경로가 TinyUSB 에 막혀 있다.

> **근본 원칙: TinyUSB 앱이 USB PHY 를 점유하는 순간, 호스트는 보드에 펌웨어를 넣을
> 수단이 없어진다.** 그래서 **최초의 CDC 펌웨어부터 Self-OTA 를 포함시켜야 한다** —
> 나중에 추가하려고 하면 이미 갇혀 있다(부트스트랩 문제).

### 파티션 테이블

`ota_0`/`ota_1` 슬롯이 있어야 한다.
factory-only 테이블에서는 `esp_ota_get_next_update_partition()` 이 NULL 을 준다.

```csv
# partitions.csv
nvs,      data, nvs,     ,        0x4000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        1M,
ota_0,    app,  ota_0,   ,        1M,
ota_1,    app,  ota_1,   ,        1M,
```

```ini
# sdkconfig.defaults
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
```

### 보드 측 구현

```c
#include "esp_ota_ops.h"
#include "esp_partition.h"

esp_ota_handle_t h = 0;
const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);

esp_ota_begin(part, size, &h);
esp_ota_write(h, buf, n);        // 반복
esp_ota_end(h);
esp_ota_set_boot_partition(part);
esp_restart();                   // 새 파티션으로 부팅
```

### 호스트 측 전송

```sh
python3 scripts/usb-selfota-push.py build/usb_cdc.bin
```

성공 기준: `OK OTA done, rebooting` → 재부팅 후 CDC 포트 재열거 →
`GET /status` 가 `sensor:true` 면 완료.

---

## DLMODE — ROM 다운로드 모드로 스스로 진입시키기

```c
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

/* GPIO0 스트랩을 재시작 후에도 LOW 로 유지해야 ROM 이 다운로드 모드로 진입한다.
 * gpio_hold_en() 은 deep-sleep 전용이라 esp_restart() 를 못 tangible.
 * RTC GPIO hold 는 RTC 도메인에 붙어 있어 RTC 리셋을 넘어 유지된다. */
if (rtc_gpio_is_valid_gpio(GPIO_NUM_0)) {
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_0, 0);
    rtc_gpio_hold_en(GPIO_NUM_0);
}
esp_restart();
```

> ⚠️ **DLMODE 는 응답해도 ROM 진입을 보장하지 않는다.**
> 스트랩이 풀리면 보드는 그냥 일반 부팅으로 돌아온다.
> `rtc_gpio_output_enable()` 은 **존재하지 않는다** —
> `rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_OUTPUT_ONLY)` +
> `rtc_gpio_set_level()` 을 쓴다.
> `RTC_GPIO_MODE_OUTPUT_ONLY` 상수는 `hal/rtc_io_types.h` 에 있다.
> `rtc_io.h` 는 `esp_driver_gpio` 컴포넌트 내부라 `REQUIRES esp_driver_gpio` 면 충분하다.

---

## 관련 문서

- [`pitfalls.md`](pitfalls.md) — 함정 18가지 상세
- [`build-guide.md`](build-guide.md) — macOS 개발환경 구축
- [`session-log.md`](session-log.md) — 실전 기록 원본
- 구현: [`../firmware/usb_cdc_acm/`](../firmware/usb_cdc_acm/) , [`../firmware/usb_cdc_ncm/`](../firmware/usb_cdc_ncm/)
- 스크립트: [`../scripts/`](../scripts/)
