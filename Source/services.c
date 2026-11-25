#include "services.h"
#include "board.h"
#include <stdio.h>
#include <string.h>

static GpsFix fix;
static char nmea[96];
static unsigned nmea_length;

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool coordinate(const char *s, const char *hemisphere, bool latitude, int32_t *result)
{
    unsigned degrees = 0, degree_digits = latitude ? 2 : 3;
    for (unsigned i = 0; i < degree_digits; ++i) {
        if (!digit(*s))
            return false;
        degrees = degrees * 10 + (unsigned)(*s++ - '0');
    }
    if (!digit(s[0]) || !digit(s[1]))
        return false;
    unsigned minutes = (unsigned)(s[0] - '0') * 10u + (unsigned)(s[1] - '0');
    unsigned limit = latitude ? 90u : 180u;
    if (minutes >= 60 || degrees > limit)
        return false;
    s += 2;
    uint32_t fraction = 0, place = 1000000;
    if (*s == '.')
        ++s;
    while (digit(*s)) {
        fraction += (unsigned)(*s++ - '0') * place;
        place /= 10;
    }
    if (*s || (degrees == limit && (minutes || fraction)))
        return false;

    int32_t value = (int32_t)(degrees * 10000000u + (minutes * 10000000u + fraction + 30u) / 60u);
    if (!hemisphere[0] || hemisphere[1])
        return false;
    if (*hemisphere == (latitude ? 'S' : 'W'))
        value = -value;
    else if (*hemisphere != (latitude ? 'N' : 'E'))
        return false;
    *result = value;
    return true;
}

static void parse_nmea(uint32_t now)
{
    char *checksum = strchr(nmea, '*');
    if (!checksum || strlen(checksum) != 3)
        return;
    int high = hex_digit(checksum[1]), low = hex_digit(checksum[2]);
    uint8_t expected = 0;
    for (const char *c = nmea; c < checksum; ++c)
        expected ^= (uint8_t)*c;
    if (high < 0 || low < 0 || (unsigned)(high * 16 + low) != expected)
        return;
    *checksum = '\0';

    char *field[10] = {nmea};
    unsigned count = 1;
    for (char *c = nmea; *c && count < 10; ++c) {
        if (*c == ',') {
            *c = '\0';
            field[count++] = c + 1;
        }
    }
    if (count < 8 || strlen(field[0]) != 5)
        return;
    bool rmc = strcmp(field[0] + 2, "RMC") == 0;
    if (!rmc && strcmp(field[0] + 2, "GGA"))
        return;
    bool valid = rmc ? strcmp(field[2], "A") == 0 :
                 (field[6][0] >= '1' && field[6][0] <= '9');
    unsigned index = rmc ? 3 : 2;
    int32_t lat, lon;
    if (!valid || !coordinate(field[index], field[index + 1], true, &lat) ||
        !coordinate(field[index + 2], field[index + 3], false, &lon)) {
        fix.valid = false;
        return;
    }

    fix.latitude = lat;
    fix.longitude = lon;
    fix.updated_ms = now;
    fix.valid = true;
    if (strlen(field[1]) >= 6) {
        const char *time = field[1];
        char utc[] = {time[0], time[1], ':', time[2], time[3], ':', time[4], time[5], 0};
        memcpy(fix.utc, utc, sizeof(utc));
    }
}

void gps_clear(void)
{
    memset(&fix, 0, sizeof(fix));
    nmea_length = 0;
    nmea[0] = 0;
}

void gps_poll(uint32_t now)
{
    int byte;
    while ((byte = board_gps_get()) >= 0) {
        if (byte == '$') {
            nmea_length = 1;
        } else if (nmea_length && (byte == '\r' || byte == '\n')) {
            nmea[nmea_length - 1] = 0;
            parse_nmea(now);
            nmea_length = 0;
        } else if (nmea_length && nmea_length < sizeof(nmea)) {
            nmea[nmea_length++ - 1] = (char)byte;
        } else {
            nmea_length = 0;
        }
    }
}

const GpsFix *gps_fix(void) { return &fix; }
bool gps_fresh(uint32_t now) { return fix.valid && now - fix.updated_ms < 5000u; }

#define SI_SCL PIN(6)
#define SI_SDA PIN(7)
#define SI_CTS 0x80u
#define SI_ERR 0x40u
#define SI_STC 0x01u

const TunerRange tuner_ranges[TUNER_BANDS] = {
    {"FM", 8750, 10800, 10, 9950},
    {"LW", 153, 279, 9, 198},
    {"MW", 520, 1710, 1, 999},
    {"SW", 2300, 23000, 5, 7100}
};

