# 펌웨어 수집 계획 — esp32-s3-rlcd-4.2-guide

## 목적

이 저장소는 "보드를 처음 켜는 사람"의 안내서였다.
여기에 **실제로 쓸 수 있는 펌웨어 소스**를 모아 "복사해서 바로 쓰는" 단계로 확장한다.

## 수집 원칙

1. **직접 작성한 소스만** — `managed_components/` (컴포넌트 매니저가 받아온 외부 라이브러리)는 제외.
   크기가 100배 차이 난다 (174MB → 수백 KB).
2. **재현 가능** — `sdkconfig.defaults` + `partitions.csv` + `idf_component.yml` 을 같이 넣어야
   같은 결과가 나온다. `sdkconfig` 자체는 생성물이라 제외.
3. **라이선스** — 전부 MIT. 저장소와 동일.
4. **비밀값 금지** — MiniMax API 키, Wi-Fi PSK 등. 보드에 저장하는 게 아니라 Mac에만 있다.
   펌웨어 소스에는 키가 없어야 한다.
5. **한 저장소 = 한 주제** — Xiazhi(대화 AI) 펌웨어는 어제 만든
   `xiaozhi-rlcd-4.2-cdc` 별도 저장소로 분리하는 게 맞다고 판단했다.
   (이 저장소는 "브링업 + 최소 예제" 가 주제)

## 수집 대상

### A. 이 저장소에 들어갈 것 (최소 예제 + 조각)

| 순번 | 예제 | 소스 크기 | 이유 |
|---|---|---|---|
| 01 | `01_WIFI_AP` | 12KB | Wi-Fi AP (2.4GHz 대역 주의) |
| 03 | `03_ADC_Test` | 16KB | ADC — 배터리 전압 측정 |
| 05 | `05_I2C_SHTC3` | 16KB | 온습도 센서 (SHTC3 @ 0x70) |
| 08 | `08_LVGL_V8_Test` | 16KB | LVGL v8 (upstream 기본) |
| 09 | `09_LVGL_V9_Test` | 16KB | LVGL v9 (v8 대비 무엇이 바뀌었나) |
| 11 | `11_U8G2_Test` | 16KB | U8G2 — 1-bit 직접 제어 |
| 12 | `12_USB_NCM` | 20KB | USB 이더넷 NIC |
| 13 | `13_USB_CDC` | 24KB | USB CDC 통신 (기반) |

> `14_USB_CDC_AI` 는 여기서 제외한다.
> MiniMax AI 대화 터미널 원형이고, 완성판이 별도 저장소로 분리된다.
> → 중복 소스 방지

### B. 별도 저장소로 분리할 것

**`xiaozhi-rlcd-4.2-cdc`** — XiaoZhi upstream 기반 RLCD 4.2" USB CDC AI 대화 보드

- 보드 폴더 `main/boards/waveshare-esp32-s3-rlcd-4.2/` (168KB) ← 내가 만든 핵심
- `components/usb_cdc/` ← CDC 프로토콜
- `main/protocols/cdc_protocol.cc` ← Wi-Fi 제거 프로토콜
- `main/display/`, `main/board/` ← Display 베이스에 RecordDashboardTurn 승격
- `tests/` ← 회귀 테스트 5종 (C)
- `PATCH_NOTES.md` ← 21개 Phase 상세
- MiniMax 브리지 (Mac 측 Python) — 별도 `host/` 폴더

## 저장소 구조 (변경 후)

```
esp32-s3-rlcd-4.2-guide/
├── README.md
├── site/                     블로그 (GitHub Pages)
├── docs/
│   ├── pitfalls.md                 함정 1-18
│   ├── pitfalls-dashboard.md       함정 19-22
│   ├── build-guide.md
│   ├── usb-communication.md
│   └── session-log.md
├── firmware/
│   ├── usb_cdc_acm/                기존 (자체 작성 최소 CDC-ACM)
│   ├── usb_cdc_ncm/                기존
│   ├── examples/                   ★ NEW — waveshare 원본 최소 예제
│   │   ├── 01_wifi_ap/
│   │   ├── 03_adc_test/
│   │   ├── 05_i2c_shtc3/
│   │   ├── 08_lvgl_v8_test/
│   │   ├── 09_lvgl_v9_test/
│   │   ├── 11_u8g2_test/
│   │   ├── 12_usb_ncm/
│   │   └── 13_usb_cdc/
│   └── README.md              ★ NEW — 어떤 예제를 언제 쓰는지
├── scripts/                   재현 스크립트
└── docs/firmware-index.md     ★ NEW — 펌웨어 카탈로그
```

## 블로그 반영

1. `site/index.html` — "펌웨어 카탈로그" 섹션 추가
2. "진행 순서" 섹션에 "8. 펌웨어 고르기" 추가
3. README — 펌웨어 표 추가

## 검증

- 스크럽 (시크릿 / MAC / U+FFFD / 대용량)
- 각 예제 `idf build` 가능 여부 (환경이 되는 것만 — managed_components 는 받아와야 함)
- 사이트 태그 균형
- 라이브 200
