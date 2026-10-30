#!/bin/bash
# USB 통신 펌웨어 플래시 + 실제 통신 검증
#
# 사용법:
#   ./flash-usb-mode.sh cdc   # USB CDC-ACM (안전 — 실패해도 디버깅 포트가 남음)
#   ./flash-usb-mode.sh ncm   # USB CDC-NCM  (이더넷 NIC)
#
# 환경변수:
#   ESP_PORT   포트를 자동 탐색하지 않고 직접 지정
#   IDF_WS     ESP-IDF 워크스페이스 경로 (기본: ~/esp/rlcd-workspace)
#   NO_POWER_PROMPT=1   전원주기 안내를 건너뛰고 자동 대기
#
# 왜 CDC 를 먼저 하냐:
#   CDC-ACM 은 lwIP 를 건드리지 않고 새 직렬 포트 하나만 추가한다.
#   실패해도 그 포트 자체가 디버깅 채널이 남아 원인 추적이 가능하다.
#   NCM 은 lwIP 를 직접 만들어야 하고, 실패하면 조용히 무응답이라 진단이 어렵다.
#
# ★ 중요: 플래시 후 보드는 자동으로 일반 부팅하지 않는다.
#   ESP32-S3 의 USB 칩 리셋은 GPIO0 과 무관하게 USB 다운로드 경로를 강제한다
#   (rst:0x15 ... boot:0x3). 그래서 플래시가 Done 으로 끝나도 보드는 ROM 로더로
#   부팅한 상태이고, CDC/NCM 으로 열거되려면 PWR 로 전원을 완전히 차단했다가
#   다시 넣어야 한다. 이 스크립트가 그 물리 조작을 요청한다.

set -uo pipefail

MODE="${1:-cdc}"
IDF_WS="${IDF_WS:-$HOME/esp/rlcd-workspace}"

case "$MODE" in
  cdc) PROJ="13_USB_CDC" ;;
  ncm) PROJ="12_USB_NCM" ;;
  *)   echo "사용법: $0 [cdc|ncm]"; exit 1 ;;
esac

# ── 1) 포트 탐색 ───────────────────────────────────────────
PORT="${ESP_PORT:-}"
if [ -z "$PORT" ]; then
  PORT=$(ls /dev/tty.usbmodem* /dev/tty.usbserial-* 2>/dev/null | head -1 || true)
fi
if [ -z "$PORT" ]; then
  echo "✗ USB 포트를 찾지 못했습니다."
  echo "  → BOOT(GPIO0)을 누른 채로 케이블을 꽂으세요."
  echo "    (ROM 부트 로더는 항상 열거되므로 여기서 재플래시 가능합니다)"
  exit 1
fi
echo "▶ 대상 포트: $PORT"
echo "▶ 프로젝트:   $PROJ"

# ── 2) 살아있음 확인 ───────────────────────────────────────
echo
echo "▶ 칩 확인"
if ! esptool --port "$PORT" chip-id 2>&1 | grep -qE "MAC:|ESP32"; then
  echo "✗ 칩 응답 없음. BOOT(GPIO0)을 누른 채로 재연결 후 다시 시도하세요."
  exit 1
fi

# ── 3) 플래시 ──────────────────────────────────────────────
cd "$IDF_WS/$PROJ" || { echo "✗ 프로젝트 경로 없음: $IDF_WS/$PROJ"; exit 1; }
echo
echo "▶ 플래시"
env -i HOME="$HOME" TERM=xterm \
  PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" \
  /bin/zsh -lc "idf -p '$PORT' flash" 2>&1 \
  | grep -E "Hash of data verified|^Done|error|Error"

# ── 4) 전원주기 요청 ───────────────────────────────────────
# CDC 의 USB 시리얼 문자열은 sdkconfig 의 CONFIG_TINYUSB_DESC_SERIAL_STRING (기본 "123456")
SERIAL_STR=$(grep -E "^CONFIG_TINYUSB_DESC_SERIAL_STRING=" "$IDF_WS/$PROJ/sdkconfig" 2>/dev/null \
             | head -1 | cut -d'"' -f2)
[ -z "$SERIAL_STR" ] && SERIAL_STR="123456"

echo
echo "▶ 플래시 완료. 이제 일반 부팅으로 전환해야 합니다."
echo "  ESP32-S3 는 USB 리셋만으로 일반 부팅이 되지 않습니다."
echo
echo "  [사용자 조치]"
echo "   1) BOOT 버튼을 놓으세요 (오른쪽 버튼, GPIO0)"
echo "   2) PWR 버튼(가운데)을 짧게 한 번 → 전원 Off"
echo "   3) 3초 대기"
echo "   4) PWR 버튼을 짧게 한 번 → 전원 On"
echo "   5) ★ 그 뒤엔 아무것도 건드리지 말아주세요 (케이블도 뽑지 마세요)"
echo
echo "  (전원 재투입 직후 에이전트가 RTS 를 토글하면 다시 다운로드 모드로 튕깁니다)"

# 전원주기 대기 — CDC 포트가 뜰 때까지 감시
echo
echo "▶ CDC 포트 대기 중..."
FOUND=""
for i in $(seq 1 60); do
  # CDC 포트는 시리얼 문자열로 구별한다 (JTAG/ROM 포트와 번호 체계가 다르다)
  CAND=$(ls /dev/tty.usbmodem* 2>/dev/null | grep "${SERIAL_STR}" | head -1 || true)
  if [ -n "$CAND" ]; then
    FOUND="$CAND"
    echo "  ✓ [$i] CDC 포트 발견: $FOUND"
    break
  fi
  sleep 1
done

if [ -z "$FOUND" ]; then
  echo "⚠ CDC 포트가 안 보입니다."
  echo "  → 합성 디바이스 확인:"
  echo "     ioreg -r -c IOUSBHostDevice -w 0 | grep -c AppleUSBCDCCompositeDevice"
  echo "  → 1 이면 열거는 됐으니 조금 더 기다린 뒤 재확인하세요."
  echo "  → 0 이면 앱이 아직 다운로드 모드에 있습니다."
  echo "     PWR 로 전원을 완전히 껐다 켜고 다시 실행하세요."
  exit 1
fi

echo "✓ CDC 포트: $FOUND"
echo
echo "▶ 실제 통신 테스트"
PYVENV="$HOME/Library/Application Support/pipx/venvs/pyserial/bin/python"
"$PYVENV" - "$FOUND" <<'PY'
import serial, sys, time
s = serial.Serial(sys.argv[1], 115200, timeout=0.3)
time.sleep(1.0)
s.reset_input_buffer()
ok = False
for cmd in ["PING", "GET /status", "GET /temp", "GET /hum", "HELP", "DLMODE"]:
    if cmd == "DLMODE":
        print(f"  {cmd:14s} -> (미리보기 — 이걸 보내면 다운로드 모드로 넘어갑니다)")
        continue
    s.write((cmd + "\r\n").encode())
    time.sleep(0.7)
    try:
        data = s.read(4096)
    except Exception:
        data = b""
    txt = data.decode("utf-8", "replace").strip()
    print(f"  {cmd:14s} -> {txt or '(응답 없음)'}")
    if cmd == "PING" and "PONG" in txt:
        ok = True
s.close()
print()
print("✓ 통신 확인" if ok else "✗ PING 응답 없음 — 아직 통과 아님")
PY
