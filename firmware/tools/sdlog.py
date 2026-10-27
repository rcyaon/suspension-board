"""Read a suspension logger file (RUNnnnnn.BIN, format in src/log_format.h).

    python3 firmware/tools/sdlog.py RUN00001.BIN            # summary
    python3 firmware/tools/sdlog.py RUN00001.BIN out.csv    # samples as CSV

Bridge columns are raw ADC codes and nominal mV/V (2.49 V excitation, 1.2 V
reference). Calibration and the offline frequency-response correction are
separate processing steps and are not applied here.
"""
import sys

import numpy as np

HEADER = 4096
BLOCK = 512
REC = np.dtype([("sample", "<u4"), ("t_us", "<u4"), ("br", "u1", (24,)), ("ads_status", "<u2"),
                ("flags", "<u2"), ("aux", "<u2", (5,)), ("accel", "<i2", (3,)), ("gyro", "<i2", (3,)),
                ("imu_temp", "<i2")])
EVT = np.dtype([("t_us", "<u4"), ("sample", "<u4"), ("code", "<u2"), ("a", "<u2"), ("b", "<u4")])
EVENT_NAMES = {1: "run_start", 2: "phase", 3: "shunt", 4: "drop", 5: "overrun", 6: "crc", 7: "can_sync_tx",
               8: "can_sync_rx", 9: "can_cmd_rx", 10: "run_stop", 11: "sd_error"}
PHASES = ["idle", "settle", "cal_start", "run", "cal_end"]
F_EXC, F_SHUNT, F_CRC, F_LATE, F_IMU_STALE = 1, 2, 32, 64, 128


def parse_header(raw):
    text = raw[:HEADER].split(b"\0", 1)[0].decode("ascii", "replace")
    meta, config = {}, {}
    body, _, cfg = text.partition("[config]\n")
    for line in body.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            meta[k] = v
    for line in cfg.splitlines():
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            config[k.strip()] = v.strip()
    return meta, config


class Log:
    def __init__(self, raw):
        if not raw.startswith(b"SUSPLOG=1"):
            raise ValueError("not a suspension logger file")
        self.meta, self.config = parse_header(raw)
        self.gain = np.array([int(g) for g in self.meta["gain"].split(",")])
        self.rate = float(self.meta["sample_rate_sps"])
        recs, evts, self.trailer, self.seq = [], [], {}, []
        for off in range(HEADER, len(raw) - BLOCK + 1, BLOCK):
            b = raw[off:off + BLOCK]
            kind, n = b[0], b[1]
            if kind == ord("D"):
                recs.append(np.frombuffer(b, REC, n, 8))
            elif kind == ord("E"):
                evts.append(np.frombuffer(b, EVT, n, 8))
            elif kind == ord("T"):
                for line in b[8:].split(b"\0", 1)[0].decode().splitlines():
                    k, _, v = line.partition("=")
                    self.trailer[k] = v
            else:
                continue
            self.seq.append(int.from_bytes(b[4:8], "little"))
        self.rec = np.concatenate(recs) if recs else np.zeros(0, REC)
        self.events = np.concatenate(evts) if evts else np.zeros(0, EVT)
        br = self.rec["br"].reshape(-1, 8, 3).astype(np.int32)
        codes = br[..., 0] | (br[..., 1] << 8) | (br[..., 2] << 16)
        self.codes = np.where(codes >= 1 << 23, codes - (1 << 24), codes)

    @property
    def t(self):
        """seconds since boot, unwrapped"""
        return np.unwrap(self.rec["t_us"].astype(np.float64), period=2.0 ** 32) * 1e-6

    def mvv(self, exc_volts=2.49):
        return self.codes * (1.2 / (self.gain * 2.0 ** 23)) / exc_volts * 1e3

    def phase(self):
        return (self.rec["flags"] >> 2) & 7

    def shunt_cal(self):
        """per trailer: start/end shunt increment in nV/V and drift in ppm"""
        out = {}
        for ch in range(1, 9):
            a, b, ppm = (int(x) for x in self.trailer[f"ch{ch}"].split(","))
            out[ch] = (a, b, ppm)
        return out


def main():
    raw = open(sys.argv[1], "rb").read()
    log = Log(raw)
    t = log.t
    print(f"{sys.argv[1]}: {len(log.rec)} samples, {t[-1] - t[0]:.2f} s at {log.rate} sps, gains {[int(g) for g in log.gain]}")
    gaps = np.diff(log.rec["sample"].astype(np.int64)) - 1
    print(f"dropped frames: {int(gaps[gaps > 0].sum())}, late reads: {int(((log.rec['flags'] & F_LATE) > 0).sum())}")
    for k in ("frames", "dropped", "gain_drift_flag"):
        print(f"trailer {k}: {log.trailer.get(k)}")
    if len(sys.argv) > 2:
        mvv = log.mvv()
        with open(sys.argv[2], "w") as f:
            f.write("sample,t_s,phase,exc,shunt," + ",".join(f"ch{i}_mvv" for i in range(1, 9))
                    + ",aux1,aux2,aux3,aux4,vbat_v,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n")
            for i, r in enumerate(log.rec):
                vals = [r["sample"], f"{t[i]:.6f}", PHASES[(r['flags'] >> 2) & 7], r["flags"] & 1,
                        (r["flags"] >> 1) & 1, *(f"{v:.6f}" for v in mvv[i]), *r["aux"][:4],
                        f"{r['aux'][4] / 4095 * 3.3 * 11:.2f}", *(f"{a / 2048:.4f}" for a in r["accel"]),
                        *(f"{g / 16.4:.2f}" for g in r["gyro"])]
                f.write(",".join(str(v) for v in vals) + "\n")
        print(f"wrote {sys.argv[2]}")


if __name__ == "__main__":
    main()
