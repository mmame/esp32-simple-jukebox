#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#include "display_board.h"
#include "sd_card.h"
#include "library.h"
#include "player.h"
#include "settings.h"
#include "ui.h"
#include "volume_pot.h"

static const char *TAG = "musicplayer";

void app_main(void)
{
    /* first of all: the I2S clocks run (silence) as early as the code can, so the amplifier does not play noise
     * while the rest of the system starts up */
    ESP_ERROR_CHECK(player_init(ui_player_event));
    ESP_LOGI(TAG, "app_main: free heap %u", (unsigned)esp_get_free_heap_size());
    settings_init();
    board_backlight_init();

    lv_display_t *disp = board_lcd_init();
    ESP_LOGI(TAG, "LCD + LVGL initialised (%dx%d), free heap %u",
             (int)lv_display_get_horizontal_resolution(disp), (int)lv_display_get_vertical_resolution(disp),
             (unsigned)esp_get_free_heap_size());

    i2c_master_bus_handle_t i2c_master = board_i2c_master();
    board_touch_init(i2c_master);
    volume_pot_init(i2c_master);        /* optional: potentiometer on an ADS1115 */

    size_t n_albums = 0;
    const library_album_t *albums = NULL;
    bool sd_ok = false;
    for (int attempt = 1; attempt <= 5 && !sd_ok; attempt++)
    {
        sd_ok = sd_card_mount() == ESP_OK;
        if (!sd_ok)
        {
            ESP_LOGW(TAG, "SD card not found (attempt %d/5)", attempt);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (sd_ok && library_scan() == ESP_OK)
        albums = library_albums(&n_albums);

    lv_lock();
    ui_create(albums, n_albums);
    lv_unlock();
    ESP_LOGI(TAG, "UI created, %u albums, free heap %u", (unsigned)n_albums, (unsigned)esp_get_free_heap_size());
}
