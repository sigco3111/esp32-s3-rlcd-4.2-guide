#!/usr/bin/env bash
# firmware/examples 전 예제 빌드 검증
#
# ★ 왜 이 스크립트가 필요한가
#   예제들은 sdkconfig.defaults 만 커밋되어 있다. (sdkconfig 는 생성물)
#   그런데 ESP-IDF 의 기본 flash size 는 2MB 라
#   partitions.csv (3.1MB) 가 "does not fit in configured flash size 2MB" 로
#   빌드가 막힌다. → 16MB 로 명시해야 재현된다.
#
# ★ 또 하나: target 도 명시해야 한다
#   sdkconfig.defaults 에 CONFIG_IDF_TARGET="esp32s3" 가 있어도
#   IDF 는 처음에 esp32 로 target 을 잡는다. → set-target 이 필요하다.
#
# 사용법:
#   ./build-all-examples.sh          # 전체 빌드
#   ./build-all-examples.sh 13_usb_cdc  # 하나만
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 이 스크립트는 firmware/ 에 있고, 예제는 firmware/examples/ 에 있다.
EXAMPLES_DIR="${SCRIPT_DIR}/examples"

IDF="${IDF:-${HOME}/.local/bin/idf}"

if [[ ! -x "${IDF}" ]]; then
  echo "오류: idf 를 찾을 수 없습니다. IDF=<경로> 로 지정하세요."
  exit 1
fi

TARGETS=()
if [[ $# -gt 0 ]]; then
  TARGETS=("$@")
else
  while IFS= read -r d; do
    [[ -n "$d" ]] && TARGETS+=("$d")
  done < <(cd "${EXAMPLES_DIR}" && ls -d */ 2>/dev/null | sed 's#/$##')
fi

PASS=0
FAIL=0
FAILED_LIST=()

for name in "${TARGETS[@]}"; do
  dir="${EXAMPLES_DIR}/${name}"
  if [[ ! -d "$dir" ]]; then
    echo "── ${name}: 디렉터리 없음 — 스킵"
    continue
  fi

  echo "── ${name} 빌드 중..."
  cd "$dir" || { FAIL=$((FAIL+1)); FAILED_LIST+=("$name"); continue; }

  # build/ 와 sdkconfig 는 생성물이라 항상 새로 시작
  rm -rf build sdkconfig

  if ! "${IDF}" set-target esp32s3 >/tmp/bf_set_${name}.log 2>&1; then
    echo "   ✗ set-target 실패"
    tail -5 /tmp/bf_set_${name}.log | sed 's/^/     /'
    FAIL=$((FAIL+1)); FAILED_LIST+=("$name")
    continue
  fi

  if "${IDF}" build >/tmp/bf_build_${name}.log 2>&1; then
    BIN=$(find build -maxdepth 1 -name "*.bin" ! -name "bootloader*" ! -name "partition*" ! -name "ota_data*" | head -1)
    if [[ -n "$BIN" ]]; then
      SZ=$(wc -c < "$BIN" | tr -d ' ')
      echo "   ✓ 성공 — $(basename "$BIN") ${SZ} bytes"
    else
      echo "   ✓ 성공 (bin 확인 안 됨)"
    fi
    PASS=$((PASS+1))
  else
    echo "   ✗ 빌드 실패"
    grep -E "error:|Error|does not fit|FAILED" /tmp/bf_build_${name}.log | head -5 | sed 's/^/     /'
    FAIL=$((FAIL+1)); FAILED_LIST+=("$name")
  fi
done

echo
echo "════════════════════════════════════════"
echo "  성공 ${PASS}  /  실패 ${FAIL}"
if [[ ${#FAILED_LIST[@]} -gt 0 ]]; then
  echo "  실패한 예제: ${FAILED_LIST[*]}"
  echo "  로그: /tmp/bf_build_<이름>.log"
fi
echo "════════════════════════════════════════"

[[ ${FAIL} -eq 0 ]]
