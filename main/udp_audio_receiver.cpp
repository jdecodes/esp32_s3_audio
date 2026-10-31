#include "udp_audio_receiver.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "esp_log.h"

namespace {

const char* TAG = "UdpAudioReceiver";

constexpr uint32_t MICROS_PER_MILLI = 1000;

}  // namespace

esp_err_t UdpAudioReceiver::open(uint16_t port, uint32_t receiveTimeoutMs) {
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_ < 0) {
        ESP_LOGE(TAG, "failed to create UDP socket, errno=%d", errno);
        return ESP_FAIL;
    }

    sockaddr_in local = {};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        ESP_LOGE(TAG, "failed to bind UDP port %u, errno=%d", port, errno);
        close(socket_);
        socket_ = -1;
        return ESP_FAIL;
    }

    timeval timeout = {};
    timeout.tv_sec  = receiveTimeoutMs / MILLIS_PER_SECOND;
    timeout.tv_usec = (receiveTimeoutMs % MILLIS_PER_SECOND) * MICROS_PER_MILLI;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGE(TAG, "failed to set the receive timeout, errno=%d", errno);
        close(socket_);
        socket_ = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "listening on UDP port %u", port);
    return ESP_OK;
}

void UdpAudioReceiver::flush() {
    while (recvfrom(socket_, raw_, sizeof(raw_), MSG_DONTWAIT, nullptr, nullptr) > 0) {
        // discard
    }
}

esp_err_t UdpAudioReceiver::receive(uint32_t* seqOut, int16_t* samplesOut, size_t* framesOut) {
    *framesOut = 0;

    sockaddr_in from = {};
    socklen_t fromLength = sizeof(from);
    const int received = recvfrom(socket_, raw_, sizeof(raw_), 0,
                                  reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ESP_ERR_TIMEOUT;
        ESP_LOGW(TAG, "recvfrom failed, errno=%d", errno);
        return ESP_FAIL;
    }

    if (!hasAllowedSource_ || from.sin_addr.s_addr != allowedIp_) {
        ++filtered_;
        return ESP_OK;
    }

    // packet = header (seq) + data (samples)
    const size_t length = static_cast<size_t>(received);
    if (length <= AUDIO_PACKET_HEADER_BYTES ||
        (length - AUDIO_PACKET_HEADER_BYTES) % sizeof(int16_t) != 0) {
        ++malformed_;
        return ESP_OK;
    }
    const size_t frames = (length - AUDIO_PACKET_HEADER_BYTES) / sizeof(int16_t);
    if (frames > FRAMES_PER_PACKET) {
        ++malformed_;
        return ESP_OK;
    }

    memcpy(seqOut, raw_, AUDIO_PACKET_HEADER_BYTES);   // raw_ is not aligned for a direct uint32_t read
    memcpy(samplesOut, raw_ + AUDIO_PACKET_HEADER_BYTES, frames * sizeof(int16_t));
    *framesOut = frames;
    return ESP_OK;
}