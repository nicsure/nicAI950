#include "board.h"
#include "display.h"
#include "packet.h"
#include "radio.h"
#include "services.h"
#include <stdio.h>
#include <string.h>

// DO NOT REMOVE OR RELOCATE THESE SIGNATURES
// THEY ARE REQUIRED FOR THE BOOTLOADER AND FLASHING TOOL
__attribute__((used, section(".ARM.__at_0x080033E0")))
const char SIG1[] = "RT-950      \0\0";
__attribute__((used, section(".ARM.__at_0x080037E0")))
const char SIG2[] = "RT-950      \0\0";

#define CHANNELS 32u
#define VFOS 3u
#define CONFIG_VERSION 1u
#define PTT_LIMIT_MS 90000u
#define RX_SETTLE_MS 100u
#define PACKET_CLEAR_MS 400u
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    RadioConfig frequency;
    uint8_t channel;
    uint8_t memory_mode;
    uint8_t step;
    uint8_t reserved;
} Vfo;

typedef struct {
    uint32_t version;
    Vfo vfo[VFOS];
    RadioConfig channels[CHANNELS];
    uint32_t aprs_hz;
    uint16_t tuner_freq[TUNER_BANDS];
    char call[7];
    uint8_t ssid, interval, active, brightness, volume, gps_on, tuner_band;
} Settings;

typedef enum { SCREEN_RADIO, SCREEN_MENU, SCREEN_TUNER, SCREEN_GPS,
               SCREEN_APRS, SCREEN_CALL } Screen;
typedef enum { TX_NONE, TX_VOICE, TX_PACKET } Transmit;
typedef enum {
    MENU_SQUELCH, MENU_RX_BW, MENU_TX_BW, MENU_RX_TONE, MENU_RX_CODE,
    MENU_TX_TONE, MENU_TX_CODE, MENU_POWER, MENU_STEP, MENU_CHANNEL,
    MENU_STORE, MENU_CLEAR, MENU_LIGHT, MENU_GPS_POWER, MENU_TUNER_VOLUME,
    MENU_APRS_FREQ, MENU_APRS_CALL, MENU_APRS_SSID, MENU_APRS_INTERVAL, MENU_COUNT
} Menu;

static const char *const menu_names[MENU_COUNT] = {
    "SQUELCH", "RX BANDWIDTH", "TX BANDWIDTH", "RX TONE TYPE", "RX TONE",
    "TX TONE TYPE", "TX TONE", "TX POWER", "VFO STEP", "CHANNEL SLOT",
    "STORE VFO", "CLEAR CHANNEL", "BACKLIGHT", "GPS POWER", "TUNER VOLUME",
    "APRS FREQUENCY", "APRS CALLSIGN", "APRS SSID", "APRS INTERVAL"
};
static const char *const bw_names[] = {"12.5 KHZ", "25 KHZ", "6.25 KHZ"};
static const char *const tone_names[] = {"OFF", "CTCSS", "DCS NORMAL", "DCS INVERTED"};
static const uint16_t steps[] = {5000, 6250, 10000, 12500, 25000, 50000};
static const uint8_t intervals[] = {0, 1, 2, 5, 10, 20, 30};

static Settings settings;
static Screen screen;
static Menu menu;
static Transmit tx;
static RadioConfig applied;
static bool rf_valid, tuner_ok, settings_dirty;
static const char *save_error;
static bool redraw = true, clear_screen = true, previous_ptt = true;
static uint32_t last_change, tx_start, rx_started, last_busy, last_beacon;
static char entry[11], call_edit[7];
static unsigned entry_length, call_cursor;
static const char *notice;
static uint32_t notice_start;
static uint16_t battery_mv;

static bool radio_equal(const RadioConfig *a, const RadioConfig *b)
{
    return a->hz == b->hz && a->rx_tone.mode == b->rx_tone.mode &&
           a->rx_tone.index == b->rx_tone.index && a->tx_tone.mode == b->tx_tone.mode &&
           a->tx_tone.index == b->tx_tone.index && a->rx_bw == b->rx_bw &&
           a->tx_bw == b->tx_bw && a->squelch == b->squelch && a->power == b->power;
}

static void settings_defaults(void)
{
    static const uint32_t initial[VFOS] = {145500000u, 433500000u, 29600000u};
    memset(&settings, 0, sizeof(settings));
    settings.version = CONFIG_VERSION;
    settings.aprs_hz = 144800000u;
    settings.brightness = 70;
    settings.volume = 40;
    settings.gps_on = 1;
    for (unsigned i = 0; i < VFOS; ++i) {
        settings.vfo[i].frequency = (RadioConfig){
            .hz = initial[i], .squelch = 3, .power = 10
        };
        settings.vfo[i].step = 3;
    }
    for (unsigned i = 0; i < TUNER_BANDS; ++i)
        settings.tuner_freq[i] = tuner_ranges[i].initial;
}

