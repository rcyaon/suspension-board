#ifndef ADS131M08_H
#define ADS131M08_H
#include <stdint.h>

#define ADS_NCH 8
#define ADS_WORD 3                          /* 24-bit words (default) */
#define ADS_FRAME_BYTES (10 * ADS_WORD)     /* status + 8 ch + CRC */

typedef struct {
    uint16_t status;
    int32_t ch[ADS_NCH];
    int crc_ok;
} ads_frame;

/* gain codes are the PGA value (1..128); osr is 128..16256 */
int ads_init(uint16_t osr, const uint8_t gain[ADS_NCH]);
/* one data frame with a NULL command; caller owns SPI1 and has set ADC mode */
void ads_read_frame(ads_frame *f);
uint16_t ads_crc16(const uint8_t *p, uint32_t n);
int ads_osr_code(uint16_t osr);   /* -1 if unsupported */

#endif
