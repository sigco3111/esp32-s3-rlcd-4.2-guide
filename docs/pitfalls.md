# 함정 18가지 — 실측 로그 기반 상세

> 이 문서의 모든 항목은 **Waveshare ESP32-S3 4.2" RLCD** 보드에서 실제로 겪고 고친 것이다.
> 에러 메시지는 그대로 옮겼고, 없는 로그를 지어내지 않았다.
>
> 원문 부팅 로그: [`logs/factory-bootlog.txt`](logs/factory-bootlog.txt)

---

## 환경 구축 (1–7)

### 1. `pip: command not found`

**증상**
```
pip: command not found
```

**원인**: macOS 기본 `python3` 는 Xcode 것이라 `pip` **바이너리가 아예 없다**(`pip3` 만 있다).
Homebrew python 을 쓸 때 쓰는 `pip` 은 `/opt/homebrew/bin/` 가 아니라
**`/opt/homebrew/opt/python@3.15/libexec/bin/`** 에 있다.

**처방**
```sh
ls /opt/homebrew/opt/python@3.15/libexec/bin/pip
# → /opt/homebrew/opt/python@3.15/libexec/bin/pip

# bin/ 에 노출 (dangling link 되지 않게 실제 경로로)
ln -sf /opt/homebrew/opt/python@3.15/libexec/bin/pip  /opt/homebrew/bin/pip
ln -sf /opt/homebrew/opt/python@3.15/libexec/bin/pip3 /opt/homebrew/bin/pip3
```

**주의**: `bin/` 에 링크하면 dangling link 가 된다. `libexec/bin/` 이 정답.

---

### 2. brew python 이 새 버전을 끌어오면 `pip3` 링크가 뒤집힌다

**증상**
```
$ python3 --version
Python 3.15.x
$ pip3 --version
pip 24.x from ... (python 3.12)     ← 버전이 다름
```

**원인**: brew 가 새 python 을 설치하며 `link` 단계가 `~/Library/Caches/Homebrew/Backup` 으로
백업 후 **`/opt/homebrew/bin/pip3` 링크를 덮어쓴다.**

**처방**: 설치 후 두 버전이 같은 인터프리터를 가리키는지 반드시 대조한다.
```sh
python3 --version && pip --version
```

---

### 3. `error: externally-managed-environment` (PEP 668)

**증상**
```
error: externally-managed-environment

× This environment is externally managed
╰─> To install Python packages system-wide, try brew install
    ...
hint: See PEP 668 for the detailed specification.
```

**원인**: Homebrew python 에 `EXTERNALLY-MANAGED` 마커가 있다.

**처방**
```sh
pipx install esptool        # 격리 venv + ~/.local/bin 셰이빙
pipx install pyserial
```
> `--break-system-packages` 는 시스템 python 을 망가뜨리므로 쓰지 않는다.

---

### 4. brew 와 pipx 에 같은 도구를 둘 다 두면 버전이 갈린다

**증상**: `esptool --version` 이 brew 설치본(5.6.0)과 pipx 설치본(4.12.0) 중
PATH 우선순위로 엇갈려 실행마다 버전이 다르다.

**원인**: `~/.local/bin` 이 `/opt/homebrew/bin` 보다 PATH 앞에 온다.

**처방**: 한쪽만 유지한다. 벤더가 공식 배포하는 도구는 brew 가 자동 업데이트를 맡는다.
```sh
pipx uninstall esptool     # 또는 brew uninstall esptool
```

---

### 5. `brew install ninja-build` 실패 → 전체 롤백

**증상** (실측 출력 원문)
```
==> Downloading Homebrew API data
✔︎ JSON API packages.arm64_golden_gate.jws.json
Warning: No available formula with the name "ninja-build".
Error: No formulae or casks found for ninja-build.
==> Searching for similarly named formulae and casks...
```
cmake, ccache 도 함께 설치되지 않는다.

**원인**: 공식 이름은 **`ninja`** 이다(`ninja-build` 는 존재하지 않는다).
brew 는 install 을 **하나의 transaction** 으로 처리하므로 하나만 틀리면 전부 롤백된다.

**처방**
```sh
brew search ninja        # 설치 전 실제 이름 확인
brew install cmake ninja ccache dfu-util
```

---

### 6. `export.sh` 가 다른 python venv 를 찾는다

**증상**
```
ERROR: Python virtual environment not found for ... 
/home/mac/.espressif/python_env/idf5.5_py3.9_env/bin/python
```