static bool settings_valid(void)
{
    if (settings.version != CONFIG_VERSION || settings.active >= VFOS ||
        settings.brightness < 10 || settings.brightness > 100 || settings.volume > 63 ||
        settings.gps_on > 1 || settings.tuner_band >= TUNER_BANDS || settings.ssid > 15 ||
        settings.interval >= COUNT(intervals) || !radio_frequency_valid(settings.aprs_hz) ||
        settings.aprs_hz % 10 || (settings.call[0] && !packet_callsign_valid(settings.call)))
        return false;

    for (unsigned i = 0; i < VFOS; ++i) {
        const Vfo *vfo = &settings.vfo[i];
        if (vfo->channel >= CHANNELS || vfo->memory_mode > 1 ||
            vfo->step >= COUNT(steps) || !radio_config_valid(&vfo->frequency))
            return false;
    }
    for (unsigned i = 0; i < CHANNELS; ++i)
        if (settings.channels[i].hz && !radio_config_valid(&settings.channels[i]))
            return false;
    for (unsigned i = 0; i < TUNER_BANDS; ++i)
        if (settings.tuner_freq[i] < tuner_ranges[i].minimum ||
            settings.tuner_freq[i] > tuner_ranges[i].maximum)
            return false;
    return true;
}

static Vfo *active_vfo(void) { return &settings.vfo[settings.active]; }

static RadioConfig *vfo_config(Vfo *vfo)
{
    return vfo->memory_mode ? &settings.channels[vfo->channel] : &vfo->frequency;
}

static RadioConfig *editable_config(void)
{
    Vfo *vfo = active_vfo();
    RadioConfig *config = vfo_config(vfo);
    return config->hz ? config : &vfo->frequency;
}

static RadioConfig aprs_config(void)
{
    return (RadioConfig){.hz = settings.aprs_hz, .squelch = 3, .power = editable_config()->power};
}

static void changed(void)
{
    settings_dirty = redraw = true;
    save_error = NULL;
    last_change = board_ms();
}

static void notify_user(const char *text)
{
    notice = text;
    notice_start = board_ms();
    redraw = true;
}

static void refresh_rf(void)
{
    if (screen == SCREEN_TUNER || tx != TX_NONE)
        return;
    bool aprs = screen == SCREEN_APRS;
    RadioConfig config = aprs ? aprs_config() : *vfo_config(active_vfo());
    if (rf_valid && radio_equal(&config, &applied))
        return;

    board_packet_audio(false, false);
    packet_reset_receiver();
    if (config.hz)
        radio_receive(&config, aprs);
    else
        radio_idle();
    if (aprs)
        board_packet_audio(true, false);
    applied = config;
    rf_valid = true;
    rx_started = last_busy = board_ms();
    redraw = true;
}

static void tune_broadcast(void)
{
    TunerBand band = (TunerBand)settings.tuner_band;
    tuner_ok = tuner_tune(band, settings.tuner_freq[band]) && tuner_volume(settings.volume);
    if (!tuner_ok)
        tuner_stop();
    redraw = true;
}

static void cancel_entry(void)
{
    entry_length = 0;
    entry[0] = 0;
    redraw = true;
}

static void set_screen(Screen next)
{
    if (screen == next)
        return;
    bool was_aprs = screen == SCREEN_APRS;
    if (screen == SCREEN_TUNER) {
        tuner_stop();
        rf_valid = false;
    }
    screen = next;
    notice = NULL;
    cancel_entry();
    clear_screen = true;
    if (next == SCREEN_APRS)
        last_beacon = board_ms();
    if (next == SCREEN_TUNER) {
        board_packet_audio(false, false);
        radio_idle(); /* Both BK4829s idle and the HF relay is low. */
        tune_broadcast();
    } else {
        if (was_aprs || next == SCREEN_APRS)
            rf_valid = false;
        refresh_rf();
    }
}

static void format_hz(char text[16], uint32_t hz)
{
    snprintf(text, 16, "%lu.%05lu", (unsigned long)(hz / 1000000u),
             (unsigned long)(hz / 10u % 100000u));
}

static void line(unsigned y, uint16_t color, const char *text)
{
    display_text(10, y, 18, FONT_MEDIUM, color, COLOR_BG, text);
}

static void small(unsigned y, uint16_t color, const char *text)
{
    display_text(10, y, 36, FONT_SMALL, color, COLOR_BG, text);
}

