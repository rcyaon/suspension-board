"""expected arm loads for the baja double wishbone.

hand calc, quasi-static, rigid parts. everything comes from the olin baja sheets in
../data/olin-baja: hardpoints from the front and rear suspension sheets, and vehicle
mass, weight split, cg height, wheelbase, brake/drive split and unsprung masses from
the global variables.

solves upright equilibrium for a set of contact-patch load cases, then resolves
the ball joint forces into the arm frame the gauges measure in.

run: python3 analysis/expected_loads.py
"""
import csv
from pathlib import Path

import numpy as np

DATA = Path(__file__).resolve().parent.parent / "data" / "olin-baja"
OLIN_GLOBALS = DATA / "olin_baja_global_variables.csv"
HARDPOINT_FILES = {"front": "olin_baja_front_hardpoints_mm.csv", "rear": "olin_baja_rear_hardpoints_mm.csv"}
G = 9.81

# --- assumptions not covered by the olin sheets ---
DRIVER_KG = 0.0              # added to the olin total mass. set to ~80 if that total excludes the driver
TIRE_RADIUS_MM = 295.0       # rear wheel centre height in the rear sheet, if that frame's z = 0 is the ground.
                             # ~23 in tire. no tire data in the sheets; confirm against the tire size
MU = 0.85                    # fiala UMAX, caps lateral and longitudinal force
TUBE_OD_MM, TUBE_WALL_MM = 25.4, 1.65   # 1.0 x 0.065 in 4130, typical baja arm
E_MPA = 207000.0             # steel, from the model's material block
YIELD_MPA = 435.0            # 4130 normalized, as in double-wishbone-fatigue.md

# the front and rear sheets list the same 18 points in the same order but name them differently,
# so map by row number (#). rows 11/12 repeat the damper ends; 15-18 are part cgs, not used.
HARDPOINT_ROWS = {
    1: "lca_front", 2: "lca_rear", 3: "lca_outer",
    4: "uca_front", 5: "uca_rear", 6: "uca_outer",
    7: "lwr_strut_mount", 8: "top_mount",
    9: "tierod_outer", 10: "tierod_inner",
    13: "spindle", 14: "wheel_center",
}


def load_olin_globals():
    with open(OLIN_GLOBALS) as f:
        raw = {row["Variable"].strip(): float(row["Value"]) for row in csv.DictReader(f)}
    mass = raw["Total Mass (kg)"] + DRIVER_KG
    front = raw["Total Mass Front (%)"] / 100
    return {
        "mass": mass,
        "frac": {"front": front, "rear": 1 - front},
        "cg_h": raw["Total Mass C of G Height (mm)"],
        "wheelbase": raw["Wheelbase (mm)"],
        "brake": {"front": raw["Braking Front (%)"] / 100, "rear": 1 - raw["Braking Front (%)"] / 100},
        "drive": {"front": raw["Drive Front (%)"] / 100, "rear": 1 - raw["Drive Front (%)"] / 100},
        # left wheel is what the hardpoints describe; warn if the car is not symmetric
        "unsprung": {"front": raw["Unsprung Mass FL (kg)"], "rear": raw["Unsprung Mass RL (kg)"]},
        "asym": raw["Unsprung Mass FL (kg)"] != raw["Unsprung Mass FR (kg)"]
                or raw["Unsprung Mass RL (kg)"] != raw["Unsprung Mass RR (kg)"],
    }


def load_hardpoints():
    """olin hardpoints in mm, keyed (end, name). x rearward, z up, as in the sheets.

    the front sheet is on the -y side and the rear sheet on +y; the rear is mirrored so both
    are the left (-y) corner. each sheet has its own origin, so do not mix front and rear points.
    """
    pts = {}
    for end, name in HARDPOINT_FILES.items():
        with open(DATA / name) as f:
            rows = list(csv.reader(f))[1:]       # rear mm header says (in); the values are mm
        for row in rows:
            key = HARDPOINT_ROWS.get(int(row[0]))
            if key:
                pts[(end, key)] = np.array([float(v) for v in row[2:5]])
        if pts[(end, "wheel_center")][1] > 0:
            for k in [k for k in pts if k[0] == end]:
                pts[k] = pts[k] * np.array([1, -1, 1])
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


def solve_corner(hp, end, f_cp, at_wheel_center=False, f_wc=np.zeros(3)):
    p = lambda name: hp[(end, name)]
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
    # f_wc (unsprung weight/inertia) acts at the wheel centre, so it has no moment about it
    b = -np.concatenate([f_cp + f_wc, np.cross(load_pt - wc, f_cp)])
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


def motion_ratio(hp, end):
    """shock travel per wheel travel, from small rotation of the lca."""
    p = lambda name: hp[(end, name)]
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


