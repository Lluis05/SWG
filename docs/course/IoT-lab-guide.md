# Internet of Things Labs — course guide

Markdown conversion of `IoT-lab-guide.pdf` (figures omitted; see the PDF).

Groups of three work on an IoT project, sending data from an electronic device
to the cloud through an intermediate Edge device. The group acts as a company
hired to deliver the project, with minimum requirements evaluated at the end of
the course. A small-scale implementation of the design is allowed: parts that
cannot be built can be simulated, generating data and transmitting it from the
Device to the Cloud.

## Layers

### Device (Sensor and PCB)
Sensors (button, accelerometer, light, sound, temperature, humidity, …) are
connected to a microcontroller that receives and processes their data. Data is
extracted over Bluetooth Low Energy (BLE). Each group is lent a Nordic
nRF52840-DK (see the nRF Connect SDK Quick Start documentation).

### Edge (Android app, Raspberry Pi or laptop)
An application connects to the PCB via BLE and receives the data, which can be
processed on the device or sent to a server over the Internet. Android is
suggested (about 70 % of phones); iOS/macOS or an embedded platform such as a
Raspberry Pi are also possible.

### Cloud
Data must be stored and displayed: a database with a back end and a front end
(e.g. a web graph). The data can be used for computational tasks such as
prediction or detection. A commercial platform or a server on your own
computer can be used.

## Tasks
- Program the PCB and send data via BLE
- Receive data from BLE in an Android app or Edge device
- Send data from Edge to Cloud
- Store data on the cloud server
- Create some ML model with the data at Edge and/or Cloud level
- Show data to final users

## Embedded system example (Nordic nRF52)
Options for programming the PCB:
- Nordic tools (nRF Connect) and the VS Code plugin.
- PlatformIO, which supports Arduino, Zephyr RTOS and Mbed.
  - Arduino: **deprecated — errors in the BLE library since 2024**.
  - Zephyr.

Step by step to run Bluetooth through Zephyr (v2.7) with PlatformIO:
1. Download https://github.com/platformio/platform-nordicnrf52/tree/master
2. Copy `example/zephyr-ble-beacon` to your working directory.
3. In VS Code, "Open project" and select that folder.
4. In `platformio.ini`, delete the `particle_xenon` board block.
5. Compile and upload.
6. In the nRF Connect phone app you should see a beacon called "Test beacon".

## Android BLE example
Reference: https://developer.android.com/guide/topics/connectivity/bluetooth/ble-overview
(Kotlin recommended, Java allowed.)

### 1. Permissions (`AndroidManifest.xml`)
For Android 12 / API 31+:
```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```
Android 11 or lower uses the legacy Bluetooth and location permissions.

### 2. Technical references
- **BLE scanning:** `BluetoothLeScanner` and `ScanCallback`, filtering by your Service UUID.
- **GATT connection and service discovery:** `BluetoothGatt` and `BluetoothGattCallback`.
- **Notifications:** enable characteristic notifications locally and write to the
  CCCD (`00002902-0000-1000-8000-00805f9b34fb`) to receive continuous updates.

## Example cloud platforms
Amazon, Google Cloud, Azure, Oracle (free tier VM), or simulate the cloud on
your computer by installing back-end / web / DB servers.

## Sample tools (any tool is allowed)
- **PCB:** PlatformIO (Arduino, Zephyr RTOS, Mbed) in VS Code; Nordic SDK in VS Code.
- **Android:** Android Studio, Xamarin, Flutter, Ionic.
- **Back-end:** Firebase, MQTT, Influx, AWS Amplify.
- **Front-end:** HTML5/CSS/JS framework, mobile.

## Evaluation
Final score = 1st report (20 %) + 2nd report (20 %) + project (60 %).
Groups A = 1 to 7, Groups B = 8 to 14.

- **First report** (PCB and Android work on BLE communication), Virtual Campus: **30 October**.
- **Project evaluation in the lab:** **7 January** (date changes published on Virtual Campus).
- **Second report** (back end and front end) + zip of the code or GitHub repository,
  Virtual Campus: **10 January**.

## Project deliverables and report guidelines
Both reports must follow a professional technical format, detailing design
choices, custom protocol specifications and validation steps.

### Required report structure

1. **Executive Summary & Project Overview**
   - Description of the company, the IoT product and the target use case.
   - Summary of project goals and system capabilities.
2. **System Architecture & Diagram**
   - Complete block diagram with all three layers: Device (sensors & PCB),
     Edge (mobile/gateway), Cloud (backend/frontend/ML).
   - Interfaces, data flows and communication channels between layers.
3. **Layer Implementation Details**
   - **Device:** board, sensor hardware, firmware framework, hardware interrupts, power management.
   - **Edge:** platform choice, OS version, BLE scanning/connection management.
   - **Cloud:** backend technology, database strategy (time-series, relational, NoSQL),
     web UI framework, ML model design and purpose.
   - **Libraries & frameworks:** explicit list of third-party dependencies, APIs and libraries per stage.
4. **Data Structures & Protocol Payload Specifications**
   - **BLE data frame:** precise byte breakdown (header, sensor fields, timestamp, CRC/checksum).
   - **Edge-to-Cloud format:** protocol (MQTT, HTTP REST, WebSockets, …) and JSON/payload schema.
5. **Testing, Validation & Results**
   - **Per-layer testing:** unit and functional tests per component (sensor reading
     accuracy, BLE connection stability, API response times).
   - **End-to-end testing:** full data flow from the physical measurement to the web dashboard.
   - **Test evidence:** graphs, logs or screenshots showing operational stability.

### Team assessment & course feedback (final section of the 2nd report)

1. **Teamwork & individual contribution**
   - **Task distribution matrix:** responsibilities, modules and code led by each member.
   - **Self-assessment:** technical growth, problem-solving approach, personal commitment.
   - **Peer evaluation & collaboration:** workflow (version control, task tracking,
     meetings), decision making, how challenges were resolved.
2. **Course & subject feedback**
   - **Strengths:** most engaging/beneficial aspects.
   - **Areas for improvement:** what to modify or expand (instructions, frameworks,
     hardware, schedule, evaluation criteria).