static uint8_t si_address;
static bool si_powered, si_fm;

/* The tuner takes SCL as an input. SDA is bidirectional and needs the
   MCU's pull-up when released on the RT-950 Pro. */
static void i2c_sda_release(void)
{
    board_pin_mode(GPIOB, SI_SDA, GPIO_MODE_INPUT,
                   GPIO_PULL_UP, GPIO_OUTPUT_OPEN_DRAIN);
}

static void i2c_sda_low(void)
{
    GPIO_LOW(GPIOB, SI_SDA);
    board_pin_mode(GPIOB, SI_SDA, GPIO_MODE_OUTPUT,
                   GPIO_PULL_NONE, GPIO_OUTPUT_OPEN_DRAIN);
}

static void i2c_clock_high(void)
{
    GPIO_HIGH(GPIOB, SI_SCL);
    board_delay_us(5);
}

static bool i2c_start(void)
{
    i2c_sda_release();
    i2c_clock_high();
    if (!GPIO_READ(GPIOB, SI_SCL) || !GPIO_READ(GPIOB, SI_SDA))
        return false;
    i2c_sda_low();
    board_delay_us(5);
    GPIO_LOW(GPIOB, SI_SCL);
    board_delay_us(5);
    return true;
}

static void i2c_stop(void)
{
    i2c_sda_low();
    board_delay_us(5);
    i2c_clock_high();
    i2c_sda_release();
    board_delay_us(5);
}

static bool i2c_recover(void)
{
    i2c_sda_release();
    i2c_clock_high();
    if (!GPIO_READ(GPIOB, SI_SCL))
        return false;
    if (GPIO_READ(GPIOB, SI_SDA))
        return true;

    /* A slave left mid-byte can release SDA after up to nine clocks. */
    for (unsigned i = 0; i < 9 && !GPIO_READ(GPIOB, SI_SDA); ++i) {
        GPIO_LOW(GPIOB, SI_SCL);
        board_delay_us(5);
        i2c_clock_high();
    }
    GPIO_LOW(GPIOB, SI_SCL);
    i2c_stop();
    return GPIO_READ(GPIOB, SI_SCL) && GPIO_READ(GPIOB, SI_SDA);
}

static bool i2c_write(uint8_t data)
{
    for (unsigned mask = 0x80; mask; mask >>= 1) {
        if (data & mask)
            i2c_sda_release();
        else
            i2c_sda_low();
        board_delay_us(5);
        i2c_clock_high();
        GPIO_LOW(GPIOB, SI_SCL);
    }
    i2c_sda_release();
    board_delay_us(5);
    i2c_clock_high();
    bool ack = !GPIO_READ(GPIOB, SI_SDA);
    GPIO_LOW(GPIOB, SI_SCL);
    return ack;
}

static uint8_t i2c_read(bool ack)
{
    uint8_t data = 0;
    i2c_sda_release();
    for (unsigned i = 0; i < 8; ++i) {
        board_delay_us(5);
        i2c_clock_high();
        data = (uint8_t)((data << 1) | GPIO_READ(GPIOB, SI_SDA));
        GPIO_LOW(GPIOB, SI_SCL);
    }
    if (ack)
        i2c_sda_low();
    board_delay_us(5);
    i2c_clock_high();
    GPIO_LOW(GPIOB, SI_SCL);
    i2c_sda_release();
    return data;
}

static bool si_write(const uint8_t *bytes, unsigned count)
{
    if (!i2c_start())
        return false;
    bool ok = i2c_write(si_address << 1);
    for (unsigned i = 0; i < count && ok; ++i)
        ok = i2c_write(bytes[i]);
    i2c_stop();
    return ok;
}

static bool si_read(uint8_t *bytes, unsigned count)
{
    if (!i2c_start())
        return false;
    bool ok = i2c_write((si_address << 1) | 1u);
    if (ok)
        for (unsigned i = 0; i < count; ++i)
            bytes[i] = i2c_read(i + 1 < count);
    i2c_stop();
    return ok;
}

static bool si_ready(unsigned timeout, uint8_t *response)
{
    uint32_t start = board_ms();
    do {
        uint8_t status;
        if (si_read(&status, 1) && (status & SI_CTS)) {
            if (response)
                *response = status;
            return (status & SI_ERR) == 0;
        }
        board_delay_ms(2);
    } while (board_ms() - start < timeout);
    return false;
}

static bool si_command(const uint8_t *bytes, unsigned count)
{
    return si_write(bytes, count) && si_ready(100, NULL);
}

