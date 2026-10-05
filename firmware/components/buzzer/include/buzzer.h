#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set up the buzzer on BOARD_BUZZER_GPIO (LEDC timer 1 / channel 1). Call once.
 */
esp_err_t buzzer_init(void);

/**
 * @brief Play `count` beeps of `on_ms` each, separated by an equal pause.
 *
 * Returns immediately: the beeps are timed by an esp_timer, so the caller's sampling loop is never delayed.
 * A new call replaces a pattern that is still playing.
 */
void buzzer_beep(uint32_t freq_hz, uint32_t on_ms, uint32_t count);

#ifdef __cplusplus
}
#endif
