#include "radio.h"
#include "board.h"
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint8_t reg;
    uint16_t value;
} Register;

typedef enum { READ_UNVERIFIED, READ_BEFORE_FALL, READ_AFTER_FALL } ReadAlignment;

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* Captured baseline. Repeated writes, undocumented fields and ordering are intentional. */
static const Register startup[] = {
    {0x00, 0x8000}, {0x00, 0x0000}, {0x37, 0x9D1F}, {0x13, 0x03DF},
    {0x12, 0x03DB}, {0x11, 0x033A}, {0x10, 0x0318}, {0x14, 0x0210},
    {0x49, 0x2AB2}, {0x7B, 0x73DC}, {0x1C, 0x07C0}, {0x1D, 0xE555},
    {0x1E, 0x4C58}, {0x1F, 0x865A}, {0x3E, 0x94C6}, {0x3F, 0x07FE},
    {0x25, 0xC1BA}, {0x3A, 0x9A7C}, {0x19, 0x1041}, {0x28, 0x0B40},
    {0x29, 0xAA00}, {0x2A, 0x6600}, {0x2C, 0x0022}, {0x2F, 0x9890},
    {0x53, 0x2028}, {0x7E, 0x303E}, {0x46, 0x6050}, {0x4A, 0x5430},
    {0x48, 0xB3BF}, {0x49, 0x2AB2}, {0x4A, 0x5430}, {0x4D, 0xA004},
    {0x4E, 0x3815}, {0x4F, 0x3F3B}, {0x77, 0xCCEF}, {0x7E, 0x303E},
    {0x40, 0x34D2}, {0x7D, 0xE912}, {0x48, 0xB3FF}, {0x09, 0x006F},
    {0x09, 0x106B}, {0x09, 0x2067}, {0x09, 0x3062}, {0x09, 0x4050},
    {0x09, 0x5047}, {0x09, 0x603A}, {0x09, 0x702C}, {0x09, 0x8041},
    {0x09, 0x9037}, {0x09, 0xA025}, {0x09, 0xB017}, {0x09, 0xC0E4},
    {0x09, 0xD0CB}, {0x09, 0xE0B5}, {0x09, 0xF09F}, {0x74, 0xDF22},
    {0x44, 0x91C1}, {0x45, 0x3040}, {0x75, 0xE61C}, {0x54, 0x91C1},
    {0x55, 0x3040}, {0x47, 0x6042}, {0x48, 0xB2BF}, {0x30, 0x0000}
};
static const Register rx_setup[] = {
    {0x4F, 0x2A29}, {0x52, 0x0292}, {0x40, 0x350A}, {0x24, 0x87FF},
    {0x58, 0x3FC3}, {0x72, 0x3065}, {0x70, 0x00EC}, {0x5D, 0x0D00},
    {0x59, 0x4028}, {0x59, 0x1028}, {0x5A, 0xFB72}, {0x5B, 0x4099},
    {0x5C, 0xA730}, {0x48, 0xB33C}, {0x47, 0x6142}, {0x2B, 0x0000},
    {0x31, 0x0000}, {0x37, 0x9F1F}, {0x30, 0x0000}
};
static const Register tx_setup[] = {
    {0x70, 0x0000}, {0x58, 0x0000}, {0x3F, 0x0000}, {0x59, 0x0028},
    {0x70, 0x0000}, {0x58, 0x0000}, {0x70, 0x0000}, {0x58, 0x0000},
    {0x3F, 0x0000}, {0x59, 0x0028}, {0x70, 0x0000}, {0x58, 0x0000},
    {0x24, 0x87DF}, {0x70, 0x0000}, {0x31, 0x0000}, {0x40, 0x350A},
    {0x47, 0x6042}, {0x48, 0xB2BF}, {0x37, 0x9D1F}, {0x47, 0x6042},
    {0x48, 0xB2BF}, {0x37, 0x9D1F}, {0x30, 0x0000}, {0x47, 0x6142},
    {0x30, 0x0000}, {0x51, 0x0000}, {0x52, 0x0292}, {0x51, 0x0000},
    {0x7D, 0xE924}, {0x31, 0x0000}, {0x40, 0x350A}, {0x30, 0x0200},
    {0x24, 0x87DF}, {0x70, 0x0000}, {0x31, 0x0000}, {0x40, 0x350A},
    {0x31, 0x0000}, {0x7D, 0xE93F}, {0x36, 0x007F}, {0x36, 0x007F}
};
static const uint16_t ctcss[] = {
    670,693,719,744,770,797,825,854,885,915,948,974,1000,1035,1072,
    1109,1148,1188,1230,1273,1318,1365,1413,1462,1514,1567,1598,1622,
    1655,1679,1713,1738,1773,1799,1835,1862,1899,1928,1966,1995,2035,
    2065,2107,2181,2257,2291,2336,2418,2503,2541
};
static const uint16_t dcs[] = {
    0023,0025,0026,0031,0032,0036,0043,0047,0051,0053,0054,0065,0071,0072,0073,0074,
    0114,0115,0116,0122,0125,0131,0132,0134,0143,0145,0152,0155,0156,0162,0165,0172,0174,
    0205,0212,0223,0225,0226,0243,0244,0245,0246,0251,0252,0255,0261,0263,0265,0266,0271,0274,
    0306,0311,0315,0325,0331,0332,0343,0346,0351,0356,0364,0365,0371,
    0411,0412,0413,0423,0431,0432,0445,0446,0452,0454,0455,0462,0464,0465,0466,
    0503,0506,0516,0523,0526,0532,0546,0565,0606,0612,0624,0627,0631,0632,0654,0662,0664,
    0703,0712,0723,0731,0732,0734,0743,0754
};
static uint16_t selected;
static ReadAlignment read_alignment[2];
static bool relay_hf;
static uint32_t relay_changed;
static bool receiving, packet_mode, busy, open;
static uint8_t squelch;
static Tone rx_tone;
static int rssi = RADIO_RSSI_UNAVAILABLE;

