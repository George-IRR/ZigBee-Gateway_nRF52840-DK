#ifndef BMP_180_H
#define BMP_180_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int16_t  ac1;
    int16_t  ac2;
    int16_t  ac3;
    uint16_t ac4;
    uint16_t ac5;
    uint16_t ac6;
    int16_t  b1;
    int16_t  b2;
    int16_t  mb;
    int16_t  mc;
    int16_t  md;
} bmp180_calib_t;

/**
 * @brief Initialize the BMP180 sensor over I2C and load calibration data.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int bmp180_init(void);

/**
 * @brief Read a compensated temperature from the BMP180 sensor.
 *
 * @param[out] temp_out Temperature in tenths of a degree Celsius (e.g. 150 = 15.0 C).
 * @return 0 on success, or a negative error code on failure.
 */
int bmp180_read_temperature(int32_t *temp_out);

/**
 * @brief Compute compensated temperature from a raw ADC value and calibration data.
 *
 * This is a pure mathematical function with no hardware dependencies.
 * Implements the BMP180 datasheet formula (section 3.5).
 *
 * @param[in]  UT   Raw uncompensated temperature ADC value.
 * @param[in]  cal  Pointer to calibration coefficients.
 * @param[out] temp_out Compensated temperature in tenths of a degree Celsius.
 * @return 0 on success, -EINVAL if cal or temp_out is NULL, -EDOM on division by zero.
 */
int bmp180_compute_temperature(int32_t UT, const bmp180_calib_t *cal, int32_t *temp_out);

#endif /* BMP_180_H */