/* post.h - power-on self-test framework for the HX Stomp bring-up firmware. */
#ifndef HX_POST_H
#define HX_POST_H
#include <stdint.h>

typedef enum { POST_PASS = 0, POST_FAIL = 1, POST_SKIP = 2 } post_result;

typedef struct {
    const char *name;
    post_result result;
    uint32_t    detail;     /* measured value / error code, test-specific */
} post_entry;

#define POST_MAX 24
typedef struct { post_entry e[POST_MAX]; int n; } post_log;

void post_add(post_log *L, const char *name, post_result r, uint32_t detail);

/* Generic subsystem checks (no device-specific pins required). */
post_result post_clock (post_log *L);   /* read PLL1/BASE_M4, report M4 clock kHz */
post_result post_chipid(post_log *L);   /* read device/chip id from CREG */
post_result post_sdram (post_log *L);   /* march test over external SDRAM       */

#endif