**원인**: `tools/detect_python.sh` 가 PATH 의 `python3` 를 첫 순위로 고정 탐색한다.
설치 시점의 python(brew 3.15)과 셸의 python(Xcode 3.9)이 다르면 서로 다른 venv 를 찾는다.

**처방**: 로그인 셸에서는 `/opt/homebrew/bin/python3` 가 앞에 오는 것이 정상.
wrapper 로 venv 경로를 명시한다.
```sh
#!/bin/sh
# ~/.local/bin/idf
export IDF_PATH="$HOME/esp/esp-idf"
exec "$IDF_PATH"/export.sh
```

---

### 7. `idf.py set-target` 가 ccache 를 조용히 끈다

**증상**: ccache 를 설치했는데도 cmake 실행줄에 `-DCCACHE_ENABLE=0` 이 찍히고
`-- ccache will be used` 메시지가 없다.

**원인**: `idf.py set-target` 가 기본값 `CCACHE_ENABLE=0` 을 넘긴다.

**처방**
```sh
export IDF_CCACHE_ENABLE=1
```
> cmake 실행줄을 직접 읽어야 상태가 보인다.

---

## Wi-Fi (8–10)

### 8. `reason=201` 을 비밀번호 오류로 오해

**증상** (실측 로그 원문)
```
I (757) app_init: Project name:     03_WIFI_Test
STA: connect started
SCAN: 0 AP found — 주변에 2.4GHz 공유기가 없거나 안 잡힘
disconnected: reason=201 ssid=UBOB
STA: retry connect
disconnected: reason=201 ssid=UBOB
```

**원인**: **`reason=201` 은 NO_AP_FOUND — 비밀번호 오류가 아니라 AP 를 못 찾은 것이다.**
비밀번호 오류는 202(AUTH_FAIL) 다.

**처방**: 코드부터 고치지 말고 **보드에서 스캔부터** 한다.
| 스캔 결과 | 결론 | 조치 |
|---|---|---|
| 목표 SSID 가 스캔 목록에 **있음** | 코드/비밀번호 문제 | reason 코드 읽고 인증 방식·비밀번호 확인 |
| 목표 SSID 가 **없음** | 공유기 쪽 문제 | 2.4GHz SSID 실명·동시 모드·숨김 설정 확인 |

> **통제 실험 필수**: directed probe(SSID 이름 지정 스캔)가 0 을 줬다고 "그 SSID 는 아니다"로
> 단정하면 안 된다. **확실히 보이는 SSID** 로 먼저 질문해 1 AP 이 잡히는 것을 확인한 뒤에야
> 유효한 부정 증거가 된다. 0 은 "대상 없음"과 "실험 실패"가 구분되지 않는다.

**추가 확인**: 게이트웨이 MAC 이 보드 스캔 목록에 한 번도 안 나오면, 게이트웨이는
**Wi-Fi 방송기가 아닐 수 있다** — 게이트웨이(방화벽)와 방송기(AP/메쉬)가 별도 장비인 구조다.
게이트웨이가 AP 컨트롤러면 SSID 프로필마다 대역이 고정(Band Mode)돼 5GHz 전용이 된다.

---

### 9. country code 미설정 → 12/13채널 AP 가 막힌다

**증상**: 공유기가 12/13채널일 때 AP 가 안 잡힌다.

**원인**: ESP-IDF 기본 국가 코드는 `"01"`(world safe mode) 이며 12/13채널이 제한된다.
`ieee80211d=true` 면 라우터가 다른 국가 코드로 광고할 때 채널이 잘린다.

**처방**
```c
esp_wifi_set_country_code("KR", false);
```

**실측 로그 원문**
```
I (967) wifi:(2.4G)set country: cc=KR schan=1 nchan=13 policy=1
COUNTRY: KR
I (1019) wifi:mode : sta (28:84:85:xx:xx:xx)
```
> `nchan` 이 13 인지 확인한다. 11 이면 12/13채널 공유기가 안 잡힌다.
> `policy=1` 은 국가코드 변경 시 채널 목록을 강제 적용한다는 의미다.

---

### 10. STA 연결 중 동기 스캔 → 스캔 결과가 빈 목록

**증상**: `esp_wifi_scan_start()` 후 `0 AP found`. 주변에 공유기가 분명히 있는데도.

**원인**: **연결 시도가 진행 중이면 스캔 결과가 비어 나온다.**
`esp_wifi_start()` 직후 STA_START 이벤트에서 `esp_wifi_connect()` 를 호출하는 동안
blocking 스캔을 걸면 둘이 서로 방해한다. 재연결 루프가 있어도 동일하다.

