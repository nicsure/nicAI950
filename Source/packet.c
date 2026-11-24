#include "packet.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_RATE 9600u
#define AUDIO_IDLE 2048u
#define BIT_SAMPLES (AUDIO_RATE / 1200u)
#define SAMPLE_BUFFER 2048u
#define MAX_FRAME 330u
#define FLAG 0x7eu
#define SYMBOL_PHASE 1024u
#define SAMPLE_PHASE (SYMBOL_PHASE / BIT_SAMPLES)
#define NRZI_MARK 0x20000000u
#define NRZI_SPACE 0x3aaaaaabu

static const int16_t sine[32] = {
    0, 65, 126, 183, 233, 274, 305, 323, 330, 323, 305, 274, 233, 183, 126, 65,
    0, -65, -126, -183, -233, -274, -305, -323, -330, -323, -305, -274, -233, -183, -126, -65
};

static uint8_t tx_bits[128];
static unsigned tx_length, tx_index, tx_samples;
static uint32_t tx_phase, tx_step;
static volatile bool transmitting;
static PacketReception latest;

static uint16_t crc16(uint16_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned i = 0; i < 8; ++i)
        crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0x8408u : 0u));
    return crc;
}

static void append_bit(unsigned bit)
{
    if (bit)
        tx_bits[tx_length / 8u] |= (uint8_t)(1u << (tx_length % 8u));
    ++tx_length;
}

static void append_flag(void)
{
    for (unsigned bit = 0; bit < 8; ++bit)
        append_bit((FLAG >> bit) & 1u);
}

bool packet_callsign_valid(const char call[7])
{
    bool letter = false, number = false;
    unsigned length = 0;
    while (length < 7 && call[length]) {
        char c = call[length++];
        if (c >= 'A' && c <= 'Z')
            letter = true;
        else if (c >= '0' && c <= '9')
            number = true;
        else
            return false;
    }
    return length >= 3 && length <= 6 && letter && number;
}

static void encode_address(uint8_t *bytes, const char *call, unsigned ssid, bool last)
{
    unsigned i = 0;
    while (i < 6 && call[i]) {
        bytes[i] = (uint8_t)call[i] << 1;
        ++i;
    }
    while (i < 6)
        bytes[i++] = (uint8_t)' ' << 1;
    bytes[6] = (uint8_t)(0x60 | (ssid << 1) | (last ? 1u : 0u));
}

static void position(char *text, int32_t latitude, int32_t longitude)
{
    unsigned lat = (unsigned)(latitude < 0 ? -latitude : latitude);
    unsigned lon = (unsigned)(longitude < 0 ? -longitude : longitude);
    unsigned lat_min = (lat % 10000000u * 3u + 2500u) / 5000u;
    unsigned lon_min = (lon % 10000000u * 3u + 2500u) / 5000u;
    unsigned lat_deg = lat / 10000000u + lat_min / 6000u;
    unsigned lon_deg = lon / 10000000u + lon_min / 6000u;
    snprintf(text, 21, "!%02u%02u.%02u%c/%03u%02u.%02u%c>",
             lat_deg, lat_min / 100u % 60u, lat_min % 100u, latitude < 0 ? 'S' : 'N',
             lon_deg, lon_min / 100u % 60u, lon_min % 100u, longitude < 0 ? 'W' : 'E');
}

bool packet_prepare(const char call[7], uint8_t ssid, const GpsFix *fix)
{
    if (transmitting || !packet_callsign_valid(call) || ssid > 15 || !fix->valid ||
        fix->latitude < -900000000 || fix->latitude > 900000000 ||
        fix->longitude < -1800000000 || fix->longitude > 1800000000)
        return false;

    uint8_t frame[16 + 20 + 2];
    encode_address(frame, "APRS", 0, false);
    frame[6] |= 0x80u;  /* UI command. */
    encode_address(frame + 7, call, ssid, true);
    frame[14] = 0x03;
    frame[15] = 0xf0;
    char info[21];
    position(info, fix->latitude, fix->longitude);
    memcpy(frame + 16, info, sizeof(info) - 1);
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < sizeof(frame) - 2; ++i)
        crc = crc16(crc, frame[i]);
    crc = (uint16_t)~crc;
    frame[sizeof(frame) - 2] = (uint8_t)crc;
    frame[sizeof(frame) - 1] = (uint8_t)(crc >> 8);

    memset(tx_bits, 0, sizeof(tx_bits));
    tx_length = 0;
    for (unsigned i = 0; i < 40; ++i)
        append_flag();
    unsigned ones = 0;
    for (unsigned i = 0; i < sizeof(frame); ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            unsigned value = (frame[i] >> bit) & 1u;
            append_bit(value);
            ones = value ? ones + 1 : 0;
            if (ones == 5) {
                append_bit(0);
                ones = 0;
            }
        }
    }
    for (unsigned i = 0; i < 4; ++i)
        append_flag();
    tx_index = tx_samples = tx_phase = 0;
    tx_step = NRZI_MARK;
    transmitting = true;
    return true;
}

