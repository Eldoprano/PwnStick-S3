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
#define C_RED        0xF800
#define C_BLACK      0x0000
#define C_WHITE      0xFFFF

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

// Dynamic GIF Storage
static uint16_t* gif_storage[15]; // Support up to 15 frames if RAM allows
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

void setLastKey(String k) { lastKey = k; lastKeyTime = millis(); }

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>PwnDongle v40</title>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<style>
body { background:#000; color:#0f0; font-family:monospace; margin:0; text-align:center; overflow:hidden; overscroll-behavior:none; }
.tabs { display:flex; border-bottom:1px solid #0f0; background:#0a0a0a; }
.tab { flex:1; padding:15px; cursor:pointer; font-weight:bold; border-right:1px solid #111; }
.tab.active { background:#0f0; color:#000; }
.content { padding:10px; display:none; height:calc(100vh - 50px); overflow-y:auto; box-sizing:border-box; }
.content.active { display:block; }
button { background:#000; color:#0f0; border:1px solid #0f0; padding:15px; margin:5px; font-weight:bold; width:100%; font-size:16px; border-radius:4px; transition:0.2s; }
button:active { background:#0f0; color:#000; }
button.toggled { background:#0f0 !important; color:#000 !important; box-shadow:0 0 15px #0f0; }
.row { display:flex; gap:10px; }
textarea { width:100%; height:80px; background:#111; color:#0f0; border:1px dashed #333; padding:10px; box-sizing:border-box; font-size:1.2em; outline:none; }
#pad { flex:1; background:#0a0a0a; border:1px solid #333; margin-top:10px; display:flex; align-items:center; justify-content:center; color:#444; height:35vh; border-radius:8px; font-weight:bold; }
#crop-wrap { width:100%; border:1px solid #333; margin-top:10px; background:#050505; position:relative; overflow:hidden; touch-action:none; }
#crop-canvas { display:block; margin:0 auto; max-width:100%; background:#111; }
.file-btn { position:relative; overflow:hidden; display:inline-block; width:100%; }
.file-btn input[type=file] { position:absolute; font-size:100px; right:0; top:0; opacity:0; cursor:pointer; }
.status { color:#888; font-size:12px; margin:5px; height:15px; }
.opt-box { background:#111; border:1px solid #333; padding:10px; margin-top:10px; display:none; flex-direction:column; gap:5px; text-align:left; font-size:12px; }
.opt-row { display:flex; justify-content:space-between; align-items:center; }
input[type=number] { background:#000; color:#0f0; border:1px solid #0f0; width:50px; padding:5px; font-family:monospace; }
</style>
</head>
<body>
<div class="tabs"><div class="tab active" onclick="sT('kb',this)">KB</div><div class="tab" onclick="sT('ms',this)">MS</div><div class="tab" onclick="sT('ig',this)">IMG</div></div>
<div id="c-kb" class="content active">
    <div class="row"><button id="b-win-os" class="toggled" onclick="os='win';wsS('O:win');uOS()">WIN OS</button><button id="b-lin-os" onclick="os='lin';wsS('O:lin');uOS()">LINUX OS</button></div>
    <div class="row" style="margin-top:10px">
        <button id="mod-win" onmousedown="mD('win',this)" onmouseup="mU('win',this)" ontouchstart="mD('win',this);event.preventDefault()" ontouchend="mU('win',this);event.preventDefault()">WIN</button>
        <button id="mod-ctrl" onmousedown="mD('ctrl',this)" onmouseup="mU('ctrl',this)" ontouchstart="mD('ctrl',this);event.preventDefault()" ontouchend="mU('ctrl',this);event.preventDefault()">CTRL</button>
        <button id="mod-alt" onmousedown="mD('alt',this)" onmouseup="mU('alt',this)" ontouchstart="mD('alt',this);event.preventDefault()" ontouchend="mU('alt',this);event.preventDefault()">ALT</button>
    </div>
    <textarea id="ta" placeholder="Type or Paste..."></textarea>
    <div class="row"><button onclick="wsS('A:term')">TERM</button><button onclick="wsS('A:calc')">CALC</button></div>
    <button onclick="wsS('A:rick')">RICKROLL</button>
</div>
<div id="c-ms" class="content"><div id="pad">TRACKPAD</div></div>
<div id="c-ig" class="content">
    <div class="file-btn"><button id="b-sel">SELECT IMG</button><input type="file" id="img-f" accept="image/*"></div>
    <div id="status" class="status"></div>
    <div id="crop-wrap"><canvas id="crop-canvas" width="160" height="80"></canvas></div>
    <div id="gif-opts" class="opt-box">
        <div class="opt-row"><span>Capture Frames:</span><input type="number" id="g-cnt" value="5" min="1" max="15"></div>
        <div class="opt-row"><span>Skip Steps:</span><input type="number" id="g-skp" value="1" min="0" max="10"></div>
    </div>
    <div id="ig-controls" style="display:none;margin-top:10px">
        <div class="row"><button onclick="z(-0.02)">- ZOOM</button><button onclick="z(0.02)">+ ZOOM</button><button onclick="rot()">ROT</button></div>
        <button id="b-up" onclick="upl()">UPLOAD</button>
    </div>
    <button onclick="wsS('I:clear')" style="margin-top:20px;border-color:#444;color:#666">CLEAR</button>
</div>
<script>
let ws=new WebSocket('ws://'+location.host+'/ws');
let os='win', history=[], isGif=false, gifBytes=null;
function wsS(m){if(ws.readyState===1)ws.send(m);}
function sT(t,el){
document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
document.querySelectorAll('.content').forEach(x=>x.classList.remove('active'));
el.classList.add('active'); document.getElementById('c-'+t).classList.add('active');
}
function uOS(){
document.getElementById('b-win-os').className=(os=='win'?'toggled':'');
document.getElementById('b-lin-os').className=(os=='lin'?'toggled':'');
}
function mD(m,el){
    el.dataset.t=Date.now();
    el.dataset.h=setTimeout(()=>{
        el.dataset.h=0; el.classList.toggle('toggled');
        wsS('H:'+m+','+(el.classList.contains('toggled')?'1':'0'));
    },500);
}
function mU(m,el){
    if(el.dataset.h){
        clearTimeout(el.dataset.h); el.dataset.h=0;
        wsS('P:'+m); // Just a quick tap
    }
}
let ta=document.getElementById('ta');
ta.oninput=e=>{ if(e.inputType==='insertFromPaste'||ta.value.length>1){wsS('V:'+ta.value);ta.value='';}else{let c=ta.value.slice(-1);ta.value='';if(c)wsS('K:'+c);} };
ta.onkeydown=e=>{ if(e.key==='Enter'){e.preventDefault();wsS('E:1');} if(e.key==='Backspace'){e.preventDefault();wsS('B:1');} if(e.key==='Tab'){e.preventDefault();wsS('T:1');} };
// Trackpad
let p=document.getElementById('pad'),lX=0,lY=0,isD=false,tapT=0;
p.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;tapT=Date.now();};
document.onmouseup=()=>{if(isD&&Date.now()-tapT<200)wsS('C:l');isD=false;};
p.onmousemove=e=>{if(isD){wsS('M:'+(e.clientX-lX)+','+(e.clientY-lY));lX=e.clientX;lY=e.clientY;}};
p.ontouchstart=e=>{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;tapT=Date.now();};
p.ontouchend=e=>{if(isD&&Date.now()-tapT<200)wsS('C:'+(e.touches.length>0?'r':'l'));isD=false;};
p.ontouchmove=e=>{if(isD){wsS('M:'+Math.round(e.touches[0].clientX-lX)+','+Math.round(e.touches[0].clientY-lY));lX=e.touches[0].clientX;lY=e.touches[0].clientY;}};
// Image logic
let scale=1,rotation=0,oX=0,oY=0,cvs=document.getElementById('crop-canvas'),ctx=cvs.getContext('2d'),curImg=new Image(),pD=0;
document.getElementById('img-f').onchange=e=>{
    let f=e.target.files[0]; if(!f)return;
    isGif=(f.type==='image/gif');
    let r=new FileReader(); r.onload=ev=>{
        gifBytes=new Uint8Array(ev.target.result);
        curImg.onload=()=>{
            document.getElementById('ig-controls').style.display='block';
            document.getElementById('gif-opts').style.display=isGif?'flex':'none';
            scale=Math.max(160/curImg.width,80/curImg.height);oX=0;oY=0;rotation=0;drw(true);
        }; curImg.src=URL.createObjectURL(new Blob([gifBytes]));
    }; r.readAsArrayBuffer(f);
};
function drw(clr){
    if(clr){ ctx.fillStyle='#000'; ctx.fillRect(0,0,160,80); }
    ctx.save(); ctx.translate(160/2+oX,80/2+oY); ctx.rotate(rotation*Math.PI/180);
    ctx.drawImage(curImg,-curImg.width*scale/2,-curImg.height*scale/2,curImg.width*scale,curImg.height*scale);
    ctx.restore();
}
function z(v){scale+=v;drw(true);} function rot(){rotation=(rotation+90)%360;drw(true);}
cvs.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;};
cvs.onmousemove=e=>{if(isD){oX+=e.clientX-lX;oY+=e.clientY-lY;lX=e.clientX;lY=e.clientY;drw(true);}};
cvs.ontouchstart=e=>{if(e.touches.length===2)pD=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);else{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;}};
cvs.ontouchmove=e=>{if(e.touches.length===2){let d=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);scale*=(d/pD);pD=d;drw(true);}else if(isD){oX+=e.touches[0].clientX-lX;oY+=e.touches[0].clientY-lY;lX=e.touches[0].clientX;lY=e.touches[0].clientY;drw(true);}e.preventDefault();};
function getB(){
    let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600);
    for(let j=0;j<12800;j++){
        let r=d[j*4],g=d[j*4+1],bl=d[j*4+2];
        let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3);
        b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF;
    }
    return b;
}
async function upl(){
    let btn=document.getElementById('b-up'); btn.disabled=true;
    try{
        if(isGif){
            wsS('I:gif');
            let hasGCT=(gifBytes[10]&0x80), gctSize=hasGCT?3*Math.pow(2,(gifBytes[10]&7)+1):0;
            let header=gifBytes.slice(0,13+gctSize);
            let frames=[], pos=13+gctSize, curF=[];
            while(pos<gifBytes.length && gifBytes[pos]!==0x3B && frames.length<50){
                let b = gifBytes[pos];
                if(b===0x21){ let st=pos; pos+=2; while(pos<gifBytes.length && gifBytes[pos]!==0) pos += gifBytes[pos]+1; pos++; if(gifBytes[st+1]===0xF9) curF.push(gifBytes.slice(st,pos)); }
                else if(b===0x2C){
                    let st=pos; pos+=10; if(gifBytes[pos-1]&0x80) pos+=3*Math.pow(2,(gifBytes[pos-1]&7)+1); pos++;
                    while(pos<gifBytes.length && gifBytes[pos]!==0) pos += gifBytes[pos]+1; pos++;
                    curF.push(gifBytes.slice(st,pos));
                    let len=header.length+1; for(let c of curF) len+=c.length;
                    let f=new Uint8Array(len); f.set(header,0); let off=header.length;
                    for(let c of curF){ f.set(c,off); off+=c.length; } f[off]=0x3B;
                    frames.push(URL.createObjectURL(new Blob([f],{type:'image/gif'}))); curF=[];
                } else pos++;
            }
            let maxFrames = parseInt(document.getElementById('g-cnt').value)||5;
            let skip = parseInt(document.getElementById('g-skp').value)||0;
            let filtered = [];
            for(let i=0; i<frames.length && filtered.length<maxFrames; i+=(skip+1)) filtered.push(frames[i]);
            ctx.fillStyle='#000'; ctx.fillRect(0,0,160,80);
            for(let i=0; i<filtered.length; i++){
                await new Promise((res)=>{
                    curImg.onload=()=>{ drw(false); ws.send(getB()); res(); };
                    curImg.src=filtered[i];
                });
                await new Promise(r=>setTimeout(r,400));
                document.getElementById('status').innerText='Uploading: '+(i+1)+'/'+filtered.length;
            }
            document.getElementById('status').innerText='Animated!';
            curImg.src=URL.createObjectURL(new Blob([gifBytes]));
        }else{
            wsS('I:img'); ws.send(getB()); document.getElementById('status').innerText='Success!';
        }
    }catch(e){console.error(e);}
    btn.disabled=false;
}
ws.onopen=()=>wsS('U:1');
</script>
</body>
</html>
)rawliteral";

static uint32_t binaryOffset = 0;

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

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len) {
        data[len] = 0; String msg = (char*)data;
        if (msg.startsWith("K:")) { Keyboard.print(msg.substring(2)); setLastKey(msg.substring(2)); }
        else if (msg.startsWith("V:")) { Keyboard.print(msg.substring(2)); setLastKey("PASTE"); }
        else if (msg.startsWith("E:")) { Keyboard.write(KEY_RETURN); setLastKey("ENT"); }
        else if (msg.startsWith("B:")) { Keyboard.write(KEY_BACKSPACE); setLastKey("DEL"); }
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
            if(msg.charAt(2) == '1') { user_on_site = true; }
            else { char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT; Mouse.release(btn); }
        } else if (msg.startsWith("H:")) {
            int comma = msg.indexOf(','); String mod = msg.substring(2, comma); bool st = msg.substring(comma+1)=="1";
            uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
            if(st) Keyboard.press(k); else { Keyboard.release(k); delay(10); Keyboard.write(0); }
        } else if (msg.startsWith("P:")) {
            String mod = msg.substring(2); uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:KEY_LEFT_ALT;
            Keyboard.press(k); delay(100); Keyboard.release(k); setLastKey(mod);
        } else if (msg.startsWith("A:")) { 
            String act = msg.substring(2); Keyboard.releaseAll();
            if(act=="term") { if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("cmd"); } else { Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('t'); delay(300); Keyboard.releaseAll(); } }
            else if(act=="calc") { if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("calc"); } else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(500); Keyboard.releaseAll(); delay(1000); Keyboard.print("gnome-calculator"); delay(100); Keyboard.write(KEY_RETURN); } }
            else if(act=="rick") { if(targetOS=="win") { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.println("https://www.youtube.com/watch?v=dQw4w9WgXcQ"); } else { Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_F2); delay(300); Keyboard.releaseAll(); delay(800); Keyboard.print("xdg-open 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'"); delay(50); Keyboard.write(KEY_RETURN); } }
            setLastKey(act); 
        }
        else if (msg.startsWith("O:")) { targetOS = msg.substring(2); }
        else if (msg.startsWith("I:clear")) { show_img = false; gif_mode = false; gif_count = 0; }
        else if (msg.startsWith("I:gif")) { gif_mode = true; gif_count = 0; gif_idx = 0; show_img = false; }
        else if (msg.startsWith("I:img")) { gif_mode = false; gif_count = 0; show_img = true; }
    } else if (info->opcode == WS_BINARY) {
        if (info->index == 0) binaryOffset = 0;
        if (binaryOffset + len <= 25600) {
            memcpy(((uint8_t*)custom_img_buf) + binaryOffset, data, len);
            binaryOffset += len;
            if (binaryOffset == 25600) {
                if(gif_mode && gif_count < 15) {
                    if(gif_storage[gif_count]) free(gif_storage[gif_count]);
                    gif_storage[gif_count] = (uint16_t*)malloc(25600);
                    if(gif_storage[gif_count]) memcpy(gif_storage[gif_count], custom_img_buf, 25600);
                    gif_count++;
                }
                if (!gif_mode) show_img = true;
                else if (gif_count > 1) show_img = true;
            }
        }
    }
}