static const char *si_wait_tune(bool fm)
{
    const uint8_t command = 0x14; /* GET_INT_STATUS latches STC into the reply. */
    uint32_t start = board_ms();
    do {
        uint8_t status;
        if (!si_write(&command, 1) || !si_ready(20, &status))
            return "TUNER STATUS FAILED";
        if (status & SI_STC) {
            uint8_t ack[] = {fm ? 0x22 : 0x42, 1};
            return si_command(ack, sizeof(ack)) ? NULL : "TUNER STATUS FAILED";
        }
        board_delay_ms(5);
    } while (board_ms() - start < 650);
    return "TUNER TUNE TIMEOUT";
}

void tuner_stop(void)
{
    board_audio(false, false);
    GPIO_LOW(GPIOA, PIN(6));
    GPIO_HIGH(GPIOA, PIN(7));
    GPIO_LOW(GPIOC, PIN(5));
    si_powered = false;
}

static const char *si_start(bool fm)
{
    tuner_stop();
    if (fm) {
        GPIO_LOW(GPIOA, PIN(7));
    } else {
        GPIO_HIGH(GPIOC, PIN(5));
    }
    board_delay_ms(10);
    GPIO_HIGH(GPIOA, PIN(6));
    board_delay_ms(10);
    if (!i2c_recover())
        return "TUNER I2C BUS HELD LOW";

    /* After reset, POWER_UP is the first command. CTS is checked afterwards.
       Use the crystal oscillator and analog audio output. */
    const uint8_t addresses[] = {0x11, 0x63};
    uint8_t power[] = {0x01, fm ? 0x10 : 0x11, 0x05};
    for (unsigned i = 0; i < sizeof(addresses); ++i) {
        si_address = addresses[i];
        if (!si_write(power, sizeof(power)))
            continue;
        uint8_t status = 0;
        if (!si_ready(500, &status))
            return status & SI_ERR ? "TUNER POWER-UP REJECTED" : "TUNER POWER-UP TIMEOUT";
        board_delay_ms(500);
        si_powered = true;
        si_fm = fm;
        return NULL;
    }
    return "TUNER NO I2C ACK";
}

static bool si_volume(uint8_t volume)
{
    uint8_t set[] = {0x12, 0, 0x40, 0, 0, volume};
    return si_command(set, sizeof(set));
}

const char *tuner_tune(TunerBand band, uint16_t frequency, uint8_t volume)
{
    bool fm = band == TUNER_FM;
    board_audio(false, false);
    const char *error = !si_powered || fm != si_fm ? si_start(fm) : NULL;
    uint8_t tune[] = {fm ? 0x20 : 0x40, 0, (uint8_t)(frequency >> 8),
                      (uint8_t)frequency, 0, band == TUNER_SW ? 1 : 0};
    if (!error) {
        if (!si_volume(volume))
            error = "TUNER VOLUME FAILED";
        else if (!si_command(tune, fm ? 5 : 6))
            error = "TUNER TUNE COMMAND FAILED";
        else
            error = si_wait_tune(fm);
    }
    if (error)
        tuner_stop();
    else
        board_audio(false, true);
    return error;
}

#define FLASH_CS PIN(12)
#define FLASH_CLK PIN(13)
#define FLASH_DI PIN(15)
#define FLASH_SECTOR 4096u
#define FLASH_STORAGE_BASE 0x0c0000u
#define FLASH_STORAGE_SLOTS 4u
#define FLASH_LEGACY_A 0x3fe000u
#define FLASH_LEGACY_B 0x3ff000u
#define FLASH_COMMIT 0x39354346u
#define FLASH_PENDING (FLASH_COMMIT | 0x80000000u)

typedef struct {
    uint32_t commit;
    uint32_t serial;
    uint32_t length;
    uint32_t crc;
} StorageHeader;

static uint32_t flash_slot, flash_serial;
static uint32_t flash_id, flash_size;
static bool flash_transfer_ok;

static void flash_init(void)
{
    spi_init_type config;
    crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);
    spi_i2s_reset(SPI2);
    GPIO_HIGH(GPIOB, FLASH_CS);
    board_pin_mode(GPIOB, FLASH_CLK | FLASH_DI, GPIO_MODE_MUX,
                   GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
    spi_default_para_init(&config);
    config.master_slave_mode = SPI_MODE_MASTER;
    config.mclk_freq_division = SPI_MCLK_DIV_16;
    config.clock_polarity = SPI_CLOCK_POLARITY_HIGH;
    config.clock_phase = SPI_CLOCK_PHASE_2EDGE;
    config.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
    spi_init(SPI2, &config);
    spi_software_cs_internal_level_set(SPI2, SPI_SWCS_INTERNAL_LEVEL_HIGHT);
    spi_enable(SPI2, TRUE);
}

