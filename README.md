# PwnStick-S3

PwnStick-S3 is a versatile, wireless HID injection and media platform built specifically for the **LILYGO® T-Dongle-S3** (ESP32-S3). It transforms the dongle into a standalone access point that hosts a high-performance web interface for real-time remote control and visual manipulation.

**Hardware:** [Buy LILYGO® T-Dongle-S3 on AliExpress](https://ja.aliexpress.com/item/1005004860003638.html)

---

## Features

### 1. Wireless HID Control
*   **Keyboard Injection:** Low-latency keystroke streaming via WebSockets. Supports international character sets and paste-streaming.
*   **Precision Trackpad:** Mobile-friendly touch area for remote mouse control, including left/right click gestures.
*   **Sticky Modifiers:** Toggleable WIN, CTRL, and ALT keys for executing complex system shortcuts (e.g., `CTRL+ALT+DEL`, `ALT+F4`).

### 2. Built-in OS Macros
*   **Quick Payloads:** One-tap buttons to trigger common tasks on target systems (Windows and Linux supported).
*   **Macros included:** Open Terminal, Launch Calculator, and the classic Rickroll.

### 3. Real-time Visuals & Feedback
*   **Status Display:** The integrated 0.96" ST7735 LCD provides a "Matrix rain" visual effect with live overlays for typed keys and mouse movement.
*   **QR Connectivity:** Displays an auto-generated QR code on boot for instant smartphone connection to the captive portal.
*   **RGB Feedback:** Onboard WS2812 LED provides system status alerts.

### 4. Media Beaming Engine
*   **Image Uploader:** Upload and crop static images directly from your browser to the dongle screen.
*   **GIF Flipbook:** Specialized binary parser that extracts and loops animated GIF frames on the device hardware.
*   **On-Device Editor:** Full support for zooming, rotating, and panning images/GIFs before "beaming" them to the LCD.

---

## Installation

This project is built using **PlatformIO**.

1.  Clone this repository.
2.  Open the project folder in VS Code with the PlatformIO extension installed.
3.  Connect your T-Dongle-S3.
4.  Run the **Upload** task (`pio run -t upload`).
5.  The device will reboot and host a WiFi AP named `PwnDongle`.

---

## Configuration & Customization

The project is designed to be easily modified to fit specific use cases:

*   **Logic & UI:** The core application, including the embedded HTML/JavaScript dashboard and the HID logic, is located in `src/main.cpp`.
*   **QR Code:** The hardcoded connection QR code matrix is stored in `src/qr_code.h`.
*   **Hardware Drivers:** Low-level display handling is managed by `src/esp_lcd_st7735.h`.

---

## Technical Note
PwnStick-S3 operates 100% locally. It serves its own web environment, handles binary image processing in-browser, and requires no external internet connection or third-party JS libraries.
