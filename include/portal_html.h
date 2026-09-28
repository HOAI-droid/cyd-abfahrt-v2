#pragma once
#include <Arduino.h>

// Einrichtungsseite (wird vom CYD im eigenen WLAN ausgeliefert)
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Abfahrtsmonitor einrichten</title>
<style>
:root{--bg:#CFCBBF;--fg:#111;--sub:#4A473F;--card:#E4E1D8;--panel:#3A3833;--pfg:#F2F1EC;--line:#B8B4A9;--ok:#1F7A45;--err:#A8281C}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif}
main{max-width:520px;margin:0 auto;padding:20px 16px 48px}
h1{font-size:26px;margin:4px 0 2px;letter-spacing:-.4px}
.lead{color:var(--sub);margin:0 0 18px}
section{background:var(--card);border-radius:16px;padding:16px;margin:0 0 14px}
h2{font-size:13px;letter-spacing:1px;text-transform:uppercase;margin:0 0 10px;color:var(--sub)}
label{display:block;font-weight:600;font-size:14px;margin:12px 0 6px}
label:first-of-type{margin-top:0}
input,select{width:100%;font:inherit;padding:12px;border:1.5px solid var(--line);border-radius:12px;background:#fff;color:var(--fg)}
input:focus,select:focus{outline:none;border-color:var(--fg)}
input:disabled{background:#EEE;color:#888}
.hint{font-size:13px;color:var(--sub);margin-top:6px}
.list{margin-top:6px;border-radius:12px;overflow:hidden;background:#fff;border:1.5px solid var(--line);display:none;max-height:300px;overflow-y:auto}
.list button{display:block;width:100%;text-align:left;border:0;border-bottom:1px solid #eee;background:#fff;padding:11px 12px;font:inherit;color:var(--fg);cursor:pointer}
.list button:last-child{border-bottom:0}
.list button:active,.list button:hover{background:#F2F1EC}
.list small{color:var(--sub);margin-left:6px}
.chosen{display:none;margin-top:8px;padding:10px 12px;border-radius:12px;background:var(--panel);color:var(--pfg);align-items:center;gap:8px}
.chosen span{flex:1}
.chosen small{color:#B8B4A9}
.chosen button{border:0;background:#4F4C45;color:var(--pfg);border-radius:8px;padding:6px 10px;font:inherit;font-size:13px;cursor:pointer}
.row{display:flex;gap:8px;align-items:center}
.row select{flex:1}
.step{display:flex;align-items:center;gap:12px}
.step button{width:52px;height:48px;border:0;border-radius:12px;background:var(--panel);color:var(--pfg);font-size:24px;cursor:pointer}
.step output{flex:1;text-align:center;font-size:30px;font-weight:800}
.small{border:0;background:transparent;color:var(--sub);text-decoration:underline;font:inherit;font-size:14px;cursor:pointer;padding:6px 0}
#save{width:100%;padding:16px;border:0;border-radius:14px;background:var(--fg);color:var(--pfg);font:inherit;font-size:18px;font-weight:700;cursor:pointer}
#msg{margin:10px 0 0;font-weight:600;min-height:1em}
.bins{display:flex;flex-wrap:wrap;gap:8px}
.bins label{display:flex;align-items:center;gap:7px;margin:0;font-weight:500;background:#fff;border:1.5px solid var(--line);border-radius:10px;padding:8px 10px;font-size:14px;cursor:pointer}
.bins input{width:auto;margin:0}
.bins i{width:12px;height:14px;border-radius:2px;display:inline-block}
#msg.err{color:var(--err)} #msg.ok{color:var(--ok)}
</style></head>
<body><main>
<h1>Abfahrtsmonitor</h1>
<p class="lead">Einmal einrichten, danach zeigt das Display, wann du losgehen musst.</p>

<section>
<h2>1 · WLAN zu Hause</h2>
<label for="ssidSel">Netzwerk</label>
<div class="row"><select id="ssidSel"><option value="">wird gesucht …</option></select>
<button type="button" class="small" id="rescan">neu suchen</button></div>
<input id="ssid" placeholder="Netzwerkname" style="display:none;margin-top:8px" autocomplete="off">
<label for="pass">Passwort</label>
<input id="pass" type="password" autocomplete="off">
<div class="hint" id="passHint"></div>
</section>

<section>
<h2>2 · RMV-Schlüssel</h2>
<label for="key">Schlüssel von opendata.rmv.de</label>
<input id="key" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx">
<div class="hint" id="keyHint"></div>
</section>

<section>
<h2>3 · Abfahrt</h2>
<label for="aCity">Stadt</label>
<input id="aCity" autocomplete="off" placeholder="z.&nbsp;B. Darmstadt">
<div class="list" id="aCityList"></div>
<label for="aStop">Haltestelle</label>
<input id="aStop" autocomplete="off" placeholder="erst Stadt wählen" disabled>
<div class="list" id="aStopList"></div>
<div class="chosen" id="aChosen"><span></span><button type="button">ändern</button></div>
</section>

<section>
<h2>4 · Fahrtrichtung</h2>
<p class="hint" style="margin:0 0 10px">Eine Haltestelle, über die deine Bahn fährt. Dann werden nur Bahnen in diese Richtung gezeigt.</p>
<label for="bCity">Stadt</label>
<input id="bCity" autocomplete="off" placeholder="z.&nbsp;B. Darmstadt">
<div class="list" id="bCityList"></div>
<label for="bStop">Haltestelle</label>
<input id="bStop" autocomplete="off" placeholder="erst Stadt wählen" disabled>
<div class="list" id="bStopList"></div>
<div class="chosen" id="bChosen"><span></span><button type="button">ändern</button></div>
<button type="button" class="small" id="noDir">keine Richtung, alle Bahnen zeigen</button>
</section>

<section>
<h2>5 · Linien und Gehzeit</h2>
<label for="lines">Linien (optional)</label>
<input id="lines" placeholder="z.&nbsp;B. 1, 6 · leer = alle Linien" autocomplete="off">
<label>Gehzeit zur Haltestelle</label>
<div class="step"><button type="button" id="wm" aria-label="weniger">−</button><output id="walkOut">6 min</output><button type="button" id="wp" aria-label="mehr">+</button></div>
</section>

<section>
<h2>6 · Müllabfuhr (EAD Darmstadt)</h2>
<label for="mStreet">Straße</label>
<input id="mStreet" autocomplete="off" placeholder="die ersten Buchstaben eingeben">
<div class="list" id="mStreetList"></div>
<div class="chosen" id="mChosen"><span></span><button type="button">ändern</button></div>
<div class="hint" id="mHint">Optional. Das Gerät holt die Abfuhrtermine danach selbst, alle 4 Wochen.</div>
<label for="mHnr">Hausnummer</label>
<input id="mHnr" autocomplete="off" inputmode="numeric" placeholder="nur nötig bei mehreren Abfuhrbezirken">
<label>Welche Tonnen anzeigen?</label>
<div class="bins">
<label><input type="checkbox" id="mb0" checked><i style="background:#3C3C3A"></i>Restmüll</label>
<label><input type="checkbox" id="mb1" checked><i style="background:#7A4E24"></i>Bio</label>
<label><input type="checkbox" id="mb2" checked><i style="background:#2F5FA8"></i>Papier</label>
<label><input type="checkbox" id="mb3" checked><i style="background:#E0B000"></i>Gelbe Tonne</label>
</div>
</section>

<button id="save" type="button">Speichern und starten</button>
<p id="msg"></p>
</main>
<script>
var cfg={}, stops=[], cities=[], walk=6;
var $=function(i){return document.getElementById(i)};
function norm(s){return (s||"").toLowerCase().replace(/ä/g,"ae").replace(/ö/g,"oe").replace(/ü/g,"ue").replace(/ß/g,"ss").normalize("NFD").replace(/[̀-ͯ]/g,"").replace(/[^a-z0-9]+/g," ").trim()}
function ascii(s){return (s||"").replace(/Ä/g,"Ae").replace(/Ö/g,"Oe").replace(/Ü/g,"Ue").replace(/ä/g,"ae").replace(/ö/g,"oe").replace(/ü/g,"ue").replace(/ß/g,"ss").normalize("NFD").replace(/[̀-ͯ]/g,"").replace(/[^\x20-\x7E]/g,"")}
function esc(s){return (s||"").replace(/[&<>"]/g,function(c){return{"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]})}
function msg(t,c){var m=$("msg");m.textContent=t;m.className=c||""}

function picker(p,onChoose){
  var st={city:"",id:"",name:"",ort:""};
  var ci=$(p+"City"),cl=$(p+"CityList"),si=$(p+"Stop"),sl=$(p+"StopList"),ch=$(p+"Chosen");
  function show(list,html){list.innerHTML=html;list.style.display=html?"block":"none"}
  function cityHits(q){q=norm(q);if(!q)return [];var a=[],b=[];
    cities.forEach(function(c){var n=norm(c);if(n.indexOf(q)==0)a.push(c);else if(n.indexOf(" "+q)>=0||n.indexOf(q)>0)b.push(c)});
    return a.concat(b).slice(0,12)}
  function setCity(c){st.city=c;ci.value=c;show(cl,"");si.disabled=false;si.placeholder="Haltestelle in "+c+" suchen";si.value="";stopList();}
  function stopList(){var q=norm(si.value),out=[];
    for(var i=0;i<stops.length&&out.length<40;i++){var s=stops[i];if(s[1]!=st.city)continue;
      if(!q||norm(s[3]).indexOf(q)>=0||norm(s[2]+" "+s[3]).indexOf(q)>=0)out.push(s)}
    show(sl,out.map(function(s){return '<button type="button" data-id="'+s[0]+'">'+esc(s[3])+(s[2]?'<small>'+esc(s[2])+'</small>':'')+'</button>'}).join(""));}
  function choose(s){st.id=s[0];st.name=s[3];st.ort=s[2];st.city=s[1];
    ch.querySelector("span").innerHTML="✓ <b>"+esc(s[3])+"</b> <small>"+esc(s[1]+(s[2]?" · "+s[2]:""))+"</small>";
    ch.style.display="flex";show(sl,"");show(cl,"");si.value=s[3];ci.value=s[1];si.disabled=false;if(onChoose)onChoose(s)}
  ci.addEventListener("blur",function(){setTimeout(function(){if(st.city)return;var q=norm(ci.value);
    for(var i=0;i<cities.length;i++)if(norm(cities[i])==q){setCity(cities[i]);show(sl,"");break}},250)});
  ci.addEventListener("input",function(){st.city="";st.id="";ch.style.display="none";si.disabled=true;si.value="";show(sl,"");
    show(cl,cityHits(ci.value).map(function(c){return '<button type="button">'+esc(c)+'</button>'}).join(""))});
  cl.addEventListener("click",function(e){var b=e.target.closest("button");if(b){setCity(b.textContent);si.focus()}});
  si.addEventListener("focus",function(){if(st.city&&!st.id)stopList()});
  si.addEventListener("input",function(){st.id="";ch.style.display="none";stopList()});
  sl.addEventListener("click",function(e){var b=e.target.closest("button");if(!b)return;
    var id=b.getAttribute("data-id");for(var i=0;i<stops.length;i++)if(stops[i][0]==id){choose(stops[i]);break}});
  ch.querySelector("button").addEventListener("click",function(){st.id="";ch.style.display="none";si.value="";si.focus();stopList()});
  return {st:st,choose:choose,setCity:function(c){setCity(c);show(sl,"")},clear:function(){st.id="";st.city="";ci.value="";si.value="";si.disabled=true;ch.style.display="none";show(sl,"");show(cl,"")}};
}
var B=picker("b"), A=picker("a",function(s){if(!B.st.city&&!B.st.id)B.setCity(s[1])});


var streets=[], mSel=null;
(function(){
  var si=$("mStreet"),sl=$("mStreetList"),ch=$("mChosen");
  function show(h){sl.innerHTML=h;sl.style.display=h?"block":"none"}
  function hits(q){q=norm(q);if(q.length<2)return [];var a=[],b=[];
    streets.forEach(function(s){var n=norm(s.n);if(n.indexOf(q)==0)a.push(s);else if(n.indexOf(q)>0)b.push(s)});
    return a.concat(b).slice(0,30)}
  window.chooseStreet=function(s){mSel=s;ch.querySelector("span").innerHTML="✓ <b>"+esc(s.n)+"</b>";ch.style.display="flex";si.value=s.n;show("")};
  si.addEventListener("input",function(){mSel=null;ch.style.display="none";
    show(hits(si.value).map(function(s,i){return '<button type="button" data-i="'+streets.indexOf(s)+'">'+esc(s.n)+'</button>'}).join(""))});
  sl.addEventListener("click",function(e){var b=e.target.closest("button");if(b)chooseStreet(streets[+b.getAttribute("data-i")])});
  ch.querySelector("button").addEventListener("click",function(){mSel=null;ch.style.display="none";si.value="";si.focus()});
})();
function loadStreets(){
  return fetch("/streets.csv").then(function(r){return r.text()}).then(function(t){
    t.split("\n").forEach(function(l){if(!l)return;var p=l.split(";");streets.push({n:p[0],v:p[1]||p[0]})});
    streets.sort(function(a,b){return a.n.localeCompare(b.n,"de")});
    if(!streets.length)$("mHint").textContent="Straßenliste nicht verfügbar: Straße bitte genau wie beim EAD schreiben (z.\u00a0B. Frankfurter Landstraße).";
    if(cfg.street){var f=null;streets.forEach(function(s){if(s.v==(cfg.streetVal||cfg.street))f=s});
      if(f)chooseStreet(f);else $("mStreet").value=cfg.street}
  }).catch(function(){});
}

function setWalk(v){walk=Math.max(1,Math.min(45,v));$("walkOut").textContent=walk+" min"}
$("wm").onclick=function(){setWalk(walk-1)}; $("wp").onclick=function(){setWalk(walk+1)};
$("noDir").onclick=function(){B.clear()};
$("ssidSel").onchange=function(){$("ssid").style.display=this.value=="__other"?"block":"none"};

function loadScan(refresh){
  $("ssidSel").innerHTML='<option value="">wird gesucht …</option>';
  fetch("/scan"+(refresh?"?refresh=1":"")).then(function(r){return r.json()}).then(function(list){
    var sel=$("ssidSel"),h="";
    list.forEach(function(n){h+='<option>'+esc(n)+'</option>'});
    h+='<option value="__other">anderes Netz eingeben …</option>';
    sel.innerHTML=h;
    if(cfg.ssid){var found=list.indexOf(cfg.ssid)>=0;if(found)sel.value=cfg.ssid;else{sel.value="__other";$("ssid").value=cfg.ssid}}
    sel.onchange();
  }).catch(function(){$("ssidSel").innerHTML='<option value="__other">Netz eingeben …</option>';$("ssidSel").onchange()});
}
$("rescan").onclick=function(){loadScan(true)};

fetch("/config").then(function(r){return r.json()}).then(function(c){
  cfg=c;
  if(c.hasPass)$("passHint").textContent="Gespeichert. Leer lassen, um es zu behalten.";
  if(c.keyTail)$("keyHint").textContent="Gespeichert (…"+c.keyTail+"). Leer lassen, um ihn zu behalten.";
  if(c.lines)$("lines").value=c.lines;
  if(c.walk)setWalk(+c.walk);
  if(c.hnr)$("mHnr").value=c.hnr;
  if(c.bins!==undefined)for(var i=0;i<4;i++)$("mb"+i).checked=!!(c.bins&(1<<i));
}).catch(function(){}).then(function(){
  loadScan(false);
  return fetch("/stops.csv").then(function(r){return r.text()});
}).then(function(t){
  var set={};
  t.split("\n").forEach(function(l){if(!l)return;var p=l.split(";");stops.push(p);set[p[1]]=1});
  cities=Object.keys(set).sort(function(a,b){return a.localeCompare(b,"de")});
  stops.sort(function(a,b){return a[3].localeCompare(b[3],"de")});
  function pre(P,id){if(!id)return;for(var i=0;i<stops.length;i++)if(stops[i][0]==id){P.choose(stops[i]);return}}
  pre(A,cfg.stop);pre(B,cfg.dir);
}).catch(function(){msg("Haltestellenliste konnte nicht geladen werden.","err")}).then(loadStreets);

$("save").onclick=function(){
  var sel=$("ssidSel").value, ssid=sel=="__other"?$("ssid").value.trim():sel;
  if(!ssid)return msg("Bitte ein WLAN wählen.","err");
  if(!$("key").value.trim()&&!cfg.keyTail)return msg("Bitte den RMV-Schlüssel eintragen.","err");
  if(!A.st.id)return msg("Bitte Stadt und Abfahrts-Haltestelle wählen.","err");
  var f=new URLSearchParams();
  f.append("ssid",ssid);f.append("pass",$("pass").value);f.append("key",$("key").value.trim());
  f.append("stop",A.st.id);f.append("stopName",ascii(A.st.name));
  f.append("dir",B.st.id||"");f.append("dirName",B.st.id?ascii(B.st.name):"");
  f.append("lines",$("lines").value.replace(/[^0-9A-Za-z, ]/g,""));f.append("walk",walk);
  f.append("place",ascii(A.st.city+(A.st.ort?"-"+A.st.ort:"")));
  var st=$("mStreet").value.trim();
  if(st&&!mSel&&streets.length)return msg("Bitte die Straße aus der Liste wählen.","err");
  var bins=0;for(var i=0;i<4;i++)if($("mb"+i).checked)bins|=1<<i;
  if(st&&!bins)return msg("Bitte mindestens eine Tonne auswählen.","err");
  f.append("street",mSel?mSel.n:st);f.append("streetVal",mSel?mSel.v:st);
  f.append("hnr",$("mHnr").value.replace(/[^0-9a-zA-Z]/g,""));f.append("bins",bins||15);
  msg("Speichere …");
  fetch("/save",{method:"POST",body:f}).then(function(r){return r.text()}).then(function(t){
    if(t=="OK"){msg("Gespeichert. Das Display startet jetzt neu, du kannst dieses WLAN verlassen.","ok");$("save").disabled=true}
    else msg(t,"err")}).catch(function(){msg("Gespeichert. Das Display startet neu.","ok")});
};
</script>
</body></html>)HTML";