static bool spi_wait(uint32_t flag, bool set)
{
    uint32_t start = board_ms();
    while (((SPI2->sts & flag) != 0) != set)
        if (board_ms() - start >= 2u)
            return false;
    return true;
}

static uint8_t spi(uint8_t output)
{
    if (flash_transfer_ok && spi_wait(SPI_I2S_TDBE_FLAG, true)) {
        spi_i2s_data_transmit(SPI2, output);
        if (spi_wait(SPI_I2S_RDBF_FLAG, true))
            return (uint8_t)spi_i2s_data_receive(SPI2);
    }
    flash_transfer_ok = false;
    return 0xffu;
}

static void flash_begin(uint8_t command)
{
    flash_transfer_ok = true;
    GPIO_LOW(GPIOB, FLASH_CS);
    board_delay_us(1);
    spi(command);
}

static bool flash_end(void)
{
    bool ok = flash_transfer_ok && spi_wait(SPI_I2S_BF_FLAG, false);
    GPIO_HIGH(GPIOB, FLASH_CS);
    board_delay_us(1);
    return ok;
}

static void flash_address(uint32_t address)
{
    spi((uint8_t)(address >> 16));
    spi((uint8_t)(address >> 8));
    spi((uint8_t)address);
}

static bool flash_read(uint32_t address, void *data, size_t count)
{
    uint8_t *bytes = data;
    flash_begin(0x03);
    flash_address(address);
    for (size_t i = 0; i < count; ++i)
        bytes[i] = spi(0);
    return flash_end();
}

static uint8_t flash_status(void)
{
    flash_begin(0x05);
    uint8_t status = spi(0);
    return flash_end() ? status : 0xffu;
}

static bool flash_wait(uint32_t timeout)
{
    uint32_t start = board_ms();
    do {
        if (!(flash_status() & 1u))
            return true;
        board_delay_ms(1);
    } while (board_ms() - start < timeout);
    return false;
}

static bool flash_write_enable(void)
{
    flash_begin(0x06);
    return flash_end() && (flash_status() & 3u) == 2u;
}

static bool flash_program(uint32_t address, const void *data, size_t remaining)
{
    const uint8_t *bytes = data;
    while (remaining) {
        size_t size = 256u - (address & 255u);
        if (size > remaining)
            size = remaining;
        if (!flash_write_enable())
            return false;
        flash_begin(0x02);
        flash_address(address);
        for (size_t i = 0; i < size; ++i)
            spi(bytes[i]);
        if (!flash_end() || !flash_wait(20))
            return false;
        address += size;
        bytes += size;
        remaining -= size;
    }
    return true;
}

static uint32_t crc_byte(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (unsigned i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    return crc;
}

static uint32_t header_crc(const StorageHeader *header)
{
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 32; i += 8) {
        crc = crc_byte(crc, (uint8_t)(header->serial >> i));
        crc = crc_byte(crc, (uint8_t)(header->length >> i));
    }
    return crc;
}

static bool flash_check(uint32_t address, const StorageHeader *header)
{
    uint32_t crc = header_crc(header);
    flash_begin(0x03);
    flash_address(address + sizeof(*header));
    for (uint32_t i = 0; i < header->length; ++i)
        crc = crc_byte(crc, spi(0));
    return flash_end() && ~crc == header->crc;
}

static bool flash_probe(void)
{
    /* The bootloader can leave the flash in deep power-down. */
    flash_begin(0xabu);
    flash_end();
    board_delay_us(50);
    flash_begin(0x9f);
    uint8_t maker = spi(0xffu);
    uint8_t type = spi(0xffu);
    uint8_t capacity = spi(0xffu);
    flash_id = (uint32_t)maker << 16 | (uint32_t)type << 8 | capacity;
    flash_size = 0;
    if (!flash_end() || maker == 0 || maker == 0xffu || type == 0 || type == 0xffu ||
        (capacity != 0x15u && capacity != 0x16u))
        return false;

    if (maker != 0xefu) {
        /* Accept a compatible 2 or 4 MiB SPI NOR with an SFDP signature. */
        uint8_t signature[4];
        flash_begin(0x5au);
        flash_address(0);
        spi(0xffu); /* Dummy byte before the SFDP table. */
        for (unsigned i = 0; i < sizeof(signature); ++i)
            signature[i] = spi(0xffu);
        if (!flash_end() || memcmp(signature, "SFDP", sizeof(signature)))
            return false;
    }
    flash_size = UINT32_C(1) << capacity;
    return true;
}