static void heading(const char *name)
{
    char volts[12];
    snprintf(volts, sizeof(volts), "%u.%02uV", battery_mv / 1000u, battery_mv / 10u % 100u);
    display_text(10, 11, 13, FONT_MEDIUM, COLOR_TEXT, COLOR_PANEL, name);
    display_text(181, 15, 8, FONT_SMALL, COLOR_DIM, COLOR_PANEL, volts);
}

static void footer(const char *text)
{
    display_text(10, 302, 36, FONT_SMALL, COLOR_DIM, COLOR_PANEL,
                 notice ? notice : save_error ? save_error : text);
}

static void draw_radio(void)
{
    char text[40];
    heading("RADIO");
    for (unsigned i = 0; i < VFOS; ++i) {
        Vfo *vfo = &settings.vfo[i];
        RadioConfig *config = vfo_config(vfo);
        bool active = i == settings.active;
        unsigned y = 42 + 74 * i;
        uint16_t fg = active ? COLOR_ACCENT : COLOR_TEXT;
        uint16_t bg = active ? COLOR_PANEL : COLOR_BG;
        display_fill(8, y, 3, 65, active ? COLOR_ACCENT : COLOR_EDGE);
        snprintf(text, sizeof(text), "%c  %s", (int)('A' + i),
                 vfo->memory_mode ? "MEMORY" : "VFO");
        display_text(16, y + 4, 12, FONT_SMALL, fg, bg, text);
        if (vfo->memory_mode)
            snprintf(text, sizeof(text), "CH %02u", (unsigned)vfo->channel + 1u);
        else
            snprintf(text, sizeof(text), "FM");
        display_text(166, y + 4, 10, FONT_SMALL, COLOR_DIM, bg, text);
        if (active && entry_length)
            snprintf(text, sizeof(text), "%s", entry);
        else if (config->hz)
            format_hz(text, config->hz);
        else
            snprintf(text, sizeof(text), "EMPTY");
        display_text(16, y + 17, 10, FONT_LARGE, fg, bg, text);
        if (config->hz && active) {
            if (tx == TX_VOICE)
                snprintf(text, sizeof(text), "TX   POWER %u%%", config->power);
            else if (radio_rssi() == RADIO_RSSI_UNAVAILABLE)
                snprintf(text, sizeof(text), "RSSI UNAVAILABLE   SQUELCH %u", config->squelch);
            else if (radio_audio_open())
                snprintf(text, sizeof(text), "RX   %d DBM", radio_rssi());
            else
                snprintf(text, sizeof(text), "%s   SQUELCH %u", bw_names[config->rx_bw], config->squelch);
        } else {
            snprintf(text, sizeof(text), "%s", config->hz ? bw_names[config->rx_bw] : "");
        }
        display_text(16, y + 48, 35, FONT_SMALL, tx == TX_VOICE && active ? COLOR_TX : COLOR_DIM,
                     bg, text);
    }
    small(272, COLOR_DIM, "SK1 TUNER    SK2 GPS    SK3 APRS");
    if (entry_length && active_vfo()->memory_mode)
        footer("MENU SELECTS CHANNEL   EXIT CANCELS");
    else if (entry_length)
        footer("* DECIMAL  MENU TUNES  EXIT CANCELS");
    else
        footer("MENU SETTINGS  ABC VFO  VM MEMORY");
}

static void menu_value(Menu item, char text[24])
{
    Vfo *vfo = active_vfo();
    RadioConfig *config = editable_config();
    switch (item) {
    case MENU_SQUELCH: snprintf(text, 24, "%u", config->squelch); break;
    case MENU_RX_BW: snprintf(text, 24, "%s", bw_names[config->rx_bw]); break;
    case MENU_TX_BW: snprintf(text, 24, "%s", bw_names[config->tx_bw]); break;
    case MENU_RX_TONE: snprintf(text, 24, "%s", tone_names[config->rx_tone.mode]); break;
    case MENU_TX_TONE: snprintf(text, 24, "%s", tone_names[config->tx_tone.mode]); break;
    case MENU_RX_CODE: radio_tone_label(config->rx_tone, text); break;
    case MENU_TX_CODE: radio_tone_label(config->tx_tone, text); break;
    case MENU_POWER: snprintf(text, 24, "%u%%", config->power); break;
    case MENU_STEP: {
        unsigned step = steps[vfo->step];
        snprintf(text, 24, "%u.%03u KHZ", step / 1000u, step % 1000u);
        break;
    }
    case MENU_CHANNEL:
        snprintf(text, 24, "CH %02u %s", (unsigned)vfo->channel + 1u,
                 settings.channels[vfo->channel].hz ? "STORED" : "EMPTY");
        break;
    case MENU_STORE: case MENU_CLEAR: snprintf(text, 24, "PRESS MENU"); break;
    case MENU_LIGHT: snprintf(text, 24, "%u%%", settings.brightness); break;
    case MENU_GPS_POWER: snprintf(text, 24, "%s", settings.gps_on ? "ON" : "OFF"); break;
    case MENU_TUNER_VOLUME: snprintf(text, 24, "%u", settings.volume); break;
    case MENU_APRS_FREQ: format_hz(text, settings.aprs_hz); break;
    case MENU_APRS_CALL: snprintf(text, 24, "%s", settings.call[0] ? settings.call : "UNSET"); break;
    case MENU_APRS_SSID: snprintf(text, 24, "%u", settings.ssid); break;
    case MENU_APRS_INTERVAL:
        if (settings.interval)
            snprintf(text, 24, "%u MIN", intervals[settings.interval]);
        else
            snprintf(text, 24, "MANUAL");
        break;
    default: text[0] = 0; break;
    }
}

