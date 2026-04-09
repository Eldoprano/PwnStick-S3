#pragma once
#include "HidDriver.h"
class BleHidDriver : public IHidDriver {
public:
    void begin() override;
    void print(const String &s) override;
    void println(const String &s) override;
    void write(uint8_t k) override;
    void press(uint8_t k) override;
    void release(uint8_t k) override;
    void releaseAll() override;
    void printScreen() override;
    void mouseMove(int x, int y) override;
    void mouseScroll(int8_t wheel) override;
    void mouseClick(uint8_t b) override;
    void mousePress(uint8_t b) override;
    void mouseRelease(uint8_t b) override;
    void mediaAction(const String& act) override;
    bool isConnected() override;
    
    // BLE Device Management
    String getBondedDevices();
    void renameDevice(String mac, String name);
    void connectToDevice(String mac);
    void deleteDevice(String mac);
};
