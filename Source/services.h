#ifndef SERVICES_H
#define SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t latitude;       /* Signed degrees times 10000000. */
    int32_t longitude;
    uint32_t updated_ms;
    bool valid;
    char utc[9];
} GpsFix;

void gps_poll(uint32_t now);
const GpsFix *gps_fix(void);
bool gps_fresh(uint32_t now);
void gps_clear(void);

typedef enum { TUNER_FM, TUNER_LW, TUNER_MW, TUNER_SW, TUNER_BANDS } TunerBand;
typedef struct {
    const char *name;
    uint16_t minimum, maximum, step, initial; /* 10 kHz units on FM, kHz on AM. */
} TunerRange;

extern const TunerRange tuner_ranges[TUNER_BANDS];
bool tuner_tune(TunerBand band, uint16_t frequency);
bool tuner_volume(uint8_t volume);
void tuner_stop(void);

bool storage_load(void *data, size_t length);
/* NULL on success, otherwise a status suitable for the settings footer. */
const char *storage_save(const void *data, size_t length);

#endif
