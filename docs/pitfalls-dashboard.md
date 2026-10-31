# 함정 19–21 — 대시보드/화면 계층 (2026-10-31 추가)

> 앞의 18개는 "보드를 켜는 법" 이었다.
> 19–21 은 **"화면에 뭔가를 띄우는 법"** 이다.
>
> 셋 다 **컴파일 에러를 내지 않는다.** 조용히 죽는 게 함정이다.

---

## 19. 정의만 있고 호출이 없는 함수는 "화면이 안 바뀐다"가 된다

**증상**

대시보드에 3페이지(요약/상세/시스템)를 만들었다.
BOOT 짧게 누르면 **아무것도 바뀌지 않는다.** `page_` 변수 값만 바뀌고 화면은 그대로.

**원인**

```cpp
void RlcdDashboard::Create() {
    BuildHeader();
    BuildGaugePanel();
    BuildChartPanel();
    BuildBars();
    BuildFooter();
    // ← BuildDetailPage() 없음!
    // ← BuildSystemPage() 없음!
}
```

`BuildDetailPage()` 는 **정의만 있고 호출이 한 번도 없었다.**
(첫 커밋부터 계속 이 상태였다)

그러면 이렇게 된다:

```
detail_root_ == nullptr
  → ApplyPageVisibility() 의 첫 줄 if (detail_root_ == nullptr) return;
    → 아무것도 안 바꾸고 끝
      → TogglePage() 는 page_ 만 바꿈
        → 화면은 P1 에서 P1
```

★ **null 가드가 "안전장치" 처럼 보이지만 실제로는 기능을 죽인다.**
`ApplyPageVisibility()` 는 뒤에서 모든 위젯에 이미 null 체크를 하고 있었다.
앞의 조기 반환은 그 위에서 **전체 기능을 꺼버렸다.**

**처방**

```cpp
    BuildHeader();
    BuildGaugePanel();
    BuildChartPanel();
    BuildBars();
    BuildFooter();
    BuildDetailPage();   // ★ 빠지면 페이지 전환이 통째로 죽는다
    BuildSystemPage();   // ★
```

그리고 조기 반환을 지운다:

```cpp
void RlcdDashboard::ApplyPageVisibility()
{
    // null 이라도 각 위젯을 개별적으로 null 체크하므로 여기서 return 하지 않는다
    if (page_ == DashboardPage::System) { ... }
```

★ **눈으로 확인할 방법**

```bash
# 정의만 있고 호출 안 된 함수는 ELF 에 심볼이 남지 않는다
nm build/xiaozhi.elf | grep -c BuildDetailPage      # → 0 이면 호출 없음
strings build/xiaozhi.elf | grep -cE "LATENCY DIST|CUMULATIVE"  # → 0 이면 문자열도 없음
```

`strings` 로_UI 문자열까지 확인하면 더 확실하다.
화면에 나올 문자가 아예 바이너리에 없으면 그 화면은 절대 안 나온다.

★ **재발 방지 — 정의/호출 자동 대조 테스트**

```python
defined = set(re.findall(r'void RlcdDashboard::(Build\w+)\(\)', src))
called  = set(re.findall(r'\b(Build\w+)\(\);', src))
missing = defined - called
assert not missing, f"미호출: {missing}"
```

이걸 레이아웃 테스트에 넣어두면 같은 실수를 못 한다.

---

## 20. `#if CONFIG_...` 로 감싼 코드는 컴파일 에러 없이 사라진다

**증상**

PC 쪽 브리지가 `{"type":"metrics", ...}` JSON 을 USB CDC 로 보내는데,
보드 화면의 대시보드 통계가 **항상 0** 이다.
감정도 계속 "neutral" 이고, 턴수도 늘지 않는다.

**원인**

`application.cc` 의 metrics 처리부가 이렇게 되어 있었다:

```cpp
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
} else if (strcmp(type->valuestring, "metrics") == 0) {
    // 대시보드 통계 처리
    ...
}
#endif
```

이 프로젝트는 **Wi-Fi/MQTT 없이 USB CDC 만 쓴다.**
`CONFIG_RECEIVE_CUSTOM_MESSAGE` 는 MQTT 서버로부터 커스텀 메시지를 받는 옵션이라
**꺼져 있다.**

→ `#if` 안의 코드가 **컴파일 단계에서 통째로 삭제된다.**
→ 컴파일 에러 0건, 경고 0건, 바이너리에 `metrics` 문자열도 없음.

★ 게다가 이 경로는 **4중으로 막혀 있었다.**

