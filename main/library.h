/**
 * @file library.h
 * @brief Music library on the SD card. One album (= one "CD") per folder directly below SD_MOUNT_POINT:
 *
 *          /sdcard/<Album name>/cover.jpg     square cover (optional)
 *          /sdcard/<Album name>/<track>.mp3   tracks, played in file name order
 *
 *        The folder name is the title shown in the UI.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct {
    char *title;        /* folder name */
    char *cover;        /* full path of cover.jpg, NULL if the album has none */
    char **tracks;      /* full paths, sorted by name */
    size_t n_tracks;
} library_album_t;

/** (Re)scans the SD card. Invalidates pointers from a previous scan. */
esp_err_t library_scan(void);
/** Albums sorted by title; the array lives until the next library_scan(). */
const library_album_t *library_albums(size_t *count);