static void draw_menu(void)
{
    heading("SETTINGS");
    unsigned first = (unsigned)menu / 6u * 6u;
    for (unsigned row = 0; row < 6; ++row) {
        unsigned item = first + row;
        unsigned y = 39 + row * 41;
        bool selected = item == (unsigned)menu;
        uint16_t fg = selected ? COLOR_ACCENT : COLOR_TEXT;
        uint16_t bg = selected ? COLOR_PANEL : COLOR_BG;
        display_fill(8, y, 223, 36, bg);
        if (item < MENU_COUNT) {
            char text[24];
            display_text(14, y + 4, 18, FONT_MEDIUM, fg, bg, menu_names[item]);
            menu_value((Menu)item, text);
            display_text(14, y + 23, 34, FONT_SMALL, selected ? COLOR_TEXT : COLOR_DIM, bg, text);
        }
    }
    const char *hint = "UP/DOWN SELECT   LEFT/RIGHT CHANGE";
    if (menu == MENU_APRS_FREQ)
        hint = "L/R 5 KHZ   MENU COPIES ACTIVE VFO";
    else if (menu == MENU_STORE || menu == MENU_CLEAR || menu == MENU_APRS_CALL)
        hint = "MENU SELECTS ACTION   EXIT BACK";
    footer(hint);
}

static void draw_tuner(void)
{
    heading("BROADCAST");
    TunerBand band = (TunerBand)settings.tuner_band;
    char text[20];
    snprintf(text, sizeof(text), "%s BAND", tuner_ranges[band].name);
    line(57, COLOR_ACCENT, text);
    unsigned f = settings.tuner_freq[band];
    if (band == TUNER_FM)
        snprintf(text, sizeof(text), "%u.%02u", f / 100u, f % 100u);
    else
        snprintf(text, sizeof(text), "%u", f);
    display_text(10, 91, 11, FONT_LARGE, COLOR_TEXT, COLOR_BG, text);
    line(127, COLOR_DIM, band == TUNER_FM ? "MHZ" : "KHZ");
    snprintf(text, sizeof(text), "VOLUME %u", settings.volume);
    line(175, COLOR_TEXT, text);
    small(211, tuner_ok ? COLOR_ACCENT : COLOR_TX, tuner_ok ? "RECEIVING" : "TUNER DID NOT RESPOND");
    footer("L/R BAND   U/D TUNE   MENU RETRY");
}

static void gps_coordinate(char text[24], bool fresh, int32_t coord, const char *label)
{
    if (fresh) {
        uint32_t value = (uint32_t)(coord < 0 ? -coord : coord);
        snprintf(text, 24, "%s %c%lu.%05lu", label, coord < 0 ? '-' : '+',
                 (unsigned long)(value / 10000000u), (unsigned long)(value / 100u % 100000u));
    } else {
        snprintf(text, 24, "%s --", label);
    }
}

static void draw_gps(void)
{
    char text[24];
    heading("GPS");
    const GpsFix *fix = gps_fix();
    bool fresh = settings.gps_on && gps_fresh(board_ms());
    line(58, fresh ? COLOR_ACCENT : COLOR_AMBER,
         fresh ? "POSITION FIX" : settings.gps_on ? "WAITING FOR FIX" : "GPS POWER OFF");
    gps_coordinate(text, fresh, fix->latitude, "LAT");
    line(105, COLOR_TEXT, text);
    gps_coordinate(text, fresh, fix->longitude, "LON");
    line(135, COLOR_TEXT, text);
    snprintf(text, sizeof(text), "UTC %s", fresh && fix->utc[0] ? fix->utc : "--:--:--");
    line(183, COLOR_DIM, text);
    footer("MENU GPS POWER   EXIT RADIO");
}

