#ifndef PAYLOAD_PARSER_H
#define PAYLOAD_PARSER_H

#include <stdint.h>

typedef struct {
    uint8_t node_id;
    int16_t temperature;
    uint8_t battery_level;

} SensorPayload_t;

void serialize_payload(const SensorPayload_t *payload, uint8_t *buffer);

void deserialize_payload(const uint8_t *buffer, SensorPayload_t *payload);

#endif