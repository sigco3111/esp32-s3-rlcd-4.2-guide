# 05 — I2C 온습도 센서 (SHTC3)

보드에 **내장**된 SHTC3 온습도 센서를 I2C 로 읽는 예제.

## 하드웨어

| 항목 | 값 |
|---|---|
| 센서 | SHTC3 (온도 + 습도) |
| I2C 주소 | `0x70` |
| SDA | GPIO 13 |
| SCL | GPIO 14 |

## 핵심 코드

```cpp
I2cMasterBus I2cbus(14, 13, 0);      // SCL, SDA, 포트
Shtc3Port *shtc3port = new Shtc3Port(I2cbus);

void Shtc3_LoopTask(void *arg) {
    for(;;) {
        float rh, temp;
        shtc3port->Shtc3_ReadTempHumi(&temp, &rh);
        ESP_LOGW("Shtc3-Example","RH:%.2f%%,Temp:%.2f°", rh, temp);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## CDC 프로토콜에서 쓰이는 곳

`13_usb_cdc` 의 `GET /temp` / `GET /hum` 명령이
**바로 이 센서**에서 값을 읽는다.

```
GET /temp   → 29.75
GET /hum    → 21.03
```

★ I2C master bus handle 은 **포트당 한 번만** acquire 해야 한다.
두 번 acquire 하면 assert 로 죽는다.

## 빌드

```bash
cd 05_i2c_shtc3

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
