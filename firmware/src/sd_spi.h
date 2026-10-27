#ifndef SD_SPI_H
#define SD_SPI_H
#include <stdint.h>

/* Blocking calls: only while the ADC interrupt is off (before/after a run). */
int sd_init(void);
int sd_read(uint32_t lba, uint8_t *buf);
int sd_write(uint32_t lba, const uint8_t *buf);

/* Bounded steps for use during acquisition. Each selects the card, does one
 * short transaction, deselects it and clocks one byte to release MISO.
 * Caller owns SPI1 and has selected SD mode. Return 0 ok, 1 busy, <0 error. */
int sd_step_busy(void);                        /* 0 = ready, 1 = still busy */
int sd_step_cmd25(uint32_t lba);               /* open multi-block write */
int sd_step_block(const uint8_t *buf);         /* one 512-byte block in the stream */
int sd_step_stop(void);                        /* stop-tran token */
int sd_step_cmd24(uint32_t lba, const uint8_t *buf);   /* single block */

#endif
