with open('src/main.cpp', 'r') as f:
    code = f.read()

# Restore the original full-width trackpad and remove the scroll div
old_html = '<div style="display:flex; flex:1; height:100%;"><div id="pad" style="flex:1; background:#0a0a0a; border:1px solid #333; display:flex; align-items:center; justify-content:center; color:#222; font-weight:bold; font-size:20px; touch-action:none;">TRACKPAD</div><div id="scroll" style="width:40px; background:#111; border:1px solid #333; border-left:none; display:flex; align-items:center; justify-content:center; color:#555; font-size:20px; touch-action:none; cursor:ns-resize;">↕</div></div>'
new_html = '<div id="pad" style="flex:1; background:#0a0a0a; border:1px solid #333; display:flex; align-items:center; justify-content:center; color:#222; border-radius:8px 8px 0 0; font-weight:bold; font-size:20px; touch-action:none;">TRACKPAD</div>'
code = code.replace(old_html, new_html)

# Replace the JS
import re

# We will match everything from "let scrl =" up to the "// Image Editor" comment.
js_pattern = re.compile(r"let scrl = document\.getElementById\('scroll'\).*?p\.ontouchmove=.*?e\.preventDefault\(\);\s*};", re.DOTALL)

new_js = """
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
};"""

code = js_pattern.sub(new_js.strip(), code)

with open('src/main.cpp', 'w') as f:
    f.write(code)
