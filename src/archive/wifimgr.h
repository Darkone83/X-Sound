#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace WiFiMgr {

    // Expose the shared server so other modules (e.g., fileman) can add routes.
    AsyncWebServer& getServer();

    void begin();
    void loop();
    void restartPortal();
    void forgetWiFi();
    bool isConnected();
    String getStatus();
}
