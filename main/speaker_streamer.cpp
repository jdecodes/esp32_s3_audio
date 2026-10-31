#include "speaker_streamer.h"

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

namespace {

const char* TAG = "SpeakerStreamer";

// ---- wiring: ESP32-S3 pins to the MAX98357A breakout (change to match your wiring) ----

// MAX98357A pin	ESP32-S3 connection
// VIN	5V
// GND	GND
// SD	Leave unconnected
// GAIN	Leave unconnected
// DIN	GPIO 9
// BCLK	GPIO 7
// LRC	GPIO 8
constexpr gpio_num_t PIN_BCLK = GPIO_NUM_7;    // amp BCLK
constexpr gpio_num_t PIN_WS   = GPIO_NUM_8;    // amp LRC
constexpr gpio_num_t PIN_DOUT = GPIO_NUM_9;    // amp DIN

// Optional amp shutdown pin. GPIO_NUM_NC = leave the amp's SD pin at the board's default.
constexpr gpio_num_t PIN_AMP_SD = GPIO_NUM_NC;
constexpr uint32_t   AMP_ON  = 1;
constexpr uint32_t   AMP_OFF = 0;

// The DMA buffers are the jitter cushion: 6 x 240 frames = 1440 frames = 90 ms at 16 kHz
constexpr uint32_t DMA_DESC_NUM     = 6;
constexpr uint32_t DMA_FRAME_NUM    = 240;
constexpr size_t   DMA_TOTAL_FRAMES = DMA_DESC_NUM * DMA_FRAME_NUM;

constexpr int32_t  PERCENT_FULL_SCALE     = 100;   // signed on purpose: samples can be negative
constexpr uint32_t DRAIN_WRITE_TIMEOUT_MS = 200;

}  // namespace

esp_err_t SpeakerStreamer::init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);  // the mic uses I2S_NUM_0
    chan_cfg.dma_desc_num  = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;
    chan_cfg.auto_clear    = true;   // send zeros when no data is queued, instead of repeating old data
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_, nullptr), TAG, "i2s_new_channel failed");

    // 16-bit stereo: the same mono sample goes into both slots, so the amp's SD pin
    // setting (left, right or mixed) does not matter
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,       // the MAX98357A needs no MCLK
            .bclk = PIN_BCLK,
            .ws = PIN_WS,
            .dout = PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_, &cfg), TAG, "i2s_channel_init_std_mode failed");

    if (PIN_AMP_SD != GPIO_NUM_NC) {
        gpio_reset_pin(PIN_AMP_SD);
        gpio_set_direction(PIN_AMP_SD, GPIO_MODE_OUTPUT);
        gpio_set_level(PIN_AMP_SD, AMP_OFF);   // amp in shutdown until start()
    }
    return ESP_OK;
}

esp_err_t SpeakerStreamer::start() {
    if (running_) return ESP_OK;
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_), TAG, "i2s_channel_enable failed");
    if (PIN_AMP_SD != GPIO_NUM_NC) {
        gpio_set_level(PIN_AMP_SD, AMP_ON);    // enable the amp after the clocks run, avoids a pop
    }
    running_ = true;
    return ESP_OK;
}

esp_err_t SpeakerStreamer::stop() {
    if (!running_) return ESP_OK;

    // Let what is still queued play out: write one DMA length of silence behind it
    size_t remaining = DMA_TOTAL_FRAMES;
    while (remaining > 0) {
        const size_t chunk = std::min(remaining, FRAMES_PER_PACKET);
        if (writeSilence(chunk, DRAIN_WRITE_TIMEOUT_MS) != ESP_OK) break;
        remaining -= chunk;
    }

    if (PIN_AMP_SD != GPIO_NUM_NC) {
        gpio_set_level(PIN_AMP_SD, AMP_OFF);
    }
    running_ = false;
    return i2s_channel_disable(tx_);
}

void SpeakerStreamer::setVolumePercent(uint8_t percent) {
    volumePercent_ = static_cast<uint8_t>(std::min<int32_t>(percent, PERCENT_FULL_SCALE));
}

esp_err_t SpeakerStreamer::writeFrames(const int16_t* samples, size_t frames, uint32_t timeoutMs) {
    if (!running_) return ESP_ERR_INVALID_STATE;
    if (frames > FRAMES_PER_PACKET) return ESP_ERR_INVALID_ARG;
    if (frames == 0) return ESP_OK;

    for (size_t frame = 0; frame < frames; ++frame) {
        const int32_t scaled = static_cast<int32_t>(samples[frame]) * volumePercent_ / PERCENT_FULL_SCALE;
        stereo_[frame * SLOTS_PER_FRAME + LEFT_SLOT_INDEX]  = static_cast<int16_t>(scaled);
        stereo_[frame * SLOTS_PER_FRAME + RIGHT_SLOT_INDEX] = static_cast<int16_t>(scaled);
    }
    return writeStereo(frames, timeoutMs);
}

esp_err_t SpeakerStreamer::writeSilence(size_t frames, uint32_t timeoutMs) {
    if (!running_) return ESP_ERR_INVALID_STATE;
    if (frames > FRAMES_PER_PACKET) return ESP_ERR_INVALID_ARG;

    memset(stereo_, 0, frames * SLOTS_PER_FRAME * sizeof(int16_t));
    return writeStereo(frames, timeoutMs);
}

esp_err_t SpeakerStreamer::writeStereo(size_t frames, uint32_t timeoutMs) {
    const size_t bytes = frames * SLOTS_PER_FRAME * sizeof(int16_t);
    size_t bytesWritten = 0;
    return i2s_channel_write(tx_, stereo_, bytes, &bytesWritten, timeoutMs);
}