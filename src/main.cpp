#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include <FastLED.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7735.h"
#include <Fonts/glcdfont.c>
#include "qr_code.h"

// Hardware Pins
#define LED_DI_PIN     40
#define NUM_LEDS       1
#define PIN_NUM_MOSI   3
#define PIN_NUM_CLK    5
#define PIN_NUM_CS     4
#define PIN_NUM_DC     2
#define PIN_NUM_RST    1
#define PIN_NUM_BCKL   38

// Swapped colors for ST7735
#define C_GREEN      0xE007
#define C_DARKGREEN  0x4002
#define C_DIM        0x2001
#define C_RED        0x00F8
#define C_WHITE      0xFFFF
#define C_BLACK      0x0000

CRGB leds[NUM_LEDS];
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static uint16_t screen_buf[160 * 80];
static uint16_t custom_img_buf[160 * 80];

const char* ssid = "PwnDongle";
String targetOS = "win";
String lastKey = "";
unsigned long lastKeyTime = 0;
bool show_img = false;

// Mouse cursor
int cursorX = 80, cursorY = 40;
int showCursorFrames = 0;

// QR Code
unsigned long qrStartTime = 0;
bool qrActive = false;
bool qrWasActive = false;
int lastClientCount = 0;

void setLastKey(String k) { lastKey = k; lastKeyTime = millis(); }

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>PwnDongle v14</title>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/cropperjs/1.5.13/cropper.min.css">
<style>
body { background:#000; color:#0f0; font-family:monospace; margin:0; text-align:center; overflow:hidden; overscroll-behavior:none; }
.tabs { display:flex; border-bottom:1px solid #0f0; background:#0a0a0a; }
.tab { flex:1; padding:15px; cursor:pointer; font-weight:bold; border-right:1px solid #111; font-size:14px; }
.tab.active { background:#0f0; color:#000; }
.content { padding:10px; display:none; height:calc(100vh - 50px); overflow-y:auto; box-sizing:border-box; }
.content.active { display:block; }
button { background:#000; color:#0f0; border:1px solid #0f0; padding:15px; margin:5px; font-weight:bold; width:100%; font-size:16px; touch-action:manipulation; border-radius:4px; }
button:active { background:#0f0; color:#000; }
button.toggled { background:#0f0; color:#000; box-shadow:0 0 10px #0f0; }
.row { display:flex; gap:10px; }
textarea { width:100%; height:80px; background:#111; color:#0f0; border:1px dashed #333; padding:10px; box-sizing:border-box; font-size:1.2em; outline:none; margin-top:5px; }
#pad { flex:1; background:#0a0a0a; border:1px solid #333; margin-top:10px; display:flex; align-items:center; justify-content:center; color:#444; touch-action:none; min-height:30vh; border-radius:8px; font-weight:bold; }
#crop-container { width:100%; height:250px; background:#111; margin-top:10px; border:1px solid #333; position:relative; overflow:hidden; display:none; }
#crop-img { max-width:100%; display:block; }
.file-btn { position:relative; overflow:hidden; display:inline-block; width:100%; }
.file-btn input[type=file] { position:absolute; font-size:100px; right:0; top:0; opacity:0; cursor:pointer; }
.status { color:#888; font-size:12px; margin:5px; height:15px; }
</style>
<script src="https://cdnjs.cloudflare.com/ajax/libs/cropperjs/1.5.13/cropper.min.js"></script>
</head>
<body>
<div class="tabs">
<div class="tab active" onclick="sT('kb',this)">KEYBOARD</div>
<div class="tab" onclick="sT('ms',this)">MOUSE</div>
<div class="tab" onclick="sT('ig',this)">SCREEN</div>
</div>

<div id="c-kb" class="content active">
    <div class="row">
        <button id="b-win" class="toggled" onclick="os='win';wsS('O:win');uOS()">WIN OS</button>
        <button id="b-lin" onclick="os='lin';wsS('O:lin');uOS()">LINUX OS</button>
    </div>
    <div class="row" style="margin-top:10px">
        <button id="m-win" onmousedown="mD('win')" onmouseup="mU('win')" ontouchstart="mD('win');event.preventDefault()">WIN</button>
        <button id="m-ctrl" onmousedown="mD('ctrl')" onmouseup="mU('ctrl')" ontouchstart="mD('ctrl');event.preventDefault()">CTRL</button>
        <button id="m-alt" onmousedown="mD('alt')" onmouseup="mU('alt')" ontouchstart="mD('alt');event.preventDefault()">ALT</button>
    </div>
    <textarea id="ta" placeholder="Type messages..."></textarea>
    <div class="row" style="margin-top:10px">
        <button onclick="wsS('A:term')">TERM</button>
        <button onclick="wsS('A:calc')">CALC</button>
    </div>
    <div id="win-tools" class="row">
        <button onclick="wsS('A:cad')" style="color:#f00;border-color:#f00">C-A-D</button>
        <button onclick="wsS('A:lock')">LOCK</button>
    </div>
    <div id="lin-tools" class="row" style="display:none;">
        <button onclick="wsS('A:reisub')" style="color:#f00;border-color:#f00">REBOOT</button>
        <button onclick="wsS('A:reisuo')" style="color:#f00;border-color:#f00">OFF</button>
    </div>
    <button onclick="wsS('A:rick')">RICKROLL PC</button>
</div>

<div id="c-ms" class="content">
    <button id="b-air" onclick="tAir()">GYRO MOUSE: OFF</button>
    <div id="pad">TRACKPAD</div>
    <div class="row" style="margin-top:10px;">
        <button onmousedown="wsS('D:l')" onmouseup="wsS('U:l')" ontouchstart="wsS('D:l');event.preventDefault()">LEFT</button>
        <button onmousedown="wsS('D:m')" onmouseup="wsS('U:m')" ontouchstart="wsS('D:m');event.preventDefault()">MID</button>
        <button onmousedown="wsS('D:r')" onmouseup="wsS('U:r')" ontouchstart="wsS('D:r');event.preventDefault()">RIGHT</button>
    </div>
</div>

<div id="c-ig" class="content">
    <div class="file-btn">
        <button>SELECT IMAGE</button>
        <input type="file" id="img-f" accept="image/*">
    </div>
    <div id="status" class="status"></div>
    <div id="crop-container"><img id="crop-img"></div>
    <div id="ig-controls" style="display:none;">
        <div class="row">
            <button onclick="cropper.rotate(-90)">ROT L</button>
            <button onclick="cropper.rotate(90)">ROT R</button>
        </div>
        <button onclick="uploadImg()">UPLOAD TO DONGLE</button>
    </div>
    <button onclick="wsS('I:clear')" style="margin-top:20px;border-color:#444;color:#666">CLEAR SCREEN</button>
</div>

<script>
let ws=new WebSocket('ws://'+location.host+'/ws');
let os='win', cropper, air=false;
function wsS(m){if(ws.readyState===1)ws.send(m);}
function sT(t,el){
document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
document.querySelectorAll('.content').forEach(x=>x.classList.remove('active'));
el.classList.add('active');
document.getElementById('c-'+t).classList.add('active');
}
function uOS(){
document.getElementById('b-win').className=(os=='win'?'toggled':'');
document.getElementById('b-lin').className=(os=='lin'?'toggled':'');
document.getElementById('win-tools').style.display=(os=='win'?'flex':'none');
document.getElementById('lin-tools').style.display=(os=='lin'?'flex':'none');
}
function mD(m){
let el=document.getElementById('m-'+m);
el.dataset.t=Date.now();
el.dataset.h=setTimeout(()=>{
el.dataset.h=0;
el.classList.toggle('toggled');
wsS('H:'+m+','+(el.classList.contains('toggled')?'1':'0'));
},400);
}
function mU(m){
let el=document.getElementById('m-'+m);
if(el.dataset.h){
clearTimeout(el.dataset.h); el.dataset.h=0;
if(Date.now()-el.dataset.t<400)wsS('P:'+m);
}
}
let ta=document.getElementById('ta');
ta.addEventListener('input',e=>{ let c=ta.value.slice(-1); ta.value=''; if(c)wsS('K:'+c); });
ta.addEventListener('keydown',e=>{
if(e.key==='Enter'){e.preventDefault();wsS('E:1');}
if(e.key==='Backspace'){e.preventDefault();wsS('B:1');}
if(e.key==='Tab'){e.preventDefault();wsS('T:1');}
});

// Trackpad
let p=document.getElementById('pad'), lX=0, lY=0, isD=false, moved=false, tapT=0, fingers=0;
p.addEventListener('mousedown',e=>{isD=true;moved=false;lX=e.clientX;lY=e.clientY;tapT=Date.now();fingers=1;});
document.addEventListener('mouseup',()=>{if(isD&&!moved&&Date.now()-tapT<300)wsS('C:l');isD=false;});
p.addEventListener('mousemove',e=>{if(isD){let dx=e.clientX-lX,dy=e.clientY-lY;if(Math.abs(dx)>1||Math.abs(dy)>1)moved=true;wsS('M:'+dx+','+dy);lX=e.clientX;lY=e.clientY;}});
p.addEventListener('touchstart',e=>{isD=true;moved=false;fingers=e.touches.length;lX=e.touches[0].clientX;lY=e.touches[0].clientY;tapT=Date.now();e.preventDefault();},{passive:false});
p.addEventListener('touchend',e=>{if(isD&&!moved&&Date.now()-tapT<300)wsS('C:'+(fingers>1?'r':'l'));isD=false;e.preventDefault();},{passive:false});
p.addEventListener('touchmove',e=>{if(isD){let dx=e.touches[0].clientX-lX,dy=e.touches[0].clientY-lY;if(Math.abs(dx)>1||Math.abs(dy)>1)moved=true;wsS('M:'+Math.round(dx)+','+Math.round(dy));lX=e.touches[0].clientX;lY=e.touches[0].clientY;}e.preventDefault();},{passive:false});

function tAir(){
if(!air && typeof DeviceOrientationEvent!=='undefined'&&typeof DeviceOrientationEvent.requestPermission==='function'){
DeviceOrientationEvent.requestPermission().then(r=>{if(r==='granted')togA();}).catch(console.error);
}else togA();
}
function togA(){
air=!air; let b=document.getElementById('b-air');
b.innerText='GYRO MOUSE: '+(air?'ON':'OFF'); b.className=air?'toggled':'';
}
window.addEventListener('deviceorientation',e=>{
if(air){ let x=Math.round((e.gamma||0)/2), y=Math.round(((e.beta||0)-45)/2); if(x||y)wsS('M:'+x+','+y); }
});

document.getElementById('img-f').addEventListener('change', function(e) {
    let f=e.target.files[0]; if(!f)return;
    document.getElementById('status').innerText='Loading...';
    let r=new FileReader();
    r.onload=ev=>{
        let i=document.getElementById('crop-img'); i.src=ev.target.result;
        document.getElementById('crop-container').style.display='block';
        document.getElementById('ig-controls').style.display='block';
        if(cropper)cropper.destroy();
        cropper=new Cropper(i,{aspectRatio:2,viewMode:1,guides:false,background:false});
        document.getElementById('status').innerText='Image Ready';
    };
    r.readAsDataURL(f);
});

function uploadImg(){
if(!cropper)return;
document.getElementById('status').innerText='Sending...';
let canvas=cropper.getCroppedCanvas({width:160,height:80});
let d=canvas.getContext('2d').getImageData(0,0,160,80).data;
let b=new Uint8Array(25600);
for(let j=0;j<12800;j++){
let r=d[j*4],g=d[j*4+1],bl=d[j*4+2];
let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3);
b[j*2]=rgb>>8; b[j*2+1]=rgb&0xFF;
}
ws.send(b);
setTimeout(()=>document.getElementById('status').innerText='Uploaded!',1000);
}
</script>
</body>
</html>
)rawliteral";

void runMacro(String c) {
    Keyboard.releaseAll();
    if(c=="term") {
        if(targetOS == "win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("cmd"); }
        else { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('t'); delay(300); Keyboard.releaseAll(); }
    } else if(c=="calc") {
        if(targetOS == "win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("calc"); }
        else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.print("gnome-calculator"); delay(50); Keyboard.write(KEY_RETURN); }
    } else if(c=="cad") { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_DELETE); delay(100); Keyboard.releaseAll(); }
    else if(c=="lock") {
        if(targetOS == "win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('l'); delay(100); Keyboard.releaseAll(); }
        else { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('l'); delay(100); Keyboard.releaseAll(); }
    } else if(c=="reisub" || c=="reisuo") {
        Keyboard.press(KEY_LEFT_ALT); Keyboard.press(70);
        const char* seq = (c=="reisub") ? "reisub" : "reisuo";
        for(int i=0; seq[i]; i++) { delay(200); Keyboard.press(seq[i]); delay(100); Keyboard.release(seq[i]); }
        delay(100); Keyboard.releaseAll();
    } else if(c=="rick") {
        if(targetOS == "win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("https://www.youtube.com/watch?v=dQw4w9WgXcQ"); }
        else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.print("xdg-open 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'"); delay(50); Keyboard.write(KEY_RETURN); }
    }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
        if (info->opcode == WS_TEXT) {
            data[len] = 0; String msg = (char*)data;
            if (msg.startsWith("K:")) { String k = msg.substring(2); Keyboard.print(k); setLastKey(k); }
            else if (msg.startsWith("E:")) { Keyboard.write(KEY_RETURN); setLastKey("ENT"); }
            else if (msg.startsWith("B:")) { Keyboard.write(KEY_BACKSPACE); setLastKey("DEL"); }
            else if (msg.startsWith("T:")) { Keyboard.write(KEY_TAB); setLastKey("TAB"); }
            else if (msg.startsWith("M:")) {
                int comma = msg.indexOf(',');
                if (comma > 0) {
                    int x = msg.substring(2, comma).toInt(); int y = msg.substring(comma+1).toInt();
                    Mouse.move(x, y); cursorX += x; cursorY += y;
                    if(cursorX < 0) cursorX = 0; if(cursorX > 156) cursorX = 156;
                    if(cursorY < 0) cursorY = 0; if(cursorY > 76) cursorY = 76;
                    showCursorFrames = 20;
                }
            } else if (msg.startsWith("D:") || msg.startsWith("C:")) {
                char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT;
                if (msg.startsWith("C:")) Mouse.click(btn); else Mouse.press(btn);
            } else if (msg.startsWith("U:")) {
                char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT;
                Mouse.release(btn);
            } else if (msg.startsWith("H:")) {
                int comma = msg.indexOf(','); String mod = msg.substring(2, comma); bool st = msg.substring(comma+1)=="1";
                uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
                if(st) Keyboard.press(k); else { Keyboard.release(k); delay(50); Keyboard.write(0); } // WIN key fix
            } else if (msg.startsWith("P:")) {
                String mod = msg.substring(2); uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
                Keyboard.press(k); delay(100); Keyboard.release(k); setLastKey(mod);
            } else if (msg.startsWith("A:")) { String act = msg.substring(2); runMacro(act); setLastKey(act); }
            else if (msg.startsWith("O:")) { targetOS = msg.substring(2); }
            else if (msg.startsWith("I:clear")) { show_img = false; }
        } else if (info->opcode == WS_BINARY && len == 25600) {
            memcpy(custom_img_buf, data, 25600); show_img = true;
        }
    }
}

void drawChar(int x, int y, char c, uint16_t color, int scale) {
    if (c < 0 || c > 255) return;
    for (int i = 0; i < 5; i++) {
        uint8_t line = pgm_read_byte(&font[c * 5 + i]);
        for (int j = 0; j < 8; j++) {
            if (line & 0x1) {
                for (int sx = 0; sx < scale; sx++) {
                    for (int sy = 0; sy < scale; sy++) {
                        int px = x + i * scale + sx; int py = y + j * scale + sy;
                        if (px >= 0 && px < 160 && py >= 0 && py < 80) screen_buf[py * 160 + px] = color;
                    }
                }
            }
            line >>= 1;
        }
    }
}

void drawString(int x, int y, const char* str, uint16_t color, int scale) {
    while (*str) { drawChar(x, y, *str, color, scale); x += 6 * scale; str++; }
}

void updateDisplay() {
    if (show_img) {
        memcpy(screen_buf, custom_img_buf, 25600);
    } else {
        int clients = WiFi.softAPgetStationNum();
        if (clients > lastClientCount) { qrActive = true; qrStartTime = millis(); }
        lastClientCount = clients;

        if (qrActive) {
            if (millis() - qrStartTime > 10000) { qrActive = false; qrWasActive = true; }
            for(int i=0; i<160*80; i++) screen_buf[i] = C_BLACK;
            int sc = 3; int ox = (160 - qr_size * sc) / 2; int oy = (80 - qr_size * sc) / 2;
            for(int y=0; y<qr_size; y++) {
                for(int x=0; x<qr_size; x++) {
                    uint16_t c = qr_data[y * qr_size + x] ? C_BLACK : C_WHITE;
                    for(int sy=0; sy<sc; sy++) for(int sx=0; sx<sc; sx++) screen_buf[(oy + y*sc + sy)*160 + (ox + x*sc + sx)] = c;
                }
            }
        } else {
            if (qrWasActive) { for(int i=0; i<160*80; i++) screen_buf[i] = C_BLACK; qrWasActive = false; }
            static int drops[160]; static bool init = false;
            if (!init) { for(int i=0; i<160; i++) drops[i] = random(-40, 0); init = true; }
            
            // Fast trail fading
            for(int i=0; i<160*80; i++) {
                uint16_t c = screen_buf[i];
                if (c == C_RED) screen_buf[i] = C_BLACK; // No trail for mouse
                else if (c != C_BLACK) {
                    // Manual shift-down fading
                    uint16_t r = (c >> 11) & 0x1F; uint16_t g = (c >> 5) & 0x3F; uint16_t b = c & 0x1F;
                    if(r > 4) r -= 4; else r = 0; if(g > 4) g -= 4; else g = 0; if(b > 4) b -= 4; else b = 0;
                    screen_buf[i] = (r << 11) | (g << 5) | b;
                }
            }
            
            for(int x=0; x<160; x+=6) {
                if(drops[x] >= 0 && drops[x] < 80) {
                    screen_buf[drops[x] * 160 + x] = C_GREEN;
                    if(x+1 < 160) screen_buf[drops[x] * 160 + x + 1] = C_GREEN;
                }
                drops[x] += 1;
                if(drops[x] >= 80) drops[x] = random(-20, 0);
            }
            
            char info[32]; sprintf(info, "192.168.4.1 U:%d", clients);
            drawString(2, 2, info, C_WHITE, 1);
            unsigned long age = millis() - lastKeyTime;
            if (age < 2000 && lastKey.length() > 0) {
                uint16_t kc = (age > 1000) ? C_DARKGREEN : C_WHITE;
                int len = lastKey.length(), sc = (len > 3) ? 2 : 4;
                int tx = (160 - (len * 6 * sc)) / 2, ty = (80 - 8 * sc) / 2;
                drawString(tx, ty, lastKey.c_str(), kc, sc);
            }
        }
    }

    if (showCursorFrames > 0) {
        for(int y=0; y<4; y++) for(int x=0; x<4; x++) screen_buf[(cursorY + y)*160 + (cursorX + x)] = C_RED;
        showCursorFrames--;
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 160, 80, screen_buf);
}

void setup_lcd() {
    spi_bus_config_t b = ST7735_PANEL_BUS_SPI_CONFIG(PIN_NUM_CLK, PIN_NUM_MOSI, 160 * 80 * 2);
    spi_bus_initialize(SPI2_HOST, &b, SPI_DMA_CH_AUTO);
    esp_lcd_panel_io_spi_config_t ioc = ST7735_PANEL_IO_SPI_CONFIG(PIN_NUM_CS, PIN_NUM_DC, NULL, NULL);
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io_handle);
    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = PIN_NUM_RST, .color_space = ESP_LCD_COLOR_SPACE_BGR, .bits_per_pixel = 16 };
    esp_lcd_new_panel_st7735(io_handle, &pc, &panel_handle);
    esp_lcd_panel_reset(panel_handle); esp_lcd_panel_init(panel_handle); esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_set_gap(panel_handle, 1, 26); esp_lcd_panel_swap_xy(panel_handle, true); esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_disp_on_off(panel_handle, true); pinMode(PIN_NUM_BCKL, OUTPUT); digitalWrite(PIN_NUM_BCKL, 0);
}

void handleCaptiveRedirect(AsyncWebServerRequest *request) { request->redirect("http://192.168.4.1/"); }

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812, LED_DI_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB::Red; FastLED.show();
    WiFi.mode(WIFI_AP); WiFi.softAP(ssid);
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    Keyboard.begin(); Mouse.begin(); USB.begin(); setup_lcd();
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){ if(type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len); });
    server.addHandler(&ws);
    server.on("/generate_204", handleCaptiveRedirect);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.onNotFound(handleCaptiveRedirect);
    server.begin();
    leds[0] = CRGB::Blue; FastLED.show();
}

void loop() {
    dnsServer.processNextRequest(); ws.cleanupClients();
    static uint32_t last = 0; if(millis() - last > 50) { last = millis(); updateDisplay(); }
}