static void draw_aprs(void)
{
    const PacketReception *packet = packet_latest();
    char text[40], hz[16];
    heading("APRS");
    format_hz(hz, settings.aprs_hz);
    snprintf(text, sizeof(text), "%s MHZ", hz);
    line(47, COLOR_ACCENT, text);
    snprintf(text, sizeof(text), "%s  %s", settings.call[0] ? settings.call : "CALL UNSET",
             gps_fresh(board_ms()) ? "GPS FIX" : "NO GPS FIX");
    small(73, COLOR_DIM, text);
    line(94, tx == TX_PACKET ? COLOR_TX : COLOR_TEXT,
         tx == TX_PACKET ? "BEACON TX" : radio_busy() ? "CHANNEL BUSY" : "LISTENING");
    snprintf(text, sizeof(text), "PACKETS RX %lu", (unsigned long)packet->count);
    small(127, COLOR_ACCENT, text);
    if (packet->count)
        snprintf(text, sizeof(text), "%s > %s", packet->source, packet->destination);
    else
        snprintf(text, sizeof(text), "NO PACKETS RECEIVED");
    small(150, COLOR_TEXT, text);
    const char *info = packet->info;
    for (unsigned row = 0; row < 8; ++row) {
        unsigned n = 0;
        while (n < 36 && *info)
            text[n++] = *info++;
        text[n] = 0;
        small(167 + row * 13, COLOR_DIM, text);
    }
    footer("MENU SEND BEACON   EXIT RADIO");
}

static void draw_call(void)
{
    heading("APRS CALLSIGN");
    for (unsigned i = 0; i < 6; ++i) {
        char text[2] = {call_edit[i] == ' ' ? '_' : call_edit[i], 0};
        uint16_t bg = i == call_cursor ? COLOR_EDGE : COLOR_PANEL;
        display_text(14 + 35 * i, 102, 1, FONT_LARGE, COLOR_ACCENT, bg, text);
    }
    small(159, COLOR_TEXT, "UP/DOWN LETTER OR NUMBER");
    small(175, COLOR_TEXT, "LEFT/RIGHT CURSOR   * SPACE");
    footer("MENU SAVE   EXIT CANCEL");
}

static void draw(void)
{
    if (clear_screen) {
        display_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
        display_fill(0, 0, LCD_WIDTH, 34, COLOR_PANEL);
        display_fill(0, 290, LCD_WIDTH, 30, COLOR_PANEL);
        clear_screen = false;
    }
    switch (screen) {
    case SCREEN_RADIO: draw_radio(); break;
    case SCREEN_MENU: draw_menu(); break;
    case SCREEN_TUNER: draw_tuner(); break;
    case SCREEN_GPS: draw_gps(); break;
    case SCREEN_APRS: draw_aprs(); break;
    case SCREEN_CALL: draw_call(); break;
    }
    redraw = false;
}

static unsigned wrap(unsigned value, int direction, unsigned count)
{
    return direction > 0 ? (value + 1u) % count : (value + count - 1u) % count;
}

static unsigned adjust(unsigned value, int change, unsigned min, unsigned max)
{
    int next = (int)value + change;
    if (next < (int)min)
        return min;
    if (next > (int)max)
        return max;
    return (unsigned)next;
}

static uint32_t step_hz(uint32_t hz, int change)
{
    int32_t next = (int32_t)hz + change;
    if (next < 18000000)
        return 1160000000u;
    if (next > 1160000000)
        return 18000000u;
    if (!radio_frequency_valid((uint32_t)next))
        return change > 0 ? 760000000u : 580000000u;
    return (uint32_t)next;
}

static void set_gps_power(bool on)
{
    settings.gps_on = on;
    if (!on)
        gps_clear();
    board_gps_power(on);
}

