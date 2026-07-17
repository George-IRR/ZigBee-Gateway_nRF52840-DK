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
#include <dk_buttons_and_leds.h>
#include <zephyr/console/console.h>
#include <string.h>
#include <zephyr/sys/reboot.h>
#include <ram_pwrdn.h>
#include <zephyr/random/random.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>
#include "zb_mem_config_custom.h"
#include "bmp_180.h"
#include "my_nrf52_timer.h"
#include "my_rtc.h"
#include <zephyr/irq.h>

#if defined(CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER)
#include <osif/mac_platform.h>
#endif

/* Source endpoint used to report temperature. */
#define BMP180_SENSOR_ENDPOINT      1

/* Do not erase NVRAM to save the network parameters after device reboot or
 * power-off. NOTE: If this option is set to ZB_TRUE then do full device erase
 * for all network devices before running other samples.
 */
#define ERASE_PERSISTENT_CONFIG    ZB_TRUE
/* LED indicating that light switch successfully joind Zigbee network. */
#define ZIGBEE_NETWORK_STATE_LED   DK_LED3

/* Button ID used to enable sleepy behavior. */
#define BUTTON_SLEEPY              DK_BTN3_MSK

/* Button to start Factory Reset */
#define FACTORY_RESET_BUTTON       DK_BTN4_MSK

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE to compile light switch (End Device) source code.
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* True once the device has successfully joined the Zigbee network.
 * Temperature reports are suppressed until this is set to prevent
 * spurious ZCL write failures when not associated. */
static bool g_network_joined = false;

struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
};

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

/* Custom cluster list containing Temp Measurement client cluster */
zb_zcl_cluster_desc_t bmp180_sensor_clusters[] =
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
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		0,
		NULL,
		ZB_ZCL_CLUSTER_CLIENT_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID
	)
};

/* Custom simple descriptor with 2 IN and 2 OUT clusters */
ZB_DECLARE_SIMPLE_DESC(2, 2);
static ZB_AF_SIMPLE_DESC_TYPE(2, 2) simple_desc_bmp180_ep =
{
	BMP180_SENSOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0x0104, /* ZB_DIMMER_SWITCH_DEVICE_ID */
	0,      /* ZB_DEVICE_VER_DIMMER_SWITCH */
	0,
	2,
	2,
	{
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
	}
};

/* Bind custom cluster list to Endpoint */
ZB_AF_DECLARE_ENDPOINT_DESC(
	bmp180_sensor_ep,
	BMP180_SENSOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(bmp180_sensor_clusters, zb_zcl_cluster_desc_t),
	bmp180_sensor_clusters,
	(zb_af_simple_desc_1_1_t *)&simple_desc_bmp180_ep,
	0, NULL,
	0, NULL
);

/* Declare application's device context (list of registered endpoints) */
ZBOSS_DECLARE_DEVICE_CTX_1_EP(bmp180_sensor_ctx, bmp180_sensor_ep);


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
        BMP180_SENSOR_ENDPOINT,
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
	/* Inform default signal handler about user input at the device. */
	user_input_indicate();

	check_factory_reset_button(button_state, has_changed);
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

/**@brief Function for initializing all clusters attributes. */
static void app_clusters_attr_init(void)
{
	/* Basic cluster attributes data. */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

/**@brief Zigbee stack event handler.
 *
 * @param[in]   bufid   Reference to the Zigbee stack buffer
 *                      used to pass signal.
 */
#if !defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
/* CRC-16/X-25 (reflected / Kermit variant) — same algorithm the ZBOSS
 * stack uses to validate Install Codes. */
static uint16_t ic_crc16_x25(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
		}
	}
	return crc ^ 0xFFFF;
}

/* Generate a random Install Code (16 bytes + 2-byte CRC-16/X-25 LE)
 * and print the ready-to-paste UART commands for both devices. */
