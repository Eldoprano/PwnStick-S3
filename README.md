# PwnStick-S3

PwnStick-S3 is a versatile, wireless HID injection and media platform built specifically for the **LILYGO® T-Dongle-S3** (ESP32-S3). It transforms the dongle into a standalone access point that hosts a web interface for real-time remote control, keystroke injection, and dongle screen manipulation via either USB or Bluetooth Low Energy (BLE).

**Hardware:** [Buy LILYGO® T-Dongle-S3 (With LCD) on AliExpress](https://ja.aliexpress.com/item/1005004860003638.html)

---

## Features

### 1. Dual-Mode Wireless HID Control (USB & BLE)
*   **Keyboard Injection:** Low-latency keystroke streaming via WebSockets. Supports international character sets, paste-streaming, and custom macros for Windows, Linux, and Media controls.
*   **Dual Transport Layers:** Switch between injecting payloads over the physical USB connection or wirelessly over Bluetooth (BLE) with the click of a button in the Web UI.
*   **BLE Device Manager:** The dongle utilizes NVS memory for persistent Bluetooth bonding. Use the Web UI's Device Manager (⚙️) to view paired devices, rename them, and selectively connect to specific hosts (like switching between your laptop and phone).

### 2. Precision Trackpad & Gestures
*   **Two-Finger Scroll:** The mobile-friendly web trackpad natively supports two-finger scrolling—drag two fingers to scroll up and down, even outside the bounds of the trackpad.
*   **Multi-touch Clicks:** Supports one-finger tap for left-click and two-finger tap for right-click.

### 3. Real-time Visuals & Feedback
*   **Status Display:** The integrated 0.96" ST7735 LCD provides a "Matrix rain" visual effect with live overlays for typed keys and mouse movement.
*   **QR Connectivity:** Displays an auto-generated QR code on boot for instant smartphone connection to the captive portal.

### 4. Media Beaming Engine
*   **Image & GIF Uploader:** Upload, crop, and beam static images or animated GIFs directly from your browser to the dongle's LCD screen. Full support for zooming, rotating, and panning before uploading.

---

## Installation

This project is built using **PlatformIO**. You can upload the firmware using either the VS Code GUI or the Command Line Interface (CLI).

### ⚠️ Important: Bootloader Mode
Before uploading the firmware, you must put the T-Dongle-S3 into **Bootloader Mode**. 
1. Press and **hold** the tiny button on the side of the dongle.
2. While holding the button, plug the dongle into your computer's USB port.
3. Once inserted, you can release the button. The device is now ready to be flashed.

### Option A: VS Code (GUI)
1.  Clone this repository.
2.  Open the project folder in VS Code with the [PlatformIO extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) installed.
3.  Put the dongle into Bootloader Mode and connect it.
4.  Click the **Upload** icon (arrow) in the bottom PlatformIO toolbar.

### Option B: Command Line (CLI)
1.  Ensure you have **Python** installed, then install the **PlatformIO Core**:
    ```bash
    pip install platformio
    ```
2.  Put the dongle into Bootloader Mode and connect it.
3.  Navigate to the project directory in your terminal.
4.  Run the compile and upload command:
    ```bash
    pio run -t upload
    ```
    *If you have multiple devices connected, you may need to specify the port: `pio run -t upload --upload-port /dev/ttyACM0`.*

**After Upload:** The device will reboot and host a WiFi AP named **`PwnStick`**. Connect to it using your phone (or scan the QR code on the LCD) to access the interface at `http://192.168.4.1/`.

---

## Where is what

*   **Logic & UI:** The core application, including the embedded HTML/JavaScript dashboard and the WebSocket logic, is located in `src/main.cpp`.
*   **HID Drivers:** The transport layer is abstracted in `src/HidDriver.h`, with specific implementations for USB (`src/UsbHidDriver.cpp`) and Bluetooth (`src/BleHidDriver.cpp`).
*   **QR Code:** The hardcoded connection QR code matrix is stored in `src/qr_code.h`.
*   **Hardware Drivers:** Low-level display handling is managed by `src/esp_lcd_st7735.c` and `src/esp_lcd_st7735.h`.

---

## Technical Note
PwnStick-S3 operates 100% locally. It serves its own web environment, handles binary image processing in-browser, and requires no external internet connection or third-party JS libraries. The BLE implementation utilizes the highly optimized `NimBLE-Arduino` stack to minimize memory footprint and run simultaneously alongside the ESP32's WiFi stack.
