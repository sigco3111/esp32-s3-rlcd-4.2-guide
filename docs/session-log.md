# 실전 기록 — 이 프로젝트에서 실제로 겪은 일

> 시간순 기록. 각 항목은 **무슨 일이 있었고 → 무엇을 시도했고 → 결과가 어땠고 →
> 결론은 무엇인지**. 추정으로 채운 부분은 없다.
>
> 원본 부팅 로그: [`logs/factory-bootlog.txt`](logs/factory-bootlog.txt)

---

## 1단계 — 구매 후 하드웨어 확인

### 시냅(스킬) 절차로 시작

보드 스펙과 목표를 먼저 확인했다. ESP32-S3 는 **2.4GHz 전용**이라는 점이
모든 후속 판단의 전제가 됐다.

### esptool 4종으로 상태 판정

```sh
esptool --port $PORT chip-id
esptool --port $PORT flash-id
esptool --port $PORT get-security-info
esptool --port $PORT read-mac
```

**결과**: ESP32-S3 rev v0.2 / Flash 16MB / PSRAM 8MB Octal /
Secure Boot 비활성 / Flash Encryption 비활성.

> Secure Boot·Flash Encryption 이 꺼져 있으므로 개발에 방해되는 잠금은 없다.
> 단, 양산 환경으로 갈 때는 켜는 걸 권장한다.

### 출고 펌웨어 백업

```sh
esptool --port $PORT read-flash 0x0 0x400000 current_flash_4mb.bin   # 4,194,304 bytes
esptool --port $PORT read-flash 0x9000 0x6000  nvs_dump.bin          # 24,576 bytes
```

**결과**: 백업 완료. 출고 펌웨어를 덮어써도 되돌릴 수 있게 됐다.

### 부팅 로그 수집 — 펌웨어 식별

부팅 로그가 가장 신뢰할 수 있는 식별 방법이다
(Flash 덤프에서 문자열을 뒤지면 `WIFI_AP_DEF` 같은 상수 문자열 때문에 오판한다).

```
ESP-ROM:esp32s3-20210327
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
I (25) boot: chip revision: v0.2
I (43) boot.esp32s3: SPI Flash Size : 16MB
I (829) esp_psram: Found 8MB PSRAM device
I (1285) app_init: Project name:     03_Fac
```

**결론**: 출고 펌웨어는 `03_Fac` 프로젝트. factory 앱 파티션 8MB.

---

## 2단계 — macOS 개발환경 구축

### 파이썬 3연계

`pip: command not found` → `libexec/bin/` 에서 링크 생성 →
brew python 이 `pip3` 링크를 덮어써서 버전 불일치 → 대조 후 조정.

### `brew install` 전체 롤백

```
Error: No available formula with the name "ninja-build".
```
cmake, ccache 도 안 깔렸다. **brew 는 transaction** 이라 하나만 틀리면 전부 롤백.
정답은 `ninja`.

### export.sh → wrapper

`export.sh` 를 `.zshrc` 에 넣으면 매 셸 시작마다 1.7초 지연.
`~/.local/bin/idf` wrapper 로 대체.

### ccache

`idf.py set-target` 가 `CCACHE_ENABLE=0` 을 기본으로 넘겨서 조용히 꺼짐.
`IDF_CCACHE_ENABLE=1` 로 해결.

---

## 3단계 — Wi-Fi STA 진단

### `reason=201` 해석

```
wifi:sta:connect failed, status: wifi_connect_fail_reason:201
```

**201 은 NO_AP_FOUND** — 비밀번호 오류(202)가 아니라 **AP 를 못 찾은 것**이다.

### country code

ESP-IDF 기본 국가 코드는 `"01"`(world safe mode) → 12/13채널 제한.
`esp_wifi_set_country_code("KR", false)` 로 고정.

### Mac 측 교차검증

```
Mac 이 붙어있는 SSID: Channel 64, 802.11ax → 5GHz
```

**결론**: Mac 이 5GHz 에 붙어있어서 ESP32-S3(2.4GHz 전용)와 대역 불일치.
2.4GHz 목록에는 telecop / hidden(CH7, -36dBm) / A1ASSET / Galaxy Jump2 4304 만 있고
목표 AP 가 없었다.

### directed probe 통제 실험

hidden SSID 를 확정하려고 SSID 이름을 지정해 물어봤다.
**그 전에, 확실히 보이는 SSID 로 같은 실험을 먼저 해 1 AP 이 잡히는 것을 확인**했다.
0 이 나오면 "대상 없음"과 "실험 실패"가 구분되지 않기 때문이다.

