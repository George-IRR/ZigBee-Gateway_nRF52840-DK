#include "payload_parser.h"


void serialize_payload(const SensorPayload_t *payload, uint8_t *buffer) {
    buffer[0] = payload->node_id;

    // split temperature (2 Bytes) in 2 x 8bits
    buffer[1] = (uint8_t)(payload->temperature >> 8); // High byte (MSB)
    buffer[2] = (uint8_t)(payload->temperature & 0xFF); // Low byte (LSB)

    buffer[3] = payload->battery_level;
}


void deserialize_payload(const uint8_t *buffer, SensorPayload_t *payload) {
    // 1. Recuperăm ID-ul (este un singur octet, deci e simplu)
    payload->node_id = buffer[0];

    // 2. Reconstruim temperatura din cei doi octeți
    // Hint: (octet_high << 8) | octet_low
    payload->temperature = (int16_t)(buffer[1] << 8 | buffer[2]);

    // 3. Recuperăm nivelul bateriei
    payload->battery_level = buffer[3];
}