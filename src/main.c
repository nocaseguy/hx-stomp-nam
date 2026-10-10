/* main.c - HX Stomp bring-up + DSP self-test application (from scratch).
 *
 * Boot flow (DFU-loaded into internal SRAM, or flashed to SPIFI):
 *   1. clock_init()   -> 204 MHz (confirmed from stock firmware)
 *   2. UART console   -> text POST report
 *   3. POST checks    -> clock, chip id, SDRAM, codec, display, DSP
 *   4. display        -> colored bars (green/red/yellow) per check + gradient
 *   5. DSP self-test  -> runs the biquad engine on a buffer, validates output
 *   6. idle loop
 *
 * Pin choices marked "(verify)" are standard LPC4350 mappings not individually
 * confirmed from the binary; adjust to the board once schematic/hardware is known. */
#include "lpc4350.h"
#include "init.h"
#include "delay.h"
#include "uart.h"
#include "i2c.h"
#include "cs4272.h"
#include "post.h"
#include "display.h"
#include "dsp/engine.h"
#include "input/input.h"
#include "input/led.h"
#include "flash.h"
#include "dfu.h"

#define DBG_UART  USART0_BASE
#define UART_PCLK 204000000u   /* BASE_UART0 from PLL1 */

static void console_pins(void) {
    /* USART0: P2_0 = U0_TXD (func1), P2_1 = U0_RXD (func1)  (verify) */
    SCU_SFS(2,0) = SCU_FUNC(1)|SCU_EPUN;
    SCU_SFS(2,1) = SCU_FUNC(1)|SCU_EPUN|SCU_EZI|SCU_ZIF;
}

/* On-device DSP self-test: prove the M4F FPU audio path runs and stays bounded. */
static post_result post_dsp(post_log *L) {
    static int32_t buf[128];              /* stereo interleaved: 64 frames */
    audio_engine e; engine_init(&e, 48000.0f); engine_set_gain_db(&e, 0.0f);
    for (int i=0;i<64;i++) {              /* ~1 kHz sine, half scale */
        int32_t s = (int32_t)(4000000.0f * __builtin_sinf(2.0f*3.14159265f*1000.0f*i/48000.0f));
        buf[2*i]=s; buf[2*i+1]=s;
    }
    engine_process_i32(&e, buf, 64);
    int ok=1; int32_t peak=0;
    for (int i=0;i<128;i++){ int32_t v=buf[i]; if(v<-8388608||v>8388607) ok=0;
                             int32_t a=v<0?-v:v; if(a>peak)peak=a; }
    if (peak==0) ok=0;                    /* must produce signal */
    post_add(L,"DSP engine self-test", ok?POST_PASS:POST_FAIL, (uint32_t)peak);
    return ok?POST_PASS:POST_FAIL;
}

static uint16_t bar_color(post_result r){
    return r==POST_PASS?RGB565(0,200,0):r==POST_FAIL?RGB565(220,0,0):RGB565(200,200,0);
}

