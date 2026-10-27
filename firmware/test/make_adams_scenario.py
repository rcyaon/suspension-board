"""Build a 10 s rear-corner drive from the Adams mini baja model, as test input
for the firmware simulator.

Everything vehicle-specific comes from data/adams-mini-baja:
  - rear spring curve + preload, rear damper curve, bump stop curve + clearance
  - rear tire vertical stiffness and damping
  - rear hardpoints -> shock motion ratio
  - part masses -> rear unsprung mass; full-vehicle mass -> corner load
  - the 15 mph straight-line event speed

A 2-DOF quarter car is driven over: rough ground, a 100 mm bump, whoops, a
0.5 m drop (jump landing) and a braking event. Shock force is turned into
arm forces with the static ratios from analysis/expected_loads.py (as tabled
in docs/test-plan.md), then into bridge mV/V with the test-plan gauge
sensitivities. This is a plausible, physically consistent load history for
exercising the logger, not a validated load prediction.

run: python3 firmware/test/make_adams_scenario.py
"""
import csv
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ADAMS = ROOT / "data" / "adams-mini-baja"
OUT = Path(__file__).resolve().parent / "data" / "adams_rear_drive.csv"

G = 9.81
IN, LBF = 25.4, 4.4482216
DRIVER_KG = 70.0          # assumption: the Adams full-vehicle mass has no driver
REAR_FRACTION = 0.60      # docs/test-plan.md
OUT_HZ, SIM_HZ, T_END = 1000, 20000, 10.0


def curve(name):
    xs, ys = [], []
    with open(ADAMS / "mini_baja_curves.csv") as f:
        for r in csv.DictReader(f):
            if r["File"] == name:
                xs.append(float(r["x"])); ys.append(float(r["y"]))
    return np.array(xs), np.array(ys)


def hardpoints():
    hp = {}
    with open(ADAMS / "mini_baja_hardpoints.csv") as f:
        for r in csv.DictReader(f):
            if r["Subsystem"] == "baja_dw_rear.sub":
                hp[r["Hardpoint"]] = np.array([float(r["x (mm)"]), float(r["y (mm)"]), float(r["z (mm)"])])
    return hp


def motion_ratio(hp):
    """shock length change per unit wheel travel, from the LCA and strut hardpoints"""
    axis = hp["lca_rear"] - hp["lca_front"]
    axis /= np.linalg.norm(axis)

    def dist_to_axis(p):
        v = p - hp["lca_front"]
        return np.linalg.norm(v - axis * (v @ axis))

    lever = dist_to_axis(hp["lwr_strut_mount"]) / dist_to_axis(hp["lca_outer"])
    shock = hp["top_mount"] - hp["lwr_strut_mount"]
    return lever * abs(shock[2]) / np.linalg.norm(shock)


def tire_rear():
    with open(ADAMS / "mini_baja_tires.csv") as f:
        rows = {(r["Block"], r["Parameter"]): r for r in csv.DictReader(f)}
    k = float(rows[("PARAMETER", "VERTICAL_STIFFNESS")]["baja_rear.tir"])     # N/mm
    c = float(rows[("PARAMETER", "VERTICAL_DAMPING")]["baja_rear.tir"])       # N s/mm
    return k, c


def masses():
    with open(ADAMS / "mini_baja_assemblies.csv") as f:
        total = next(float(r["Subsystem mass (kg)"]) for r in csv.DictReader(f)
                     if r["Assembly"] == "baja_full_vehicle_rdw.asy" and r["Major role"] == "TOTAL")
    parts = {}
    with open(ADAMS / "mini_baja_parts.csv") as f:
        for r in csv.DictReader(f):
            if r["Subsystem"] in ("baja_dw_rear.sub", "baja_rear_tires.sub"):
                parts[r["Part"]] = float(r["Mass each (kg)"])
    # whole parts that move with the wheel, plus half of each link that spans body and wheel
    unsprung = (parts["wheel"] + parts["upright"] + parts["spindle"] + parts["tripot"]
                + 0.5 * (parts["drive_shaft"] + parts["lower_control_arm"] + parts["upper_control_arm"]
                         + parts["lower_strut"]))
    corner = (total + DRIVER_KG) * REAR_FRACTION / 2
    return corner, unsprung, total


