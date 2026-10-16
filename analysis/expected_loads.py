"""expected arm loads for the mini baja double wishbone, from the adams export.

hand calc, quasi-static, rigid parts. reads hardpoints from ../data/adams-mini-baja
and solves upright equilibrium for a set of contact-patch load cases, then
resolves the ball joint forces into the arm frame the gauges measure in.

run: python3 analysis/expected_loads.py
"""
import csv
from pathlib import Path

import numpy as np

DATA = Path(__file__).resolve().parent.parent / "data" / "adams-mini-baja"
G = 9.81

# --- assumptions: change these once the car is weighed and the tubes measured ---
VEHICLE_MASS_KG = 285.0      # model 256.6 kg - 51 kg template subframes + 80 kg driver
REAR_FRACTION = 0.60         # rear-engine baja, guess until corner weights exist
UNSPRUNG_KG = {"front": 14.0, "rear": 17.0}   # wheel 10 + upright + spindle + half the arms
TIRE_RADIUS_MM = 225.0       # baja_front_fiala.tir, used front and on rdw rear
MU = 0.85                    # fiala UMAX, caps lateral and longitudinal force
TUBE_OD_MM, TUBE_WALL_MM = 25.4, 1.65   # 1.0 x 0.065 in 4130, typical baja arm
E_MPA = 207000.0             # steel, from the model's material block
YIELD_MPA = 435.0            # 4130 normalized, as in double-wishbone-fatigue.md

SUBSYSTEMS = {"front": "baja_front_suspension.sub", "rear": "baja_dw_rear.sub"}


def load_hardpoints():
    pts = {}
    with open(DATA / "mini_baja_hardpoints.csv") as f:
        for row in csv.DictReader(f):
            key = (row["Subsystem"], row["Hardpoint"])
            pts[key] = np.array([float(row["x (mm)"]), float(row["y (mm)"]), float(row["z (mm)"])])
    return pts


def unit(v):
    return v / np.linalg.norm(v)


def arm_frame(front_pivot, rear_pivot, outer):
    """axis along the pivot line, 'along' from the pivot line to the ball joint, normal to the arm plane."""
    a = unit(rear_pivot - front_pivot)
    foot = front_pivot + np.dot(outer - front_pivot, a) * a
    along = unit(outer - foot)
    normal = np.cross(a, along)
    if normal[2] < 0:
        normal = -normal
    return a, along, normal, foot


def solve_corner(hp, sub, f_cp, at_wheel_center=False):
    p = lambda name: hp[(sub, name)]
    wc = p("wheel_center")
    cp = wc - np.array([0, 0, TIRE_RADIUS_MM])
    load_pt = wc if at_wheel_center else cp

    ua, ualong, _, ufoot = arm_frame(p("uca_front"), p("uca_rear"), p("uca_outer"))
    la, lalong, lnorm, lfoot = arm_frame(p("lca_front"), p("lca_rear"), p("lca_outer"))
    tr_dir = unit(p("tierod_outer") - p("tierod_inner"))

    # unknowns: uca force = s1*ua + s2*ualong (in arm plane), lca force (3), tie rod tension (1)
    # all are forces *on the upright*. equilibrium of upright: sum F = -f_cp, sum M = -M_cp
    cols, pts = [ua, ualong, np.eye(3)[0], np.eye(3)[1], np.eye(3)[2], tr_dir], \
                [p("uca_outer")] * 2 + [p("lca_outer")] * 3 + [p("tierod_outer")]
    A = np.zeros((6, 6))
    for i, (d, r) in enumerate(zip(cols, pts)):
        A[:3, i] = d
        A[3:, i] = np.cross(r - wc, d)
    b = -np.concatenate([f_cp, np.cross(load_pt - wc, f_cp)])
    x = np.linalg.solve(A, b)

    f_uca_on_upright = x[0] * ua + x[1] * ualong
    f_lca_on_upright = x[2:5]
    tierod = x[5]

    # lca equilibrium about its pivot axis gives the shock force (shock mounts on the lca)
    s_dir = unit(p("top_mount") - p("lwr_strut_mount"))
    f_on_lca = -f_lca_on_upright
    m_bj = np.dot(np.cross(p("lca_outer") - lfoot, f_on_lca), la)
    m_unit_shock = np.dot(np.cross(p("lwr_strut_mount") - lfoot, s_dir), la)
    shock = m_bj / m_unit_shock   # + = compression (force on lca points away from top_mount)

    f_uca_on_arm = -f_uca_on_upright
    return {
        "lca_axial": np.dot(f_on_lca, lalong),
        "lca_foreaft": np.dot(f_on_lca, la),
        "lca_normal": np.dot(f_on_lca, lnorm),
        "uca_axial": np.dot(f_uca_on_arm, ualong),
        "uca_foreaft": np.dot(f_uca_on_arm, ua),
        "tierod": tierod,
        "shock": shock,
        "geom": (lfoot, lalong, p),
    }


