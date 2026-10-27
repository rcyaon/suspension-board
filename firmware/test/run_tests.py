"""Firmware-in-the-loop test with the Adams rear-corner drive.

Runs the real firmware (src/) on the PC against simulated chips, then reads
the SD card image back with independent code and compares every logged sample
with the Adams-derived input. Writes test/results/summary.md (+ a plot when
matplotlib is installed). Exit code 0 only if every check passes.

    make -C firmware test
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
FW = HERE.parent
BUILD = FW / "build" / "test"
RESULTS = HERE / "results"
SCENARIO = HERE / "data" / "adams_rear_drive.csv"
sys.path.insert(0, str(FW / "tools"))
sys.path.insert(0, str(HERE))
import fat32img  # noqa: E402
import sdlog  # noqa: E402

SHUNT_VV = -350.0 / (4 * 100e3 + 2 * 350.0)
NOISE_UV = 0.35


class Checks:
    def __init__(self, name):
        self.name, self.rows, self.ok = name, [], True

    def add(self, what, passed, detail):
        self.rows.append((what, bool(passed), detail))
        self.ok &= bool(passed)


def run_case(name, extra_args, osr=2048):
    BUILD.mkdir(parents=True, exist_ok=True)
    img = BUILD / f"{name}.img"
    report = BUILD / f"{name}.json"
    cfg = (HERE / "data" / "CONFIG.TXT").read_bytes().replace(b"OSR=2048", f"OSR={osr}".encode())
    fat32img.make(img, [(b"CONFIG  TXT", cfg)])
    subprocess.run([str(FW / "build" / "sim"), str(SCENARIO), str(img), str(report), *extra_args], check=True)
    return fat32img.Image(img), json.loads(report.read_text()), (BUILD / f"{name}.json.console.txt").read_text()


def load_scenario():
    with open(SCENARIO) as f:
        cols = f.readline().strip().split(",")
    data = np.loadtxt(SCENARIO, delimiter=",", skiprows=1)
    return {c: data[:, i] for i, c in enumerate(cols)}


def truth(sc, t_scn, col):
    return np.interp(t_scn, sc["t_s"], sc[col])


def check_case(name, image, rep, sc, expect_drops, rate="1953.125"):
    c = Checks(name)
    per_us = rep["drdy_period_ns"] / 1000
    dev = rep["devices"]
    for k in ("bus_conflicts", "ads_wrong_spi_mode", "ads_bad_frames", "sd_protocol_errors", "sd_wrong_spi_mode",
              "imu_errors"):
        c.add(f"simulated hardware: {k.replace('_', ' ')}", dev[k] == 0, str(dev[k]))
    if not expect_drops:
        c.add("ADC conversions never overwritten before being read", dev["ads_conversions_missed"] == 0,
              str(dev["ads_conversions_missed"]))

    # --- card image ---
    names = [n for n, _, _ in image.entries()]
    c.add("run file created on the card", "RUN00001.BIN" in names, ", ".join(names))
    raw, chain = image.read("RUN00001.BIN")
    contiguous = chain == list(range(chain[0], chain[0] + len(chain)))
    c.add("FAT chain contiguous, both FAT copies identical", contiguous and image.fats_match(),
          f"{len(chain)} clusters")
    c.add("unused preallocation released at close", len(chain) == -(-len(raw) // (image.spc * 512)),
          f"file {len(raw)} B, {len(chain)} clusters")

    log = sdlog.Log(raw)
    gains = [int(g) for g in log.gain]
    c.add("header: rate, gains and run metadata", log.meta["sample_rate_sps"] == rate
          and gains == [128, 128, 32, 32, 128, 128, 64, 32] and log.config.get("DRIVER") == "sim"
          and "TYRE_KPA_RL" in log.config, f"{log.meta['sample_rate_sps']} sps, gains {gains}")
    c.add("block sequence numbers contiguous", log.seq == list(range(len(log.seq))), f"{len(log.seq)} blocks")

    r = log.rec
    gaps = np.diff(r["sample"].astype(np.int64)) - 1
    missing = int(gaps[gaps > 0].sum())
    dt = np.diff(r["t_us"].astype(np.int64))
    if expect_drops:
        c.add("drops are counted honestly (counter gaps = trailer = firmware count)",
              missing > 0 and missing == int(log.trailer["dropped"]) == rep["fw"]["dropped"],
              f"gaps {missing}, trailer {log.trailer['dropped']}, firmware {rep['fw']['dropped']}")
        drop_evts = int((log.events["code"] == 4).sum())
        c.add("drop events written to the log", drop_evts > 0, f"{drop_evts} events")
        ok = gaps == 0
        c.add("timestamps regular where no drop (+-5 us interrupt jitter)", np.all(np.abs(dt[ok] - per_us) <= 5),
              f"{dt[ok].min()}..{dt[ok].max()} us")
    else:
        c.add("zero dropped samples", missing == 0 and int(log.trailer["dropped"]) == 0 and rep["fw"]["dropped"] == 0,
              f"{len(r)} samples, {missing} missing")
        c.add(f"every sample {per_us:.0f} us apart ({rate} sps, +-5 us interrupt jitter)", np.all(np.abs(dt - per_us) <= 5),
              f"{dt.min()}..{dt.max()} us")
    c.add("trailer frame count matches the file", int(log.trailer["frames"]) == len(r) + (missing if expect_drops else 0),
          f"{log.trailer['frames']}")

    # --- bridge samples vs the Adams truth ---
    t = log.t
    # the logged time is the interrupt entry, a few us after the sample instant. The simulator
    # reports its exact ADC sample grid; snap each record to the grid point just before it.
    ph, per = rep["drdy_phase_ns"], rep["drdy_period_ns"]
    t_ns = r["t_us"].astype(np.int64) * 1000 + 999
    t_drdy = (ph + ((t_ns - ph) // per) * per) * 1e-9
    lag = (t_ns - 999) * 1e-9 - t_drdy
    c.add("logged timestamp (1 us steps) within 5 us of the ADC sample instant", np.all(np.abs(lag) < 5e-6),
          f"{lag.min() * 1e6:.1f}..{lag.max() * 1e6:.1f} us")
    t_scn = t_drdy - rep["drive_start_s"]
    flags = r["flags"]
    exc_on = (flags & sdlog.F_EXC) > 0
    t_exc = t[np.argmax(exc_on)]
    shunt = (flags & sdlog.F_SHUNT) > 0
    edges = t[np.flatnonzero(np.diff(shunt.astype(int))) + 1]
    near_edge = np.zeros(len(t), bool)
    for e in edges:
        near_edge |= np.abs(t - e) < 0.003
    use = exc_on & (t > t_exc + 0.2) & ~near_edge
    vexc = rep["exc_volts"]
    err_uv = np.zeros((len(t), 8))
    for ch in range(8):
        mvv = truth(sc, t_scn, f"ch{ch + 1}_mvv")
        vin = (mvv * 1e-3 + shunt * SHUNT_VV) * vexc
        got = log.codes[:, ch] * 1.2 / (log.gain[ch] * 2.0 ** 23)
        err_uv[:, ch] = (got - vin) * 1e6
    off = np.median(err_uv[use], axis=0)
    resid = err_uv[use] - off
    rms = np.sqrt((resid ** 2).mean(axis=0))
    c.add("all 8 bridge channels match the input (after per-channel offset)",
          np.all(rms < 1.5 * NOISE_UV) and np.all(np.abs(resid) < 7 * NOISE_UV),
          f"residual rms {rms.min():.2f}-{rms.max():.2f} uV input-referred, max {np.abs(resid).max():.2f} uV "
          f"(sim noise {NOISE_UV} uV)")

    # --- shunt calibration ---
    cal = log.shunt_cal()
    nominal_nvv = SHUNT_VV * 1e9
    start_err = max(abs(a / nominal_nvv - 1) for a, _, _ in cal.values())
    drift = max(abs(p) for _, _, p in cal.values())
    c.add("shunt cal -0.874 mV/V +-1 % on every channel (start and end)",
          all(abs(a / -874000 - 1) < 0.01 and abs(b / -874000 - 1) < 0.01 for a, b, _ in cal.values()),
          f"start {min(a for a, _, _ in cal.values()) / 1e6:.4f}..{max(a for a, _, _ in cal.values()) / 1e6:.4f} mV/V")
    c.add("start/end shunt drift < 0.5 %, no gain-drift flag", drift < 5000 and log.trailer["gain_drift_flag"] == "0",
          f"max {drift} ppm")
    exc_est = 2.49 * float(np.mean([a for a, _, _ in cal.values()])) / nominal_nvv

    # --- force reconstruction from the shock load cell (CH8) ---
    run = use & (log.phase() == 3)
    ch8 = log.mvv(exc_est)[:, 7]
    force = ch8 / 2.0 * 8000.0 - off[7] * 1e-3 / exc_est / 2.0 * 8000.0
    f_true = truth(sc, t_scn, "shock_force_n")
    f_err = force[run] - f_true[run]
    c.add("shock force from CH8 matches Adams-derived force", np.sqrt((f_err ** 2).mean()) < 2.0,
          f"rms error {np.sqrt((f_err ** 2).mean()):.2f} N, peak {force[run].max():.0f} N "
          f"(truth {f_true[run].max():.0f} N)")

    # --- aux travel (scan started at the previous DRDY) and IMU ---
    aux1 = r["aux"][1:, 0]
    exp_aux = truth(sc, t_scn[:-1] + 40e-6, "aux1_v") / 3.3 * 4095
    k = use[1:] & (np.diff(r["sample"].astype(np.int64)) == 1)
    aerr = np.abs(aux1[k] - exp_aux[k])
    c.add("AUX1 shock travel matches (within 1.5 LSB)", aerr.max() < 1.5, f"max {aerr.max():.2f} LSB")
    # IMU: the read lands on the 2 kHz ODR tick current at read time (within one tick)
    t_imu = np.floor((t_scn + 30e-6) / 500e-6) * 500e-6
    az = r["accel"][:, 2] / 2048
    ierr = np.min([np.abs(az[use] - truth(sc, t_imu[use] + k * 500e-6, "accel_z_g")) for k in (-1, 0, 1)], axis=0)
    c.add("IMU vertical acceleration matches", ierr.max() < 2 / 2048, f"max {ierr.max() * 1000:.2f} mg")
    stale = (flags[use] & sdlog.F_IMU_STALE) > 0
    want = min(1.0, 2000.0 / log.rate)          # IMU ODR is 2 kHz
    c.add(f"new IMU sample on >= 95 % of the {100 * want:.0f} % of frames that can have one",
          1 - stale.mean() >= 0.95 * want, f"{100 * (1 - stale.mean()):.1f} % fresh")

    # --- run sequence, events, CAN ---
    ph = log.phase()
    settle = (ph == 1).sum() / log.rate
    on_runs = np.diff(np.flatnonzero(np.diff(np.r_[0, shunt.astype(int), 0]))) [::2] / log.rate
    c.add("sequence: 1 s settle, 0.5 s shunt at start and end, then excitation off",
          abs(settle - 1.0) < 0.01 and len(on_runs) == 2 and np.all(np.abs(on_runs - 0.5) < 0.01)
          and not exc_on[-1], f"settle {settle:.3f} s, shunt on {', '.join(f'{x:.3f}' for x in on_runs)} s")
    ev = log.events
    syncs = int((ev["code"] == 7).sum())
    rx = ev[ev["code"] == 8]
    c.add("CAN: start/stop sent, 10 Hz sync logged, other board's sync logged",
          rep["can_tx"]["start"] == 1 and rep["can_tx"]["stop"] == 1
          and (expect_drops or syncs >= 10 * (t[-1] - t[0]) - 3)
          and len(rx) == 1 and rx["a"][0] == 2, f"{syncs} sync tx events, rx from node {[int(a) for a in rx['a']]}")
    late = int(((flags & sdlog.F_LATE) > 0).sum())
    c.add("SD writes never delayed an ADC read past its deadline", rep["fw"]["overrun"] == 0,
          f"{late} frames read right after an SD step, ring max {rep['fw']['ring_max']}/256 blocks, "
          f"{dev['sd_long_stalls']} card stalls")

    stats = dict(samples=len(r), seconds=t[-1] - t[0], late=late, ring_max=rep["fw"]["ring_max"],
                 sd_block_us=rep["fw"]["sd_block_us_max"], stalls=dev["sd_long_stalls"],
                 rms=rms, exc_est=exc_est, cal=cal, force_peak=force[run].max(), truth_peak=f_true[run].max())
    return c, log, stats, (t, t_scn, force, f_true, run)


def plot_svg(data, sc, path):
    """logged vs input shock force as a dependency-free SVG"""
    t, t_scn, force, f_true, run = data
    w, h, padl, padb = 900, 300, 60, 40
    t0, t1 = sc["t_s"][0], sc["t_s"][-1]
    fmin, fmax = -500.0, 5500.0
    X = lambda x: padl + (x - t0) / (t1 - t0) * (w - padl - 10)
    Y = lambda y: (h - padb) - (y - fmin) / (fmax - fmin) * (h - padb - 10)
    ref = " ".join(f"{X(a):.1f},{Y(b):.1f}" for a, b in zip(sc["t_s"][::2], sc["shock_force_n"][::2]))
    k = np.flatnonzero(run)[::4]
    got = " ".join(f"{X(a):.1f},{Y(b):.1f}" for a, b in zip(t_scn[k], force[k]))
    grid = "".join(f'<line x1="{padl}" x2="{w - 10}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="#ddd"/>'
                   f'<text x="{padl - 6}" y="{Y(v) + 4:.1f}" text-anchor="end">{v:.0f}</text>' for v in range(0, 5001, 1000))
    xt = "".join(f'<text x="{X(v):.1f}" y="{h - padb + 16}" text-anchor="middle">{v:.0f}</text>' for v in range(0, 11, 1))
    notes = [(1.0, "bump"), (2.5, "whoops"), (5.0, "0.5 m drop"), (7.0, "braking")]
    nt = "".join(f'<text x="{X(a) + 3:.1f}" y="22" fill="#666">{n}</text>' for a, n in notes)
    path.write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" font-family="sans-serif" font-size="11">
<rect width="100%" height="100%" fill="white"/>{grid}{xt}{nt}
<polyline points="{ref}" fill="none" stroke="#bbb" stroke-width="4"/>
<polyline points="{got}" fill="none" stroke="#1f6feb" stroke-width="1"/>
<text x="{padl}" y="{h - 6}">drive time (s)   grey: Adams-derived shock force input   blue: force read back from the logged CH8 load cell (N)</text>
</svg>''')


def main():
    subprocess.run(["make", "-s", "-C", str(FW), "sim"], check=True)
    if not SCENARIO.exists():
        subprocess.run([sys.executable, str(HERE / "make_adams_scenario.py")], check=True)
    sc = load_scenario()
    RESULTS.mkdir(exist_ok=True)

    cases = []
    img, rep, con = run_case("drive", [])
    c1, log, st, data = check_case("Adams drive, normal card", img, rep, sc, False)
    cases.append(c1)
    plot_svg(data, sc, RESULTS / "shock_force.svg")
    img2, rep2, _ = run_case("stall", ["--stall-at-block", "1800", "--stall-ms", "1500"])
    c2, _, st2, _ = check_case("Adams drive, card stalls 1.5 s (longer than the RAM buffer)", img2, rep2, sc, True)
    cases.append(c2)
    img3, rep3, _ = run_case("fast", [], osr=1024)
    c3, _, st3, _ = check_case("Adams drive at OSR 1024 (3906.25 sps, 256 us between samples)", img3, rep3, sc, False,
                               rate="3906.250")
    cases.append(c3)

    lines = ["# Firmware test results", "",
             "Generated by `make -C firmware test`. The firmware in `src/` runs on the PC against simulated",
             "ADS131M08, microSD and ICM-42688 chips. The input is a 10 s rear-corner drive built from the Adams",
             "mini baja model (`test/make_adams_scenario.py`: spring, damper, bump stop, tire, hardpoints,",
             "masses, 15 mph event speed). The card image is read back with separate FAT and log decoders and",
             "every sample is compared with that input.", ""]
    for c in cases:
        lines += [f"## {c.name}: {'PASS' if c.ok else 'FAIL'}", "", "| check | result | detail |", "|---|---|---|"]
        lines += [f"| {w} | {'pass' if p else '**FAIL**'} | {d} |" for w, p, d in c.rows]
        lines.append("")
    lines += ["## Normal run in numbers", "",
              f"- {st['samples']} samples over {st['seconds']:.2f} s, zero dropped, every frame read before the next sample"
              f" arrived ({st['late']} needed to wait for an SD step); RAM ring peaked at {st['ring_max']}/256 blocks through"
              f" {st['stalls']} simulated 120 ms card stalls; slowest SD block step {st['sd_block_us']} us.",
              f"- Shunt cal reads excitation as {st['exc_est']:.4f} V (simulated board: {rep['exc_volts']} V), so the"
              " calibration step finds the real rail without a trim pot.",
              f"- Peak landing shock force logged {st['force_peak']:.0f} N vs {st['truth_peak']:.0f} N in the Adams-based input.",
              f"- At 3906.25 sps (256 us between samples) the same code logs {st3['samples']} samples with zero drops;"
              f" ring peaked at {st3['ring_max']}/256.",
              f"- With a 1.5 s card stall (longer than the ~1 s RAM buffer) the firmware drops frames and reports the"
              f" same count in the counter gaps, the trailer and its own stats; catching up, it read {st2['late']} frames"
              " right after an SD block, each before the next sample.",
              "", "![shock force: logged vs input](shock_force.svg)",
              "", "## What this does and does not show", "",
              "Shows: pin/SPI sequencing, ADC reset and register setup, frame CRC, shared-bus scheduling, the"
              " ring buffer, FAT32 file handling, log format, run/cal sequence, CAN and drop accounting all work"
              " together on realistic signals. Does not show: analog noise, real card timing, the ADC's digital"
              " filter delay/droop (corrected offline), or anything about the PCB. Those need the bench tests in spec.md.", ""]
    (RESULTS / "summary.md").write_text("\n".join(lines))
    (RESULTS / "console.txt").write_text(con)
    for c in cases:
        print(f"{'PASS' if c.ok else 'FAIL'}: {c.name}")
        for w, p, d in c.rows:
            if not p:
                print(f"   FAIL {w}: {d}")
    print(f"results: {RESULTS / 'summary.md'}")
    sys.exit(0 if all(c.ok for c in cases) else 1)


if __name__ == "__main__":
    main()
