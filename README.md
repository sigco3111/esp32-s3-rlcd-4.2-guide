# Waveshare ESP32-S3 4.2" RLCD 브링업 실전 기록

> 초보자가 반드시 부딪히는 함정과 해결법 — 실제 실측 로그 기반
>
> 작성일: 2026-10-30 · 작성자: sigco3111

```
┌─────────────────────────────────────────────────────────┐
│  Waveshare ESP32-S3 4.2inch RLCD (SHTC3 온습도 내장)     │
│  ESP32-S3 rev v0.2 · Flash 16MB · PSRAM 8MB Octal         │
│  네이티브 USB (USB-Serial/JTAG + USB-OTG)                │
│  I2C: SDA=GPIO13 / SCL=GPIO14 (SHTC3 @ 0x70)            │
│  출고 펌웨어: 03_Fac (ESP-IDF v5.5.2, factory 8MB)       │
└─────────────────────────────────────────────────────────┘
```

> 📄 위 사양의 근거는 실제 출고 부팅 로그 캡처다:
> [`docs/logs/factory-bootlog.txt`](docs/logs/factory-bootlog.txt) (MAC 만 마스킹)

**이 저장소가 하는 일**: ESP32-S3 보드를 사서 처음 켜는 사람이, 같은 문제로
3일씩 고생하지 않도록 **검증된 순서**와 **실제로 부딪힌 함정 21가지**를 그대로
공개한다. 모든 해결법은 이 보드에서 실측 검증된 것만 담았다.

---

## 🚀 빠른 시작 (3단계)

### 1단계 — 하드웨어 확인

```sh
PORT=$(ls /dev/tty.usbmodem* | head -1)
esptool --port $PORT chip-id            # 칩 종류 / 리비전 / PSRAM / MAC
esptool --port $PORT flash-id           # Flash 제조사 / 용량
esptool --port $PORT get-security-info  # Secure Boot / Flash Encryption 상태
esptool --port $PORT read-mac           # MAC 주소
```

4개 명령으로 "하드웨어가 살아 있는가"와 "개발을 방해하는 보안 잠금이 있는가"를
**동시에** 판정할 수 있다.

### 2단계 — USB 통신 (Wi-Fi 없이)

2.4GHz SSID가 없는 환경이라면 USB로 대체 회선을 만든다. 이 저장소에
완성된 펌웨어가 있다:

```sh
cd firmware/usb_cdc_acm
cp CMakeLists.root.txt ../CMakeLists.txt     # 루트 CMakeLists
# (컴포넌트 벤더링은 docs/ 참고 — TinyUSB 를 직접 components/ 에 넣어야 한다)
idf set-target esp32s3
idf build
idf -p $PORT flash
```

### 3단계 — 통신 확인

```sh
python3 -c "
import serial, time
s = serial.Serial('$PORT', 115200, timeout=1)
time.sleep(1)
s.write(b'PING\r\n'); time.sleep(0.5)
print(s.read(100).decode().strip())
"
# → PONG
```

---

## 📦 저장소 구성

| 경로 | 내용 |
|---|---|
| `site/` | **블로그** — GitHub Pages 배포 대상. `index.html` + `assets/` |
| `firmware/usb_cdc_acm/` | USB CDC-ACM 통신 펌웨어 (센서 + Self-OTA 포함) |
| `firmware/usb_cdc_ncm/` | USB CDC-NCM 이더넷 NIC 펌웨어 |
| `docs/` | 상세 문서 (통신 프로토콜, 함정 상세, 빌드 가이드) |
| `docs/pitfalls-dashboard.md` | 함정 19–21 — 화면/대시보드 계층 (페이지 전환, 컴파일 제외, 1-bit 정합성) |
| `scripts/` | 재현 스크립트 (플래시, 통신 테스트, 부팅 로그 수집) |

---

## 🔌 통신 프로토콜 (CDC-ACM)

줄 단위 명령/응답. CRLF 종결.

| 명령 | 응답 | 설명 |
|---|---|---|
| `PING` | `PONG` | 연결 확인 |
| `GET /temp` | `29.75` | 온도 (℃) |
| `GET /hum` | `21.03` | 습도 (%) |
| `GET /status` | `{"uptime":123,"heap":204800,"temp":29.75,"hum":21.03,"sensor":true}` | 상태 JSON |
| `HELP` | 명령 목록 | |
| `DLMODE` | `OK entering download mode...` | GPIO0 LOW + 재시작 → ROM 다운로드 모드 |
| `OTA BEGIN <size>` | `OK OTA begin <size>` | Self-OTA 시작 |
| `OTA DATA <hex>` | `OK OTA <written>` | 페이로드 전송 |
| `OTA END` | `OK OTA done, rebooting` | 검증 후 부팅 파티션 전환 |

