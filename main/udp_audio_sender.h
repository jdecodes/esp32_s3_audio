#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lwip/sockets.h"   // sockaddr_in
#include "audio_packet.h"

// Owns the UDP socket, the destination and the packet layout. Knows nothing about I2S.
// Not thread-safe: use it from one task only (StreamController's task).
class UdpAudioSender {
public:
    esp_err_t open();   // creates the socket once

    // ip: network byte order (as in sockaddr_in), port: host byte order
    void setDestination(uint32_t ip, uint16_t port);
    void clearDestination() { hasDestination_ = false; }
    bool hasDestination() const { return hasDestination_; }

    void resetSequence() { nextSeq_ = 0; }

    // Sends one packet; does nothing without a destination.
    void send(const int16_t* samples, size_t frames);

private:
    // // packet = header + data
    // //   header:  seq      uint32                          (4 bytes)
    // //   data:    samples  int16 x up to FRAMES_PER_PACKET
    // struct __attribute__((packed)) Packet {
    //     uint32_t seq;
    //     int16_t  samples[FRAMES_PER_PACKET];
    // };
    // static constexpr size_t HEADER_SIZE_BYTES = sizeof(uint32_t);

    void logSendError(int errorNumber);

    int         socket_          = -1;
    sockaddr_in destination_     = {};
    bool        hasDestination_  = false;
    uint32_t    nextSeq_         = 0;
    AudioPacket      packet_          = {};
    int64_t     lastErrorLogUs_  = 0;
    uint32_t    suppressedErrors_ = 0;
};