#!/usr/bin/env python3
"""ESP32-S3 부팅 로그 수집 — 현재 올라있는 펌웨어 식별 + 에러 요약.

사용법:
    python3 boot-probe.py [PORT]

부팅 로그가 가장 신뢰할 수 있는 펌웨어 식별 방법이다.
Flash 덤프에서 문자열을 뒤지는 방법은 오판한다
(예: WIFI_AP_DEF 같은 상수 문자열 때문에 다른 예제로 오인).

주의:
    ESP32-S3 의 네이티브 USB 리셋(rst:0x15)은 GPIO0 스트랩과 무관하게
    USB 다운로드 경로를 강제한다(boot:0x3). 그래서 이 스크립트의 RTS 토글은
    항상 다운로드 모드로 빠질 수 있다. 그럴 때는 케이블을 뽑았다가 다시 꽂아
    전원을 완전히 차단한 뒤 재투입한다.
"""

import glob
import re
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    print("✗ pyserial 이 없습니다: pipx install pyserial", file=sys.stderr)
    sys.exit(1)


def find_port(arg):
    """CDC 앱 포트를 우선 찾고, 없으면 아무 포트나."""
    if arg:
        return arg
    for p in sorted(glob.glob("/dev/tty.usbmodem*")):
        try:
            out = subprocess.run(
                ["ioreg", "-p", "IOUSB", "-w", "0"],
                capture_output=True, text=True, timeout=15,
            ).stdout
        except Exception:
            out = ""
        if "Espressif Device" in out:
            return p
    ports = sorted(glob.glob("/dev/tty.usbmodem*"))
    return ports[0] if ports else None


def main():
    port = find_port(sys.argv[1] if len(sys.argv) > 1 else None)
    if not port:
        print("✗ 포트를 찾지 못했습니다.", file=sys.stderr)
        return 1

    print(f"▶ 포트: {port}", file=sys.stderr)
    s = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.5)

    # GPIO0 을 HIGH(부트해제) 로 두고 EN 을 짧게 토글해 일반 부팅을 유도.
    try:
        s.setDTR(False)
        time.sleep(0.15)
        s.setRTS(True)
        time.sleep(0.06)
        s.setRTS(False)
    except Exception:
        pass

    buf = b""
    t0 = time.time()
    while time.time() - t0 < 12:
        try:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
        except Exception:
            pass
    s.close()

    text = buf.decode("utf-8", "replace")
    print(text)

    print("\n" + "=" * 62)
    print("요약")
    print("=" * 62)

    def find(pattern, label, default="(없음)"):
        m = re.search(pattern, text)
        print(f"  {label:22s} {m.group(1) if m else default}")

    find(r"rst:0x[0-9a-f]+ \(([^)]+)\)", "리셋 원인")
    find(r"boot:0x[0-9a-f]+ \(([^)]+)\)", "부트 모드")
    find(r"Project name:\s*(\S+)", "프로젝트명")
    find(r"ESP-IDF:\s*([\d.]+)", "ESP-IDF 버전")
    find(r"chip revision:\s*v(\d+)", "칩 리비전")
    find(r"PSRAM:\s*(\d+)", "PSRAM (MB)")
    find(r"free heap:\s*(\d+)", "free heap")

    errors = re.findall(
        r"^.*\b(panic|abort|assert|Guru Meditation|error|Error|ERROR|fail|FAIL)\b.*$",
        text, re.M,
    )
    if errors:
        print(f"\n  ⚠ 에러 라인 {len(errors)}개:")
        for line in errors[:15]:
            print(f"    {line.strip()[:130]}")
    else:
        print("\n  ✓ 에러/패닉 라인 없음")

    if "DOWNLOAD" in text:
        print("\n  ⚠ 보드가 다운로드 모드입니다. 일반 부팅하려면:")
        print("    BOOT 버튼을 놓고 전원을 완전히 껐다 켜세요 (USB 케이블 분리 후 재연결).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
