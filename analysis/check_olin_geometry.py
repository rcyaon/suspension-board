"""check the olin hardpoints against the olin kinematic sweeps.

the sweep sheets (toe through travel, steer angle through rack travel) came out of the
suspension tool that the hardpoints were entered into. if the hardpoints in this repo are
the ones that made those sweeps, the static toe and the slopes at ride height must match.
a mismatch means a typo in a hardpoint or a sheet from a different revision.

small-displacement kinematics at ride height only; no sweep is re-run.

run: python3 analysis/check_olin_geometry.py
"""
import csv

import numpy as np

from expected_loads import DATA, arm_frame, load_hardpoints, unit


def read_sweep(name, col=1, skip=0):
    with open(DATA / name) as f:
        rows = list(csv.reader(f))[skip:]
    return {float(r[0]): float(r[col]) for r in rows}


def slope_at_zero(sweep):
    return (sweep[2.0] - sweep[-2.0]) / 4.0


def static_angles(hp, end):
    """sae toe (+ = toe-in) and camber (+ = top out) of the left wheel, deg, from the spindle axis."""
    s = unit(hp[(end, "wheel_center")] - hp[(end, "spindle")])     # outward, -y
    toe = -np.degrees(np.arctan2(s[0], -s[1]))
    camber = -np.degrees(np.arctan2(s[2], -s[1]))
    return toe, camber


def upright_twist(hp, end, rows):
    """solve the upright's instantaneous twist (omega, v at origin) from 6 velocity constraints."""
    A = np.zeros((6, 6))
    b = np.zeros(6)
    for i, (r, d, rhs) in enumerate(rows):
        # d . (v + omega x r) = d . v + omega . (r x d)
        A[i, :3] = np.cross(r, d)
        A[i, 3:] = d
        b[i] = rhs
    x = np.linalg.solve(A, b)
    return x[:3], x[3:]


def bump_rates(hp, end):
    """toe and camber change per mm of wheel-centre travel at ride height, lca rotating at 1 rad."""
    p = lambda n: hp[(end, n)]
    la, _, _, lfoot = arm_frame(p("lca_front"), p("lca_rear"), p("lca_outer"))
    ua, _, _, ufoot = arm_frame(p("uca_front"), p("uca_rear"), p("uca_outer"))
    v_l = np.cross(la, p("lca_outer") - lfoot)
    tr = p("tierod_outer") - p("tierod_inner")
    rows = [(p("lca_outer"), e, v_l @ e) for e in np.eye(3)]
    rows += [(p("uca_outer"), ua, 0.0), (p("uca_outer"), p("uca_outer") - ufoot, 0.0), (p("tierod_outer"), tr, 0.0)]
    w, v = upright_twist(hp, end, rows)
    dz = (v + np.cross(w, p("wheel_center")))[2]
    # left wheel: +rotation about z swings the outward spindle rearward = toe-out; about x tilts the top out
    return np.degrees(-w[2] / dz), np.degrees(w[0] / dz)


def steer_rate(hp, end):
    """toe change per mm of inner tie rod travel in +y, wheel height fixed (upright turns about the kingpin)."""
    p = lambda n: hp[(end, n)]
    k = unit(p("uca_outer") - p("lca_outer"))
    tr = p("tierod_outer") - p("tierod_inner")
    # outer joint moves with the upright, inner joint moves 1 mm in y; rod length fixed
    v_per_w = np.cross(k, p("tierod_outer") - p("lca_outer"))
    w = (np.array([0, 1.0, 0]) @ tr) / (v_per_w @ tr)
    return np.degrees(-w * k[2])


def main():
    hp = load_hardpoints()
    toe_sweep = read_sweep("olin_baja_front_toe_vs_travel.csv", skip=2)
    steer_sweep = read_sweep("olin_baja_steer_angle_vs_rack.csv")

    for end in ("front", "rear"):
        toe, camber = static_angles(hp, end)
        dtoe, dcamber = bump_rates(hp, end)
        kp = unit(hp[(end, "uca_outer")] - hp[(end, "lca_outer")])
        incl = np.degrees(np.arctan2(kp[1], kp[2]))      # + = top inboard (+y on the left wheel)
        caster = np.degrees(np.arctan2(kp[0], kp[2]))
        print(f"== {end} (left wheel, from hardpoints)")
        print(f"  static toe {toe:+.3f} deg, camber {camber:+.3f} deg, kingpin inclination {incl:.1f} deg, "
              f"caster {caster:+.1f} deg")
        print(f"  bump steer {dtoe * 1000:+.3f} mdeg/mm, camber gain {dcamber * 1000:+.2f} mdeg/mm (+ = bump)")
        if end == "front":
            s_toe, s_dtoe = toe_sweep[0.0], slope_at_zero(toe_sweep)
            s_steer = slope_at_zero(steer_sweep)
            h_steer = steer_rate(hp, end)
            print(f"  toe sheet:   static {s_toe:+.3f} deg, bump steer {s_dtoe * 1000:+.3f} mdeg/mm")
            print(f"  steer sheet: static {steer_sweep[0.0]:+.3f} deg, {s_steer:+.4f} deg per mm rack;"
                  f" hardpoints {h_steer:+.4f} deg per mm of +y inner tie rod")
            ok = (abs(toe - s_toe) < 0.02 and abs(dtoe - s_dtoe) < 0.2 * abs(s_dtoe) + 1e-4
                  and abs(abs(h_steer) - abs(s_steer)) < 0.05 * abs(s_steer))
            print(f"  {'MATCH' if ok else 'MISMATCH'}: hardpoints vs sweeps")
        print()


if __name__ == "__main__":
    main()
