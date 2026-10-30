# macOS 에서 ESP32-S3 개발 환경 구축 완전 가이드

> macOS 27.2 (Apple Silicon) 에서 ESP-IDF v5.5.5 로 Waveshare ESP32-S3 4.2" RLCD 를
> 개발하는 데 필요한 전 과정. 실제로 겪은 함정과 해결책 포함.

---

## 1. 사전 요구사항

| 항목 | 필요 여부 | 확인 방법 | 비고 |
|---|---|---|---|
| Xcode CLT | 필수 | `xcode-select -p` | 파이썬·git 빌드 도구 |
| Homebrew | 필수 | `brew --version` | Apple Silicon 기준 `/opt/homebrew` |
| Python 3.11+ | 필수 | `python3 --version` | IDF 5.5 요구 |
| Git | 필수 | `git --version` | |
| cmake / ninja / ccache | 필수 | `cmake --version` | **이름은 `ninja`** (아래 함정) |
| dfu-util | 선택 | `dfu-util -V` | |
| SSH 키 (툴체인 다운로드) | 필수 | `ls ~/.ssh/id_rsa.pub` | 없으면 생성 |

---

## 2. 설치 순서

### 2-1. Homebrew 패키지

```sh
# 설치 전 실제 이름 확인 — brew 는 transaction 이라 하나만 틀리면 전체 롤백된다
brew search ninja
brew install cmake ninja ccache dfu-util
```

> ⚠️ **`ninja-build` 는 존재하지 않는다.** `ninja` 가 정답이다.
> 틀리면 cmake, ccache 도 함께 설치되지 않는다.

### 2-2. SSH 키 (툴체인 다운로드용)

```sh
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ""
```

### 2-3. ESP-IDF

```sh
git clone -b v5.5.5 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3
```

> `--depth 1 --recursive --shallow-submodules` 로 clone 크기를 크게 줄인다.

### 2-4. esptool

```sh
brew install esptool
```

### 2-5. pyserial (로그 수집용)

```sh
pipx install pyserial
```

---

## 3. 파이썬 문제 3연계 (가장 자주 막히는 구간)

### 3-1. `pip: command not found`

macOS 기본 `python3` 는 Xcode 것이라 `pip` **바이너리가 아예 없다**(`pip3` 만 있다).

Homebrew python 의 `pip` 은 **`bin/` 이 아니라 `libexec/bin/`** 에 있다:

```sh
ls /opt/homebrew/opt/python@3.15/libexec/bin/pip
# → /opt/homebrew/opt/python@3.15/libexec/bin/pip

# 실제 경로로 링크 (dangling link 방지)
ln -sf /opt/homebrew/opt/python@3.15/libexec/bin/pip  /opt/homebrew/bin/pip
ln -sf /opt/homebrew/opt/python@3.15/libexec/bin/pip3 /opt/homebrew/bin/pip3
```

### 3-2. 버전 불일치

brew 가 새 python 을 끌어오며 `pip3` 링크를 덮어쓸 수 있다:

```sh
python3 --version && pip --version    # 반드시 대조
```

### 3-3. PEP 668 차단

```
error: externally-managed-environment
```

```sh
pipx install esptool      # 격리 venv
pipx install pyserial
```

> `--break-system-packages` 는 시스템 python 을 망가뜨리므로 쓰지 않는다.

### 3-4. brew 와 pipx 중복

같은 도구를 양쪽에 두면 PATH 우선순위로 버전이 갈린다. **한쪽만 유지한다.**

---

## 4. `export.sh` 대신 wrapper 쓰기

`export.sh` 를 `.zshrc` 에 넣으면 **모든 새 터미널이 시작할 때마다 1.7초씩 느려진다.**

```sh
# ~/.local/bin/idf
#!/bin/sh
export IDF_PATH="$HOME/esp/esp-idf"
exec "$IDF_PATH"/export.sh
```

```sh
chmod +x ~/.local/bin/idf
export PATH="$HOME/.local/bin:$PATH"
```

### export.sh 의 venv 탐색 함정

```
ERROR: Python virtual environment not found for ...idf5.5_py3.9_env/bin/python
```

`tools/detect_python.sh` 가 PATH 의 `python3` 를 첫 순위로 고정 탐색한다.
설치 시점 python(brew 3.15)과 셸의 python(Xcode 3.9)이 다르면 다른 venv 를 찾는다.
로그인 셸에서는 `/opt/homebrew/bin/python3` 가 앞에 오는 것이 정상이다.

> `export.sh` 직후 명령이 안 잡히는 것처럼 보이면 **다음 명령부터** 반영된다.
> 셸을 새로 띄우면 정상 동작한다.

---

## 5. ccache 켜기

`idf.py set-target` 가 기본값 `CCACHE_ENABLE=0` 을 넘겨서 ccache 가 조용히 꺼진다.

```sh
export IDF_CCACHE_ENABLE=1
```

확인은 cmake 실행줄을 직접 읽어야 한다:
```
-- ccache will be used
```

---

## 6. TinyUSB 컴포넌트 벤더링

`idf_component.yml` 에 `espressif/esp_tinyusb: version: "^2.0.1~1"` 을 쓰면
레지스트리에 존재하는 버전인데도 매니저가 못 찾는다(prerelease 해석 실패).

