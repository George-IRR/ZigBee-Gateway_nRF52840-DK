#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
uint8_t address = 0x77;

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

static bmp180_calib_t calib_data;

void bmp_init()
{
    uint8_t calib_reg = 0xAA;
    uint8_t calib_read[22]; 
    
    i2c_write_read(i2c_dev, address, &calib_reg, 1, calib_read, 22);

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
    
    printk("BMP180 Calibration Data Loaded Successfully.\n");
}


void read_temperature()
{
    // read uncompressed temperature value
    uint8_t reg_addr = 0xF4;
    uint8_t command = 0x2E;
    uint8_t buffer[2] = {reg_addr, command};
    i2c_write(i2c_dev, buffer, 2, address);
    
    // wait for temp readout
    k_msleep(5);
    
    uint8_t ut_reg = 0xF6;
    uint8_t ut_buffer[2];
    i2c_write_read(i2c_dev, address, &ut_reg, 1, ut_buffer, 2);


    int32_t UT = (int32_t)((ut_buffer[0] << 8) | ut_buffer[1]);
    // printk("UT: 0x%08X\n", UT);

    int32_t X1, X2, B5, T;
    X1 = (UT - (int32_t)calib_data.ac6) * (int32_t)calib_data.ac5 / (1 << 15);
    X2 = ((int32_t)calib_data.mc * (1 << 11)) / (X1 + (int32_t)calib_data.md);
    B5 = X1 + X2;
    T = (B5 + 8) / (1 << 4);

    printk("Temperature: %d.%d C\n", T / 10, T % 10);
}

int main(void)
{
    if (!device_is_ready(i2c_dev)) {
        printk("Cannot init I2C\n");
        return 0;
    }

    // Send empty packet to address / ping
    uint8_t data = 0;
    if (i2c_write(i2c_dev, &data, 0, address) == 0) {
        printk("Device found at : 0x%02X\n", address);
    }
    
    //read_calibration();
    bmp_init();
    read_temperature();
    
    return 0;
}