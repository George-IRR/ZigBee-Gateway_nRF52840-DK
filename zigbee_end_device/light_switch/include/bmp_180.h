#ifndef BMP_180_H
#define BMP_180_H

#include <stdint.h>

/**
 * @brief Initialize the BMP180 sensor.
 * 
 * @return 0 on success, or a negative error code on failure.
 */
int bmp180_init(void);

/**
 * @brief Read the temperature from the BMP180 sensor.
 * 
 * @return The temperature in tenths of a degree Celsius (e.g. 254 for 25.4°C),
 *         or 0 if reading fails.
 */
int bmp180_read_temperature(int32_t *temp_out);

#endif // BMP_180_H