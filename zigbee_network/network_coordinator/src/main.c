#include <zephyr/kernel.h>
#include <zboss_api.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

void zboss_signal_handler(zb_bufid_t bufid)
{
    zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
    zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
    zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

    switch (sig) {
        case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (status == RET_OK) {
                printk("Coordinator network established.\n");
                /* Open network for end devices to join (180 second window) */
                bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
            }
            break;

        case ZB_ZDO_SIGNAL_DEVICE_ANNCE:
            printk("End device joined the network.\n");
            break;

        default:
            break;
    }

    /* Pass signal to default Zephyr Zigbee handler */
    zigbee_default_signal_handler(bufid);
    
    if (bufid) {
        zb_buf_free(bufid);
    }
}

int main(void)
{
    /* Initialize ZBOSS stack and form network */
    zigbee_enable();
    
    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}