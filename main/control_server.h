#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "command.h"

class HttpControlServer {
public:
    esp_err_t start(QueueHandle_t rec_queue, QueueHandle_t playback_queue);

private:
    static constexpr int NO_CLIENT_FD = -1;

    httpd_handle_t server_ = nullptr;
    QueueHandle_t record_queue_ = nullptr;
    QueueHandle_t playback_queue_ = nullptr;
    std::atomic<int> client_fd_{NO_CLIENT_FD};

    // ESP-IDF's close callback has no user_ctx argument.
    // This implementation supports one global server instance.
    static HttpControlServer* instance_;

    static esp_err_t wsHandler(httpd_req_t* req);
    static void sessionClosed(httpd_handle_t server, int sockfd);

    esp_err_t handleMessage(httpd_req_t* req, const char* payload);
    esp_err_t handleHello(httpd_req_t* req, const void* json, bool isRec);
    esp_err_t handleStartStop(httpd_req_t* req, bool start, bool isRec);
    esp_err_t handleTestTone(httpd_req_t* req, const void* json);

    bool postCommand(const Command& command, bool isRec);
    bool peerIpv4(int fd, uint32_t* ip_out) const;
    void claimSession(int fd);

    esp_err_t sendError(httpd_req_t* req, const char* message);
    esp_err_t sendHelloAck(httpd_req_t* req, bool isRec);
};