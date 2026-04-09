with open('src/BleHidDriver.cpp', 'r') as f:
    code = f.read()

code = code.replace('static BleComboMouse bleMouse;', 'static BleComboMouse bleMouse(&bleKeyboard);')
code = code.replace('bleKeyboard.write(KEY_MEDIA_WWW_FORWARD); // May fail if undefined, replace below if err', 'bleKeyboard.write(KEY_MEDIA_WWW_HOME);')
code = code.replace('bool BleHidDriver::isConnected() { return bleKeyboard.isConnected() || bleMouse.isConnected(); }', 'bool BleHidDriver::isConnected() { return bleKeyboard.isConnected(); }')

with open('src/BleHidDriver.cpp', 'w') as f:
    f.write(code)
