
#include <cstring>

#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "softap_setup.h"
#include "esp_log.h"

const char* TAG = "WiFiMic_SoftApSetup";

void SoftApSetup::init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&icfg));

    wifi_config_t wc = {};
    strncpy(reinterpret_cast<char*>(wc.ap.ssid), AP_SSID, sizeof(wc.ap.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wc.ap.password), AP_PASS, sizeof(wc.ap.password) - 1);
    wc.ap.ssid_len       = strlen(AP_SSID);
    wc.ap.channel        = AP_CHANNEL;
    wc.ap.max_connection = AP_MAX_CLIENTS;
    wc.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    initDone = true;
    ESP_LOGI(TAG, "softap '%s' up", AP_SSID);
}