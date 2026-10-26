/**
 * @file eq.h
 * @brief Simple tone control for the small speaker (Faital 3FE22 in about 1.5 l, sealed):
 *          - high-pass 80 Hz (fixed): the cone cannot play below that and only moves (Xmax 1.8 mm)
 *          - bass: low shelf at 160 Hz, 0..+6 dB
 *          - treble: high shelf at 6 kHz, 0..+6 dB
 *        No fixed level reduction: the volume gain (<= 1) is applied inside, and a soft limiter catches the peaks that
 *        the boost pushes above full scale (only the highest volume steps reach it).
 *        Bass and treble come from settings.h; a change is picked up by the next eq_process() call.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Filters interleaved stereo 16-bit samples in place, applies the volume (gain_q15, 32768 = 1.0) and limits. With `mono` the two channels carry the same signal: only the left
 * one is filtered and copied to the right (half the work).
 */
void eq_process(int16_t *pcm, int frames, uint32_t rate, bool mono, int32_t gain_q15);
