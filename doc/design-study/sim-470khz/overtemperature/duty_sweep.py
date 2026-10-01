"""Continuous-carrier duty sweep, original Lite capacitors, 470 kHz, 20 V.

Duty refers to gate-source command midpoints, not actual MOSFET conduction.
Run with --verify for longer, finer runs at selected settings.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess

from sweep import HERE, make_netlist, analyze


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--analyze-only', action='store_true')
    args = parser.parse_args()
    duties = (10, 15, 20, 25, 40, 50, 55) if args.verify else (30, 35, 40, 45, 47.5, 50, 52.5, 55, 60, 65)
    cases = [dict(label=f'D{d:g}', duty=d/100) for d in duties]
    cycles = 800 if args.verify else 400
    stem = 'duty_verify' if args.verify else 'duty'
    if not args.analyze_only:
        net = make_netlist(cases, cycles=cycles, step=5e-9 if args.verify else 10e-9)
        net = net.replace('50% at source midpoints', 'Variable duty at source midpoints')
        net = net.replace('{0.5/f-50n}', '{duty/f-50n}')
        entries = ','.join(f'{i},{row["duty"]}' for i, row in enumerate(cases))
        net = net.replace('.end', f'.param duty=table(Case,{entries})\n.end')
        path = HERE / f'{stem}.cir'
        path.write_text(net, encoding='ascii')
        (HERE / f'{stem}_cases.json').write_text(json.dumps(cases, indent=2)+'\n')
        exe = Path(os.environ['LOCALAPPDATA']) / 'Programs/ADI/LTspice/LTspice.exe'
        subprocess.run([str(exe), '-b', str(path)], check=True, timeout=600,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    rows = analyze(stem, cases, cycles)
    with (HERE / f'{stem}.csv').open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f'{stem}: {len(rows)} cases; max settling change {max(abs(r["settle_change"]) for r in rows):.4g}')
    for r in rows:
        print(f'{r["label"]:6s} {r["tip"]:4s} tip={r["ptip"]:6.2f}W '
              f'input={r["pin"]:6.2f}W Q={r["pq"]:5.2f}W '
              f'Vpk={r["vpeak"]:6.1f}V Von={r["vturn_3V"]:5.1f}V')


if __name__ == '__main__':
    main()
