#!/usr/bin/env python3
"""Builds SPICE reference data for the amp's null tests.

Runs ngspice on the real circuit (Koren 12AX7 models with grid conduction and
interelectrode capacitance, self-biased stages with bypass caps, the tone
stack and gain pot wired together as they are on the chassis) and writes the
results to tests/data/spice/, where tests/SpiceNullTests.cpp compares the
plugin's models against them.

    python3 tools/spice/generate.py        # needs ngspice on PATH

The CSVs are committed, so the tests run without ngspice. Rerun this after
changing a circuit value here or in the plugin; the two must agree.
"""

import math
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "tests", "data", "spice")

# Circuit values. Keep in step with TriodeStage::Design, ToneStack::Components
# and Preamp in source/dsp/amp.
SUPPLY, RP, RK = 250.0, 100e3, 1.5e3
CK = 22e-6                  # cathode bypass: large enough to be "fully bypassed"
GRID_STOPPER = 68e3         # input grid stopper on V1A
TREBLE_POT, BASS_POT, MID_POT, SLOPE = 250e3, 1e6, 25e3, 100e3
TREBLE_CAP, BASS_CAP, MID_CAP = 250e-12, 22e-9, 22e-9
GAIN_POT, BRIGHT_CAP = 1e6, 120e-12
COUPLING_CAP, NEXT_GRID = 22e-9, 1e6

RATE = 192000.0             # the amp's oversampled rate at 48 kHz
TONE_FREQS = [100.0 * 2 ** k for k in range(7)]   # 100 Hz .. 6.4 kHz, octaves

# Koren's 12AX7: plate current from his triode equation, grid conduction as a
# diode behind RGI, and the published interelectrode capacitances.
TRIODE = """
.subckt TRIODE P G K
Be E 0 V = V(P,K)/600*ln(1+exp(600*(1/100+V(G,K)/sqrt(300+V(P,K)*V(P,K)))))
Re E 0 1G
Bp P K I = pwr(V(E),1.4)*(1+sgn(V(E)))/1060
Rgi G G1 2000
Dg G1 K DX
Cgk G K 2.3p
Cgp G P 2.4p
Cpk P K 0.9p
.model DX D(IS=1N RS=1 CJO=10PF TT=1N)
.ends
"""


def audio_taper(knob):
    x = min(max(knob, 0.0), 10.0) / 10.0
    return (10.0 ** (2.0 * x) - 1.0) / 99.0


def pot(name, a, wiper, b, total, fraction):
    """A pot as two resistors, a->wiper is (1 - fraction), wiper->b is fraction."""
    top = max((1.0 - fraction) * total, 1e-3)
    bottom = max(fraction * total, 1e-3)
    return f"R{name}u {a} {wiper} {top}\nR{name}l {wiper} {b} {bottom}\n"


def tone_stack(src, out, treble, bass, mid):
    """The netlist in ToneStack.h; out is loaded by whatever follows it."""
    return (
        f"C1 {src} T {TREBLE_CAP}\n"
        f"Rslope {src} S {SLOPE}\n"
        + pot("t", "T", out, "X", TREBLE_POT, treble)
        + f"C2 S X {BASS_CAP}\n"
        f"C3 S M {MID_CAP}\n"
        f"Rb X M {max(bass * BASS_POT, 1e-3)}\n"
        f"Rm M 0 {max(mid * MID_POT, 1e-3)}\n"
    )


def run(netlist, outputs):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "c.cir")
        files = {k: os.path.join(d, k + ".txt") for k in outputs}
        control = "\n".join(cmd.format(**files) for cmd in outputs.values())
        with open(path, "w") as f:
            f.write(netlist + "\n.control\nset wr_singlescale\nset wr_vecnames\n"
                    + control + "\nquit\n.endc\n.end\n")
        r = subprocess.run(["ngspice", "-b", path], capture_output=True, text=True)
        if r.returncode != 0 or any(not os.path.exists(p) for p in files.values()):
            sys.exit(f"ngspice failed:\n{netlist}\n{r.stdout}\n{r.stderr}")
        result = {}
        for k, p in files.items():
            with open(p) as f:
                lines = f.read().split("\n")
            rows = [list(map(float, l.split())) for l in lines[1:] if l.strip()]
            result[k] = rows
        return result


def stage(name, plate, grid, supply="bplus"):
    return (f"X{name} {plate} {grid} k{name} TRIODE\n"
            f"Rk{name} k{name} 0 {RK}\nCk{name} k{name} 0 {CK}\n"
            f"Rp{name} {supply} {plate} {RP}\n")


def triode_reference():
    # Quiescent point of one self-biased stage.
    op = run(f"* op\n{TRIODE}\nVB bplus 0 {SUPPLY}\nVg g 0 0\n" + stage("1", "p", "g"),
             {"op": "op\nwrdata {op} v(p) v(k1)"})["op"][0]
    vp, vk = op[1], op[2]

    # Plate transfer with the cathode held at its quiescent voltage (what the
    # bypass cap does at audio frequencies), grid driven from a stiff source.
    dc = run(f"* dc\n{TRIODE}\nVB bplus 0 {SUPPLY}\nVg g 0 0\nVk k 0 {vk}\n"
             f"X1 p g k TRIODE\nRp bplus p {RP}\n",
             {"dc": "dc Vg -8 1 0.25\nwrdata {dc} v(p)"})["dc"]

    with open(os.path.join(OUT, "triode_dc.csv"), "w") as f:
        f.write("# Koren 12AX7, Rp 100k, B+ 250 V, cathode fixed at its quiescent voltage\n")
        f.write(f"# quiescent vp={vp:.6f} vk={vk:.6f}\n")
        f.write("vin,plate_minus_quiescent\n")
        for row in dc:
            f.write(f"{row[0]:.4f},{row[1] - vp:.6f}\n")