static void adjust_menu(int direction)
{
    Vfo *vfo = active_vfo();
    RadioConfig *config = editable_config();
    switch (menu) {
    case MENU_SQUELCH: config->squelch = adjust(config->squelch, direction, 0, 9); break;
    case MENU_RX_BW: config->rx_bw = wrap(config->rx_bw, direction, BW_COUNT); break;
    case MENU_TX_BW: config->tx_bw = wrap(config->tx_bw, direction, BW_COUNT); break;
    case MENU_RX_TONE: config->rx_tone = (Tone){wrap(config->rx_tone.mode, direction, TONE_COUNT), 0}; break;
    case MENU_TX_TONE: config->tx_tone = (Tone){wrap(config->tx_tone.mode, direction, TONE_COUNT), 0}; break;
    case MENU_RX_CODE:
        config->rx_tone.index = wrap(config->rx_tone.index, direction,
                                     radio_tone_count(config->rx_tone.mode));
        break;
    case MENU_TX_CODE:
        config->tx_tone.index = wrap(config->tx_tone.index, direction,
                                     radio_tone_count(config->tx_tone.mode));
        break;
    case MENU_POWER: config->power = adjust(config->power, direction * 5, 1, 100); break;
    case MENU_STEP: vfo->step = wrap(vfo->step, direction, COUNT(steps)); break;
    case MENU_CHANNEL: vfo->channel = wrap(vfo->channel, direction, CHANNELS); break;
    case MENU_LIGHT:
        settings.brightness = adjust(settings.brightness, direction * 10, 10, 100);
        board_backlight(settings.brightness);
        break;
    case MENU_GPS_POWER: set_gps_power(!settings.gps_on); break;
    case MENU_TUNER_VOLUME: settings.volume = adjust(settings.volume, direction, 0, 63); break;
    case MENU_APRS_FREQ: settings.aprs_hz = step_hz(settings.aprs_hz, direction * 5000); break;
    case MENU_APRS_SSID: settings.ssid = wrap(settings.ssid, direction, 16); break;
    case MENU_APRS_INTERVAL: settings.interval = wrap(settings.interval, direction, COUNT(intervals)); break;
    default: return;
    }
    changed();
}

static void menu_action(void)
{
    Vfo *vfo = active_vfo();
    RadioConfig *channel = &settings.channels[vfo->channel];
    if (menu == MENU_STORE) {
        *channel = vfo->frequency;
        changed();
        notify_user("VFO STORED TO CHANNEL");
    } else if (menu == MENU_CLEAR) {
        memset(channel, 0, sizeof(*channel));
        changed();
        notify_user("CHANNEL CLEARED");
    } else if (menu == MENU_APRS_CALL) {
        memset(call_edit, ' ', 6);
        memcpy(call_edit, settings.call, strlen(settings.call));
        call_cursor = 0;
        set_screen(SCREEN_CALL);
    } else if (menu == MENU_APRS_FREQ && vfo_config(vfo)->hz) {
        settings.aprs_hz = vfo_config(vfo)->hz;
        changed();
        notify_user("APRS FREQUENCY COPIED");
    }
}

static void callsign_key(Key key)
{
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    if (key == KEY_LEFT || key == KEY_RIGHT) {
        call_cursor = wrap(call_cursor, key == KEY_RIGHT ? 1 : -1, 6);
    } else if (key == KEY_UP || key == KEY_DOWN) {
        unsigned index = (unsigned)(strchr(chars, call_edit[call_cursor]) - chars);
        call_edit[call_cursor] = chars[wrap(index, key == KEY_UP ? 1 : -1, sizeof(chars) - 1)];
    } else if (key <= KEY_9 || key == KEY_STAR) {
        call_edit[call_cursor] = key == KEY_STAR ? ' ' : (char)('0' + key);
        if (call_cursor < 5)
            ++call_cursor;
    } else if (key == KEY_MENU) {
        char call[7] = {0};
        unsigned size = 6;
        while (size && call_edit[size - 1] == ' ')
            --size;
        memcpy(call, call_edit, size);
        if (size && !packet_callsign_valid(call)) {
            notify_user("ENTER A VALID CALLSIGN");
            return;
        }
        memcpy(settings.call, call, sizeof(call));
        changed();
        set_screen(SCREEN_MENU);
    }
    redraw = true;
}

static uint32_t parse_frequency(void)
{
    uint32_t mhz = 0, fraction = 0, multiplier = 100000u;
    bool decimal = false;
    for (unsigned i = 0; i < entry_length; ++i) {
        if (entry[i] == '.') {
            decimal = true;
        } else if (decimal) {
            if (multiplier < 10)
                return 0;
            fraction += (unsigned)(entry[i] - '0') * multiplier;
            multiplier /= 10u;
        } else {
            mhz = mhz * 10u + (unsigned)(entry[i] - '0');
            if (mhz > 1160)
                return 0;
        }
    }
    return mhz * 1000000u + fraction;
}

static void finish_entry(void)
{
    Vfo *vfo = active_vfo();
    if (vfo->memory_mode) {
        unsigned channel = (unsigned)(entry[0] - '0');
        if (entry_length == 2)
            channel = channel * 10u + (unsigned)(entry[1] - '0');
        if (channel && channel <= CHANNELS) {
            vfo->channel = channel - 1u;
            changed();
        } else {
            notify_user("CHANNEL MUST BE 1 THROUGH 32");
        }
    } else {
        uint32_t hz = parse_frequency();
        if (radio_frequency_valid(hz)) {
            vfo->frequency.hz = hz;
            changed();
        } else {
            notify_user("CHECK FREQUENCY AND DECIMAL DIGITS");
        }
    }
    cancel_entry();
}

