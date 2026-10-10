/* input_hw.c - direct-GPIO + ADC control backend (mechanism confirmed by RE).
 * Footswitches/buttons/encoder: GPIO PIN reads (active-low, internal pull-ups).
 * LEDs: GPIO SET/CLR. Pedals/volpot: polled ADC. Pin numbers come from
 * hx_pinmap.h (fill from the board). */
#include "input.h"
#include "hx_pinmap.h"
#include "../lpc4350.h"
#include "../adc.h"

static inline int rd(gpio_pin g){ return (GPIO_PIN(g.port) >> g.pin) & 1; }
static inline void mk_input(gpio_pin g){
    GPIO_DIR(g.port) &= ~(1u<<g.pin);
    SCU_SFS(g.port,g.pin) = SCU_FUNC(0)|SCU_EPUN /* pull-up on (EPUN=disable pd) */|SCU_EZI|SCU_ZIF;
}
static inline void mk_output(gpio_pin g){
    SCU_SFS(g.port,g.pin) = SCU_FUNC(0)|SCU_EPUN;
    GPIO_DIR(g.port) |= (1u<<g.pin);
}

int input_hw_init(void){
    for (int i=0;i<8;i++){ mk_input(FSW_PIN[i]); mk_output(LED_PIN[i]); }
    for (int i=0;i<4;i++)  mk_input(BTN_PIN[i]);
    mk_input(ENC_A_PIN[0]); mk_input(ENC_B_PIN[0]); mk_input(ENC_SW_PIN[0]);
    adc_init(ADC0_BASE, 204000000u);
    adc_init(ADC1_BASE, 204000000u);
    return 0;
}

void input_hw_read(uint8_t fsw[N_FOOTSW], uint8_t btn[N_BUTTON],
                   uint8_t ab[N_ENCODER], uint8_t push[N_ENCODER],
                   uint16_t pedal[N_PEDAL]){
    for (int i=0;i<N_FOOTSW;i++) fsw[i] = !rd(FSW_PIN[i]);      /* active-low */
    for (int i=0;i<N_BUTTON;i++) btn[i] = !rd(BTN_PIN[i]);
    ab[0]   = (uint8_t)((rd(ENC_A_PIN[0])<<1) | rd(ENC_B_PIN[0]));
    push[0] = !rd(ENC_SW_PIN[0]);
    for (int i=0;i<N_PEDAL;i++){
        uint32_t base = PEDAL_ADC[i] ? ADC1_BASE : ADC0_BASE;
        pedal[i] = adc_read(base, PEDAL_CH[i]);
    }
}

/* LED control (GPIO SET/CLR). */
void led_set(int i, int on){
    if (i<0||i>=8) return;
    if (on) GPIO_SET(LED_PIN[i].port) = (1u<<LED_PIN[i].pin);
    else    GPIO_CLR(LED_PIN[i].port) = (1u<<LED_PIN[i].pin);
}
