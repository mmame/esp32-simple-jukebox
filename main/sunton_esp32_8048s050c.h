/**
 * @file sunton_esp32_8048s050c.h
 * @author Sven Fabricius (sven.fabricius@livediesel.de)
 * @brief
 * @version 0.1
 * @date 2024-09-09
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#define SUNTON_ESP32_LCD_WIDTH                  800
#define SUNTON_ESP32_LCD_HEIGHT                 480

#define SUNTON_ESP32_PIN_BCKL                   GPIO_NUM_2

// GT911 Pin config
#define SUNTON_ESP32_TOUCH_PIN_I2C_SCL          GPIO_NUM_20
#define SUNTON_ESP32_TOUCH_PIN_I2C_SDA          GPIO_NUM_19
#define SUNTON_ESP32_TOUCH_PIN_RST              GPIO_NUM_38
// interupt pin was falsely routed to GND instead via R17 to IO18
#define SUNTON_ESP32_TOUCH_PIN_INT              GPIO_NUM_NC

// interupt pin was falsely routed to GND, so its 0x5D
#define SUNTON_ESP32_TOUCH_ADDRESS              ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS

// 1 = show GT911 config/raw/mapped touch info and a red dot at the touch point on screen
#define SUNTON_ESP32_TOUCH_DEBUG                0

// not required, external pullups R3 / R4 in place
//#define SUNTON_ESP32_TOUCH_I2C_PULLUP           y

#define SUNTON_ESP32_BACKLIGHT_LEDC_TIMER       LEDC_TIMER_0
#define SUNTON_ESP32_BACKLIGHT_LEDC_CHANNEL     LEDC_CHANNEL_0

// SD card (SPI mode). Pin map taken from the Elecrow 5" ESP32-S3 wiki (same layout), verify on your unit.
#define SUNTON_ESP32_SD_PIN_CS                  GPIO_NUM_10
#define SUNTON_ESP32_SD_PIN_MOSI                GPIO_NUM_11
#define SUNTON_ESP32_SD_PIN_SCLK                GPIO_NUM_12
#define SUNTON_ESP32_SD_PIN_MISO                GPIO_NUM_13

// Speaker output (I2S to the on-board amplifier). GPIO0 is also the BOOT button.
#define SUNTON_ESP32_I2S_PIN_BCLK               GPIO_NUM_0
#define SUNTON_ESP32_I2S_PIN_LRC                GPIO_NUM_18
#define SUNTON_ESP32_I2S_PIN_DOUT               GPIO_NUM_17

#define LVGL_TICK_PERIOD_MS                     2
