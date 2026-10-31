# 03 — ADC (배터리 전압)

ADC oneshot 으로 **배터리 전압을 읽는** 예제.
1초마다 로그로 전압을 출력한다.

## 핵심 코드

```cpp
// components/user_app/user_app.cpp
void Adc_LoopTask(void *arg) {
    for(;;) {
        int data;
        float vol = Adc_GetBatteryVoltage(&data);
        ESP_LOGW("Adc-Example","Adc Value:%d,Batt Voltage:%f\n",data,vol);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## 쓰이는 곳

대시보드의 배터리 게이지. `Board::GetBatteryLevel()` 이
이 패턴(ADC oneshot + calibration)으로 전압을 읽고 퍼센트로 환산한다.

> ⚠️ **이 RLCD 4.2" 보드에는 배터리가 없다.** USB-C 전원만 쓰는 데스크 디스플레이다.
> 보드 쪽 `BatterygetVoltage()` 는 ADC_CHANNEL_3 을 읽지만
> 이 보드엔 배터리가 연결돼 있지 않아 값이 의미 없다.
> → 대시보드는 배터리 값을 "0" 이 아니라 **"--"** 로 표시해야 한다.

## 하드웨어

| 항목 | 값 |
|---|---|
| ADC 채널 | `ADC_CHANNEL_3` |
| attenuation | `ADC_ATTEN_DB_12` |
| bitwidth | `ADC_BITWIDTH_12` |

## 빌드

```bash
cd 03_adc_test

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
