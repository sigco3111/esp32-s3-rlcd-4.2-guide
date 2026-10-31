# 08 — LVGL v8

LVGL **8.x** 로 GUI Guider 스타일의 화면을 띄우는 예제.
화면 이미지 2개를 1.5초 간격으로 교차 표시한다.

## ★ v8 vs v9 — 이 보드에서는 v8 이 낫다

이 저장소에 v8 과 v9 예제가 둘 다 있다. **둘 다 넣은 이유**가 있다.

| | v8 | v9 |
|---|---|---|
| GUI Guider 이미지 포맷 | **ALPHA** (8.4MB) | RGB565A8 (2.1MB) |
| 1-bit 화면 적합성 | ★ **높음** | 낮음 |
| 포맷 크기 | 큼 | 작음 |

### 왜 ALPHA 가 1-bit 에 맞는가

이 보드 패널은 **400×300, 1-bit 반사형 모노크롬**이다.
화면을 그대로 16비트 RGB 로 밀어넣으면 회색이 생기고,
1-bit 로 양자화하면서 **안티앨리어싱이 뭉개진다**.

ALPHA 포맷은 **알파(투명도)만 8비트로** 저장한다.
→ 1비트 화면의 "흰색/검정 + 알파" 구조에 정확히 맞는다.

★ 실제로 1-bit 화면용 UI 를 만들 때는
**RGB565A8(16비트) 대신 ALPHA(8비트) 를 쓰는 게 맞다.**
(→ [문서: 1-bit 정합성](../docs/pitfalls-dashboard.md#21-1-bit-화면에-upstream-테마폰트를-그대로-쓰면-뭉개진다))

## 구조

```
main/
  user_config.h            하드웨어 핀맵 (LCD/I2C)
components/
  port_bsp/                LCD/SPI 초기화
  user_app/                앱 로직 (이미지 교차 표시)
  ui_bsp/
    generated/             GUI Guider 산출물 (gui_guider.c, screens, images)
    custom/                커스텀 위젯
```

## 핵심 코드

```cpp
void Lvgl_LoopTask(void *arg) {
    for(;;) {
        lv_obj_clear_flag(init_ui.screen_img_1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(init_ui.screen_img_2, LV_OBJ_FLAG_HIDDEN);
        vTaskDelay(pdMS_TO_TICKS(1500));
        lv_obj_clear_flag(init_ui.screen_img_2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(init_ui.screen_img_1, LV_OBJ_FLAG_HIDDEN);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
```

★ LVGL 태스크는 **코어 1 에 고정**(`xTaskCreatePinnedToCore(..., 1)`) 한다.

## 빌드

```bash
cd 08_lvgl_v8_test

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
