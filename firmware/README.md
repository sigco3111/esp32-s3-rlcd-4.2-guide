# 펌웨어 모음

이 저장소의 펌웨어는 **두 갈래**다.

| 경로 | 내용 | 용도 |
|---|---|---|
| `examples/` | waveshare 공식 최소 예제 8종 | **기능 하나씩** 배워갈 때 |
| `usb_cdc_acm/` | 자체 작성 CDC-ACM 펌웨어 | 센서 + Self-OTA 포함 |
| `usb_cdc_ncm/` | 자체 작성 CDC-NCM 펌웨어 | 이더넷 NIC |

---

## 예제 8종 — "기능 하나"씩

각 예제는 **이 보드에서 그 기능 하나만** 딱 떼어낸 최소 구현이다.
"이거 어디서 시작해야 하지"에 대한 답이 여기 있다.

| 예제 | 하는 일 | 시작하는 순서 |
|---|---|---|
| [`01_wifi_ap`](examples/01_wifi_ap/) | SoftAP 로 Wi-Fi 제약 우회 | **1순위** — 네트워크부터 |
| [`03_adc_test`](examples/03_adc_test/) | ADC 로 배터리 전압 | 2 |
| [`05_i2c_shtc3`](examples/05_i2c_shtc3/) | I2C 온습도 센서 (SHTC3 @ 0x70) | 3 |
| [`08_lvgl_v8_test`](examples/08_lvgl_v8_test/) | LVGL v8 화면 | 4 |
| [`09_lvgl_v9_test`](examples/09_lvgl_v9_test/) | LVGL v9 화면 (v8 비교용) | 5 |
| [`11_u8g2_test`](examples/11_u8g2_test/) | U8G2 로 1-bit 직접 제어 | 6 |
| [`12_usb_ncm`](examples/12_usb_ncm/) | USB 를 이더넷 NIC 로 | 7 |
| [`13_usb_cdc`](examples/13_usb_cdc/) | USB CDC 직렬 통신 (가장 단순) | **8** |

### 어떤 순서로 하면 되나

```
① 01_wifi_ap      →  "보드가 살아있나?" 확인 (LED 대신 화면으로)
② 05_i2c_shtc3   →  "I2C 버스가 되나?" + 온습도 값 확인
③ 08_lvgl_v8_test →  "화면에 그림이 나오나?"
④ 11_u8g2_test    →  "1-bit 를 직접 다루면 어떤 느낌인가?"
⑤ 13_usb_cdc     →  "Mac 과 통신되나?"
⑥ 03_adc_test    →  "ADC 를 읽으면 되나?"
⑦ 12_usb_ncm     →  "이더넷이 필요해지면"
⑧ 09_lvgl_v9_test→  "LVGL 업그레이드 Considerations"
```

★ **03_adc_test 는 이 보드에서 의미가 제한적이다.**
RLCD 4.2" 는 USB-C 전원만 쓰는 데스크 디스플레이라 **배터리가 없다.**
배터리 게이지를 만들 때 참고만 하자.

---

## ★ 세 번은 만나는 설정 — 복사할 때 놓치기 쉬움

각 예제 README 에 개별적으로 적어뒀지만,
**예제 8개 전부에서 똑같이 필요한 설정**이라 여기에도 모아둔다.

### 1. target 은 반드시 명시

```bash
idf.py set-target esp32s3
```

`sdkconfig.defaults` 에 `CONFIG_IDF_TARGET="esp32s3"` 가 있어도
IDF 는 처음에 `esp32` 로 target 을 잡는다. 그대로 빌드하면:

```
ERROR: Component "esp_tinyusb" ... is not compatible with target "esp32"
```

### 2. flash size 는 반드시 16MB

ESP-IDF 의 기본 flash size 는 **2MB** 다. 이 보드는 **16MB** 다.

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

이게 없으면 `partitions.csv` (3.1MB) 와 충돌한다:

```
Partitions tables occupies 3.1MB of flash which does not fit in
configured flash size 2MB. Change the flash size in menuconfig
under the 'Serial Flasher Config' menu.
```

★ **`sdkconfig` 는 커밋하지 않는다** (생성물이라 `.gitignore` 에 있음).
그래서 `sdkconfig.defaults` 에 명시해둬야 재현된다.