static void load_slot(uint32_t address, size_t length)
{
    StorageHeader header;
    if (flash_read(address, &header, sizeof(header)) &&
        header.commit == FLASH_COMMIT && header.length == length &&
        (!flash_slot || (int32_t)(header.serial - flash_serial) > 0) &&
        flash_check(address, &header)) {
        flash_slot = address;
        flash_serial = header.serial;
    }
}

bool storage_load(void *data, size_t length)
{
    flash_init();
    if (length > FLASH_SECTOR - sizeof(StorageHeader) || !flash_probe() || !flash_wait(500))
        return false;

    for (unsigned i = 0; i < FLASH_STORAGE_SLOTS; ++i)
        load_slot(FLASH_STORAGE_BASE + i * FLASH_SECTOR, length);

    /* Read older settings from the update area if present. All future writes
       go below the bootloader's fonts and outside its update area. */
    if (!flash_slot && flash_size >= FLASH_LEGACY_B + FLASH_SECTOR) {
        load_slot(FLASH_LEGACY_A, length);
        load_slot(FLASH_LEGACY_B, length);
    }
    return flash_slot && flash_read(flash_slot + sizeof(StorageHeader), data, length);
}

static bool flash_erased(uint32_t address)
{
    flash_begin(0x03);
    flash_address(address);
    bool erased = true;
    for (unsigned i = 0; i < FLASH_SECTOR; ++i) {
        if (spi(0xffu) != 0xffu) {
            erased = false;
            break;
        }
    }
    return flash_end() && erased;
}

const char *storage_save(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    if (length > FLASH_SECTOR - sizeof(StorageHeader))
        return "SETTINGS TOO LARGE";
    if (!flash_size && !flash_probe()) {
        static char message[20];
        snprintf(message, sizeof(message), "FLASH ID %06lX", (unsigned long)flash_id);
        return message;
    }
    if (!flash_wait(500))
        return "FLASH BUSY TIMEOUT";

    uint32_t slot = 0;
    bool erase = false;
    unsigned first = 0;
    if (flash_slot >= FLASH_STORAGE_BASE &&
        flash_slot < FLASH_STORAGE_BASE + FLASH_STORAGE_SLOTS * FLASH_SECTOR)
        first = (flash_slot - FLASH_STORAGE_BASE) / FLASH_SECTOR + 1u;
    for (unsigned i = 0; i < FLASH_STORAGE_SLOTS; ++i) {
        uint32_t candidate = FLASH_STORAGE_BASE + (first + i) % FLASH_STORAGE_SLOTS * FLASH_SECTOR;
        uint32_t marker;
        if (candidate == flash_slot)
            continue;
        if (!flash_read(candidate, &marker, sizeof(marker)))
            return "FLASH READ FAILED";
        erase = marker == FLASH_COMMIT || marker == FLASH_PENDING;
        if (erase || (marker == UINT32_MAX && flash_erased(candidate))) {
            slot = candidate;
            break;
        }
    }
    if (!slot)
        return "FLASH SETTINGS SECTORS IN USE";

    if (erase) {
        if (!flash_write_enable())
            return "FLASH WRITE ENABLE FAILED";
        flash_begin(0x20);
        flash_address(slot);
        if (!flash_end() || !flash_wait(500) || !flash_erased(slot))
            return "FLASH ERASE FAILED";
    }

    StorageHeader header = {FLASH_COMMIT, flash_serial + 1u, length, 0};
    uint32_t crc = header_crc(&header);
    for (size_t i = 0; i < length; ++i)
        crc = crc_byte(crc, bytes[i]);
    header.crc = ~crc;
    /* Claim an erased sector before the payload. A complete commit only clears
       one extra bit, and the preceding valid settings stay available. */
    const uint32_t pending = FLASH_PENDING;
    if (!flash_program(slot, &pending, sizeof(pending)) ||
        !flash_program(slot + sizeof(header.commit), &header.serial,
                       sizeof(header) - sizeof(header.commit)) ||
        !flash_program(slot + sizeof(header), data, length))
        return "FLASH SETTINGS WRITE FAILED";
    if (!flash_check(slot, &header))
        return "FLASH SETTINGS VERIFY FAILED";
    if (!flash_program(slot, &header.commit, sizeof(header.commit)))
        return "FLASH SETTINGS COMMIT FAILED";

    StorageHeader written;
    if (!flash_read(slot, &written, sizeof(written)) || memcmp(&written, &header, sizeof(header)))
        return "FLASH SETTINGS VERIFY FAILED";
    flash_slot = slot;
    flash_serial = header.serial;
    return NULL;
}