def tone_stack_reference():
    rows = []
    for t in (0.0, 0.1, 0.5, 1.0):
        for b in (0.0, 0.1, 0.5, 1.0):
            for m in (0.0, 0.5, 1.0):
                net = (f"* tone stack\nVin in 0 AC 1\n" + tone_stack("in", "out", t, b, m)
                       + f"RL out 0 {GAIN_POT}\n")
                ac = run(net, {"ac": "ac oct 1 100 6400\nwrdata {ac} vdb(out) vp(out)"})["ac"]
                for r in ac:
                    rows.append((t, b, m, r[0], r[1], r[2]))

    with open(os.path.join(OUT, "tonestack_ac.csv"), "w") as f:
        f.write("# Tone stack alone, ideal source, loaded by the 1M gain pot. Pot positions are electrical.\n")
        f.write("treble,bass,mid,hz,db,phase_rad\n")
        for r in rows:
            f.write(",".join(f"{x:.6g}" for x in r) + "\n")


def preamp(gain, treble, bass, mid, bright, source):
    """Guitar jack to V1B's plate, wired as on the chassis."""
    g, t, b = audio_taper(gain), audio_taper(treble), audio_taper(bass)
    m = min(max(mid, 0.0), 10.0) / 10.0
    net = (f"{TRIODE}\nVB bplus 0 {SUPPLY}\nVin in 0 {source}\n"
           f"Rstop in g1 {GRID_STOPPER}\n"
           + stage("1", "p1", "g1")
           + tone_stack("p1", "ts", t, b, m)
           + pot("g", "ts", "g2", "0", GAIN_POT, g)
           + (f"Cbright ts g2 {BRIGHT_CAP}\n" if bright else "")
           + stage("2", "p2", "g2")
           + f"Cout p2 o {COUPLING_CAP}\nRnext o 0 {NEXT_GRID}\n")
    return net


AC_SETTINGS = [  # gain, treble, bass, mid (knobs 0..10), bright
    (5, 5, 5, 5, 0), (2, 5, 5, 5, 0), (2, 5, 5, 5, 1), (8, 5, 5, 5, 0),
    (10, 5, 5, 5, 1), (5, 10, 0, 0, 0), (5, 0, 10, 10, 0), (5, 10, 10, 10, 0),
]

TRAN_CASES = [  # input peak volts at 200 Hz, gain knob; tone at noon, bright off
    (0.1, 3), (0.3, 5), (0.3, 8), (1.0, 8), (1.0, 10), (2.0, 10),
]


def preamp_ac_reference():
    rows = []
    for s in AC_SETTINGS:
        ac = run("* preamp ac\n" + preamp(*s, "DC 0 AC 1"),
                 {"ac": "op\nac oct 1 100 6400\nwrdata {ac} vdb(o) vp(o)"})["ac"]
        for r in ac:
            rows.append(s + (r[0], r[1], r[2]))

    with open(os.path.join(OUT, "preamp_ac.csv"), "w") as f:
        f.write("# Guitar jack to V1B plate (after the coupling cap), small signal. Knobs 0..10.\n")
        f.write("gain,treble,bass,mid,bright,hz,db,phase_rad\n")
        for r in rows:
            f.write(",".join(f"{x:.6g}" for x in r) + "\n")


def preamp_tran_reference():
    hz, cycles, settle = 200.0, 2, 0.3
    n = int(round(cycles * RATE / hz))
    dt = 1.0 / RATE
    stop = settle + n * dt

    with open(os.path.join(OUT, "preamp_tran.csv"), "w") as f:
        f.write(f"# Guitar jack to V1B plate, {hz:g} Hz sine from t=0, tone at noon, bright off.\n")
        f.write(f"# Each case is the last {cycles} cycles; sample is the index at {RATE:g} Hz.\n")
        f.write("amplitude,gain,sample,volts\n")
        for amp, gain in TRAN_CASES:
            src = f"SIN(0 {amp} {hz})"
            cmd = (f"tran {dt / 4} {stop} {settle} {dt / 4}\n"
                   f"linearize v(o)\nwrdata {{tr}} v(o)")
            tr = run("* preamp tran\n.options reltol=1e-5 vntol=1e-7\n"
                     + preamp(gain, 5, 5, 5, 0, src), {"tr": cmd})["tr"]
            # linearize resamples at the step given to tran, so pick every 4th point.
            for t, v in tr:
                k = (t - settle) / dt
                if abs(k - round(k)) < 1e-3 and 1 <= round(k) <= n:
                    f.write(f"{amp:g},{gain:g},{int(round(t * RATE))},{v:.6f}\n")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    triode_reference()
    tone_stack_reference()
    preamp_ac_reference()
    preamp_tran_reference()
    print("wrote", ", ".join(sorted(os.listdir(OUT))))
