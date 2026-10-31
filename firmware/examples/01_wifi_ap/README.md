# 01 — Wi-Fi SoftAP

ESP32-S3 를 **SoftAP(접근점)** 로 띄우는 가장 작은 예제.
다른 기기가 보드에 직접 연결할 때 쓴다.

## 왜 필요한가

이 보드는 **2.4GHz 전용**이다. 5GHz 공유기에는 절대 붙지 않는다.
(→ [문서: 2.4GHz 제약](../docs/pitfalls.md#9-country-code-미설정--1213채널-ap-가-막힌다))

SoftAP 는 상대 AP 가 필요 없으므로 **대역 제약을 우회**하는 가장 빠른 방법이다.
보드가 자체적으로 AP 를 만들고, 그에 붙어서 설정값을 넣는다.

## 핵심 설정 (`sdkconfig.defaults`)

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

## 참고

- `CONFIG_LOG_DEFAULT_LEVEL_ERROR=y` 로 되어 있어 로그가 거의 안 나온다.
  디버깅하려면 `menuconfig` 에서 `Log output default level` 을 `Info` 로 올린다.
- AP 이름/비밀번호는 `main/softap_example_main.c` 에 있다.

## 빌드

```bash
cd 01_wifi_ap

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
