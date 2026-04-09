#include "BleHidDriver.h"
#include <BleCombo.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

static BleComboKeyboard bleKeyboard;
static BleComboMouse bleMouse(&bleKeyboard);

void BleHidDriver::begin() {
    NimBLEDevice::init("PwnStick");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    
    bleKeyboard.begin();
    bleMouse.begin();
}
void BleHidDriver::print(const String &s) { 
    for(unsigned int i = 0; i < s.length(); i++) {
        bleKeyboard.print((char)s[i]);
        delay(12); // Throttle BLE keystrokes to prevent packet dropping
    }
}
void BleHidDriver::println(const String &s) { 
    print(s);
    bleKeyboard.println();
    delay(12);
}
void BleHidDriver::write(uint8_t k) { bleKeyboard.write(k); }
void BleHidDriver::press(uint8_t k) { bleKeyboard.press(k); }
void BleHidDriver::release(uint8_t k) { bleKeyboard.release(k); }
void BleHidDriver::releaseAll() { bleKeyboard.releaseAll(); }
void BleHidDriver::printScreen() { 
    // Usually PrintScreen is mapped as 206 (0xCE)
    bleKeyboard.write(206); 
}
void BleHidDriver::mouseMove(int x, int y) { bleMouse.move(x, y); }
void BleHidDriver::mouseScroll(int8_t wheel) { bleMouse.move(0, 0, wheel); }
void BleHidDriver::mouseClick(uint8_t b) { bleMouse.click(b); }
void BleHidDriver::mousePress(uint8_t b) { bleMouse.press(b); }
void BleHidDriver::mouseRelease(uint8_t b) { bleMouse.release(b); }
void BleHidDriver::mediaAction(const String& act) {
    if(act=="m_vup") bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
    else if(act=="m_vdn") bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
    else if(act=="m_mute") bleKeyboard.write(KEY_MEDIA_MUTE);
    else if(act=="m_pp") bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
    else if(act=="m_next") bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
    else if(act=="m_prev") bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
    else if(act=="m_stop") bleKeyboard.write(KEY_MEDIA_STOP);
    else if(act=="m_calc") bleKeyboard.write(KEY_MEDIA_CALCULATOR);
    else if(act=="m_web") bleKeyboard.write(KEY_MEDIA_WWW_HOME);
    else if(act=="m_mail") bleKeyboard.write(KEY_MEDIA_LOCAL_MACHINE_BROWSER); 
    else if(act=="m_srch") bleKeyboard.write(KEY_MEDIA_WWW_SEARCH);
    else if(act=="m_home") bleKeyboard.write(KEY_MEDIA_WWW_HOME);
    else if(act=="m_back") bleKeyboard.write(KEY_MEDIA_WWW_BACK);
    else if(act=="m_fwd") bleKeyboard.write(KEY_MEDIA_WWW_HOME);
    else if(act=="m_refr") bleKeyboard.write(KEY_MEDIA_WWW_STOP); 
    else if(act=="m_book") bleKeyboard.write(KEY_MEDIA_WWW_BOOKMARKS);
}
bool BleHidDriver::isConnected() { return bleKeyboard.isConnected(); }

String BleHidDriver::getBondedDevices() {
    Preferences prefs;
    prefs.begin("ble_names", true);
    int numBonds = NimBLEDevice::getNumBonds();
    String json = "[";
    for (int i = 0; i < numBonds; i++) {
        NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
        String mac = String(addr.toString().c_str());
        String name = prefs.getString(mac.c_str(), "Unknown Device");
        if (i > 0) json += ",";
        json += "{\"mac\":\"" + mac + "\",\"name\":\"" + name + "\"}";
    }
    json += "]";
    prefs.end();
    return json;
}

void BleHidDriver::renameDevice(String mac, String name) {
    Preferences prefs;
    prefs.begin("ble_names", false);
    prefs.putString(mac.c_str(), name);
    prefs.end();
}

void BleHidDriver::connectToDevice(String mac) {
    if (mac.length() == 0 || mac == "any") {
        while (NimBLEDevice::getWhiteListCount() > 0) {
            NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
        }
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->stop();
        pAdvertising->setScanFilter(false, false);
        pAdvertising->start();
    } else {
        NimBLEAddress targetHost(mac.c_str());
        while (NimBLEDevice::getWhiteListCount() > 0) {
            NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
        }
        NimBLEDevice::whiteListAdd(targetHost);
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->stop();
        pAdvertising->setScanFilter(true, true);
        pAdvertising->start();
    }
}

void BleHidDriver::deleteDevice(String mac) {
    NimBLEAddress targetHost(mac.c_str());
    NimBLEDevice::deleteBond(targetHost);
    Preferences prefs;
    prefs.begin("ble_names", false);
    prefs.remove(mac.c_str());
    prefs.end();
}
