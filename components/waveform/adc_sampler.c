#include "adc_sampler.h"
#include "app_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "adc";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static uint16_t s_sample_buf[WAVE_SAMPLE_DEPTH];
static bool s_running = false;

esp_err_t adc_sampler_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = WAVE_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) return ret;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, WAVE_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "ADC sampler initialized");
    return ESP_OK;
}

static void sampler_task(void *arg)
{
    int idx = 0;
    while (s_running) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, WAVE_ADC_CHANNEL, &raw) == ESP_OK) {
            s_sample_buf[idx] = raw;
            idx++;
            if (idx >= WAVE_SAMPLE_DEPTH) idx = 0;
        }
        /* ~100KSPS target: 10us per sample, but task delay min is 1ms.
           In practice, read + loop takes ~20-50us per sample. */
    }
    vTaskDelete(NULL);
}

esp_err_t adc_sampler_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    memset(s_sample_buf, 0, sizeof(s_sample_buf));
    xTaskCreatePinnedToCore(sampler_task, "adc_samp", 2048, NULL, 6, NULL, 0);
    return ESP_OK;
}

void adc_sampler_stop(void)
{
    s_running = false;
}

const uint16_t *adc_sampler_get_buffer(void)
{
    return s_sample_buf;
}

int adc_sampler_get_depth(void)
{
    return WAVE_SAMPLE_DEPTH;
}
