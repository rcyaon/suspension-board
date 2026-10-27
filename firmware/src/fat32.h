#ifndef FAT32_H
#define FAT32_H
#include <stdint.h>

typedef struct {
    char name[12];          /* 8.3 as "RUN0001 BIN" */
    uint32_t first_clus, nclus;
    uint32_t first_lba;     /* file data is contiguous from here */
    uint32_t nblocks;       /* preallocated 512-byte blocks */
    uint32_t dir_lba, dir_off;
    uint8_t dir_sector[512];
} fat_file;

int fat_mount(void);
/* read a root-directory file (8.3 name, e.g. "CONFIG  TXT"); returns bytes or <0 */
int fat_read_file(const char *name83, uint8_t *buf, uint32_t max);
/* create the next RUNnnnn.BIN, contiguous, preallocated to `bytes` */
int fat_create_run(fat_file *f, uint32_t bytes, uint16_t fat_date, uint16_t fat_time);
/* directory sector with the size field updated, for writing during a run */
void fat_dir_with_size(fat_file *f, uint32_t size);
/* final size, release the unused preallocated clusters */
int fat_close(fat_file *f, uint32_t size);

#endif
