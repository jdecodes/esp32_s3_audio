#include "mic_streamer.h"

#include <cmath>

#include "driver/gpio.h"
#include "esp_check.h"

namespace {

const char* TAG = "WiFiMic_MicStreamer";

constexpr int SHIFT = 14;                      // 8 clips, 16 is quiet

constexpr gpio_num_t PIN_BCLK = GPIO_NUM_4;
constexpr gpio_num_t PIN_WS   = GPIO_NUM_5;
constexpr gpio_num_t PIN_DIN  = GPIO_NUM_6;

constexpr size_t LEFT_SLOT_INDEX = 0;          // INMP441 with L/R pin tied to GND
constexpr int    DISCARD_READS   = 16;         // startup junk after every start

// Test tone
constexpr float TEST_TONE_HZ        = 440.f;
constexpr float TEST_TONE_AMPLITUDE = 8000.f;
constexpr float TWO_PI              = 6.2831853f;
constexpr float TEST_TONE_STEP      = TWO_PI * TEST_TONE_HZ / static_cast<float>(SAMPLE_RATE);

}  // namespace

esp_err_t MicStreamer::init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, nullptr, &rx_), TAG, "i2s_new_channel failed");

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_BCLK,
            .ws = PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    return i2s_channel_init_std_mode(rx_, &cfg);
}

esp_err_t MicStreamer::start() {
    if (running_) return ESP_OK;
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_), TAG, "i2s_channel_enable failed");
    readsSinceStart_ = 0;   // discard the startup junk again
    running_ = true;
    return ESP_OK;
}

esp_err_t MicStreamer::stop() {
    if (!running_) return ESP_OK;
    running_ = false;
    return i2s_channel_disable(rx_);
}

esp_err_t MicStreamer::readFrames(int16_t* out, size_t* framesOut, uint32_t timeoutMs) {
    *framesOut = 0;
    if (!running_) return ESP_ERR_INVALID_STATE;

    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(rx_, raw_, sizeof(raw_), &bytesRead, timeoutMs);
    if (err != ESP_OK) return err;

    if (++readsSinceStart_ < DISCARD_READS) return ESP_OK;

    const size_t wordsRead  = bytesRead / sizeof(int32_t);
    const size_t frameCount = wordsRead / SLOTS_PER_FRAME;

    for (size_t frame = 0; frame < frameCount; ++frame) {
        int32_t leftWord = raw_[frame * SLOTS_PER_FRAME + LEFT_SLOT_INDEX];
        int32_t value = leftWord >> SHIFT;
        if (testTone_) {
            value = static_cast<int32_t>(TEST_TONE_AMPLITUDE * sinf(phase_));
            phase_ += TEST_TONE_STEP;
            if (phase_ > TWO_PI) phase_ -= TWO_PI;
        }
        out[frame] = static_cast<int16_t>(value);
    }
    *framesOut = frameCount;
    return ESP_OK;
}