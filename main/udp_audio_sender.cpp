#include "udp_audio_sender.h"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace {

const char* TAG = "WifiMic_UdpAudioSender";

// A failing sendto would otherwise log about 31 times a second
constexpr int64_t SEND_ERROR_LOG_INTERVAL_US = 1000000;

}  // namespace

esp_err_t UdpAudioSender::open() {
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_ < 0) {
        ESP_LOGE(TAG, "failed to create UDP socket, errno=%d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "UDP socket created");
    return ESP_OK;
}

void UdpAudioSender::setDestination(uint32_t ip, uint16_t port) {
    destination_.sin_family      = AF_INET;
    destination_.sin_port        = htons(port);
    destination_.sin_addr.s_addr = ip;
    hasDestination_ = true;
}

void UdpAudioSender::send(const int16_t* samples, size_t frames) {
    if (!hasDestination_ || frames == 0 || frames > FRAMES_PER_PACKET) return;

    packet_.seq = nextSeq_++;
    memcpy(packet_.samples, samples, frames * sizeof(int16_t));

    const size_t packetBytes = AUDIO_PACKET_HEADER_BYTES + frames * sizeof(int16_t);   // was HEADER_SIZE_BYTES
    const int sent = sendto(socket_, &packet_, packetBytes, 0,
                            reinterpret_cast<sockaddr*>(&destination_), sizeof(destination_));
    if (sent < 0) logSendError(errno);   // errno is read right here, before anything can change it
}

void UdpAudioSender::logSendError(int errorNumber) {
    ++suppressedErrors_;
    const int64_t now = esp_timer_get_time();
    if (now - lastErrorLogUs_ < SEND_ERROR_LOG_INTERVAL_US) return;

    ESP_LOGW(TAG, "UDP send failed, errno=%d (%u failures since last log)", errorNumber,
             static_cast<unsigned>(suppressedErrors_));
    suppressedErrors_ = 0;
    lastErrorLogUs_   = now;
}