| # | 지점 | 문제 |
|---|---|---|
| ① | `main/application.cc` | metrics 블록이 `#if CONFIG_RECEIVE_CUSTOM_MESSAGE` 안 → 컴파일 제외 |
| ② | `main/display/display.h` | `RecordDashboardTurn` 가 `LcdDisplay` 레벨에만 있음. `Display` 베이스엔 없어서 `disp->RecordDashboardTurn()` 이 **컴파일 자체가 불가능** |
| ③ | `components/usb_cdc/cdc_aware_display.cc` | `CdcAwareDisplay` 가 위임 안 함. RLCD 화면을 감싸고 있는 게 이 클래스 |
| ④ | `main/protocols/cdc_protocol.cc` | CDC 가 metrics 를 별도 처리 안 함 |

★ **② 는 compile error 가 났어야 한다** — 하지만 그 코드가 ① 에 의해
애초에 컴파일 대상이 아니었으므로 에러가 안 났다.
**첫 번째 막힘이 나머지 세 개를 숨겼다.**

**처방**

① — `#if` 밖으로:

```cpp
        } else if (strcmp(type->valuestring, "metrics") == 0) {
            // ★ CONFIG_RECEIVE_CUSTOM_MESSAGE 로 감싸면 안 된다.
            //   이 프로젝트는 Wi-Fi/MQTT 없이 USB CDC 만 쓰기 때문에
            //   그 옵션이 꺼져 있고, 켜 두면 MQTT 관련 코드가 함께 컴파일된다.
            ...
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            ...
#endif
```

② — `Display` 베이스로 승격 (파생클래스에만 두면 `Display*` 로 못 부른다):

```cpp
// main/display/display.h
    virtual void RecordDashboardTurn(const char* emotion, uint32_t asr_ms,
                                     uint32_t llm_ms, uint32_t tts_ms) {
        (void)emotion; (void)asr_ms; (void)llm_ms; (void)tts_ms;
    }
```

③ — 래퍼는 내부로 위임:

```cpp
void CdcAwareDisplay::RecordDashboardTurn(const char* emotion, uint32_t asr_ms,
                                           uint32_t llm_ms, uint32_t tts_ms)
{
    if (inner_ != nullptr) {
        inner_->RecordDashboardTurn(emotion, asr_ms, llm_ms, tts_ms);
    }
}
```

★ **검증 — 컴파일됐는지 확인**

```bash
strings build/esp-idf/main/CMakeFiles/__idf_main.dir/application.cc.obj \
  | grep -xE "metrics"
# → "metrics" 가 나오면 컴파일된 것이다. 안 나오면 통째로 제외된 것.
```

★ **일반 원칙**

`#if CONFIG_*` 로 감싼 코드는 **"기능이 켜져 있을 때만 존재"** 하는 코드다.
그 설정이 꺼진 빌드에서 그 기능이 필요하면, 감싸지 말고
**해당 설정과 무관한 경로**로 빼야 한다.

---

## 21. 1-bit 화면에 upstream 테마/폰트를 그대로 쓰면 뭉개진다

**증상**

XiaoZhi upstream 의 대시보드 UI 를 1-bit 반사형 화면(400×300)에 올렸다.
화면은 나오는데 **글자가 뭉개지고 말풍선이 배경에 묻힌다.**

**원인**

1-bit 에는 회색이 없다. **색이 2개(흰/검정)뿐**이므로
upstream 의 16비트 색상 설계가 전부 깨진다.

| upstream | 1-bit 결과 |
|---|---|
| light 테마: 배경 `FFFFFF`, 말풍선 `DDDDDD` | **둘 다 흰색으로 양자화** → 말풍선이 사라짐 |
| dark 테마: 배경 `000000`, 말풍선 `222222` | **둘 다 검정으로 양자화** → 말풍선이 사라짐 |
| `bg_opa = 70%` (반투명) | 1-bit 에는 투명도 개념이 없음 → 의미 없음 |
| `radius = 8` | 1-bit 에서 모서리 둥글기는 노이즈 |
| `border_width = 0` | 경계선 없음 → 말풍선 구분 불가 |
| 4bpp 폰트 | 안티앨리어싱 회색이 흰/검정으로 양자화 → 글자 뭉개짐 |

**처방**

**① 테마를 1-bit 전용으로 새로 만든다** — 색 대신 **구조**로 구분:

```cpp
// assistant/system 말풍선: 흰 배경 + 검정 테두리
// user 말풍선:            검정 배경 + 흰 글자 (역색)
// 반투명 없음, radius 0, 테두리 1px
```

**② 폰트를 1bpp 로 교체**

```cmake
set(BUILTIN_TEXT_FONT font_puhui_basic_14_1)   # 218KB  (4bpp 16_4 는 511KB)
set(BUILTIN_ICON_FONT font_awesome_14_1)      #  39KB  (4bpp 119KB)
```

