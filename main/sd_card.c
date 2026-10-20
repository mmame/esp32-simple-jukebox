#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

#include "sd_card.h"
#include "sunton_esp32_8048s050c.h"

static const char *TAG = "sd_card";

static bool s_mounted;

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_card_mount(void)
{
    if (s_mounted)
        return ESP_OK;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = SUNTON_ESP32_SD_PIN_MOSI,
        .miso_io_num = SUNTON_ESP32_SD_PIN_MISO,
        .sclk_io_num = SUNTON_ESP32_SD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SUNTON_ESP32_SD_PIN_CS;
    slot_cfg.host_id = host.slot;

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "mount failed: %s (card inserted? FAT32?)", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }
    sdmmc_card_print_info(stdout, card);
    s_mounted = true;
    return ESP_OK;
}
