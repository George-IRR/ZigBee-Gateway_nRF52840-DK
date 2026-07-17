/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief Simple Zigbee network coordinator implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/console/console.h>
#include <string.h>
#include <zephyr/sys/reboot.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_mem_config_max.h>
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>
#include <zb_nrf_platform.h>

#define RUN_STATUS_LED                         DK_LED1
#define RUN_LED_BLINK_INTERVAL                 1000

/* Device endpoint, used to receive ZCL commands. */
#define ZIGBEE_COORDINATOR_ENDPOINT            10

/* Type of power sources available for the device.
 * For possible values see section 3.2.2.2.8 of ZCL specification.
 */
#define COORDINATOR_INIT_BASIC_POWER_SOURCE    ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE

/* LED indicating that device successfully joined Zigbee network. */
#define ZIGBEE_NETWORK_STATE_LED               DK_LED3

/* LED used for device identification. */
#define IDENTIFY_LED                           DK_LED4

/* Button which reopens the Zigbee Network. */
#define KEY_ZIGBEE_NETWORK_REOPEN              DK_BTN1_MSK

/* Button used to enter the Identify mode. */
#define IDENTIFY_MODE_BUTTON                   DK_BTN4_MSK

/* If set to ZB_TRUE then device will not open the network after forming or reboot. */
#define ZIGBEE_MANUAL_STEERING                 ZB_FALSE

#define ZIGBEE_PERMIT_LEGACY_DEVICES           ZB_FALSE

#ifndef ZB_COORDINATOR_ROLE
#error Define ZB_COORDINATOR_ROLE to compile coordinator source code.
#endif

/* Button to start Factory Reset */
#define FACTORY_RESET_BUTTON IDENTIFY_MODE_BUTTON

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Main application customizable context.
 * Stores all settings and static values.
 */
struct zb_device_ctx {
	zb_zcl_basic_attrs_ext_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
};

/* Zigbee device application context storage. */
static struct zb_device_ctx dev_ctx;

/* Declare attributes for the newly added On/Off Server Cluster */
static zb_uint8_t on_off_attr_value = ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE;
ZB_ZCL_DECLARE_ON_OFF_ATTRIB_LIST(
	on_off_attr_list, 
	&on_off_attr_value);

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(
	identify_attr_list,
	&dev_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
	basic_attr_list,
	&dev_ctx.basic_attr.zcl_version,
	&dev_ctx.basic_attr.app_version,
	&dev_ctx.basic_attr.stack_version,
	&dev_ctx.basic_attr.hw_version,
	dev_ctx.basic_attr.mf_name,
	dev_ctx.basic_attr.model_id,
	dev_ctx.basic_attr.date_code,
	&dev_ctx.basic_attr.power_source,
	dev_ctx.basic_attr.location_id,
	&dev_ctx.basic_attr.ph_env,
	dev_ctx.basic_attr.sw_ver);

static zb_int16_t temp_meas_value = 0;
static zb_int16_t temp_meas_min = -5000;  // -50.00 C
static zb_int16_t temp_meas_max = 10000; // 100.00 C
static zb_uint16_t temp_meas_tolerance = 0;

ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(temp_measurement_attr_list, ZB_ZCL_TEMP_MEASUREMENT)
  ZB_ZCL_SET_ATTR_DESC_M(ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temp_meas_value, ZB_ZCL_ATTR_TYPE_S16, ZB_ZCL_ATTR_ACCESS_READ_WRITE | ZB_ZCL_ATTR_ACCESS_REPORTING)
  ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID, &temp_meas_min)
  ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID, &temp_meas_max)
  ZB_ZCL_SET_ATTR_DESC(ZB_ZCL_ATTR_TEMP_MEASUREMENT_TOLERANCE_ID, &temp_meas_tolerance)
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

/* Construct a custom cluster array explicitly declaring Basic, Identify, and On/Off Server support */
static zb_zcl_cluster_desc_t nwk_coordinator_clusters[] = {
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),
		basic_attr_list,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		0
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),
		identify_attr_list,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		0
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_ARRAY_SIZE(on_off_attr_list, zb_zcl_attr_t),
		on_off_attr_list,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		0
	),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_ARRAY_SIZE(temp_measurement_attr_list, zb_zcl_attr_t),
		temp_measurement_attr_list,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		0
	)
};

