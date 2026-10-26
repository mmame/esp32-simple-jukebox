/**
 * @file settings.h
 * @brief Settings that the user can change on the web page, kept in flash (NVS) over power cycles.
 */
#pragma once

#include "esp_err.h"

#define SETTINGS_DIM_S_DEFAULT      90      /* seconds without touch until the backlight is dimmed, 0 = never */
#define SETTINGS_DIM_S_MAX          3600
#define SETTINGS_DIM_LEVEL_DEFAULT  190     /* backlight when dimmed, of 255 */
#define SETTINGS_DIM_LEVEL_MIN      130
#define SETTINGS_DIM_LEVEL_MAX      255

/* Correction of the battery voltage measurement (divider tolerance, ADS1115 input loading), in 0.01 %: 10000 = x1.0000 */
#define SETTINGS_BATT_CAL_DEFAULT   10000
#define SETTINGS_BATT_CAL_MIN       9000
#define SETTINGS_BATT_CAL_MAX       11000

/** Opens the storage and loads the values (defaults if nothing is stored). */
void settings_init(void);

int settings_dim_timeout_s(void);
int settings_dim_level(void);

/** Clamps to the allowed ranges, stores and applies at once. */
esp_err_t settings_set_dim(int timeout_s, int level);

#define SETTINGS_WIFI_SSID_MAX      32      /* characters, without the terminator */
#define SETTINGS_WIFI_PASS_MIN      8       /* WPA2 */
#define SETTINGS_WIFI_PASS_MAX      63

/** Access point name and password (defaults from wifi_ap.h). Used the next time the WiFi is switched on. */
const char *settings_wifi_ssid(void);
const char *settings_wifi_password(void);
/** Stores new credentials; ESP_ERR_INVALID_ARG if the SSID is empty/too long or the password is not 8..63 characters. */
esp_err_t settings_set_wifi(const char *ssid, const char *password);

#define SETTINGS_EQ_DEFAULT_BASS    3       /* dB, see eq.h */
#define SETTINGS_EQ_DEFAULT_TREBLE  2
#define SETTINGS_EQ_MAX             6

/** Tone control in dB (0..SETTINGS_EQ_MAX); a change is audible at once. */
int settings_eq_bass(void);
int settings_eq_treble(void);
esp_err_t settings_set_eq(int bass, int treble);

/** Battery voltage correction factor in 0.01 % units (see SETTINGS_BATT_CAL_*). */
int settings_batt_cal(void);
esp_err_t settings_set_batt_cal(int cal);
