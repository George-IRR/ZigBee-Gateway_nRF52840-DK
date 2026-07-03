#include "bmp_180.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(bmp180, LOG_LEVEL_INF);

#define BMP180_I2C_ADDR   0x77
#define BMP180_REG_CALIB  0xAA
#define BMP180_REG_CTRL   0xF4
#define BMP180_CMD_TEMP   0x2E
#define BMP180_REG_DATA   0xF6
#define BMP180_CALIB_LEN  22

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static bmp180_calib_t calib_data;
static bool is_calibrated = false;

int bmp180_init(void)
{
#if defined(CONFIG_BMP180_DEVICE_TEST_MODE)
    LOG_INF("[TEST MODE] Mocked calibration data loaded");
    is_calibrated = true;
    return 0;
#endif

    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device i2c0 not ready");
        return -ENODEV;
    }

    uint8_t calib_reg = BMP180_REG_CALIB;
    uint8_t raw[BMP180_CALIB_LEN];
    int ret = i2c_write_read(i2c_dev, BMP180_I2C_ADDR, &calib_reg, 1, raw, BMP180_CALIB_LEN);
    if (ret < 0) {
        LOG_ERR("Failed to read calibration data (err %d)", ret);
        return ret;
    }

    calib_data.ac1 = (raw[ 0] << 8) | raw[ 1];
    calib_data.ac2 = (raw[ 2] << 8) | raw[ 3];
    calib_data.ac3 = (raw[ 4] << 8) | raw[ 5];
    calib_data.ac4 = (raw[ 6] << 8) | raw[ 7];
    calib_data.ac5 = (raw[ 8] << 8) | raw[ 9];
    calib_data.ac6 = (raw[10] << 8) | raw[11];
    calib_data.b1  = (raw[12] << 8) | raw[13];
    calib_data.b2  = (raw[14] << 8) | raw[15];
    calib_data.mb  = (raw[16] << 8) | raw[17];
    calib_data.mc  = (raw[18] << 8) | raw[19];
    calib_data.md  = (raw[20] << 8) | raw[21];

    LOG_INF("BMP180 calibration data loaded");
    is_calibrated = true;
    return 0;
}


int bmp180_read_temperature(int32_t *temp_out)
{
    if (temp_out == NULL) {
        return -EINVAL;
    }

#if defined(CONFIG_BMP180_DEVICE_TEST_MODE)
    static int32_t mock_temp = 200;
    *temp_out = mock_temp;
    LOG_INF("[TEST MODE] Mocked temperature: %d.%d C", *temp_out / 10, *temp_out % 10);
    mock_temp += 5;
    if (mock_temp > 300) {
        mock_temp = 200;
    }
    return 0;
#endif

    if (!is_calibrated) {
        int ret = bmp180_init();
        if (ret < 0) {
            return ret;
        }
    }

    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    uint8_t cmd[2] = {BMP180_REG_CTRL, BMP180_CMD_TEMP};
    int ret = i2c_write(i2c_dev, cmd, sizeof(cmd), BMP180_I2C_ADDR);
    if (ret < 0) {
        LOG_ERR("Failed to send temperature command (err %d)", ret);
        return ret;
    }

    k_msleep(5);

    uint8_t data_reg = BMP180_REG_DATA;
    uint8_t raw[2];
    ret = i2c_write_read(i2c_dev, BMP180_I2C_ADDR, &data_reg, 1, raw, 2);
    if (ret < 0) {
        LOG_ERR("Failed to read temperature register (err %d)", ret);
        return ret;
    }

    int32_t UT = (int32_t)((raw[0] << 8) | raw[1]);

    ret = bmp180_compute_temperature(UT, &calib_data, temp_out);
    if (ret < 0) {
        LOG_ERR("Temperature computation failed (err %d)", ret);
        return ret;
    }

    LOG_INF("Temperature: %d.%d C", *temp_out / 10, *temp_out % 10);
    return 0;
}