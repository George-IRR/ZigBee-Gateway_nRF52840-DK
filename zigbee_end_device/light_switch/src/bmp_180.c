#include "bmp_180.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmp180, LOG_LEVEL_INF);

#define BMP180_I2C_ADDR 0x77

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

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static bmp180_calib_t calib_data;

int bmp180_init(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device i2c0 not ready");
        return -ENODEV;
    }

    uint8_t calib_reg = 0xAA;
    uint8_t calib_read[22]; 
    int ret;
    
    ret = i2c_write_read(i2c_dev, BMP180_I2C_ADDR, &calib_reg, 1, calib_read, 22);
    if (ret < 0) {
        LOG_ERR("Failed to read calibration data from BMP180 (err %d)", ret);
        return ret;
    }

    calib_data.ac1 = (calib_read[0 ] << 8) | calib_read[1 ];
    calib_data.ac2 = (calib_read[2 ] << 8) | calib_read[3 ];
    calib_data.ac3 = (calib_read[4 ] << 8) | calib_read[5 ];
    calib_data.ac4 = (calib_read[6 ] << 8) | calib_read[7 ];
    calib_data.ac5 = (calib_read[8 ] << 8) | calib_read[9 ];
    calib_data.ac6 = (calib_read[10] << 8) | calib_read[11];
    calib_data.b1  = (calib_read[12] << 8) | calib_read[13];
    calib_data.b2  = (calib_read[14] << 8) | calib_read[15];
    calib_data.mb  = (calib_read[16] << 8) | calib_read[17];
    calib_data.mc  = (calib_read[18] << 8) | calib_read[19];
    calib_data.md  = (calib_read[20] << 8) | calib_read[21];
    
    LOG_INF("BMP180 Calibration Data Loaded Successfully.");
    return 0;
}

int32_t bmp180_read_temperature(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready during temp read");
        return 0;
    }

    // Read uncompressed temperature value
    uint8_t reg_addr = 0xF4;
    uint8_t command = 0x2E;
    uint8_t buffer[2] = {reg_addr, command};
    int ret;

    ret = i2c_write(i2c_dev, buffer, 2, BMP180_I2C_ADDR);
    if (ret < 0) {
        LOG_ERR("Failed to write temperature readout command (err %d)", ret);
        return 0;
    }
    
    // Wait for temperature readout
    k_msleep(5);
    
    uint8_t ut_reg = 0xF6;
    uint8_t ut_buffer[2];
    ret = i2c_write_read(i2c_dev, BMP180_I2C_ADDR, &ut_reg, 1, ut_buffer, 2);
    if (ret < 0) {
        LOG_ERR("Failed to read temperature register (err %d)", ret);
        return 0;
    }

    int32_t UT = (int32_t)((ut_buffer[0] << 8) | ut_buffer[1]);

    int32_t X1, X2, B5, T;
    X1 = (UT - (int32_t)calib_data.ac6) * (int32_t)calib_data.ac5 / (1 << 15);
    X2 = ((int32_t)calib_data.mc * (1 << 11)) / (X1 + (int32_t)calib_data.md);
    B5 = X1 + X2;
    T = (B5 + 8) / (1 << 4);

    LOG_INF("Temperature: %d.%d C", T / 10, T % 10);
    return T;
}
