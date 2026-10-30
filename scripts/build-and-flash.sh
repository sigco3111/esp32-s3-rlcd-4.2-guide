#!/usr/bin/env bash
# USB CDC-ACM 펌웨어 빌드 + 플래시 + 통신 검증 (원샷)
#
# 사용법:
#   ./build-and-flash.sh              # CDC-ACM 모드
#   ./build-and-flash.sh ncm          # CDC-NCM 모드
#
# 사전조건:
#   · ESP-IDF 설치 완료 (~/esp/esp-idf)
#   · TinyUSB 컴포넌트가 프로젝트 components/ 에 벤더링돼 있을 것
#     (docs/build-guide.md 의 "TinyUSB 컴포넌트 벤더링" 참고)
#
# 주의:
#   TinyUSB 는 USB PHY 를 점유하므로 플래시 후 기존 JTAG 콘솔이 사라진다.
#   CDC 는 앱이 USB 를 다시 열거해야 하므로, 플래시가 끝나면
#   전원을 완전히 껐다 켜는 것(USB 케이블 분리 후 재연결)이 필요하다.
set -uo pipefail

MODE="${1:-cdc}"
WS="${WS:-$HOME/esp/rlcd-workspace}"

case "$MODE" in
  cdc) PROJ="$WS/13_USB_CDC" ;;
  ncm) PROJ="$WS/12_USB_NCM" ;;
  *)   echo "사용법: $0 [cdc|ncm]"; exit 1 ;;
esac

if [ ! -d "$PROJ" ]; then
  echo "✗ 프로젝트 없음: $PROJ"
  exit 1
fi

echo "▶ 프로젝트: $PROJ"
cd "$PROJ"

echo "▶ 빌드"
env -i HOME="$HOME" TERM=xterm \
  PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" \
  /bin/zsh -lc 'idf build' 2>&1 | grep -E "error:|Project build complete|binary size" | head -8

# 포트 결정 — ROM 다운로드 모드가 이미면 그 포트, 아니면 유일한 포트
PORT="${ESP_PORT:-}"
if [ -z "$PORT" ]; then
  PORTS=$(ls /dev/tty.usbmodem* 2>/dev/null)
  if [ "$(echo "$PORTS" | wc -w | tr -d ' ')" = "1" ]; then
    PORT="$PORTS"
  else
    echo "✗ 포트가 여러 개입니다. ESP_PORT 로 지정하세요:"
    echo "$PORTS" | sed 's/^/  /'
    exit 1
  fi
fi
echo "▶ 플래시 포트: $PORT"

echo "▶ 플래시"
env -i HOME="$HOME" TERM=xterm \
  PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" \
  /bin/zsh -lc "idf -p '$PORT' flash" 2>&1 \
  | grep -E "Hash of data verified|^Done|error|Error|Failed"

cat <<'EOF'

────────────────────────────────────────────────────────────
✅ 플래시 완료. 하지만 이게 끝이 아닙니다.

  플래시 성공("Hash of data verified")은 *기록이 성공했다*는 뜻일 뿐입니다.
  앱이 실제로 부팅해서 USB 를 다시 열거했는지까지 확인해야 통과입니다.

다음 단계:
  1) USB 케이블을 완전히 뽑았다가 다시 꽂으세요
     (TinyUSB 가 USB PHY 를 점유하므로 전원 완전 차단 후 재투입이 필요)
  2) 5초 대기
  3) 새 포트 확인:
       ls /dev/tty.usbmodem*
       ioreg -p IOUSB -w 0 | grep -i espressif
     → "Espressif Device" 가 보여야 정상입니다
  4) 통신 검증:
       ./scripts/test-cdc.sh
────────────────────────────────────────────────────────────
EOF
