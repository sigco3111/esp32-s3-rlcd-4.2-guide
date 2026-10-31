# 13 — USB CDC-ACM (직렬 통신)

**가장 단순하고 확실한 "USB 로 Wi-Fi 대체"** 예제.
TinyUSB CDC-ACM 으로 Mac 에 `/dev/tty.usbmodemXXXX` 포트를 만들고,
줄 단위 명령/응답 프로토콜을 구현한다.

## 왜 이것이 최선인가

- ESP32-S3 의 **네이티브 USB** 라 추가 드라이버 없이 동작
- 보드가 Wi-Fi 를 전혀 쓰지 않으므로 **네트워크 문제와 완전 분리**
- USB CDC-NCM(12)보다 **프로토콜이 단순**하다.
  L2 이더넷을 다룰 필요가 없다.

## 프로토콜 (줄 단위, CRLF 종결)

| 명령 | 응답 | 설명 |
|---|---|---|
| `PING` | `PONG` | 연결 확인 |
| `GET /temp` | `29.75` | 온도 (℃) — SHTC3 |
| `GET /hum` | `21.03` | 습도 (%) — SHTC3 |
| `GET /status` | `{"uptime":123,"heap":204800,"ip":"none"}` | 상태 JSON |
| `HELP` | 명령 목록 | |
| `DLMODE` | `OK entering download mode...` | GPIO0 LOW + 재시작 |
| `OTA BEGIN <size>` | `OK OTA begin <size>` | Self-OTA 시작 |
| `OTA DATA <hex>` | `OK OTA <written>` | 페이로드 전송 |
| `OTA END` | `OK OTA done, rebooting` | 검증 후 부팅 파티션 전환 |
| `OTA ABORT` | `OK OTA aborted` | 취소 |

## Self-OTA — 물리 조작 없이 보드 업그레이드

```
[Mac] --USB--> [ESP32-S3] ──> ota_0 파티션 쓰기 ──> 부팅 파티션 전환
```

`ota_0` / `ota_1` 두 슬롯을 번갈아 쓴다.
보드가 **Download Mode 로 직접 들어가지 않아도 된다.**
→ [문서: Self-OTA](../docs/pitfalls.md)

## 사용법

```bash
# 포트 찾기
ls /dev/tty.usbmodem*

# 테스트
./scripts/test-cdc.sh
```

## 파티션

`partitions.csv` — factory + 2개 OTA 슬롯 (총 3.1MB).
★ 보드는 **16MB** 인데 ESP-IDF 기본 flash size 는 **2MB** 다.
`sdkconfig.defaults` 에 16MB 설정을 명시하지 않으면
`does not fit in configured flash size 2MB` 로 빌드가 막힌다.

## 빌드

```bash
cd 13_usb_cdc

# ★ sdkconfig 는 생성물이라 커밋하지 않는다.
#   처음 빌드할 때 target 과 flash size 를 명시해야 한다.
idf.py set-target esp32s3
idf.py build
```

★ **왜 `set-target` 이 필요한가**
`sdkconfig.defaults` 에 `CONFIG_IDF_TARGET="esp32s3"` 가 있어도
IDF 는 처음에 `esp32` 로 target 을 잡는다. 그대로 빌드하면
`is not compatible with target "esp32"` 로 막힌다.

★ **왜 flash size 를 명시해야 하는가**
ESP-IDF 의 기본 flash size 는 **2MB** 다. 이 보드는 **16MB** 이고
`partitions.csv` 는 3.1MB 를 쓴다. 기본값으로 빌드하면
`Partitions tables occupies 3.1MB ... does not fit in configured flash size 2MB`
로 막힌다.

전 예제 한 번에 빌드하려면 저장소 루트에서:
```bash
./firmware/build-all-examples.sh
```
