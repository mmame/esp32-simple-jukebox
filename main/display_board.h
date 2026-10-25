/**
 * @file display_board.h
 * @brief Board abstraction for the Sunton ESP32-8048S050C (ESP32-S3, 800x480 RGB panel, GT911 touch).
 */
#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

void board_backlight_init(void);
/** Backlight brightness 0 (off) .. 255 (full). */
void board_backlight_set(uint8_t duty);
/** Creates the panel + LVGL display (and the LVGL task); returns the display. */
lv_display_t *board_lcd_init(void);
/** Lowers (true) or restores (false) the pixel clock; takes effect at the next vsync. Use while WiFi is on. */
void board_lcd_set_low_bandwidth(bool low);
i2c_master_bus_handle_t board_i2c_master(void);
/** Creates the LVGL pointer input device for the touch controller. */
void board_touch_init(i2c_master_bus_handle_t i2c_master);
