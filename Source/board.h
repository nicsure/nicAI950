#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "at32a403a.h"

#define PIN(n) (1u << (n))
#define GPIO_HIGH(port, pins) ((port)->scr = (uint32_t)(pins))
#define GPIO_LOW(port, pins) ((port)->clr = (uint32_t)(pins))
#define GPIO_READ(port, pins) (((port)->idt & (pins)) != 0)

#define BK_SELECT_A PIN(8)
#define BK_SELECT_B PIN(15)
#define BK_CLOCK PIN(10)
#define BK_DATA PIN(11)

typedef enum {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_MENU, KEY_ABC, KEY_EXIT, KEY_VM, KEY_HASH, KEY_UP, KEY_DOWN,
    KEY_LEFT, KEY_RIGHT, KEY_STAR, KEY_SK1, KEY_SK2, KEY_SK3, KEY_NONE
} Key;

#define AUDIO_RATE 9600u
#define AUDIO_IDLE 2048u

void board_init(void);
uint32_t board_ms(void);
void board_delay_ms(uint32_t ms);
void board_delay_us(uint32_t us);
void board_pin_mode(gpio_type *port, uint16_t pins, gpio_mode_type mode,
                    gpio_pull_type pull, gpio_output_type output);
void board_backlight(unsigned percent);
void board_audio(bool acquire, bool speaker);
void board_apc(uint16_t level);
bool board_headset(void);
bool board_ptt(void);
bool board_power_on(void);
void board_power_release(void);
uint16_t board_battery_mv(void);
Key board_key(void);
void board_gps_power(bool on);
int board_gps_get(void);

/* The packet module supplies these callbacks for the sample timer. */
void packet_sample_in(uint16_t sample);
uint16_t packet_sample_out(void);
void board_packet_audio(bool receive, bool transmit);

#endif
