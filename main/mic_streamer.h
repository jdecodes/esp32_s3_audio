#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"   // i2s_chan_handle_t
#include "esp_err.h"

#include "audio_config.h"

// Owns the I2S channel and the microphone read path. Knows nothing about sockets or clients.
// Not thread-safe: use it from one task only (StreamController's task).
class MicStreamer {
public:
    // Creates and configures the I2S channel; it stays disabled until start().
    esp_err_t init();

    esp_err_t start();
    esp_err_t stop();
    bool isRunning() const { return running_; }

    void setTestTone(bool enabled) { testTone_ = enabled; }

    // Reads one packet worth of audio into out[] (room for FRAMES_PER_PACKET samples).
    //   ESP_OK and *framesOut > 0   -> samples ready
    //   ESP_OK and *framesOut == 0  -> startup junk was dropped, call again
    //   ESP_ERR_TIMEOUT             -> no data in time, call again
    // With the test tone on, the I2S read still paces the loop, but the samples are replaced by the tone.
    esp_err_t readFrames(int16_t* out, size_t* framesOut, uint32_t timeoutMs);

private:
    // One I2S frame = one WS cycle = [left slot word][right slot word]
    static constexpr size_t SLOTS_PER_FRAME = 2;
    static constexpr size_t WORDS_PER_READ  = FRAMES_PER_PACKET * SLOTS_PER_FRAME;

    i2s_chan_handle_t rx_ = nullptr;
    bool  running_         = false;
    bool  testTone_        = false;
    int   readsSinceStart_ = 0;
    float phase_           = 0.f;
    int32_t raw_[WORDS_PER_READ] = {};   // 32-bit words straight from I2S, 2 slots per frame
};