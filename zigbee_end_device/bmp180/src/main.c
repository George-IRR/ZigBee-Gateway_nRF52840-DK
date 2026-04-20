#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
uint8_t address = 0x77;

void read_calibration()
{
    uint8_t calib_reg = 0xAA;
    uint8_t calib_read[22]; 
    
    i2c_write_read(i2c_dev, address, &calib_reg, sizeof(calib_reg), calib_read, sizeof(calib_read));
    
    int16_t AC1 = (calib_read[0 ] << 8) | calib_read[1 ];
    int16_t AC2 = (calib_read[2 ] << 8) | calib_read[3 ];
    int16_t AC3 = (calib_read[4 ] << 8) | calib_read[5 ];
    uint16_t AC4 = (calib_read[6 ] << 8) | calib_read[7 ];
    uint16_t AC5 = (calib_read[8 ] << 8) | calib_read[9 ];
    uint16_t AC6 = (calib_read[10] << 8) | calib_read[11];
    int16_t B1  = (calib_read[12] << 8) | calib_read[13];
    int16_t B2  = (calib_read[14] << 8) | calib_read[15];
    int16_t MB  = (calib_read[16] << 8) | calib_read[17];
    int16_t MC  = (calib_read[18] << 8) | calib_read[19];
    int16_t MD  = (calib_read[20] << 8) | calib_read[21];

    

    printk("AC1: %d\n", AC1);
    printk("AC2: %d\n", AC2);
    printk("AC3: %d\n", AC3);
    printk("AC4: %d\n", AC4);
    printk("AC5: %d\n", AC5);
    printk("AC6: %d\n", AC6);
    printk("B1 : %d\n", B1 );
    printk("B2 : %d\n", B2 );
    printk("MB : %d\n", MB );
    printk("MC : %d\n", MC );
    printk("MD : %d\n", MD );

    
}


void read_temperature()
{
    uint8_t out_msb_reg = 0xF6; 
    uint8_t out_lsb_reg = 0xF7;

    //read uncompressed temperature value
    uint8_t reg_addr = 0xF4;
    uint8_t command = 0x2E;
    uint8_t buffer[2] = {reg_addr, command};
    i2c_write(i2c_dev, buffer, 2, address);
    
    uint8_t msb, lsb;
    k_msleep(5);
    
    //read msb and lsb 
    i2c_write_read(i2c_dev, address, &out_msb_reg, 2, &msb, 2);
    i2c_write_read(i2c_dev, address, &out_lsb_reg, 2, &lsb, 2);

    int32_t UT = (int32_t)( (msb << 8) | lsb);
    printk("UT: 0x%08X\n", UT);
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
    
    read_calibration();
    read_temperature();
    
    return 0;
}