바인aries **46KB 절감** + 가독성 확보.

**③ 1-bit 양자화 회귀 테스트**

```python
# upstream 테마의 모든 색을 1-bit 로 양자화해
# "배경과 말풍선이 같아지는가" 를 자동 검사
assert quantize(0xDDDDDD) != quantize(0xFFFFFF) or use_inverted_style
```

★ **1-bit 에서 화면을 좋게 만드는 수단은 색이 아니라 구조다**

- 역색(검정 배경 + 흰 글자)
- 굵은 선 (최소 2px)
- 개방형 프레임 (선 대신 채움)
- 도형 (호/도넛/세그먼트)
- 여백
- 애니메이션

세그먼트 막대(칸 단위 채움)는 1-bit 에서 특히 강력하다.
"몇 칸이 채워졌는지" 가 색 대비보다 정확한 정보를 준다.

---

## 부가 — ESP-IDF 에 CPU 사용률 API 가 없다

**증상**: "칩 CPU 사용률" 을 대시보드에 넣고 싶었다.
`esp_cpu_get_usage()`, `esp_get_cpu_usage()`, `esp_get_idle_time()` — 전부 없다.

**실제 API**

ESP-IDF v5.5.5 에 범용 CPU 사용률 함수는 없다.
대신 FreeRTOS 런타임 통계를 쓴다:

```c
// idle 태스크가 소비한 시간 카운터
configRUN_TIME_COUNTER_TYPE idle = ulTaskGetIdleRunTimeCounter();
// 부하(%) = 100 - (idleΔ / 경과Δ) × 100
```

필요한 설정 (이 sdkconfig 에는 이미 켜져 있다):

```
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
```

★ **가장 중요: idle 카운터 단위는 μs 다**

```c
#define portGET_RUN_TIME_COUNTER_VALUE()  esp_timer_get_time()   // μs
```

ms 로 비교하면 **1000배 오차**가 난다. 부하가 항상 0 으로 나온다.

★ **U32 카운터는 ~71.6분 후 wrap 된다**

```
CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y
```

장시간 돌리려면 반드시 U64.

★ **한 번만 읽으면 순간값이라 흔들린다.** 이전 샘플을 보관하고 차이를 내야 한다.

---

## 부가 — `idf_component_register` 에 `REQUIRES` 를 추가하면 자동 의존성이 사라진다

**증상**: 칩 온도 센서(`esp_driver_tsens`)를 쓰려고
`idf_component_register` 에 `REQUIRES esp_driver_tsens` 를 추가했다.

```
FAILED: opus.h: No such file or directory
```

**원인**: 컴포넌트 매니저가 `idf_component.yml` 기반으로 **자동으로 넣는 의존성**
(opus 등)이 통째로 사라진다. `REQUIRES` 를 명시하면 그걸 대체하기 때문이다.

`PRIV_REQUIRES` 로 바꿔도 같다. `target_link_libraries` 로 우회하면
컴포넌트명이 `-lesp_driver_tsens` 라는 잘못된 플래그가 된다.

**처방**: 이 프로젝트는 `esp_driver_tsens` 가 `driver` 상위 컴포넌트에
포함돼 있어 **CMakeLists 를 건드리지 않아도** include/링크가 된다.
(링크 라인에 `libesp_driver_tsens.a` 가 이미 있다)

```bash
grep -o "libesp_driver_tsens.a" build/esp-idf/main/CMakeFiles/__idf_main.dir/link.txt
nm build/xiaozhi.elf | grep temperature_sensor_get_celsius
```

둘 다 확인되면 의존성이 만족된 것이다.

★ **일반 원칙**: `REQUIRES`/`PRIV_REQUIRES` 를 추가하기 전에는
**기존 의존성이 뭐였는지** 먼저 확인한다. `git show HEAD:main/CMakeLists.txt` 로
원본이 비어 있었는지 보면 된다.

---

## 부가 — 새 `.cc` 파일을 추가하면 CMake 를 재구성해야 한다

**증상**: 새 `.cc` 를 만들고 호출했는데 `undefined reference`.

**원인**: `main/CMakeLists.txt` 의 `file(GLOB ...)` 에 `CONFIGURE_DEPENDS` 가 없다.
CMake 는 빌드 사이에 glob 결과를 다시 안 읽는다.

**처방**

```bash
rm -f build/CMakeCache.txt && idf build
```

★ 영구히 고치려면:

```cmake
file(GLOB BOARD_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
```

★ **보드 폴더에 `CMakeLists.txt` 를 만들지 말 것.** 무시된다.
보드 소스 통합은 `main/CMakeLists.txt` 의 GLOB 가 전부 담당한다.
