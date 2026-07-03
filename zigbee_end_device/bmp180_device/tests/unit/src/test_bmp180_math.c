/*
 * Unit tests for bmp180_compute_temperature().
 *
 * These tests exercise only the pure mathematical compensation formula
 * from BMP180 datasheet section 3.5. No hardware, no I2C, no Zephyr
 * kernel is required — runs on native_sim / unit_testing board.
 *
 * Reference values are taken directly from the BMP180 datasheet
 * "Example of Pressure and Temperature Calculation" (Bosch BST-BMP180-DS000-12).
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include "bmp_180.h"

/*
 * Datasheet calibration coefficients (Table 8 example).
 */
static const bmp180_calib_t ds_calib = {
    .ac1 =  408,
    .ac2 = -72,
    .ac3 = -14383,
    .ac4 =  32741,
    .ac5 =  32757,
    .ac6 =  23153,
    .b1  =  6190,
    .b2  =  4,
    .mb  = -32768,
    .mc  = -8711,
    .md  =  2868,
};

ZTEST_SUITE(bmp180_math, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Datasheet example: UT = 27898 → T = 150 (15.0 °C)
 *
 * This is the exact reference case from BMP180 datasheet section 3.5.
 */
ZTEST(bmp180_math, test_datasheet_example)
{
    int32_t temp = 0;
    int ret = bmp180_compute_temperature(27898, &ds_calib, &temp);

    zassert_ok(ret, "Expected success, got %d", ret);
    zassert_equal(temp, 150, "Expected 150 (15.0 C), got %d", temp);
}

/**
 * @brief NULL calibration pointer → -EINVAL
 */
ZTEST(bmp180_math, test_null_calib_pointer)
{
    int32_t temp = 0;
    int ret = bmp180_compute_temperature(27898, NULL, &temp);

    zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

/**
 * @brief NULL output pointer → -EINVAL
 */
ZTEST(bmp180_math, test_null_output_pointer)
{
    int ret = bmp180_compute_temperature(27898, &ds_calib, NULL);

    zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

/**
 * @brief Both pointers NULL → -EINVAL
 */
ZTEST(bmp180_math, test_both_pointers_null)
{
    int ret = bmp180_compute_temperature(27898, NULL, NULL);

    zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

/**
 * @brief Division by zero protection: force divisor (X1 + md) = 0.
 *
 * When UT = ac6, X1 = 0. Setting md = 0 forces the divisor to zero.
 */
ZTEST(bmp180_math, test_division_by_zero_guard)
{
    bmp180_calib_t bad_cal = ds_calib;
    bad_cal.md = 0;

    int32_t temp = 0;
    /* UT == ac6 → X1 = 0, divisor = X1 + md = 0 */
    int ret = bmp180_compute_temperature((int32_t)bad_cal.ac6, &bad_cal, &temp);

    zassert_equal(ret, -EDOM, "Expected -EDOM for division by zero, got %d", ret);
}

/**
 * @brief Temperature result is not modified on error.
 *
 * On -EDOM the output variable must remain unchanged.
 */
ZTEST(bmp180_math, test_output_unchanged_on_error)
{
    bmp180_calib_t bad_cal = ds_calib;
    bad_cal.md = 0;

    int32_t temp = 9999;
    bmp180_compute_temperature((int32_t)bad_cal.ac6, &bad_cal, &temp);

    zassert_equal(temp, 9999, "Output must not be modified on error");
}

/**
 * @brief Temperature is returned in tenths of a degree (fixed-point).
 *
 * 150 means 15.0 °C, not 150 °C.
 */
ZTEST(bmp180_math, test_fixed_point_unit)
{
    int32_t temp = 0;
    bmp180_compute_temperature(27898, &ds_calib, &temp);

    zassert_true(temp > 0 && temp < 1000,
                 "Temperature %d is outside expected 0..100.0 C range", temp);
}

/**
 * @brief A UT value lower than the reference must produce a lower temperature.
 *
 * UT=25000 stays within the valid ADC range for the datasheet calibration.
 * UT=20000 was intentionally avoided: it drives the divisor (X1 + md) negative
 * and out of the physically valid range, producing a non-physical result.
 */
ZTEST(bmp180_math, test_lower_ut_lower_temperature)
{
    int32_t temp_ref = 0;
    int32_t temp_low = 0;

    bmp180_compute_temperature(27898, &ds_calib, &temp_ref);
    bmp180_compute_temperature(25000, &ds_calib, &temp_low);

    zassert_true(temp_low < temp_ref,
                 "Lower UT (%d) must give lower T (%d) than ref (%d)",
                 25000, temp_low, temp_ref);
}

/**
 * @brief High UT value should produce a higher temperature than the reference.
 */
ZTEST(bmp180_math, test_higher_ut_higher_temperature)
{
    int32_t temp_ref = 0;
    int32_t temp_high = 0;

    bmp180_compute_temperature(27898, &ds_calib, &temp_ref);
    bmp180_compute_temperature(35000, &ds_calib, &temp_high);

    zassert_true(temp_high > temp_ref,
                 "Higher UT (%d) must give higher T (%d) than ref (%d)",
                 35000, temp_high, temp_ref);
}

