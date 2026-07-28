#!/usr/bin/env python3
"""
RS485 bus sniffer — passive capture on /dev/tty.usbserial-BG01QR02.
Groups bytes into frames using inter-byte gap (> GAP_MS = new frame).
Parses motor protocol: STX(0x02)|ADDR|CMD|NBR_DATA|DATA[N]|END(0x03).
Cannot distinguish TX vs RX direction (single-ended capture).
"""

import serial
import sys
import time
import argparse

GAP_MS   = 15.0   # ms silence → new frame boundary (> FRAMETIMEOUT 8.7 ms)
PORT     = "/dev/tty.usbserial-BG01QR02"
BAUD     = 115200

CMD_NAMES = {
    0x01: "ENABLE",
    0x02: "MOVE_REL",
    0x03: "MOVE_ABS",
    0x04: "STOP",
    0x05: "RESET",
    0x06: "SET_PARAM",
    0x07: "GET_STATUS",
    0x08: "SET_ZERO",
}

def parse_frame(data: bytes) -> str:
    if len(data) == 0:
        return ""
    if data[0] != 0x02:
        return f"(non-STX frame, {len(data)} B)"
    if len(data) < 5:
        return f"INCOMPLETE ({len(data)} B)"
    addr     = data[1]
    cmd      = data[2]
    nbr_data = data[3]
    expected = nbr_data + 5
    cmd_name = CMD_NAMES.get(cmd, f"CMD_0x{cmd:02X}")
    end_ok   = data[-1] == 0x03 if len(data) >= 1 else False
    payload  = data[4:-1] if len(data) >= 5 else b""
    pl_hex   = " ".join(f"{b:02X}" for b in payload)
    status = "OK" if len(data) == expected and end_ok else f"PARTIAL({len(data)}/{expected})"
    return (
        f"ADDR=0x{addr:02X} {cmd_name} nbr_data={nbr_data} "
        f"[{pl_hex}] END={'OK' if end_ok else 'MISSING'} {status}"
    )

ANSI_RESET  = "\033[0m"
ANSI_CYAN   = "\033[36m"
ANSI_YELLOW = "\033[33m"
ANSI_RED    = "\033[31m"
ANSI_GRAY   = "\033[90m"

def color_frame(raw: bytes) -> str:
    if len(raw) == 0:
        return ANSI_GRAY
    if raw[0] != 0x02:
        return ANSI_RED
    nbr_data = raw[3] if len(raw) >= 4 else 0
    expected = nbr_data + 5
    if len(raw) == expected:
        # 9 B frames are likely TX (commands), 6/13 B are likely RX (replies)
        return ANSI_CYAN if len(raw) in (5, 6, 7, 8, 9, 13) else ANSI_YELLOW
    return ANSI_RED

def main():
    parser = argparse.ArgumentParser(description="RS485 passive sniffer")
    parser.add_argument("--port", default=PORT)
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--gap",  type=float, default=GAP_MS,
                        help="Inter-frame gap in ms")
    args = parser.parse_args()

    print(f"Opening {args.port} @ {args.baud} baud  (gap={args.gap} ms)")
    print(f"  Cyan  = well-formed frame")
    print(f"  Red   = malformed / unexpected")
    print(f"  Gray  = non-STX bytes")
    print("─" * 72)

    try:
        ser = serial.Serial(args.port, args.baud,
                            bytesize=8, parity='N', stopbits=1,
                            timeout=args.gap / 1000.0)
    except serial.SerialException as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    frame_buf = bytearray()
    t_start   = time.monotonic()
    frame_idx = 0

    print("Listening… Ctrl-C to stop.\n")

    try:
        while True:
            chunk = ser.read(256)   # returns after timeout if no bytes
            now   = time.monotonic() - t_start

            if chunk:
                frame_buf.extend(chunk)
            else:
                # Timeout expired with no new bytes → frame boundary
                if frame_buf:
                    raw    = bytes(frame_buf)
                    col    = color_frame(raw)
                    ts     = f"[{now:9.3f}s]"
                    hex_s  = " ".join(f"{b:02X}" for b in raw)
                    parsed = parse_frame(raw)
                    frame_idx += 1
                    print(f"{ANSI_GRAY}{ts}{ANSI_RESET} "
                          f"#{frame_idx:04d} {len(raw):3d} B  "
                          f"{col}{hex_s}{ANSI_RESET}")
                    print(f"           {ANSI_GRAY}→ {parsed}{ANSI_RESET}")
                    frame_buf.clear()

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("\nDone.")

if __name__ == "__main__":
    main()
