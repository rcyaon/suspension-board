"""Make and read FAT32 SD-card images for the simulator. This is written
separately from the firmware's fat32.c so each one checks the other.

    python3 fat32img.py make card.img CONFIG.TXT
    python3 fat32img.py get card.img RUN00001.BIN out.bin
"""
import struct
import sys

SEC = 512
PART_LBA = 2048
SIZE_MB = 64
RESERVED, NFATS, SPC = 32, 2, 1


def make(path, files):
    total = SIZE_MB * 1024 * 1024 // SEC
    vol = total - PART_LBA
    clusters = (vol - RESERVED) // SPC
    fat_sz = (clusters * 4 + 8 + SEC - 1) // SEC
    clusters = (vol - RESERVED - NFATS * fat_sz) // SPC
    assert clusters >= 65525, "too small for FAT32"
    img = bytearray(total * SEC)
    # MBR with one FAT32 LBA partition
    mbr = bytearray(SEC)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\0\0\0", 0x0C, b"\0\0\0", PART_LBA, vol)
    mbr[510:512] = b"\x55\xAA"
    img[0:SEC] = mbr
    bs = bytearray(SEC)
    bs[0:3] = b"\xEB\x58\x90"; bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SEC, SPC, RESERVED, NFATS, 0, 0, 0xF8, 0, 63, 255, PART_LBA, vol)
    struct.pack_into("<IHHIHH", bs, 36, fat_sz, 0, 0, 2, 1, 6)
    bs[64] = 0x80; bs[66] = 0x29; bs[71:82] = b"SUSPLOG    "; bs[82:90] = b"FAT32   "
    bs[510:512] = b"\x55\xAA"
    base = PART_LBA * SEC
    img[base:base + SEC] = bs
    fsi = bytearray(SEC)
    struct.pack_into("<I", fsi, 0, 0x41615252); struct.pack_into("<III", fsi, 484, 0x61417272, 0xFFFFFFFF, 3)
    fsi[510:512] = b"\x55\xAA"
    img[base + SEC:base + 2 * SEC] = fsi
    fat_lba = PART_LBA + RESERVED
    data_lba = fat_lba + NFATS * fat_sz
    fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF, 2: 0x0FFFFFFF}   # cluster 2 = root directory
    root = bytearray(SPC * SEC * 8)
    nxt = 3
    root_clusters = [2] + list(range(nxt, nxt + 7))      # 8-cluster root dir chain
    for a, b in zip(root_clusters, root_clusters[1:]):
        fat[a] = b
    fat[root_clusters[-1]] = 0x0FFFFFFF
    nxt += 7
    for i, (name, data) in enumerate(files):
        n = max(1, (len(data) + SPC * SEC - 1) // (SPC * SEC))
        chain = list(range(nxt, nxt + n)); nxt += n
        for a, b in zip(chain, chain[1:] + [0x0FFFFFFF]):
            fat[a] = b
        for k, c in enumerate(chain):
            off = (data_lba + (c - 2) * SPC) * SEC
            chunk = data[k * SPC * SEC:(k + 1) * SPC * SEC]
            img[off:off + len(chunk)] = chunk
        ent = struct.pack("<11sBBBHHHHHHHI", name, 0x20, 0, 0, 0, 0x5B5A, 0x5B5A, 0, 0, 0x5B5A, chain[0], len(data))
        root[32 * i:32 * i + 32] = ent
    for k, c in enumerate(root_clusters):
        off = (data_lba + (c - 2) * SPC) * SEC
        img[off:off + SPC * SEC] = root[k * SPC * SEC:(k + 1) * SPC * SEC]
    for f in range(NFATS):
        off = (fat_lba + f * fat_sz) * SEC
        for c, v in fat.items():
            struct.pack_into("<I", img, off + 4 * c, v)
    open(path, "wb").write(img)


class Image:
    def __init__(self, path):
        self.b = open(path, "rb").read()
        lba = struct.unpack_from("<I", self.b, 446 + 8)[0]
        bs = self.b[lba * SEC:(lba + 1) * SEC]
        self.spc = bs[13]
        res, nf = struct.unpack_from("<HB", bs, 14)
        self.fat_sz, = struct.unpack_from("<I", bs, 36)
        self.root, = struct.unpack_from("<I", bs, 44)
        self.fat_lba = lba + res
        self.nfats = nf
        self.data_lba = self.fat_lba + nf * self.fat_sz

    def fat(self, c, copy=0):
        return struct.unpack_from("<I", self.b, (self.fat_lba + copy * self.fat_sz) * SEC + 4 * c)[0] & 0x0FFFFFFF

    def chain(self, c):
        out = []
        while 2 <= c < 0x0FFFFFF8:
            out.append(c)
            c = self.fat(c)
            if len(out) > 10_000_000:
                raise ValueError("FAT loop")
        return out

    def clus(self, c):
        off = (self.data_lba + (c - 2) * self.spc) * SEC
        return self.b[off:off + self.spc * SEC]

    def entries(self):
        d = b"".join(self.clus(c) for c in self.chain(self.root))
        for i in range(0, len(d), 32):
            e = d[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x0F == 0x0F:
                continue
            name = e[0:8].decode().rstrip() + "." + e[8:11].decode().rstrip()
            clus = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
            yield name, clus, struct.unpack_from("<I", e, 28)[0]

    def read(self, name):
        for n, c, size in self.entries():
            if n == name:
                ch = self.chain(c)
                return b"".join(self.clus(x) for x in ch)[:size], ch
        raise FileNotFoundError(name)

    def fats_match(self):
        a = self.b[self.fat_lba * SEC:(self.fat_lba + self.fat_sz) * SEC]
        b = self.b[(self.fat_lba + self.fat_sz) * SEC:(self.fat_lba + 2 * self.fat_sz) * SEC]
        return a == b

    def used_clusters(self):
        n = self.fat_sz * SEC // 4
        fat = struct.unpack_from(f"<{n}I", self.b, self.fat_lba * SEC)
        return sum(1 for v in fat[2:] if v & 0x0FFFFFFF)


if __name__ == "__main__":
    if sys.argv[1] == "make":
        files = [(b"CONFIG  TXT", open(sys.argv[3], "rb").read())] if len(sys.argv) > 3 else []
        make(sys.argv[2], files)
    elif sys.argv[1] == "get":
        data, _ = Image(sys.argv[2]).read(sys.argv[3])
        open(sys.argv[4], "wb").write(data)
