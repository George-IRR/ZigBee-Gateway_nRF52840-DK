#include "bmp_180.h"
#include <stddef.h>
#include <errno.h>

int bmp180_compute_temperature(int32_t UT, const bmp180_calib_t *cal, int32_t *temp_out)
{
    if (cal == NULL || temp_out == NULL) {
        return -EINVAL;
    }

    int32_t X1 = (UT - (int32_t)cal->ac6) * (int32_t)cal->ac5 / (1 << 15);
    int32_t divisor = X1 + (int32_t)cal->md;

    if (divisor == 0) {
        return -EDOM;
    }

    int32_t X2 = ((int32_t)cal->mc * (1 << 11)) / divisor;
    int32_t B5 = X1 + X2;

    *temp_out = (B5 + 8) / (1 << 4);
    return 0;
}
