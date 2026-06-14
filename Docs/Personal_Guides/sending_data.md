# A Beginner's Guide: Sending Data from a Zigbee End Device to a Coordinator (nRF Connect SDK / ZBOSS)

When you want to create a blank Zigbee project and send data (like sensor readings or custom telemetry) from an **End Device** to a **Coordinator**, you must follow these 5 core architectural steps.

---

### Step 1: Choose Your ZCL Cluster & Role
In Zigbee, all data is organized into **Clusters** (functional domains) and **Attributes** (individual variables).
* **The Coordinator (Server):** Holds the actual attribute database. It runs a **Server Cluster** (e.g. Temperature Measurement Server).
* **The End Device (Client):** Gathers physical data and wants to send it to the coordinator. It runs a **Client Cluster** (e.g. Temperature Measurement Client).
* **The Action:** The End Device sends a ZCL `Write Attributes` request over-the-air to overwrite the attribute value on the Coordinator.

---

### Step 2: Configure the Coordinator (Server Side)

#### A. Declare the Variable and Attribute List
You define the variables where the ZBOSS stack will store the received values. You must manually declare the list to ensure the attribute is **Read-Write** (`ZB_ZCL_ATTR_ACCESS_READ_WRITE`), allowing remote write commands:

```c
static zb_int16_t my_temperature_value = 0;

// Declare the attributes list for the Temp Measurement cluster
ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(temp_measurement_attr_list, ZB_ZCL_TEMP_MEASUREMENT)
  // CRITICAL: Set the access to ZB_ZCL_ATTR_ACCESS_READ_WRITE
  ZB_ZCL_SET_ATTR_DESC_M(
      ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, 
      &my_temperature_value, 
      ZB_ZCL_ATTR_TYPE_S16, 
      ZB_ZCL_ATTR_ACCESS_READ_WRITE | ZB_ZCL_ATTR_ACCESS_REPORTING
  )
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;
```

#### B. Register the Server Cluster on an Endpoint
Register the cluster in the coordinator's cluster array and simple descriptor as an **input (server)** cluster:

```c
#define COORDINATOR_ENDPOINT 10

static zb_zcl_cluster_desc_t coordinator_clusters[] = {
    ZB_ZCL_CLUSTER_DESC(
        ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ZB_ZCL_ARRAY_SIZE(temp_measurement_attr_list, zb_zcl_attr_t),
        temp_measurement_attr_list,
        ZB_ZCL_CLUSTER_SERVER_ROLE, // Coordinator is the Server
        ZB_ZCL_MANUF_CODE_INVALID
    )
};

// Simple descriptor (1 Input Cluster, 0 Output Clusters)
ZB_DECLARE_SIMPLE_DESC(1, 0);
static ZB_AF_SIMPLE_DESC_TYPE(1, 0) simple_desc_coordinator_ep = {
    COORDINATOR_ENDPOINT,
    ZB_AF_HA_PROFILE_ID,
    0, 0, 0,
    1, 0, // 1 Input (Server), 0 Output (Client)
    { ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT }
};
```

#### C. Handle Incoming Writes in `main.c`
Register the modify attribute callback. This function will be triggered immediately when the end device writes the value:

```c
static void modify_attr_value_callback(zb_uint8_t ep, zb_uint16_t cluster_id, zb_uint16_t attr_id, zb_uint8_t *value)
{
    if (cluster_id == ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT &&
        attr_id == ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID) {
        
        zb_int16_t temp = *(zb_int16_t *)value;
        LOG_INF("Temperature updated remotely: %d", temp);
    }
}

int main(void) {
    // ... basic initialization ...
    ZB_ZCL_SET_MODIFY_ATTR_VALUE_CB(modify_attr_value_callback);
    // ... start Zigbee default thread ...
}
```

---

### Step 3: Configure the End Device (Client Side)

#### A. Register the Client Cluster
Even if the Client role doesn't store attributes locally, it **must** register the cluster as an **output (client)** cluster. If you omit this, the ZBOSS stack will block outgoing requests for that cluster.

```c
#define END_DEVICE_ENDPOINT 1

static zb_zcl_cluster_desc_t end_device_clusters[] = {
    ZB_ZCL_CLUSTER_DESC(
        ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        0, NULL,                     // Client role has no local attributes
        ZB_ZCL_CLUSTER_CLIENT_ROLE,  // End Device is the Client
        ZB_ZCL_MANUF_CODE_INVALID
    )
};

// Simple descriptor (0 Input Clusters, 1 Output Cluster)
ZB_DECLARE_SIMPLE_DESC(0, 1);
static ZB_AF_SIMPLE_DESC_TYPE(0, 1) simple_desc_end_device_ep = {
    END_DEVICE_ENDPOINT,
    ZB_AF_HA_PROFILE_ID,
    0, 0, 0,
    0, 1, // 0 Input (Server), 1 Output (Client)
    { ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT }
};
```

---

### Step 4: Pair (Commission) the Devices
For the end device to send data to the coordinator, they must first join the same network:
1. **Coordinator:** Starts commissioning and forms the network. It opens its "Network Steering" window (pairing mode).
2. **End Device:** Starts and searches for open networks. It joins the coordinator's network.
   * *Tip for development:* Call `zigbee_erase_persistent_storage(ZB_TRUE)` on startup during testing to ensure the end device clears old configurations and pairs fresh every time.

---

### Step 5: Send the Data from the End Device

Because Zigbee buffer allocation is asynchronous, you must first request an output buffer, then populate and send it in a callback:

```c
// 1. Trigger the send sequence
void trigger_send_data(void) {
    zb_ret_t err = zb_buf_get_out_delayed(send_data_callback);
    if (err != RET_OK) {
        LOG_ERR("Could not allocate buffer");
    }
}

// 2. Buffer callback that packs and sends the packet
static void send_data_callback(zb_bufid_t bufid)
{
    zb_int16_t temperature_to_send = 250; // Represents 25.0 C
    zb_uint8_t *ptr;

    // A. Initialize Write Attribute request in the buffer
    ZB_ZCL_GENERAL_INIT_WRITE_ATTR_REQ(bufid, ptr, ZB_ZCL_ENABLE_DEFAULT_RESPONSE);

    // B. Pack the Attribute ID, type, and pointer to your data
    ZB_ZCL_GENERAL_ADD_VALUE_WRITE_ATTR_REQ(
        ptr,
        ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        ZB_ZCL_ATTR_TYPE_S16,
        (zb_uint8_t *)&temperature_to_send
    );

    // C. Send the Write Attribute request over-the-air
    ZB_ZCL_GENERAL_SEND_WRITE_ATTR_REQ(
        bufid,
        ptr,
        0x0000,                            // Destination Address (0x0000 is always the Coordinator)
        ZB_APS_ADDR_MODE_16_ENDP_PRESENT,  // Direct addressing with Endpoint
        COORDINATOR_ENDPOINT,              // Destination Endpoint (10)
        END_DEVICE_ENDPOINT,               // Source Endpoint (1)
        ZB_AF_HA_PROFILE_ID,               // Profile ID
        ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,// Cluster ID
        NULL                               // Optional callback for transmission status
    );
}
```