### 3. managed_components 는 받아와야 한다

```bash
idf.py reconfigure        # 컴포넌트 매니저가 의존성을 받아온다
```

`managed_components/` 는 `.gitignore` 에 있어 커밋하지 않는다.
(외부 라이브러리라 수십 MB 다)

---

## 전 예제 한 번에 빌드

```bash
./firmware/build-all-examples.sh              # 전체
./firmware/build-all-examples.sh 13_usb_cdc   # 하나만
```

각 예제의 `build/` 에서 `.bin` 이 나온다.

### ★ 클론 직후에 해야 할 일

```bash
git clone https://github.com/sigco3111/esp32-s3-rlcd-4.2-guide.git
cd esp32-s3-rlcd-4.2-guide/firmware
./build-all-examples.sh
```

★ **`managed_components/` 와 `build/` 는 커밋하지 않는다.**
저장소가 29MB 인 이유다.
컴포넌트 매니저가 `idf build` 중에 자동으로 받아온다
(LVGL 은 예제당 99~201MB).

★ **따라서 첫 빌드는 네트워크가 필요하고 오래 걸린다.**
8개 예제 전체면 수십 분 걸린다.
하나만 먼저 확인하고 싶으면:

```bash
./build-all-examples.sh 13_usb_cdc
```

★ **`idf.py reconfigure` 는 따로 안 돌려도 된다** —
`idf build` 가 컴포넌트 해석부터 같이 하기 때문이다.

---

## ★ v8 과 v9 를 둘 다 둔 이유

[08_lvgl_v8_test](examples/08_lvgl_v8_test/) 와
[09_lvgl_v9_test](examples/09_lvgl_v9_test/) 의
**소스가 거의 같은데** 화면 이미지 포맷이 다르다.

| | v8 | v9 |
|---|---|---|
| 이미지 포맷 | **ALPHA** (8.4MB) | RGB565A8 (2.1MB) |
| 1-bit 화면 적합성 | ★ 높음 | 낮음 |

이 보드는 **400×300 1-bit 반사형 모노크롬**이다.
16비트 RGB 이미지를 그대로 밀어넣으면 회색이 생기고,
1-bit 로 양자화하면서 **안티앨리어싱이 뭉개진다.**

ALPHA 는 **알파만 8비트로** 저장해서
"흰색/검정 + 알파" 구조에 정확히 맞는다.

→ **1-bit 화면용 UI 를 만들 때는 RGB565A8(16비트) 대신 ALPHA(8비트) 가 맞다.**

자세한 근거: [docs/pitfalls-dashboard.md](../docs/pitfalls-dashboard.md#21-1-bit-화면에-upstream-테마폰트를-그대로-쓰면-뭇개진다)

---

## 소스 출처

`examples/` 는 **waveshareteam/ESP32-S3-RLCD-4.2** 공식 저장소
(https://github.com/waveshareteam/ESP32-S3-RLCD-4.2) 의
`examples/esp-idf/` 에서 가져온 것이다. MIT 라이선스.

수집하면서 한 것:

- `managed_components/` (외부 라이브러리) 제외 — 크기 100배 감소
- `sdkconfig` 제외 (생성물) → 대신 `sdkconfig.defaults` 로 재현 가능하게
- `12_usb_ncm` / `13_usb_cdc` 에 **flash size 16MB 설정 추가**
  (원본엔 2MB 로 남아 있어 재현이 안 됐다)
- 각 예제에 README 추가 (무엇을 하는지, 어디서 시작할지)

---

## 더 큰 펌웨어

**AI 음성 대화 대시보드** 펌웨어 (XiaoZhi 기반, USB CDC 로 MiniMax 연동)는
별도 저장소다:

> [`sigco3111/xiaozhi-rlcd-4.2-cdc`](https://github.com/sigco3111/xiaozhi-rlcd-4.2-cdc)
> · [블로그](https://sigco3111.github.io/xiaozhi-rlcd-4.2-cdc/)

400×300 1-bit 화면에 캐릭터 + 말풍선 + 3페이지 대시보드
(요약 / 상세 / 시스템) 를 구현했다.
