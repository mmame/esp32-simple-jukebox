/* Keeps the MAX98357A amplifier quiet while the chip boots: its data (GPIO17) and word clock (GPIO18) inputs would
 * otherwise float and the amplifier turns that into noise. The pins are driven low as the first thing the bootloader
 * does and stay so until the I2S driver takes them over in app_main(). (BCLK is GPIO0, a strapping pin: left alone.) */
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"

#define AMP_DIN_GPIO    17
#define AMP_LRC_GPIO    18

/* Tells the linker to include this file with all its symbols. */
void bootloader_hooks_include(void)
{
}

void bootloader_before_init(void)
{
    const int pins[] = { AMP_DIN_GPIO, AMP_LRC_GPIO };
    for (int i = 0; i < 2; i++)
    {
        esp_rom_gpio_pad_select_gpio(pins[i]);
        gpio_ll_set_level(&GPIO, pins[i], 0);
        gpio_ll_output_enable(&GPIO, pins[i]);
    }
}