```sh
mkdir -p components
# 레지스트리 API 에서 URL 을 뽑는다
curl -s https://components.espressif.com/api/components/espressif/esp_tinyusb \
  | python3 -c "import json,sys; [print(v['url']) for v in json.load(sys.stdin)['versions'] if v['version']=='2.0.1~1']"

curl -L -o /tmp/esp_tinyusb.zip "<위 URL>"
curl -L -o /tmp/tinyusb.zip    "<upstream tinyusb 0.17.0~2 URL>"

unzip -q /tmp/esp_tinyusb.zip -d components/ && mv components/esp_tinyusb-* components/esp_tinyusb
unzip -q /tmp/tinyusb.zip    -d components/ && mv components/tinyusb-*    components/tinyusb
```

> `esp_tinyusb` 는 upstream `tinyusb` 를 `>=0.17.0~2` 로 요구하므로 **둘 다** 넣어야 한다.

### DWC2 패치 (필수)

```c
// components/tinyusb/src/portable/synopsys/dwc2/dwc2_esp32.h:47
.ep_in_count = 5   →   .ep_in_count = 7
```

이거 안 하면 USB 열거가 **조용히** 실패한다. → [`pitfalls.md` §12](pitfalls.md)

---

## 7. lwIP 직접 다루기 (NCM 모드)

IDF 5.5 의 lwIP API:

| 항목 | 정답 |
|---|---|
| `netif_output_fn` | `netif_output_fn(netif, pbuf, ipaddr)` — 3인자 |
| `netif->input` | `(pbuf, netif)` — 순서 반대 |
| IP 주소 | `netif->ip_addr.u_addr.ip4` |
| IP 타입 | `ip4_addr_t` |
| 문자열 길이 | `IP4ADDR_STRLEN_MAX` |
| 컴포넌트명 | `lwip` (not `esp_lwip`) |
| `esp_timer.h` | 별도 `REQUIRES esp_timer` |

정답 위치:
`~/esp/esp-idf/components/lwip/lwip/src/include/lwip/netif.h` 의
`netif_input_fn` / `netif_output_fn` typedef

---

## 8. 빌드 → 플래시 → 검증

```sh
cd ~/esp/rlcd-workspace/13_USB_CDC
idf set-target esp32s3
idf build
idf -p /dev/tty.usbmodemXXXX flash
```

### ⚠️ 검증 게이트 — 플래시 성공 ≠ 동작 확인

```
Hash of data verified.  ×3
Done.
```

이건 **기록이 성공했다**는 뜻일 뿐이다. **보드가 정상 동작한다는 증거가 아니다.**
USB 펌웨어는 **앱이 실제로 부팅해서 USB 를 다시 열거했는지**까지 봐야 통과다.

```sh
# ① TinyUSB 가 USB PHY 를 점유하므로 전원 완전 차단 후 재투입
#    (케이블 뽑았다가 꽂기)
# ② 재열거 확인
ls /dev/tty.usbmodem*
ioreg -p IOUSB -w 0 | grep -i espressif
#    → "Espressif Device" 가 보여야 정상
# ③ 실제 통신 왕복
./scripts/test-cdc.sh
#    → "성공 10/10" + "✓ 테스트 통과"
```

성공 기준:
- ✅ `AppleUSBCDCCompositeDevice` 등장
- ✅ `PING` → `PONG` 왕복 성공
- ✅ 실측값 출력 (`GET /status` 의 JSON 파싱 성공)
- ✅ `panic` / `abort` / `error` 계열 로그 없음

---

## 9. 사용자 셸에서 검증하기

에이전트 셸과 로그인 셸의 PATH가 다르면 **검증이 거짓말을 한다.**

```sh
env -i HOME=$HOME TERM=xterm PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" \
  /bin/zsh -lc 'idf build'
```

이 형태로 재현해야 사용자가 실제로 타는 셸과 같은 결과가 나온다.

---

## 10. 자주 쓰는 명령

```sh
# 빌드
idf build

# 플래시
idf -p /dev/tty.usbmodemXXXX flash

# 모니터 (UART 콘솔로 돌린 경우)
idf -p /dev/tty.usbmodemXXXX monitor

# 칩 정보
esptool --port /dev/tty.usbmodemXXXX chip-id
esptool --port /dev/tty.usbmodemXXXX flash-id
esptool --port /dev/tty.usbmodemXXXX get-security-info

# 앱 파티션 백업
esptool --port /dev/tty.usbmodemXXXX read-flash 0x10000 0x80000 app_backup.bin

# 빌드 산출물 초기화 (타깃 변경 시)
rm -rf build && idf set-target esp32s3 && idf build
```

---

## 11. 전체 요약 체크리스트

```sh
# 환경
brew --version && cmake --version && ninja --version && ccache --version
python3 --version && pip --version        # 버전 일치 확인
ls /opt/homebrew/bin/pip                   # dangling link 아닌지
cat ~/esp/esp-idf/version.txt
esptool version

# 보드
ls /dev/tty.usbmodem*
esptool --port /dev/tty.usbmodemXXXX chip-id
esptool --port /dev/tty.usbmodemXXXX get-security-info

# 빌드
env -i HOME=$HOME TERM=xterm PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" \
  /bin/zsh -lc 'idf build'
```

---

## 관련 문서

- [`pitfalls.md`](pitfalls.md) — 함정 18가지 상세
- [`usb-communication.md`](usb-communication.md) — USB 통신 방식
- [`session-log.md`](session-log.md) — 실전 기록
