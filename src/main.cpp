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

#define LED_DI_PIN     40
#define NUM_LEDS       1
#define PIN_NUM_MOSI   3
#define PIN_NUM_CLK    5
#define PIN_NUM_CS     4
#define PIN_NUM_DC     2
#define PIN_NUM_RST    1
#define PIN_NUM_BCKL   38

#define C_GREEN      0x07E0
#define C_DARKGREEN  0x03E0
#define C_DIM        0x01E0
#define C_RED        0xF800
#define C_WHITE      0xFFFF
#define C_BLACK      0x0000

inline uint16_t SWAP(uint16_t v) { return (v >> 8) | (v << 8); }

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

// GIF Engine (5 frames max)
static uint16_t* gif_frames[5] = {NULL, NULL, NULL, NULL, NULL};
static int gif_count = 0;
static int gif_idx = 0;
static unsigned long last_gif_ms = 0;
static bool gif_mode = false;

const char* ssid = "PwnDongle";
String targetOS = "win";
String lastKey = "";
unsigned long lastKeyTime = 0;
bool show_img = false;
bool user_on_site = false;

int cursorX = 80, cursorY = 40;
int showCursorFrames = 0;
unsigned long qrStartTime = 0;
bool qrActive = false;

void setLastKey(String k) { lastKey = k; lastKeyTime = millis(); }

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>PwnDongle v20</title>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<style>
body { background:#000; color:#0f0; font-family:monospace; margin:0; text-align:center; overflow:hidden; overscroll-behavior:none; }
.tabs { display:flex; border-bottom:1px solid #0f0; background:#0a0a0a; }
.tab { flex:1; padding:15px; cursor:pointer; font-weight:bold; border-right:1px solid #111; font-size:14px; }
.tab.active { background:#0f0; color:#000; }
.content { padding:10px; display:none; height:calc(100vh - 50px); overflow-y:auto; box-sizing:border-box; }
.content.active { display:block; }
button { background:#000; color:#0f0; border:1px solid #0f0; padding:15px; margin:5px; font-weight:bold; width:100%; font-size:16px; touch-action:manipulation; border-radius:4px; }
button:active { background:#0f0; color:#000; }
button.toggled { background:#0f0 !important; color:#000 !important; }
.row { display:flex; gap:10px; }
textarea { width:100%; height:80px; background:#111; color:#0f0; border:1px dashed #333; padding:10px; box-sizing:border-box; font-size:1.2em; outline:none; }
#pad { flex:1; background:#0a0a0a; border:1px solid #333; margin-top:10px; display:flex; align-items:center; justify-content:center; color:#444; touch-action:none; min-height:30vh; border-radius:8px; }
#crop-wrap { width:100%; border:1px solid #333; margin-top:10px; background:#050505; position:relative; overflow:hidden; touch-action:none; }
#crop-canvas { display:block; margin:0 auto; max-width:100%; }
.file-btn { position:relative; overflow:hidden; display:inline-block; width:100%; }
.file-btn input[type=file] { position:absolute; font-size:100px; right:0; top:0; opacity:0; cursor:pointer; }
.status { color:#888; font-size:12px; margin:5px; height:15px; }
.history { display:flex; gap:10px; overflow-x:auto; padding:10px 0; border-top:1px solid #222; margin-top:10px; }
.hist-item { width:60px; height:30px; border:1px solid #0f0; flex-shrink:0; cursor:pointer; background-size:cover; }
</style>
</head>
<body>
<div class="tabs"><div class="tab active" onclick="sT('kb',this)">KB</div><div class="tab" onclick="sT('ms',this)">MS</div><div class="tab" onclick="sT('ig',this)">IMG</div></div>
<div id="c-kb" class="content active">
    <div class="row"><button id="b-win" class="toggled" onclick="os='win';wsS('O:win');uOS()">WIN OS</button><button id="b-lin" onclick="os='lin';wsS('O:lin');uOS()">LINUX OS</button></div>
    <div class="row" style="margin-top:10px">
        <button id="m-win" onmousedown="mD('win',this)" onmouseup="mU('win',this)" ontouchstart="mD('win',this);event.preventDefault()" ontouchend="mU('win',this);event.preventDefault()">WIN</button>
        <button id="m-ctrl" onmousedown="mD('ctrl',this)" onmouseup="mU('ctrl',this)" ontouchstart="mD('ctrl',this);event.preventDefault()" ontouchend="mU('ctrl',this);event.preventDefault()">CTRL</button>
        <button id="m-alt" onmousedown="mD('alt',this)" onmouseup="mU('alt',this)" ontouchstart="mD('alt',this);event.preventDefault()" ontouchend="mU('alt',this);event.preventDefault()">ALT</button>
    </div>
    <textarea id="ta" placeholder="Type or Paste..."></textarea>
    <div class="row"><button onclick="wsS('A:term')">TERM</button><button onclick="wsS('A:calc')">CALC</button></div>
    <div id="win-tools" class="row"><button onclick="wsS('A:cad')" style="color:#f00;border-color:#f00">C-A-D</button><button onclick="wsS('A:lock')">LOCK</button></div>
    <div id="lin-tools" class="row" style="display:none;"><button onclick="wsS('A:reisub')" style="color:#f00;border-color:#f00">REBOOT</button><button onclick="wsS('A:reisuo')" style="color:#f00;border-color:#f00">OFF</button></div>
    <button onclick="wsS('A:rick')">RICKROLL</button>
</div>
<div id="c-ms" class="content"><button id="b-air" onclick="tAir()">GYRO: OFF</button><div id="pad">TRACKPAD</div></div>
<div id="c-ig" class="content">
    <div class="file-btn"><button>SELECT IMG / GIF</button><input type="file" id="img-f" accept="image/*"></div>
    <div id="status" class="status"></div>
    <div id="crop-wrap"><canvas id="crop-canvas" width="160" height="80"></canvas></div>
    <div id="ig-controls" style="display:none;margin-top:10px">
        <div class="row"><button onclick="z(-0.02)">- ZOOM</button><button onclick="z(0.02)">+ ZOOM</button><button onclick="rot()">ROT</button></div>
        <button id="b-up" onclick="uploadImg()">UPLOAD IMAGE</button>
        <button id="b-gif" onclick="uploadGif()" style="display:none;border-color:#f0f;color:#f0f">UPLOAD AS GIF</button>
    </div>
    <div class="history" id="ig-hist"></div>
    <button onclick="wsS('I:clear')" style="margin-top:20px;border-color:#444;color:#666">CLEAR</button>
</div>
<script>
let ws=new WebSocket('ws://'+location.host+'/ws');
let os='win', air=false, history=[], isGif=false;
function wsS(m){if(ws.readyState===1)ws.send(m);}
function sT(t,el){
document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
document.querySelectorAll('.content').forEach(x=>x.classList.remove('active'));
el.classList.add('active'); document.getElementById('c-'+t).classList.add('active');
}
function uOS(){
document.getElementById('b-win').className=(os=='win'?'toggled':'');
document.getElementById('b-lin').className=(os=='lin'?'toggled':'');
document.getElementById('win-tools').style.display=(os=='win'?'flex':'none');
document.getElementById('lin-tools').style.display=(os=='lin'?'flex':'none');
}
function mD(m,el){ el.dataset.t=Date.now(); el.dataset.h=setTimeout(()=>{ el.dataset.h=0; el.classList.toggle('toggled'); wsS('H:'+m+','+(el.classList.contains('toggled')?'1':'0')); },400); }
function mU(m,el){ if(el.dataset.h){ clearTimeout(el.dataset.h); el.dataset.h=0; wsS('P:'+m); } }
let ta=document.getElementById('ta');
ta.oninput=e=>{ if(e.inputType==='insertFromPaste'||ta.value.length>1){wsS('V:'+ta.value);ta.value='';}else{let c=ta.value.slice(-1);ta.value='';if(c)wsS('K:'+c);} };
ta.onkeydown=e=>{ if(e.key==='Enter'){e.preventDefault();wsS('E:1');} if(e.key==='Backspace'){e.preventDefault();wsS('B:1');} if(e.key==='Tab'){e.preventDefault();wsS('T:1');} };
// Trackpad
let p=document.getElementById('pad'),lX=0,lY=0,isD=false,moved=false,tapT=0,fingers=0;
p.onmousedown=e=>{isD=true;moved=false;lX=e.clientX;lY=e.clientY;tapT=Date.now();fingers=1;};
document.onmouseup=()=>{if(isD&&!moved&&Date.now()-tapT<300)wsS('C:l');isD=false;};
p.onmousemove=e=>{if(isD){let dx=e.clientX-lX,dy=e.clientY-lY;if(Math.abs(dx)>1||Math.abs(dy)>1)moved=true;wsS('M:'+dx+','+dy);lX=e.clientX;lY=e.clientY;}};
p.ontouchstart=e=>{isD=true;moved=false;fingers=e.touches.length;lX=e.touches[0].clientX;lY=e.touches[0].clientY;tapT=Date.now();e.preventDefault();};
p.ontouchend=e=>{if(isD&&!moved&&Date.now()-tapT<300)wsS('C:'+(fingers>1?'r':'l'));isD=false;e.preventDefault();};
p.ontouchmove=e=>{if(isD){let dx=e.touches[0].clientX-lX,dy=e.touches[0].clientY-lY;if(Math.abs(dx)>1||Math.abs(dy)>1)moved=true;wsS('M:'+Math.round(dx)+','+Math.round(dy));lX=e.touches[0].clientX;lY=e.touches[0].clientY;}e.preventDefault();};
// Image logic
let img=new Image(),scale=1,rotation=0,oX=0,oY=0,cvs=document.getElementById('crop-canvas'),ctx=cvs.getContext('2d'),pD=0;
document.getElementById('img-f').onchange=e=>{
    let f=e.target.files[0]; if(!f)return;
    isGif = (f.type === 'image/gif');
    let r=new FileReader(); r.onload=ev=>{
        img.onload=()=>{
            document.getElementById('ig-controls').style.display='block';
            document.getElementById('b-gif').style.display=isGif?'block':'none';
            scale=Math.max(160/img.width,80/img.height);oX=0;oY=0;rotation=0;drw();
        }; img.src=ev.target.result;
    }; r.readAsDataURL(f);
};
function drw(){
    ctx.fillStyle='#000';ctx.fillRect(0,0,160,80);
    ctx.save();ctx.translate(80+oX,40+oY);ctx.rotate(rotation*Math.PI/180);
    ctx.drawImage(img,-img.width*scale/2,-img.height*scale/2,img.width*scale,img.height*scale);ctx.restore();
}
function z(v){scale+=v;drw();} function rot(){rotation=(rotation+90)%360;drw();}
cvs.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;};
cvs.onmousemove=e=>{if(isD){oX+=e.clientX-lX;oY+=e.clientY-lY;lX=e.clientX;lY=e.clientY;drw();}};
cvs.ontouchstart=e=>{
    if(e.touches.length===2)pD=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);
    else{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;}
};
cvs.ontouchmove=e=>{
    if(e.touches.length===2){
        let d=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);
        scale*=(d/pD);pD=d;drw();
    }else if(isD){oX+=e.touches[0].clientX-lX;oY+=e.touches[0].clientY-lY;lX=e.touches[0].clientX;lY=e.touches[0].clientY;drw();}
    e.preventDefault();
};
function getB(){
    let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600);
    for(let j=0;j<12800;j++){
        let r=d[j*4],g=d[j*4+1],bl=d[j*4+2];
        let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3);
        b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF;
    }
    return b;
}
function uploadImg(){
    document.getElementById('status').innerText='Sending Image...';
    let b=getB(); ws.send(b);
    let thumb=cvs.toDataURL('image/jpeg',0.5);
    if(!history.find(x=>x.src==thumb)){history.unshift({src:thumb,data:b});if(history.length>10)history.pop();uHist();}
    setTimeout(()=>document.getElementById('status').innerText='Success!',1000);
}
function uploadGif(){
    document.getElementById('status').innerText='Capturing Animation...';
    wsS('I:gif'); // Start GIF mode
    let frames = 0;
    let iv = setInterval(()=>{
        drw(); ws.send(getB()); frames++;
        document.getElementById('status').innerText='Capturing: '+(frames*20)+'%';
        if(frames >= 5){ clearInterval(iv); document.getElementById('status').innerText='GIF Looping!'; }
    },200);
}
function uHist(){
    let h=document.getElementById('ig-hist'); h.innerHTML='';
    history.forEach(x=>{
        let i=document.createElement('div'); i.className='hist-item'; i.style.backgroundImage=`url(${x.src})`;
        i.onclick=()=>{ ws.send(x.data); document.getElementById('status').innerText='Restored!'; };
        h.appendChild(i);
    });
}
function tAir(){if(!air&&typeof DeviceOrientationEvent!=='undefined'&&typeof DeviceOrientationEvent.requestPermission==='function'){DeviceOrientationEvent.requestPermission().then(r=>{if(r==='granted')togA();}).catch(console.error);}else togA();}
function togA(){air=!air;let b=document.getElementById('b-air');b.innerText='GYRO: '+(air?'ON':'OFF');b.className=air?'toggled':'';}
window.ondeviceorientation=e=>{if(air){let x=Math.round((e.gamma||0)/2),y=Math.round(((e.beta||0)-45)/2);if(x||y)wsS('M:'+x+','+y);}};
ws.onopen=()=>wsS('U:1');
</script>
</body>
</html>
)rawliteral";

void runMacro(String c) {
    Keyboard.releaseAll();
    if(c=="term") {
        if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("cmd"); }
        else { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('t'); delay(300); Keyboard.releaseAll(); }
    } else if(c=="calc") {
        if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("calc"); }
        else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.print("gnome-calculator"); delay(50); Keyboard.write(KEY_RETURN); }
    } else if(c=="cad") { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_DELETE); delay(100); Keyboard.releaseAll(); }
    else if(c=="lock") {
        if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('l'); delay(100); Keyboard.releaseAll(); }
        else { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('l'); delay(100); Keyboard.releaseAll(); }
    } else if(c=="reisub" || c=="reisuo") {
        Keyboard.press(KEY_LEFT_ALT); Keyboard.press(70);
        const char* seq = (c=="reisub")?"reisub":"reisuo";
        for(int i=0;seq[i];i++){delay(200); Keyboard.press(seq[i]); delay(100); Keyboard.release(seq[i]);}
        delay(100); Keyboard.releaseAll();
    } else if(c=="rick") {
        if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("https://www.youtube.com/watch?v=dQw4w9WgXcQ"); }
        else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.print("xdg-open 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'"); delay(50); Keyboard.write(KEY_RETURN); }
    }
}

