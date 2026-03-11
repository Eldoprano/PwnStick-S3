# PwnStick-S3

PwnStick-S3 is a versatile, wireless HID injection and media platform built specifically for the **LILYGO® T-Dongle-S3** (ESP32-S3). It transforms the dongle into a standalone access point that hosts a web interface for real-time remote control and dongle screen manipulation. 

**Hardware:** [Buy LILYGO® T-Dongle-S3 (With LCD) on AliExpress](https://ja.aliexpress.com/item/1005004860003638.html)

---

## Features

### 1. Wireless HID Control
*   **Keyboard Injection:** Low-latency keystroke streaming via WebSockets. Supports international character sets and paste-streaming.
*   **Precision Trackpad:** Mobile-friendly touch area for remote mouse control, including left/right click gestures.
*   **Sticky Modifiers:** Toggleable WIN, CTRL, and ALT keys for executing complex system shortcuts (e.g., `CTRL+ALT+DEL`, `ALT+F4`).

### 2. Real-time Visuals & Feedback
*   **Status Display:** The integrated 0.96" ST7735 LCD provides a "Matrix rain" visual effect with live overlays for typed keys and mouse movement.
*   **QR Connectivity:** Displays an auto-generated QR code on boot for instant smartphone connection to the captive portal.

### 3. Media Beaming Engine
*   **Image Uploader:** Upload and crop static images directly from your browser to the dongle screen.
*   **GIF Flipbook:** Specialized binary parser that extracts and loops animated GIF frames on the device hardware.
*   **On-Device Editor:** Full support for zooming, rotating, and panning images/GIFs before "beaming" them to the LCD.

---

## Installation

This project is built using **PlatformIO**. You can upload the firmware using either the VS Code GUI or the Command Line Interface (CLI).

### Option A: VS Code (GUI)
1.  Clone this repository.
2.  Open the project folder in VS Code with the [PlatformIO extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) installed.
3.  Connect your T-Dongle-S3 via USB.
4.  Click the **Upload** icon (arrow) in the bottom PlatformIO toolbar.

### Option B: Command Line (CLI)
1.  Ensure you have **Python** installed, then install the **PlatformIO Core**:
    ```bash
    pip install platformio
    ```
2.  Connect your T-Dongle-S3 via USB.
3.  Navigate to the project directory in your terminal.
4.  Run the compile and upload command:
    ```bash
    pio run -t upload
    ```
    *If you have multiple devices connected, you may need to specify the port: `pio run -t upload --upload-port /dev/ttyACM0`.*

**After Upload:** The device will reboot and host a WiFi AP named **`PwnDongle`**. Connect to it using your phone to access the interface.

---

## Where is what

*   **Logic & UI:** The core application, including the embedded HTML/JavaScript dashboard and the HID logic, is located in `src/main.cpp`.
*   **QR Code:** The hardcoded connection QR code matrix is stored in `src/qr_code.h`.
*   **Hardware Drivers:** Low-level display handling is managed by `src/esp_lcd_st7735.h`.

---

## Technical Note
PwnStick-S3 operates 100% locally. It serves its own web environment, handles binary image processing in-browser, and requires no external internet connection or third-party JS libraries.