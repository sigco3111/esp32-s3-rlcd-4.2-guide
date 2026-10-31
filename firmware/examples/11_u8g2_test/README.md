# 11 — U8G2 (1-bit 직접 제어)

**LVGL 없이** U8G2 로 1-bit 화면을 직접 그리는 예제.
이 보드의 화면 제어에 가장 근접한 저수준 예제.

## ★ 왜 U8G2 가 이 보드에 특히 맞는가

RLCD 4.2" 의 패널은 **ST7305** 컨트롤러 + **1-bit** 이다.
U8G2 는 비트맵을 페이지 단위로 밀어넣는 방식이라
1-bit 에 필요한 최소 작업만 한다.

| 항목 | 값 |
|---|---|
| 패널 컨트롤러 | ST7305 |
| SPI 클럭 | 24 MHz |
| 타일 폭 | 38 (바이트) |
| 타일 높이 | 50 (페이지) |
| 버퍼 | `38 × 8` 바이트 = 304 바이트/페이지 |

### SPI 핀

| 신호 | GPIO |
|---|---|
| MOSI | 12 |
| SCLK | 11 |
| DC | 5 |
| CS | 40 |

★ **버퍼 크기 함정이 있다.**
`ST7305_FULL_BUFFER_HEIGHT` 는 50 이지만
**실제 패널 높이(300px)는 50페이지 = 400px** 다.
400px 를 50페이지로 담는 게 맞지만, 버퍼를 페이지 단위로
덮어쓸 때 **높이 50 vs 49** 처럼 한 페이지 차이가 생기면
화면이 1줄 어긋난다. (→ 이 저장소 대시보드 작업에서 실제로 겪음)

## 구조

```
components/
  u8g2/                    U8G2 라이브러리 (외부)
  u8g2_st7305/             ★ ST7305 패널 드라이버 (직접 작성)
  ui_bsp/generated/        GUI Guider 산출물
  port_bsp/                LCD/SPI 초기화
```

★ `u8g2_st7305` 컴포넌트가 이 예제의 핵심 자산이다.
ST7305 의 초기화 시퀀스와 페이지 단위 전송을 담고 있다.

## 빌드

```bash
cd 11_u8g2_test

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
