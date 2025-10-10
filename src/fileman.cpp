#include "fileman.h"
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "wifimgr.h"
#include "audio_player.h"   // ⬅️ to set/get volume

// -------- Settings --------
static const char* kBootPath  = "/boot.mp3";
static const char* kEjectPath = "/eject.mp3";
static const size_t kMaxUploadBytes = 6 * 1024 * 1024; // safety cap

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
<title>File Manager</title>
<style>
:root{--bg:#111;--card:#222;--ink:#EEE;--mut:#AAB;--pri:#299a2c;--warn:#a22;--link:#9ec1ff;--btn:#2563eb}
*{box-sizing:border-box} html,body{height:100%}
body{background:var(--bg);color:var(--ink);font-family:system-ui,Segoe UI,Roboto,Arial;margin:0}
.wrap{min-height:100%;display:flex;align-items:center;justify-content:center;padding:env(safe-area-inset-top) 12px env(safe-area-inset-bottom)}
.card{width:100%;max-width:520px;margin:16px auto;background:var(--card);padding:18px;border-radius:12px;box-shadow:0 8px 20px #0008}
h1{margin:.2rem 0 1rem;font-size:1.45rem}
.grid{display:grid;grid-template-columns:1fr;gap:.7rem}
.row{display:grid;grid-template-columns:1fr 1fr;gap:.5rem;align-items:center}
input[type=file],button{width:100%;padding:.7rem .8rem;border-radius:9px;border:1px solid #555;background:#111;color:var(--ink);font-size:1rem}
button{cursor:pointer}
.btn{background:var(--btn);border:0;color:#fff}
.btn-del{background:var(--warn)}
.btn-sec{background:#3d3d7a}
.small{font-size:.92rem;color:var(--mut)}
.kv{display:flex;justify-content:space-between;font-size:.95rem;background:#161616;border:1px solid #444;padding:.6rem .7rem;border-radius:9px}
.group{padding:.7rem;border:1px solid #444;border-radius:12px;background:#171717}
.actions{display:flex;gap:.5rem;flex-wrap:wrap}
.note{color:var(--mut);font-size:.92rem;margin-top:.4rem}
.link{color:var(--link);text-decoration:none}
hr{border:none;height:1px;background:#333;margin:.8rem 0}
.playrow{display:flex;gap:.5rem;flex-wrap:wrap;margin-top:.4rem}
.sliderrow{display:grid;grid-template-columns:1fr auto;gap:.6rem;align-items:center;margin-top:.6rem}
.valuechip{background:#0f0f0f;border:1px solid #444;border-radius:9px;padding:.4rem .6rem;font-variant-numeric:tabular-nums}
</style></head>
<body>
<div class="wrap"><div class="card">
  <h1>File Manager</h1>
  <div class="grid">

    <!-- Volume -->
    <div class="group">
      <div class="kv"><strong>Volume</strong><span class="small">0–255</span></div>
      <div class="sliderrow">
        <input id="vol" type="range" min="0" max="255" step="1" value="200" oninput="onVolSlide(this.value)" onchange="commitVol(this.value)">
        <div class="valuechip"><span id="volv">200</span></div>
      </div>
      <div class="note">Adjust output gain in real time.</div>
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
    const v = (typeof j.vol==='number') ? j.vol : 200;
    const s = document.getElementById('vol');
    const vv= document.getElementById('volv');
    s.value = v; vv.textContent = v;
  }).catch(()=>0);
}

function onVolSlide(v){
  document.getElementById('volv').textContent = v;
}

let volCommitTimer = null;
function commitVol(v){
  if (volCommitTimer) clearTimeout(volCommitTimer);
  volCommitTimer = setTimeout(()=>{
    fetch('/api/vol?val='+encodeURIComponent(v), {method:'POST'})
      .then(r=>r.json()).then(j=>{
        setStatus(j.ok ? ('Volume set to '+v) : ('Volume change failed'));
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
      setStatus(j.ok ? ('Playing '+slot+'…') : ('Play failed (missing file?)'));
    }).catch(()=>setStatus('Play failed (network).'));
}

function stopPlay(){
  fetch('/api/stop', {method:'POST'}).then(r=>r.json()).then(j=>{
    setStatus(j.ok ? 'Stopped.' : 'Stop failed.');
  }).catch(()=>setStatus('Stop failed (network).'));
}

refresh();
</script>
</body></html>
)HTML";

// -------------- Handlers --------------
static String slotToPath(const String& slot) {
  if (slot == "boot")  return String(kBootPath);
  if (slot == "eject") return String(kEjectPath);
  return String();
}

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
  j += "\"eject\":{\"exists\":" + String(eject.exists?"true":"false") + ",\"size\":" + String((uint32_t)eject.size) + ",\"size_h\":\"" + jsonEscape(humanSize(eject.size)) + "\"}";
  j += "}";
  auto* resp = req->beginResponse(200, "application/json", j);
  addNoStore(resp);
  req->send(resp);
}

static void handleDownload(AsyncWebServerRequest* req) {
  if (!req->hasParam("slot")) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot param\"}"); return; }
  String p = slotToPath(req->getParam("slot")->value());
  if (!p.length() || !SPIFFS.exists(p)) { req->send(404, "application/json", "{\"ok\":false,\"err\":\"not found\"}"); return; }
  AsyncWebServerResponse* resp = req->beginResponse(SPIFFS, p, "audio/mpeg", /*download*/ true);
  const char* fname = (p == kBootPath) ? "boot.mp3" : "eject.mp3";
  resp->addHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
  addNoStore(resp);
  req->send(resp);
}

static void handleDelete(AsyncWebServerRequest* req) {
  if (!req->hasParam("slot")) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot param\"}"); return; }
  String p = slotToPath(req->getParam("slot")->value());
  if (!p.length()) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad slot\"}"); return; }
  bool ok = SPIFFS.exists(p) ? SPIFFS.remove(p) : true;
  req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleUploadCompleted(AsyncWebServerRequest* request, bool ok, const char* errMsg) {
  String body = ok ? "{\"ok\":true}" : (String("{\"ok\":false,\"err\":\"") + (errMsg?errMsg:"fail") + "\"}");
  auto* resp = request->beginResponse(ok?200:400, "application/json", body);
  addNoStore(resp);
  request->send(resp);
}

static void registerRoutes(AsyncWebServer& server) {
  // Main UI
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* resp = req->beginResponse_P(200, "text/html", FILES_PAGE);
    addNoStore(resp);
    req->send(resp);
  });

  // API: list
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req){ handleList(req); });

  // API: download
  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest* req){ handleDownload(req); });

  // API: delete
  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest* req){ handleDelete(req); });

  // API: upload (multipart)
  server.on(
    "/api/upload",
    HTTP_POST,
    [](AsyncWebServerRequest* request){
      if (!request->hasParam("slot")) {
        handleUploadCompleted(request, false, "slot param");
      }
    },
    [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final){
      static File out;
      static String path;
      static size_t written = 0;
      static bool ok = true;
      static String err;

      if (index == 0) {
        if (!request->hasParam("slot")) { ok=false; err="slot param"; }
        else {
          String slot = request->getParam("slot")->value();
          path = slotToPath(slot);
          if (!path.length()) { ok=false; err="bad slot"; }
        }

        String lower = filename; lower.toLowerCase();
        if (ok && !lower.endsWith(".mp3")) { ok=false; err="only .mp3"; }

        if (ok) {
          uint64_t total = SPIFFS.totalBytes(), used=SPIFFS.usedBytes();
          if (total - used < (uint64_t)len + 4096) { ok=false; err="not enough space"; }
        }
        if (ok) {
          SPIFFS.remove(path);
          out = SPIFFS.open(path, "w");
          if (!out) { ok=false; err="open fail"; }
          written = 0;
        }
      }

      if (ok && len) {
        if (written + len > kMaxUploadBytes) { ok=false; err="file too large"; }
        else if (out.write(data, len) != len) { ok=false; err="write fail"; }
        else written += len;
      }

      if (final) {
        if (out) out.close();
        handleUploadCompleted(request, ok, ok?nullptr:err.c_str());
        path = ""; written = 0; ok = true; err="";
      }
    }
  );

  // ===== Volume API =====
  // GET  /api/vol        -> {"vol": N}
  // POST /api/vol?val=N  -> {"ok":true}
  server.on("/api/vol", HTTP_GET, [](AsyncWebServerRequest* req){
    uint8_t v = AudioPlayer::getVolume();
    String body = String("{\"vol\":") + String((int)v) + "}";
    auto* resp = req->beginResponse(200, "application/json", body);
    addNoStore(resp);
    req->send(resp);
  });

  server.on("/api/vol", HTTP_POST, [](AsyncWebServerRequest* req){
    if (!req->hasParam("val")) {
      req->send(400, "application/json", "{\"ok\":false,\"err\":\"val param\"}");
      return;
    }
    int v = req->getParam("val")->value().toInt();
    if (v < 0) v = 0; if (v > 255) v = 255;
    AudioPlayer::setVolume((uint8_t)v);
    req->send(200, "application/json", "{\"ok\":true}");
  });
}

namespace FileMan {
  void begin() {
    // Ensure SPIFFS is mounted; if WiFiMgr mounted earlier, this will no-op.
    SPIFFS.begin(true);
    AsyncWebServer& server = WiFiMgr::getServer();
    registerRoutes(server);
  }
}