bool packet_transmitting(void)
{
    return transmitting;
}

void packet_cancel_transmit(void)
{
    transmitting = false;
}

uint16_t packet_sample_out(void)
{
    if (!transmitting)
        return AUDIO_IDLE;
    if (tx_samples == 0) {
        if (tx_index == tx_length) {
            transmitting = false;
            return AUDIO_IDLE;
        }
        if (!(tx_bits[tx_index / 8u] & (1u << (tx_index % 8u))))
            tx_step = tx_step == NRZI_MARK ? NRZI_SPACE : NRZI_MARK;
        ++tx_index;
        tx_samples = BIT_SAMPLES;
    }
    uint16_t value = (uint16_t)(AUDIO_IDLE + sine[tx_phase >> 27]);
    tx_phase += tx_step;
    --tx_samples;
    return value;
}

static uint16_t sample_buffer[SAMPLE_BUFFER];
static volatile unsigned sample_write, sample_read;
static volatile bool sample_overrun;
static int16_t history[BIT_SAMPLES];
static unsigned history_index;
static unsigned symbol_phase;
static int rate_adjust;
static bool mark, last_symbol;
static uint16_t raw_bits;
static unsigned raw_count, rx_ones, rx_bit, rx_length;
static uint8_t rx_byte, rx_frame[MAX_FRAME];
static bool in_frame;

void packet_sample_in(uint16_t sample)
{
    unsigned write = sample_write;
    unsigned next = (write + 1u) & (SAMPLE_BUFFER - 1u);
    if (next == sample_read) {
        sample_overrun = true;
        return;
    }
    sample_buffer[write] = sample;
    sample_write = next;
}

void packet_reset_receiver(void)
{
    sample_overrun = false;
    sample_read = sample_write;
    memset(history, 0, sizeof(history));
    history_index = symbol_phase = 0;
    rate_adjust = 0;
    mark = last_symbol = false;
    raw_bits = 0;
    raw_count = rx_ones = rx_bit = rx_length = rx_byte = 0;
    in_frame = false;
}

static void decode_address(char out[10], const uint8_t *bytes)
{
    unsigned i = 0;
    while (i < 6 && (bytes[i] >> 1) != ' ') {
        unsigned c = bytes[i] >> 1;
        out[i++] = c >= 32 && c <= 126 ? (char)c : '?';
    }
    unsigned ssid = (bytes[6] >> 1) & 15u;
    if (ssid)
        snprintf(out + i, 10 - i, "-%u", ssid);
    else
        out[i] = '\0';
}

static void accept_frame(void)
{
    if (rx_bit || rx_length < 18 || (rx_frame[6] & 1u))
        return;
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < rx_length; ++i)
        crc = crc16(crc, rx_frame[i]);
    if (crc != 0xf0b8)
        return;

    unsigned offset = 7;
    while (offset + 7u < rx_length - 2u) {
        bool last = (rx_frame[offset + 6] & 1u) != 0;
        offset += 7;
        if (last)
            break;
    }
    if (offset + 2u > rx_length - 2u || !(rx_frame[offset - 1] & 1u) ||
        (rx_frame[offset] & 0xefu) != 0x03u || rx_frame[offset + 1] != 0xf0u)
        return;

    decode_address(latest.destination, rx_frame);
    decode_address(latest.source, rx_frame + 7);
    unsigned count = rx_length - offset - 4u;
    if (count >= sizeof(latest.info))
        count = sizeof(latest.info) - 1u;
    for (unsigned i = 0; i < count; ++i) {
        unsigned c = rx_frame[offset + 2u + i];
        latest.info[i] = c >= 32 && c <= 126 ? (char)c : '.';
    }
    latest.info[count] = '\0';
    ++latest.count;
}

