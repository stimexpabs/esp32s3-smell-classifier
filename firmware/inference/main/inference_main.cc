/*
 * MQ-135 smell classifier (tutorial stage 8).
 *
 * Samples the sensor at 10 Hz into a 10 s ring buffer. Once a second it normalises the buffer exactly as
 * training/preprocess.py does, runs the int8 model with TensorFlow Lite Micro and prints the result. When an event
 * is recognised the buzzer beeps: once for breath, twice for alcohol, three times for smoke.
 *
 * For testing, the PC can send a window of 100 voltages on one line:
 *
 *     R <v0> <v1> ... <v99>\n      answered with     R,<out0>,<out1>,<out2>,<out3>
 *
 * The answer is the model's raw int8 output, which tools/replay.py compares with the same model run on the PC.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "board_config.h"
#include "buzzer.h"
#include "model_data.h"

// Pin and divider come from firmware/components/board/include/board_config.h.
#define MQ135_ADC_CHANNEL   BOARD_MQ135_ADC_CHANNEL
#define MQ135_ADC_ATTEN     ADC_ATTEN_DB_12
#define DIVIDER_GAIN        ((float)(BOARD_DIVIDER_TOP_OHM + BOARD_DIVIDER_BOTTOM_OHM) / BOARD_DIVIDER_BOTTOM_OHM)

#define SAMPLE_PERIOD_MS    100   // 10 Hz, the rate the model was trained on
#define OVERSAMPLE          64
#define SAMPLES_PER_INFER   10    // classify once a second

// Deciding what an event is. A response needs a few seconds to show its shape, so the first non-clean
// prediction only opens a short voting period; the class with the most votes is announced.
#define MIN_CONFIDENCE      0.60f // below this a prediction counts as clean
#define START_STREAK        2     // consecutive non-clean predictions that open the vote
#define VOTE_PREDICTIONS    5     // predictions collected before deciding
#define MIN_EVENT_VOTES     3     // non-clean votes needed to announce anything
#define HOLDOFF_S           20    // quiet period after an announcement, while the sensor recovers

#define BEEP_FREQ_HZ        2700
#define BEEP_MS             80

#define TENSOR_ARENA_BYTES  (16 * 1024)

static const char *TAG = "inference";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static uint8_t s_arena[TENSOR_ARENA_BYTES] __attribute__((aligned(16)));
static tflite::MicroInterpreter *s_interpreter;
static TfLiteTensor *s_input;
static TfLiteTensor *s_output;
static SemaphoreHandle_t s_model_lock;   // the sampling loop and the replay task both run the model

static float s_ring[MODEL_WINDOW];
static int s_ring_next;
static int s_ring_count;

static void sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &s_adc));

    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = MQ135_ADC_ATTEN;
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, MQ135_ADC_CHANNEL, &channel_config));

    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = MQ135_ADC_CHANNEL;
    cali_config.atten = MQ135_ADC_ATTEN;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali));
}

// Sensor output in millivolts, averaged over 64 reads. Returns a negative value if the ADC failed.
static float sensor_read_mv(void)
{
    int sum_mv = 0;
    int reads = 0;
    for (int i = 0; i < OVERSAMPLE; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, MQ135_ADC_CHANNEL, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            sum_mv += mv;
            reads++;
        }
    }
    if (reads == 0) {
        return -1.0f;
    }
    return (float)sum_mv / reads * DIVIDER_GAIN;
}

static void model_init(void)
{
    const tflite::Model *model = tflite::GetModel(g_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model schema %d, library expects %d", (int)model->version(), TFLITE_SCHEMA_VERSION);
        abort();
    }

    // Only the operators listed here are linked into the firmware. The list comes from training/convert.py.
    static tflite::MicroMutableOpResolver<7> resolver;
    resolver.AddConv2D();
    resolver.AddExpandDims();
    resolver.AddFullyConnected();
    resolver.AddMaxPool2D();
    resolver.AddMean();
    resolver.AddReshape();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter interpreter(model, resolver, s_arena, TENSOR_ARENA_BYTES);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed: the tensor arena is too small or an operator is missing");
        abort();
    }
    s_interpreter = &interpreter;
    s_input = interpreter.input(0);
    s_output = interpreter.output(0);
    s_model_lock = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "model: %u bytes, arena used %u of %d bytes", g_model_data_len,
             (unsigned)interpreter.arena_used_bytes(), TENSOR_ARENA_BYTES);
    ESP_LOGI(TAG, "input scale %f, zero point %d", s_input->params.scale, (int)s_input->params.zero_point);
}

/*
 * Run the model on one window of sensor voltages, oldest sample first.
 *
 * The feature maths must match training/preprocess.py: every sample relative to the mean of the window's first
 * second, on a log scale, clipped. The result is then converted to int8 with the scale and zero point stored in
 * the model. `out` receives the raw int8 outputs. Returns the time the interpreter took, or -1 on failure.
 */
