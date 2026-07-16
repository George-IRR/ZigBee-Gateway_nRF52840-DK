# Code Cleanup and Removals Log

This document tracks and explains all major code components and boilerplate cleaned up or removed from the original Nordic SDK samples during the refactoring of `bmp180_device`.

---

## 1. ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER (Identify Cluster Notifications)

### 1.1 What it is
The `ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER` macro registers an application callback function (usually `identify_cb`) that triggers when a Zigbee endpoint enters or exits "Identify Mode". This mode is used in Zigbee networks to physically locate a node (e.g. flashing a status LED) during commissioning.

### 1.2 Why it was removed
* **Purpose Mismatch:** The BMP180 end-device is a simple, battery-powered temperature sensor. It does not need to enter identify mode to bind to lights or switches since it uses a static address straight to the Network Coordinator.
* **Power Savings:** Running the identification routine involves scheduled LED toggling (`toggle_identify_led`) which consumes valuable battery power on an end device designed for deep sleep.
* **Code Size reduction:** Removing the handler allowed us to purge dead functions (`start_identifying`, `toggle_identify_led`, `identify_cb`) and clean up the button input handler state machine (`case IDENTIFY_MODE_BUTTON:`), reducing main code footprint.

### 1.3 Removed Elements
* Registration call `ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(LIGHT_SWITCH_ENDPOINT, identify_cb);` inside `main()`.
* Toggle function `toggle_identify_led()` and the identification status check `identify_cb()`.
* Button release identify triggers from `button_handler()`.
* Action trigger function `start_identifying()`.

---

## 2. alarm_timers_init & Bulb Find Logic

### 2.1 What it is
`alarm_timers_init()` initialized two software timers (`buttons_ctx.alarm` and `bulb_ctx.find_alarm`). These timers were used to periodically schedule active Zigbee Match Descriptor Requests to find nearby Zigbee light bulbs and handle long-press dimming actions.

### 2.2 Why it was removed
* **Static Addressing:** The end-device does not discover endpoints dynamically. It addresses the Network Coordinator directly at static address `0x0000` (Endpoint `10`) for temperature reporting.
* **No Dimmer Behavior:** The application has been refactored from a dimmer switch into a sensor reporter. Active light-switch button handling (e.g., UP/DOWN dimming commands, ON/OFF toggle commands, and button timers) is dead logic.
* **Drastic Code Reduction:** Removing this logic allowed us to drop the entire Match Descriptor finding state machine and serial command builders (`find_light_bulb`, `find_light_bulb_cb`, `light_switch_send_on_off`, `light_switch_send_step`, `light_switch_button_handler`), cleaning up ~200 lines of boilerplate.

### 2.3 Removed Elements
* The initialization function `alarm_timers_init()` and variables `buttons_ctx`, `bulb_ctx`.
* Finding functions `find_light_bulb()`, `find_light_bulb_cb()`, `find_light_bulb_alarm()`.
* Bulb transmitter helpers `light_switch_send_on_off()` and `light_switch_send_step()`.
* Button hold polling logic inside `button_handler()` (simplified to forward factory resets only).
* Unused timers declaration from `main()`.

---

## 3. Unused Zigbee Clusters (Scenes, Groups, On/Off, Level Control)

### 3.1 What it is
The original dimmer switch sample declared and registered client clusters for Zigbee Home Automation commands: Scenes client, Groups client, On/Off client, and Level Control client. It also declared and allocated lists of attributes for these clusters.

### 3.2 Why it was removed
* **Single Responsibility:** The End Device only has a temperature sensor (BMP180) and sends data using the `TEMP_MEASUREMENT` client cluster. It does not control light nodes, organize groups, switch states, or manage lighting levels.
* **Network & Memory Footprint:** Removing these clusters reduces the Zigbee endpoint configuration complexity and RAM footprint. The Simple Descriptor is downsized from 2 IN and 6 OUT clusters to 2 IN (Basic, Identify) and 2 OUT (Identify client, Temp Measurement client).

### 3.3 Removed Elements
* Attribute lists `scenes_client_attr_list`, `groups_client_attr_list`, `on_off_client_attr_list`, and `level_control_client_attr_list`.
* Cluster list entries inside `dimmer_switch_clusters` for scenes, groups, on/off, and level control.
* Descriptor registration fields inside `simple_desc_dimmer_switch_ep` (changing it from `ZB_DECLARE_SIMPLE_DESC(2, 6)` to `ZB_DECLARE_SIMPLE_DESC(2, 2)`).

---

## 4. Unused Macros and Constants

### 4.1 What it is
A collection of unused preprocessor `#define` macros and constants inherited from the original dimmer switch application sample.

### 4.2 Why it was removed
* **Code Health & Readability:** Cleaning up dead preprocessor macros makes it clear which definitions (like LED mapping, endpoint identifiers, and configs) are actively used by the sensor logic.

### 4.3 Removed Elements
* `MATCH_DESC_REQ_START_DELAY`, `MATCH_DESC_REQ_TIMEOUT`, and `MATCH_DESC_REQ_ROLE` (finding timers configurations).
* `BULB_FOUND_LED` (replaced with direct call to `DK_LED4` to indicate successful static steering).
* `BUTTON_ON`, `BUTTON_OFF`, and `DIMM_STEP` (dimmer switch buttons and step sizes).
* `IDENTIFY_MODE_BUTTON` (replaced with unified `FACTORY_RESET_BUTTON`).
* `DIMM_TRANSACTION_TIME` and `BUTTON_LONG_POLL_TMO` (unused dimming transition timers).
