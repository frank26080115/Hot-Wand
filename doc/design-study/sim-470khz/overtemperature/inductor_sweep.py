"""Screen one-at-a-time winding adjustments using the existing Lite model.

python inductor_sweep.py [--without-c11]
The model changes L only; squeezing also changes parasitics and winding losses.
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
    parser.add_argument("--without-c11", action="store_true")
    parser.add_argument("--analyze-only", action="store_true")
    args = parser.parse_args()
    cases = [dict(label="baseline")] + [
        dict(label=f"L{n}_{percent:+d}%", **{f"scale{n}": 1+percent/100})
        for n in (2, 3, 4, 5) for percent in (-5, 2, 5, 10)]
    if args.without_c11:
        for row in cases:
            row["co"] = -1e-9
    stem = "inductors_no_c11" if args.without_c11 else "inductors"
    if not args.analyze_only:
        net = make_netlist(cases)
        for n, value in [(2, "6.2u"), (3, "16.9u"), (4, "16.9u"), (5, "11.5u")]:
            name = f"scale{n}"
            old = next(line for line in net.splitlines() if line.startswith(f"L{n} "))
            net = net.replace(old, old.replace(value, "{"+value+"*"+name+"}"))
            entries = ",".join(f"{i},{row.get(name, 1)}" for i, row in enumerate(cases))
            net = net.replace(".end", f".param {name}=table(Case,{entries})\n.end")
        path = HERE / f"{stem}.cir"
        path.write_text(net, encoding="ascii")
        (HERE / f"{stem}_cases.json").write_text(json.dumps(cases, indent=2)+"\n")
        exe = Path(os.environ["LOCALAPPDATA"]) / "Programs/ADI/LTspice/LTspice.exe"
        subprocess.run([str(exe), "-b", str(path)], check=True, timeout=600,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    rows = analyze(stem, cases, 400)
    with (HERE / f"{stem}.csv").open("w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"{stem}: {len(rows)} cases; max settling change {max(abs(r['settle_change']) for r in rows):.4g}")
    for row in rows:
        print(f"{row['label']:10s} {row['tip']:4s} tip={row['ptip']:6.2f}W "
              f"input={row['pin']:6.2f}W Q={row['pq']:5.2f}W "
              f"Vpk={row['vpeak']:6.1f}V Von={row['vturn_3V']:5.1f}V")


if __name__ == "__main__":
    main()
