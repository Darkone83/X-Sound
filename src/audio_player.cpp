#include "audio_player.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <FS.h>

// Helix MP3 via ESP8266Audio (works on ESP32/ESP32-S3)
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

namespace {
  AudioFileSourceFS*  g_src  = nullptr;   // file source (SPIFFS)
  AudioGeneratorMP3*  g_mp3  = nullptr;   // helix mp3 decoder
  AudioOutputI2S*     g_i2s  = nullptr;   // i2s sink

  uint8_t g_volume = 200;                 // 0..255

  // Clean up current playback chain, keep I2S alive between tracks
  void cleanupPlayback() {
    if (g_mp3) { g_mp3->stop(); delete g_mp3; g_mp3 = nullptr; }
    if (g_src) { delete g_src; g_src = nullptr; }
  }
}

namespace AudioPlayer {

void begin(int pin_bclk, int pin_lrck, int pin_dout, uint8_t volume_0_255) {
  g_volume = volume_0_255;

  // Mount filesystem (idempotent in case it’s already mounted)
  SPIFFS.begin(true);

  // Create I2S output: 44.1 kHz, stereo
  g_i2s = new AudioOutputI2S();
  g_i2s->SetPinout(pin_bclk, pin_lrck, pin_dout);
  g_i2s->SetChannels(2);          // stereo; mono MP3s mirror to both
  g_i2s->SetRate(44100);          // standard CD rate
  g_i2s->SetOutputModeMono(false);
  g_i2s->SetGain(g_volume / 255.0f);
}

bool playPath(const char* path, bool stop_current) {
  if (!path || !*path) return false;
  if (!SPIFFS.exists(path)) return false;

  if (g_mp3 && !stop_current) return false;
  cleanupPlayback();

  // File source: pass FS explicitly (avoids global SPIFFS header issue on some cores)
  g_src = new AudioFileSourceFS(SPIFFS, path);
  if (!g_src) { cleanupPlayback(); return false; }

  // Decoder
  g_mp3 = new AudioGeneratorMP3();
  if (!g_mp3) { cleanupPlayback(); return false; }

  // Start decoding to I2S
  if (!g_mp3->begin(g_src, g_i2s)) {
    cleanupPlayback();
    return false;
  }
  return true;
}

void loop() {
  if (g_mp3) {
    // When loop() returns false, track finished or hit an error
    if (!g_mp3->loop()) {
      cleanupPlayback();
    }
  }
}

void stop() {
  cleanupPlayback();
}

bool isPlaying() {
  return g_mp3 != nullptr;
}

void setVolume(uint8_t vol) {
  g_volume = vol;
  if (g_i2s) g_i2s->SetGain(g_volume / 255.0f);
}

uint8_t getVolume() {
  return g_volume;
}

} // namespace AudioPlayer