def motion_ratio(hp, sub):
    """shock travel per wheel travel, from small rotation of the lca."""
    p = lambda name: hp[(sub, name)]
    la, _, _, lfoot = arm_frame(p("lca_front"), p("lca_rear"), p("lca_outer"))
    dth = 1e-4
    v_bj = np.cross(la * dth, p("lca_outer") - lfoot)
    v_sm = np.cross(la * dth, p("lwr_strut_mount") - lfoot)
    s_dir = unit(p("top_mount") - p("lwr_strut_mount"))
    return abs(np.dot(v_sm, s_dir) / v_bj[2])


def tube_strain(force_axial_n, moment_nmm):
    ro, ri = TUBE_OD_MM / 2, TUBE_OD_MM / 2 - TUBE_WALL_MM
    area = np.pi * (ro**2 - ri**2)
    inertia = np.pi / 4 * (ro**4 - ri**4)
    eps_ax = force_axial_n / area / E_MPA * 1e6
    eps_bend = moment_nmm * ro / inertia / E_MPA * 1e6
    return eps_ax, eps_bend


def main():
    hp = load_hardpoints()
    yield_ue = YIELD_MPA / E_MPA * 1e6
    print(f"assumed: {VEHICLE_MASS_KG:.0f} kg with driver, {REAR_FRACTION:.0%} rear, mu {MU}, "
          f"arm tube {TUBE_OD_MM} x {TUBE_WALL_MM} mm. 4130 yield = {yield_ue:.0f} ue\n")

    for end, sub in SUBSYSTEMS.items():
        frac = REAR_FRACTION if end == "rear" else 1 - REAR_FRACTION
        fz0 = VEHICLE_MASS_KG * G * frac / 2
        mr = motion_ratio(hp, sub)
        print(f"== {end} ({sub})  static corner Fz0 = {fz0:.0f} N, motion ratio (shock/wheel) = {mr:.2f}")

        # x is rearward in adams. braking force on the tire points rearward (+x), drive forward (-x).
        # lateral +y = toward the car centre for the left wheel (outside wheel, right-hand turn).
        cases = [
            ("static 1 g", [0, 0, fz0], False),
            ("bump 3 g", [0, 0, 3 * fz0], False),
            ("landing 5 g", [0, 0, 5 * fz0], False),
            ("brake mu", [MU * fz0, 0, fz0], False),
            ("drive mu", [-MU * fz0, 0, fz0], True),
            ("corner, outside", [0, MU * 1.6 * fz0, 1.6 * fz0], False),
            ("corner, inside", [0, -MU * 0.4 * fz0, 0.4 * fz0], False),
            ("bump 3 g + corner", [0, MU * 3 * fz0, 3 * fz0], False),
        ]
        hdr = f"{'case':<20}{'shock':>8}{'LCA ax':>8}{'LCA fa':>8}{'LCA nrm':>8}{'UCA ax':>8}{'UCA fa':>8}{'tierod':>8}" \
              f"{'LCA ue ax':>11}{'LCA ue bend':>13}{'UCA ue ax':>11}"
        print(hdr)
        print("-" * len(hdr))
        for name, f, at_wc in cases:
            r = solve_corner(hp, sub, np.array(f, float), at_wc)
            lfoot, lalong, p = r["geom"]
            # bending at the shock tab: normal ball joint force times the lever from tab to ball joint.
            # an a-arm splits this over two legs, so per-leg strain is about half.
            lever = np.dot(p("lca_outer") - p("lwr_strut_mount"), lalong)
            m_tab = r["lca_normal"] * lever
            lca_ax_ue, lca_bend_ue = tube_strain(r["lca_axial"] / 2, m_tab / 2)
            uca_ax_ue, _ = tube_strain(r["uca_axial"] / 2, 0)
            print(f"{name:<20}{r['shock']:>8.0f}{r['lca_axial']:>8.0f}{r['lca_foreaft']:>8.0f}{r['lca_normal']:>8.0f}"
                  f"{r['uca_axial']:>8.0f}{r['uca_foreaft']:>8.0f}{r['tierod']:>8.0f}"
                  f"{lca_ax_ue:>11.0f}{lca_bend_ue:>13.0f}{uca_ax_ue:>11.0f}")
        print("forces in N on the arm at its ball joint, arm frame. shock + = compression. "
              "strain per leg (force split over two legs).\n")


if __name__ == "__main__":
    main()
