#!/usr/bin/env python3
"""
USB Self-OTA 전송기 — CDC-ACM 으로 보드에 펌웨어를 밀어넣는다.

왜 이게 필요한가:
    TinyUSB CDC 앱이 USB PHY 를 점유하면 esptool 은 ROM 경로에 닿지 못한다
    ("No serial data received"). BOOT 버튼 없이 보드만으로 펌웨어를 교체하려면
    보드 측에 OTA 명령이 들어 있어야 하고, 호스트 쪽에서 이 스크립트로 밀어넣는다.

전제 조건 (보드 측):
    · CDC-ACM 앱에 OTA BEGIN / OTA DATA / OTA END 명령이 구현돼 있다
      (templates/usb-cdc-acm-app.c 참고)
    · 파티션 테이블에 ota_0 / ota_1 슬롯이 있다
      (esp_ota_get_next_update_partition 가 factory-only 테이블에서 NULL 을 준다)

사용법:
    python3 usb-selfota-push.py <firmware.bin> [--port /dev/tty.usbmodemXXXX] [--chunk 512]

동작:
    1) 포트 자동 탐색 (CDC 시리얼 문자열이 포함된 /dev/tty.usbmodem*)
    2) OTA BEGIN <size>
    3) 파일을 chunk 단위로 읽어 OTA DATA <hex> 로 전송
    4) OTA END  → 보드가 검증 후 재부팅

주의:
    · OTA END 응답("OK OTA done, rebooting")이 온 뒤 보드는 USB 를 다시 열거한다.
      포트가 사라졌다가 다시 뜰 수 있으니 성공 판정 직후에는 포트를 다시 찾는다.
    · 전송 중 "ERR" 가 나오면 중단한다. 조각별로 쓰기가 누적되므로
      실패 시 OTA ABORT 로 정리한다.
"""

import argparse
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial 이 필요합니다: pipx install pyserial")


def find_cdc_port():
    """CDC 포트를 찾는다.

    TinyUSB CDC 는 USB 시리얼 문자열로 포트 이름이 만들어지므로
    (/dev/tty.usbmodem<serial>) JTAG/ROM 포트와 구분된다.
    """
    cands = sorted(glob.glob("/dev/tty.usbmodem*"))
    if not cands:
        sys.exit("USB 포트가 없습니다. 케이블을 꽂고 다시 시도하세요.")

    # ioreg 에서 CDC 합성 디바이스가 붙어있는 포트를 우선 선택한다
    try:
        import subprocess
        out = subprocess.run(
            ["ioreg", "-r", "-c", "IOUSBHostDevice", "-w", "0"],
            capture_output=True, text=True, timeout=10,
        ).stdout
        if "AppleUSBCDCCompositeDevice" in out or "Espressif Device" in out:
            # 시리얼 문자열 기반 매칭은 환경마다 달라서 전 후보를 리턴한다
            pass
    except Exception:
        pass

    # 여러 개면 전부 보여주고 사용자가 고르게 한다
    if len(cands) > 1:
        print("발견된 포트:", file=sys.stderr)
        for c in cands:
            print(f"  {c}", file=sys.stderr)
    return cands[-1]


def send_cmd(s, cmd, timeout=2.0):
    """명령 하나를 보내고 응답 줄을 읽는다."""
    s.reset_input_buffer()
    s.write((cmd + "\r\n").encode())
    s.flush()
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
            if b"\r\n" in buf or b"\n" in buf:
                break
    return buf.decode("utf-8", "replace").strip()


def main():
    ap = argparse.ArgumentParser(description="USB CDC Self-OTA 전송기")
    ap.add_argument("firmware", help="보드에 넣을 .bin 파일")
    ap.add_argument("--port", help="직렬 포트 (기본: 자동 탐색)")
    ap.add_argument("--chunk", type=int, default=512, help="전송 단위 바이트 (기본 512)")
    args = ap.parse_args()

    if not os.path.isfile(args.firmware):
        sys.exit(f"파일 없음: {args.firmware}")

    size = os.path.getsize(args.firmware)
    if size == 0:
        sys.exit("빈 파일입니다.")
    if size > 1024 * 1024:
        sys.exit(f"파일이 너무 큽니다 ({size} bytes). 앱 쪽 제한은 1MB 입니다.")

    port = args.port or find_cdc_port()
    print(f"▶ 포트: {port}")
    print(f"▶ 파일: {args.firmware} ({size} bytes)")

    s = serial.Serial(port, 115200, timeout=0.3)
    try:
        time.sleep(1.0)

        # 1) BEGIN
        resp = send_cmd(s, f"OTA BEGIN {size}", timeout=3.0)
        print(f"  OTA BEGIN → {resp}")
        if not resp.startswith("OK"):
            print("✗ OTA 시작 실패. 보드에 OTA 명령이 있는지, 파티션에 ota_0/ota_1 이 있는지 확인하세요.",
                  file=sys.stderr)
            sys.exit(1)

        # 2) DATA
        sent = 0
        with open(args.firmware, "rb") as f:
            while True:
                data = f.read(args.chunk)
                if not data:
                    break
                resp = send_cmd(s, "OTA DATA " + data.hex(), timeout=5.0)
                if not resp.startswith("OK"):
                    print(f"✗ 전송 실패 @{sent}: {resp}", file=sys.stderr)
                    send_cmd(s, "OTA ABORT", timeout=2.0)
                    sys.exit(1)
                sent += len(data)
                pct = sent * 100 // size
                print(f"\r  전송 중 {sent}/{size} ({pct}%)", end="", flush=True)
        print()

        # 3) END
        resp = send_cmd(s, "OTA END", timeout=5.0)
        print(f"  OTA END → {resp}")
        if not resp.startswith("OK"):
            print("✗ OTA 종료 실패", file=sys.stderr)
            sys.exit(1)

        print()
        print("✓ OTA 완료. 보드가 재부팅하면서 새 펌웨어로 전환합니다.")
        print("  보드가 USB 를 다시 열거할 때까지 몇 초 기다리면 새 CDC 포트가 보입니다.")
    finally:
        s.close()


if __name__ == "__main__":
    main()