static int64_t classify(const float *v, int8_t *out)
{
    float ref = 0.0f;
    for (int i = 0; i < MODEL_REF_SAMPLES; i++) {
        ref += v[i];
    }
    ref /= MODEL_REF_SAMPLES;
    if (ref <= 0.0f) {
        return -1;
    }

    xSemaphoreTake(s_model_lock, portMAX_DELAY);
    const float scale = s_input->params.scale;
    const int zero_point = s_input->params.zero_point;
    for (int i = 0; i < MODEL_WINDOW; i++) {
        float x = v[i] > 0.0f ? logf(v[i] / ref) : MODEL_CLIP_LO;
        x = fminf(fmaxf(x, MODEL_CLIP_LO), MODEL_CLIP_HI);
        long q = lroundf(x / scale) + zero_point;
        s_input->data.int8[i] = (int8_t)(q < -128 ? -128 : q > 127 ? 127 : q);
    }

    int64_t start = esp_timer_get_time();
    TfLiteStatus status = s_interpreter->Invoke();
    int64_t elapsed = esp_timer_get_time() - start;
    memcpy(out, s_output->data.int8, MODEL_CLASS_COUNT);
    xSemaphoreGive(s_model_lock);
    return status == kTfLiteOk ? elapsed : -1;
}

// An int8 output back to a probability: real = scale * (q - zero_point).
static float probability(int8_t q)
{
    return s_output->params.scale * (q - s_output->params.zero_point);
}

// Reads "R v0 ... v99" lines from the PC and answers with the model's raw outputs.
static void replay_task(void *arg)
{
    static char line[MODEL_WINDOW * 12 + 16];
    static float v[MODEL_WINDOW];
    size_t len = 0;

    while (true) {
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (c != '\n') {
            if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
            continue;
        }
        line[len] = '\0';
        len = 0;
        if (line[0] != 'R') {
            continue;
        }

        char *cursor = line + 1;
        int count = 0;
        while (count < MODEL_WINDOW) {
            char *end;
            float value = strtof(cursor, &end);
            if (end == cursor) {
                break;
            }
            v[count++] = value;
            cursor = end;
        }

        int8_t out[MODEL_CLASS_COUNT];
        if (count != MODEL_WINDOW || classify(v, out) < 0) {
            printf("R,error\n");
            continue;
        }
        printf("R");
        for (int i = 0; i < MODEL_CLASS_COUNT; i++) {
            printf(",%d", out[i]);
        }
        printf("\n");
    }
}

static void announce(int cls)
{
    printf(">>> EVENT: %s\n", kModelClasses[cls]);
    buzzer_beep(BEEP_FREQ_HZ, BEEP_MS, cls);   // class index = number of beeps
}

// Called once a second with the newest prediction. Turns a stream of predictions into single events.
static void track_event(int cls)
{
    static enum { IDLE, VOTING, HOLDOFF } state = IDLE;
    static int streak, collected, holdoff;
    static int votes[MODEL_CLASS_COUNT];

    switch (state) {
    case IDLE:
        streak = cls != 0 ? streak + 1 : 0;
        if (streak >= START_STREAK) {
            memset(votes, 0, sizeof(votes));
            collected = 0;
            state = VOTING;
        }
        break;

    case VOTING: {
        votes[cls]++;
        if (++collected < VOTE_PREDICTIONS) {
            break;
        }
        int winner = 1;
        int event_votes = 0;
        for (int i = 1; i < MODEL_CLASS_COUNT; i++) {
            event_votes += votes[i];
            if (votes[i] > votes[winner]) {
                winner = i;
            }
        }
        streak = 0;
        if (event_votes >= MIN_EVENT_VOTES) {
            announce(winner);
            holdoff = HOLDOFF_S;
            state = HOLDOFF;
        } else {
            state = IDLE;   // it faded before it looked like anything
        }
        break;
    }

    case HOLDOFF:
        if (--holdoff <= 0) {
            state = IDLE;
        }
        break;
    }
}

extern "C" void app_main(void)
{
    // Route stdin/stdout through the USB driver. The receive buffer has to hold a whole replay line.
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_config.rx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();

    sensor_init();
    model_init();
    ESP_ERROR_CHECK(buzzer_init());
    xTaskCreate(replay_task, "replay", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "collecting the first %d s of samples", MODEL_WINDOW * SAMPLE_PERIOD_MS / 1000);
    buzzer_beep(BEEP_FREQ_HZ, BEEP_MS, 1);

    static float window[MODEL_WINDOW];
    int since_infer = 0;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        float mv = sensor_read_mv();
        if (mv >= 0.0f) {
            s_ring[s_ring_next] = mv;
            s_ring_next = (s_ring_next + 1) % MODEL_WINDOW;
            if (s_ring_count < MODEL_WINDOW) {
                s_ring_count++;
            }
            since_infer++;
        }

        if (s_ring_count == MODEL_WINDOW && since_infer >= SAMPLES_PER_INFER) {
            since_infer = 0;
            // Unroll the ring so the oldest sample comes first, as in a training window.
            for (int i = 0; i < MODEL_WINDOW; i++) {
                window[i] = s_ring[(s_ring_next + i) % MODEL_WINDOW];
            }

            int8_t out[MODEL_CLASS_COUNT];
            int64_t infer_us = classify(window, out);
            if (infer_us >= 0) {
                int best = 0;
                for (int i = 1; i < MODEL_CLASS_COUNT; i++) {
                    if (out[i] > out[best]) {
                        best = i;
                    }
                }
                bool confident = probability(out[best]) >= MIN_CONFIDENCE;

                printf("%7.1f s  %7.1f mV  %-8s %3d%% %s |", esp_timer_get_time() / 1e6, mv, kModelClasses[best],
                       (int)lroundf(probability(out[best]) * 100), confident ? " " : "?");
                for (int i = 0; i < MODEL_CLASS_COUNT; i++) {
                    printf(" %s %d%%", kModelClasses[i], (int)lroundf(probability(out[i]) * 100));
                }
                printf(" | %lld us\n", infer_us);

                track_event(confident ? best : 0);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
