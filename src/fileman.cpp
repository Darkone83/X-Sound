#include "fileman.h"

#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "wifimgr.h"
#include "audio_player.h"
#include "led_stat.h"

// -------- Settings --------
static const char* kBootPath  = "/boot.mp3";
static const char* kEjectPath = "/eject.mp3";
static const size_t kMaxUploadBytes = 6 * 1024 * 1024; // safety cap

// -------- Persistent prefs (xsound namespace) --------
// Cached settings to reduce NVS wear
static uint8_t g_volume = 200;          // 0–255
static bool    g_bootEnabled  = true;
static bool    g_ejectEnabled = true;
static unsigned long g_lastVolWriteMs = 0;

// NVS helpers
static void fmPrefsReadAll() {
  Preferences p;
  if (p.begin("xsound", /*ro=*/true)) {
    g_volume       = p.getUChar("volume", 200);
    g_bootEnabled  = p.getBool("boot_enabled",  true);
    g_ejectEnabled = p.getBool("eject_enabled", true);
    p.end();
  }
}

static void fmBootSoundWrite(bool en) {
  if (g_bootEnabled == en) return;
  g_bootEnabled = en;
  AudioPlayer::setBootEnabled(en);  // Sync with audio player
  Preferences p;
  if (p.begin("xsound", /*ro=*/false)) {
    p.putBool("boot_enabled", en);
    p.end();
  }
}

static void fmEjectSoundWrite(bool en) {
  if (g_ejectEnabled == en) return;
  g_ejectEnabled = en;
  AudioPlayer::setEjectEnabled(en);  // Sync with audio player
  Preferences p;
  if (p.begin("xsound", /*ro=*/false)) {
    p.putBool("eject_enabled", en);
    p.end();
  }
}

// Persist volume only when changed (and not too frequently)
static void fmVolumeWrite(uint8_t vol) {
  if (g_volume == vol) return;
  g_volume = vol;
  unsigned long now = millis();
  if (now - g_lastVolWriteMs < 250) return; // soft throttle
  g_lastVolWriteMs = now;
  Preferences p;
  if (p.begin("xsound", /*ro=*/false)) {
    p.putUChar("volume", vol);
    p.end();
  }
}

// Parse an integer JSON field like {"val":123} without pulling in ArduinoJson
static int parseJsonIntField(const String& body, const char* key, int fallback = -1) {
  // Look for "key"
  String needle = String("\"") + key + "\"";
  int k = body.indexOf(needle);
  if (k < 0) return fallback;
  // Find the colon after "key"
  int colon = body.indexOf(':', k + needle.length());
  if (colon < 0) return fallback;

  // Skip whitespace
  int i = colon + 1;
  while (i < (int)body.length() && (body[i]==' '||body[i]=='\t'||body[i]=='\r'||body[i]=='\n')) i++;

  // Optional sign
  bool neg = false;
  if (i < (int)body.length() && (body[i]=='+' || body[i]=='-')) { neg = (body[i]=='-'); i++; }

  // Read digits
  if (i >= (int)body.length() || body[i]<'0' || body[i]>'9') return fallback;
  long v = 0;
  while (i < (int)body.length() && body[i] >= '0' && body[i] <= '9') {
    v = v*10 + (body[i]-'0');
    i++;
    if (v > 1000000) break; // sanity
  }
  if (neg) v = -v;
  return (int)v;
}

// -------------- Utils --------------
static String humanSize(uint64_t b) {
  const char* units[] = {"B","KB","MB","GB"};
  double s = (double)b;
  int u = 0;
  while (s >= 1024.0 && u < 3) { s /= 1024.0; u++; }
  char buf[32];
  snprintf(buf, sizeof(buf), (u==0) ? "%.0f %s" : "%.2f %s", s, units[u]);
  return String(buf);
}