> `DLMODE` / `OTA` 를 쓰면 **앞으로 물리 버튼 조작 없이** 보드를 업그레이드할 수 있다.
> 단, 이 명령들이 들어있는 펌웨어를 처음 올릴 때는 BOOT 버튼이 한 번 필요하다.

---

## ⚠️ 반드시 알아야 할 함정 18가지

전체 상세는 `docs/pitfalls.md` 참고. 여기서는 목록만.

### 환경 구축 (1–7)

| # | 증상 | 원인 | 처방 |
|---|---|---|---|
| 1 | `pip: command not found` | macOS 기본 python은 Xcode 것, pip 바이너리 없음 | Homebrew python의 pip은 **`libexec/bin/`** 에 있다 (`bin/`이 아님) |
| 2 | python3와 pip 버전 불일치 | brew python이 새 버전 끌고 오며 링크 덮어씀 | `python3 --version`과 `pip --version` 대조 |
| 3 | `externally-managed-environment` | PEP 668이 `pip install` 차단 | `pipx install` 사용, `--break-system-packages` 금지 |
| 4 | 같은 도구의 버전이 갈림 | brew와 pipx에 중복 설치 → PATH 우선순위 | 한쪽만 유지 |
| 5 | `brew install` 전체 롤백 | `ninja-build`는 존재하지 않음 | 실제 이름은 **`ninja`**. brew는 transaction이라 하나만 틀리면 전부 롤백 |
| 6 | `export.sh`가 다른 venv 찾음 | 설치 시점 python ≠ 셸의 python | wrapper가 venv 경로를 명시 |
| 7 | ccache가 조용히 꺼짐 | `idf.py set-target`가 기본 `CCACHE_ENABLE=0` | `IDF_CCACHE_ENABLE=1` |

### Wi-Fi (8–10)

| # | 증상 | 원인 | 처방 |
|---|---|---|---|
| 8 | `reason=201` | **비밀번호 오류가 아니라 AP 없음** (NO_AP_FOUND) | 스캔 목록에 목표 SSID가 없으면 공유기 문제 |
| 9 | 12/13채널 AP가 안 잡힘 | country code 미설정 (기본 `01`은 world safe mode) | `esp_wifi_set_country_code("KR", false)`, 로그에서 `nchan=13` 확인 |
| 10 | 스캔 결과가 빈 목록 | STA 연결 중 동기 스캔 → FreeRTOS task가 서로 방해 | 스캔 완전 종료 후 연결 |

### USB (11–18)