/* Declare simple descriptor structure space for 4 input clusters and 0 output clusters */
ZB_DECLARE_SIMPLE_DESC(4, 0);

static ZB_AF_SIMPLE_DESC_TYPE(4, 0) simple_desc_nwk_coordinator_ep = {
	ZIGBEE_COORDINATOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0, /* Application Device ID */
	0, /* Application Device Version */
	0, /* Reserved flags */
	4, /* 4 input clusters*/
	0, /* Output cluster count */
	{
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_ON_OFF,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
	}
};

/* Bind the custom cluster array to Endpoint 10 using full endpoint descriptor macro rules */
ZB_AF_DECLARE_ENDPOINT_DESC(
	nwk_coordinator_ep,
	ZIGBEE_COORDINATOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0, NULL,
	ZB_ZCL_ARRAY_SIZE(nwk_coordinator_clusters, zb_zcl_cluster_desc_t),
	nwk_coordinator_clusters,
	(zb_af_simple_desc_1_1_t*)&simple_desc_nwk_coordinator_ep, 
	0, NULL,
	0, NULL 
);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(
	nwk_coordinator,
	nwk_coordinator_ep);

/**@brief Function for initializing all clusters attributes. */
static void app_clusters_attr_init(void)
{
	/* Basic cluster attributes data. */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.app_version = 0;
	dev_ctx.basic_attr.stack_version = 0;
	dev_ctx.basic_attr.hw_version = 0;
	dev_ctx.basic_attr.power_source = COORDINATOR_INIT_BASIC_POWER_SOURCE;

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.mf_name,
		"Nordic",
		ZB_ZCL_STRING_CONST_SIZE("Nordic"));

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.model_id,
		"Coordinator",
		ZB_ZCL_STRING_CONST_SIZE("Coordinator"));

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.date_code,
		"20200329",
		ZB_ZCL_STRING_CONST_SIZE("20200329"));

	/* Initialize location_id as empty Pascal string */
	dev_ctx.basic_attr.location_id[0] = 0;

	dev_ctx.basic_attr.ph_env = 0;

	ZB_ZCL_SET_STRING_VAL(
		dev_ctx.basic_attr.sw_ver,
		"",
		0);

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

/**@brief Function to toggle the identify LED.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void toggle_identify_led(zb_bufid_t bufid)
{
	static int blink_status;

	dk_set_led(IDENTIFY_LED, (++blink_status) % 2);
	ZB_SCHEDULE_APP_ALARM(toggle_identify_led, bufid, ZB_MILLISECONDS_TO_BEACON_INTERVAL(100));
}

/**@brief Function to handle identify notification events on the first endpoint.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void identify_cb(zb_bufid_t bufid)
{
	zb_ret_t zb_err_code;

	if (bufid) {
		/* Schedule a self-scheduling function that will toggle the LED. */
		ZB_SCHEDULE_APP_CALLBACK(toggle_identify_led, bufid);
	} else {
		/* Cancel the toggling function alarm and turn off LED. */
		zb_err_code = ZB_SCHEDULE_APP_ALARM_CANCEL(toggle_identify_led, ZB_ALARM_ANY_PARAM);
		ZVUNUSED(zb_err_code);

		dk_set_led(IDENTIFY_LED, 0);
	}
}

/**@brief Starts identifying the device.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void start_identifying(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);

	if (ZB_JOINED()) {
		/* Check if endpoint is in identifying mode,
		 * if not, put desired endpoint in identifying mode.
		 */
		if (dev_ctx.identify_attr.identify_time ==
		    ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE) {

			zb_ret_t zb_err_code = zb_bdb_finding_binding_target(
				ZIGBEE_COORDINATOR_ENDPOINT);

			if (zb_err_code == RET_OK) {
				LOG_INF("Enter identify mode");
			} else if (zb_err_code == RET_INVALID_STATE) {
				LOG_WRN("RET_INVALID_STATE - Cannot enter identify mode");
			} else {
				ZB_ERROR_CHECK(zb_err_code);
			}
		} else {
			LOG_INF("Cancel identify mode");
			zb_bdb_finding_binding_target_cancel();
		}
	} else {
		LOG_WRN("Device not in a network - cannot enter identify mode");
	}
}