static String jsonEscape(const String& in) {
  String out; out.reserve(in.length()+8);
  for (size_t i=0;i<in.length();++i) {
    char c=in[i];
    switch(c){
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

static void addNoStore(AsyncWebServerResponse* resp) {
  resp->addHeader("Cache-Control", "no-store");
}

// -------------- HTML UI (/files) --------------
static const char FILES_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>X-Sound File Manager</title>
<style>
:root{--bg:#0f0f11;--card:#1b1b22;--ink:#EDEFF2;--mut:#AAB;--warn:#b12424;--btn:#2563eb}
*{box-sizing:border-box} html,body{height:100%}
body{background:var(--bg);color:var(--ink);font-family:system-ui,Segoe UI,Roboto,Arial;margin:0}
.wrap{min-height:100%;display:flex;align-items:center;justify-content:center;padding:env(safe-area-inset-top) 12px env(safe-area-inset-bottom)}
.card{width:100%;max-width:540px;margin:16px auto;background:var(--card);padding:18px;border-radius:12px;box-shadow:0 8px 20px #0008}
h1{margin:.2rem 0 1rem;font-size:1.45rem}
.grid{display:grid;grid-template-columns:1fr;gap:.7rem}
.row{display:grid;grid-template-columns:1fr 1fr;gap:.5rem;align-items:center}
input[type=file],button,input[type=range]{width:100%;padding:.7rem .8rem;border-radius:9px;border:1px solid #555;background:#111;color:var(--ink);font-size:1rem}
button{cursor:pointer}
.btn{background:var(--btn);border:0;color:#fff}
.btn-del{background:var(--warn)}
.btn-sec{background:#3d3d7a}
.small{font-size:.92rem;color:var(--mut)}
.kv{display:flex;justify-content:space-between;font-size:.95rem;background:#161616;border:1px solid #444;padding:.6rem .7rem;border-radius:9px}
.group{padding:.7rem;border:1px solid #444;border-radius:12px;background:#171717}
.actions{display:flex;gap:.5rem;flex-wrap:wrap}
.note{color:var(--mut);font-size:.92rem;margin-top:.4rem}
hr{border:none;height:1px;background:#333;margin:.8rem 0}
.playrow{display:flex;gap:.5rem;flex-wrap:wrap;margin-top:.4rem}
.sliderrow{display:grid;grid-template-columns:1fr auto;gap:.6rem;align-items:center;margin-top:.3rem}
.valuechip{background:#0f0f0f;border:1px solid #444;border-radius:9px;padding:.4rem .6rem;font-variant-numeric:tabular-nums}
.badge{display:inline-block;padding:.15rem .5rem;border-radius:999px;border:1px solid #444;background:#0f0f0f;font-size:.85rem}
</style></head>
<body>
<div class="wrap"><div class="card">
  <h1>X-Sound File Manager</h1>
  <div class="grid">

    <!-- Volume -->
    <div class="group">
      <div class="kv"><strong>Volume</strong><span class="small">0–100%</span></div>
      <div class="sliderrow">
        <input id="vol" type="range" min="0" max="100" step="1" value="80" oninput="onVolSlide(this.value)" onchange="commitVol(this.value)">
        <div class="valuechip"><span id="volv">80%</span></div>
      </div>
      <div class="note">Adjust output gain in real time. (Persistent)</div>
    </div>

    <!-- Boot Sound Toggle -->
    <div class="group">
      <div class="kv"><strong>Boot Sound</strong><span class="badge" id="bootState">…</span></div>
      <div class="actions">
        <button class="btn" id="bootBtn" onclick="toggleBoot()">…</button>
      </div>
      <div class="note">Controls whether <code>/boot.mp3</code> plays at startup. (Persistent)</div>
    </div>

    <!-- Eject Sound Toggle -->
    <div class="group">
      <div class="kv"><strong>Eject Sound</strong><span class="badge" id="ejectState">…</span></div>
      <div class="actions">
        <button class="btn" id="ejectBtn" onclick="toggleEject()">…</button>
      </div>
      <div class="note">Controls whether <code>/eject.mp3</code> plays when triggered. (Persistent)</div>
    </div>

    <div class="kv"><span>Storage used</span><span id="used">…</span></div>
    <div class="kv"><span>Storage free</span><span id="free">…</span></div>

    <div class="group">
      <div class="kv"><strong>Boot MP3</strong><span id="bootInfo">—</span></div>
      <div class="row">
        <input id="bootFile" type="file" accept=".mp3">
        <button class="btn" onclick="upload('boot')">Upload/Replace</button>
      </div>
      <div class="actions">
        <button class="btn-sec" onclick="downloadFile('boot')">Download</button>
        <button class="btn-del" onclick="delFile('boot')">Delete</button>
      </div>
      <div class="playrow">
        <button class="btn" onclick="play('boot')">▶ Play Boot</button>
        <button class="btn-sec" onclick="stopPlay()">■ Stop</button>
      </div>
      <div class="note">Saved as <code>/boot.mp3</code>.</div>
    </div>

    <div class="group">
      <div class="kv"><strong>Eject MP3</strong><span id="ejectInfo">—</span></div>
      <div class="row">
        <input id="ejectFile" type="file" accept=".mp3">
        <button class="btn" onclick="upload('eject')">Upload/Replace</button>
      </div>
      <div class="actions">
        <button class="btn-sec" onclick="downloadFile('eject')">Download</button>
        <button class="btn-del" onclick="delFile('eject')">Delete</button>
      </div>
      <div class="playrow">
        <button class="btn" onclick="play('eject')">▶ Play Eject</button>
        <button class="btn-sec" onclick="stopPlay()">■ Stop</button>
      </div>
      <div class="note">Saved as <code>/eject.mp3</code>.</div>
    </div>

    <div class="actions">
      <button onclick="location.href='/'" class="btn-sec">⟵ WiFi Setup</button>
      <button onclick="location.href='/ota'" class="btn-sec">OTA Update</button>
    </div>
    <div class="small" id="status"></div>
  </div>
</div></div>

<script>
function setStatus(t){document.getElementById('status').textContent=t;}

function refresh(){
  fetch('/api/files',{cache:'no-store'}).then(r=>r.json()).then(j=>{
    document.getElementById('used').textContent = j.used_h;
    document.getElementById('free').textContent = j.free_h;
    document.getElementById('bootInfo').textContent  = j.boot.exists ? (j.boot.size_h) : 'missing';
    document.getElementById('ejectInfo').textContent = j.eject.exists ? (j.eject.size_h): 'missing';
  }).catch(()=>setStatus('Failed to query storage.'));

  fetch('/api/vol',{cache:'no-store'}).then(r=>r.json()).then(j=>{
    // prefer "percent" if present; else convert
    let percent = (typeof j.percent === 'number') ? j.percent :
                  (typeof j.vol === 'number') ? Math.round((j.vol/255)*100) : 80;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const s = document.getElementById('vol');
    const vv= document.getElementById('volv');
    s.value = percent; vv.textContent = percent + '%';
  }).catch(()=>0);

  fetch('/api/boot_pref',{cache:'no-store'}).then(r=>r.json()).then(j=>{
    const on = !!j.enabled;
    document.getElementById('bootState').textContent = on ? 'Enabled' : 'Disabled';
    document.getElementById('bootBtn').textContent   = on ? 'Disable Boot Sound' : 'Enable Boot Sound';
    document.getElementById('bootBtn').dataset.next  = on ? '0' : '1';
  }).catch(()=>setStatus('Failed to read boot sound setting.'));

  fetch('/api/eject_pref',{cache:'no-store'}).then(r=>r.json()).then(j=>{
    const on = !!j.enabled;
    document.getElementById('ejectState').textContent = on ? 'Enabled' : 'Disabled';
    document.getElementById('ejectBtn').textContent   = on ? 'Disable Eject Sound' : 'Enable Eject Sound';
    document.getElementById('ejectBtn').dataset.next  = on ? '0' : '1';
  }).catch(()=>setStatus('Failed to read eject sound setting.'));
}

function onVolSlide(v){
  document.getElementById('volv').textContent = v + '%';
}

let volCommitTimer = null;
function commitVol(v){
  const scaled = Math.round((v/100)*255);
  if (volCommitTimer) clearTimeout(volCommitTimer);
  volCommitTimer = setTimeout(()=>{
    fetch('/api/vol?val='+encodeURIComponent(scaled), {method:'POST'})
      .then(r=>r.json()).then(j=>{
        const p = (typeof j.percent==='number') ? j.percent :
                  Math.round((j.vol/255)*100);
        setStatus(j.ok ? ('Volume set to '+p+'%') : ('Volume change failed'));
      }).catch(()=>setStatus('Volume change failed (network).'));
  }, 120);
}

function upload(slot){
  const inp = document.getElementById(slot==='boot'?'bootFile':'ejectFile');
  if(!inp.files || !inp.files[0]) { setStatus('Please choose an MP3 file.'); return; }
  const f = inp.files[0];
  if(!f.name.toLowerCase().endsWith('.mp3')){ setStatus('Only .mp3 files are allowed.'); return; }
  setStatus('Uploading '+f.name+' …');
  const xhr = new XMLHttpRequest();
  xhr.open('POST','/api/upload?slot='+encodeURIComponent(slot),true);
  xhr.onload = function(){
    try{
      const j = JSON.parse(xhr.responseText||'{}');
      setStatus(j.ok ? 'Upload complete.' : ('Upload failed: '+(j.err||'unknown')));
    }catch(e){ setStatus('Upload status unknown.'); }
    refresh();
  };
  const form = new FormData();
  form.append('file', f, f.name);
  xhr.send(form);
}

function delFile(slot){
  if(!confirm('Delete '+slot+' MP3?')) return;
  fetch('/api/delete?slot='+encodeURIComponent(slot),{method:'POST'}).then(r=>r.json()).then(j=>{
    setStatus(j.ok?'Deleted.':('Delete failed: '+(j.err||'unknown'))); refresh();
  }).catch(()=>setStatus('Delete failed (network).'));
}

function downloadFile(slot){
  window.location = '/api/download?slot='+encodeURIComponent(slot);
}

function play(slot){
  fetch('/api/play?slot='+encodeURIComponent(slot), {cache:'no-store'})
    .then(r=>r.json()).then(j=>{
      setStatus(j.ok ? ('Playing '+slot+'…') : (j.err ? ('Play failed: '+j.err) : 'Play failed (missing file?)'));
    }).catch(()=>setStatus('Play failed (network).'));
}

function stopPlay(){
  fetch('/api/stop', {method:'POST'}).then(r=>r.json()).then(j=>{
    setStatus(j.ok ? 'Stopped.' : 'Stop failed.');
  }).catch(()=>setStatus('Stop failed (network).'));
}

function toggleBoot(){
  const btn = document.getElementById('bootBtn');
  const next = btn.dataset.next || '0';
  fetch('/api/boot_pref?enabled='+encodeURIComponent(next), {method:'POST'})
    .then(r=>r.json()).then(j=>{
      if(j.ok){ setStatus('Boot sound '+(j.enabled?'enabled':'disabled')); refresh(); }
      else setStatus('Failed to change boot setting');
    }).catch(()=>setStatus('Network error'));
}

function toggleEject(){
  const btn = document.getElementById('ejectBtn');
  const next = btn.dataset.next || '0';
  fetch('/api/eject_pref?enabled='+encodeURIComponent(next), {method:'POST'})
    .then(r=>r.json()).then(j=>{
      if(j.ok){ setStatus('Eject sound '+(j.enabled?'enabled':'disabled')); refresh(); }
      else setStatus('Failed to change eject setting');
    }).catch(()=>setStatus('Network error'));
}

refresh();
</script>
</body></html>
)HTML";

// -------------- Helpers --------------
static String slotToPath(const String& slot) {
  if (slot == "boot")  return String(kBootPath);
  if (slot == "eject") return String(kEjectPath);
  return String();
}

// -------------- REST: list --------------
static void handleList(AsyncWebServerRequest* req) {
  uint64_t total = SPIFFS.totalBytes();
  uint64_t used  = SPIFFS.usedBytes();
  uint64_t freeb = (total > used) ? (total - used) : 0;

  struct Info { bool exists; uint64_t size; };
  Info boot{false,0}, eject{false,0};

  File f;
  if ((f = SPIFFS.open(kBootPath, "r"))) { boot.exists = true; boot.size = f.size(); f.close(); }
  if ((f = SPIFFS.open(kEjectPath,"r"))) { eject.exists = true; eject.size= f.size(); f.close(); }

  String j = "{";
  j += "\"used\":" + String((uint32_t)used) + ",";
  j += "\"free\":" + String((uint32_t)freeb) + ",";
  j += "\"used_h\":\"" + jsonEscape(humanSize(used)) + "\",";
  j += "\"free_h\":\"" + jsonEscape(humanSize(freeb)) + "\",";
  j += "\"boot\":{\"exists\":" + String(boot.exists?"true":"false") + ",\"size\":" + String((uint32_t)boot.size) + ",\"size_h\":\"" + jsonEscape(humanSize(boot.size)) + "\"},";
  j += "\"eject\":{\"exists\":" + String((eject.exists?"true":"false")) + ",\"size\":" + String((uint32_t)eject.size) + ",\"size_h\":\"" + jsonEscape(humanSize(eject.size)) + "\"}";
  j += "}";
  auto* resp = req->beginResponse(200, "application/json", j);
  addNoStore(resp);
  req->send(resp);
}

// -------------- REST: download --------------
static void handleDownload(AsyncWebServerRequest* req) {
  if (!req->hasParam("slot")) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot param\"}");
    return;
  }
  String p = slotToPath(req->getParam("slot")->value());
  if (!p.length() || !SPIFFS.exists(p)) {
    req->send(404, "application/json", "{\"ok\":false,\"err\":\"not found\"}");
    return;
  }

  AsyncWebServerResponse* resp = req->beginResponse(SPIFFS, p, "audio/mpeg", /*download*/ true);
  const char* fname = (p == String("/boot.mp3")) ? "boot.mp3" : "eject.mp3";
  resp->addHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
  addNoStore(resp);
  req->send(resp);
}

// -------------- REST: delete --------------
static void handleDelete(AsyncWebServerRequest* req) {
  if (!req->hasParam("slot")) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot param\"}"); return; }
  String p = slotToPath(req->getParam("slot")->value());
  if (!p.length()) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad slot\"}"); return; }
  bool ok = SPIFFS.exists(p) ? SPIFFS.remove(p) : true;
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// -------------- REST: upload (multipart) --------------
static void handleUploadCompleted(AsyncWebServerRequest* request, bool ok, const char* errMsg) {
  String body = ok ? "{\"ok\":true}" : (String("{\"ok\":false,\"err\":\"") + (errMsg?errMsg:"fail") + "\"}");
  auto* resp = request->beginResponse(ok?200:400, "application/json", body);
  resp->addHeader("Cache-Control", "no-store");
  request->send(resp);
}

static void handleUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  static File out;
  static String targetPath;
  static size_t written = 0;
  static bool ok = true;
  static String err;

  if (index == 0) {
    ok = true; err = ""; written = 0; targetPath = "";
    
    // Get the slot (boot or eject)
    if (!request->hasParam("slot")) { 
      ok = false; 
      err = "slot param"; 
    } else {
      String slot = request->getParam("slot")->value();
      targetPath = slotToPath(slot);
      if (!targetPath.length()) { 
        ok = false; 
        err = "bad slot"; 
      }
    }
    
    // Check file extension - must be .mp3
    String lower = filename; 
    lower.toLowerCase();
    if (ok && !lower.endsWith(".mp3")) { 
      ok = false; 
      err = "only .mp3 files allowed"; 
    }

    // Check available space
    if (ok) {
      uint64_t total = SPIFFS.totalBytes();
      uint64_t used = SPIFFS.usedBytes();
      if (total - used < (uint64_t)len + 4096) { 
        ok = false; 
        err = "not enough space"; 
      }
    }
    
    if (ok) {
      // Remove existing file at target path
      if (SPIFFS.exists(targetPath)) {
        SPIFFS.remove(targetPath);
      }
      
      // Open file at TARGET path (e.g. /boot.mp3 or /eject.mp3)
      // This automatically renames any uploaded file to the correct name
      out = SPIFFS.open(targetPath, "w");
      if (!out) { 
        ok = false; 
        err = "failed to create file"; 
      } else {
        Serial.printf("[FileMan] Uploading to: %s (original: %s)\n", 
                     targetPath.c_str(), filename.c_str());
      }
    }
  }

  if (ok && len) {
    if (written + len > kMaxUploadBytes) { 
      ok = false; 
      err = "file too large"; 
    } else if (out.write(data, len) != len) { 
      ok = false; 
      err = "write failed"; 
    } else {
      written += len;
    }
  }

  if (final) {
    if (out) {
      out.close();
      if (ok) {
        Serial.printf("[FileMan] Upload complete: %u bytes written to %s\n", 
                     written, targetPath.c_str());
      }
    }
    handleUploadCompleted(request, ok, ok ? nullptr : err.c_str());
    targetPath = ""; 
    written = 0; 
    ok = true; 
    err = "";
  }
}

static void handleVolGet(AsyncWebServerRequest* req) {
  int percent = (int)round((g_volume / 255.0f) * 100.0f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  String body = String("{\"vol\":") + String((int)g_volume) +
                ",\"percent\":" + String(percent) + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

static void handleVolSet(AsyncWebServerRequest* req) {
  // accept query (?val=), x-www-form-urlencoded (val), or JSON {"val":N}
  // NOTE: val is still in RAW 0..255 units from the UI (we convert there)
  int vParsed = -1;

  if (req->hasParam("val")) {
    vParsed = req->getParam("val")->value().toInt();
  } else if (req->hasParam("plain")) {
    const String body = req->getParam("plain")->value();
    vParsed = parseJsonIntField(body, "val", -1);
  }

  if (vParsed < 0) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"val param\"}");
    return;
  }

  if (vParsed < 0) vParsed = 0;
  if (vParsed > 255) vParsed = 255;

  AudioPlayer::setVolume((uint8_t)vParsed);
  fmVolumeWrite((uint8_t)vParsed);   // persist volume (throttled)

  int percent = (int)round((vParsed / 255.0f) * 100.0f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  String body = String("{\"ok\":true,\"vol\":") + String(vParsed) +
                ",\"percent\":" + String(percent) + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

// -------------- REST: play/stop --------------
// NOTE: these now ENQUEUE commands so the audio decoder is only touched
// from the Arduino loop task. This avoids cross-task heap races.
static void handlePlay(AsyncWebServerRequest* req) {
  if (!req->hasParam("slot")) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot param\"}");
    return;
  }
  String slot = req->getParam("slot")->value();
  String path = slotToPath(slot);
  if (!path.length()) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad slot\"}");
    return;
  }

  // CHECK: Boot sound enabled flag
  if (slot == "boot" && !g_bootEnabled) {
    req->send(200, "application/json", "{\"ok\":false,\"err\":\"boot sound disabled\"}");
    return;
  }

  // CHECK: Eject sound enabled flag
  if (slot == "eject" && !g_ejectEnabled) {
    req->send(200, "application/json", "{\"ok\":false,\"err\":\"eject sound disabled\"}");
    return;
  }

  if (!SPIFFS.exists(path)) {
    req->send(404, "application/json", "{\"ok\":false,\"err\":\"missing file\"}");
    return;
  }

  // Enqueue command for Audio task
  if (slot == "boot")      AudioPlayer::enqueue(AudioPlayer::Cmd::PlayBoot);
  else /* eject */         AudioPlayer::enqueue(AudioPlayer::Cmd::PlayEject);

  req->send(200, "application/json", "{\"ok\":true}");
}

static void handleStop(AsyncWebServerRequest* req) {
  AudioPlayer::enqueue(AudioPlayer::Cmd::Stop);
  req->send(200, "application/json", "{\"ok\":true}");
}

// -------------- REST: boot/eject sound prefs --------------
static void handleBootPrefGet(AsyncWebServerRequest* req) {
  String body = String("{\"enabled\":") + (g_bootEnabled ? "true" : "false") + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

static void handleBootPrefSet(AsyncWebServerRequest* req) {
  if (!req->hasParam("enabled")) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"enabled param\"}");
    return;
  }
  const bool en = (req->getParam("enabled")->value().toInt() != 0);
  fmBootSoundWrite(en);
  String body = String("{\"ok\":true,\"enabled\":") + (en ? "true" : "false") + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

static void handleEjectPrefGet(AsyncWebServerRequest* req) {
  String body = String("{\"enabled\":") + (g_ejectEnabled ? "true" : "false") + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

static void handleEjectPrefSet(AsyncWebServerRequest* req) {
  if (!req->hasParam("enabled")) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"enabled param\"}");
    return;
  }
  const bool en = (req->getParam("enabled")->value().toInt() != 0);
  fmEjectSoundWrite(en);
  String body = String("{\"ok\":true,\"enabled\":") + (en ? "true" : "false") + "}";
  auto* resp = req->beginResponse(200, "application/json", body);
  addNoStore(resp);
  req->send(resp);
}

// -------------- Route registration --------------
static void registerRoutes(AsyncWebServer& server) {
  // Main UI
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* resp = req->beginResponse_P(200, "text/html", FILES_PAGE);
    addNoStore(resp);
    req->send(resp);
  });

  // File APIs
  server.on("/api/files",    HTTP_GET,  [](AsyncWebServerRequest* r){ handleList(r);     });
  server.on("/api/download", HTTP_GET,  [](AsyncWebServerRequest* r){ handleDownload(r); });
  server.on("/api/delete",   HTTP_POST, [](AsyncWebServerRequest* r){ handleDelete(r);   });

  // Upload (multipart). Response is sent in upload handler at final=true.
  server.on("/api/upload", HTTP_POST,
    [](AsyncWebServerRequest* request){ /* response is sent in upload callback */ },
    handleUpload
  );

  // Volume + audio control
  server.on("/api/vol",        HTTP_GET,  [](AsyncWebServerRequest* r){ handleVolGet(r);      });
  server.on("/api/vol",        HTTP_POST, [](AsyncWebServerRequest* r){ handleVolSet(r);      });
  server.on("/api/play",       HTTP_GET,  [](AsyncWebServerRequest* r){ handlePlay(r);        });
  server.on("/api/stop",       HTTP_POST, [](AsyncWebServerRequest* r){ handleStop(r);        });

  // Boot/Eject sound prefs
  server.on("/api/boot_pref",  HTTP_GET,  [](AsyncWebServerRequest* r){ handleBootPrefGet(r); });
  server.on("/api/boot_pref",  HTTP_POST, [](AsyncWebServerRequest* r){ handleBootPrefSet(r); });
  server.on("/api/eject_pref", HTTP_GET,  [](AsyncWebServerRequest* r){ handleEjectPrefGet(r); });
  server.on("/api/eject_pref", HTTP_POST, [](AsyncWebServerRequest* r){ handleEjectPrefSet(r); });
}

namespace FileMan {
  void begin() {
    SPIFFS.begin(true);

    // Load cached prefs once
    fmPrefsReadAll();
    if (g_volume > 255) g_volume = 255;
    
    // Sync all settings with AudioPlayer
    AudioPlayer::setVolume(g_volume);
    AudioPlayer::setBootEnabled(g_bootEnabled);
    AudioPlayer::setEjectEnabled(g_ejectEnabled);

    AsyncWebServer& server = WiFiMgr::getServer();
    registerRoutes(server);
  }
}
