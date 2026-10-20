/**
 * @file web_server.h
 * @brief Upload page and its REST API (all under /api):
 *          GET    /api/albums                        albums, tracks, free space, firmware info
 *          GET    /api/cover?album=                  cover.jpg of an album
 *          PUT    /api/file?album=&file=             raw body -> /sdcard/<album>/<file> (<track>.mp3 or cover.jpg)
 *          DELETE /api/file?album=&file=
 *          DELETE /api/album?album=
 *          POST   /api/rename?album=&to=                rename an album (409 if the new name exists)
 *          POST   /api/ota                           raw app image (esp32-cyd-musicplayer-ota.bin), reboots
 *          POST   /api/done                          the user finished uploading
 */
#pragma once

#include "esp_err.h"

/** on_done is called (from the HTTP task) when the page posts /api/done. */
esp_err_t web_server_start(void (*on_done)(void));
void web_server_stop(void);
