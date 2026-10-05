/*
 * Minimal non-blocking buzzer driver.
 *
 * The buzzer is a passive (magnetic) type behind a transistor, so it needs a square wave: LEDC generates it and the
 * tone frequency is the PWM frequency. 50% duty is the loudest point.
 */
#include "buzzer.h"

#include <stdbool.h>
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "board_config.h"

#define BUZZER_GPIO       BOARD_BUZZER_GPIO
#define BUZZER_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER      LEDC_TIMER_1
#define BUZZER_CHANNEL    LEDC_CHANNEL_1
#define BUZZER_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_DUTY_ON    (1 << 9)   // 50% of 10 bits

static const char *TAG = "buzzer";

static esp_timer_handle_t s_timer;
static uint32_t s_freq_hz;
static uint32_t s_on_us;
static uint32_t s_remaining;   // beeps still to play, including the one sounding now
static bool s_sounding;

static void set_tone(bool on)
{
    if (on) {
        ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, s_freq_hz);
    }
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, on ? BUZZER_DUTY_ON : 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    s_sounding = on;
}

// Runs in the esp_timer task at the end of every beep and every pause.
static void step_cb(void *arg)
{
    if (s_sounding) {
        set_tone(false);
        if (--s_remaining == 0) {
            return;
        }
    } else {
        set_tone(true);
    }
    esp_timer_start_once(s_timer, s_on_us);
}

esp_err_t buzzer_init(void)
{
    if (BUZZER_GPIO < 0) {
        return ESP_OK;   // no buzzer on this board: buzzer_beep() does nothing
    }

    ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .timer_num = BUZZER_TIMER,
        .duty_resolution = BUZZER_RESOLUTION,
        .freq_hz = 2700,
        .clk_cfg = LEDC_USE_XTAL_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer config failed");

    ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,   // silent until the first beep
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "channel config failed");

    const esp_timer_create_args_t args = { .callback = step_cb, .name = "buzzer" };
    return esp_timer_create(&args, &s_timer);
}

void buzzer_beep(uint32_t freq_hz, uint32_t on_ms, uint32_t count)
{
    if (s_timer == NULL || count == 0) {
        return;
    }
    esp_timer_stop(s_timer);   // fails harmlessly when nothing is playing
    s_freq_hz = freq_hz;
    s_on_us = on_ms * 1000;
    s_remaining = count;
    set_tone(true);
    esp_timer_start_once(s_timer, s_on_us);
}
