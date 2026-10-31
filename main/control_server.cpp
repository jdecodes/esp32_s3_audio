#include "control_server.h"

#include <cstring>
#include <memory>

#include "cJSON.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "unistd.h"

namespace {

constexpr char TAG[] = "WiFiMic_ControlServer";
constexpr char WS_URI[] = "/ws";
constexpr size_t MAX_MESSAGE_BYTES = 256;
constexpr uint16_t WS_CLOSE_NORMAL = 1000;

constexpr char KEY_TYPE[] = "type";
constexpr char KEY_UDP_PORT[] = "udp_port";
constexpr char KEY_ENABLED[] = "enabled";
constexpr char KEY_OK[] = "ok";
constexpr char KEY_MESSAGE[] = "message";

constexpr char TYPE_HELLO_PLAY[] = "hello_play";
constexpr char TYPE_HELLO_REC[] = "hello_rec";
constexpr char TYPE_START_REC[] = "start_rec";
constexpr char TYPE_STOP_REC[] = "stop_rec";

constexpr char TYPE_START_PLAY[] = "start_play";
constexpr char TYPE_STOP_PLAY[] = "stop_play";

constexpr char TYPE_TEST_TONE[] = "enable_test_tone";

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonText = std::unique_ptr<char, decltype(&cJSON_free)>;

esp_err_t sendJson(httpd_req_t* req, const cJSON* json) {
    if (json == nullptr) return ESP_ERR_NO_MEM;

    JsonText text(cJSON_PrintUnformatted(json), &cJSON_free);
    if (!text) return ESP_ERR_NO_MEM;

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(text.get());
    frame.len = std::strlen(text.get());

    return httpd_ws_send_frame(req, &frame);
}

} // namespace

HttpControlServer* HttpControlServer::instance_ = nullptr;

bool HttpControlServer::postCommand(const Command& command, bool isRec) {
    if (isRec) {
        return record_queue_ != nullptr &&
            xQueueSend(record_queue_, &command, 0) == pdTRUE;
    } else {
        return playback_queue_ != nullptr &&
            xQueueSend(playback_queue_, &command, 0) == pdTRUE;        
    }
}

bool HttpControlServer::peerIpv4(int fd, uint32_t* ip_out) const {
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return false;
    }

    if (address.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        *ip_out = ipv4->sin_addr.s_addr;
        return true;
    }
#if CONFIG_LWIP_IPV6
    if (address.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        const auto& bytes = ipv6->sin6_addr.s6_addr;

        // Accept IPv4-mapped IPv6 addresses only: ::ffff:a.b.c.d
        for (size_t i = 0; i < 10; ++i) {
            if (bytes[i] != 0) return false;
        }
        if (bytes[10] != 0xff || bytes[11] != 0xff) return false;

        std::memcpy(ip_out, &bytes[12], sizeof(*ip_out));
        return true;
    }
#endif
    return false;
}

void HttpControlServer::claimSession(int fd) {
    const int previous = client_fd_.exchange(fd);

    if (previous != NO_CLIENT_FD && previous != fd) {
        ESP_LOGW(TAG, "new client replaces previous session");
        esp_err_t err = httpd_sess_trigger_close(server_, previous);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not close old session fd=%d: %s",
                    previous, esp_err_to_name(err));
        }
    }
}

esp_err_t HttpControlServer::sendError(
    httpd_req_t* req, const char* message) {
    ESP_LOGW(TAG, "rejecting message: %s", message);

    JsonPtr json(cJSON_CreateObject(), &cJSON_Delete);
    if (!json) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(json.get(), KEY_TYPE, "error");
    cJSON_AddStringToObject(json.get(), KEY_MESSAGE, message);
    return sendJson(req, json.get());
}

esp_err_t HttpControlServer::sendHelloAck(httpd_req_t* req, bool isRec) {
    JsonPtr json(cJSON_CreateObject(), &cJSON_Delete);
    if (!json) return ESP_ERR_NO_MEM;

    const char *type = isRec ? TYPE_HELLO_REC : TYPE_HELLO_PLAY; 

    cJSON_AddStringToObject(json.get(), KEY_TYPE, type);
    cJSON_AddBoolToObject(json.get(), KEY_OK, true);
    return sendJson(req, json.get());
}

esp_err_t HttpControlServer::handleHello(
    httpd_req_t* req, const void* payload, bool isRec) {
    ESP_LOGI(TAG, "%s ", __func__);

    const auto* json = static_cast<const cJSON*>(payload);
    const cJSON* udp_port =
        cJSON_GetObjectItemCaseSensitive(json, KEY_UDP_PORT);

    if (!cJSON_IsNumber(udp_port) ||
        udp_port->valuedouble < 1 ||
        udp_port->valuedouble > 65535 ||
        udp_port->valuedouble != static_cast<double>(udp_port->valueint)) {
        return sendError(req, "udp_port must be an integer from 1 to 65535");
    }

    const int fd = httpd_req_to_sockfd(req);
    uint32_t client_ip = 0;

    if (!peerIpv4(fd, &client_ip)) {
        return sendError(req, "cannot read client IPv4 address");
    }

    Command command = {};
    command.type = isRec ? CommandType::HELLO_REC : CommandType::HELLO_PLAY;
    command.client_ip = client_ip;
    command.client_udp_port = static_cast<uint16_t>(udp_port->valueint);

    // Only claim this client after its HELLO command has entered the queue.
    if (!postCommand(command, isRec)) {
        return sendError(req, "command queue is full");
    }

    claimSession(fd);
    return sendHelloAck(req, isRec);
}

