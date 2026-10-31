#pragma once

#include <cstddef>
#include <cstdint>

// Audio format shared by the I2S reader and the hello message sent to clients
constexpr uint32_t SAMPLE_RATE         = 16000;
constexpr uint32_t CHANNEL_COUNT       = 1;
constexpr size_t   FRAMES_PER_PACKET   = 512;    // audio samples per UDP packet
constexpr uint32_t MILLIS_PER_SECOND   = 1000;
constexpr uint32_t FRAME_DURATION_MS   = FRAMES_PER_PACKET * MILLIS_PER_SECOND / SAMPLE_RATE;  // 32 ms