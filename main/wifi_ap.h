#pragma once

#include "esp_err.h"

/* Fixed access point credentials (shown on the player when the WiFi button is pressed). Change here if needed. */
#define WIFI_AP_SSID        "MyMusicPlayer"
#define WIFI_AP_PASSWORD    "music1234"     /* WPA2: 8..63 characters */
#define WIFI_AP_URL         "http://192.168.4.1"

/** Starts the WPA2 access point (address 192.168.4.1). */
esp_err_t wifi_ap_start(void);
/** Stops the AP and releases all WiFi memory. */
void wifi_ap_stop(void);