def preload_and_clearance():
    pre, clr = None, None
    with open(ADAMS / "mini_baja_components.csv") as f:
        for row in csv.reader(f):
            if row[0] == "baja_dw_rear.sub" and row[2] == "ride_spring":
                pre = float(row[5].split("USER_VALUE=")[1])
            if row[0] == "baja_dw_rear.sub" and row[2] == "jounce_stop":
                clr = float(row[5].split("USER_DISTANCE=")[1])
    return pre, clr


def event_speed():
    with open(ADAMS / "mini_baja_property_values.csv") as f:
        for row in csv.reader(f):
            if row[0].endswith("Speed_15mph.dcf") and row[2] == "INITIAL_SPEED":
                return float(row[3]) / 1000.0     # m/s


def road(t, v):
    """road height (mm) under the rear wheel at time t"""
    x = v * t
    rng = np.random.default_rng(7)
    k = np.arange(1, 40)
    amp = 3.0 / k                      # rough ground, a few mm
    phase = rng.uniform(0, 2 * np.pi, k.size)
    h = (amp[:, None] * np.sin(2 * np.pi * k[:, None] * x[None, :] / 2.0 + phase[:, None])).sum(0)
    bx = v * 1.0                       # 100 mm half-sine bump, 0.6 m long, at t = 1 s
    on = (x >= bx) & (x <= bx + 0.6)
    h[on] += 100 * np.sin(np.pi * (x[on] - bx) / 0.6)
    w0, w1 = v * 2.5, v * 4.5          # whoops, 60 mm, 1.6 m wavelength
    on = (x >= w0) & (x <= w1)
    h[on] += 30 * (1 - np.cos(2 * np.pi * (x[on] - w0) / 1.6))
    h[x >= v * 5.0] -= 500             # 0.5 m drop at t = 5 s
    return h


def braking(t):
    """0..1 braking demand, 7.0-8.2 s with smooth ramps"""
    b = np.clip((t - 7.0) / 0.2, 0, 1) * np.clip((8.2 - t) / 0.2, 0, 1)
    return b * b * (3 - 2 * b)