**처방**: **스캔을 완전히 마친 뒤 연결한다.**
```c
esp_wifi_scan_start(&cfg, true);   // blocking
// 스캔 완료 후에만 connect
```
> **추가**: 3~5회 반복 스캔하고 **모든 라운드에 한 번도 안 나온 SSID** 만 "없는 것"으로
> 판단한다. 실측에서 연속 라운드마다 잡힌 AP 수가 달랐다(3 → 4 → 5).

---

## USB (11–18)

### 11. TinyUSB CDC 와 NCM 을 동시에 켜면 보드가 사라진다

**증상**: 플래시는 성공(`Hash of data verified` ×3, `Done`)했는데
`/dev/tty.usbmodem*` 포트가 사라지고 `ioreg -p IOUSB` 에 디바이스가 없다.
허브·컨트롤러는 `active` 인데 **보드만 탈락**했다.

```sh
$ ioreg -p IOUSB -w 0
+-o AppleT8132USBXHCI@02000000  <class AppleT8132USBXHCI, ... active ...>
  +-o USB3 Gen2 Hub@02200000  <class IOUSBHostDevice, ... active ...>
  +-o USB2 Hub@02100000  <class IOUSBHostDevice, ... active ...>
# ↑ 보드만 없다
```

**원인**: USB descriptor 충돌. CDC 와 NCM 은 동시에 활성화할 수 없다.

**처방**: `sdkconfig` 에서 **하나만** 켠다.
```ini
# NCM 사용 시
CONFIG_TINYUSB_CDC_ENABLED=n
# CDC 사용 시
CONFIG_TINYUSB_NET_MODE_NONE=y
```

---

### 12. DWC2 `ep_in_count=5` — 하드웨어 인위적 제한

**증상**: 호스트 로그에 아래가 찍히고 디바이스가 열거되지 않는다.
**디바이스 쪽 로그는 하나도 없다.**
```
AppleUSBXHCICommandRing::setAddress: completed with result code 4
usb2-hub-port2@02120000: AppleUSBHostPort::createDevice: failed to create device (0xe00002bc)
usb2-hub-port2@02120000: AppleUSB20HubPort::resetAndCreateDevice: failed to address device, disabling port
usb2-hub-port2@02120000: AppleUSBHostPort::disconnect: persistent enumeration failures
```

**원인**: `espressif__tinyusb/src/portable/synopsys/dwc2/dwc2_esp32.h` 가
`.ep_count = 7` 인데 `.ep_in_count = 5` 로 적어놨다.
하드웨어(ESP32-S3 TRM)는 **EP0~EP6 = 7개 양방향 엔드포인트**를 지원하므로 이 값은
인위적인 제한이다. `dfifo_alloc()` 이 `ep_in_count` 를 상한으로 쓰기 때문에
IN 엔드포인트가 5개를 초과하면 `TU_ASSERT` 가 **조용히** 실패하고,
그 실패가 `SET_CONFIGURATION` 의 STALL 로 올라가 호스트는 EPIPE 로 해석한다.