/**@brief Callback used in order to visualise network steering period.
 *
 * @param[in]   param   Not used. Required by callback type definition.
 */
static void steering_finished(zb_uint8_t param)
{
	ARG_UNUSED(param);

	LOG_INF("Network steering finished");
	dk_set_led_off(ZIGBEE_NETWORK_STATE_LED);
}

/**@brief Callback for button events.
 *
 * @param[in]   button_state  Bitmask containing buttons state.
 * @param[in]   has_changed   Bitmask containing buttons that has changed their state.
 */
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	/* Calculate bitmask of buttons that are pressed and have changed their state. */
	uint32_t buttons = button_state & has_changed;
	zb_bool_t comm_status;

	if (buttons & KEY_ZIGBEE_NETWORK_REOPEN) {
		(void)(ZB_SCHEDULE_APP_ALARM_CANCEL(steering_finished, ZB_ALARM_ANY_PARAM));

		comm_status = bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
		if (comm_status) {
			LOG_INF("Top level commissioning restated");
		} else {
			LOG_INF("Top level commissioning hasn't finished yet!");
		}
	}

	if (IDENTIFY_MODE_BUTTON & has_changed) {
		if (IDENTIFY_MODE_BUTTON & button_state) {
			/* Button changed its state to pressed */
		} else {
			/* Button changed its state to released */
			if (was_factory_reset_done()) {
				/* The long press was for Factory Reset */
				LOG_DBG("After Factory Reset - ignore button release");
			} else   {
				/* Button released before Factory Reset */

				/* Start identification mode */
				ZB_SCHEDULE_APP_CALLBACK(start_identifying, 0);
			}
		}
	}

	check_factory_reset_button(button_state, has_changed);
}

/**@brief Function for initializing LEDs and Buttons. */
static void configure_gpio(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}


static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	if (device_cb_param->device_cb_id == ZB_ZCL_SET_ATTR_VALUE_CB_ID) {
		zb_zcl_set_attr_value_param_t *cb_param = &(device_cb_param->cb_param.set_attr_value_param);
		
		// ON OFF Switch LOGIC
		if (cb_param->cluster_id == ZB_ZCL_CLUSTER_ID_ON_OFF &&
		    cb_param->attr_id == ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
			
			zb_uint8_t value = cb_param->values.data8;
			LOG_INF("Data packet received via attribute update! Value: %d", value);
			if (value == 1) {
				LOG_INF("Command parsed: Turn ON");
			} else {
				LOG_INF("Command parsed: Turn OFF");
			}
		} 
		
		else if (cb_param->cluster_id == ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT &&
		         cb_param->attr_id == ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID) {

			zb_int16_t temperature_raw = (zb_int16_t)cb_param->values.data16;

			LOG_INF("zcl_device_cb - Temperature: %d.%d C (Raw value: %d)", 
			        temperature_raw / 10, 
			        (temperature_raw % 10 < 0 ? -(temperature_raw % 10) : (temperature_raw % 10)), 
			        temperature_raw);
		}
	}
	device_cb_param->status = RET_OK;
}

#if defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
static void register_static_development_keys(void)
{
	LOG_INF("Registering static development Install Code in active stack...");
	zb_set_installcode_policy(ZB_TRUE);
	zb_ieee_addr_t dev_ieee_addr = {0xec, 0xa7, 0x12, 0x33, 0x9e, 0x36, 0xce, 0xf4};
	zb_uint8_t dev_install_code[18] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
		0x42, 0x78
	};
	zb_secur_ic_add(dev_ieee_addr, ZB_IC_TYPE_128, dev_install_code, NULL);
	LOG_INF("Static development Install Code registered successfully.");
}
#endif

/**@brief Zigbee stack event handler.
 *
 * @param[in]   bufid   Reference to the Zigbee stack buffer used to pass signal.
 */
