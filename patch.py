import re

with open('src/main.cpp', 'r') as f:
    code = f.read()

# HTML updates
# 1. Settings tab
code = code.replace('<div class="tab" onclick="sT(\'ig\',this)">IMAGE Beamer</div></div>',
'<div class="tab" onclick="sT(\'ig\',this)">IMAGE Beamer</div><div class="tab" style="flex:0.2; display:flex; align-items:center; justify-content:center;" onclick="sT(\'set\',this)">⚙️</div></div>')

# 2. Control Center button cleanup
code = code.replace('<button id="tgl-ble" onclick="wsS(\'T:\'+(this.classList.contains(\'toggled\')?\'usb\':\'ble\')); this.classList.toggle(\'toggled\');" style="background:#005;border-color:#00f;color:#fff;flex:1.5">BLE Mode</button>\n        <button onclick="oM()" style="background:#050;border-color:#0f0;color:#fff;flex:1.5">MACROS</button>',
'<button onclick="oM()" style="background:#050;border-color:#0f0;color:#fff;flex:3">MACROS</button>')

# 3. Settings content pane
settings_pane = '''</div>
<div id="c-set" class="content">
    <div style="display:flex; align-items:center; justify-content:space-between; padding:10px; border-bottom:1px solid #333; flex-shrink:0;">
        <span>BLE Mode</span>
        <button id="tgl-ble" onclick="wsS('T:'+(this.classList.contains('toggled')?'usb':'ble')); this.classList.toggle('toggled');" style="flex:none; width:80px; height:35px; background:#005; border-color:#00f; color:#fff;">OFF</button>
    </div>
    <div style="padding:10px; flex:1; overflow-y:auto;">
        <div style="display:flex; justify-content:space-between; margin-bottom:10px;">
            <span style="font-weight:bold;">Bonded Devices</span>
            <button onclick="wsS('B:list')" style="flex:none; width:60px; height:25px; padding:0; font-size:10px; background:#222; border-color:#555; color:#fff;">Refresh</button>
        </div>
        <div id="ble-list" style="display:flex; flex-direction:column; gap:5px;"></div>
    </div>
</div>'''

code = code.replace('</div>\n<script>', settings_pane + '\n<script>')

# 4. Trackpad layout fix for scroll
code = code.replace('<div id="pad">TRACKPAD</div>',
'<div style="display:flex; flex:1; height:100%;"><div id="pad" style="flex:1; background:#0a0a0a; border:1px solid #333; display:flex; align-items:center; justify-content:center; color:#222; font-weight:bold; font-size:20px; touch-action:none;">TRACKPAD</div><div id="scroll" style="width:40px; background:#111; border:1px solid #333; border-left:none; display:flex; align-items:center; justify-content:center; color:#555; font-size:20px; touch-action:none; cursor:ns-resize;">↕</div></div>')

code = code.replace('#pad { flex:1; background:#0a0a0a; border:1px solid #333; display:flex; align-items:center; justify-content:center; color:#222; border-radius:8px 8px 0 0; font-weight:bold; font-size:20px; touch-action:none; }',
'') # removed old #pad CSS since inline now

# 5. JS Updates
js_injection = '''
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

let scrl = document.getElementById('scroll'), sY=0, isS=false;
scrl.onmousedown=e=>{ isS=true; sY=e.clientY; };
scrl.ontouchstart=e=>{ isS=true; sY=e.touches[0].clientY; };
window.addEventListener('mouseup', ()=>{ isS=false; });
window.addEventListener('touchend', ()=>{ isS=false; });
window.addEventListener('mousemove', e=>{
    if(isS) { let dy=e.clientY-sY; if(Math.abs(dy)>5){ wsS('W:'+(dy>0?-1:1)); sY=e.clientY; } }
});
window.addEventListener('touchmove', e=>{
    if(isS) { let dy=e.touches[0].clientY-sY; if(Math.abs(dy)>5){ wsS('W:'+(dy>0?-1:1)); sY=e.touches[0].clientY; } }
});
'''

code = code.replace('let p=document.getElementById(\'pad\'),lX=0,lY=0,isD=false,tapT=0,maxT=0;',
js_injection + '\nlet p=document.getElementById(\'pad\'),lX=0,lY=0,isD=false,tapT=0,maxT=0;')


# 6. C++ WebSocket Handler Updates
cpp_injection = '''        else if (msg.startsWith("W:")) {
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
        else if (msg.startsWith("T:"))'''

code = code.replace('else if (msg.startsWith("T:"))', cpp_injection)


# Ensure BLE toggle updates text as well
code = code.replace('wsS(\'T:\'+(this.classList.contains(\'toggled\')?\'usb\':\'ble\')); this.classList.toggle(\'toggled\');',
'wsS(\'T:\'+(this.classList.contains(\'toggled\')?\'usb\':\'ble\')); this.classList.toggle(\'toggled\'); this.innerText=this.classList.contains(\'toggled\')?\'ON\':\'OFF\';')


with open('src/main.cpp', 'w') as f:
    f.write(code)
