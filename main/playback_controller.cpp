#include "playback_controller.h"

#include <algorithm>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "tone_generator.h"

namespace {

const char* TAG = "PlaybackController";

constexpr uint32_t    PLAYBACK_TASK_STACK_BYTES = 8192;
constexpr UBaseType_t PLAYBACK_TASK_PRIORITY    = 5;

// How long a receive may block. This is also the worst-case delay before a STOP is noticed.
constexpr uint32_t RECEIVE_TIMEOUT_MS = 50;
constexpr uint32_t PLAYBACK_UDP_PORT = 5000;

// How long a write may wait for room in the I2S buffers. It only triggers when the sender is faster than real time.
constexpr uint32_t WRITE_TIMEOUT_MS = 200;

constexpr uint32_t MAX_SILENCE_FILL_PACKETS     = 5;     // cap on silence inserted for one gap
constexpr uint32_t SEQ_RESYNC_THRESHOLD_PACKETS = 100;   // seq this far behind = the sender restarted

constexpr uint32_t READ_ERROR_BACKOFF_MS = 100;
constexpr int64_t  STATS_LOG_INTERVAL_US = 1000000;

// Speaker self-test at boot: plays a tone with no network involved. Set to false once the speaker works.
constexpr bool     PLAY_SELF_TEST_ON_BOOT = true;
constexpr float    SELF_TEST_TONE_HZ      = 440.f;
constexpr float    SELF_TEST_AMPLITUDE    = 8000.f;
constexpr uint32_t SELF_TEST_DURATION_MS  = 2000;

}  // namespace

esp_err_t PlaybackController::init(QueueHandle_t commandQueue) {
    commandQueue_ = commandQueue;
    ESP_RETURN_ON_ERROR(speaker_.init(), TAG, "speaker init failed");
    ESP_RETURN_ON_ERROR(receiver_.open(PLAYBACK_UDP_PORT, RECEIVE_TIMEOUT_MS), TAG, "UDP receiver failed");

    const BaseType_t created = xTaskCreate(&PlaybackController::taskEntry, "playback",
                                           PLAYBACK_TASK_STACK_BYTES, this,
                                           PLAYBACK_TASK_PRIORITY, nullptr);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void PlaybackController::taskEntry(void* self) {
    static_cast<PlaybackController*>(self)->run();
}

// Idle: block until a command arrives. Playing: only peek at the queue, then wait for audio.
void PlaybackController::run() {
    if (PLAY_SELF_TEST_ON_BOOT) runSelfTest();

    for (;;) {
        const TickType_t waitTicks = (state_ == State::PLAYING) ? 0 : portMAX_DELAY;
        Command command;
        if (xQueueReceive(commandQueue_, &command, waitTicks) == pdTRUE) {
            do {
                apply(command);
            } while (xQueueReceive(commandQueue_, &command, 0) == pdTRUE);  // drain what else is waiting
        }
        if (state_ == State::PLAYING) {
            playOnePacket();
            logStatsIfDue();
        }
    }
}

void PlaybackController::apply(const Command& command) {
    switch (command.type) {
        case CommandType::HELLO_PLAY:
            receiver_.setAllowedSource(command.client_ip);
            if (state_ == State::NO_CLIENT) state_ = State::READY;
            ESP_LOGI(TAG, "client registered");
            break;

        case CommandType::START_PLAYBACK:
            if (state_ == State::NO_CLIENT) {
                ESP_LOGW(TAG, "start_playback ignored: no client has said hello");
            } else if (state_ == State::READY) {
                receiver_.flush();           // old datagrams must not play
                hasExpectedSeq_ = false;
                received_ = lost_ = late_ = overflow_ = 0;
                if (speaker_.start() == ESP_OK) {
                    state_ = State::PLAYING;
                    ESP_LOGI(TAG, "playback started");
                } else {
                    ESP_LOGE(TAG, "could not start the speaker");
                }
            }
            break;

        case CommandType::STOP_PLAYBACK:
            if (state_ == State::PLAYING) {
                speaker_.stop();
                state_ = State::READY;
                ESP_LOGI(TAG, "playback stopped");
            }
            break;

        case CommandType::CLIENT_LEFT:
            if (state_ == State::NO_CLIENT) {
                ESP_LOGI(TAG, "ignore, no client");
                break;
            }
            if (state_ == State::PLAYING) speaker_.stop();
            receiver_.clearAllowedSource();
            state_ = State::NO_CLIENT;
            ESP_LOGI(TAG, "client left");
            break;

        default:
            break;   // microphone commands are handled by StreamController
    }
}

void PlaybackController::playOnePacket() {
    uint32_t seq = 0;
    size_t frames = 0;
    const esp_err_t err = receiver_.receive(&seq, samples_, &frames);
    if (err == ESP_ERR_TIMEOUT) return;   // nothing arrived; the I2S output plays silence on its own
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(READ_ERROR_BACKOFF_MS));   // no hot loop on a broken socket
        return;
    }
    if (frames == 0) return;              // the receiver dropped the datagram (wrong source or malformed)

    if (hasExpectedSeq_) {
        if (seq < expectedSeq_) {
            if (expectedSeq_ - seq <= SEQ_RESYNC_THRESHOLD_PACKETS) {
                ++late_;                  // old or duplicate packet
                return;
            }
            ESP_LOGW(TAG, "seq went back from %u to %u: sender restarted, resync",
                     static_cast<unsigned>(expectedSeq_), static_cast<unsigned>(seq));
        } else if (seq > expectedSeq_) {
            const uint32_t gap = seq - expectedSeq_;
            lost_ += gap;
            // silence keeps the timeline and the jitter cushion intact
            const uint32_t fill = std::min(gap, MAX_SILENCE_FILL_PACKETS);
            for (uint32_t i = 0; i < fill; ++i) {
                speaker_.writeSilence(FRAMES_PER_PACKET, WRITE_TIMEOUT_MS);
            }
        }
    }
    hasExpectedSeq_ = true;
    expectedSeq_    = seq + 1;

    if (speaker_.writeFrames(samples_, frames, WRITE_TIMEOUT_MS) == ESP_OK) {
        ++received_;
    } else {
        ++overflow_;
    }
}

