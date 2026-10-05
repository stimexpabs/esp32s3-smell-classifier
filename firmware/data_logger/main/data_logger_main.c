/*
 * MQ-135 data logger (tutorial stage 3).
 *
 * Streams one line per sample over USB at 10 Hz:
 *
 *     D,<t_ms>,<adc_mv>,<true_mv>,<rs_kohm>
 *
 * and takes one-byte commands from the PC, used by tools/collect.py to mark when a stimulus is applied:
 *
 *     '1'  one beep,  prints M,<t_ms>,1   (stimulus on)
 *     '0'  two beeps, prints M,<t_ms>,0   (stimulus off)
 *
 * The marker carries the board's own timestamp, so it lines up with the samples exactly, whatever the USB latency.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_config.h"
#include "buzzer.h"

// Pin, divider and load resistor come from firmware/components/board/include/board_config.h.
#define MQ135_ADC_CHANNEL   BOARD_MQ135_ADC_CHANNEL
#define MQ135_ADC_ATTEN     ADC_ATTEN_DB_12

// The divider in front of the pin scales the sensor voltage down: V_sensor = V_pin * (TOP + BOTTOM) / BOTTOM.
#define DIVIDER_TOTAL_OHM   (BOARD_DIVIDER_TOP_OHM + BOARD_DIVIDER_BOTTOM_OHM)

// Sensor resistance: Rs = RL_eff * (VCC - V_sensor) / V_sensor. The divider is in parallel with the module's
// load resistor, so the sensor works into RL_eff, not RL.
#define MQ135_RL_EFF_OHM    ((int64_t)BOARD_MQ135_RL_OHM * DIVIDER_TOTAL_OHM / (BOARD_MQ135_RL_OHM + DIVIDER_TOTAL_OHM))
#define MQ135_VCC_CMV       500000  // 5 V sensor supply, in hundredths of a millivolt

#define SAMPLE_PERIOD_MS    100   // 10 Hz
#define OVERSAMPLE          64    // ADC reads averaged into one sample

#define BEEP_FREQ_HZ        2700
#define BEEP_MS             80

static const char *TAG = "data_logger";

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void command_task(void *arg)
{
    uint8_t c;
    while (1) {
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (c == '1') {
            buzzer_beep(BEEP_FREQ_HZ, BEEP_MS, 1);
            printf("M,%lld,1\n", now_ms());
        } else if (c == '0') {
            buzzer_beep(BEEP_FREQ_HZ, BEEP_MS, 2);
            printf("M,%lld,0\n", now_ms());
        }
    }
}

void app_main(void)
{
    // Route stdin/stdout through the USB driver so reads block instead of polling.
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = MQ135_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, MQ135_ADC_CHANNEL, &channel_config));

    // Calibration turns raw counts into millivolts using the per-chip values burned into eFuse at the factory.
    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = MQ135_ADC_CHANNEL,
        .atten = MQ135_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali));

    ESP_ERROR_CHECK(buzzer_init());
    xTaskCreate(command_task, "command", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "logging MQ-135 on ADC1 channel %d at %d Hz", MQ135_ADC_CHANNEL, 1000 / SAMPLE_PERIOD_MS);
    buzzer_beep(BEEP_FREQ_HZ, BEEP_MS, 1);

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        int64_t t_ms = now_ms();

        int sum_mv = 0;
        int reads = 0;
        for (int i = 0; i < OVERSAMPLE; i++) {
            int raw, mv;
            if (adc_oneshot_read(adc, MQ135_ADC_CHANNEL, &raw) == ESP_OK &&
                adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
                sum_mv += mv;
                reads++;
            }
        }

        if (reads > 0) {
            // Hundredths of a millivolt, in integers: averaging 64 reads resolves well below the ADC's 1 mV steps.
            int adc_cmv = sum_mv * 100 / reads;
            int true_cmv = (int)((int64_t)adc_cmv * DIVIDER_TOTAL_OHM / BOARD_DIVIDER_BOTTOM_OHM);
            printf("D,%lld,%d.%02d,%d.%02d,", t_ms, adc_cmv / 100, adc_cmv % 100, true_cmv / 100, true_cmv % 100);
            if (true_cmv > 0) {
                int rs_ohm = (int)(MQ135_RL_EFF_OHM * (MQ135_VCC_CMV - true_cmv) / true_cmv);
                printf("%d.%02d\n", rs_ohm / 1000, rs_ohm % 1000 / 10);
            } else {
                printf("nan\n");   // 0 V at the sensor: Rs is infinite (sensor unplugged)
            }
        } else {
            ESP_LOGW(TAG, "ADC read failed");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
