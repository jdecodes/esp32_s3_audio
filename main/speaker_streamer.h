#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"   // i2s_chan_handle_t
#include "esp_err.h"

#include "audio_config.h"

// Owns the I2S TX channel and the MAX98357A amplifier. Knows nothing about sockets or clients.
// Not thread-safe: use it from one task only (PlaybackController's task).
class SpeakerStreamer {
public:
    // Creates and configures the I2S TX channel and the optional amp enable pin; stays off until start().
    esp_err_t init();

    esp_err_t start();   // starts the I2S clocks, then enables the amp
    esp_err_t stop();    // lets queued audio play out, mutes the amp, stops the clocks
    bool isRunning() const { return running_; }

    void setVolumePercent(uint8_t percent);

    // Plays mono int16 samples (at most FRAMES_PER_PACKET). Blocks until the I2S DMA buffers have room,
    // at most timeoutMs. ESP_ERR_TIMEOUT means the data was not accepted in time.
    esp_err_t writeFrames(const int16_t* samples, size_t frames, uint32_t timeoutMs);
    esp_err_t writeSilence(size_t frames, uint32_t timeoutMs);

private:
    // One I2S frame = one WS cycle = [left slot sample][right slot sample]
    static constexpr size_t SLOTS_PER_FRAME  = 2;
    static constexpr size_t LEFT_SLOT_INDEX  = 0;
    static constexpr size_t RIGHT_SLOT_INDEX = 1;
    static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 50;

    esp_err_t writeStereo(size_t frames, uint32_t timeoutMs);

    i2s_chan_handle_t tx_ = nullptr;
    bool    running_       = false;
    uint8_t volumePercent_ = DEFAULT_VOLUME_PERCENT;
    int16_t stereo_[FRAMES_PER_PACKET * SLOTS_PER_FRAME] = {};   // interleaved left/right, ready for I2S
};