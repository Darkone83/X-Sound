#pragma once
#include <Arduino.h>

// Lightweight MP3 player for ESP32/ESP32-S3
// - Helix MP3 decoder via ESP8266Audio
// - Reads from SPIFFS
// - I2S output (e.g., MAX98357A)
// API:
//   AudioPlayer::begin(bclk, lrck, dout, volume0..255)
//   AudioPlayer::playPath("/boot.mp3")
//   AudioPlayer::playBoot() / playEject()
//   AudioPlayer::stop(), isPlaying(), loop()
//   AudioPlayer::setVolume(0..255), getVolume()

namespace AudioPlayer {
  // Initialize I2S + decoder. Call once in setup().
  void begin(int pin_bclk, int pin_lrck, int pin_dout, uint8_t volume_0_255 = 200);

  // Non-blocking pump; call each loop()
  void loop();

  // Start playing an MP3 file from SPIFFS path (e.g., "/boot.mp3").
  // Returns false if missing or cannot start. Stops current track unless stop_current=false.
  bool playPath(const char* path, bool stop_current = true);

  // Stop playback (no error if nothing is playing)
  void stop();

  // True while MP3 decoder is actively running
  bool isPlaying();

  // Volume is a linear 0..255 mapped to I2S gain 0.0..1.0
  void setVolume(uint8_t vol);
  uint8_t getVolume();

  // Convenience helpers
  inline bool playBoot()  { return playPath("/boot.mp3"); }
  inline bool playEject() { return playPath("/eject.mp3"); }
}
