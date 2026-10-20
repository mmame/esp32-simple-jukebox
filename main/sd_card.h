#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SD_MOUNT_POINT "/sdcard"

/** Mounts the FAT filesystem of the SD card (SPI mode) at SD_MOUNT_POINT. */
esp_err_t sd_card_mount(void);

/** True once sd_card_mount() succeeded. */
bool sd_card_is_mounted(void);