### 결론 — 통신 경로 변경

2.4GHz AP 문제에 더 파고들지 않고 **USB 로 대체 회선**을 만들기로 결정.

---

## 4단계 — USB CDC-NCM 첫 시도 → 실패

### descriptor 충돌

CDC 와 NCM 을 함께 켜서 플래시. 결과:
```
Hash of data verified.  ×3
Done.
```
플래시는 성공했는데 **보드가 USB 버스에서 사라졌다.**

```sh
$ ioreg -p IOUSB -w 0
+-o AppleT8132USBXHCI@02000000  <class ..., active ...>
  +-o USB3 Gen2 Hub@02200000  <class IOUSBHostDevice, ..., active ...>
  +-o USB2 Hub@02100000  <class IOUSBHostDevice, ..., active ...>
# ↑ 보드만 없다
```

허브와 컨트롤러는 `active` 인데 **보드만 탈락**. descriptor 충돌이 원인.
→ `CONFIG_TINYUSB_CDC_ENABLED=n` 으로 NCM 만 켠다.

### 그래도 안 됨 — ep_in_count 버그

호스트 로그를 깊이 파자:
```
AppleUSBXHCICommandRing::setAddress: completed with result code 4
usb2-hub-port2@02120000: AppleUSBHostPort::createDevice: failed to create device (0xe00002bc)
usb2-hub-port2@02120000: AppleUSB20HubPort::resetAndCreateDevice: failed to address device, disabling port
usb2-hub-port2@02120000: AppleUSBHostPort::disconnect: persistent enumeration failures
```

디바이스 쪽 로그는 **하나도 없었다**. 이것이 핵심 단서였다.