void PlaybackController::runSelfTest() {
    ESP_LOGI(TAG, "speaker self-test: %u Hz tone for %u ms",
             static_cast<unsigned>(SELF_TEST_TONE_HZ), static_cast<unsigned>(SELF_TEST_DURATION_MS));
    if (speaker_.start() != ESP_OK) {
        ESP_LOGE(TAG, "self-test: could not start the speaker");
        return;
    }

    ToneGenerator tone(SELF_TEST_TONE_HZ, SELF_TEST_AMPLITUDE, SAMPLE_RATE);
    const uint32_t packets = SELF_TEST_DURATION_MS / FRAME_DURATION_MS;
    for (uint32_t i = 0; i < packets; ++i) {
        tone.fill(samples_, FRAMES_PER_PACKET);
        speaker_.writeFrames(samples_, FRAMES_PER_PACKET, WRITE_TIMEOUT_MS);
    }
    speaker_.stop();
}

void PlaybackController::logStatsIfDue() {
    const int64_t now = esp_timer_get_time();
    if (now - lastStatsLogUs_ < STATS_LOG_INTERVAL_US) return;
    lastStatsLogUs_ = now;
    ESP_LOGI(TAG, "playing: played=%u lost=%u late=%u overflow=%u filtered=%u malformed=%u",
             static_cast<unsigned>(received_), static_cast<unsigned>(lost_),
             static_cast<unsigned>(late_), static_cast<unsigned>(overflow_),
             static_cast<unsigned>(receiver_.filteredCount()),
             static_cast<unsigned>(receiver_.malformedCount()));
}