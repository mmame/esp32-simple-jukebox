/**
 * @file upload_mode.h
 * @brief "Add music" mode: stops playback, opens the WiFi access point (credentials in wifi_ap.h) and serves the upload page.
 *        Normal operation never has WiFi on.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[16];
    char url[32];
} upload_info_t;

/** on_done is called from the HTTP task when the page says it is finished. */
esp_err_t upload_mode_start(upload_info_t *info, void (*on_done)(void));
void upload_mode_stop(void);
