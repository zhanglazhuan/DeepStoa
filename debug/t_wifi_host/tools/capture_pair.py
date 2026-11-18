"""Capture COM8 sniffer and COM11 DUT with a shared host monotonic clock."""
import datetime
import json
from pathlib import Path
import threading
import time
import serial
from esptool.reset import HardReset
from capture import convert

root = Path(__file__).resolve().parents[1] / 'build' / ('capture_19dbm_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
root.mkdir(parents=True)
print(root, flush=True)
start = time.monotonic()
stop = threading.Event()
lock = threading.Lock()
timeline = (root / 'timeline.jsonl').open('w', encoding='utf-8')
ports = {}
finished = threading.Event()
def mark(port, line):
    with lock:
        timeline.write(json.dumps(dict(host_s=round(time.monotonic()-start, 6), utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), port=port, line=line), ensure_ascii=False)+'\n')
        timeline.flush()

def read(port):
    pending = bytearray()
    with (root / (port+'.log')).open('wb') as raw:
        while not stop.is_set():
            chunk = ports[port].read_until(b'\n', 8192)
            if not chunk:
                continue
            raw.write(chunk)
            raw.flush()
            pending.extend(chunk)
            if pending.endswith(b'\n'):
                line = pending.decode('utf-8', errors='replace').strip()
                mark(port, line)
                if port == 'COM11' and 't_wifi: test finished' in line:
                    finished.set()
                if port == 'COM11' and any(s in line for s in ('TX_DIAG', 'target scan', 'FAIL', 'PASS', 'associated', 'test finished')):
                    print(line, flush=True)
                pending.clear()

try:
    for port in ('COM8', 'COM11'):
        device = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        device.dtr = device.rts = False
        device.port = port
        device.open()
        ports[port] = device
    workers = [threading.Thread(target=read, args=(p,)) for p in ports]
    for worker in workers:
        worker.start()
    for port in ('COM8', 'COM11'):
        mark('CONTROL', 'reset '+port)
        HardReset(ports[port], uses_usb=(port == 'COM11'))()
        time.sleep(2 if port == 'COM8' else 0)
    finished_at = None
    while time.monotonic()-start < 90:
        if finished.is_set():
            finished_at = finished_at or time.monotonic()
            if time.monotonic()-finished_at > 7:
                break
        time.sleep(0.2)
finally:
    stop.set()
    for worker in locals().get('workers', []):
        worker.join()
    for device in ports.values():
        device.close()
    timeline.close()
with (root/'COM8.log').open(encoding='utf-8', errors='replace') as source, (root/'sniffer.pcap').open('wb') as target:
    count, bad = convert(source, target)
print(f'PCAP frames={count} malformed={bad}', flush=True)
