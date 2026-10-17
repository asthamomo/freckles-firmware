#pragma once
#include <Arduino.h>

struct FrecklesSettings {
    String serverUrl;
    String token;
    String listenMode;  // "tap" or "always"
    uint8_t volume;

    bool alwaysListening() const { return listenMode == "always"; }
};

namespace settings {
void load(FrecklesSettings& s);
void save(const FrecklesSettings& s);
String deviceId();  // stable id derived from the Wi-Fi MAC
}  // namespace settings
