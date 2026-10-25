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

/** Opens the storage and loads the values (defaults if nothing is stored). */
void settings_init(void);

int settings_dim_timeout_s(void);
int settings_dim_level(void);

/** Clamps to the allowed ranges, stores and applies at once. */
esp_err_t settings_set_dim(int timeout_s, int level);
