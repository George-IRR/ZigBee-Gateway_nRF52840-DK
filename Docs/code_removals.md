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
