#ifndef DRIVER_BMP180_BASIC_H
#define DRIVER_BMP180_BASIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize basic bmp180 driver.
 * Returns 0 on success.
 */
uint8_t bmp180_basic_init(void);

/**
 * Deinitialize basic bmp180 driver.
 */
uint8_t bmp180_basic_deinit(void);

/**
 * Read temperature (C) and pressure (Pa).
 * temperature: pointer to float
 * pressure: pointer to uint32_t
 */
uint8_t bmp180_basic_read(float *temperature, uint32_t *pressure);

/* helper exports used in example snippet */
void bmp180_interface_delay_ms(uint32_t ms);
void bmp180_interface_debug_print(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