static void radio_key(Key key)
{
    Vfo *vfo = active_vfo();
    if (key <= KEY_9 || (key == KEY_STAR && !vfo->memory_mode)) {
        unsigned limit = vfo->memory_mode ? 2u : sizeof(entry) - 1u;
        if (entry_length < limit && (key != KEY_STAR || !strchr(entry, '.'))) {
            entry[entry_length++] = key == KEY_STAR ? '.' : (char)('0' + key);
            entry[entry_length] = 0;
            redraw = true;
        }
    } else if (key == KEY_MENU && entry_length) {
        finish_entry();
    } else if (key == KEY_EXIT && entry_length) {
        cancel_entry();
    } else if (key == KEY_LEFT && entry_length) {
        entry[--entry_length] = 0;
        redraw = true;
    } else if (key == KEY_MENU) {
        set_screen(SCREEN_MENU);
    } else if (key == KEY_ABC || key == KEY_VM) {
        cancel_entry();
        if (key == KEY_ABC)
            settings.active = wrap(settings.active, 1, VFOS);
        else
            vfo->memory_mode ^= 1u;
        changed();
    } else if (!entry_length &&
               (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT)) {
        int direction = key == KEY_UP || key == KEY_RIGHT ? 1 : -1;
        bool coarse = key == KEY_LEFT || key == KEY_RIGHT;
        if (vfo->memory_mode)
            vfo->channel = (uint8_t)((vfo->channel + (int)CHANNELS + direction * (coarse ? 5 : 1)) % (int)CHANNELS);
        else
            vfo->frequency.hz = step_hz(vfo->frequency.hz, direction * (coarse ? 1000000 : steps[vfo->step]));
        changed();
    }
}

static void tuner_key(Key key)
{
    if (key == KEY_LEFT || key == KEY_RIGHT) {
        settings.tuner_band = wrap(settings.tuner_band, key == KEY_RIGHT ? 1 : -1, TUNER_BANDS);
    } else if (key == KEY_UP || key == KEY_DOWN) {
        TunerBand band = (TunerBand)settings.tuner_band;
        const TunerRange *range = &tuner_ranges[band];
        int step = key == KEY_UP ? range->step : -(int)range->step;
        uint16_t value = (uint16_t)adjust(settings.tuner_freq[band], step,
                                         range->minimum, range->maximum);
        if (value == settings.tuner_freq[band])
            return;
        settings.tuner_freq[band] = value;
    } else if (key != KEY_MENU) {
        return;
    }
    tune_broadcast();
    if (key != KEY_MENU)
        changed();
}

static bool prepare_beacon(uint32_t now, bool manual)
{
    const char *failure = NULL;
    if (!packet_callsign_valid(settings.call))
        failure = "SET APRS CALLSIGN IN SETTINGS";
    else if (!settings.gps_on || !gps_fresh(now))
        failure = "WAIT FOR A GPS FIX";
    else if (radio_busy() || now - last_busy < PACKET_CLEAR_MS)
        failure = "APRS CHANNEL BUSY";
    else if (board_ptt())
        failure = "RELEASE PTT";
    if (failure) {
        if (manual)
            notify_user(failure);
        return false;
    }

    if (!packet_prepare(settings.call, settings.ssid, gps_fix()))
        return false;
    board_packet_audio(false, false);
    packet_reset_receiver();
    RadioConfig config = aprs_config();
    radio_transmit(&config, true);
    board_packet_audio(false, true);
    tx = TX_PACKET;
    tx_start = last_beacon = board_ms();
    redraw = true;
    return true;
}

static void handle_key(Key key)
{
    if (key == KEY_NONE)
        return;
    if (key == KEY_SK1 || key == KEY_SK2 || key == KEY_SK3) {
        Screen shortcut = key == KEY_SK1 ? SCREEN_TUNER : key == KEY_SK2 ? SCREEN_GPS : SCREEN_APRS;
        set_screen(screen == shortcut ? SCREEN_RADIO : shortcut);
        return;
    }
    if (key == KEY_EXIT && screen != SCREEN_RADIO) {
        set_screen(screen == SCREEN_CALL ? SCREEN_MENU : SCREEN_RADIO);
        return;
    }

    switch (screen) {
    case SCREEN_RADIO: radio_key(key); break;
    case SCREEN_MENU:
        if (key == KEY_UP || key == KEY_DOWN) {
            menu = (Menu)wrap(menu, key == KEY_DOWN ? 1 : -1, MENU_COUNT);
            redraw = true;
        } else if (key == KEY_LEFT || key == KEY_RIGHT) {
            adjust_menu(key == KEY_RIGHT ? 1 : -1);
        } else if (key == KEY_MENU) {
            menu_action();
        }
        break;
    case SCREEN_TUNER: tuner_key(key); break;
    case SCREEN_GPS:
        if (key == KEY_MENU) {
            set_gps_power(!settings.gps_on);
            changed();
        }
        break;
    case SCREEN_APRS:
        if (key == KEY_MENU)
            prepare_beacon(board_ms(), true);
        break;
    case SCREEN_CALL: callsign_key(key); break;
    }
}

