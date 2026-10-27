/* Minimal FAT32 for an append-only logger: root directory only, 512-byte
 * sectors, one contiguous preallocated file per run. The FAT chain for the
 * whole preallocation is written when the file is created and the directory
 * size is refreshed during the run, so a power cut leaves a readable file. */
#include "fat32.h"
#include "sd_spi.h"
#include "util.h"

static struct {
    uint32_t fat_lba, fat_sz, nfats, data_lba, spc, root_clus, nclus, fsinfo_lba;
} v;
static uint8_t sec[512];

static uint32_t clus_lba(uint32_t c) { return v.data_lba + (c - 2) * v.spc; }

int fat_mount(void) {
    if (sd_read(0, sec)) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -2;
    uint32_t vol = 0;
    if (!(get_le16(sec + 11) == 512 && (sec[0] == 0xEB || sec[0] == 0xE9))) {
        uint8_t type = sec[0x1BE + 4];
        if (type != 0x0B && type != 0x0C) return -3;      /* need a FAT32 partition */
        vol = get_le32(sec + 0x1BE + 8);
        if (sd_read(vol, sec)) return -1;
    }
    if (get_le16(sec + 11) != 512 || get_le16(sec + 17) != 0 || get_le32(sec + 36) == 0) return -4;
    v.spc = sec[13];
    v.fat_lba = vol + get_le16(sec + 14);
    v.nfats = sec[16];
    v.fat_sz = get_le32(sec + 36);
    v.root_clus = get_le32(sec + 44);
    v.fsinfo_lba = vol + get_le16(sec + 48);
    v.data_lba = v.fat_lba + v.nfats * v.fat_sz;
    uint32_t tot = get_le32(sec + 32);
    v.nclus = (tot - (v.data_lba - vol)) / v.spc;
    return 0;
}

static uint32_t fat_get(uint32_t c) {
    uint32_t lba = v.fat_lba + c / 128;
    if (sd_read(lba, sec)) return 0x0FFFFFFF;
    return get_le32(sec + (c % 128) * 4) & 0x0FFFFFFF;
}

/* walk the root directory; cb returns 1 to stop */
typedef int (*dir_cb)(uint8_t *ent, uint32_t lba, uint32_t off, void *ctx);
static int walk_root(dir_cb cb, void *ctx) {
    uint32_t c = v.root_clus;
    while (c >= 2 && c < 0x0FFFFFF8) {
        for (uint32_t s = 0; s < v.spc; s++) {
            uint32_t lba = clus_lba(c) + s;
            if (sd_read(lba, sec)) return -1;
            for (uint32_t off = 0; off < 512; off += 32) {
                int r = cb(sec + off, lba, off, ctx);
                if (r) return r;
            }
        }
        c = fat_get(c);
    }
    return 0;
}

struct find_ctx { const char *name; uint32_t clus, size; };
static int find_cb(uint8_t *e, uint32_t lba, uint32_t off, void *p) {
    (void)lba; (void)off;
    struct find_ctx *f = p;
    if (e[0] == 0) return 2;
    if (e[0] == 0xE5 || (e[11] & 0x0F) == 0x0F || (e[11] & 0x18)) return 0;
    if (u_memcmp(e, f->name, 11)) return 0;
    f->clus = ((uint32_t)get_le16(e + 20) << 16) | get_le16(e + 26);
    f->size = get_le32(e + 28);
    return 1;
}

int fat_read_file(const char *name83, uint8_t *buf, uint32_t max) {
    struct find_ctx f = {name83, 0, 0};
    if (walk_root(find_cb, &f) != 1) return -1;
    uint32_t n = f.size < max ? f.size : max, done = 0, c = f.clus;
    while (done < n && c >= 2 && c < 0x0FFFFFF8) {
        for (uint32_t s = 0; s < v.spc && done < n; s++) {
            if (sd_read(clus_lba(c) + s, sec)) return -2;
            uint32_t k = n - done < 512 ? n - done : 512;
            u_memcpy(buf + done, sec, k);
            done += k;
        }
        c = fat_get(c);
    }
    return (int)done;
}

struct scan_ctx { uint32_t max_run; uint32_t free_lba, free_off; int have_free; };
static int scan_cb(uint8_t *e, uint32_t lba, uint32_t off, void *p) {
    struct scan_ctx *s = p;
    if (e[0] == 0 || e[0] == 0xE5) {
        if (!s->have_free) { s->have_free = 1; s->free_lba = lba; s->free_off = off; }
        return e[0] == 0 ? 2 : 0;
    }
    if (!u_memcmp(e, "RUN", 3) && !u_memcmp(e + 8, "BIN", 3)) {
        uint32_t n = 0;
        for (int i = 3; i < 8; i++) {
            if (e[i] < '0' || e[i] > '9') return 0;
            n = n * 10 + (uint32_t)(e[i] - '0');
        }
        if (n > s->max_run) s->max_run = n;
    }
    return 0;
}

