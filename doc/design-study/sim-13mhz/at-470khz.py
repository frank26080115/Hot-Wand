"""Screen the *unmodified* 13.56 MHz output network at 467.6 kHz.

Direct 12 V drive of Q1 is assumed. The load states are inherited STP-CN04
470 kHz estimates, not measurements of a K75 cartridge. This is a warning
screen, not a design or a hardware prediction.
"""
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'sim-470khz' / 'overtemperature'))
from sweep import make_netlist, read_raw  # noqa: E402


def main():
    cases = [dict(label='original-13-network', f=27.12e6/58)]
    net = make_netlist(cases, cycles=400)
    changes = {
        'L1 supply drain 33u': 'L1 supply drain 9u',
        'M1 drain gate 0 0 FDP18N20F': 'M1 drain gate 0 0 STP19NF20_EST',
        'C1 drain 0 {15n+cd}': 'C1 drain 0 1f',
        'C2to5 drain m1 {81n+cs}': 'C2to5 drain m1 400n',
        'L2 m1 m2 6.2u': 'L2 m1 m2 180n',
        'C6to8 m2 0 {30n+cm}': 'C6to8 m2 0 600p',
        'L3 m2 m3 16.9u': 'L3 m2 m3 400n',
        'C9 m3 0 {6.8n+ci}': 'C9 m3 0 382p',
        'L4 m3 m4 16.9u': 'L4 m3 m4 540n',
        'L5 m4 0 11.5u Rser=0.1 Rpar=100Meg\n': '',
        'CacrossL5 m4 0 {1f+cl} Rser=50m\n': '',
        'C10to11 m4 out {7.8n+co} Rser=50m Rpar=100Meg': 'Routput m4 out 1m',
        'Cextra out 0 {1f+cp}': 'Cextra out 0 235p',
        '.model FDP18N20F VDMOS(Rg=2.3 Vto=4 Rd=100m Rs=0m Rb=1.0m Kp=16 Cgdmax=0.5n Cgdmin=0.02n Cgs=1n Cjo=0.5n Is=0.2p tt=80n ksubthres=.1 mfg=Fairchild Vds=200 Ron=120m Qg=20n)':
        '.model STP19NF20_EST VDMOS(Vto=3 Kp=7 Rd=85m Rs=5m Rg=2 Rb=0.1 Cgs=774p Cgdmin=10p Cgdmax=550p A=0.43 Cjo=789p Vj=0.8 M=0.5 Is=10p N=1.3 Tt=65n)',
    }
    for old, new in changes.items():
        if old not in net:
            raise ValueError(f'Missing {old}')
        net = net.replace(old, new)
    net = net.replace('* Nominal Hot-Wand Lite PCB; capacitor-bank names use PCB references',
                      '* 13 MHz board output passive values at 467.6 kHz; element names inherited from Lite parser')
    net = net.replace('* ESR, DCR and FDP18N20F model inherited from Discord simulation, not measured',
                      '* STP19NF20 estimate from 13 MHz study; passive losses are assumptions, not measured')
    net = net.replace('* D3/D4 clamp, avalanche, core saturation and thermal feedback are NOT modeled',
                      '* Drain clamp, avalanche, core saturation and thermal feedback are NOT modeled')
    net = net.replace('Rser=60m Rpar=100Meg', 'Rser=30m Rpar=100Meg')
    net = net.replace('Rser=0.1 Rpar=100Meg', 'Rser=30m Rpar=100Meg')
    net = net.replace('400n Rser=30m Rpar=1Meg', '400n Rser=10m Rpar=1Meg')
    net = net.replace('600p Rser=30m Rpar=100Meg', '600p Rser=10m Rpar=100Meg')
    net = net.replace('382p Rser=50m Rpar=100Meg', '382p Rser=10m Rpar=100Meg')
    net = net.replace('235p Rser=50m', '235p Rser=10m')
    net = net.replace('L1 supply drain 9u Rser=30m', 'L1 supply drain 9u Rser=0.1')
    net = net.replace('I(C10to11)', 'I(Routput)')
    net = net.replace('.step param Case list 0', '.param Case=0')
    path = HERE / 'at-470khz.cir'
    path.write_text(net, encoding='ascii')
    exe = Path(os.environ['LOCALAPPDATA']) / 'Programs/ADI/LTspice/LTspice.exe'
    subprocess.run([str(exe), '-b', str(path)], check=True, timeout=300,
                   creationflags=subprocess.CREATE_NO_WINDOW)
    raw = read_raw(path.with_suffix('.raw'))
    cuts = np.r_[0, np.flatnonzero(np.diff(raw['time']) < 0)+1, len(raw['time'])]
    freq = cases[0]['f']
    for i, (a, b) in enumerate(zip(cuts[:-1], cuts[1:])):
        t, gate, drain = raw['time'][a:b], raw['v(gate)'][a:b], raw['v(drain)'][a:b]
        steady = t >= 360/freq
        crosses = np.flatnonzero((gate[:-1] < 3) & (gate[1:] >= 3) & (t[:-1] >= 360/freq))
        frac = (3-gate[crosses])/(gate[crosses+1]-gate[crosses])
        at_turnon = drain[crosses] + frac*(drain[crosses+1]-drain[crosses])
        print(f'{("cold", "warm", "hot")[i]}: final drain peak {drain[steady].max():.1f} V, '
              f'drain at gate 3 V {at_turnon.mean():.1f} V')
    print(path.with_suffix('.log').read_text(errors='replace')[-1500:])


if __name__ == '__main__':
    main()
