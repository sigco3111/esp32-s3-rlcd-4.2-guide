#!/usr/bin/env bash
# ESP32-S3 보드 USB 통신 테스트 — CDC-ACM 명령/응답 왕복 검증
#
# 사용법:
#   ./test-cdc.sh                 # 자동 포트 탐색
#   ./test-cdc.sh /dev/tty.usbmodem1234561
#
# 성공 기준:
#   PING → PONG  (연속 10회 중 10회)
#   GET /status → JSON 파싱 성공
set -uo pipefail

PORT="${1:-}"
if [ -z "$PORT" ]; then
  for p in $(ls /dev/tty.usbmodem* 2>/dev/null); do
    if ioreg -p IOUSB -w 0 2>/dev/null | grep -q "Espressif Device"; then PORT="$p"; break; fi
  done
fi
if [ -z "$PORT" ]; then
  echo "✗ CDC 포트를 찾지 못했습니다."
  echo "  발견됨: $(ls /dev/tty.usbmodem* 2>/dev/null | tr '\n' ' ')"
  echo "  보드가 TinyUSB 앱을 돌고 있는지 확인:"
  echo "    ioreg -p IOUSB -w 0 | grep -i espressif"
  exit 1
fi
echo "▶ CDC 포트: $PORT"

PYVENV="$HOME/Library/Application Support/pipx/venvs/pyserial/bin/python"
if [ ! -x "$PYVENV" ]; then
  echo "✗ pyserial venv 없음: $PYVENV"
  echo "  설치: pipx install pyserial"
  exit 1
fi

"$PYVENV" - "$PORT" <<'PY'
import serial, sys, time, json

port = sys.argv[1]
s = serial.Serial(port, 115200, timeout=0.3)
time.sleep(1.5)
s.reset_input_buffer()

def cmd(c, wait=1.2):
    """명령 1개를 보내고 그 응답만 정확히 읽는다.

    주의: 직전 명령의 잔여 응답이 버퍼에 남아 있으면 섞인다.
    보내기 전에 버퍼를 비우고, 응답 첫 줄만 취한다.
    """
    s.reset_input_buffer()
    s.write((c + "\r\n").encode())
    s.flush()
    time.sleep(0.35)
    buf = b""
    t0 = time.time()
    while time.time() - t0 < wait:
        try:
            chunk = s.read(4096)
        except Exception:
            break
        if chunk:
            buf += chunk
            if b"\r\n" in buf or b"\n" in buf:
                break
    # 첫 줄만 취한다 (응답은 항상 한 줄로 끝난다)
    text = buf.decode("utf-8", "replace")
    line = text.split("\n")[0].strip()
    return line

print("\n=== 명령/응답 ===")
for c in ["PING", "GET /status", "GET /temp", "GET /hum", "HELP"]:
    print(f"  {c:12s} -> {cmd(c)!r}")

print("\n=== 안정성 (PING 10회) ===")
ok = 0
for i in range(10):
    r = cmd("PING", wait=0.8)
    if r == "PONG":
        ok += 1
    else:
        print(f"  [{i+1}] 실패: {r!r}")
    time.sleep(0.1)
print(f"  성공 {ok}/10")

print("\n=== 송신 속도 ===")
payload = ("X" * 100 + "\r\n").encode()
t0 = time.time(); n = 0
while time.time() - t0 < 3:
    s.write(payload); n += len(payload)
    time.sleep(0.01)
el = time.time() - t0
print(f"  {n/1024:.1f} KB / {el:.1f}s = {n/1024/el:.1f} KB/s")

s.close()
sys.exit(0 if ok == 10 else 1)
PY
RC=$?
echo
[ $RC -eq 0 ] && echo "✓ 테스트 통과" || echo "✗ 테스트 실패"
exit $RC
