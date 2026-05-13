#include "unity.h"
#include "payload_parser.h"

// Această funcție rulează înainte de fiecare test (opțional)
void setUp(void) {}

// Această funcție rulează după fiecare test (opțional)
void tearDown(void) {}

void test_serialization_deserialization_logic(void) {
    // 1. DATE DE INTRARE (Ce trimitem)
    SensorPayload_t input = {
        .node_id = 10,
        .temperature = 2550, // 25.50 C
        .battery_level = 95
    };
    
    uint8_t buffer[4];
    SensorPayload_t output;

    // 2. EXECUȚIE (Ce testăm)
    serialize_payload(&input, buffer);
    deserialize_payload(buffer, &output);

    // 3. VERIFICARE (Assert)
    // Robotul de CI va verifica dacă input == output
    TEST_ASSERT_EQUAL_UINT8(input.node_id, output.node_id);
    TEST_ASSERT_EQUAL_INT16(input.temperature, output.temperature);
    TEST_ASSERT_EQUAL_UINT8(input.battery_level, output.battery_level);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_serialization_deserialization_logic);
    return UNITY_END();
}