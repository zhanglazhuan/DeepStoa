"""Repeat the existing two-board capture without changing either image."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--runs', type=int, default=5)
args = parser.parse_args()
if args.runs < 1:
    parser.error('--runs must be positive')
tools = Path(__file__).resolve().parent
for i in range(args.runs):
    print(f'REPEAT {i+1}/{args.runs}', flush=True)
    result = subprocess.run([sys.executable, str(tools/'capture_pair.py')], capture_output=True, text=True, encoding='utf-8', errors='replace')
    print(result.stdout, flush=True)
    print(result.stderr, file=sys.stderr, flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
    root = Path(result.stdout.splitlines()[0])
    subprocess.run([sys.executable, str(tools/'analyze_pair.py'), str(root)], check=True, stdout=subprocess.DEVNULL)
    attempts = json.loads((root/'attempts.json').read_text(encoding='utf-8'))
    high = [a for a in attempts if a['power'] == [76, 76]]
    low = [a for a in attempts if a['power'] == [8, 8]]
    summary = dict(run=i+1, root=str(root), high_attempts=len(high),
                   high_ap_auth_seen=sum(a['frames'].get('AUTH AP->STA', 0)>0 for a in high),
                   high_sta_auth_seen=sum(a['frames'].get('AUTH STA->AP', 0)>0 for a in high),
                   high_ip_pass=sum(': PASS' in a['result'] for a in high),
                   low_attempts=len(low), low_ip_pass=sum(': PASS' in a['result'] for a in low))
    (root/'repeat_summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SUMMARY '+json.dumps(summary), flush=True)
