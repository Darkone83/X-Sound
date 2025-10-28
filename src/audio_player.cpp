// audio_player.cpp — updated to reflect real Wi-Fi status for idle LED
#include "audio_player.h"

#include <FS.h>
#include <SPIFFS.h>
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#include "led_stat.h"
#include "wifimgr.h"   // NEW: query WiFiMgr::isConnected() for LED idle state

// Default sound paths (match FileMan)
static const char* kBootPath  = "/boot.mp3";
static const char* kEjectPath = "/eject.mp3";

// Audio objects (owned by the main loop only)
static AudioFileSourceFS* fileSrc = nullptr;
static AudioGeneratorMP3* mp3     = nullptr;
static AudioOutputI2S*    out     = nullptr;

// I2S pin config (from begin)
static int g_bclk = -1, g_lrck = -1, g_dout = -1;

// Volume (0..255)
static uint8_t g_vol = 200;

// Enable flags for boot/eject sounds
static bool g_bootEnabled = true;
static bool g_ejectEnabled = true;

// --- Single-threaded command handoff ---
static volatile AudioPlayer::Cmd g_pendingCmd = AudioPlayer::Cmd::None;

// Map 0..255 -> a linear-ish gain (0.0 .. ~1.0)
static float volToGain(uint8_t v) {
  return (float)v * (1.0f / 255.0f);
}

// Helper: set idle LED based on Wi-Fi reality (connected → green, else portal purple)
static void setIdleLedByWifi() {
  if (WiFiMgr::isConnected()) {        // uses WiFiMgr public API
    LedStat::setStatus(LedStatus::WifiConnected);
  } else {
    LedStat::setStatus(LedStatus::Portal);
  }
}

// Internal: cleanup after stop/end/error (loop-thread only)
static void cleanupPlayer() {
  if (mp3) {
    mp3->stop();
    delete mp3; mp3 = nullptr;
  }
  if (fileSrc) {
    fileSrc->close();
    delete fileSrc; fileSrc = nullptr;
  }
}

// Internal: start playback of a path (loop-thread only)
static bool startPlayPath(const char* path) {
  cleanupPlayer();

  if (!SPIFFS.exists(path)) {
    LedStat::setStatus(LedStatus::Error);
    return false;
  }

  fileSrc = new AudioFileSourceFS(SPIFFS, path);
  if (!fileSrc) { LedStat::setStatus(LedStatus::Error); return false; }

  mp3 = new AudioGeneratorMP3();
  if (!mp3) { delete fileSrc; fileSrc = nullptr; LedStat::setStatus(LedStatus::Error); return false; }

  bool ok = mp3->begin(fileSrc, out);
  if (!ok) {
    cleanupPlayer();
    LedStat::setStatus(LedStatus::Error);
    return false;
  }

  LedStat::setStatus(LedStatus::Playing);
  return true;
}

// ---- Public API ----
namespace AudioPlayer {

void begin(int bclkPin, int lrclkPin, int doutPin) {
  g_bclk = bclkPin; g_lrck = lrclkPin; g_dout = doutPin;

  SPIFFS.begin(true);

  out = new AudioOutputI2S();
  if (out) {
    out->SetPinout(g_bclk, g_lrck, g_dout);
    out->SetChannels(1);
    out->SetGain(volToGain(g_vol));
  }

  // Old behavior hard-set WifiConnected here. Now we reflect actual status:
  setIdleLedByWifi();  // ✔ only green if Wi-Fi really connected, else portal purple
}

void setVolume(uint8_t v) {
  g_vol = v;
  if (out) out->SetGain(volToGain(g_vol));
}

uint8_t getVolume() { return g_vol; }

bool isPlaying() {
  return (mp3 && mp3->isRunning());
}

// Set boot sound enabled state (synced from FileMan)
void setBootEnabled(bool enabled) {
  g_bootEnabled = enabled;
  Serial.printf("[AudioPlayer] Boot sound %s\n", enabled ? "ENABLED" : "DISABLED");
}

// Set eject sound enabled state (synced from FileMan)
void setEjectEnabled(bool enabled) {
  g_ejectEnabled = enabled;
  Serial.printf("[AudioPlayer] Eject sound %s\n", enabled ? "ENABLED" : "DISABLED");
}

// --- Command helpers: enqueue only; loop() does the work ---
bool enqueue(Cmd c) {
  g_pendingCmd = c;
  return true;
}

bool playBoot()  { return enqueue(Cmd::PlayBoot); }
bool playEject() { return enqueue(Cmd::PlayEject); }
bool stop()      { return enqueue(Cmd::Stop);     }

// Main-thread pump
void loop() {
  // 1) If currently playing, drive the decoder
  if (mp3 && out && fileSrc) {
    if (!mp3->loop()) {
      cleanupPlayer();
      setIdleLedByWifi();  // ✔ when playback ends, reflect current Wi-Fi status
    }
  }

  // 2) Process exactly one pending command per loop tick (if any)
  AudioPlayer::Cmd cmd = g_pendingCmd;
  if (cmd != Cmd::None) {
    g_pendingCmd = Cmd::None;

    switch (cmd) {
      case Cmd::Stop: {
        cleanupPlayer();
        setIdleLedByWifi();  // ✔ stopping returns to real Wi-Fi status
        break;
      }
      case Cmd::PlayBoot: {
        if (!g_bootEnabled) {
          Serial.println("[AudioPlayer] Boot sound disabled, skipping playback");
          break;
        }
        if (mp3) cleanupPlayer();
        if (out) {
          (void)startPlayPath(kBootPath);
        }
        break;
      }
      case Cmd::PlayEject: {
        if (!g_ejectEnabled) {
          Serial.println("[AudioPlayer] Eject sound disabled, skipping playback");
          break;
        }
        if (mp3) cleanupPlayer();
        if (out) {
          (void)startPlayPath(kEjectPath);
        }
        break;
      }
      case Cmd::None:
      default:
        break;
    }
  }
}

} // namespace AudioPlayer
