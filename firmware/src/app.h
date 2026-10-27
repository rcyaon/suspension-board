#ifndef APP_H
#define APP_H
#include <stdint.h>

#define FW_VERSION "0.1.0"

void app_init(void);
void app_poll(void);            /* call forever from main() */

/* requests (console, CAN or test harness) */
void app_request_start(void);
void app_request_stop(void);

/* state for status reporting and tests */
typedef struct {
    int state;                  /* RS_* */
    uint32_t frames, dropped, crc_err, late, overrun, ring_max, sd_errors;
    uint32_t sd_block_us_max;
    uint32_t runs;
    int last_error;
} app_stats;
const app_stats *app_get_stats(void);

enum { RS_IDLE, RS_SETTLE, RS_CAL_START, RS_RUN, RS_CAL_END, RS_CLOSING };

#endif