원인을 찾아보니 `espressif__tinyusb` 의 DWC2 드라이버 버그:
```c
.ep_count = 7, .ep_in_count = 5,   // 하드웨어는 7개 지원
```
IN 엔드포인트가 5개 초과하면 `TU_ASSERT` 가 **조용히** 실패한다.
→ [espressif/esp-idf#18374](https://github.com/espressif/esp-idf/issues/18374)

**패치**: `.ep_in_count = 5` → `7`. 재빌드.

---

## 5단계 — 복구와 CDC 전환

### BOOT 모드가 확실했다

케이블을 다시 꽂아도 안 잡혔다. 커널 로그에 `dateRate: 2 [12 Mbps (Full Speed)]` —
High-Speed 협상 실패. FS 로 떨어지면 ESP32-S3 네이티브 USB 는 열거 자체가 불가능.

**BOOT(GPIO0) 을 누른 채로 케이블을 꽂는 것**이 확실했다.
ROM 부트 로더는 descriptor 문제가 없어서 항상 열거된다.

```sh
$ esptool --port /dev/tty.usbmodem31301 chip-id
MAC:                28:84:85:xx:xx:xx      # ← 실제 MAC 은 여기서만 확인 (문서에는 마스킹)
Stub flasher running.
```
→ 살았다.

### CDC-ACM 플래시 성공

```sh
idf -p /dev/tty.usbmodem31301 flash
# Hash of data verified. ×3
# Done.
```

### 일반 부팅으로 넘기기 — 전원 완전 차단

`rst:0x15 (USB_UART_CHIP_RESET)` 는 GPIO0 스트랩과 무관하게 USB 다운로드 경로를
강제(`boot:0x3`)한다. RTS/DTR 시퀀스 6가지를 시도했으나 **전부 실패**.

**유일한 해법은 전원 완전 차단 후 재투입**이었다.

### 첫 통신 검증 — 성공

```
PING           -> PONG
GET /status    -> {"uptime":48115,"heap":364924,"temp":0.00,"hum":0.00,"sensor":false}
GET /temp      -> 0.00
GET /hum       -> 0.00
HELP           -> PING | GET /temp | GET /hum | GET /status | HELP
```

왕복 10/10 성공, 3.0 KB/s.

> ⚠️ 이때 **`GET /temp` 가 0.00** — 센서가 안 읽혔다. 이유는 5단계 §6 에 있다.

---

## 6단계 — SHTC3 센서 0.00 해결

### 추측하지 않고 벤더 소스에서 읽었다

`05_I2C_SHTC3/main/user_config.h`:
```c
#define ESP32_I2C_SDA_PIN   GPIO_NUM_13     // 내가 11 로 넣었다
#define ESP32_I2C_SCL_PIN   GPIO_NUM_14     // 내가 12 로 넣었다
```

`components/port_bsp/i2c_equipment.h`:
```c
const uint8_t Shtc3Address = 0x70;          // 내가 0x44 로 넣었다
MEAS_T_RH_POLLING  = 0x7866,               // 내가 0x2400 으로 넣었다
```

**원인이 3개 동시에** 있었다:
1. I2C 핀 11/12 → **13/14**
2. 주소 0x44 → **0x70**
3. 측정 명령 0x2400 → **0x7866** + **CRC-8(다항식 0x131) 검증** 추가

수정 후 재빌드 완료. 플래시는 5단계 §7 에서.

---

## 7단계 — BOOT 없이 플래시하기 (Self-OTA)

### 문제

TinyUSB 앱이 USB PHY 를 점유하므로 `idf -p PORT flash` 가
`No serial data received` 로 실패. `--before usb-reset` 로도 같았다.

### DLMODE 시도 — 스트랩 유지 문제

CDC 앱에 `DLMODE` 명령을 넣었다: GPIO0 LOW + `esp_restart()`.

응답은 왔다:
```
OK entering download mode...
```
그런데 **0.3초 뒤 포트가 한 번 바뀐 뒤 다시 TinyUSB 앱으로 돌아온다.**
ROM 다운로드 모드로 진입하지 못했다.

**원인**: `esp_restart()` 는 GPIO 스트랩을 보존하지 않아 GPIO0 이 HIGH(플로팅)로 풀린다.

**해법 시도 1 — `gpio_hold_en()`**: deep-sleep 전용이라 재시작을 못 탄다. ✗

**해법 (정답) — RTC GPIO hold**:
```c
rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_OUTPUT_ONLY);
rtc_gpio_set_level(GPIO_NUM_0, 0);
rtc_gpio_hold_en(GPIO_NUM_0);      /* RTC 도메인 → RTC 리셋을 넘어 유지 */
esp_restart();
```
`SOC_RTCIO_HOLD_SUPPORTED`(ESP32-S3=1) 확인.

> API 함정: `rtc_gpio_output_enable()` 은 **존재하지 않는다**.
> `RTC_GPIO_MODE_OUTPUT_ONLY` 상수는 `hal/rtc_io_types.h` 에 있다.
> `rtc_io.h` 는 `esp_driver_gpio` 내부라 별도 컴포넌트 아님.

### 근본 해법 — Self-OTA

어차피 최초 1회는 BOOT 이 필요하니, **최초 펌웨어부터 Self-OTA 를 포함**시키는 게
정답이다. 나중에 추가하려고 하면 이미 갇혀 있다.

파티션 테이블에 `ota_0`/`ota_1` 추가 + `OTA BEGIN/DATA/END` 명령 구현.
호스트 전송기 `scripts/usb-selfota-push.py`.

**이렇게 하면 이후 펌웨어 교체에 물리 조작이 전혀 필요 없다.**

---

## 8단계 — 저장소 · 블로그 · 핸드오프

### GitHub 저장소

`sigco3111/esp32-s3-rlcd-4.2-guide` — 공개, Pages 배포 완료.

### 블로그

순수 정적 HTML/CSS/JS (빌드 스텝 없음). 다크/라이트 테마, TOC 스크롤 스파이,
코드블록 복사. 18가지 함정 카드 + 8개 표 + 10개 코드블록.

### 보안

공유기 비밀번호·관리자 비밀번호·Wi-Fi PSK 는 **일절 기록하지 않았다.**
MAC 주소는 `28:84:85:xx:xx:xx` 로 마스킹.
자동 검증 스크립트(`scripts/verify-security.sh`) 통과:
시크릿 0 hits / MAC 전체주소 0 hits / U+FFFD 0 hits / 대용량 파일 0.

---

## 남은 작업

| # | 작업 | 상태 | 선행 조건 |
|---|---|---|---|
| ① | Self-OTA 펌웨어 플래시 | 대기 | **BOOT 버튼 1회** |
| ② | SHTC3 온습도 실측 확인 | 대기 | ① |
| ③ | CDC-NCM 전환 검증 | 대기 | 별도 BOOT 1회 가능 |

①이 끝나면 ②는 자동 검증된다. 이후부터는 물리 조작이 끝이다.
