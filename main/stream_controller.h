#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "audio_config.h"
#include "command.h"
#include "mic_streamer.h"
#include "udp_audio_sender.h"

// The only task that touches the mic and the sender.
// Commands from the control server arrive through the queue, so no locks are needed.
class StreamController {
public:
    // Initialises the mic and the socket, then starts the task.
    esp_err_t init(QueueHandle_t commandQueue);

private:
    // NO_CLIENT -> (HELLO) -> READY -> (START) -> STREAMING -> (STOP) -> READY
    // any state -> (CLIENT_LEFT) -> NO_CLIENT
    enum class State { NO_CLIENT, READY, STREAMING };

    static void taskEntry(void* self);   // FreeRTOS entry point, calls run()
    void run();                          // never returns
    void apply(const Command& command);
    void streamOnePacket();

    QueueHandle_t  commandQueue_ = nullptr;
    MicStreamer    mic_;
    UdpAudioSender sender_;
    State          state_ = State::NO_CLIENT;
    int16_t        samples_[FRAMES_PER_PACKET] = {};
};