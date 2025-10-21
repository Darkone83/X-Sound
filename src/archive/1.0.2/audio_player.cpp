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

// Audio objects (owned by the main loop only)
static AudioFileSourceFS* fileSrc = nullptr;
static AudioGeneratorMP3* mp3     = nullptr;
static AudioOutputI2S*    out     = nullptr;

// I2S pin config (from begin)
static int g_bclk = -1, g_lrck = -1, g_dout = -1;

// Volume (0..255)
static uint8_t g_vol = 200;

// --- Single-threaded command handoff ---
// HTTP handlers and other tasks only *set* this; the main loop consumes it.
static volatile AudioPlayer::Cmd g_pendingCmd = AudioPlayer::Cmd::None;

// Map 0..255 -> a linear-ish gain (0.0 .. ~1.0)
// Keep conservative headroom to avoid clipping.
static float volToGain(uint8_t v) {
  return (float)v * (1.0f / 255.0f);
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
  // Ensure any current playback is stopped on this thread
  cleanupPlayer();

  if (!SPIFFS.exists(path)) {
    LedStat::setStatus(LedStatus::Error);
    return false;
  }

  // Allocate only here (never in HTTP callbacks)
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

  // Prepare SPIFFS if not already (safe if already mounted)
  SPIFFS.begin(true);

  // Create output once; reuse across tracks
  out = new AudioOutputI2S();
  if (out) {
    out->SetPinout(g_bclk, g_lrck, g_dout);
    out->SetChannels(1);            // mono
    out->SetGain(volToGain(g_vol)); // initial gain
  }

  // Idle LED
  LedStat::setStatus(LedStatus::WifiConnected);
}

void setVolume(uint8_t v) {
  g_vol = v;
  if (out) out->SetGain(volToGain(g_vol));
}

uint8_t getVolume() { return g_vol; }

bool isPlaying() {
  return (mp3 && mp3->isRunning());
}

// --- Command helpers: enqueue only; loop() does the work ---
bool enqueue(Cmd c) {
  g_pendingCmd = c;   // last command wins
  return true;
}

bool playBoot()  { return enqueue(Cmd::PlayBoot); }
bool playEject() { return enqueue(Cmd::PlayEject); }
bool stop()      { return enqueue(Cmd::Stop);     }

// Main-thread pump
void loop() {
  // 1) If currently playing, drive the decoder
  if (mp3) {
    if (!mp3->loop()) {
      // Finished or error
      cleanupPlayer();
      LedStat::setStatus(LedStatus::WifiConnected);
    }
  }

  // 2) Process exactly one pending command per loop tick (if any)
  AudioPlayer::Cmd cmd = g_pendingCmd;
  if (cmd != Cmd::None) {
    // Consume it
    g_pendingCmd = Cmd::None;

    switch (cmd) {
      case Cmd::Stop: {
        cleanupPlayer();
        LedStat::setStatus(LedStatus::WifiConnected);
        break;
      }
      case Cmd::PlayBoot: {
        // If a track is playing, stop first (same thread, safe)
        if (mp3) cleanupPlayer();
        (void)startPlayPath(kBootPath);
        break;
      }
      case Cmd::PlayEject: {
        if (mp3) cleanupPlayer();
        (void)startPlayPath(kEjectPath);
        break;
      }
      case Cmd::None:
      default:
        break;
    }
  }
}

} // namespace AudioPlayer
