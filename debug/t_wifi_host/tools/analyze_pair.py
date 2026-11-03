"""Summarize attempts; align callback clocks when enhanced timing is available."""
import collections
import csv
import json
from pathlib import Path
import re
import sys
from capture import parse_record

root = Path(sys.argv[1])
rows = [json.loads(x) for x in (root/'timeline.jsonl').read_text(encoding='utf-8').splitlines()]
# Estimate the sniffer clock origin from log creation time, not queued CAP
# arrival. This is approximate host alignment, not hardware synchronization.
offsets = []
for r in rows:
    if r['port'] == 'COM8':
        m = re.search(r'rx_callback_us=(\d+).*output_delay_us=(\d+)', r['line'])
        if m:
            offsets.append(r['host_s'] - sum(map(int, m.groups())) / 1000000)
clock_offset = min(offsets) if offsets else None
power = None
attempts = []
current = None
for r in rows:
    if r['port'] != 'COM11':
        continue
    line = r['line']
    match = re.search(r'requested_qdbm=(\d+) actual_qdbm=(\d+)', line)
    if match:
        power = [int(x) for x in match.groups()]
    if ': connecting to ' in line:
        current = dict(power=power, start=r['host_s'], profile=line.split('t_wifi: ')[1].split(': connecting')[0])
    if current and ('config: FAIL' in line or 'defaults: FAIL' in line or 'config: PASS' in line or 'defaults: PASS' in line):
        current.update(end=r['host_s'], result=line)
        attempts.append(current)
        current = None

frames = []
for r in rows:
    if r['port'] != 'COM8' or not r['line'].startswith('CAP '):
        continue
    parts = r['line'].split()
    try:
        parse_record(r['line'])
        data = bytes.fromhex(parts[4])
        if len(data)<24:
            continue
        fc = int.from_bytes(data[:2], 'little')
        subtype = (fc >> 4) & 15
        typ = (fc >> 2) & 3
        ra, ta = data[4:10].hex(':'), data[10:16].hex(':')
        kind = {0:'ASSOC_REQ',1:'ASSOC_RESP',4:'PROBE_REQ',5:'PROBE_RESP',8:'BEACON',10:'DISASSOC',11:'AUTH',12:'DEAUTH'}.get(subtype, 'MGMT') if typ==0 else 'DATA/EAPOL'
        frames.append(dict(host_s=r['host_s'],sniffer_us=int(parts[1]),kind=kind,ra=ra,ta=ta,seq=int.from_bytes(data[22:24],'little')>>4,retry=(fc>>11)&1,wire_len=int(parts[2]),cap_len=int(parts[3]),body=data[24:30].hex()))
    except (ValueError, IndexError) as exc:
        print('Invalid CAP at host_s=', r['host_s'], str(exc), r['line'][:120], file=sys.stderr)
        continue
for a in attempts:
    # Allow for buffered summary/RAW output after the DUT's result.
    def frame_time(f):
        return f['sniffer_us']/1000000 + clock_offset if clock_offset is not None else f['host_s']
    selected=[f for f in frames if a['start']<=frame_time(f)<=a['end']+0.25]
    a['alignment'] = 'estimated callback clock origin from AUTH output delay' if offsets else 'host receipt time'
    a['frames']=dict(collections.Counter(f['kind']+(' STA->AP' if f['ta']=='38:44:be:c9:d7:d0' else ' AP->STA' if f['ra']=='38:44:be:c9:d7:d0' else '') for f in selected))
(root/'attempts.json').write_text(json.dumps(attempts,ensure_ascii=False,indent=2),encoding='utf-8')
if frames:
    with (root/'frames.csv').open('w',newline='',encoding='utf-8') as output:
        writer=csv.DictWriter(output,fieldnames=frames[0].keys())
        writer.writeheader()
        writer.writerows(frames)
print(json.dumps(attempts,ensure_ascii=False,indent=2))
