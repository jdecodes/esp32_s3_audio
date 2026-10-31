#pragma once

#include <cstdint>

// What the control connection asks the stream task to do.
// Shared by HttpControlServer (producer) and StreamController (consumer).
enum class CommandType : uint8_t {
    HELLO_PLAY,
    HELLO_REC,
    START_RECORD,
    STOP_RECORD,
    START_PLAYBACK,
    STOP_PLAYBACK,
    SET_TEST_TONE,
    CLIENT_LEFT
};

struct Command {
    CommandType type{};
    uint32_t client_ip{};          // IPv4 address in network byte order
    uint16_t client_udp_port{};
    bool tone_enabled{};
};