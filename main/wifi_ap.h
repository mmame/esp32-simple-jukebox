#pragma once

#include "esp_err.h"

/* Default access point credentials; the user can change them on the web page (settings.h). Shown on the player when the WiFi button is pressed. */
#define WIFI_AP_SSID        "MyMusicPlayer"
#define WIFI_AP_PASSWORD    "music1234"     /* WPA2: 8..63 characters */
#define WIFI_AP_URL         "http://192.168.4.1"

/** Starts the WPA2 access point (address 192.168.4.1). */
esp_err_t wifi_ap_start(void);
/** Stops the AP and releases all WiFi memory. */
void wifi_ap_stop(void);
