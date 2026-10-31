# 펌웨어 예제 — 빌드 함정 3가지

> `firmware/examples/` 의 8개 예제를 수집하면서 **실제로 막힌** 3가지다.
> 전부 컴파일 에러가 아니라 **다른 에러 메시지**로 나타난다.

---

## 23. flash size 기본값이 2MB — 파티션 테이블이 안 들어간다

**증상**

```
Partitions tables occupies 3.1MB of flash (3211264 bytes) which does not
fit in configured flash size 2MB. Change the flash size in menuconfig
under the 'Serial Flasher Config' menu.
```

**원인**

ESP-IDF 의 기본 flash size 는 **2MB** 다.
이 보드는 **16MB** 다. 그리고 `partitions.csv` 는
factory + 2개 OTA 슬롯으로 **3.1MB** 다.

★ `sdkconfig.defaults` 에 이 설정이 없었다.

**처방**

```bash
# sdkconfig.defaults 에 추가
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

★ **왜 `sdkconfig` 에는 쓰면 안 되는가**
` sdkconfig` 는 **생성물**이라 커밋하지 않는다(`.gitignore` 에 있음).
그래서 재현하려면 `sdkconfig.defaults` 에 명시해야 한다.

★ **이게 원본 저장소에도 있던 문제다**
waveshare 공식 예제 중 `12_usb_ncm` 과 `13_usb_cdc` 의
`sdkconfig.defaults` 에 flash size 설정이 없다.
그냥 복사하면 재현이 안 된다.

---

## 24. target 을 명시하지 않으면 esp32 로 잡힌다

**증상**

```
ERROR: Component "esp_tinyusb" defined in manifest file
".../components/esp_tinyusb/idf_component.yml"
is not compatible with target "esp32"
```

**원인**

`sdkconfig.defaults` 에 `CONFIG_IDF_TARGET="esp32s3"` 가 **있어도**
IDF 는 처음에 `esp32` 로 target 을 잡는다.
defaults 는 **처음 생성되는 sdkconfig 의 초기값**이기 때문이다.
이미 생성된 sdkconfig 가 있으면 defaults 의 target 은 무시된다.

**처방**

```bash
idf.py set-target esp32s3
```

★ 항상 `set-target` 을 먼저 돌린다.
`rm -rf build sdkconfig` 후 `set-target` 이 가장 확실하다.

---

## 25. `ESP_ROM_ELF_DIR` 이 없으면 컴파일이 죽는다

**증상**

```
OSError: ESP_ROM_ELF_DIR environment variable is not defined.
Please try to run IDF "install" and "export" scripts.
```

이 메시지가 **빌드 중간에** 뜬다.
CMake configure 는 통과하고 컴파일 단계에서 죽는다.

**원인**

`esp_rom/gen_gdbinit.py` 가 `ESP_ROM_ELF_DIR` 환경변수를 요구한다.
IDF 의 `tools.json` 이 이 변수를 정의하고,
`idf export.sh` 가 `idf_tools.py export` 로 설정한다.

★ **직접 만든 `idf` 래퍼 스크립트가 이걸 빠뜨리기 쉽다.**
PATH 에 `esp-rom-elfs` 만 넣고 환경변수를 안 내보내면 정확히 이 에러가 난다.

**처방**

래퍼 스크립트에 추가:

```bash
for d in "$IDF_TOOLS"/tools/esp-rom-elfs/*/; do
    if [ -d "$d" ]; then
        export ESP_ROM_ELF_DIR="$d"
        break
    fi
done
```

★ `idf export.sh` 를 매번 치는 게 귀찮아서 wrapper 를 만든 것이라면,
이 변수를 빠뜨리기 쉽다. wrapper 를 만들 때는 `export.sh` 가 내보내는
환경변수를 전부 비교하는 게 안전하다.

---

## 부가 — 외부의 거대한 라이브러리를 통째로 넣지 말 것

**증상**: 저장소가 174MB 가 된다.

**원인**: waveshare 예제들이 각자 외부 라이브러리를 통째로 포함한다.

| 라이브러리 | 크기 | 실제로 필요한 것 |
|---|---|---|
| `ExternLib/SensorLib` | 21MB | SHTC3 접근엔 헤더 3개 (111KB) |
| ├ `bosch/firmware/` | 19MB | BHI260AP 펌웨어 이미지 — **불필요** |
| `u8g2` | 42MB | 이 예제에선 폰트 2개 (8KB) |
| ├ `u8g2_fonts.c` | 38MB | **전체 폰트** — 2개만 필요 |
| `u8g2_d_setup.c` | 449KB | CMakeLists 가 이미 FILTER 로 제외 |
| `ui_bsp/generated/images` | 16MB | 400×300 ALPHA — **필요** (화면 콘텐츠) |

★ `managed_components/` (컴포넌트 매니저가 받아온 것) 는
`.gitignore` 에 있으니 제외된다.
문제는 **프로젝트 안에 직접 복사해둔** 라이브러리다.

**처방**

**① 컴포넌트가 다른 컴포넌트를 `REQUIRES` 하면 반드시 포함해야 한다**

```cmake
# u8g2_st7305/CMakeLists.txt
idf_component_register(
    SRCS "u8g2_st7305.c"
    REQUIRES driver u8g2      # ← 이 줄이 있으면 u8g2 가 필요하다
)
```

`u8g2` 를 빼면:
```
Failed to resolve component 'u8g2' required by component 'u8g2_st7305'
```

★ **"외부 라이브러리라 제외" 는 위험하다.**
헤더만 쓰더라도 컴포넌트 해석 단계에서 필요하다.

**② 실제로 쓰는 것만 남긴다**

```bash
# u8g2_fonts.c 에서 쓰는 폰트만 추출
grep -rE '\bu8g2_font_[a-z0-9_]+' --include='*.c' --include='*.cpp' \
    . --exclude-dir=components/u8g2 | sort -u
```

★ **헤더를 스캔하면 안 된다.**
`u8g2.h` 에 **모든 폰트가 선언**돼 있어서 1521개가 잡힌다.
**예제 자신의 코드만** 스캔해야 정확히 2개가 나온다.

**③ CMakeLists 의 `SRC_DIRS` 를 줄였으면 그 선언도 줄여야 한다**

```cmake
# 원본
set(src_dirs ./src ./src/touch ./src/platform ./src/bosch ...)
# → 파일을 줄였는데 이 선언을 안 줄이면:
#   SRC_DIRS entry './src/touch' does not exist.
```

---

## 부가 — 컴포넌트의 CMakeLists.txt 를 통째로 복사할 것

**증상**

```
Component directory ".../components/ui_bsp" does not contain a CMakeLists.txt file.
No component will be added
...
Failed to resolve component 'ui_bsp' required by component 'user_app'
```

**원인**: 컴포넌트 디렉터리를 복사할 때 `CMakeLists.txt` 를 놓쳤다.
디렉터리만 있어서는 IDF 가 그걸 컴포넌트로 못 본다.

**처방**: 복사할 때 **컴포넌트 디렉터리면 `CMakeLists.txt` 를 반드시 포함**한다.

★ 이건 눈으로 안 보임 — 디렉터리 복사로는 "다 했다" 고 느껴진다.

---

## 검증 스크립트

```bash
./firmware/build-all-examples.sh              # 전체
./firmware/build-all-examples.sh 13_usb_cdc   # 하나만
```

각 예제의 `build/` 에서 `.bin` 이 나오면 성공.