void updateDisplay() {
    if (show_img) {
        if(gif_mode && gif_count > 1) {
            if(millis() - last_gif_ms > 150) {
                last_gif_ms = millis();
                memcpy(screen_buf, gif_storage[gif_idx], 25600);
                gif_idx = (gif_idx + 1) % gif_count;
            }
        } else {
            memcpy(screen_buf, custom_img_buf, 25600);
        }
    } else {
        int clients = WiFi.softAPgetStationNum();
        if (clients > 0 && ws.count() == 0) {
            for(int i=0; i<160*80; i++) screen_buf[i] = SWAP(C_BLACK);
            int sc = 3; int ox = (160 - qr_size * sc) / 2; int oy = (80 - qr_size * sc) / 2;
            for(int y=0; y<qr_size; y++) {
                for(int x=0; x<qr_size; x++) {
                    uint16_t c = qr_data[y * qr_size + x] ? C_BLACK : C_WHITE;
                    for(int sy=0; sy<sc; sy++) for(int sx=0; sx<sc; sx++) screen_buf[(oy + y*sc + sy)*160 + (ox + x*sc + sx)] = SWAP(c);
                }
            }
        } else {
            if (ws.count() > 0) user_on_site = true; else user_on_site = false;
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
            if (millis() - lastKeyTime < 2000 && lastKey.length() > 0) {
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
    for(int i=0; i<15; i++) gif_storage[i] = NULL;
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
        if(type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len);
        else if(type == WS_EVT_DISCONNECT) { user_on_site = false; }
    });
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
