// web_page.h -- the physio's page, served by the ESP32 itself (works offline:
// no fonts or scripts from the internet, the phone is on the device's hotspot).
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" href="data:,">
<title>RehabSense</title>
<style>
:root{--ink:#14212b;--sub:#56656f;--line:#d9e1e5;--paper:#ffffff;--wash:#f3f6f7;--band:#cfe8dc;--good:#1f7a55;--limit:#b3261e;--accent:#0f6c8c}
*{box-sizing:border-box}
body{margin:0;font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:var(--ink);background:var(--paper)}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 0;font-weight:650}
h2{font-size:17px;margin:28px 0 8px;font-weight:650}
p{margin:4px 0}
.sub{color:var(--sub);font-size:14px}
.dial{display:block;width:100%;max-width:420px;margin:8px auto 0}
.readout{text-align:center;margin-top:-8px}
.angle{font-size:56px;font-weight:700;letter-spacing:-1px;font-variant-numeric:tabular-nums}
.state{font-size:15px;color:var(--sub)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:16px 0}
.stats div{background:var(--wash);border-radius:10px;padding:10px;text-align:center}
.stats b{display:block;font-size:22px;font-variant-numeric:tabular-nums}
.stats span{font-size:13px;color:var(--sub)}
.warn{color:var(--limit);font-weight:600;min-height:22px;text-align:center}
fieldset{border:1px solid var(--line);border-radius:12px;padding:12px 14px;margin:0 0 12px}
legend{padding:0 6px;font-weight:650}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px 14px}
label{display:block;font-size:14px;color:var(--sub)}
input[type=number],input[type=text],input[type=password]{width:100%;font:inherit;color:var(--ink);padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:#fff}
input:focus-visible,button:focus-visible{outline:3px solid var(--accent);outline-offset:1px}
.err{color:var(--limit);font-size:13px;min-height:0}
.check{display:flex;gap:8px;align-items:center;margin:6px 0;color:var(--ink);font-size:15px}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:11px 16px;background:var(--ink);color:#fff;cursor:pointer}
button.quiet{background:var(--wash);color:var(--ink)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.msg{font-size:14px}
table{width:100%;border-collapse:collapse;font-size:14px;font-variant-numeric:tabular-nums}
th,td{padding:7px 6px;border-bottom:1px solid var(--line);text-align:right}
th:first-child,td:first-child{text-align:left}
th{font-weight:600;color:var(--sub)}
.scroll{overflow-x:auto}
.health{font-size:13px;color:var(--sub)}
.testline{font-size:17px;font-weight:650;min-height:0;margin-top:4px;font-variant-numeric:tabular-nums}
@media (max-width:420px){.grid{grid-template-columns:1fr 1fr}.angle{font-size:48px}}
</style></head><body><main>
<h1>RehabSense</h1>
<p class="sub" id="exline">Connecting to the brace...</p>

<svg class="dial" viewBox="-18 0 356 180" role="img" aria-label="Live joint angle dial">
 <path id="track" fill="none" stroke="#d9e1e5" stroke-width="16" stroke-linecap="round"/>
 <path id="band" fill="none" stroke="#cfe8dc" stroke-width="16"/>
 <line id="limit" stroke="#b3261e" stroke-width="4" stroke-linecap="round"/>
 <g id="ticks" stroke="#9aa7ae" stroke-width="1.5"></g>
 <g id="labels" font-size="11" fill="#56656f" text-anchor="middle"></g>
 <line id="needle" x1="160" y1="160" x2="160" y2="40" stroke="#14212b" stroke-width="4" stroke-linecap="round"/>
 <circle cx="160" cy="160" r="8" fill="#14212b"/>
</svg>
<div class="readout"><div class="angle" id="angle">--</div><div class="state" id="state">&nbsp;</div><div class="testline" id="testline"></div></div>
<div class="warn" id="warn"></div>
<div class="stats">
 <div><b id="valid">0</b><span>valid reps</span></div>
 <div><b id="attempts">0</b><span>attempts</span></div>
 <div><b id="hold">0.0</b><span>hold (s)</span></div>
</div>
<p class="health" id="health"></p>

<h2>Prescription</h2>
<form id="cfg" novalidate>
<fieldset><legend>Knee flexion</legend><div class="grid">
 <div><label for="k_target">Target angle (&deg;)</label><input id="k_target" name="k_target" type="number" step="1"><div class="err" data-for="k_target"></div></div>
 <div><label for="k_limit">Safety limit (&deg;)</label><input id="k_limit" name="k_limit" type="number" step="1"><div class="err" data-for="k_limit"></div></div>
 <div><label for="k_hold">Hold (s)</label><input id="k_hold" name="k_hold" type="number" step="0.5"><div class="err" data-for="k_hold"></div></div>
 <div><label for="k_reps">Reps</label><input id="k_reps" name="k_reps" type="number" step="1"><div class="err" data-for="k_reps"></div></div>
 <div><label for="k_tol">Hold tolerance (&deg;)</label><input id="k_tol" name="k_tol" type="number" step="1"><div class="err" data-for="k_tol"></div></div>
 <div><label for="k_speed">Speed limit (&deg;/s)</label><input id="k_speed" name="k_speed" type="number" step="5"><div class="err" data-for="k_speed"></div></div>
 <div><label for="k_rest">Rest angle (&deg;)</label><input id="k_rest" name="k_rest" type="number" step="1"><div class="err" data-for="k_rest"></div></div>
 <div><label for="k_rotate">Rotation check (&deg;)</label><input id="k_rotate" name="k_rotate" type="number" step="1"><div class="err" data-for="k_rotate"></div></div>
</div><p class="sub">A rep ends when the knee goes back below the rest angle: raise it if the patient can't fully straighten. Speed limit or rotation check 0 turns that check off.</p></fieldset>
<fieldset><legend>Straight leg raise</legend><div class="grid">
 <div><label for="s_target">Target lift (&deg;)</label><input id="s_target" name="s_target" type="number" step="1"><div class="err" data-for="s_target"></div></div>
 <div><label for="s_limit">Safety limit (&deg;)</label><input id="s_limit" name="s_limit" type="number" step="1"><div class="err" data-for="s_limit"></div></div>
 <div><label for="s_hold">Hold (s)</label><input id="s_hold" name="s_hold" type="number" step="0.5"><div class="err" data-for="s_hold"></div></div>
 <div><label for="s_reps">Reps</label><input id="s_reps" name="s_reps" type="number" step="1"><div class="err" data-for="s_reps"></div></div>
 <div><label for="s_knee">Max knee bend (&deg;)</label><input id="s_knee" name="s_knee" type="number" step="1"><div class="err" data-for="s_knee"></div></div>
 <div><label for="s_speed">Speed limit (&deg;/s)</label><input id="s_speed" name="s_speed" type="number" step="5"><div class="err" data-for="s_speed"></div></div>
 <div><label for="s_rest">Rest angle (&deg;)</label><input id="s_rest" name="s_rest" type="number" step="1"><div class="err" data-for="s_rest"></div></div>
 <div><label for="s_rotate">Rotation check (&deg;)</label><input id="s_rotate" name="s_rotate" type="number" step="1"><div class="err" data-for="s_rotate"></div></div>
</div></fieldset>
<fieldset><legend>Elbow flexion</legend><div class="grid">
 <div><label for="e_target">Target angle (&deg;)</label><input id="e_target" name="e_target" type="number" step="1"><div class="err" data-for="e_target"></div></div>
 <div><label for="e_limit">Safety limit (&deg;)</label><input id="e_limit" name="e_limit" type="number" step="1"><div class="err" data-for="e_limit"></div></div>
 <div><label for="e_hold">Hold (s)</label><input id="e_hold" name="e_hold" type="number" step="0.5"><div class="err" data-for="e_hold"></div></div>
 <div><label for="e_reps">Reps</label><input id="e_reps" name="e_reps" type="number" step="1"><div class="err" data-for="e_reps"></div></div>
 <div><label for="e_tol">Hold tolerance (&deg;)</label><input id="e_tol" name="e_tol" type="number" step="1"><div class="err" data-for="e_tol"></div></div>
 <div><label for="e_speed">Speed limit (&deg;/s)</label><input id="e_speed" name="e_speed" type="number" step="5"><div class="err" data-for="e_speed"></div></div>
 <div><label for="e_rest">Rest angle (&deg;)</label><input id="e_rest" name="e_rest" type="number" step="1"><div class="err" data-for="e_rest"></div></div>
 <div><label for="e_rotate">Rotation check (&deg;)</label><input id="e_rotate" name="e_rotate" type="number" step="1"><div class="err" data-for="e_rotate"></div></div>
</div><p class="sub">Sensors on the upper arm and the forearm. The rotation check catches the forearm twisting: keep the thumb up.</p></fieldset>
<fieldset><legend>Feedback</legend>
 <label class="check"><input type="checkbox" id="buzzer" name="buzzer"> Sound cues</label>
 <label class="check"><input type="checkbox" id="safety" name="safety"> Vibrate at the safety limit even in assessment mode</label>
</fieldset>
<fieldset><legend>Cloud sync (optional)</legend><div class="grid">
 <div><label for="wifi_ssid">Wi-Fi name</label><input id="wifi_ssid" name="wifi_ssid" type="text" autocomplete="off"></div>
 <div><label for="wifi_pass">Wi-Fi password</label><input id="wifi_pass" name="wifi_pass" type="password" placeholder="unchanged"></div>
 <div style="grid-column:1/-1"><label for="ts_key">ThingSpeak write API key</label><input id="ts_key" name="ts_key" type="text" autocomplete="off" placeholder="unchanged"><div class="err" data-for="ts_key"></div></div>
</div><p class="sub" id="netline"></p></fieldset>
<div class="row"><button type="submit">Save prescription</button><span class="msg" id="savemsg" role="status"></span></div>
</form>

<h2>Sessions</h2>
<div class="row"><a href="/sessions.csv" download="rehabsense_sessions.csv"><button type="button" class="quiet">Download CSV</button></a><button type="button" class="quiet" id="refresh">Refresh</button></div>
<div class="scroll"><table><thead><tr><th>Session</th><th>Exercise</th><th>Valid</th><th>Peak</th><th>Hold</th><th>Alerts</th></tr></thead><tbody id="rows"><tr><td colspan="6">No sessions yet. Finish a set on the brace and it appears here.</td></tr></tbody></table></div>

<h2>Clinical tests</h2>
<p class="sub">On the brace: Clinical tests. 30 s chair stand and Timed Up and Go follow the CDC STEADI protocol; position sense is done with eyes closed.</p>
<div class="row"><a href="/tests.csv" download="rehabsense_tests.csv"><button type="button" class="quiet">Download CSV</button></a></div>
<div class="scroll"><table><thead><tr><th>#</th><th>Test</th><th>Result</th></tr></thead><tbody id="trows"><tr><td colspan="3">No tests yet.</td></tr></tbody></table></div>
</main>
<script>
const $=id=>document.getElementById(id);
const CX=160,CY=160,R=120,MAX=150;
function pt(deg,r){const a=Math.PI*(1-Math.min(Math.max(deg,0),MAX)/MAX);return[CX+r*Math.cos(a),CY-r*Math.sin(a)];}
function arc(a,b,r){const[p,q]=[pt(a,r),pt(b,r)];return`M${p[0].toFixed(1)} ${p[1].toFixed(1)} A${r} ${r} 0 0 1 ${q[0].toFixed(1)} ${q[1].toFixed(1)}`;}
$('track').setAttribute('d',arc(0,MAX,R));
(function(){let t='',l='';for(let d=0;d<=MAX;d+=10){const[a,b]=[pt(d,R+12),pt(d,R+(d%30?17:22))];t+=`<line x1="${a[0]}" y1="${a[1]}" x2="${b[0]}" y2="${b[1]}"/>`;if(d%30==0){const c=pt(d,R+32);l+=`<text x="${c[0].toFixed(1)}" y="${(c[1]+4).toFixed(1)}">${d}</text>`;}}$('ticks').innerHTML=t;$('labels').innerHTML=l;})();
let shown=null;
function setDial(p){const lo=p.target-p.tol;$('band').setAttribute('d',arc(lo,Math.min(p.limit,MAX),R));const[a,b]=[pt(p.limit,R-12),pt(p.limit,R+12)];const L=$('limit');L.setAttribute('x1',a[0]);L.setAttribute('y1',a[1]);L.setAttribute('x2',b[0]);L.setAttribute('y2',b[1]);}
function needle(v){const[x,y]=pt(v,R-18);$('needle').setAttribute('x2',x.toFixed(1));$('needle').setAttribute('y2',y.toFixed(1));}
const PH={0:'At rest',1:'Moving to target',2:'Holding',3:'Hold complete, return slowly',4:'Returning'};
const TN=['30 s chair stand','Timed Up and Go','Position sense'];
function testText(d){const sg=v=>(v>0?'+':'')+v.toFixed(1);
 if(d.testPhase==1)return'Waiting for the patient to sit down';if(d.testPhase==2)return`Starting in ${d.testCd}`;
 if(d.testKind==0)return`${d.testCount} stands · ${Math.max(0,30-d.testTime).toFixed(0)} s left`;
 if(d.testKind==1)return`${d.testTime.toFixed(1)} s · ${['getting up','walking','sitting down'][d.testStage]||''}`;
 return`Trial ${Math.min(3,d.testCount+1)} of 3 · target ${d.testTarget}°`+(d.testCount?` · last error ${sg(d.testLast)}°`:'');}
let wasTest=false;
async function live(){
 try{const r=await fetch('/api/live',{cache:'no-store'});const d=await r.json();
  const ex=['Knee flexion','Straight leg raise','Elbow flexion'][d.exercise]||'Exercise';
  $('exline').textContent=d.active?`${ex} · ${d.mode==0?'guided':'assessment'} session in progress`:(d.calibrated?'Calibrated. Start a session on the brace.':'Not calibrated yet. Straighten the leg and calibrate on the brace.');
  if(d.testActive)$('exline').textContent=`${TN[d.testKind]||'Test'} in progress`;
  $('testline').textContent=d.testActive?testText(d):'';
  if(wasTest&&!d.testActive)setTimeout(tests,800);wasTest=!!d.testActive;
  const v=d.active?d.metric:d.knee;
  $('angle').textContent=d.calibrated?`${v.toFixed(0)}°`:'--';
  needle(d.calibrated?v:0);
  if(d.active){setDial(d.params);$('state').textContent=PH[d.phase]||'';}else{setDial({target:+$('k_target').value||80,tol:+$('k_tol').value||5,limit:+$('k_limit').value||95});$('state').textContent=d.calibrated?'Joint angle (knee or elbow)':'';}
  $('valid').textContent=d.valid;$('attempts').textContent=d.attempts;$('hold').textContent=d.hold.toFixed(1);
  let w='';if(d.lost)w='Sensor signal lost. Check the shin and thigh cables.';else if(d.rotation>20)w='Leg is rotated. Keep the knee pointing up.';
  $('warn').textContent=w;
  $('health').textContent=`Sensors ${d.thighOk&&d.shinOk?'OK':'check wiring'} · ${d.hz.toFixed(0)} samples/s · ${d.pending} session(s) waiting to sync`;
 }catch(e){$('exline').textContent='Lost connection to the brace. Reconnect to its Wi-Fi.';}
}
let timer=null;function startLive(){if(!timer)timer=setInterval(live,250);}function stopLive(){clearInterval(timer);timer=null;}
document.addEventListener('visibilitychange',()=>document.hidden?stopLive():startLive());
async function loadCfg(){const d=await (await fetch('/api/config')).json();const k=d.params[0],s=d.params[1],e=d.params[2];
 const set=(id,v)=>$(id).value=v;set('k_target',k.target);set('k_limit',k.limit);set('k_hold',k.hold);set('k_reps',k.reps);set('k_tol',k.tol);set('k_speed',k.speed);set('k_rest',k.rest);set('k_rotate',k.rotate);
 set('s_target',s.target);set('s_limit',s.limit);set('s_hold',s.hold);set('s_reps',s.reps);set('s_knee',s.knee);set('s_speed',s.speed);set('s_rest',s.rest);set('s_rotate',s.rotate);
 if(e){set('e_target',e.target);set('e_limit',e.limit);set('e_hold',e.hold);set('e_reps',e.reps);set('e_tol',e.tol);set('e_speed',e.speed);set('e_rest',e.rest);set('e_rotate',e.rotate);}
 $('buzzer').checked=d.buzzer;$('safety').checked=d.safety;$('wifi_ssid').value=d.wifiSsid;
 $('netline').textContent=(d.staConnected?`Online (${d.staIp})`:'Offline')+(d.tsKeySet?' · ThingSpeak key saved':' · no ThingSpeak key');}
$('cfg').addEventListener('submit',async e=>{e.preventDefault();document.querySelectorAll('.err').forEach(x=>x.textContent='');
 const f=new URLSearchParams(new FormData($('cfg')));f.set('buzzer',$('buzzer').checked?'1':'0');f.set('safety',$('safety').checked?'1':'0');
 $('savemsg').textContent='Saving...';
 try{const r=await fetch('/api/config',{method:'POST',body:f});const d=await r.json();
  if(d.ok){$('savemsg').textContent=d.adjusted?'Saved. Some values were adjusted to safe ranges.':'Saved. Applies to the next session.';loadCfg();}
  else{$('savemsg').textContent='Not saved. Fix the highlighted fields.';for(const[k,m]of Object.entries(d.errors||{})){const el=document.querySelector(`.err[data-for="${k}"]`);if(el)el.textContent=m;}}
 }catch(e){$('savemsg').textContent='Not saved: the brace did not answer.';}});
async function sessions(){try{const d=await (await fetch('/api/sessions')).json();if(!d.length)return;
 $('rows').innerHTML=d.reverse().map(s=>`<tr><td>#${s.id}</td><td>${['Knee','SLR','Elbow'][s.ex]||'?'}${s.mode==1?' (A)':''}</td><td>${s.valid}/${s.target}</td><td>${s.peak.toFixed(0)}°</td><td>${s.hold.toFixed(1)} s</td><td>${s.warn}</td></tr>`).join('');}catch(e){}}
$('refresh').addEventListener('click',()=>{sessions();tests();});
async function tests(){try{const d=await (await fetch('/api/tests')).json();if(!d.length)return;
 const res=t=>t.test==0?`${t.score.toFixed(0)} stands`:t.test==1?`${t.score.toFixed(1)} s`:`${t.score.toFixed(1)}° mean error (${t.target}°)`;
 $('trows').innerHTML=d.reverse().map(t=>`<tr><td>#${t.id}</td><td>${TN[t.test]||'?'}</td><td>${res(t)}</td></tr>`).join('');}catch(e){}}
loadCfg().catch(()=>{});sessions();tests();live();startLive();
</script></body></html>)rawliteral";
