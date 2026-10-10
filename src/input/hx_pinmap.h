/* hx_pinmap.h - HX Stomp XL board control pin map.
 *
 * The stock firmware reads all digital controls via DIRECT GPIO (confirmed:
 * GPIO HAL table with DIR/PIN/SET/CLR for all 8 ports) and reads
 * the volume pot + expression pedals via polled ADC0/ADC1 (confirmed). What is
 * NOT in the firmware as constants is the per-control port/pin and ADC channel
 * -- those are applied as board config. The values below are PLACEHOLDERS with
 * the correct structure; fill each {port,pin} and channel from the schematic or
 * by probing (the factory "Button Test" walks one control at a time). Mechanism
 * is correct; only these numbers need confirming. */
#ifndef HX_PINMAP_H
#define HX_PINMAP_H
#include <stdint.h>

typedef struct { uint8_t port, pin; } gpio_pin;

/* ---- TODO: set to real board pins ---- */
#define PIN_TODO(port,pin) { (port), (pin) }

static const gpio_pin FSW_PIN[8] = {   /* footswitches 1..8, active-low inputs */
    PIN_TODO(0,0), PIN_TODO(0,1), PIN_TODO(0,2), PIN_TODO(0,3),
    PIN_TODO(1,0), PIN_TODO(1,1), PIN_TODO(1,2), PIN_TODO(1,3),
};
static const gpio_pin BTN_PIN[4] = {   /* mode/page/menu/tap buttons */
    PIN_TODO(2,0), PIN_TODO(2,1), PIN_TODO(2,2), PIN_TODO(2,3),
};
static const gpio_pin ENC_A_PIN[1] = { PIN_TODO(3,0) };   /* encoder phase A */
static const gpio_pin ENC_B_PIN[1] = { PIN_TODO(3,1) };   /* encoder phase B */
static const gpio_pin ENC_SW_PIN[1]= { PIN_TODO(3,2) };   /* encoder push     */
static const gpio_pin LED_PIN[8]   = {  /* footswitch LEDs (active-high out)  */
    PIN_TODO(4,0), PIN_TODO(4,1), PIN_TODO(4,2), PIN_TODO(4,3),
    PIN_TODO(5,0), PIN_TODO(5,1), PIN_TODO(5,2), PIN_TODO(5,3),
};
/* pedals/volpot: {ADC base selector 0/1, channel}. */
static const uint8_t PEDAL_ADC[3]  = { 1, 1, 1 };   /* ADC1 */
static const uint8_t PEDAL_CH[3]   = { 0, 1, 2 };   /* channels 0..2 */

#endif