static void print_production_pairing_commands(void)
{
	zb_ieee_addr_t self_ieee;
	zb_get_long_address(self_ieee);

	uint8_t key[16];
	sys_rand_get(key, sizeof(key));

	uint16_t crc = ic_crc16_x25(key, sizeof(key));
	uint8_t ic[18];
	memcpy(ic, key, 16);
	ic[16] = (uint8_t)(crc & 0xFF);        /* CRC low byte */
	ic[17] = (uint8_t)((crc >> 8) & 0xFF); /* CRC high byte */

	/* Format IEEE address (big-endian display) */
	char ieee_str[17];
	snprintk(ieee_str, sizeof(ieee_str), "%02x%02x%02x%02x%02x%02x%02x%02x",
	         self_ieee[7], self_ieee[6], self_ieee[5], self_ieee[4],
	         self_ieee[3], self_ieee[2], self_ieee[1], self_ieee[0]);

	/* Format IC as 36-char hex string */
	char ic_str[37];
	for (int i = 0; i < 18; i++) {
		snprintk(&ic_str[i * 2], 3, "%02x", ic[i]);
	}

	printk("\n");
	printk("╔══════════════════════════════════════════════════╗\n");
	printk("║       PRODUCTION PAIRING — copy-paste commands  ║\n");
	printk("╠══════════════════════════════════════════════════╣\n");
	printk("║ Step 1 — Coordinator (/dev/ttyACM0):             ║\n");
	printk("║   ic_add %s %s  ║\n", ieee_str, ic_str);
	printk("║   expected reply: ic_add_success + steering_started\n");
	printk("╠══════════════════════════════════════════════════╣\n");
	printk("║ Step 2 — End Device (this terminal):             ║\n");
	printk("║   ic_set %s %s  ║\n", ieee_str, ic_str);
	printk("║   expected reply: ic_set_success                 ║\n");
	printk("╠══════════════════════════════════════════════════╣\n");
	printk("║ Step 3 — End Device (this terminal):             ║\n");
	printk("║   join                                           ║\n");
	printk("║   expected reply: join_started                   ║\n");
	printk("╚══════════════════════════════════════════════════╝\n");
	printk("\n");
}
#endif

#if defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
static void setup_static_install_code(void)
{
	LOG_INF("Setting static development install code in active stack...");
	zb_uint8_t dev_install_code[18] = {
		0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
		0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
		0x52, 0x0d
	};

	zb_ret_t status = zb_secur_ic_set(ZB_IC_TYPE_128, dev_install_code);
	if (status != RET_OK) {
		LOG_ERR("Failed to set static install code: %d", status);
	} else {
		LOG_INF("Static development install code set successfully.");
	}
}
#endif


void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hndler = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hndler);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	/* Update network status LED. */
	zigbee_led_status_update(bufid, ZIGBEE_NETWORK_STATE_LED);

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		#if defined(CONFIG_ZIGBEE_DEVELOPMENT_SECURITY)
		setup_static_install_code();
		/* Let default handler trigger automatic network steering */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		#else
		/* Production mode: do NOT auto-join. Print ready-to-paste commands. */
		print_production_pairing_commands();
		zb_buf_free(bufid);
		return;
		#endif
		break;

	case ZB_BDB_SIGNAL_STEERING:
		/* Call default signal handler. */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		if (status == RET_OK) {
			g_network_joined = true;
			LOG_INF("Network joined successfully.");
			dk_set_led_on(DK_LED4);
		}
		break;

	case ZB_ZDO_SIGNAL_LEAVE:
		g_network_joined = false;
		dk_set_led_off(DK_LED4);
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
	if (!g_network_joined) {
		/* Not on network yet — skip silently */
		return;
	}
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

static struct {
	zb_ieee_addr_t addr;
	zb_uint8_t ic[18];
} g_ic_set_params;

static void ic_set_callback(zb_bufid_t bufid)
{
	/* Note: do NOT call zb_set_long_address here.
	 * The device uses its own factory-burned IEEE address from FICR.
	 * We only set the Install Code for the TCLK derivation. */
	zb_ret_t status = zb_secur_ic_set(ZB_IC_TYPE_128, g_ic_set_params.ic);
	if (status == RET_OK) {
		printk("ic_set_success\n");
	} else {
		printk("ic_set_failed: %d\n", status);
	}
}