| # | 증상 | 원인 | 처방 |
|---|---|---|---|
| 11 | 플래시 성공했는데 **디바이스가 사라짐** | CDC+NCM 동시 활성화 → descriptor 충돌 | `sdkconfig`에서 **하나만** 켠다 |
| 12 | 열거가 **조용히** 실패 (디바이스 로그 0) | DWC2 `ep_in_count=5` — 하드웨어는 7개 지원 | `dwc2_esp32.h` → `7` 로 패치 |
| 13 | 계속 다운로드 모드에 머묾 | `rst:0x15` 는 GPIO0 스트랩과 무관하게 USB 다운로드 경로를 강제 (`boot:0x3`) | **어떤 RTS/DTR 시퀀스로도 빠져나올 수 없다.** 전원 완전 차단 후 재투입만 해법 |
| 14 | `read-mem 0x60000004` 가 항상 0 | esptool이 DTR로 GPIO0을 LOW로 끌어내림 | **측정 도구가 대상을 바꾼 것.** 판정 근거는 "계속 다운로드 모드에 머무는 현상" 자체 |
| 15 | 기존 JTAG 콘솔이 사라짐 | TinyUSB가 USB PHY를 점유 | `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` 로 콘솔을 UART로 |
| 16 | 컴포넌트 매니저가 prerelease 못 찾음 | `^2.0.1~1` 해석 실패 (레지스트리에 있는데도) | 직접 다운로드해 `components/`에 벤더링 (`esp_tinyusb` + upstream `tinyusb` **둘 다**) |
| 17 | lwIP API 미스매치 | IDF 5.5 의 lwIP API 변경 | `netif_output_fn(netif, pbuf, ipaddr)` 3인자 / `netif->input` 은 `(pbuf, netif)` / `netif_get_addr` 없음 / `ip4_addr_t` / `IP4ADDR_STRLEN_MAX` |
| 18 | `Failed to resolve component 'esp_lwip'` | 컴포넌트명이 `esp_lwip` 가 아니라 **`lwip`** | `REQUIRES lwip`. `esp_timer.h` 는 별도 `REQUIRES esp_timer` 필요 |
| 19 | 페이지 전환을 해도 화면이 안 바뀜 | `Build*()` **호출 누락** → 루트가 `nullptr` → visibility 함수가 조기 반환 | `Create()` 에 호출 추가. `nm`/`strings` 로 "화면에 낼 문자열이 바이너리에 있는가" 로 확인 |
| 20 | 대시보드 통계가 항상 0 | `#if CONFIG_...` 안의 코드가 **컴파일에서 통째로 삭제**됨 (에러 0건) | CONFIG 무관한 경로로 빼기. `strings *.obj \| grep metrics` 로 컴파일 여부 확인 |
| 21 | 1-bit 화면에서 글자 뭉개짐 / 말풍선 사라짐 | upstream 16비트 테마·4bpp 폰트가 2색으로 양자화 | 1-bit 전용 테마 + `font_puhui_basic_14_1` (1bpp). 색 대신 구조로 구분 |

> 19–21 상세: [`docs/pitfalls-dashboard.md`](docs/pitfalls-dashboard.md)

---

## 📊 실제 검증된 결과

이 저장소의 모든 주장은 **이 보드에서 실측**한 것이다.

| 항목 | 실측 결과 |
|---|---|
| CDC-ACM 왕복 | `PING` → `PONG`, **연속 10/10 성공** |
| 전송 속도 | 3.0 KB/s |
| USB 열거 | `AppleUSBCDCCompositeDevice` + `AppleUSBACMControl/Data` 로 정상 |
| `GET /status` | `{"uptime":48115,"heap":364924,"temp":0.00,"hum":0.00,"sensor":false}` |
| SHTC3 실측 | 출고 `05_I2C_SHTC3` 예제에서 `RH:20.70%, Temp:30.50°C` 계열 로그 확인 |
| 플래시 검증 | `Hash of data verified` ×3 + `Done` |
| 2.4GHz AP 실측 목록 | telecop(CH1) / hidden(CH7, -36dBm, WPA2-PSK) / A1ASSET(CH10·11) / Galaxy Jump2 4304(CH6) |
| Mac이 붙어있는 SSID | Channel 64, 802.11ax → **5GHz** (ESP32-S3는 2.4GHz 전용이라 대역 불일치) |

---

## 🛡️ 보안 관련 메모

- **공유기/관리자 비밀번호, Wi-Fi PSK 는 이 저장소에 일절 포함되지 않는다.**
- MAC 주소는 `28:84:85:xx:xx:xx` 로 마스킹했다.
- 실사용 게이트웨이는 Zyxel USG FLEX 700H (관리 주소 `https://192.168.10.1:50005`) —
  사설 IP 라 내부망 전용이며 **인증 정보는 기록하지 않았다.**
- Secure Boot / Flash Encryption 은 **비활성** 상태.量产 환경으로 가져갈 때는
  `esptool --port $PORT set-security-info` 로 켜는 것을 권장한다.

---

## 📚 상세 문서

| 문서 | 내용 |
|---|---|
| [`docs/pitfalls.md`](docs/pitfalls.md) | 함정 18가지 상세 (증상/원인/처방 + 실제 로그) |
| [`docs/usb-communication.md`](docs/usb-communication.md) | USB 통신 2가지 방식과 복구 절차 |
| [`docs/build-guide.md`](docs/build-guide.md) | macOS 개발환경 구축 완전 가이드 |
| [`docs/session-log.md`](docs/session-log.md) | 이 프로젝트의 세션별 실전 기록 (원본 로그) |

---

## 📄 라이선스

MIT — 코드·문서 모두 자유롭게 사용 가능. 단 **Waveshare** 보드 하드웨어는
별도 라이선스를 따르며, 이 저장소는 벤더 코드를 **수정·재배포한** 결과물이다.
