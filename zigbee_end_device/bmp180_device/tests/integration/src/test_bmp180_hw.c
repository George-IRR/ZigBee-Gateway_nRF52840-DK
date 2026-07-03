/*
 * Integration tests for the BMP180 driver.
 *
 * These tests run on real nRF52840-DK hardware with a BMP180 sensor
 * physically connected to the I2C0 bus (SDA=P0.26, SCL=P0.27).
 *
 * What is tested here:
 *   - I2C communication with the sensor
 *   - Calibration data loading (bmp180_init)
 *   - Full read pipeline (bmp180_read_temperature)
 *   - Physical plausibility of returned values
 *   - Repeatability of consecutive reads
 *
 * What is NOT tested here (covered by unit tests):
 *   - The compensation formula math
 *   - NULL pointer guards
 *   - Division-by-zero protection
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include "bmp_180.h"

/* Physically plausible temperature range for an indoor sensor: -10.0 to 85.0 C */
#define TEMP_MIN_TENTHS  (-100)
#define TEMP_MAX_TENTHS  ( 850)

/* Maximum acceptable drift between two consecutive reads (in tenths of C) */
#define TEMP_MAX_DRIFT   50

static void *suite_setup(void)
{
    int ret = bmp180_init();
    zassert_ok(ret, "bmp180_init() failed with %d — check I2C wiring", ret);
    return NULL;
}

ZTEST_SUITE(bmp180_hardware, NULL, suite_setup, NULL, NULL, NULL);

/**
 * @brief bmp180_init() must succeed on hardware with a connected sensor.
 */
ZTEST(bmp180_hardware, test_init_succeeds)
{
    int ret = bmp180_init();
    zassert_ok(ret, "Re-init failed with %d", ret);
}

/**
 * @brief A single read must succeed and return a plausible temperature.
 */
ZTEST(bmp180_hardware, test_read_succeeds_and_is_plausible)
{
    int32_t temp = 0;
    int ret = bmp180_read_temperature(&temp);

    zassert_ok(ret, "bmp180_read_temperature() failed with %d", ret);
    zassert_true(temp >= TEMP_MIN_TENTHS && temp <= TEMP_MAX_TENTHS,
                 "Temperature %d is outside plausible range [%d, %d]",
                 temp, TEMP_MIN_TENTHS, TEMP_MAX_TENTHS);
}

/**
 * @brief Two consecutive reads must produce values within TEMP_MAX_DRIFT of each other.
 *
 * A large jump in temperature between two reads ~5 ms apart indicates
 * a communication error or sensor fault.
 */
ZTEST(bmp180_hardware, test_consecutive_reads_are_consistent)
{
    int32_t temp1 = 0;
    int32_t temp2 = 0;

    zassert_ok(bmp180_read_temperature(&temp1), "First read failed");

    /* The sensor conversion time is 4.5 ms; wait to ensure it is ready */
    k_msleep(10);

    zassert_ok(bmp180_read_temperature(&temp2), "Second read failed");

    int32_t drift = temp1 - temp2;
    if (drift < 0) {
        drift = -drift;
    }

    zassert_true(drift <= TEMP_MAX_DRIFT,
                 "Consecutive reads differ by %d tenths-C (max allowed %d)",
                 drift, TEMP_MAX_DRIFT);
}

/**
 * @brief Five repeated reads must all succeed and stay within plausible range.
 */
ZTEST(bmp180_hardware, test_repeated_reads_all_succeed)
{
    for (int i = 0; i < 5; i++) {
        int32_t temp = 0;
        int ret = bmp180_read_temperature(&temp);

        zassert_ok(ret, "Read %d failed with %d", i, ret);
        zassert_true(temp >= TEMP_MIN_TENTHS && temp <= TEMP_MAX_TENTHS,
                     "Read %d: temperature %d out of range", i, temp);

        k_msleep(10);
    }
}

/**
 * @brief bmp180_read_temperature() with NULL pointer must return -EINVAL
 *        without touching I2C (fast guard path).
 */
ZTEST(bmp180_hardware, test_read_null_pointer_returns_einval)
{
    int ret = bmp180_read_temperature(NULL);
    zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}
