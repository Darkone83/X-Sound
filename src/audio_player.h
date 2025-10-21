#pragma once

#include <Arduino.h>

namespace AudioPlayer {

// Commands processed by the main loop only.
enum class Cmd : uint8_t { None = 0, PlayBoot, PlayEject, Stop };

// Init / lifecycle
void begin(int bclkPin, int lrclkPin, int doutPin);
void loop();

// Volume
void setVolume(uint8_t v);
uint8_t getVolume();

// Status
bool isPlaying();

// Enable/disable sounds (synced with FileMan preferences)
void setBootEnabled(bool enabled);
void setEjectEnabled(bool enabled);

// Public API (now enqueue-based; immediate return)
bool playBoot();
bool playEject();
bool stop();

// Explicit enqueue if you prefer to call with a Cmd
bool enqueue(Cmd c);

} // namespace AudioPlayer