bool radio_frequency_valid(uint32_t hz)
{
    return (hz >= 18000000u && hz <= 580000000u) ||
           (hz >= 760000000u && hz <= 1160000000u);
}

unsigned radio_tone_count(ToneMode mode)
{
    if (mode == TONE_CTCSS)
        return COUNT(ctcss);
    if (mode == TONE_DCS || mode == TONE_DCS_INV)
        return COUNT(dcs);
    return 1;
}

bool radio_config_valid(const RadioConfig *config)
{
    return radio_frequency_valid(config->hz) && config->hz % 10 == 0 &&
           config->rx_bw < BW_COUNT && config->tx_bw < BW_COUNT &&
           config->squelch <= 9 && config->power >= 1 && config->power <= 100 &&
           config->rx_tone.mode < TONE_COUNT && config->tx_tone.mode < TONE_COUNT &&
           config->rx_tone.index < radio_tone_count(config->rx_tone.mode) &&
           config->tx_tone.index < radio_tone_count(config->tx_tone.mode);
}

void radio_tone_label(Tone tone, char text[12])
{
    if (!tone.mode)
        snprintf(text, 12, "OFF");
    else if (tone.mode == TONE_CTCSS)
        snprintf(text, 12, "%u.%u HZ", ctcss[tone.index] / 10u, ctcss[tone.index] % 10u);
    else
        snprintf(text, 12, "D%03o%c", (unsigned)dcs[tone.index],
                 tone.mode == TONE_DCS_INV ? 'I' : 'N');
}

static void serial_out(uint32_t value, unsigned bits)
{
    /* Leave SCK high after the last bit for the read turnaround. */
    for (uint32_t mask = 1u << (bits - 1); mask; mask >>= 1) {
        GPIO_LOW(GPIOE, BK_CLOCK);
        if (value & mask)
            GPIO_HIGH(GPIOE, BK_DATA);
        else
            GPIO_LOW(GPIOE, BK_DATA);
        board_delay_us(1);
        GPIO_HIGH(GPIOE, BK_CLOCK);
        board_delay_us(1);
    }
}

static void write_register(uint8_t reg, uint16_t value)
{
    GPIO_LOW(GPIOE, BK_CLOCK);
    GPIO_LOW(GPIOE, selected);
    serial_out(((uint32_t)reg << 16) | value, 24);
    GPIO_LOW(GPIOE, BK_CLOCK);
    GPIO_HIGH(GPIOE, selected);
    board_delay_us(1);
}

