#include "driver_bmp180_basic.h"
#include "driver_bmp180.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <stdarg.h>
#include <string.h>

static bmp180_handle_t bmp_handle;
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

void bmp180_interface_delay_ms(uint32_t ms)
{
    k_msleep(ms);
}

void bmp180_interface_debug_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

static uint8_t basic_iic_init(void)
{
    if (!device_is_ready(i2c_dev)) {
        return 1;
    }
    return 0;
}

static uint8_t basic_iic_deinit(void)
{
    /* nothing to do for Zephyr device */
    return 0;
}

static uint8_t basic_iic_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t addr7 = (uint16_t)(addr >> 1);
    int rc = i2c_write_read(i2c_dev, addr7, &reg, 1, buf, len);
    return rc == 0 ? 0 : 1;
}

static uint8_t basic_iic_write(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t addr7 = (uint16_t)(addr >> 1);
    uint8_t tmp[64];
    if (len + 1 > sizeof(tmp)) {
        return 1;
    }
    tmp[0] = reg;
    memcpy(&tmp[1], buf, len);
    int rc = i2c_write(i2c_dev, tmp, len + 1, addr7);
    return rc == 0 ? 0 : 1;
}

uint8_t bmp180_basic_init(void)
{
    DRIVER_BMP180_LINK_INIT(&bmp_handle, bmp180_handle_t);
    DRIVER_BMP180_LINK_IIC_INIT(&bmp_handle, basic_iic_init);
    DRIVER_BMP180_LINK_IIC_DEINIT(&bmp_handle, basic_iic_deinit);
    DRIVER_BMP180_LINK_IIC_READ(&bmp_handle, basic_iic_read);
    DRIVER_BMP180_LINK_IIC_WRITE(&bmp_handle, basic_iic_write);
    DRIVER_BMP180_LINK_DELAY_MS(&bmp_handle, bmp180_interface_delay_ms);
    DRIVER_BMP180_LINK_DEBUG_PRINT(&bmp_handle, bmp180_interface_debug_print);

    return bmp180_init(&bmp_handle);
}

uint8_t bmp180_basic_deinit(void)
{
    return bmp180_deinit(&bmp_handle);
}

uint8_t bmp180_basic_read(float *temperature, uint32_t *pressure)
{
    uint16_t temp_raw = 0;
    uint32_t pressure_raw = 0;
    float temp_c = 0.0f;
    uint8_t res = bmp180_read_temperature_pressure(&bmp_handle, &temp_raw, &temp_c, &pressure_raw, pressure);
    if (res != 0) return res;
    if (temperature) *temperature = temp_c;
    return 0;
}