def load_cases(veh, end, track_mm):
    """contact-patch force [fx, fy, fz] on the left wheel and vertical g on the unsprung mass, per case.

    weight transfer from cg height and wheelbase; lateral transfer split by axle mass (no roll
    stiffness data), over the track from the hardpoints. brake and drive forces split by the
    olin percentages and capped at tire grip.
    """
    m, h, L = veh["mass"], veh["cg_h"], veh["wheelbase"]
    frac = veh["frac"][end]
    sign = 1 if end == "rear" else -1          # longitudinal transfer goes to the rear under accel
    fz0 = m * G * frac / 2

    # braking at the tire limit: decel mu g, load moves forward
    fz_b = fz0 - sign * m * MU * G * h / L / 2
    fx_b = min(veh["brake"][end] * m * MU * G / 2, MU * fz_b)

    # driving at the traction limit of whichever driven axle slips first
    d_f, d_r = veh["drive"]["front"], veh["drive"]["rear"]
    ff, fr = veh["frac"]["front"], veh["frac"]["rear"]
    limits = []
    if d_r > 0:
        limits.append(MU * G * fr / (d_r - MU * h / L) if d_r > MU * h / L else np.inf)
    if d_f > 0:
        limits.append(MU * G * ff / (d_f + MU * h / L))
    a_drive = min(limits)
    fz_d = fz0 + sign * m * a_drive * h / L / 2
    fx_d = veh["drive"][end] * m * a_drive / 2

    # cornering at mu g
    dfz = m * frac * MU * G * h / track_mm
    fz_out, fz_in = fz0 + dfz, max(fz0 - dfz, 0.0)

    # x is rearward in the olin sheets. braking force on the tire points rearward (+x), drive forward (-x).
    # lateral +y = toward the car centre for the left wheel (outside wheel, right-hand turn).
    cases = [
        ("static 1 g", [0, 0, fz0], 1, False),
        ("bump 3 g", [0, 0, 3 * fz0], 3, False),
        ("landing 5 g", [0, 0, 5 * fz0], 5, False),
        ("brake mu", [fx_b, 0, fz_b], 1, False),
        ("drive mu", [-fx_d, 0, fz_d], 1, True),
        ("corner, outside", [0, MU * fz_out, fz_out], 1, False),
        ("corner, inside", [0, -MU * fz_in, fz_in], 1, False),
        ("bump 3 g + corner", [0, MU * 3 * fz0, 3 * fz0], 3, False),
    ]
    return fz0, a_drive, cases


def main():
    hp = load_hardpoints()
    veh = load_olin_globals()
    yield_ue = YIELD_MPA / E_MPA * 1e6
    print(f"olin globals: {veh['mass']:.1f} kg (driver add {DRIVER_KG:.0f} kg), {veh['frac']['front']:.1%} front, "
          f"cg {veh['cg_h']:.0f} mm, wheelbase {veh['wheelbase']:.0f} mm, brake {veh['brake']['front']:.0%} front, "
          f"drive {veh['drive']['front']:.0%} front")
    print(f"assumed: mu {MU}, tire radius {TIRE_RADIUS_MM} mm, arm tube {TUBE_OD_MM} x {TUBE_WALL_MM} mm. "
          f"4130 yield = {yield_ue:.0f} ue")
    print("note: front and rear hardpoint sheets have separate origins; each corner is solved in its own frame.")
    if veh["asym"]:
        print("warning: left and right unsprung masses differ; using the left side.")
    print()

    for end in HARDPOINT_FILES:
        track = 2 * abs(hp[(end, "wheel_center")][1])
        fz0, a_drive, cases = load_cases(veh, end, track)
        mr = motion_ratio(hp, end)
        mu_kg = veh["unsprung"][end]
        print(f"== {end}  static corner Fz0 = {fz0:.0f} N, unsprung {mu_kg:.0f} kg, "
              f"track {track:.0f} mm, motion ratio (shock/wheel) = {mr:.2f}, drive limit {a_drive / G:.2f} g")

        hdr = f"{'case':<20}{'Fx':>7}{'Fy':>7}{'Fz':>7}{'shock':>8}{'LCA ax':>8}{'LCA fa':>8}{'LCA nrm':>8}" \
              f"{'UCA ax':>8}{'UCA fa':>8}{'tierod':>8}{'LCA ue ax':>11}{'LCA ue bend':>13}{'UCA ue ax':>11}"
        print(hdr)
        print("-" * len(hdr))
        for name, f, n_g, at_wc in cases:
            f_wc = np.array([0, 0, -n_g * mu_kg * G])
            r = solve_corner(hp, end, np.array(f, float), at_wc, f_wc)
            lfoot, lalong, p = r["geom"]
            # bending at the shock tab: normal ball joint force times the lever from tab to ball joint.
            # an a-arm splits this over two legs, so per-leg strain is about half.
            lever = np.dot(p("lca_outer") - p("lwr_strut_mount"), lalong)
            m_tab = r["lca_normal"] * lever
            lca_ax_ue, lca_bend_ue = tube_strain(r["lca_axial"] / 2, m_tab / 2)
            uca_ax_ue, _ = tube_strain(r["uca_axial"] / 2, 0)
            print(f"{name:<20}{f[0]:>7.0f}{f[1]:>7.0f}{f[2]:>7.0f}"
                  f"{r['shock']:>8.0f}{r['lca_axial']:>8.0f}{r['lca_foreaft']:>8.0f}{r['lca_normal']:>8.0f}"
                  f"{r['uca_axial']:>8.0f}{r['uca_foreaft']:>8.0f}{r['tierod']:>8.0f}"
                  f"{lca_ax_ue:>11.0f}{lca_bend_ue:>13.0f}{uca_ax_ue:>11.0f}")
        print("Fx/Fy/Fz: contact patch load, N. forces in N on the arm at its ball joint, arm frame. "
              "shock + = compression.\nunsprung weight (x the case g) taken off at the wheel centre. "
              "strain per leg (force split over two legs).\n")


if __name__ == "__main__":
    main()