static void join_callback(zb_bufid_t bufid)
{
	zb_bool_t comm_status = bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
	if (comm_status) {
		printk("join_started\n");
	} else {
		printk("join_failed_busy\n");
	}
}

static void reboot_handler(zb_bufid_t bufid)
{
	ARG_UNUSED(bufid);
	sys_reboot(SYS_REBOOT_COLD);
}

static void parse_uart_command(char *line)
{
	if (strncmp(line, "ic_set ", 7) == 0) {
		const char *ieee_hex = &line[7];
		const char *ic_hex = &line[24]; // 7 + 16 + 1 (space)

		if (line[23] != ' ' || strlen(ieee_hex) < 53) {
			printk("ic_set_failed: invalid format\n");
			return;
		}

		for (int i = 0; i < 8; i++) {
			g_ic_set_params.addr[7 - i] = hex_pair_to_byte(&ieee_hex[i * 2]);
		}

		for (int i = 0; i < 18; i++) {
			g_ic_set_params.ic[i] = hex_pair_to_byte(&ic_hex[i * 2]);
		}

		ZB_SCHEDULE_APP_CALLBACK(ic_set_callback, 0);
	} else if (strcmp(line, "join") == 0) {
		ZB_SCHEDULE_APP_CALLBACK(join_callback, 0);
	} else if (strcmp(line, "factory_reset") == 0) {
		printk("factory_reset_started\n");
		ZB_SCHEDULE_APP_CALLBACK(factory_reset_handler, 0);
	} else if (strcmp(line, "reboot") == 0) {
		printk("reboot_started\n");
		ZB_SCHEDULE_APP_CALLBACK(reboot_handler, 0);
	} else if (strcmp(line, "ieee") == 0) {
		zb_ieee_addr_t self_ieee;
		zb_get_long_address(self_ieee);
		printk("Device IEEE Address: %02x%02x%02x%02x%02x%02x%02x%02x\n",
		       self_ieee[7], self_ieee[6], self_ieee[5], self_ieee[4],
		       self_ieee[3], self_ieee[2], self_ieee[1], self_ieee[0]);
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
		k_msleep(20);
	}
}

K_THREAD_STACK_DEFINE(uart_rx_stack, UART_THREAD_STACK_SIZE);
static struct k_thread uart_rx_thread_data;
static k_tid_t uart_rx_tid;

int main(void)
{
	LOG_INF("Starting ZBOSS BMP180 Temperature Sensor");

	/* Start UART RX thread with 2s delay to ensure console/drivers are ready. */
	uart_rx_tid = k_thread_create(&uart_rx_thread_data, uart_rx_stack,
				      K_THREAD_STACK_SIZEOF(uart_rx_stack),
				      uart_rx_thread, NULL, NULL, NULL,
				      UART_THREAD_PRIORITY, 0, K_MSEC(2000));


	/* Initialize. */
	configure_gpio();
	register_factory_reset_button(FACTORY_RESET_BUTTON);

	bmp180_init();

	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);
	zb_set_ed_timeout(ED_AGING_TIMEOUT_64MIN);
	zb_set_keepalive_timeout(ZB_MILLISECONDS_TO_BEACON_INTERVAL(3000));


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

	/* Register sensor device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&bmp180_sensor_ctx);

	app_clusters_attr_init();




	#if defined(CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER)
		set_tx_power();
	#endif /* CONFIG_LIGHT_SWITCH_CONFIGURE_TX_POWER */



	/* Start Zigbee default thread. */
	zigbee_enable();

	zb_ieee_addr_t self_ieee;
	zb_get_long_address(self_ieee);
	LOG_INF("Device IEEE Address: %02x%02x%02x%02x%02x%02x%02x%02x",
	        self_ieee[7], self_ieee[6], self_ieee[5], self_ieee[4],
	        self_ieee[3], self_ieee[2], self_ieee[1], self_ieee[0]);

	LOG_INF("ZBOSS BMP180 Temperature Sensor started");

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
