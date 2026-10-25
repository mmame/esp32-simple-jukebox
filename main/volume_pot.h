/**
 * @file volume_pot.h
 * @brief Optional ADS1115 at the touch I2C bus (SDA 19, SCL 20, address 0x48):
 *          AIN0  volume potentiometer (ends on 3.3 V and GND, wiper on AIN0)
 *          AIN1  battery voltage through a 1:1 divider (VBat = 2 x measured voltage)
 *        Without an ADS1115 everything keeps working: touch volume buttons, no battery icon.
 *        A small task does the I2C reads, the functions below only look at the latest values.
 */
#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

#define VOLUME_POT_I2C_ADDR     0x48    /* ADS1115 with ADDR on GND */

/** Looks for the ADS1115 and starts the reader task. Returns false (and stays inactive) if it does not answer. */
bool volume_pot_init(i2c_master_bus_handle_t bus);

/** True if an ADS1115 was found by volume_pot_init(). */
bool volume_pot_present(void);

/**
 * Pot position, call it a few times per second. Returns true and sets *step (0..max) when the position changed
 * (always at the first call after init), false when nothing changed or there is no pot.
 */
bool volume_pot_changed(int max, int *step);

/**
 * Battery state. Returns false when there is no ADS1115 or no battery voltage (below 2.5 V).
 * *percent is the state of charge 0..100 from a Li-ion discharge curve (smoothed), *volts the battery voltage,
 * *external is true when the voltage is above what a cell can have (e.g. 5 V supply on the divider).
 */
bool battery_read(int *percent, float *volts, bool *external);

/** Unsmoothed battery voltage of the latest measurement (for diagnosis). False if there is none. */
bool battery_read_instant(float *volts);
