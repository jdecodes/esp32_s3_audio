idf_component_register(SRCS "stream_controller.cpp" "softap_setup.cpp"
                            
                             "playback_controller.cpp"
                             "udp_audio_sender.cpp"
                             "udp_audio_receiver.cpp"
                             "mic_streamer.cpp"
                             "speaker_streamer.cpp"
                             "control_server.cpp"
                             "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi 
                             esp_netif
                             esp_event
                             nvs_flash
                             lwip
                             esp_timer
                             esp_driver_i2s
                             esp_driver_gpio
                             esp_http_server)
)

