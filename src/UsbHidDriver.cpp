#include "UsbHidDriver.h"
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"

static USBHIDKeyboard usbKeyboard;
static USBHIDMouse usbMouse;
static USBHIDConsumerControl usbMedia;

void UsbHidDriver::begin() {
    usbKeyboard.begin();
    usbMouse.begin();
    usbMedia.begin();
    USB.begin();
}
void UsbHidDriver::print(const String &s) { usbKeyboard.print(s); }
void UsbHidDriver::println(const String &s) { usbKeyboard.println(s); }
void UsbHidDriver::write(uint8_t k) { usbKeyboard.write(k); }
void UsbHidDriver::press(uint8_t k) { usbKeyboard.press(k); }
void UsbHidDriver::release(uint8_t k) { usbKeyboard.release(k); }
void UsbHidDriver::releaseAll() { usbKeyboard.releaseAll(); }
void UsbHidDriver::printScreen() { 
    usbKeyboard.pressRaw(HID_KEY_PRINT_SCREEN); 
    delay(100); 
    usbKeyboard.releaseAll(); 
}
void UsbHidDriver::mouseMove(int x, int y) { usbMouse.move(x, y); }
void UsbHidDriver::mouseScroll(int8_t wheel) { usbMouse.move(0, 0, wheel); }
void UsbHidDriver::mouseClick(uint8_t b) { usbMouse.click(b); }
void UsbHidDriver::mousePress(uint8_t b) { usbMouse.press(b); }
void UsbHidDriver::mouseRelease(uint8_t b) { usbMouse.release(b); }
void UsbHidDriver::mediaAction(const String& act) {
    if(act=="m_vup") { usbMedia.press(CONSUMER_CONTROL_VOLUME_INCREMENT); usbMedia.release(); }
    else if(act=="m_vdn") { usbMedia.press(CONSUMER_CONTROL_VOLUME_DECREMENT); usbMedia.release(); }
    else if(act=="m_mute") { usbMedia.press(CONSUMER_CONTROL_MUTE); usbMedia.release(); }
    else if(act=="m_pp") { usbMedia.press(CONSUMER_CONTROL_PLAY_PAUSE); usbMedia.release(); }
    else if(act=="m_next") { usbMedia.press(CONSUMER_CONTROL_SCAN_NEXT); usbMedia.release(); }
    else if(act=="m_prev") { usbMedia.press(CONSUMER_CONTROL_SCAN_PREVIOUS); usbMedia.release(); }
    else if(act=="m_bup") { usbMedia.press(CONSUMER_CONTROL_BRIGHTNESS_INCREMENT); usbMedia.release(); }
    else if(act=="m_bdn") { usbMedia.press(CONSUMER_CONTROL_BRIGHTNESS_DECREMENT); usbMedia.release(); }
    else if(act=="m_calc") { usbMedia.press(CONSUMER_CONTROL_CALCULATOR); usbMedia.release(); }
    else if(act=="m_mail") { usbMedia.press(CONSUMER_CONTROL_EMAIL_READER); usbMedia.release(); }
    else if(act=="m_air") { usbMedia.press(CONSUMER_CONTROL_WIRELESS_RADIO_CONTROLS); usbMedia.release(); }
    else if(act=="m_sleep") { usbMedia.press(CONSUMER_CONTROL_SLEEP); usbMedia.release(); }
    else if(act=="m_power") { usbMedia.press(CONSUMER_CONTROL_POWER); usbMedia.release(); }
    else if(act=="m_web") { usbMedia.press(CONSUMER_CONTROL_LOCAL_BROWSER); usbMedia.release(); }
    else if(act=="m_stop") { usbMedia.press(CONSUMER_CONTROL_STOP); usbMedia.release(); }
    else if(act=="m_srch") { usbMedia.press(CONSUMER_CONTROL_SEARCH); usbMedia.release(); }
    else if(act=="m_home") { usbMedia.press(CONSUMER_CONTROL_HOME); usbMedia.release(); }
    else if(act=="m_back") { usbMedia.press(CONSUMER_CONTROL_BACK); usbMedia.release(); }
    else if(act=="m_fwd") { usbMedia.press(CONSUMER_CONTROL_FORWARD); usbMedia.release(); }
    else if(act=="m_refr") { usbMedia.press(CONSUMER_CONTROL_REFRESH); usbMedia.release(); }
    else if(act=="m_book") { usbMedia.press(CONSUMER_CONTROL_BOOKMARKS); usbMedia.release(); }
}
bool UsbHidDriver::isConnected() { return true; }
