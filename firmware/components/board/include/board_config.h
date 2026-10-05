/*
 * Everything that depends on how the hardware is wired. Both firmwares and the buzzer driver read it from here,
 * so this is the only file to edit for a different board.
 */
#pragma once

// MQ-135 analog output (AO). It must be an ADC1 pin: on the ESP32-S3, GPIO1 to GPIO10 are ADC1 channels 0 to 9.
#define BOARD_MQ135_ADC_CHANNEL     ADC_CHANNEL_5   // GPIO6

// Resistor divider between AO and the pin, which brings the sensor's 0-5 V down to the ADC's range:
// AO -- TOP --+-- pin
//             BOTTOM
//             |
//            GND
#define BOARD_DIVIDER_TOP_OHM       4700
#define BOARD_DIVIDER_BOTTOM_OHM    6800

// Load resistor on the MQ-135 module, the SMD part next to the AO pin: "102" = 1000, "202" = 2000, "103" = 10000.
// Only the Rs column of the data logger depends on it.
#define BOARD_MQ135_RL_OHM          2000

// Passive buzzer, driven through a transistor. Set to -1 if the board has none.
#define BOARD_BUZZER_GPIO           38
