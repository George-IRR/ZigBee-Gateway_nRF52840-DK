#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

    if (!device_is_ready(i2c_dev)) {
        printk("Cannot init I2C\n");
        return 0;
    }

    printk("I2C scanning \n");

    // Scan I2C address from 0x08 to 0x77 
    for (uint8_t address = 8; address<= 119; address++) {
        uint8_t data = 0;
        
        // Send empty packet to address / ping
        if (i2c_write(i2c_dev, &data, 0, address) == 0) {
            printk("Device found at : 0x%02X\n", address);
        }
    }
    printk("Scan finished\n");
    
    return 0;
}