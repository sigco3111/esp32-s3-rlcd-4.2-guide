#!/usr/bin/env bash
# 저장소 보안 검증 — 시크릿 / 민감정보 노출 검사
#
# 사용법: ./scripts/verify-security.sh [repo_root]
#
# 성공 기준:
#   1) 시크릿 패턴 0 hits
#   2) MAC 전체주소 0 hits (마스킹 버전만 존재)
#   3) U+FFFD (한글 손상) 0 hits
#
# 주의: 이 스크립트 자신의 패턴 문자열은 grep 결과에 나타난다.
# 그래서 패턴을 변수로 조립하고, 결과 판정 시 이 파일은 제외한다.
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$ROOT" || exit 1

echo "▶ 검사 대상: $ROOT"
echo

# ── 패턴을 조립한다 (자기 자신과 부분 문자열 매칭을 피하기 위해) ──
SEC_PAT='gh''p_|gh''o_|github_''pat_|^se''cret_|nt''n_|sk-[A-Za-z0-9]{10}'
MAC_FULL='28:84:85:a8:8d:0''0'
MAC_MASK='28:84:85:xx:xx:xx'

FAIL=0

echo "=== 1) 시크릿 패턴 ==="
HITS=$(grep -rnE "$SEC_PAT" . \
  --exclude-dir=.git --exclude-dir=build --exclude-dir=node_modules \
  --exclude='verify-security.sh' --exclude='*.log' 2>/dev/null)
if [ -z "$HITS" ]; then
  echo "  ✓ 0 hits"
else
  echo "  ✗ 발견됨:"
  echo "$HITS" | head -20 | sed 's/^/    /'
  FAIL=1
fi
echo

echo "=== 2) MAC 주소 ==="
FULL=$(grep -rn "$MAC_FULL" . \
  --exclude-dir=.git --exclude-dir=build --exclude-dir=node_modules \
  --exclude='verify-security.sh' 2>/dev/null)
if [ -z "$FULL" ]; then
  echo "  ✓ 전체주소 0 hits"
else
  echo "  ✗ 전체주소 발견됨:"
  echo "$FULL" | head -10 | sed 's/^/    /'
  FAIL=1
fi
MASK_N=$(grep -rn "$MAC_MASK" . --exclude-dir=.git 2>/dev/null | wc -l | tr -d ' ')
echo "  마스킹 버전 등장 횟수: $MASK_N"
echo

echo "=== 3) 한글 손상 (U+FFFD) ==="
# U+FFFD 문자를 직접 쓰지 않고 유니코드 코드로 조립한다.
# (이 파일 자체가 grep 에 걸리지 않도록)
FFFD=$(python3 -c "import sys; sys.stdout.write(chr(0xFFFD))")
BAD=$(find . -type f \( -name '*.md' -o -name '*.html' -o -name '*.c' -o -name '*.sh' -o -name '*.py' -o -name '*.js' -o -name '*.css' \) \
  -not -path './.git/*' -not -path '*/build/*' -not -name 'verify-security.sh' -print0 2>/dev/null \
  | xargs -0 grep -l "$FFFD" 2>/dev/null)
if [ -z "$BAD" ]; then
  echo "  ✓ U+FFFD 0 hits"
else
  echo "  ✗ 손상된 파일:"
  echo "$BAD" | sed 's/^/    /'
  FAIL=1
fi
echo

echo "=== 4) 대용량 파일 (100MB 초과, 커밋 부적합) ==="
BIG=$(find . -type f -size +100M -not -path './.git/*' 2>/dev/null)
if [ -z "$BIG" ]; then
  echo "  ✓ 없음"
else
  echo "  ✗ 발견됨:"
  echo "$BIG" | sed 's/^/    /'
  FAIL=1
fi
echo

if [ "$FAIL" -eq 0 ]; then
  echo "✓ 보안 검증 통과"
else
  echo "✗ 보안 검증 실패 — 위 항목 확인 필요"
fi
exit $FAIL
