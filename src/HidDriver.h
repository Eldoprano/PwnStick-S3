#pragma once
#include <Arduino.h>

class IHidDriver {
public:
    virtual void begin() = 0;
    virtual void print(const String &s) = 0;
    virtual void println(const String &s) = 0;
    virtual void write(uint8_t k) = 0;
    virtual void press(uint8_t k) = 0;
    virtual void release(uint8_t k) = 0;
    virtual void releaseAll() = 0;
    virtual void printScreen() = 0;
    
    virtual void mouseMove(int x, int y) = 0;
    virtual void mouseScroll(int8_t wheel) = 0;
    virtual void mouseClick(uint8_t b) = 0;
    virtual void mousePress(uint8_t b) = 0;
    virtual void mouseRelease(uint8_t b) = 0;
    
    virtual void mediaAction(const String& act) = 0;
    virtual bool isConnected() = 0;
};

extern IHidDriver* activeDriver;
extern class UsbHidDriver* usbDriverPtr;
extern class BleHidDriver* bleDriverPtr;
extern bool isBleMode;

void setHidMode(bool useBle);
