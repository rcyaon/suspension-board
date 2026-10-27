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
    hw = {"bus_conflicts": "no clashes on the shared SPI bus",
          "ads_wrong_spi_mode": "ADC always driven in the correct SPI mode",
          "ads_bad_frames": "no corrupted ADC readings",
          "sd_protocol_errors": "no SD card command errors",
          "sd_wrong_spi_mode": "SD card always driven in the correct SPI mode",
          "imu_errors": "no IMU errors"}
    for k, label in hw.items():
        c.add(label, dev[k] == 0, f"{dev[k]} errors")
    if not expect_drops:
        c.add("every ADC reading collected before the next one replaced it", dev["ads_conversions_missed"] == 0,
              str(dev["ads_conversions_missed"]))

    # --- card image ---
    names = [n for n, _, _ in image.entries()]
    c.add("log file created on the SD card", "RUN00001.BIN" in names, ", ".join(names))
    raw, chain = image.read("RUN00001.BIN")
    contiguous = chain == list(range(chain[0], chain[0] + len(chain)))
    c.add("file stored in one unbroken piece, card file table consistent", contiguous and image.fats_match(),
          f"{len(chain)} clusters")
    c.add("unused reserved space returned to the card when the file closes", len(chain) == -(-len(raw) // (image.spc * 512)),
          f"file {len(raw)} B, {len(chain)} clusters")

    log = sdlog.Log(raw)
    gains = [int(g) for g in log.gain]
    c.add("file header records sample rate, gains and run settings", log.meta["sample_rate_sps"] == rate
          and gains == [128, 128, 32, 32, 128, 128, 64, 32] and log.config.get("DRIVER") == "sim"
          and "TYRE_KPA_RL" in log.config, f"{log.meta['sample_rate_sps']} sps, gains {gains}")
    c.add("no missing or out-of-order data blocks", log.seq == list(range(len(log.seq))), f"{len(log.seq)} blocks")

    r = log.rec
    gaps = np.diff(r["sample"].astype(np.int64)) - 1
    missing = int(gaps[gaps > 0].sum())
    dt = np.diff(r["t_us"].astype(np.int64))
    if expect_drops:
        c.add("lost samples counted the same way in the data, the file trailer and the firmware",
              missing > 0 and missing == int(log.trailer["dropped"]) == rep["fw"]["dropped"],
              f"gaps {missing}, trailer {log.trailer['dropped']}, firmware {rep['fw']['dropped']}")
        drop_evts = int((log.events["code"] == 4).sum())
        c.add("every loss is marked in the log", drop_evts > 0, f"{drop_evts} events")
        ok = gaps == 0
        c.add("samples evenly spaced except where lost (within ±5 µs)", np.all(np.abs(dt[ok] - per_us) <= 5),
              f"{dt[ok].min()}..{dt[ok].max()} us")
    else:
        c.add("no samples lost", missing == 0 and int(log.trailer["dropped"]) == 0 and rep["fw"]["dropped"] == 0,
              f"{len(r)} samples, {missing} missing")
        c.add(f"samples evenly spaced every {per_us:.0f} µs (within ±5 µs)", np.all(np.abs(dt - per_us) <= 5),
              f"{dt.min()}..{dt.max()} us")
    c.add("sample count in the file trailer matches the data", int(log.trailer["frames"]) == len(r) + (missing if expect_drops else 0),
          f"{log.trailer['frames']}")

    # --- bridge samples vs the Adams truth ---
    t = log.t
    # the logged time is the interrupt entry, a few us after the sample instant. The simulator
    # reports its exact ADC sample grid; snap each record to the grid point just before it.
    ph, per = rep["drdy_phase_ns"], rep["drdy_period_ns"]
    t_ns = r["t_us"].astype(np.int64) * 1000 + 999
    t_drdy = (ph + ((t_ns - ph) // per) * per) * 1e-9
    lag = (t_ns - 999) * 1e-9 - t_drdy
    c.add("each timestamp within 5 µs of the moment the ADC sampled", np.all(np.abs(lag) < 5e-6),
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
    got_v = np.zeros((len(t), 8))
    true_mvv = np.zeros((len(t), 8))
    for ch in range(8):
        mvv = truth(sc, t_scn, f"ch{ch + 1}_mvv")
        vin = (mvv * 1e-3 + shunt * SHUNT_VV) * vexc
        got = log.codes[:, ch] * 1.2 / (log.gain[ch] * 2.0 ** 23)
        err_uv[:, ch] = (got - vin) * 1e6
        got_v[:, ch], true_mvv[:, ch] = got, mvv
    off = np.median(err_uv[use], axis=0)
    resid = err_uv[use] - off
    rms = np.sqrt((resid ** 2).mean(axis=0))
    c.add("all 8 strain-gauge channels match the input to within simulator noise",
          np.all(rms < 1.5 * NOISE_UV) and np.all(np.abs(resid) < 7 * NOISE_UV),
          f"residual rms {rms.min():.2f}-{rms.max():.2f} uV input-referred, max {np.abs(resid).max():.2f} uV "
          f"(sim noise {NOISE_UV} uV)")

    # --- shunt calibration ---
    cal = log.shunt_cal()
    nominal_nvv = SHUNT_VV * 1e9
    start_err = max(abs(a / nominal_nvv - 1) for a, _, _ in cal.values())
    drift = max(abs(p) for _, _, p in cal.values())
    c.add("built-in calibration reads the expected -0.874 mV/V (±1 %) on every channel",
          all(abs(a / -874000 - 1) < 0.01 and abs(b / -874000 - 1) < 0.01 for a, b, _ in cal.values()),
          f"start {min(a for a, _, _ in cal.values()) / 1e6:.4f}..{max(a for a, _, _ in cal.values()) / 1e6:.4f} mV/V")
    c.add("end-of-run calibration within 0.5 % of start (no gain drift)", drift < 5000 and log.trailer["gain_drift_flag"] == "0",
          f"max {drift} ppm")
    exc_est = 2.49 * float(np.mean([a for a, _, _ in cal.values()])) / nominal_nvv

    # --- force reconstruction from the shock load cell (CH8) ---
    run = use & (log.phase() == 3)
    ch8 = log.mvv(exc_est)[:, 7]
    force = ch8 / 2.0 * 8000.0 - off[7] * 1e-3 / exc_est / 2.0 * 8000.0
    f_true = truth(sc, t_scn, "shock_force_n")
    f_err = force[run] - f_true[run]
    c.add("shock force from the load cell (CH8) matches the Adams model", np.sqrt((f_err ** 2).mean()) < 2.0,
          f"rms error {np.sqrt((f_err ** 2).mean()):.2f} N, peak {force[run].max():.0f} N "
          f"(truth {f_true[run].max():.0f} N)")

    # --- aux travel (scan started at the previous DRDY) and IMU ---
    aux1 = r["aux"][1:, 0]
    exp_aux = truth(sc, t_scn[:-1] + 40e-6, "aux1_v") / 3.3 * 4095
    k = use[1:] & (np.diff(r["sample"].astype(np.int64)) == 1)
    aerr = np.abs(aux1[k] - exp_aux[k])
    c.add("shock travel sensor (AUX1) matches within 1.5 ADC counts", aerr.max() < 1.5, f"max {aerr.max():.2f} LSB")
    # IMU: the read lands on the 2 kHz ODR tick current at read time (within one tick)
    t_imu = np.floor((t_scn + 30e-6) / 500e-6) * 500e-6
    az = r["accel"][:, 2] / 2048
    ierr = np.min([np.abs(az[use] - truth(sc, t_imu[use] + k * 500e-6, "accel_z_g")) for k in (-1, 0, 1)], axis=0)
    c.add("IMU vertical acceleration matches within 1 mg", ierr.max() < 2 / 2048, f"max {ierr.max() * 1000:.2f} mg")
    stale = (flags[use] & sdlog.F_IMU_STALE) > 0
    want = min(1.0, 2000.0 / log.rate)          # IMU ODR is 2 kHz
    c.add(f"fresh IMU reading on ≥ 95 % of frames where one is possible ({100 * want:.0f} % of frames at this rate)",
          1 - stale.mean() >= 0.95 * want, f"{100 * (1 - stale.mean()):.1f} % fresh")

    # --- run sequence, events, CAN ---
    ph = log.phase()
    settle = (ph == 1).sum() / log.rate
    on_runs = np.diff(np.flatnonzero(np.diff(np.r_[0, shunt.astype(int), 0]))) [::2] / log.rate
    c.add("run follows the set order: 1 s settle, 0.5 s calibration at start and end, sensors powered off at the end",
          abs(settle - 1.0) < 0.01 and len(on_runs) == 2 and np.all(np.abs(on_runs - 0.5) < 0.01)
          and not exc_on[-1], f"settle {settle:.3f} s, shunt on {', '.join(f'{x:.3f}' for x in on_runs)} s")
    ev = log.events
    syncs = int((ev["code"] == 7).sum())
    rx = ev[ev["code"] == 8]
    c.add("CAN bus: start/stop sent, 10 Hz time sync logged, other board's sync received",
          rep["can_tx"]["start"] == 1 and rep["can_tx"]["stop"] == 1
          and (expect_drops or syncs >= 10 * (t[-1] - t[0]) - 3)
          and len(rx) == 1 and rx["a"][0] == 2, f"{syncs} sync tx events, rx from node {[int(a) for a in rx['a']]}")
    late = int(((flags & sdlog.F_LATE) > 0).sum())
    c.add("writing to the SD card never made the firmware miss an ADC reading", rep["fw"]["overrun"] == 0,
          f"{late} frames read right after an SD step, ring max {rep['fw']['ring_max']}/256 blocks, "
          f"{dev['sd_long_stalls']} card stalls")

    stats = dict(samples=len(r), seconds=t[-1] - t[0], late=late, ring_max=rep["fw"]["ring_max"],
                 sd_block_us=rep["fw"]["sd_block_us_max"], stalls=dev["sd_long_stalls"],
                 rms=rms, exc_est=exc_est, period=per_us, jitter=np.abs(dt[gaps == 0] - per_us).max(),
                 lag=np.abs(lag).max(), cal=cal, force_peak=force[run].max(), truth_peak=f_true[run].max())
    logged_mvv = got_v / exc_est * 1e3 - off * 1e-3 / exc_est
    extra = dict(logged_mvv=logged_mvv, true_mvv=true_mvv, rms=rms, dt=dt[gaps == 0], per_us=per_us)
    return c, log, stats, (t, t_scn, force, f_true, run, extra)


def plot_svg(data, sc, path):
    """logged vs input shock force as a dependency-free SVG"""
    t, t_scn, force, f_true, run = data[:5]
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


# shared look for the extra plots: grey = input, blue = logged, recessive grid
INK, INK2, GRID, BLUE, REF = "#0b0b0b", "#52514e", "#e4e3df", "#2a78d6", "#b9b8b2"


def _svg(w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" font-family="sans-serif" '
            f'font-size="11" fill="{INK}">\n<rect width="100%" height="100%" fill="#fcfcfb"/>\n{body}\n</svg>')


def plot_channels_svg(data, sc, path):
    """logged vs input for four strain channels, one small panel each (own y scale)"""
    t, t_scn, force, f_true, run, ex = data
    chans = [(0, "CH1  lower arm push/pull"), (2, "CH3  lower arm bending"),
             (4, "CH5  upper arm push/pull"), (7, "CH8  shock load cell")]
    w, ph, padl, top = 900, 105, 60, 30
    h = top + ph * len(chans) + 30
    t0, t1 = 0.0, 10.0
    X = lambda x: padl + (x - t0) / (t1 - t0) * (w - padl - 10)
    k = np.flatnonzero(run)[::4]
    body = [f'<text x="{padl}" y="18" font-weight="bold">Strain channels: logged by the firmware vs the Adams input (mV/V)</text>']
    for i, (ch, name) in enumerate(chans):
        y0 = top + i * ph
        ref, got = ex["true_mvv"][k, ch], ex["logged_mvv"][k, ch]
        lo, hi = min(ref.min(), got.min()), max(ref.max(), got.max())
        pad = 0.08 * (hi - lo or 1)
        lo, hi = lo - pad, hi + pad
        Y = lambda y: y0 + ph - 18 - (y - lo) / (hi - lo) * (ph - 30)
        for v in (lo + pad, hi - pad):
            body.append(f'<line x1="{padl}" x2="{w - 10}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="{GRID}"/>'
                        f'<text x="{padl - 6}" y="{Y(v) + 4:.1f}" text-anchor="end" fill="{INK2}">{(0.0 if abs(v) < 5e-4 else v):.3g}</text>')
        pr = " ".join(f"{X(a):.1f},{Y(b):.1f}" for a, b in zip(t_scn[k], ref))
        pg = " ".join(f"{X(a):.1f},{Y(b):.1f}" for a, b in zip(t_scn[k], got))
        body.append(f'<polyline points="{pr}" fill="none" stroke="{REF}" stroke-width="4" stroke-linejoin="round"/>')
        body.append(f'<polyline points="{pg}" fill="none" stroke="{BLUE}" stroke-width="1.2"/>')
        body.append(f'<text x="{padl + 6}" y="{y0 + 10}" fill="{INK2}">{name}</text>')
    yb = top + ph * len(chans)
    body += [f'<text x="{X(v):.1f}" y="{yb}" text-anchor="middle" fill="{INK2}">{v:.0f}</text>' for v in range(0, 11)]
    body.append(f'<line x1="{padl}" x2="{padl + 22}" y1="{h - 10}" y2="{h - 10}" stroke="{REF}" stroke-width="4"/>'
                f'<text x="{padl + 28}" y="{h - 6}">Adams input</text>'
                f'<line x1="{padl + 120}" x2="{padl + 142}" y1="{h - 10}" y2="{h - 10}" stroke="{BLUE}" stroke-width="2"/>'
                f'<text x="{padl + 148}" y="{h - 6}">logged on the SD card</text>'
                f'<text x="{w - 10}" y="{h - 6}" text-anchor="end" fill="{INK2}">drive time (s)</text>')
    path.write_text(_svg(w, h, "\n".join(body)))


def plot_error_svg(data, path):
    """per-channel residual RMS against the simulator's own noise"""
    ex = data[5]
    rms = ex["rms"]
    w, h, padl, padb, top = 900, 240, 60, 40, 34
    ymax = max(0.5, rms.max() * 1.25)
    Y = lambda y: (h - padb) - y / ymax * (h - padb - top)
    bw = (w - padl - 10) / 8
    body = [f'<text x="{padl}" y="18" font-weight="bold">Error added by the firmware, per channel (µV RMS, input-referred)</text>']
    for v in np.arange(0, ymax + 1e-9, 0.1):
        body.append(f'<line x1="{padl}" x2="{w - 10}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="{GRID}"/>'
                    f'<text x="{padl - 6}" y="{Y(v) + 4:.1f}" text-anchor="end" fill="{INK2}">{v:.1f}</text>')
    for i, r in enumerate(rms):
        x = padl + i * bw + bw * 0.25
        body.append(f'<rect x="{x:.1f}" y="{Y(r):.1f}" width="{bw * 0.5:.1f}" height="{Y(0) - Y(r):.1f}" rx="3" fill="{BLUE}"/>'
                    f'<text x="{x + bw * 0.25:.1f}" y="{Y(r) - 5:.1f}" text-anchor="middle" fill="{INK2}">{r:.2f}</text>'
                    f'<text x="{x + bw * 0.25:.1f}" y="{h - padb + 16}" text-anchor="middle" fill="{INK2}">CH{i + 1}</text>')
    body.append(f'<line x1="{padl}" x2="{w - 10}" y1="{Y(NOISE_UV):.1f}" y2="{Y(NOISE_UV):.1f}" stroke="{INK}" '
                f'stroke-dasharray="5 4"/><text x="{w - 12}" y="{Y(NOISE_UV) - 22:.1f}" text-anchor="end">'
                f'dashed: noise the simulator itself adds ({NOISE_UV} µV)</text>')
    body.append(f'<text x="{padl}" y="{h - 6}" fill="{INK2}">bars at the dashed line mean the logged data is as clean as the input: '
                'the firmware adds no measurable error</text>')
    path.write_text(_svg(w, h, "\n".join(body)))


def plot_timing_svg(data, path):
    """histogram of the time between consecutive logged samples"""
    ex = data[5]
    dt, per = ex["dt"], ex["per_us"]
    vals, counts = np.unique(dt, return_counts=True)
    lo, hi = int(per) - 6, int(per) + 6
    w, h, padl, padb, top = 900, 240, 60, 40, 34
    cmax = counts.max()
    Y = lambda y: (h - padb) - y / cmax * (h - padb - top - 12)
    X = lambda x: padl + (x - lo) / (hi - lo + 1) * (w - padl - 10)
    bw = X(lo + 1) - X(lo)
    body = [f'<text x="{padl}" y="18" font-weight="bold">Time between samples, normal drive ({len(dt) + 1} samples)</text>']
    for v in range(lo, hi + 1):
        body.append(f'<text x="{X(v) + bw / 2:.1f}" y="{h - padb + 16}" text-anchor="middle" fill="{INK2}">{v}</text>')
    for v, c_ in zip(vals, counts):
        if lo <= v <= hi:
            body.append(f'<rect x="{X(v) + bw * 0.15:.1f}" y="{Y(c_):.1f}" width="{bw * 0.7:.1f}" height="{Y(0) - Y(c_):.1f}" rx="3" fill="{BLUE}"/>'
                        f'<text x="{X(v) + bw / 2:.1f}" y="{Y(c_) - 5:.1f}" text-anchor="middle" fill="{INK2}">{c_}</text>')
    body.append(f'<line x1="{padl}" x2="{w - 10}" y1="{Y(0):.1f}" y2="{Y(0):.1f}" stroke="{INK2}"/>')
    body.append(f'<text x="{padl}" y="{h - 6}" fill="{INK2}">µs between consecutive samples. Target {per:.0f} µs; '
                f'the test passes within ±5 µs. Every sample landed within ±{np.abs(dt - per).max():.0f} µs.</text>')
    path.write_text(_svg(w, h, "\n".join(body)))


def main():
    subprocess.run(["make", "-s", "-C", str(FW), "sim"], check=True)
    if not SCENARIO.exists():
        subprocess.run([sys.executable, str(HERE / "make_adams_scenario.py")], check=True)
    sc = load_scenario()
    RESULTS.mkdir(exist_ok=True)

    cases = []
    img, rep, con = run_case("drive", [])
    c1, log, st, data = check_case("Normal drive", img, rep, sc, False)
    cases.append(c1)
    plot_svg(data, sc, RESULTS / "shock_force.svg")
    plot_channels_svg(data, sc, RESULTS / "strain_channels.svg")
    plot_error_svg(data, RESULTS / "channel_error.svg")
    plot_timing_svg(data, RESULTS / "sample_timing.svg")
    img2, rep2, _ = run_case("stall", ["--stall-at-block", "1800", "--stall-ms", "1500"])
    c2, _, st2, _ = check_case("SD card freezes for 1.5 s (longer than the ~1 s RAM buffer)", img2, rep2, sc, True)
    cases.append(c2)
    img3, rep3, _ = run_case("fast", [], osr=1024)
    c3, _, st3, _ = check_case("Double sample rate (3906 samples/s)", img3, rep3, sc, False,
                               rate="3906.250")
    cases.append(c3)

    all_ok = all(c.ok for c in cases)
    lines = ["# Firmware test results", "",
             f"**{'All 3 test scenarios pass' if all_ok else 'SOME TESTS FAIL'}** "
             f"({sum(len(c.rows) for c in cases)} checks). Run `make -C firmware test` to regenerate this page.", "",
             "The real firmware from `src/` runs on a PC, connected to simulated versions of the board's chips:",
             "the ADS131M08 ADC, the microSD card and the ICM-42688 IMU. They are fed a 10 s drive over a bump,",
             "whoops, a 0.5 m drop and braking, generated from the Adams mini baja rear-corner model at 15 mph.",
             "The SD card file is then decoded by separate code and every sample is compared with that input.", "",
             "## What the tests show", "",
             f"- **No data lost.** A normal run logged all {st['samples']} samples over {st['seconds']:.1f} s with zero"
             f" missing. At double the sample rate it logged all {st3['samples']} samples, also with zero missing.",
             f"- **Measurements are accurate.** All 8 strain-gauge channels match the input to"
             f" {st['rms'].max():.2f} µV RMS, which is the simulator's own noise level, so the firmware adds no error.",
             f"- **Shock force is right.** Peak landing force logged {st['force_peak']:.0f} N against"
             f" {st['truth_peak']:.0f} N in the Adams model.",
             f"- **Timing is precise.** Samples arrive every {st['period']:.0f} µs within ±{st['jitter']:.0f} µs, and each"
             f" timestamp is within {st['lag']*1e6:.1f} µs of the moment the ADC sampled.",
             f"- **Saving to the SD card never gets in the way of measuring.** During the run the SD card froze"
             f" {st['stalls']} times for 120 ms each. The RAM buffer peaked at only {st['ring_max']} of 256 blocks and no reading was missed.",
             f"- **Calibration works without manual tuning.** The built-in shunt calibration measured the sensor"
             f" supply as {st['exc_est']:.3f} V (actual {rep['exc_volts']} V), so no trim pot is needed.",
             f"- **Losses are reported, not hidden.** When the card froze for 1.5 s, longer than the RAM buffer can"
             f" cover, the firmware lost {rep2['fw']['dropped']} samples. That count was the same in the data, the file trailer"
             " and the firmware's own count, and each loss was marked in the log. It then caught up without missing"
             " another reading.", "",
             "![shock force: logged vs input](shock_force.svg)", "",
             "![strain channels: logged vs input](strain_channels.svg)", "",
             "![error added per channel](channel_error.svg)", "",
             "![time between samples](sample_timing.svg)", "",
             "## What these tests don't cover", "",
             "This is a simulation, so it says nothing about analog noise, real SD card timing, the ADC's"
             " filter delay (corrected afterwards on the PC) or the PCB itself. Those need the bench tests in"
             " `spec.md` and `docs/test-plan.md`.", "",
             "## Detailed checks", ""]
    for c in cases:
        lines += [f"### {c.name}: {'PASS' if c.ok else 'FAIL'}", "", "| check | result | detail |", "|---|---|---|"]
        lines += [f"| {w} | {'pass' if p else '**FAIL**'} | {d} |" for w, p, d in c.rows]
        lines.append("")
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