static uint32_t read_samples(uint8_t reg)
{
    /* The radio's SDATA path settles much more slowly than the chip pin.
       Sample before the first falling edge and after each falling edge. */
    const unsigned read_half_us = 5;
    GPIO_LOW(GPIOE, BK_CLOCK);
    GPIO_LOW(GPIOE, selected);
    board_delay_us(1);
    serial_out(0x80u | reg, 8);
    /* Release the push-pull driver before setting the input's pull direction. */
    board_pin_mode(GPIOE, BK_DATA, GPIO_MODE_INPUT, GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
    board_pin_mode(GPIOE, BK_DATA, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_OUTPUT_PUSH_PULL);
    board_delay_us(read_half_us);
    uint32_t samples = GPIO_READ(GPIOE, BK_DATA);
    for (unsigned i = 0; i < 16; ++i) {
        GPIO_LOW(GPIOE, BK_CLOCK);
        board_delay_us(i == 0 ? 2 * read_half_us : read_half_us);
        samples = (samples << 1) | GPIO_READ(GPIOE, BK_DATA);
        if (i != 15) {
            GPIO_HIGH(GPIOE, BK_CLOCK);
            board_delay_us(read_half_us);
        }
    }
    GPIO_HIGH(GPIOE, selected);
    board_delay_us(read_half_us);
    board_pin_mode(GPIOE, BK_DATA, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
    return samples;
}

static unsigned selected_chip(void)
{
    return selected == BK_SELECT_B ? 1u : 0u;
}

static void verify_read_alignment(void)
{
    uint32_t samples = read_samples(0x00);
    ReadAlignment alignment = READ_UNVERIFIED;
    if ((uint16_t)samples == 0x4829u)
        alignment = READ_AFTER_FALL;
    else if ((samples >> 1) == 0x4829u)
        alignment = READ_BEFORE_FALL;
    read_alignment[selected_chip()] = alignment;
}

static uint16_t read_register(uint8_t reg)
{
    ReadAlignment alignment = read_alignment[selected_chip()];
    if (alignment == READ_UNVERIFIED)
        return UINT16_MAX;
    uint32_t samples = read_samples(reg);
    return (uint16_t)(alignment == READ_BEFORE_FALL ? samples >> 1 : samples);
}

static void write_table(const Register *table, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        write_register(table[i].reg, table[i].value);
}

static void paths_off(void)
{
    GPIO_LOW(GPIOB, PIN(0) | PIN(1));
    GPIO_LOW(GPIOE, PIN(7) | PIN(12) | PIN(13) | PIN(14));
}

static void relay_set(bool hf)
{
    if (relay_hf == hf)
        return;
    uint32_t elapsed = board_ms() - relay_changed;
    if (elapsed < 150)
        board_delay_ms(150 - elapsed);
    if (hf)
        GPIO_HIGH(GPIOC, PIN(4));
    else
        GPIO_LOW(GPIOC, PIN(4));
    relay_hf = hf;
    relay_changed = board_ms();
    board_delay_ms(20);
}

static void select_path(uint32_t hz, bool tx)
{
    bool hf = hz < 70000000u;
    bool vhf = !hf && hz < 280000000u;
    relay_set(hf);
    if (vhf)
        GPIO_HIGH(GPIOB, tx ? PIN(1) : PIN(0));
    else if (hf)
        GPIO_HIGH(GPIOE, tx ? PIN(14) : PIN(13));
    else
        GPIO_HIGH(GPIOE, tx ? PIN(7) : PIN(12));
    selected = vhf ? BK_SELECT_A : BK_SELECT_B;
}

static void stop_rf(void)
{
    board_apc(0);
    GPIO_HIGH(GPIOB, PIN(8));
    board_audio(false, false);
    if (selected) {
        write_register(0x30, 0);
        write_register(0x36, 0x007F);
        selected = 0;
    }
    paths_off();
    GPIO_LOW(GPIOC, PIN(13) | PIN(14));
    receiving = busy = open = false;
    rssi = RADIO_RSSI_UNAVAILABLE;
}

void radio_idle(void)
{
    stop_rf();
    relay_set(false);
}

void radio_init(void)
{
    static const uint16_t chips[] = {BK_SELECT_A, BK_SELECT_B};
    relay_changed = board_ms();
    for (unsigned i = 0; i < COUNT(chips); ++i) {
        selected = chips[i];
        write_table(startup, COUNT(startup));
        verify_read_alignment();
    }
    stop_rf();
}

static void frequency(uint32_t hz)
{
    uint32_t word = hz / 10u;
    write_register(0x39, (uint16_t)(word >> 16));
    write_register(0x38, (uint16_t)word);
}

/* DCS sends the systematic Golay word least significant bit first. */
static uint32_t dcs_word(uint16_t code)
{
    uint32_t data = 0x800u | code;
    uint32_t parity = data;
    for (unsigned i = 0; i < 12; ++i) {
        if (parity & 1)
            parity ^= 0xAE3u;
        parity >>= 1;
    }
    return (parity << 12) | data;
}

/* The 55 Hz value in the capture (0x1CD) identifies the 13/26 MHz family. */
static uint16_t ctcss_word(unsigned tenths)
{
    return (uint16_t)((tenths * 206489u + 50000u) / 100000u);
}

static void configure_tone(Tone tone, bool tx)
{
    uint16_t reg51 = 0x0046u;
    if (tone.mode == TONE_CTCSS) {
        reg51 |= 0x1000;
        write_register(0x07, ctcss_word(ctcss[tone.index]));
        if (!tx)
            write_register(0x07, 0x21CD);
    } else if (tone.mode != TONE_OFF) {
        uint32_t code = dcs_word(dcs[tone.index]);
        write_register(0x07, 0x4000u | ctcss_word(1344));
        write_register(0x08, (uint16_t)(code & 0x0FFFu));
        write_register(0x08, 0x8000u | (uint16_t)(code >> 12));
        if (tx && tone.mode == TONE_DCS_INV)
            reg51 |= 0x2000;
    }
    if (tx && tone.mode != TONE_OFF)
        reg51 |= 0x8000;
    write_register(0x51, tone.mode == TONE_OFF ? 0x0000 : reg51);
}

static void bandwidth(Bandwidth mode, bool tx)
{
    static const uint16_t filter[] = {0x3040, 0x3020, 0x1210};
    /* Register 0x43 has separate RF and TX-audio fields. Preserve the captured bit 3. */
    uint16_t value = (uint16_t)((0x3028u & ~0x7FF0u) | filter[mode]);
    write_register(0x43, value);
    if (tx) {
        /* The 25 kHz mode retains the captured deviation. The smaller words
           for narrow modes need verification at the RF output. */
        static const uint16_t deviation[] = {0x285, 0x50A, 0x183};
        write_register(0x40, (uint16_t)((0x350Au & ~0x0FFFu) | deviation[mode]));
    }
}

void radio_receive(const RadioConfig *config, bool packet)
{
    stop_rf();
    select_path(config->hz, false);
    packet_mode = packet;
    squelch = config->squelch;
    rx_tone = packet ? (Tone){TONE_OFF, 0} : config->rx_tone;
    /* Keep the captured receive path's hardware RSSI thresholds disabled.
       The MCU gates voice audio from REG_65 and handles tone matching separately. */
    write_register(0x78, 0x0000);
    configure_tone(rx_tone, false);
    write_table(rx_setup, COUNT(rx_setup));
    bandwidth(packet ? BW_NARROW : config->rx_bw, false);
    frequency(config->hz);
    write_register(0x30, 0xBFF1);
    if (read_alignment[selected_chip()] == READ_UNVERIFIED)
        verify_read_alignment();
    board_audio(packet, false);
    receiving = true;
}

void radio_transmit(const RadioConfig *config, bool packet)
{
    stop_rf();
    select_path(config->hz, true);
    if (!packet && !board_headset())
        GPIO_LOW(GPIOB, PIN(8));
    write_table(tx_setup, COUNT(tx_setup));
    bandwidth(packet ? BW_NARROW : config->tx_bw, true);
    configure_tone(packet ? (Tone){TONE_OFF, 0} : config->tx_tone, true);
    frequency(config->hz);
    write_register(0x30, 0xC1FE);
    board_delay_us(2000);
    board_apc((uint16_t)((config->power * 4095u + 50u) / 100u));
    GPIO_HIGH(GPIOC, PIN(13));
}

void radio_poll(void)
{
    if (!receiving)
        return;
    uint16_t signal = read_register(0x67);
    rssi = signal == UINT16_MAX ? RADIO_RSSI_UNAVAILABLE : (int)(signal & 0x01ffu) / 2 - 160;
    if (packet_mode) {
        busy = rssi >= -111;
    } else if (squelch == 0) {
        busy = false;
    } else {
        uint16_t exnoise = read_register(0x65);
        unsigned limit = 42u - 2u * squelch; /* SQ 1-9: 40, 38, ..., 24. */
        busy = exnoise != UINT16_MAX && (exnoise & 0x007fu) <= limit;
    }

    bool tone_matches = rx_tone.mode == TONE_OFF;
    bool carrier_allowed = squelch == 0 || busy;
    if (carrier_allowed && !tone_matches) {
        static const uint16_t tone_bits[] = {0, 0x0400, 0x4000, 0x8000};
        uint16_t status = read_register(0x0C);
        tone_matches = status != 0xffffu && (status & tone_bits[rx_tone.mode]) != 0;
    }
    bool next = carrier_allowed && tone_matches;
    if (next != open) {
        open = next;
        board_audio(packet_mode, open && !packet_mode);
        if (open)
            GPIO_HIGH(GPIOC, PIN(14));
        else
            GPIO_LOW(GPIOC, PIN(14));
    }
}

bool radio_busy(void) { return busy; }
bool radio_audio_open(void) { return open; }
int radio_rssi(void) { return rssi; }
