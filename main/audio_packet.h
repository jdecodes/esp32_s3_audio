#pragma once

#include <cstddef>
#include <cstdint>

#include "audio_config.h"

// Audio datagram, same layout in both directions (little-endian):
// packet = header + data
//   header:  seq      uint32                           (4 bytes)
//   data:    samples  int16 x up to FRAMES_PER_PACKET  (mono)
struct __attribute__((packed)) AudioPacket {
    uint32_t seq;
    int16_t  samples[FRAMES_PER_PACKET];
};

constexpr size_t AUDIO_PACKET_HEADER_BYTES = sizeof(uint32_t);
constexpr size_t AUDIO_PACKET_MAX_BYTES    = sizeof(AudioPacket);