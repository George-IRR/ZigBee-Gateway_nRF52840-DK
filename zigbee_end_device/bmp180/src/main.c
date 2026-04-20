#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    uint8_t address = 0x77;

    if (!device_is_ready(i2c_dev)) {
        printk("Cannot init I2C\n");
        return 0;
    }

    // Send empty packet to address / ping
    uint8_t data = 0;
    if (i2c_write(i2c_dev, &data, 0, address) == 0) {
        printk("Device found at : 0x%02X\n", address);
    }

    data = 0xD0;
    uint8_t read_data;
    i2c_write_read(i2c_dev, address, &data, sizeof(data), &read_data, sizeof(read_data));

    //k_msleep(1000);

    printk("Response: 0x%02X\n", read_data);
    
    return 0;
}