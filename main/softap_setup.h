#pragma once

// Owns the I2S channel and the microphone read path. Knows nothing about sockets or clients.
// Not thread-safe: use it from one task only (StreamController's task).
class SoftApSetup {
public:
    // Creates and setup ap
    void init();
    bool isInit() {return initDone; }

private:
// ---- config ----
    // SoftAP (the ESP's own Wi-Fi; must match Config.kt on Android)
    static constexpr const char* AP_SSID        = "Jd-Mic";
    static constexpr const char* AP_PASS        = "1234567890";   // WPA2 needs at least 8 characters
    static constexpr uint8_t     AP_CHANNEL     = 1;
    static constexpr uint8_t     AP_MAX_CLIENTS = 1;                 // one receiver at a time
    bool initDone = false;
};