def main():
    hp = hardpoints()
    mr = motion_ratio(hp)
    kt, ct = tire_rear()
    corner, mu, total = masses()
    ms = corner - mu
    preload, bump_clear = preload_and_clearance()
    v = event_speed()

    sx, sf = curve("springs.tbl/baja_rear_spring.spr"); sx, sf = sx * IN, sf * LBF          # mm, N
    dx, df = curve("dampers.tbl/baja_rear_damper.dpr"); dx, df = dx * IN, df * LBF          # mm/s, N
    bx, bf = curve("bumpstops.tbl/mdi_0001.bum")                                            # mm, N

    def shock_force(x, xd):
        """compression-positive shock force for compression x (mm from design) and rate xd (mm/s)"""
        f = preload + np.interp(x, sx, sf)                  # spring curve is odd; + = compression
        f -= np.interp(-xd, dx, df)                         # damper curve is + in extension
        f += np.interp(x - bump_clear, bx, bf, left=0.0)
        f -= 200.0 * np.maximum(-x - 110.0, 0) ** 1.5      # rebound (top-out) limit at 110 mm extension
        return f

    # static equilibrium
    xs = np.linspace(-150, 150, 30001)
    x0 = xs[np.argmin(np.abs(shock_force(xs, 0) * mr - ms * G))]
    tire0 = corner * G / kt

    n = int(T_END * SIM_HZ)
    dt = 1.0 / SIM_HZ
    t = np.arange(n) * dt
    r = road(t, v)
    r -= r[0]
    rd = np.gradient(r, dt)
    b = braking(t)
    zs = np.zeros(n); zu = np.zeros(n)
    vs = vu = 0.0
    zs_i = zu_i = 0.0
    out_rows = []
    step = SIM_HZ // OUT_HZ
    for i in range(n):
        x = x0 + (zu_i - zs_i) * mr
        xd = (vu - vs) * mr                                 # mm/s
        fs = shock_force(x, xd)
        fw = fs * mr
        defl = tire0 + r[i] - zu_i
        ft = max(0.0, kt * defl + ct * (rd[i] - vu)) if defl > 0 else 0.0
        transfer = -0.15 * corner * G * b[i]               # rear unloads under braking
        a_s = (fw + transfer - ms * G) / ms * 1000.0        # mm/s^2
        a_u = (ft - fw - mu * G) / mu * 1000.0
        vs += a_s * dt; vu += a_u * dt
        zs_i += vs * dt; zu_i += vu * dt
        if i % step == 0:
            fa = 1000.0 * b[i] * ft / (corner * G)          # LCA fore-aft, from the braking row
            toe = 445.0 * b[i] * ft / (corner * G)
            lca_ax, lca_n, uca_ax = 0.1974 * fs, 0.6374 * fs, -0.2313 * fs   # static-row ratios
            area, e = np.pi / 4 * (25.4 ** 2 - 22.1 ** 2), 207000.0
            ue = lambda f: f / (area * e) * 1e6             # leg force -> microstrain
            sens = 1.29e-3                                  # mV/V per microstrain (test plan)
            ch = [sens * ue(lca_ax / 2 + fa / 2), sens * ue(lca_ax / 2 - fa / 2),
                  sens * 0.263 * lca_n, sens * 0.263 * 0.97 * lca_n,
                  sens * ue(uca_ax / 2 + 0.1 * fa), sens * ue(uca_ax / 2 - 0.1 * fa),
                  toe / 500.0 * 1.0, fs / 8000.0 * 2.0]
            aux1 = 0.15 + 3.0 * (np.clip(x, -110, 140) + 110) / 250.0
            out_rows.append([t[i], r[i], x, xd, fs, ft, lca_ax, fa, lca_n, uca_ax, toe, *ch, aux1,
                             -0.8 * b[i], 0.0, 1.0 + a_s / 1000.0 / G,
                             0.0, 0.0, 0.0])

    out = np.array(out_rows)
    # pitch rate from the braking ramp (small, for the gyro column)
    out[:, -2] = -2.0 * np.gradient(braking(out[:, 0]), 1.0 / OUT_HZ)
    cols = ["t_s", "road_mm", "shock_travel_mm", "shock_vel_mm_s", "shock_force_n", "tire_load_n",
            "lca_axial_n", "lca_foreaft_n", "lca_normal_n", "uca_axial_n", "toe_link_n",
            *[f"ch{i}_mvv" for i in range(1, 9)], "aux1_v",
            "accel_x_g", "accel_y_g", "accel_z_g", "gyro_x_dps", "gyro_y_dps", "gyro_z_dps"]
    OUT.parent.mkdir(exist_ok=True)
    with open(OUT, "w", newline="") as f:
        f.write(",".join(cols) + "\n")
        for row in out:
            f.write(",".join(f"{v:.7g}" for v in row) + "\n")
    print(f"motion ratio {mr:.3f}, corner {corner:.1f} kg (unsprung {mu:.1f}), tire {kt} N/mm, "
          f"speed {v:.2f} m/s, static shock travel {x0:.1f} mm")
    print(f"peak shock force {out[:, 4].max():.0f} N, min {out[:, 4].min():.0f} N, "
          f"travel {out[:, 2].min():.0f}..{out[:, 2].max():.0f} mm -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
