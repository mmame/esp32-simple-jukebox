idf_component_register(SRCS "main.c" "sunton_esp32_8048s050c.c" "sd_card.c" "library.c" "player.c" "ui.c"
                           "wifi_ap.c" "web_server.c" "upload_mode.c" "volume_pot.c" "settings.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "web/index.html" "web/lame.min.js"
                    LDFRAGMENTS "linker.lf"
                    REQUIRES lvgl esp_lcd esp_timer driver esp_lcd_touch_gt911 fatfs sdmmc esp-libhelix-mp3
                             esp_wifi esp_netif esp_event nvs_flash esp_http_server json app_update esp_app_format)