> 참고: [espressif/esp-idf#18374](https://github.com/espressif/esp-idf/issues/18374)
> — 동일한 증상(`can't set config #1, error -32`)의 공식 이슈.

**처방**
```c
// espressif__tinyusb/src/portable/synopsys/dwc2/dwc2_esp32.h:47
- { .reg_base = DWC2_FS_REG_BASE, .irqnum = ETS_USB_INTR_SOURCE, .ep_count = 7, .ep_in_count = 5, .ep_fifo_size = 1024 }
+ { .reg_base = DWC2_FS_REG_BASE, .irqnum = ETS_USB_INTR_SOURCE, .ep_count = 7, .ep_in_count = 7, .ep_fifo_size = 1024 }
```
> `.ep_in_count` 를 `.ep_count` 와 같게(7) 고치면 해결된다.
> 같은 값이 ESP32-S2 / ESP32-H4 / ESP32-P4(FS port)에도 있다 — 해당 칩이면 같이 감사한다.

#### macOS 커널 로그 서명표

디바이스가 왜 안 잡히는지 추적할 때 **이 서명만 grep 하면 된다.**
```sh
log show --predicate 'subsystem == "com.apple.iokit.IOAccessoryManager"' --last 5m
```

| 로그 문자열 | 의미 | 조치 |
|---|---|---|
| `Service matched! (serviceName: USB JTAG/serial debug unit)` | **열거 성공** — USB-JTAG 인터페이스 매칭됨 | 정상. 다음 단계로 |
| `dateRate: 2 [12 Mbps (Full Speed)]` | **FS 협상 성공 / High-Speed 실패** | ESP32-S3 네이티브 USB 는 FS 라 정상 동작. 다만 PHY 문제 신호일 수 있음 |
| `[Port-USB-C@5: USB2]` / `[Port-USB-C@6: USB2]` | 어느 USB-C 포트에 붙었는지 | 포트를 바꿔 볼 때 식별자로 사용 |
| `setAddress: completed with result code 4` | **주소 부여 실패** | ← 여기가 진짜 실패 지점 |
| `createDevice: failed to create device (0xe00002bc)` | 디바이스 생성 실패 | 위와 같은 원인의 후속 |
| `resetAndCreateDevice: failed to address device, disabling port` | 포트 영구 비활성화 | 복구 불가 상태 → USB 분리/재연결 또는 BOOT 모드 |
| `disconnect: persistent enumeration failures` | 반복 실패 → 최종 탈락 | **디바이스가 USB 버스에서 사라진 시점** |

> 판독 요령: `Service matched!` 까지 왔는데 `setAddress` 에서 막혔다면
> **디바이스가 살아있는데 호스트가 주소를 못 주는 상태**다.
> TinyUSB descriptor 충돌이나 엔드포인트 고갈이 이 지점에서 죽는다.
> 반대로 `Service matched!` 자체가 없으면 USB PHY 단계에서부터 실패한 것이다.

---

### 13. `rst:0x15` / `boot:0x3` — 어떤 리셋 시퀀스로도 빠져나올 수 없다

**증상**: 플래시가 끝나도 보드가 다운로드 모드에 머문다.

```
ESP-ROM:esp32s3-20210327
rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))
Saved PC:0x40041a76
waiting for download
```

**원인**: ESP32-S3 의 네이티브 USB 리셋은 **GPIO0 스트랩과 무관하게** USB 다운로드
경로를 강제한다. `boot:0x0` = GPIO0 스트랩으로 진입, `boot:0x3` = USB 칩 리셋이 강제.
RTS/DTR 조합 6가지를 시도했으나 **전부 실패**했다.

**처방**: **전원 완전 차단 후 재투입이 유일한 해법.**
```sh
# 케이블을 뽑았다가 다시 꽂는다 (전원 완전 차단)
```
> ⚠️ 전원 재투입 직후에 에이전트가 RTS 를 한 번이라도 토글하면 바로 다운로드 모드로
> 튕긴다. **"그 뒤엔 아무것도 건드리지 말아주세요"** 를 반드시 포함한다.

---

### 14. 측정 도구가 대상을 바꾸면 판정 근거로 쓸 수 없다

**증상**
```sh
$ esptool --port $PORT read-mem 0x60000004
0x60000004 = 0x00000002
```
→ bit0 = 0 이므로 "BOOT 버튼이 눌려 있다"고 단정하게 된다. **그런데 이건 틀렸다.**

**원인**: esptool 이 다운로드 모드에 들어가려면 **DTR 로 GPIO0 을 LOW 로 끌어내려야** 한다.
어떤 물리 상태에서도 0 이 나온다. **측정 도구가 대상을 바꾼 것**이다.

**처방**: 판정 근거는 **"디바이스가 계속 다운로드 모드에 머무는 현상"** 자체다.
"손이 눌려 있나"를 묻지 말고 현상으로 판단한다.

---

### 15. TinyUSB 는 USB PHY 를 점유해서 기존 콘솔을 죽인다

**증상**: TinyUSB 펌웨어를 올리면 기존 `/dev/tty.usbmodem*` 이 사라져서 로그를 못 본다.

**원인**: 네이티브 USB Serial/JTAG 와 USB-OTG(TinyUSB) 가 물리적으로 USB PHY 를 공유한다.

**처방**: TinyUSB 프로젝트에서는 콘솔을 UART 로 돌린다.
```ini
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
```
디버깅 채널은 NCM 이면 HTTP 응답, CDC-ACM 이면 **새 직렬 포트 자체**가 된다.

---

### 16. TinyUSB 컴포넌트를 매니저가 못 찾으면 직접 받아서 벤더링한다

**증상**: `idf_component.yml` 에 `espressif/esp_tinyusb: version: "^2.0.1~1"` 을 쓰면
```
no versions of espressif/esp_tinyusb match ^2.0.1~1
```
인데 **레지스트리 API 로는 그 버전이 존재한다.** 매니저의 prerelease 해석 실패다.

**처방**: 레지스트리에서 URL 을 뽑아 **직접 내려받아 프로젝트 `components/` 에 풀어 넣는다.**
`esp_tinyusb` 는 upstream `tinyusb` 를 `>=0.17.0~2` 로 요구하므로 **둘 다** 넣어야 한다.
```sh
mkdir -p components
curl -L -o esp_tinyusb.zip "<레지스트리에서 구한 esp_tinyusb 2.0.1~1 URL>"
curl -L -o tinyusb.zip    "<레지스트리에서 구한 tinyusb 0.17.0~2 URL>"
unzip -q esp_tinyusb.zip -d components/ && mv components/esp_tinyusb-* components/esp_tinyusb
unzip -q tinyusb.zip    -d components/ && mv components/tinyusb-*    components/tinyusb
```
→ 절차와 URL 패턴: `references/usb-networking.md`

---

### 17. lwIP 를 직접 만질 때 IDF 5.5 API 가 어긋난다

**증상**: 컴파일 에러가 난다 —
`netif_output_fn` 인자 개수 불일치 / `netif_get_addr` 없음 / `ip_addr_t` 를 모른다 /
`IP4ADDR_STRLEN` undefined / `netif_add` 인자 타입 불일치.

**원인**: IDF 5.5 의 lwIP API 변경.

**처방**: 헤더를 먼저 읽는다.
`~/esp/esp-idf/components/lwip/lwip/src/include/lwip/netif.h` 의 typedef 이 정답이다.

| 항목 | IDF 5.5 정답 |
|---|---|
| `netif_output_fn` | `netif_output_fn(netif, pbuf, ipaddr)` — **3인자** |
| `netif->input` | `(pbuf, netif)` — **순서 반대** |
| IP 주소 | `netif->ip_addr.u_addr.ip4` (`netif_get_addr` 는 없음) |
| IP 타입 | `ip4_addr_t` |
| 문자열 길이 | `IP4ADDR_STRLEN_MAX` |

---

### 18. lwIP 컴포넌트 이름이 "esp_lwip" 이 아니다

**증상**
```
Failed to resolve component 'esp_lwip'
```
또는
```
fatal error: esp_timer.h: No such file or directory
```

**원인**: 디렉터리는 `components/lwip` 이고 컴포넌트 이름도 `lwip` 이다.
문서/다른 프로젝트에서는 `esp_lwip` 로 부른다.

**처방**
```cmake
REQUIRES lwip esp_timer
```

---

## 부가 — 파티션 테이블을 바이너리에서 직접 파싱하지 말 것

**증상**: 엔트리 오프셋/사이즈가 뒤틀려 의미 없는 값이 출력된다.
레이블이 빈칸이고 크기가 수십 MB 로 나온다.

**원인**: 파티션 엔트리 레이아웃과 MD5 영역 규칙이 버전마다 달라 손으로 맞추기 어렵다.

**처방**: **부팅 로그가 이미 사람이 읽을 형태로 파티션 테이블을 출력한다.**
```
I (51) boot: Partition Table:
I (60) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (67) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (73) boot:  2 factory          factory app      00 00 00010000 00800000
```
이걸 그대로 읽으면 된다. (출고 펌웨어는 factory 8MB 앱 + nvs + phy_init 구성.)

---

## 부가 — 버튼 동작 (사용자가 직접 알려준 사실)

| 버튼 | GPIO | 역할 |
|---|---|---|
| 왼쪽 KEY | 18 | 사용자 기능 버튼 |
| 가운데 PWR | — | **전원 스위치. 짧게 한 번 = 전원 On, 길게 꾹 = 전원 Off** (하드웨어 전원 래치) |
| 오른쪽 BOOT | 0 | 다운로드 모드 진입 |

> ⚠️ PWR 을 **짧게 누르면 전원이 켜진다** (꺼지지 않음). 전원을 끄려면 **길게** 눌러야 한다.
> ⚠️ 전원 재투입 직후에는 **아무것도 건드리지 말 것**을 반드시 포함할 것.

---

## 부가 — CDC 응답이 `ERR unknown command` 로 오면 버퍼를 의심한다

**증상**: `PING` 에 `ERR unknown command` 가 돌아온다. 또는
`PING` → `'PONG\r\nPONG'` 처럼 두 개가 섞인다. 10회 왕복에서 4회만 성공.

**원인**: 직전 명령의 잔여 응답이 pyserial 버퍼에 남아 있다.

**처방**
```python
s.reset_input_buffer()                     # 보내기 전에 잔여 제거
s.write((c + "\r\n").encode()); s.flush()
...
line = buf.decode("utf-8", "replace").split("\n")[0].strip()   # 첫 줄만 취한다
```
> 하드웨어 문제는 아니다. 이거 안 고치면 "통신이 불안정한 보드"로 오판한다.