static uint32_t binaryOffset = 0;
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len) {
        data[len] = 0; String msg = (char*)data;
        if (msg.startsWith("K:")) { String k = msg.substring(2); Keyboard.print(k); setLastKey(k); }
        else if (msg.startsWith("V:")) { String v = msg.substring(2); Keyboard.print(v); setLastKey("PASTE"); }
        else if (msg.startsWith("E:")) { Keyboard.write(KEY_RETURN); setLastKey("ENT"); }
        else if (msg.startsWith("B:")) { Keyboard.press(KEY_BACKSPACE); delay(10); Keyboard.release(KEY_BACKSPACE); setLastKey("DEL"); }
        else if (msg.startsWith("T:")) { Keyboard.write(KEY_TAB); setLastKey("TAB"); }
        else if (msg.startsWith("M:")) {
            int comma = msg.indexOf(',');
            if (comma > 0) {
                int x = msg.substring(2, comma).toInt(); int y = msg.substring(comma+1).toInt();
                Mouse.move(x, y); cursorX += x; cursorY += y;
                if(cursorX < 0) cursorX = 0; if(cursorX > 156) cursorX = 156;
                if(cursorY < 0) cursorY = 0; if(cursorY > 76) cursorY = 76;
                showCursorFrames = 10;
            }
        } else if (msg.startsWith("D:") || msg.startsWith("C:")) {
            char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT;
            if (msg.startsWith("C:")) Mouse.click(btn); else Mouse.press(btn);
        } else if (msg.startsWith("U:")) {
            char b = msg.charAt(2);
            if(b == '1') { user_on_site = true; qrActive = false; }
            else { uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT; Mouse.release(btn); }
        } else if (msg.startsWith("H:")) {
            int comma = msg.indexOf(','); String mod = msg.substring(2, comma); bool st = msg.substring(comma+1)=="1";
            uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
            if(st) Keyboard.press(k); else { Keyboard.release(k); delay(100); Keyboard.write(0); }
        } else if (msg.startsWith("P:")) {
            String mod = msg.substring(2); uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
            Keyboard.press(k); delay(200); Keyboard.release(k); setLastKey(mod);
        } else if (msg.startsWith("A:")) { String act = msg.substring(2); runMacro(act); setLastKey(act); }
        else if (msg.startsWith("O:")) { targetOS = msg.substring(2); }
        else if (msg.startsWith("I:clear")) { show_img = false; gif_mode = false; }
        else if (msg.startsWith("I:gif")) { gif_mode = true; gif_count = 0; gif_idx = 0; show_img = false; }
    } else if (info->opcode == WS_BINARY) {
        if (info->index == 0) binaryOffset = 0;
        if (binaryOffset + len <= 25600) {
            memcpy(((uint8_t*)custom_img_buf) + binaryOffset, data, len);
            binaryOffset += len;
            if (binaryOffset == 25600) {
                if(gif_mode && gif_count < 5) {
                    if(!gif_frames[gif_count]) gif_frames[gif_count] = (uint16_t*)malloc(25600);
                    if(gif_frames[gif_count]) memcpy(gif_frames[gif_count], custom_img_buf, 25600);
                    gif_count++;
                }
                show_img = true;
            }
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
                        if (px >= 0 && px < 160 && py >= 0 && py < 80) screen_buf[py * 160 + px] = SWAP(color);
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
        if(gif_mode && gif_count > 0) {
            if(millis() - last_gif_ms > 200) {
                last_gif_ms = millis();
                memcpy(screen_buf, gif_frames[gif_idx], 25600);
                gif_idx = (gif_idx + 1) % gif_count;
            }
        } else {
            memcpy(screen_buf, custom_img_buf, 25600);
        }
    } else {
        int clients = WiFi.softAPgetStationNum();
        if (clients > 0 && !user_on_site && !qrActive) { qrActive = true; qrStartTime = millis(); }
        if (clients == 0) { user_on_site = false; qrActive = false; }
        if (qrActive) {
            for(int i=0; i<160*80; i++) screen_buf[i] = SWAP(C_BLACK);
            int sc = 3; int ox = (160 - qr_size * sc) / 2; int oy = (80 - qr_size * sc) / 2;
            for(int y=0; y<qr_size; y++) {
                for(int x=0; x<qr_size; x++) {
                    uint16_t c = qr_data[y * qr_size + x] ? C_BLACK : C_WHITE;
                    for(int sy=0; sy<sc; sy++) for(int sx=0; sx<sc; sx++) screen_buf[(oy + y*sc + sy)*160 + (ox + x*sc + sx)] = SWAP(c);
                }
            }
        } else {
            static int drops[160]; static bool init = false;
            if (!init) { for(int i=0; i<160; i++) drops[i] = random(-100, 0); init = true; }
            for(int i=0; i<160*80; i++) {
                uint16_t c = screen_buf[i];
                if (c == SWAP(C_RED)) screen_buf[i] = SWAP(C_BLACK);
                else if (c != SWAP(C_BLACK)) {
                    uint16_t ns = SWAP(c);
                    uint16_t r = (ns >> 11) & 0x1F; uint16_t g = (ns >> 5) & 0x3F; uint16_t b = ns & 0x1F;
                    if(g > 2) g -= 2; else g = 0; if(r > 4) r -= 4; else r = 0; if(b > 4) b -= 4; else b = 0;
                    screen_buf[i] = SWAP((r << 11) | (g << 5) | b);
                }
            }
            for(int x=0; x<160; x+=6) {
                if(drops[x] >= 0 && drops[x] < 80) {
                    screen_buf[drops[x] * 160 + x] = SWAP(C_GREEN);
                    if(x+1 < 160) screen_buf[drops[x] * 160 + x + 1] = SWAP(C_GREEN);
                }
                drops[x] += 1; if(drops[x] >= 80) drops[x] = random(-40, 0);
            }
            char info[32]; sprintf(info, "192.168.4.1 U:%d", clients);
            drawString(2, 2, info, C_WHITE, 1);
            unsigned long age = millis() - lastKeyTime;
            if (age < 2000 && lastKey.length() > 0) {
                int len = lastKey.length(), sc = (len > 3) ? 2 : 4;
                int tx = (160 - (len * 6 * sc)) / 2, ty = (80 - 8 * sc) / 2;
                drawString(tx, ty, lastKey.c_str(), C_GREEN, sc);
            }
        }
    }
    if (showCursorFrames > 0) {
        for(int y=0; y<4; y++) for(int x=0; x<4; x++) screen_buf[(cursorY + y)*160 + (cursorX + x)] = SWAP(C_RED);
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

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<WS2812, LED_DI_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB::Red; FastLED.show();
    WiFi.mode(WIFI_AP); WiFi.softAP(ssid);
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    Keyboard.begin(); Mouse.begin(); USB.begin(); setup_lcd();
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){ if(type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len); });
    server.addHandler(&ws);
    server.on("/generate_204", [](AsyncWebServerRequest *request){ request->redirect("http://192.168.4.1/"); });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("http://192.168.4.1/"); });
    server.begin();
    leds[0] = CRGB::Blue; FastLED.show();
}

void loop() {
    dnsServer.processNextRequest(); ws.cleanupClients();
    static uint32_t last = 0; if(millis() - last > 50) { last = millis(); updateDisplay(); }
}