void zboss_signal_handler(zb_bufid_t bufid)
{
	/* Read signal description out of memory buffer. */
	zb_zdo_app_signal_hdr_t *sg_p = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sg_p);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);
	zb_ret_t zb_err_code;
	zb_bool_t comm_status;
	zb_time_t timeout_bi;

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
		if (status == RET_OK) {
			LOG_INF("Device started for the first time. Instantiating network formation...");
			comm_status = bdb_start_top_level_commissioning(ZB_BDB_NETWORK_FORMATION);
			ZB_COMM_STATUS_CHECK(comm_status);
		} else {
			LOG_ERR("Failed to initialize Zigbee stack on first start (status: %d)", status);
		}
		break;

	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		if (status == RET_OK) {
			#if defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
			register_static_development_keys();
			#endif
			if (ZIGBEE_MANUAL_STEERING == ZB_FALSE) {
				LOG_INF("Start network steering");
				comm_status = bdb_start_top_level_commissioning(
					ZB_BDB_NETWORK_STEERING);
				ZB_COMM_STATUS_CHECK(comm_status);
			} else {
				LOG_INF("Coordinator restarted successfully");
			}
		} else {
			LOG_WRN("NVRAM empty or invalid (status: %d). Forcing clean network formation...", status);
			comm_status = bdb_start_top_level_commissioning(ZB_BDB_NETWORK_FORMATION);
			ZB_COMM_STATUS_CHECK(comm_status);
		}
		break;

	case ZB_BDB_SIGNAL_FORMATION:
		if (status == RET_OK) {
			LOG_INF("Network formed successfully");
			#if defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
			register_static_development_keys();
			#endif
			if (ZIGBEE_MANUAL_STEERING == ZB_FALSE) {
				LOG_INF("Starting top level network steering...");
				comm_status = bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
				ZB_COMM_STATUS_CHECK(comm_status);
			}
		} else {
			LOG_ERR("Zigbee network formation failed (status: %d)", status);
		}
		break;

	case ZB_BDB_SIGNAL_STEERING:
		if (status == RET_OK) {
			if (ZIGBEE_PERMIT_LEGACY_DEVICES == ZB_TRUE) {
				LOG_INF("Allow pre-Zigbee 3.0 devices to join the network");
				zb_bdb_set_legacy_device_support(1);
			}

			/* Schedule an alarm to notify about the end of steering period. */
			LOG_INF("Network steering started");
			zb_err_code = ZB_SCHEDULE_APP_ALARM(
				steering_finished, 0,
				ZB_TIME_ONE_SECOND *
				ZB_ZGP_DEFAULT_COMMISSIONING_WINDOW);
			ZB_ERROR_CHECK(zb_err_code);
		}
		break;

	case ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
		zb_zdo_signal_device_annce_params_t *dev_annce_params =
			ZB_ZDO_SIGNAL_GET_PARAMS(
				sg_p, zb_zdo_signal_device_annce_params_t);

		LOG_INF("New device commissioned or rejoined (short: 0x%04hx)",
			dev_annce_params->device_short_addr);

		zb_err_code = ZB_SCHEDULE_APP_ALARM_CANCEL(steering_finished, ZB_ALARM_ANY_PARAM);
		if (zb_err_code == RET_OK) {
			LOG_INF("Joining period extended.");
			zb_err_code = ZB_SCHEDULE_APP_ALARM(
				steering_finished, 0,
				ZB_TIME_ONE_SECOND *
				ZB_ZGP_DEFAULT_COMMISSIONING_WINDOW);
			ZB_ERROR_CHECK(zb_err_code);
		}
	} break;

	default:
		/* Call default signal handler for standard stack processing */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	}

	/* Update network status LED. */
	if (ZB_JOINED() &&
	    (ZB_SCHEDULE_GET_ALARM_TIME(steering_finished, ZB_ALARM_ANY_PARAM,
					&timeout_bi) == RET_OK)) {
		dk_set_led_on(ZIGBEE_NETWORK_STATE_LED);
	} else {
		dk_set_led_off(ZIGBEE_NETWORK_STATE_LED);
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}