static void end_tx(void)
{
    board_packet_audio(false, false);
    packet_cancel_transmit();
    tx = TX_NONE;
    rf_valid = false;
    refresh_rf();
}

static void ptt_poll(uint32_t now, bool can_transmit)
{
    bool ptt = board_ptt();
    if (tx == TX_VOICE && (!ptt || !can_transmit || now - tx_start >= PTT_LIMIT_MS)) {
        end_tx();
        if (ptt && can_transmit)
            notify_user("TX TIME LIMIT");
    } else if (tx == TX_PACKET &&
               (!packet_transmitting() || !can_transmit || now - tx_start >= 2000)) {
        end_tx();
    } else if (tx == TX_NONE && can_transmit && ptt && !previous_ptt &&
               screen == SCREEN_RADIO && !entry_length) {
        RadioConfig *config = vfo_config(active_vfo());
        if (config->hz) {
            radio_transmit(config, false);
            tx = TX_VOICE;
            tx_start = board_ms();
            redraw = true;
        } else {
            notify_user("EMPTY CHANNEL");
        }
    }
    previous_ptt = ptt;
}

static void shutdown(void)
{
    board_packet_audio(false, false);
    tuner_stop();
    radio_idle();
    board_gps_power(false);
    board_backlight(0);
    if (settings_dirty)
        storage_save(&settings, sizeof(settings));

    /* A switch returned to ON during the flash save cancels power-off. */
    __disable_irq();
    if (board_power_on())
        NVIC_SystemReset();
    board_power_release();
    for (;;)
        __WFI();
}

int main(void)
{
    board_init();
    if (!storage_load(&settings, sizeof(settings)) || !settings_valid())
        settings_defaults();
    board_gps_power(settings.gps_on);
    display_init();
    board_backlight(settings.brightness);
    radio_init();
    battery_mv = board_battery_mv();
    refresh_rf();

    uint32_t last_poll = 0, last_status = 0, power_off = 0;
    bool off_pending = false;
    for (;;) {
        uint32_t now = board_ms();
        bool power_on = board_power_on();
        if (power_on) {
            off_pending = false;
        } else if (!off_pending) {
            off_pending = true;
            power_off = now;
        } else if (now - power_off >= 30) {
            shutdown();
        }
        if (settings.gps_on)
            gps_poll(now);
        if (screen == SCREEN_APRS && tx == TX_NONE) {
            uint32_t count = packet_latest()->count;
            packet_poll();
            redraw |= packet_latest()->count != count;
        }
        if (tx == TX_NONE && screen != SCREEN_TUNER && now - rx_started >= RX_SETTLE_MS &&
            now - last_poll >= 20) {
            bool was_open = radio_audio_open();
            bool was_busy = radio_busy();
            bool had_rssi = radio_rssi() != RADIO_RSSI_UNAVAILABLE;
            last_poll = now;
            radio_poll();
            if (radio_busy())
                last_busy = now;
            redraw |= was_open != radio_audio_open() || was_busy != radio_busy() ||
                      had_rssi != (radio_rssi() != RADIO_RSSI_UNAVAILABLE);
        }
        ptt_poll(now, power_on);

        Key key = board_key();
        if (power_on && tx == TX_NONE && !previous_ptt) {
            handle_key(key);
            refresh_rf();
            now = board_ms();
            if (screen == SCREEN_APRS && settings.interval &&
                now - last_beacon >= intervals[settings.interval] * 60000u)
                prepare_beacon(now, false);
        }
        if (settings_dirty && !previous_ptt && tx == TX_NONE && screen != SCREEN_APRS &&
            now - last_change >= (save_error ? 30000u : 2000u)) {
            save_error = storage_save(&settings, sizeof(settings));
            settings_dirty = save_error != NULL;
            last_change = board_ms();
            redraw = true;
        }
        now = board_ms();
        if (now - last_status >= 800) {
            uint16_t new_mv = board_battery_mv();
            redraw |= new_mv / 20u != battery_mv / 20u || screen == SCREEN_GPS ||
                      screen == SCREEN_APRS || (screen == SCREEN_RADIO && radio_audio_open());
            battery_mv = new_mv;
            last_status = now;
        }
        if (notice && now - notice_start >= 2700) {
            notice = NULL;
            redraw = true;
        }
        if (redraw)
            draw();
        __WFI();
    }
}
