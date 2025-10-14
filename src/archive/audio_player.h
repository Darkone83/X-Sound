#pragma once
#include <Arduino.h>

namespace AudioPlayer {

  // Call once at boot. Provide your I2S pins.
  void begin(int bclkPin, int lrclkPin, int doutPin);

  // Call frequently from loop(); handles decoder loop and end-of-track cleanup.
  void loop();

  // Volume 0..255 (mapped to AudioOutputI2S gain)
  void setVolume(uint8_t v);
  uint8_t getVolume();

  // Control
  bool playBoot();            // plays /boot.mp3 from SPIFFS
  bool playEject();           // plays /eject.mp3 from SPIFFS
  bool playFile(const char* path); // generic play by path
  void stop();

  // State
  bool isPlaying();
}
