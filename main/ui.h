/**
 * @file ui.h
 * @brief The whole UI: a scrollable grid of album covers (tap = play) and a control bar with
 *        Prev, Play/Pause, Next, Stop and Volume -/+.
 */
#pragma once

#include <stddef.h>
#include "library.h"
#include "player.h"

/** Builds the screen. Call with the LVGL lock held; 'albums' must outlive the UI. */
void ui_create(const library_album_t *albums, size_t n_albums);

/** Pass to player_init(). Safe to call from the player task: it only stores a snapshot that the UI polls. */
void ui_player_event(player_state_t state, size_t track, int volume);
