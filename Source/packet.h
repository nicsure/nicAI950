#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <stdint.h>
#include "services.h"

typedef struct {
    uint32_t count;
    char source[10];
    char destination[10];
    char info[257];
} PacketReception;

bool packet_callsign_valid(const char call[7]);
bool packet_prepare(const char call[7], uint8_t ssid, const GpsFix *fix);
bool packet_transmitting(void);
void packet_cancel_transmit(void);
void packet_reset_receiver(void);
void packet_poll(void);
const PacketReception *packet_latest(void);

#endif