static void receive_data_bit(unsigned bit)
{
    if (!in_frame)
        return;
    if (bit) {
        if (++rx_ones > 5) {
            in_frame = false;
            return;
        }
    } else {
        bool stuffed = rx_ones == 5;
        rx_ones = 0;
        if (stuffed)
            return;
    }

    rx_byte |= (uint8_t)(bit << rx_bit);
    if (++rx_bit != 8)
        return;
    if (rx_length == sizeof(rx_frame)) {
        in_frame = false;
        return;
    }
    rx_frame[rx_length++] = rx_byte;
    rx_bit = rx_byte = 0;
}

static void receive_bit(unsigned bit)
{
    raw_bits |= (uint16_t)(bit << raw_count++);
    /* Delay delivery until an in-band flag can be distinguished from a data byte. */
    if (raw_count > 8) {
        receive_data_bit(raw_bits & 1u);
        raw_bits >>= 1;
        --raw_count;
    }
    if (raw_count == 8 && raw_bits == FLAG) {
        if (in_frame)
            accept_frame();
        raw_bits = 0;
        raw_count = rx_ones = rx_bit = rx_length = rx_byte = 0;
        in_frame = true;
    }
}

static void demodulate(uint16_t sample)
{
    static const int8_t mark_i[8]  = {64, 45,   0, -45, -64, -45,  0, 45};
    static const int8_t mark_q[8]  = { 0, 45,  64,  45,   0, -45,-64,-45};
    static const int8_t space_i[8] = {64,  8, -62, -24,  55,  39,-45,-51};
    static const int8_t space_q[8] = { 0, 63,  17, -59, -32,  51, 45,-39};
    history[history_index] = (int16_t)(sample - AUDIO_IDLE);
    history_index = (history_index + 1u) & 7u;

    int32_t mi = 0, mq = 0, si = 0, sq = 0, dc = 0;
    for (unsigned i = 0; i < BIT_SAMPLES; ++i)
        dc += history[i];
    dc /= (int)BIT_SAMPLES;
    for (unsigned i = 0; i < BIT_SAMPLES; ++i) {
        int32_t signal = history[(history_index + i) & 7u] - dc;
        mi += signal * mark_i[i];
        mq += signal * mark_q[i];
        si += signal * space_i[i];
        sq += signal * space_q[i];
    }
    mi /= 64;
    mq /= 64;
    si /= 64;
    sq /= 64;
    bool next = mi * mi + mq * mq >= si * si + sq * sq;

    /* Track phase at discriminator edges and absorb sustained clock mismatch
       into the symbol rate. Emit one bit on crossing the center of the symbol. */
    int advance = (int)SAMPLE_PHASE + rate_adjust;
    if (next != mark) {
        int error = (int)symbol_phase;
        if (symbol_phase >= SYMBOL_PHASE / 2)
            error -= (int)SYMBOL_PHASE;
        rate_adjust -= error / 128;
        if (rate_adjust > 8)
            rate_adjust = 8;
        else if (rate_adjust < -8)
            rate_adjust = -8;
        int correction = error / 4;
        if (correction > 64)
            correction = 64;
        else if (correction < -64)
            correction = -64;
        advance = (int)SAMPLE_PHASE + rate_adjust - correction;
        mark = next;
    }
    if (symbol_phase < SYMBOL_PHASE / 2 && symbol_phase + (unsigned)advance >= SYMBOL_PHASE / 2) {
        receive_bit(mark == last_symbol);
        last_symbol = mark;
    }
    symbol_phase = (symbol_phase + (unsigned)advance) & (SYMBOL_PHASE - 1u);
}

void packet_poll(void)
{
    if (sample_overrun)
        packet_reset_receiver();
    while (sample_read != sample_write) {
        unsigned read = sample_read;
        uint16_t sample = sample_buffer[read];
        sample_read = (read + 1u) & (SAMPLE_BUFFER - 1u);
        demodulate(sample);
    }
}

const PacketReception *packet_latest(void)
{
    return &latest;
}
