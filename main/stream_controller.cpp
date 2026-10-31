#include "stream_controller.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace {

const char* TAG = "WiFiMic_StreamController";

constexpr uint32_t    STREAM_TASK_STACK_BYTES = 8192;
constexpr UBaseType_t STREAM_TASK_PRIORITY    = 5;

// One read fills one packet (32 ms of audio), so the timeout must be longer than that.
// Reads return every ~32 ms anyway, which is how often commands get checked.
constexpr uint32_t I2S_READ_TIMEOUT_MS = FRAME_DURATION_MS * 2;

constexpr uint32_t READ_ERROR_BACKOFF_MS = 100;   // no hot loop, at most 10 logs a second

}  // namespace

esp_err_t StreamController::init(QueueHandle_t commandQueue) {
    commandQueue_ = commandQueue;
    ESP_RETURN_ON_ERROR(mic_.init(), TAG, "mic init failed");
    ESP_RETURN_ON_ERROR(sender_.open(), TAG, "UDP socket failed");

    const BaseType_t created = xTaskCreate(&StreamController::taskEntry, "stream",
                                           STREAM_TASK_STACK_BYTES, this,
                                           STREAM_TASK_PRIORITY, nullptr);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void StreamController::taskEntry(void* self) {
    static_cast<StreamController*>(self)->run();
}

// Idle: block until a command arrives. Streaming: only peek at the queue, then read audio.
void StreamController::run() {
    for (;;) {
        const TickType_t waitTicks = (state_ == State::STREAMING) ? 0 : portMAX_DELAY;
        Command command;
        if (xQueueReceive(commandQueue_, &command, waitTicks) == pdTRUE) {
            do {
                apply(command);
            } while (xQueueReceive(commandQueue_, &command, 0) == pdTRUE);  // drain what else is waiting
        }
        if (state_ == State::STREAMING) streamOnePacket();
    }
}

void StreamController::apply(const Command& command) {
    switch (command.type) {
        case CommandType::HELLO_REC:
            sender_.setDestination(command.client_ip, command.client_udp_port);
            if (state_ == State::NO_CLIENT) state_ = State::READY;
            ESP_LOGI(TAG, "client registered, audio goes to UDP port %u", command.client_udp_port);
            break;

        case CommandType::START_RECORD:
            if (state_ == State::NO_CLIENT) {
                ESP_LOGW(TAG, "start ignored: no client has said hello");
            } else if (state_ == State::READY) {
                sender_.resetSequence();   // every recording starts at seq 0
                if (mic_.start() == ESP_OK) {
                    state_ = State::STREAMING;
                    ESP_LOGI(TAG, "recording started");
                } else {
                    ESP_LOGE(TAG, "could not start the microphone");
                }
            }
            break;

        case CommandType::STOP_RECORD:
            if (state_ == State::STREAMING) {
                mic_.stop();
                state_ = State::READY;
                ESP_LOGI(TAG, "recording stopped");
            }
            break;

        case CommandType::SET_TEST_TONE:
            mic_.setTestTone(command.tone_enabled);
            ESP_LOGI(TAG, "test tone %s", command.tone_enabled ? "on" : "off");
            break;

        case CommandType::CLIENT_LEFT:
            if (state_ == State::NO_CLIENT) {
                ESP_LOGI(TAG, "ignore, no client");
                break;
            }
            if (state_ == State::STREAMING) mic_.stop();
            sender_.clearDestination();
            mic_.setTestTone(false);   // the next client starts clean
            state_ = State::NO_CLIENT;
            ESP_LOGI(TAG, "client left");
            break;
        default:
            break;
    }
}

void StreamController::streamOnePacket() {
    size_t frames = 0;
    const esp_err_t err = mic_.readFrames(samples_, &frames, I2S_READ_TIMEOUT_MS);
    if (err == ESP_ERR_TIMEOUT) return;   // commands are checked at the top of the loop
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mic read failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(READ_ERROR_BACKOFF_MS));
        return;
    }
    sender_.send(samples_, frames);
}