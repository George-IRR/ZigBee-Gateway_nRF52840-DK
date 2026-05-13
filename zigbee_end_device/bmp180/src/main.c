/* Example using the basic bmp180 wrapper API */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "driver_bmp180_basic.h"

int main(void)
{
    uint8_t res;
    uint32_t i;
    float temperature = 0.0f;
    uint32_t pressure = 0;

    res = bmp180_basic_init();
    if (res != 0) {
        printk("bmp180: init failed (%d)\n", res);
        return 1;
    }

    for (i = 0; i < 3; i++) {
        int32_t temp_centi;
        int32_t temp_abs;
        int32_t temp_int;
        int32_t temp_frac;

        bmp180_interface_delay_ms(1000);
        res = bmp180_basic_read(&temperature, &pressure);
        if (res != 0) {
            (void)bmp180_basic_deinit();
            return 1;
        }

        /* Zephyr printk/vprintk often omits float support; print fixed-point instead. */
        temp_centi = (int32_t)(temperature * 100.0f);
        temp_abs = (temp_centi < 0) ? -temp_centi : temp_centi;
        temp_int = temp_abs / 100;
        temp_frac = temp_abs % 100;
        bmp180_interface_debug_print("bmp180: temperature is %s%d.%02dC.\n",
                                     (temp_centi < 0) ? "-" : "",
                                     temp_int,
                                     temp_frac);
        bmp180_interface_debug_print("bmp180: pressure is %dPa.\n", pressure);
    }

    (void)bmp180_basic_deinit();
    return 0;
}