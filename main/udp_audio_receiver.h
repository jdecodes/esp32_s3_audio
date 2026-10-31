#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lwip/sockets.h"

#include "audio_config.h"
#include "audio_packet.h"

// Owns the UDP socket that playback audio arrives on. Knows nothing about I2S.
// Accepts datagrams only from the registered client. Not thread-safe: use it from one task only.
class UdpAudioReceiver {
public:
    // Creates the socket, binds the port, and sets how long receive() may block.
    esp_err_t open(uint16_t port, uint32_t receiveTimeoutMs);

    // ip: network byte order (as in sockaddr_in). Datagrams from any other address are dropped.
    void setAllowedSource(uint32_t ip) { allowedIp_ = ip; hasAllowedSource_ = true; }
    void clearAllowedSource() { hasAllowedSource_ = false; }

    // Throws away datagrams that are already queued (call before starting playback).
    void flush();

    // Waits for one datagram.
    //   ESP_OK and *framesOut > 0   -> *seqOut and samplesOut[0..*framesOut) are valid
    //   ESP_OK and *framesOut == 0  -> a datagram arrived but was dropped (wrong source or malformed)
    //   ESP_ERR_TIMEOUT             -> nothing arrived in time
    esp_err_t receive(uint32_t* seqOut, int16_t* samplesOut, size_t* framesOut);

    uint32_t filteredCount() const { return filtered_; }
    uint32_t malformedCount() const { return malformed_; }

private:
    // One spare byte: an oversized datagram then fails validation instead of being silently truncated
    static constexpr size_t OVERSIZE_DETECTION_BYTES = 1;

    int      socket_           = -1;
    uint32_t allowedIp_        = 0;
    bool     hasAllowedSource_ = false;
    uint32_t filtered_         = 0;   // datagrams from the wrong address
    uint32_t malformed_        = 0;   // datagrams that do not match the packet layout
    uint8_t  raw_[AUDIO_PACKET_MAX_BYTES + OVERSIZE_DETECTION_BYTES] = {};
};