esp_err_t HttpControlServer::handleStartStop(
    httpd_req_t* req, bool start, bool isRec) {
    ESP_LOGI(TAG, "%s ", __func__);
    Command command = {};
    if (isRec)
        command.type = start ? CommandType::START_RECORD : CommandType::STOP_RECORD;
    else
        command.type = start ? CommandType::START_PLAYBACK : CommandType::STOP_PLAYBACK;    
    return postCommand(command, isRec)
        ? ESP_OK
        : sendError(req, "command queue is full");
}

esp_err_t HttpControlServer::handleTestTone(
    httpd_req_t* req, const void* payload) {
    ESP_LOGI(TAG, "%s ", __func__);
    const auto* json = static_cast<const cJSON*>(payload);
    const cJSON* enabled =
        cJSON_GetObjectItemCaseSensitive(json, KEY_ENABLED);

    if (!cJSON_IsBool(enabled)) {
        return sendError(req, "enabled must be true or false");
    }

    Command command = {};
    command.type = CommandType::SET_TEST_TONE;
    command.tone_enabled = cJSON_IsTrue(enabled);

    // record only test command, remove later.
    return postCommand(command, true)
        ? ESP_OK
        : sendError(req, "command queue is full");
}

esp_err_t HttpControlServer::handleMessage(
    httpd_req_t* req, const char* payload) {
    JsonPtr json(cJSON_Parse(payload), &cJSON_Delete);
    if (!json) return sendError(req, "invalid JSON");

    const cJSON* type =
        cJSON_GetObjectItemCaseSensitive(json.get(), KEY_TYPE);

    if (!cJSON_IsString(type)) {
        return sendError(req, "missing message type");
    }

    if (std::strcmp(type->valuestring, TYPE_HELLO_PLAY) == 0) {
        return handleHello(req, json.get(), false);
    }
    if (std::strcmp(type->valuestring, TYPE_HELLO_REC) == 0) {
        return handleHello(req, json.get(), true);
    }

    const int fd = httpd_req_to_sockfd(req);
    if (fd != client_fd_.load()) {
        return sendError(req, "send hello first");
    }

    if (std::strcmp(type->valuestring, TYPE_START_REC) == 0) {
        return handleStartStop(req, true, true /*isRec*/);
    }

    if (std::strcmp(type->valuestring, TYPE_STOP_REC) == 0) {
        return handleStartStop(req, false, true /*isRec*/);
    }

    if (std::strcmp(type->valuestring, TYPE_START_PLAY) == 0) {
        return handleStartStop(req, true, false /*isRec*/);
    }

    if (std::strcmp(type->valuestring, TYPE_STOP_PLAY) == 0) {
        return handleStartStop(req, false, false /*isRec*/);
    }

    if (std::strcmp(type->valuestring, TYPE_TEST_TONE) == 0) {
        return handleTestTone(req, json.get());
    }

    return sendError(req, "unknown message type");
}

esp_err_t HttpControlServer::wsHandler(httpd_req_t* req) {
    auto* self = static_cast<HttpControlServer*>(req->user_ctx);
    if (self == nullptr) return ESP_FAIL;

    // Some ESP-IDF versions invoke the handler for the WebSocket handshake.
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;

    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0) {
        return ESP_OK;
    }

    if (frame.len > MAX_MESSAGE_BYTES) {
        ESP_LOGW(TAG, "WebSocket message too large: %u",
                 static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }

    char buffer[MAX_MESSAGE_BYTES + 1] = {};
    frame.payload = reinterpret_cast<uint8_t*>(buffer);

    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;

    if (!frame.final) {
        return self->sendError(req, "fragmented messages are not supported");
    }

    buffer[frame.len] = '\0';
    return self->handleMessage(req, buffer);
}

void HttpControlServer::sessionClosed(httpd_handle_t, int sockfd) {
    HttpControlServer* self = instance_;
    ESP_LOGI(TAG, "%s ", __func__);
    if (self != nullptr) {
        int expected = sockfd;

        // Notify stream_task only if the disconnected socket was active.
        if (self->client_fd_.compare_exchange_strong(
                expected, NO_CLIENT_FD)) {
            Command command = {};
            command.type = CommandType::CLIENT_LEFT;
            // temp hack , send to both, we dont' whom it belong to.
            self->postCommand(command, true);
            self->postCommand(command, false);
        }
    }

    close(sockfd);
}

esp_err_t HttpControlServer::start(QueueHandle_t rec_queue, QueueHandle_t playback_queue) {
    if (server_ != nullptr) return ESP_ERR_INVALID_STATE;
    if (rec_queue == nullptr || playback_queue == nullptr) return ESP_ERR_INVALID_ARG;

    record_queue_ = rec_queue;
    playback_queue_ = playback_queue;

    instance_ = this;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.close_fn = &HttpControlServer::sessionClosed;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        instance_ = nullptr;
        return err;
    }

    httpd_uri_t websocket = {};
    websocket.uri = WS_URI;
    websocket.method = HTTP_GET;
    websocket.handler = &HttpControlServer::wsHandler;
    websocket.is_websocket = true;
    websocket.user_ctx = this;

    err = httpd_register_uri_handler(server_, &websocket);
    if (err != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        instance_ = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "WebSocket control server started at %s", WS_URI);
    return ESP_OK;
}
