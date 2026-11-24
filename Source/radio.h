#ifndef RADIO_H
#define RADIO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { BW_NARROW, BW_WIDE, BW_ULTRANARROW, BW_COUNT } Bandwidth;
typedef enum { TONE_OFF, TONE_CTCSS, TONE_DCS, TONE_DCS_INV, TONE_COUNT } ToneMode;
enum { RADIO_RSSI_UNAVAILABLE = -161 };

typedef struct {
    uint8_t mode;
    uint8_t index;
} Tone;

typedef struct {
    uint32_t hz;
    Tone rx_tone;
    Tone tx_tone;
    uint8_t rx_bw;
    uint8_t tx_bw;
    uint8_t squelch;
    uint8_t power;
} RadioConfig;

bool radio_frequency_valid(uint32_t hz);
bool radio_config_valid(const RadioConfig *config);
unsigned radio_tone_count(ToneMode mode);
void radio_tone_label(Tone tone, char text[12]);
void radio_init(void);
void radio_idle(void);
void radio_receive(const RadioConfig *config, bool packet);
void radio_transmit(const RadioConfig *config, bool packet);
void radio_poll(void);
bool radio_busy(void);
bool radio_audio_open(void);
int radio_rssi(void);

#endif