int main(void) {
    uint32_t hz = clock_init();
#ifdef HX_SMOKE
    /* Safest first-boot test: clock + SDRAM + display gradient only. No GPIO to
     * unmapped pins, no I2C, no LEDs. Recoverable by power-cycle (RAM image). */
    console_pins(); uart_init(DBG_UART, UART_PCLK, 115200);
    uart_puts(DBG_UART, "\nHX smoke test: clock ok, bringing up SDRAM+display...\n");
    sdram_init();
    volatile uint32_t *m=(volatile uint32_t*)0x28000000; m[0]=0xA5A5A5A5;
    uart_puts(DBG_UART, m[0]==0xA5A5A5A5?"SDRAM ok\n":"SDRAM FAIL\n");
    display_init();
    for (int y=0;y<LCD_H;y++) for(int x=0;x<LCD_W;x++)
        display_px(x, y, RGB565(x*255/LCD_W, y*255/LCD_H, 128));
    uart_puts(DBG_UART, "display drawn. smoke test done.\n");
    for(;;){ delay_ms(500); }
#endif

    console_pins();
    uart_init(DBG_UART, UART_PCLK, 115200);
    uart_puts(DBG_UART, "\n=== HX Stomp custom firmware POST ===\n");
    uart_puts(DBG_UART, "core clock "); uart_puthex(DBG_UART, hz); uart_puts(DBG_UART, " Hz\n");

#ifdef HX_BACKUP_MODE
    sdram_init();
    uart_puts(DBG_UART, "FLASH BACKUP: streaming Intel HEX (capture to file, then tools/unhex.py)\n");
    flash_dump_ihex(DBG_UART, 0, FLASH_SIZE);
    uart_puts(DBG_UART, "BACKUP DONE\n");
    for (;;) { delay_ms(1000); }
#endif

    post_log L; L.n = 0;

    post_clock(&L);
    post_chipid(&L);

    sdram_init();
    post_sdram(&L);

    /* codec present on I2C1? */
    i2c_init(I2C1_BASE, hz, 100000);
    int cs = i2c_probe(I2C1_BASE, CS4272_I2C_ADDR);
    post_add(&L, "CS4272 codec I2C ACK", cs==0?POST_PASS:POST_FAIL, (uint32_t)cs);

    post_dsp(&L);

    /* Display: only meaningful if SDRAM (framebuffer) is up. */
    int sdram_ok = 0;
    for (int i=0;i<L.n;i++) if (L.e[i].name[0]=='S' && L.e[i].result==POST_PASS) sdram_ok=1;
    if (sdram_ok) {
        display_init();
        display_clear(RGB565(0,0,40));
        /* RGB gradient strip across the top: confirms panel + pixel format */
        for (int x=0;x<LCD_W;x++) display_fill_rect(x,0,1,30,RGB565(x*255/LCD_W,64,255-x*255/LCD_W));
        /* one bar per POST result down the screen */
        for (int i=0;i<L.n;i++) display_fill_rect(12, 40+i*18, LCD_W-24, 13, bar_color(L.e[i].result));
        post_add(&L, "Display test pattern", POST_PASS, 0);
    } else {
        post_add(&L, "Display (skipped: no SDRAM)", POST_SKIP, 0);
    }

    /* Text report */
    for (int i=0;i<L.n;i++) {
        uart_puts(DBG_UART, L.e[i].result==POST_PASS?"[PASS] ":
                            L.e[i].result==POST_FAIL?"[FAIL] ":"[SKIP] ");
        uart_puts(DBG_UART, L.e[i].name);
        uart_puts(DBG_UART, "  detail="); uart_puthex(DBG_UART, L.e[i].detail);
        uart_putc(DBG_UART, '\n');
    }
    uart_puts(DBG_UART, "POST complete.\n");

    /* ---- Interactive control monitor: confirm switches, knobs, buttons ---- */
    input_hw_init();
    /* LED chase to confirm all 8 footswitch LEDs light. */
    for (int i=0;i<8;i++){ led_set(i,1); delay_ms(60); led_set(i,0); }

    input_state in; input_init(&in);
    uart_puts(DBG_UART, "Monitor: press switches / turn knob / move pedals...\n");

    int32_t last_enc = 0; uint8_t last_fsw[N_FOOTSW] = {0};
    uint32_t frame = 0;
    for (;;) {
        input_poll(&in);
        /* mirror each footswitch to its LED: confirms read AND drive together */
        for (int i=0;i<N_FOOTSW;i++){
            uint8_t st = sw_state(&in.fsw[i]);
            led_set(i, st);
            if (st != last_fsw[i]) {
                uart_puts(DBG_UART, st?"FSW down #":"FSW up   #");
                uart_putc(DBG_UART, '1'+i); uart_putc(DBG_UART,'\n');
                last_fsw[i]=st;
            }
        }
        if (sw_state(&in.btn[N_BUTTON-1])) {   /* hold last button -> back to DFU */
            uart_puts(DBG_UART, "rebooting into DFU...\n");
            dfu_reboot();
        }
        if (in.enc[0].count != last_enc) {
            uart_puts(DBG_UART, "encoder="); uart_puthex(DBG_UART,(uint32_t)in.enc[0].count);
            uart_putc(DBG_UART,'\n'); last_enc=in.enc[0].count;
        }
        if ((frame++ & 0x3F) == 0 && sdram_ok) input_monitor_draw(&in);
        delay_ms(1);   /* ~1 kHz poll */
    }
}
