#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "command.h"
#include "control_server.h"
#include "stream_controller.h"
#include "softap_setup.h"
#include "playback_controller.h"

static const char* TAG = "WiFiMic_Main";

constexpr uint32_t COMMAND_QUEUE_LENGTH = 8;


extern "C" void app_main(void)
{
    // Function-local statics: they live forever, but nothing else in the program can reach them.
    static HttpControlServer controlServer;
    static StreamController  streamController;
    static SoftApSetup softAp;

    QueueHandle_t commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));
    configASSERT(commandQueue != nullptr);


    static PlaybackController playbackController;
    QueueHandle_t playbackQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));


    softAp.init(); // init the soft ap.

    ESP_ERROR_CHECK(streamController.init(commandQueue));    // I2S, socket and task first, so commands have a consumer
    ESP_ERROR_CHECK(playbackController.init(playbackQueue));

    ESP_ERROR_CHECK(controlServer.start(commandQueue, playbackQueue));      // HTTP server with the /ws endpoint

    ESP_LOGI(TAG, "Wireless microphone initialized");
}