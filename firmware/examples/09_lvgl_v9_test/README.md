# 09 — LVGL v9

LVGL **9.x** 로 같은 화면을 띄우는 예제.
[08_lvgl_v8_test](08_lvgl_v8_test/) 와 비교용.

## v8 → v9 에서 바뀐 것 (이 예제 기준)

| 항목 | v8 | v9 |
|---|---|---|
| GUI Guider 이미지 | ALPHA (8.4MB) | RGB565A8 (2.1MB) |
| `lv_obj_clear_flag` | 동일 | 동일 |
| 태스크 고정 | `xTaskCreatePinnedToCore(core 1)` | 동일 |

**API 자체는 거의 그대로**다. 예제 코드(`user_app.cpp`)는 두 버전이
거의 동일한데, 그게 오히려 좋은 기준점이 된다 —
"LVGL 버전만 바꿔도 되는지" 비교하기 좋아.

## ★ 이 보드에서는 v8 이 유리하다

1-bit 반사형 화면이라 **회색(안티앨리어싱)이 그대로 뭉개진다.**
v9 의 RGB565A8 이미지는 16비트 색을 담고 있어서
1-bit 로 밀어넣으면 더 손해를 본다.

→ v8 의 ALPHA 포맷이 구조적으로 맞는다.
자세한 근거는 [문서: 1-bit 정합성](../docs/pitfalls-dashboard.md#21-1-bit-화면에-upstream-테마폰트를-그대로-쓰면-뭉개진다)

## 같은 내용

이미지 교차 표시, 1.5초 간격. [08](../08_lvgl_v8_test/) 참고.

## 빌드

```bash
cd 09_lvgl_v9_test

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
