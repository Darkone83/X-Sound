// X-Sound.ino — Waveshare ESP32-S3 integration
// - Plays /boot.mp3 once at power-up
// - Eject sound: /eject.mp3 on falling edge from Xbox EJECT line (active-LOW)
// - Keeps WiFiMgr (/ + /ota), FileMan (/files), LED status, and mDNS (xsound.local)

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>

#include "led_stat.h"
#include "wifimgr.h"
#include "fileman.h"
#include "audio_player.h"

// ======================== Board/Pins ========================
// Pick your Waveshare board mapping. Default = S3-Zero.
// If you're on the ESP32-S3-LCD-1.47 board, uncomment that block instead.

// ---- Waveshare ESP32-S3-Zero suggested pins ----
// All 3 are on the castellated headers and route fine via GPIO matrix.
#ifndef I2S_PIN_BCLK
  #define I2S_PIN_BCLK  12
#endif
#ifndef I2S_PIN_LRCK
  #define I2S_PIN_LRCK  11
#endif
#ifndef I2S_PIN_DOUT
  #define I2S_PIN_DOUT  10
#endif


// Xbox EJECT sense line (active-LOW). Use any free GPIO on your board header.
#ifndef PIN_EJECT_SENSE
  #define PIN_EJECT_SENSE  9   // change if you prefer another accessible pin
#endif

// If your tap floats, you can rely on ESP32's internal pull-up.
// For *maximum* safety against backfeeding the Xbox SMC, add a 100k–220k series
// resistor at the wire. Internal pull-up is ~45–50k to 3.3V.
#define USE_INTERNAL_PULLUP_FOR_EJECT  1

// Debounce and refire guards (ms)
#define EJECT_DEBOUNCE_MS  120
#define EJECT_REFIRE_MS    800

// ======================== mDNS ========================
static const char* FW_TAG = "X-Sound " __DATE__ " " __TIME__;

static void startMDNS(const char* host) {
  WiFi.setHostname(host);
  if (!MDNS.begin(host)) {
    Serial.println("[mDNS] Failed to start responder.");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  MDNS.addServiceTxt("http", "tcp", "fw", FW_TAG);
  Serial.printf("[mDNS] http://%s.local/\n", host);
}

// ======================== Eject ISR ========================
volatile bool g_wantEject = false;
static unsigned long g_lastEjectEdge = 0;
static unsigned long g_lastEjectFire = 0;

void IRAM_ATTR onEjectEdge() {
  const unsigned long now = millis();
  if (now - g_lastEjectEdge < EJECT_DEBOUNCE_MS) return;
  g_lastEjectEdge = now;
  // Falling edge = line pulled LOW
  if (digitalRead(PIN_EJECT_SENSE) == LOW) g_wantEject = true;
}

// ======================== Setup/Loop ========================
static void registerTestRoutes() {
  auto& server = WiFiMgr::getServer();
  server.on("/api/play", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!req->hasParam("slot")) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"slot\"}"); return; }
    String slot = req->getParam("slot")->value();
    bool ok = (slot=="boot") ? AudioPlayer::playBoot()
                             : (slot=="eject") ? AudioPlayer::playEject() : false;
    req->send(ok?200:404, "application/json", ok?"{\"ok\":true}":"{\"ok\":false}");
  });
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* req){
    AudioPlayer::stop(); req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/audio_status", HTTP_GET, [](AsyncWebServerRequest* req){
    String s = String("{\"playing\":") + (AudioPlayer::isPlaying()?"true":"false") + "}";
    req->send(200, "application/json", s);
  });
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(); Serial.println(FW_TAG);

  // FS is idempotent; FileMan::begin() also ensures mount
  SPIFFS.begin(true);

  LedStat::begin();

  // WiFi + server + portal + OTA
  WiFiMgr::begin();

  // File manager (/files)
  FileMan::begin();

  // Audio (I2S + Helix MP3)
  AudioPlayer::begin(I2S_PIN_BCLK, I2S_PIN_LRCK, I2S_PIN_DOUT);

  // --- Eject sense input ---
  // Prefer INPUT (true high-Z). If your wire floats/noisy, enable internal PULLUP.
  #if USE_INTERNAL_PULLUP_FOR_EJECT
    pinMode(PIN_EJECT_SENSE, INPUT_PULLUP);
  #else
    pinMode(PIN_EJECT_SENSE, INPUT);
  #endif
  attachInterrupt(digitalPinToInterrupt(PIN_EJECT_SENSE), onEjectEdge, FALLING);

  // Simple test routes
  registerTestRoutes();

  // mDNS for xsound.local
  startMDNS("xsound");

  // ---- Boot sound on power-up ----
  // Small delay so the amp/DAC settles before first frames
  delay(150);
  (void)AudioPlayer::playBoot();

  Serial.println("[Setup] Ready — upload /boot.mp3 and /eject.mp3 at /files, test with /api/play.");
}

void loop() {
  WiFiMgr::loop();
  LedStat::loop();
  AudioPlayer::loop();

  // EJECT handling with refire guard
  const unsigned long now = millis();
  if (g_wantEject && (now - g_lastEjectFire > EJECT_REFIRE_MS)) {
    g_wantEject = false;
    g_lastEjectFire = now;
    (void)AudioPlayer::playEject();
  }
  // MDNS.update(); // optional
}
