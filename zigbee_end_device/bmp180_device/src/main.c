/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief Dimmer switch for HA profile implementation.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <dk_buttons_and_leds.h>
#include <ram_pwrdn.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>
#include "zb_mem_config_custom.h"
#include "zb_dimmer_switch.h"
#include "bmp_180.h"
#include "my_nrf52_timer.h"
#include "my_rtc.h"
#include <zephyr/irq.h>

#if defined(CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER)
#include <osif/mac_platform.h>
#endif

/* Source endpoint used to control light bulb. */
#define LIGHT_SWITCH_ENDPOINT      1
/* Delay between the light switch startup and light bulb finding procedure. */
#define MATCH_DESC_REQ_START_DELAY K_SECONDS(2)
/* Timeout for finding procedure. */
#define MATCH_DESC_REQ_TIMEOUT     K_SECONDS(5)
/* Find only non-sleepy device. */
#define MATCH_DESC_REQ_ROLE        ZB_NWK_BROADCAST_RX_ON_WHEN_IDLE

/* Do not erase NVRAM to save the network parameters after device reboot or
 * power-off. NOTE: If this option is set to ZB_TRUE then do full device erase
 * for all network devices before running other samples.
 */
#define ERASE_PERSISTENT_CONFIG    ZB_TRUE
/* LED indicating that light switch successfully joind Zigbee network. */
#define ZIGBEE_NETWORK_STATE_LED   DK_LED3
/* LED used for device identification. */
#define IDENTIFY_LED               ZIGBEE_NETWORK_STATE_LED
/* LED indicating that light witch found a light bulb to control. */
#define BULB_FOUND_LED             DK_LED4
/* Button ID used to switch on the light bulb. */
#define BUTTON_ON                  DK_BTN1_MSK
/* Button ID used to switch off the light bulb. */
#define BUTTON_OFF                 DK_BTN2_MSK
/* Dim step size - increases/decreses current level (range 0x000 - 0xfe). */
#define DIMM_STEP                  15
/* Button ID used to enable sleepy behavior. */
#define BUTTON_SLEEPY              DK_BTN3_MSK

/* Button to start Factory Reset */
#define FACTORY_RESET_BUTTON       DK_BTN4_MSK

/* Button used to enter the Identify mode. */
#define IDENTIFY_MODE_BUTTON       DK_BTN4_MSK

/* Transition time for a single step operation in 0.1 sec units.
 * 0xFFFF - immediate change.
 */
#define DIMM_TRANSACTION_TIME      2

/* Time after which the button state is checked again to detect button hold,
 * the dimm command is sent again.
 */
#define BUTTON_LONG_POLL_TMO       K_MSEC(500)

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE to compile light switch (End Device) source code.
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

struct bulb_context {
	zb_uint8_t endpoint;
	zb_uint16_t short_addr;
	struct k_timer find_alarm;
};

struct buttons_context {
	uint32_t state;
	atomic_t long_poll;
	struct k_timer alarm;
};

struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
};

static struct bulb_context bulb_ctx;
static struct buttons_context buttons_ctx;
static struct zb_device_ctx dev_ctx;

/* Declare attribute list for Basic cluster (server). */
ZB_ZCL_DECLARE_BASIC_SERVER_ATTRIB_LIST(
	basic_server_attr_list,
	&dev_ctx.basic_attr.zcl_version,
	&dev_ctx.basic_attr.power_source);

/* Declare attribute list for Identify cluster (client). */
ZB_ZCL_DECLARE_IDENTIFY_CLIENT_ATTRIB_LIST(
	identify_client_attr_list);

/* Declare attribute list for Identify cluster (server). */
ZB_ZCL_DECLARE_IDENTIFY_SERVER_ATTRIB_LIST(
	identify_server_attr_list,
	&dev_ctx.identify_attr.identify_time);

/* Declare attribute list for Scenes cluster (client). */
ZB_ZCL_DECLARE_SCENES_CLIENT_ATTRIB_LIST(
	scenes_client_attr_list);

/* Declare attribute list for Groups cluster (client). */
ZB_ZCL_DECLARE_GROUPS_CLIENT_ATTRIB_LIST(
	groups_client_attr_list);

/* Declare attribute list for On/Off cluster (client). */
ZB_ZCL_DECLARE_ON_OFF_CLIENT_ATTRIB_LIST(
	on_off_client_attr_list);

/* Declare attribute list for Level control cluster (client). */
ZB_ZCL_DECLARE_LEVEL_CONTROL_CLIENT_ATTRIB_LIST(
	level_control_client_attr_list);

