/**
 * @file screenshot.h
 * @brief GET /screenshot.png: the screen of the player as a PNG, for documentation.
 *        LVGL renders the active screen into a buffer (without the modal "WiFi" box, which lives on a layer above),
 *        the rows are compressed with the deflate code in the chip's ROM (miniz) and streamed to the browser.
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t screenshot_send_png(httpd_req_t *req);
