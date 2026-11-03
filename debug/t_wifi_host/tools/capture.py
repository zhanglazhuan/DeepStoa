"""Convert CAP serial records into IEEE 802.11 PCAP (no FCS, link type 105)."""
import argparse
import re
import struct
import sys
import time
from pathlib import Path

RECORD = re.compile(r"(?:^|\s)CAP (\d+) (\d+) (\d+) ([0-9a-fA-F]+)\s*$")


def parse_record(line):
    match = RECORD.search(line)
    if not match:
        if line.startswith("CAP "):
            raise ValueError("malformed CAP record")
        return None
    timestamp, wire_len, cap_len = map(int, match.group(1, 2, 3))
    data = bytes.fromhex(match.group(4))
    if not 0 < cap_len <= wire_len <= 65535 or len(data) != cap_len:
        raise ValueError("invalid captured/original length")
    seconds, micros = divmod(timestamp, 1000000)
    if seconds > 0xFFFFFFFF:
        raise ValueError("timestamp out of PCAP range")
    return struct.pack("<IIII", seconds, micros, cap_len, wire_len) + data


def convert(lines, output):
    output.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 105))
    count = bad = 0
    for line in lines:
        try:
            record = parse_record(line)
        except ValueError as exc:
            bad += 1
            print(f"Skipping CAP record: {exc}", file=sys.stderr)
            continue
        if record is not None:
            output.write(record)
            count += 1
    return count, bad


def serial_lines(port, baud, seconds, log):
    try:
        import serial
    except ImportError:
        raise SystemExit("pyserial is required; use ESP-IDF Python or pip install pyserial")
    # Set control lines before opening to avoid intentionally resetting the listener.
    device = serial.Serial(port=None, baudrate=baud, timeout=0.25)
    device.dtr = False
    device.rts = False
    device.port = port
    device.open()
    pending = bytearray()
    deadline = time.monotonic() + seconds
    try:
        while time.monotonic() < deadline:
            chunk = device.read_until(b"\n", 8192)
            if not chunk:
                continue
            if log:
                log.write(chunk)
                log.flush()
            pending.extend(chunk)
            if pending.endswith(b"\n"):
                yield pending.decode("utf-8", errors="replace").strip()
                pending.clear()
            elif len(pending) > 16384:
                print("Discarding overlong serial line", file=sys.stderr)
                pending.clear()
    except KeyboardInterrupt:
        pass
    finally:
        device.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="Saved UTF-8 serial log")
    source.add_argument("--port", help="Serial port, e.g. COM4; close idf.py monitor first")
    parser.add_argument("--output", type=Path, required=True, help="Output .pcap")
    parser.add_argument("--log", type=Path, help="Also save original serial bytes (serial mode)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=60)
    args = parser.parse_args()
    if args.seconds <= 0 or args.baud <= 0:
        parser.error("--seconds and --baud must be positive")
    if args.input and args.log:
        parser.error("--log applies only to --port")
    paths = [p.resolve() for p in (args.input, args.output, args.log) if p]
    if len(paths) != len(set(paths)):
        parser.error("input, output and log must use different paths")
    from contextlib import ExitStack
    with ExitStack() as stack:
        output = stack.enter_context(args.output.open("wb"))
        if args.input:
            lines = stack.enter_context(args.input.open(encoding="utf-8-sig", errors="replace"))
        else:
            log = stack.enter_context(args.log.open("wb")) if args.log else None
            lines = serial_lines(args.port, args.baud, args.seconds, log)
        count, bad = convert(lines, output)
    print(f"Wrote {count} packets to {args.output}; malformed records={bad}")
    if count == 0:
        print("No packets: enable SNIFFER_RAW, verify channel/MAC and trigger Wi-Fi.", file=sys.stderr)
    return 1 if count == 0 or bad else 0


if __name__ == "__main__":
    sys.exit(main())
