#include "audio_player.h"

#include <FS.h>
#include <SPIFFS.h>
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#include "led_stat.h"   // For LedStatus::Playing / Error / WifiConnected

// Default sound paths (match FileMan)
static const char* kBootPath  = "/boot.mp3";
static const char* kEjectPath = "/eject.mp3";

// Audio objects
static AudioFileSourceFS* fileSrc = nullptr;
static AudioGeneratorMP3* mp3     = nullptr;
static AudioOutputI2S*    out     = nullptr;

// I2S pin config (from begin)
static int g_bclk = -1, g_lrclk = -1, g_dout = -1;

// Volume (0..255) -> I2S gain
static uint8_t g_vol = 200;

// Map 0..255 -> 0.0 .. ~2.0 gain (linear)
static float volToGain(uint8_t v) {
  // Keep a sane upper bound to avoid clipping; tweak if needed
  return (float)v * (2.0f / 255.0f);
}

namespace AudioPlayer {

void begin(int bclkPin, int lrclkPin, int doutPin) {
  g_bclk = bclkPin; g_lrclk = lrclkPin; g_dout = doutPin;

  // Prepare SPIFFS if not already (safe if already mounted)
  SPIFFS.begin(true);

  // Create output once; reuse across tracks
  out = new AudioOutputI2S();
  // I2S pinning (ESP32)
  out->SetPinout(g_bclk, g_lrclk, g_dout);
  out->SetChannels(1);            // mono
  out->SetGain(volToGain(g_vol)); // initial gain

  // Idle LED
  LedStat::setStatus(LedStatus::WifiConnected);
}

void setVolume(uint8_t v) {
  g_vol = v;
  if (out) out->SetGain(volToGain(g_vol));
}

uint8_t getVolume() { return g_vol; }

// Internal: cleanup after stop/end/error
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

bool playFile(const char* path) {
  // Ensure any current playback is stopped
  cleanupPlayer();

  if (!SPIFFS.exists(path)) {
    LedStat::setStatus(LedStatus::Error);
    return false;
  }

  fileSrc = new AudioFileSourceFS(SPIFFS, path);
  mp3     = new AudioGeneratorMP3();

  // Start decoder
  bool ok = mp3->begin(fileSrc, out);
  if (!ok) {
    cleanupPlayer();
    LedStat::setStatus(LedStatus::Error);
    return false;
  }

  // Indicate now playing
  LedStat::setStatus(LedStatus::Playing);
  return true;
}

bool playBoot()  { return playFile(kBootPath);  }
bool playEject() { return playFile(kEjectPath); }

void stop() {
  cleanupPlayer();
  // Back to idle/ready (Wi-Fi connected state color)
  LedStat::setStatus(LedStatus::WifiConnected);
}

bool isPlaying() {
  return (mp3 && mp3->isRunning());
}

void loop() {
  // If nothing playing, nothing to do
  if (!mp3) return;

  // Drive the decoder; false => finished or error
  if (!mp3->loop()) {
    // Either finished cleanly or encountered an error.
    // Clean up and return to idle state.
    cleanupPlayer();
    LedStat::setStatus(LedStatus::WifiConnected);
  }
}

} // namespace AudioPlayer
