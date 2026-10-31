# ★ waveshare SensorLib — 이 예제에서 실제로 쓰는 부분만 남긴 축소본
#
#   원본은 21MB (bosch 펌웨어 이미지 19MB 포함) 로, 이 보드의 SHTC3 접근에는
#   필요 없는 센서(BHI260AP, BMM150, CST816 등)까지 들어있다.
#
#   실제로 필요한 것:
#     · SensorPCF85063.hpp  — RTC (포맷 변환)
#     · SensorRTC.h         — RTC 추상화
#     · SensorPlatform.hpp  — 플랫폼 추상화
#
#   ★ 원본 CMakeLists.txt 는 SRC_DIRS 로 touch/ bosch/ 등 많은 디렉터리를
#     요구하므로, 파일을 줄이면 그 디렉터리들이 없어서 configure 가 실패한다.
#     → 아래처럼 실제 있는 파일만 선언한다.
idf_component_register(
    INCLUDE_DIRS
        "src"
        "src/REG"
        "src/platform"
        "src/platform/espidf"
    REQUIRES esp_timer esp_driver_gpio driver
)
