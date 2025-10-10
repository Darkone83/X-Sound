#pragma once
#include <Arduino.h>

namespace FileMan {
  // Register the /files UI and REST endpoints on the shared server.
  // SPIFFS will be (re)mounted if needed.
  void begin();
}
