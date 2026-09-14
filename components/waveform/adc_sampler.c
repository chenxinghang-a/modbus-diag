#include "adc_sampler.h"
#include "app_config.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "adc";

static adc_continuous_handle_t s_adc_handle = NULL;
static uint16_t s_sample_buf[WAVE_SAMPLE_DEPTH];
static volatile bool s_running = false;
static volatile int s_write_idx = 0;
static SemaphoreHandle_t s_buf_mutex = NULL;

/* ADC continuous callback: called when DMA buffer is filled */
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle,
                                        const adc_continuous_evt_data_t *edata,
                                        void *user_data)
{
    /* Process samples in ISR context - just copy to buffer */
    const uint8_t *p = edata->conv_frame_buffer;
    uint32_t len = edata->size;

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= len; i += SOC_ADC_DIGI_RESULT_BYTES) {
        adc_digi_output_data_t *item = (adc_digi_output_data_t *)&p[i];
        uint16_t val = item->type2.data;
        s_sample_buf[s_write_idx] = val;
        s_write_idx++;
        if (s_write_idx >= WAVE_SAMPLE_DEPTH) {
            s_write_idx = 0;
        }
    }

    return false;  /* No need to yield */
}

esp_err_t adc_sampler_init(void)
{
    if (s_adc_handle) return ESP_OK;  /* Already initialized */

    if (!s_buf_mutex) {
        s_buf_mutex = xSemaphoreCreateMutex();
    }

    /* Configure ADC continuous mode */
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = WAVE_SAMPLE_DEPTH * 2 * 2,  /* double buffer */
        .conv_frame_size = WAVE_SAMPLE_DEPTH * SOC_ADC_DIGI_RESULT_BYTES,
    };
    esp_err_t ret = adc_continuous_new_handle(&handle_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure ADC channel */
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = WAVE_SAMPLE_RATE,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = WAVE_ADC_CHANNEL,
        .unit = WAVE_ADC_UNIT,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = &pattern;

    ret = adc_continuous_config(s_adc_handle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC config failed: %s", esp_err_to_name(ret));
        adc_continuous_deinit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "ADC continuous sampler initialized (%d Hz)", WAVE_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t adc_sampler_start(void)
{
    if (s_running) return ESP_OK;
    if (!s_adc_handle) {
        esp_err_t ret = adc_sampler_init();
        if (ret != ESP_OK) return ret;
    }

    memset(s_sample_buf, 0, sizeof(s_sample_buf));
    s_write_idx = 0;
    s_running = true;

    /* Register conversion done callback */
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
    };
    esp_err_t ret = adc_continuous_register_event_callbacks(s_adc_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC callback register failed: %s", esp_err_to_name(ret));
        s_running = false;
        return ret;
    }

    ret = adc_continuous_start(s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC start failed: %s", esp_err_to_name(ret));
        s_running = false;
        return ret;
    }

    ESP_LOGI(TAG, "ADC sampling started");
    return ESP_OK;
}

void adc_sampler_stop(void)
{
    if (!s_running) return;
    s_running = false;

    if (s_adc_handle) {
        adc_continuous_stop(s_adc_handle);
    }

    ESP_LOGI(TAG, "ADC sampling stopped");
}

const uint16_t *adc_sampler_get_buffer(void)
{
    return s_sample_buf;
}

int adc_sampler_get_depth(void)
{
    return WAVE_SAMPLE_DEPTH;
}
