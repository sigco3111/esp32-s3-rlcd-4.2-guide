#!/usr/bin/env bash
# 공개 저장소 배포 전 보안 스크럽 스캐너.
#
# 사용법: ./scripts/verify-publication-safety.sh [repo_root]
#
# ★ 검사 대상은 "커밋될 파일" 이다 (git ls-files).
#   디스크 전체를 검사하면 .gitignore 대상(managed_components/ 등)이
#   걸려서 "커밋해도 안전한 소스" 와 "로컬에만 있는 파일" 을 구분 못 한다.
#   → 커밋 대상만 검사하는 게 정확하다.
#
# 검사 항목:
#   1) 시크릿 패턴 (토큰·키·비밀번호)
#   2) 전체 MAC 주소 (개인 식별정보)
#   3) U+FFFD (한글/특수문자 손상)
#   4) 100MB 초과 파일 (커밋 부적합)
#
# ★ 자기참조 회피: 검사 패턴 문자열을 변수로 조립하고 자기 자신은 제외한다.
set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$ROOT" || exit 1

SELF="$(basename "$0")"
echo "▶ 검사 대상: $ROOT"
echo

# ── 커밋 대상 파일 목록 ──
if git rev-parse --git-dir >/dev/null 2>&1; then
  if git ls-files --error-unmatch "$SELF" >/dev/null 2>&1; then
    FILES=$(git ls-files | grep -v "^${SELF}$")
  else
    FILES=$(git ls-files)
  fi
  UNTRACKED=$(git ls-files --others --exclude-standard | grep -v "^${SELF}$")
  SCOPE="git 커밋 대상 (tracked ${FILES:+tracked})"
  if [ -n "$UNTRACKED" ]; then
    SCOPE="$SCOPE + untracked $(echo "$UNTRACKED" | wc -l | tr -d ' ')"
  fi
else
  # git 저장소가 아니면 전체 스캔
  FILES=$(find . -type f -not -path './.git/*' -not -path '*/build/*' \
          -not -path '*/managed_components/*' -not -path '*/node_modules/*' \
          -not -path '*/.venv/*' -not -name "$SELF")
  SCOPE="디스크 전체 (git 저장소 아님)"
fi
echo "▶ 검사 범위: $SCOPE"
echo

# ── 패턴 조립 (자기 참조 방지) ──
SEC_PAT='gh''p_|gh''o_|github_''pat_|^se''cret_|nt''n_|sk-[A-Za-z0-9]{10}'
MAC_PAT='([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'

FAIL=0

check() {
  # $1 = 라벨, $2 = 패턴, $3 = grep 플래그(-E 또는 -F)
  local label="$1" pat="$2" mode="$3"
  local hits=""
  if [ -n "$FILES" ]; then
    # 여러 파일을 grep 으로 — 경로가 공백 있을 수 있어 -z 로 NUL 구분
    hits=$(printf '%s\0' "$FILES" | xargs -0 grep -In${mode} -E "$pat" 2>/dev/null \
           | grep -vE 'xx:xx:xx|XX:XX:XX' | head -20)
  fi
  if [ -z "$hits" ]; then
    echo "  ✓ $label 0 hits"
  else
    echo "  ✗ $label 발견됨:"
    echo "$hits" | sed 's/^/    /'
    FAIL=1
  fi
}

echo "=== 1) 시크릿 패턴 ==="
check "시크릿" "$SEC_PAT" ""
echo

echo "=== 2) MAC 주소 ==="
check "전체주소" "$MAC_PAT" ""
MASK_N=$(printf '%s\0' "$FILES" 2>/dev/null | xargs -0 grep -Il 'xx:xx:xx' 2>/dev/null | wc -l | tr -d ' ')
echo "  마스킹 버전 사용 파일 수: $MASK_N"
echo

echo "=== 3) 한글 손상 (U+FFFD) ==="
BAD=$(printf '%s\0' "$FILES" 2>/dev/null | xargs -0 python3 -c '
import sys
bad = chr(0xFFFD)
for p in sys.argv[1:]:
    try:
        c = open(p, encoding="utf-8").read()
    except Exception:
        continue
    if bad in c:
        print(f"  ✗ {p}: {c.count(bad)}건")
' 2>/dev/null)
if [ -z "$BAD" ]; then
  echo "  ✓ U+FFFD 0 hits"
else
  echo "$BAD"
  FAIL=1
fi
echo

echo "=== 4) 대용량 파일 (100MB 초과) ==="
BIG=""
if [ -n "$FILES" ]; then
  BIG=$(printf '%s\0' "$FILES" | xargs -0 ls -l 2>/dev/null | awk '$5 > 104857600 {print $9}')
fi
if [ -z "$BIG" ]; then
  echo "  ✓ 없음"
else
  echo "  ✗ 발견됨:"
  echo "$BIG" | sed 's/^/    /'
  FAIL=1
fi
echo

if [ "$FAIL" -eq 0 ]; then
  echo "✓ 보안 스크럽 통과"
  exit 0
fi
echo "✗ 보안 스크럽 실패 — 위 항목 확인 필요"
exit 1