/* find `want` consecutive free clusters; returns first or 0 */
static uint32_t find_free_run(uint32_t want) {
    uint32_t run_start = 0, run_len = 0, cached = 0xFFFFFFFF;
    for (uint32_t c = 2; c < v.nclus + 2; c++) {
        uint32_t lba = v.fat_lba + c / 128;
        if (lba != cached) { if (sd_read(lba, sec)) return 0; cached = lba; }
        if ((get_le32(sec + (c % 128) * 4) & 0x0FFFFFFF) == 0) {
            if (!run_len) run_start = c;
            if (++run_len == want) return run_start;
        } else run_len = 0;
    }
    return 0;
}

/* set FAT entries [c0, c0+n) to a chain ending in EOC (chain=1) or to free */
static int fat_set_range(uint32_t c0, uint32_t n, int chain) {
    for (uint32_t f = 0; f < v.nfats; f++) {
        uint32_t c = c0, end = c0 + n;
        while (c < end) {
            uint32_t lba = v.fat_lba + f * v.fat_sz + c / 128;
            if (sd_read(lba, sec)) return -1;
            for (; c < end && (c / 128) == (lba - v.fat_lba - f * v.fat_sz); c++) {
                uint32_t val = !chain ? 0 : (c + 1 == end ? 0x0FFFFFFF : c + 1);
                uint8_t *p = sec + (c % 128) * 4;
                put_le32(p, (get_le32(p) & 0xF0000000) | val);
            }
            if (sd_write(lba, sec)) return -1;
        }
    }
    return 0;
}

static int fsinfo_unknown(void) {
    if (sd_read(v.fsinfo_lba, sec)) return -1;
    if (get_le32(sec) != 0x41615252) return 0;
    put_le32(sec + 488, 0xFFFFFFFF);   /* free count unknown: host recomputes */
    put_le32(sec + 492, 0xFFFFFFFF);
    return sd_write(v.fsinfo_lba, sec);
}

int fat_create_run(fat_file *f, uint32_t bytes, uint16_t fat_date, uint16_t fat_time) {
    struct scan_ctx s = {0, 0, 0, 0};
    if (walk_root(scan_cb, &s) < 0) return -1;
    if (!s.have_free) return -2;                  /* root directory cluster full */
    uint32_t cbytes = v.spc * 512;
    uint32_t want = (bytes + cbytes - 1) / cbytes;
    uint32_t c0 = 0;
    while (want >= 16 && !(c0 = find_free_run(want))) want /= 2;   /* fragmented card: smaller file */
    if (!c0) return -3;
    if (fat_set_range(c0, want, 1)) return -4;
    if (fsinfo_unknown()) return -4;

    uint32_t n = s.max_run + 1;
    u_snprintf(f->name, sizeof f->name, "RUN%05uBIN", (unsigned)(n % 100000));
    f->first_clus = c0; f->nclus = want;
    f->first_lba = clus_lba(c0); f->nblocks = want * v.spc;
    f->dir_lba = s.free_lba; f->dir_off = s.free_off;

    if (sd_read(f->dir_lba, f->dir_sector)) return -5;
    uint8_t *e = f->dir_sector + f->dir_off;
    int was_end = e[0] == 0;
    u_memset(e, 0, 32);
    u_memcpy(e, f->name, 11);
    e[11] = 0x20;
    put_le16(e + 14, fat_time); put_le16(e + 16, fat_date); put_le16(e + 18, fat_date);
    put_le16(e + 22, fat_time); put_le16(e + 24, fat_date);
    put_le16(e + 20, (uint16_t)(c0 >> 16)); put_le16(e + 26, (uint16_t)c0);
    if (was_end && f->dir_off + 32 < 512) f->dir_sector[f->dir_off + 32] = 0;
    return sd_write(f->dir_lba, f->dir_sector) ? -5 : 0;
}

void fat_dir_with_size(fat_file *f, uint32_t size) {
    put_le32(f->dir_sector + f->dir_off + 28, size);
}

int fat_close(fat_file *f, uint32_t size) {
    fat_dir_with_size(f, size);
    if (sd_write(f->dir_lba, f->dir_sector)) return -1;
    uint32_t cbytes = v.spc * 512;
    uint32_t used = (size + cbytes - 1) / cbytes;
    if (!used) used = 1;
    if (used < f->nclus) {
        if (fat_set_range(f->first_clus, used, 1)) return -2;
        if (fat_set_range(f->first_clus + used, f->nclus - used, 0)) return -2;
    }
    return 0;
}
