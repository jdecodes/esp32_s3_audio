#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "audio_config.h"
#include "command.h"
#include "speaker_streamer.h"
#include "udp_audio_receiver.h"

// The only task that touches the speaker and the receiver.
// Commands from the control server arrive through the queue, so no locks are needed.
class PlaybackController {
public:
    // Initialises the speaker and the socket, then starts the task.
    esp_err_t init(QueueHandle_t commandQueue);

private:
    // NO_CLIENT -> (HELLO) -> READY -> (PLAYBACK_START) -> PLAYING -> (PLAYBACK_STOP) -> READY
    // any state -> (CLIENT_LEFT) -> NO_CLIENT
    enum class State { NO_CLIENT, READY, PLAYING };

    static void taskEntry(void* self);   // FreeRTOS entry point, calls run()
    void run();                          // never returns
    void apply(const Command& command);
    void playOnePacket();
    void runSelfTest();
    void logStatsIfDue();

    QueueHandle_t    commandQueue_ = nullptr;
    SpeakerStreamer  speaker_;
    UdpAudioReceiver receiver_;
    State            state_ = State::NO_CLIENT;
    int16_t          samples_[FRAMES_PER_PACKET] = {};

    // sequence tracking and statistics for the current playback
    bool     hasExpectedSeq_ = false;
    uint32_t expectedSeq_    = 0;
    uint32_t received_       = 0;   // packets played
    uint32_t lost_           = 0;   // gaps in seq
    uint32_t late_           = 0;   // older than expected, dropped
    uint32_t overflow_       = 0;   // speaker could not take the data in time
    int64_t  lastStatsLogUs_ = 0;
};