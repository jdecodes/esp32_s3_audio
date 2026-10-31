#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Generates a sine tone as int16 samples. Used for speaker self-tests;
// MicStreamer's inline tone code can move here later.
class ToneGenerator {
public:
    ToneGenerator(float frequencyHz, float amplitude, uint32_t sampleRateHz)
        : step_(TWO_PI * frequencyHz / static_cast<float>(sampleRateHz)),
          amplitude_(amplitude) {}

    void fill(int16_t* out, size_t frames) {
        for (size_t i = 0; i < frames; ++i) {
            out[i] = static_cast<int16_t>(amplitude_ * sinf(phase_));
            phase_ += step_;
            if (phase_ > TWO_PI) phase_ -= TWO_PI;
        }
    }

private:
    static constexpr float TWO_PI = 6.2831853f;
    float step_;
    float amplitude_;
    float phase_ = 0.f;
};