/* Custom cluster list for Dimmer Switch device containing Temp Measurement client cluster */
zb_zcl_cluster_desc_t dimmer_switch_clusters[] =
{
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(basic_server_attr_list, zb_zcl_attr_t),
		(basic_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_server_attr_list, zb_zcl_attr_t),
		(identify_server_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_client_attr_list, zb_zcl_attr_t),
		(identify_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_SCENES,
		ZB_ZCL_ARRAY_SIZE(scenes_client_attr_list, zb_zcl_attr_t),
		(scenes_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_GROUPS,
		ZB_ZCL_ARRAY_SIZE(groups_client_attr_list, zb_zcl_attr_t),
		(groups_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_ARRAY_SIZE(on_off_client_attr_list, zb_zcl_attr_t),
		(on_off_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
		ZB_ZCL_ARRAY_SIZE(level_control_client_attr_list, zb_zcl_attr_t),
		(level_control_client_attr_list),
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		0,
		NULL,
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	)
};

/* Custom simple descriptor for Dimmer Switch with 2 IN and 6 OUT clusters */
ZB_DECLARE_SIMPLE_DESC(2, 6);
static ZB_AF_SIMPLE_DESC_TYPE(2, 6) simple_desc_dimmer_switch_ep =
{
	LIGHT_SWITCH_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	ZB_DIMMER_SWITCH_DEVICE_ID,
	ZB_DEVICE_VER_DIMMER_SWITCH,
	0,
	2,
	6,
	{
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_SCENES,
		ZB_ZCL_CLUSTER_ID_GROUPS,
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
	}
};

/* Bind custom cluster list to Endpoint */
ZB_AF_DECLARE_ENDPOINT_DESC(
	dimmer_switch_ep,
	LIGHT_SWITCH_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(dimmer_switch_clusters, zb_zcl_cluster_desc_t),
	dimmer_switch_clusters,
	(zb_af_simple_desc_1_1_t *)&simple_desc_dimmer_switch_ep,
	0, NULL,
	0, NULL
);

/* Declare application's device context (list of registered endpoints)
 * for Dimmer Switch device.
 */
ZBOSS_DECLARE_DEVICE_CTX_1_EP(dimmer_switch_ctx, dimmer_switch_ep);

/* Forward declarations. */
static void light_switch_button_handler(struct k_timer *timer);
static void find_light_bulb_alarm(struct k_timer *timer);
static void find_light_bulb(zb_bufid_t bufid);
static void light_switch_send_on_off(zb_bufid_t bufid, zb_uint16_t on_off);


/**@brief Starts identifying the device.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */


static void write_attr_callback(zb_bufid_t bufid)
{
	zb_zcl_command_send_status_t *send_status = ZB_BUF_GET_PARAM(bufid, zb_zcl_command_send_status_t);
	if (send_status->status == RET_OK) {
		LOG_INF("Write attribute request sent successfully!");
	} else {
		LOG_ERR("Write attribute request failed to send: %d", send_status->status);
	}
	zb_buf_free(bufid);
}

static void send_temperature_cb(zb_bufid_t bufid)
{
    int32_t temp_raw;
    int err = bmp180_read_temperature(&temp_raw);

    if (err != 0) {
        LOG_ERR("Failed to read BMP180: %d", err);
        zb_buf_free(bufid);
        return;
    }

    zb_int16_t temp_zigbee = (zb_int16_t)(temp_raw);
    zb_uint8_t *ptr;

    ZB_ZCL_GENERAL_INIT_WRITE_ATTR_REQ(bufid, ptr, ZB_ZCL_ENABLE_DEFAULT_RESPONSE);

    ZB_ZCL_GENERAL_ADD_VALUE_WRITE_ATTR_REQ(
        ptr,
        ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        ZB_ZCL_ATTR_TYPE_S16,
        (zb_uint8_t *)&temp_zigbee);
    
    zb_uint16_t coord_addr = 0x0000;
	
    ZB_ZCL_GENERAL_SEND_WRITE_ATTR_REQ(
        bufid,
        ptr,
        coord_addr,
        ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        10, /* ZIGBEE_COORDINATOR_ENDPOINT */
        LIGHT_SWITCH_ENDPOINT,
        ZB_AF_HA_PROFILE_ID,
        ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        write_attr_callback);
}

/**@brief Callback for button events.
 *
 * @param[in]   button_state  Bitmask containing buttons state.
 * @param[in]   has_changed   Bitmask containing buttons that has
 *                            changed their state.
 */
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	zb_uint16_t cmd_id;
	zb_ret_t zb_err_code;

	/* Inform default signal handler about user input at the device. */
	user_input_indicate();

	check_factory_reset_button(button_state, has_changed);

	if (bulb_ctx.short_addr == 0xFFFF) {
		LOG_DBG("No bulb found yet.");
		return;
	}

	switch (has_changed) {
	case BUTTON_ON:
		LOG_DBG("ON - button changed");
		cmd_id = ZB_ZCL_CMD_ON_OFF_ON_ID;
		break;
	case BUTTON_OFF:
		LOG_DBG("OFF - button changed");
		cmd_id = ZB_ZCL_CMD_ON_OFF_OFF_ID;
		break;

	default:
		LOG_DBG("Unhandled button");
		return;
	}

	switch (button_state) {
	case BUTTON_ON:
	case BUTTON_OFF:
		LOG_DBG("Button pressed");
		buttons_ctx.state = button_state;

		/* Alarm can be scheduled only once. Next alarm only resets
		 * counting.
		 */
		k_timer_start(&buttons_ctx.alarm, BUTTON_LONG_POLL_TMO,
			      K_NO_WAIT);
		break;
	case 0:
		LOG_DBG("Button released");

		k_timer_stop(&buttons_ctx.alarm);

		if (atomic_set(&buttons_ctx.long_poll, ZB_FALSE) == ZB_FALSE) {
			/* Allocate output buffer and send on/off command. */
			zb_err_code = zb_buf_get_out_delayed_ext(
				light_switch_send_on_off, cmd_id, 0);
			ZB_ERROR_CHECK(zb_err_code);

			/* Queue custom random string transmission */
			zb_ret_t rand_err = zb_buf_get_out_delayed(send_temperature_cb);
			if (rand_err != RET_OK) {
				LOG_ERR("Failed to queue random string transmission (err %d)", rand_err);
			}
		}
		break;
	default:
		break;
	}
}

/**@brief Function for initializing LEDs and Buttons. */
static void configure_gpio(void)
{
	int err;

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}

static void alarm_timers_init(void)
{
	k_timer_init(&buttons_ctx.alarm, light_switch_button_handler, NULL);
	k_timer_init(&bulb_ctx.find_alarm, find_light_bulb_alarm, NULL);
}

/**@brief Function for initializing all clusters attributes. */
static void app_clusters_attr_init(void)
{
	/* Basic cluster attributes data. */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

/**@brief Function for sending ON/OFF requests to the light bulb.
 *
 * @param[in]   bufid    Non-zero reference to Zigbee stack buffer that will be
 *                       used to construct on/off request.
 * @param[in]   cmd_id   ZCL command id.
 */
static void light_switch_send_on_off(zb_bufid_t bufid, zb_uint16_t cmd_id)
{
	LOG_INF("Send ON/OFF command: %d", cmd_id);

	ZB_ZCL_ON_OFF_SEND_REQ(bufid,
			       bulb_ctx.short_addr,
			       ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
			       bulb_ctx.endpoint,
			       LIGHT_SWITCH_ENDPOINT,
			       ZB_AF_HA_PROFILE_ID,
			       ZB_ZCL_DISABLE_DEFAULT_RESPONSE,
			       cmd_id,
			       NULL);
}

/**@brief Function for sending step requests to the light bulb.
 *
 * @param[in]   bufid        Non-zero reference to Zigbee stack buffer that
 *                           will be used to construct step request.
 * @param[in]   cmd_id       ZCL command id.
 */
static void light_switch_send_step(zb_bufid_t bufid, zb_uint16_t cmd_id)
{
	LOG_INF("Send step level command: %d", cmd_id);

	ZB_ZCL_LEVEL_CONTROL_SEND_STEP_REQ(bufid,
					   bulb_ctx.short_addr,
					   ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
					   bulb_ctx.endpoint,
					   LIGHT_SWITCH_ENDPOINT,
					   ZB_AF_HA_PROFILE_ID,
					   ZB_ZCL_DISABLE_DEFAULT_RESPONSE,
					   NULL,
					   cmd_id,
					   DIMM_STEP,
					   DIMM_TRANSACTION_TIME);
}

/**@brief Callback function receiving finding procedure results.
 *
 * @param[in]   bufid   Reference to Zigbee stack buffer used to pass
 *                      received data.
 */
static void find_light_bulb_cb(zb_bufid_t bufid)
{
	/* Get the beginning of the response. */
	zb_zdo_match_desc_resp_t *resp =
		(zb_zdo_match_desc_resp_t *) zb_buf_begin(bufid);
	/* Get the pointer to the parameters buffer, which stores APS layer
	 * response.
	 */
	zb_apsde_data_indication_t *ind = ZB_BUF_GET_PARAM(bufid,
							   zb_apsde_data_indication_t);
	zb_uint8_t *match_ep;

	if ((resp->status == ZB_ZDP_STATUS_SUCCESS) &&
	    (resp->match_len > 0) &&
	    (bulb_ctx.short_addr == 0xFFFF)) {

		/* Match EP list follows right after response header. */
		match_ep = (zb_uint8_t *)(resp + 1);

		/* We are searching for exact cluster, so only 1 EP
		 * may be found.
		 */
		bulb_ctx.endpoint = *match_ep;
		bulb_ctx.short_addr = ind->src_addr;

		LOG_INF("Found bulb addr: %d ep: %d",
			bulb_ctx.short_addr,
			bulb_ctx.endpoint);

		k_timer_stop(&bulb_ctx.find_alarm);
		dk_set_led_on(BULB_FOUND_LED);
	} else {
		LOG_INF("Bulb not found, try again");
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}

/**@brief Find bulb allarm handler.
 *
 * @param[in]   timer   Address of timer.
 */
static void find_light_bulb_alarm(struct k_timer *timer)
{
	ZB_ERROR_CHECK(zb_buf_get_out_delayed(find_light_bulb));
}

/**@brief Function for sending ON/OFF and Level Control find request.
 *
 * @param[in]   bufid   Reference to Zigbee stack buffer that will be used to
 *                      construct find request.
 */
static void find_light_bulb(zb_bufid_t bufid)
{
	zb_zdo_match_desc_param_t *req;
	zb_uint8_t tsn = ZB_ZDO_INVALID_TSN;

	/* Initialize pointers inside buffer and reserve space for
	 * zb_zdo_match_desc_param_t request.
	 */
	req = zb_buf_initial_alloc(bufid,
				   sizeof(zb_zdo_match_desc_param_t) + (1) * sizeof(zb_uint16_t));

	req->nwk_addr = MATCH_DESC_REQ_ROLE;
	req->addr_of_interest = MATCH_DESC_REQ_ROLE;
	req->profile_id = ZB_AF_HA_PROFILE_ID;

	/* We are searching for 2 clusters: On/Off and Level Control Server. */
	req->num_in_clusters = 2;
	req->num_out_clusters = 0;
	req->cluster_list[0] = ZB_ZCL_CLUSTER_ID_ON_OFF;
	req->cluster_list[1] = ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL;

	/* Set 0xFFFF to reset short address in order to parse
	 * only one response.
	 */
	bulb_ctx.short_addr = 0xFFFF;
	tsn = zb_zdo_match_desc_req(bufid, find_light_bulb_cb);

	/* Free buffer if failed to send a request. */
	if (tsn == ZB_ZDO_INVALID_TSN) {
		zb_buf_free(bufid);

		LOG_ERR("Failed to send Match Descriptor request");
	}
}

/**@brief Callback for detecting button press duration.
 *
 * @param[in]   timer   Address of timer.
 */
static void light_switch_button_handler(struct k_timer *timer)
{
	zb_ret_t zb_err_code;
	zb_uint16_t cmd_id;

	if (dk_get_buttons() & buttons_ctx.state) {
		atomic_set(&buttons_ctx.long_poll, ZB_TRUE);
		if (buttons_ctx.state == BUTTON_ON) {
			cmd_id = ZB_ZCL_LEVEL_CONTROL_STEP_MODE_UP;
		} else {
			cmd_id = ZB_ZCL_LEVEL_CONTROL_STEP_MODE_DOWN;
		}

		/* Allocate output buffer and send step command. */
		zb_err_code = zb_buf_get_out_delayed_ext(light_switch_send_step,
							 cmd_id,
							 0);
		if (!zb_err_code) {
			LOG_WRN("Buffer is full");
		}

		k_timer_start(&buttons_ctx.alarm, BUTTON_LONG_POLL_TMO,
			      K_NO_WAIT);
	} else {
		atomic_set(&buttons_ctx.long_poll, ZB_FALSE);
	}
}



/**@brief Zigbee stack event handler.
 *
 * @param[in]   bufid   Reference to the Zigbee stack buffer
 *                      used to pass signal.
 */
void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	/* Update network status LED. */
	zigbee_led_status_update(bufid, ZIGBEE_NETWORK_STATE_LED);



	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
	/* fall-through */
	case ZB_BDB_SIGNAL_STEERING:
		/* Call default signal handler. */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		if (status == RET_OK) {
			/* Comment out or delete this block to keep static addressing intact */
			/*
			if (bulb_ctx.short_addr == 0xFFFF) {
				k_timer_start(&bulb_ctx.find_alarm,
					      MATCH_DESC_REQ_START_DELAY,
					      MATCH_DESC_REQ_TIMEOUT);
			}
			*/
			LOG_INF("Static addressing to coordinator initialized.");
			dk_set_led_on(BULB_FOUND_LED);
		}
		break;
	case ZB_ZDO_SIGNAL_LEAVE:
		/* If device leaves the network, reset bulb short_addr. */
		if (status == RET_OK) {
			zb_zdo_signal_leave_params_t *leave_params =
				ZB_ZDO_SIGNAL_GET_PARAMS(sig_hndler, zb_zdo_signal_leave_params_t);

			if (leave_params->leave_type == ZB_NWK_LEAVE_TYPE_RESET) {
				bulb_ctx.short_addr = 0xFFFF;
			}
		}
		/* Call default signal handler. */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;

	default:
		/* Call default signal handler. */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}

static struct k_work rtc_work;

static void zb_trigger_temp_report(zb_bufid_t param)
{
	ARG_UNUSED(param);
	zb_ret_t err = zb_buf_get_out_delayed(send_temperature_cb);
	if (err != RET_OK) {
		LOG_ERR("Failed to allocate ZBOSS buffer for temp report (err %d)", err);
	}
}

static void rtc_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	ZB_SCHEDULE_APP_CALLBACK(zb_trigger_temp_report, 0);
}

static void rtc2_isr(const void *arg)
{
	ARG_UNUSED(arg);
	bool is_compare_event = false;

	int err = my_rtc_get_compare_event(2, 1, &is_compare_event);
	
	if (err == 0 && is_compare_event) {
		my_rtc_clear_compare_event(2, 1);
		
		// clear counter to make it periodic (4-second interval)
		my_rtc_clear(2);
		my_rtc_start_compare(2, 1, 32);

		// Zephyr workqueue
		k_work_submit(&rtc_work);
	}
}

int main(void)
{
	LOG_INF("Starting ZBOSS Light Switch example");


	/* Initialize. */
	configure_gpio();
	alarm_timers_init();
	register_factory_reset_button(FACTORY_RESET_BUTTON);
	
	bmp180_init();
    

	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(3000));

	/* Set default bulb short_addr directly to the Coordinator */
	bulb_ctx.short_addr = 0x0000;
	bulb_ctx.endpoint = 10; /* Must match ZIGBEE_COORDINATOR_ENDPOINT of coordinator */

	/* Disable the match descriptor discovery timer to avoid overwriting addresses */
	// k_timer_start(&bulb_ctx.find_alarm, MATCH_DESC_REQ_START_DELAY, MATCH_DESC_REQ_TIMEOUT);

	/* If "sleepy button" is defined, check its state during Zigbee
	 * initialization and enable sleepy behavior at device if defined button
	 * is pressed.
	 */
	
	#if defined BUTTON_SLEEPY
	if (dk_get_buttons() & BUTTON_SLEEPY) {
		zigbee_configure_sleepy_behavior(true);
	}
	#endif

	/* Power off unused sections of RAM to lower device power consumption. */
	if (IS_ENABLED(CONFIG_RAM_POWER_DOWN_LIBRARY)) {
		power_down_unused_ram();
	}

	/* Register dimmer switch device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&dimmer_switch_ctx);

	app_clusters_attr_init();




	#if defined(CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER)
		set_tx_power();
	#endif /* CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER */

	/* Start Zigbee default thread. */
	zigbee_enable();

	LOG_INF("ZBOSS Light Switch example started");

	if (my_timer_init(MY_TIMER_1, 4) != 0) {
		LOG_ERR("Failed to initialize TIMER1");
	}
	if (my_timer_start(MY_TIMER_1) != 0) {
		LOG_ERR("Failed to start TIMER1");
	}

	k_work_init(&rtc_work, rtc_work_handler);

	if (my_rtc_init(2, 4095) != 0) { // 125 ms / tick
		LOG_ERR("Failed to initialize RTC2");
	}
	if (my_rtc_start_compare(2, 1, 32) != 0) { // 32 ticks = 4 seconds
		LOG_ERR("Failed to set RTC2 compare value");
	}
	if (my_rtc_enable_interrupt(2, MY_RTC_INT_COMPARE1_MASK) != 0) {
		LOG_ERR("Failed to enable RTC2 interrupt mask");
	}

	// Enable the RTC2 interrupt in the NVIC
	IRQ_CONNECT(RTC2_IRQn, 2, rtc2_isr, NULL, 0);
	irq_enable(RTC2_IRQn);

	if (my_rtc_start(2) != 0) {
		LOG_ERR("Failed to start RTC2");
	}
	
	k_msleep(10);

	while (1) {
		k_sleep(K_FOREVER);
	}

}
