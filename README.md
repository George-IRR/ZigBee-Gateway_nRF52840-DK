# Work Plan: Zigbee-Modbus Gateway

## 1. Architecture and Requirements

The system acts as a bridge between a wireless Zigbee sensor network and a control system via Modbus.

* **End Device:** Battery-powered temperature sensor. Operates primarily in **Deep Sleep** mode and utilizes a **Hardware Watchdog Timer (WDT)** for recovery from hangs. It wakes up at regular intervals, applies a random delay (**Jitter**) to avoid RF collisions, and transmits an encrypted payload (**AES-128**). In case of ACK failure, it applies an **Exponential Backoff Rejoin** algorithm.
* **Gateway:** Asynchronously captures packets from the Zigbee network, decrypts them, and extracts useful information. Data is stored in **Shared Memory (Register Map)**. A **Stale Data Watchdog** monitors the time since the last reception; if a limit is exceeded, it overwrites the register with `0xFFFF` to invalidate old data.
* **Modbus Interface:** The Gateway runs a **Modbus TCP/RTU Server** that allows polling by an external client (SCADA/PLC) of the registers stored in the shared memory.
* **CI/CD:** Development includes an automated pipeline for building and unit testing, focusing on validating parsing logic, concurrent register access, and timeout triggers.

## 2. Diagrams

### 2.1 Data Flow

![Data Flow Diagram](Docs/Diagram_out/Data_Seq.svg)

### 2.2 System Topology

![System Topology Diagram](Docs/Diagram_out/System_Architecture.svg)

## 3. Development Steps

### Phase 1: Setup & CI/CD

* Select Gateway and End-device hardware.
* Configure basic **GitHub Actions** CI/CD pipeline.
* Integrate linter and **Unit Testing** framework.

### Phase 2: End-Device (Zigbee Node)

* Interface sensors with the microcontroller.
* Implement **Hardware WDT** and **Deep Sleep / Wake Up** routines.
* Establish Zigbee network connection with **AES-128** encryption and define payload format.
* Implement cyclic transmission with pre-transmission **Jitter** and **Exponential Backoff** for lost ACKs.

### Phase 3: Gateway & Parser

* Configure Gateway as a **Zigbee Coordinator**.
* Implement a **Message Queue** for asynchronous message handling.
* Develop the payload parser and write Unit Tests in the CI/CD pipeline to validate decoding.

### Phase 4: Modbus Server & Shared Memory

* Implement **Shared Memory** with concurrent access protection (**mutex**).
* Implement the **Stale Data Watchdog** for invalidating un-updated values.
* Set up the Modbus server on the designated port.
* Integrate automated logic tests for the flow: *Memory Write -> Timeout Trigger -> Modbus Response Validation*.

### Phase 5: E2E Integration (End-to-End)

* Full hardware/software system execution.
* Test register polling using an external Modbus client.
