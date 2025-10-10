#pragma once
#include <Arduino.h>

enum class LedStatus {
    Booting,
    Portal,
    WifiConnected,
    WifiFailed,
    Playing,   // NEW: audio is playing
    Error      // NEW: generic error (e.g., playback/file)
};

namespace LedStat {
    void begin();
    void setStatus(LedStatus status);
    void loop(); // Call this in main loop for blinking/timing
}
