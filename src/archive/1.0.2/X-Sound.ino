// X-Sound.ino — Waveshare ESP32-S3 integration
// - Plays /boot.mp3 once at power-up (before WiFi init)
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
#ifndef I2S_PIN_BCLK
  #define I2S_PIN_BCLK  12
#endif
#ifndef I2S_PIN_LRCK
  #define I2S_PIN_LRCK  11
#endif
#ifndef I2S_PIN_DOUT
  #define I2S_PIN_DOUT  10
#endif

#ifndef PIN_EJECT_SENSE
  #define PIN_EJECT_SENSE  9
#endif

#define USE_INTERNAL_PULLUP_FOR_EJECT  1
#define EJECT_DEBOUNCE_MS  120
#define EJECT_REFIRE_MS    800

// ======================== mDNS ========================
static const char* HOSTNAME = "xsound";
static const char* FW_TAG   = "X-Sound " __DATE__ " " __TIME__;
static bool g_mdnsStarted   = false;

static void startMDNSIfNeeded() {
  if (g_mdnsStarted) return;

  WiFi.setHostname(HOSTNAME);
#ifdef CONFIG_IDF_TARGET_ESP32S3
  // On ESP32-S3 Arduino core, WiFi.setHostname sets both AP/STA hostnames.
#endif

  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("[mDNS] Failed to start responder (will retry later).");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  MDNS.addServiceTxt("http", "tcp", "fw", FW_TAG);
  g_mdnsStarted = true;
  Serial.printf("[mDNS] started: http://%s.local/\n", HOSTNAME);
}

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[mDNS] STA got IP: %s — ensure mDNS\n",
                    WiFi.localIP().toString().c_str());
      startMDNSIfNeeded();
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("[mDNS] AP started — ensure mDNS");
      startMDNSIfNeeded();
      break;
    default:
      break;
  }
}

// ======================== Eject ISR ========================
volatile bool g_wantEject = false;
static unsigned long g_lastEjectEdge = 0;
static unsigned long g_lastEjectFire = 0;

void IRAM_ATTR onEjectEdge() {
  const unsigned long now = millis();
  if (now - g_lastEjectEdge < EJECT_DEBOUNCE_MS) return;
  g_lastEjectEdge = now;
  if (digitalRead(PIN_EJECT_SENSE) == LOW) g_wantEject = true;
}

// ======================== Setup/Loop ========================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(); 
  Serial.println(FW_TAG);

  // ---- Base services ----
  SPIFFS.begin(true);
  LedStat::begin();

  // ---- Audio before FileMan ----
  AudioPlayer::begin(I2S_PIN_BCLK, I2S_PIN_LRCK, I2S_PIN_DOUT);

  // ---- File manager (routes /files etc.) ----
  FileMan::begin();

  // ---- Eject button ----
  #if USE_INTERNAL_PULLUP_FOR_EJECT
    pinMode(PIN_EJECT_SENSE, INPUT_PULLUP);
  #else
    pinMode(PIN_EJECT_SENSE, INPUT);
  #endif
  attachInterrupt(digitalPinToInterrupt(PIN_EJECT_SENSE), onEjectEdge, FALLING);

  // ---- Play boot sound before WiFi brings up tasks ----
  delay(150);
  AudioPlayer::playBoot();

  // ---- WiFi and mDNS ----
  WiFi.onEvent(onWiFiEvent);
  WiFiMgr::begin();
  startMDNSIfNeeded();

  Serial.println("[Setup] Ready — upload /boot.mp3 and /eject.mp3 at /files.");
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
    AudioPlayer::playEject();
  }

  if (!g_mdnsStarted) startMDNSIfNeeded();
}