static void modify_attr_value_callback(zb_uint8_t ep, zb_uint16_t cluster_id, zb_uint16_t attr_id, zb_uint8_t *value)
{
	if (cluster_id == ZB_ZCL_CLUSTER_ID_BASIC &&
	    attr_id == ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID) {
		
		zb_uint8_t len = value[0];
		if (len > 32) {
			len = 32;
		}
		
		static char print_buf[33];
		memcpy(print_buf, &value[1], len);
		print_buf[len] = '\0';
		
		LOG_INF("Received custom random string payload: %s (length: %d)", print_buf, len);
	} else if (cluster_id == ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT &&
	           attr_id == ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID) {
		
		zb_int16_t temperature_raw = *(zb_int16_t *)value;
		LOG_INF("modify_attr_value_callback - Temperature: %d.%d C (Raw value: %d)", 
		        temperature_raw / 10, 
		        (temperature_raw % 10 < 0 ? -(temperature_raw % 10) : (temperature_raw % 10)), 
		        temperature_raw);
	}
}

static inline zb_uint8_t hex_char_to_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return 0;
}

static inline zb_uint8_t hex_pair_to_byte(const char *hex)
{
	return (hex_char_to_val(hex[0]) << 4) | hex_char_to_val(hex[1]);
}

static struct k_timer reboot_timer;

static void reboot_timer_handler(struct k_timer *dummy)
{
	sys_reboot(SYS_REBOOT_COLD);
}

static void factory_reset_handler(zb_bufid_t bufid)
{
	ARG_UNUSED(bufid);
	zb_bdb_reset_via_local_action(0);
	k_timer_init(&reboot_timer, reboot_timer_handler, NULL);
	k_timer_start(&reboot_timer, K_MSEC(1000), K_FOREVER);
}

static void parse_uart_command(char *line)
{
	/* Expect: "ic_add 0807060504030201 00112233445566778899aabbccddeeff4278" */
	if (strncmp(line, "ic_add ", 7) == 0) {
		const char *ieee_hex = &line[7];
		const char *ic_hex = &line[24]; // 7 + 16 + 1 (space)
 
		// Simple length checks
		if (line[23] != ' ' || strlen(ieee_hex) < 53) {
			printk("ic_add_failed: invalid format\n");
			return;
		}
 
		zb_ieee_addr_t addr;
		for (int i = 0; i < 8; i++) {
			addr[7 - i] = hex_pair_to_byte(&ieee_hex[i * 2]);
		}
 
		zb_uint8_t ic[18];
		for (int i = 0; i < 18; i++) {
			ic[i] = hex_pair_to_byte(&ic_hex[i * 2]);
		}
 
		zb_secur_ic_add(addr, ZB_IC_TYPE_128, ic, NULL);
		printk("ic_add_success: ");
		for (int i = 0; i < 8; i++) {
			printk("%02x", addr[7 - i]);
		}
		printk("\n");
	} else if (strcmp(line, "factory_reset") == 0) {
		printk("factory_reset_started\n");
		ZB_SCHEDULE_APP_CALLBACK(factory_reset_handler, 0);
	}
}

#define UART_THREAD_STACK_SIZE 1024
#define UART_THREAD_PRIORITY 10

static void uart_rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	console_getline_init();

	while (1) {
		char *line = console_getline();
		if (line) {
			parse_uart_command(line);
		}
	}
}

K_THREAD_DEFINE(uart_rx_tid, UART_THREAD_STACK_SIZE,
                uart_rx_thread, NULL, NULL, NULL,
                UART_THREAD_PRIORITY, 0, 0);

int main(void)
{
	int blink_status = 0;

	LOG_INF("Starting ZBOSS Coordinator example");

	/* Initialize */
	configure_gpio();
	register_factory_reset_button(FACTORY_RESET_BUTTON);

	/* Register device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&nwk_coordinator);

	app_clusters_attr_init();

	/* Register handlers to identify notifications. */
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(ZIGBEE_COORDINATOR_ENDPOINT, identify_cb);

	/* Add this line to handle incoming validated ZCL commands */
	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);

	/* Set modify attribute callback to capture Write Attribute commands */
	ZB_ZCL_SET_MODIFY_ATTR_VALUE_CB(modify_attr_value_callback);

	/* Enforce Install Code Policy */
	zb_set_installcode_policy(ZB_TRUE);

	/* Start Zigbee default thread */
	zigbee_enable();

	LOG_INF("ZBOSS Coordinator example started");

	while (1) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}