#include "adc_sampler.h"
#include "app_config.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "adc";

/* ---- 双缓冲帧交换（2026-10-08 改造） --------------------------------------
 *
 * 改造前：ISR 以 100KSPS 逐样本写入单个环形缓冲，UI 直接读"正在被写"的那块
 * —— 帧间隔仅 10.24ms，而消费侧要读满 1024 点 + SMA 滤波 + 渲染，耗时远超
 * 一帧，必然读到撕裂波形（Vpp/DC/触发点全错）。原有的 s_buf_mutex 创建后
 * 从未使用（ghost 代码），一并删除。
 *
 * 现在：conv_done 回调每次对应一个完整 DMA 帧（conv_frame_size = 1024 样本），
 * ISR 把该帧写进 s_buf[s_active]，整帧写完后在临界区里：
 *   ① 登记 s_ready = s_active（若旧帧未被消费 → s_dropped++，最新帧优先）
 *   ② 翻转 s_active 到另一块
 * 消费侧 read_frame()：临界区内"领取索引 + 清信箱"，出临界区后 memcpy ——
 * ISR 已翻转到另一块，被领走的块在两个帧周期（~20ms）内不会被回写，
 * 而 2KB memcpy 仅 ~20µs（余量 ~1000×），无需在临界区内拷贝。
 * ------------------------------------------------------------------------- */

static adc_continuous_handle_t s_adc_handle = NULL;
static uint16_t s_buf[2][WAVE_SAMPLE_DEPTH];
static volatile bool s_running = false;

static portMUX_TYPE s_frame_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_active = 0;        /* ISR 当前写入块（0/1） */
static volatile int s_ready = -1;        /* 完成待领块；-1 = 无 */
static volatile int s_ready_depth = 0;   /* 待领帧的实际样本数 */
static volatile uint32_t s_dropped = 0;  /* 未消费即被新帧覆盖的帧数 */

/* ADC continuous callback: called when DMA buffer is filled（一个完整帧） */
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle,
                                        const adc_continuous_evt_data_t *edata,
                                        void *user_data)
{
    const uint8_t *p = edata->conv_frame_buffer;
    uint32_t len = edata->size;
    uint16_t *dst = s_buf[s_active];
    int n = 0;

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= len; i += SOC_ADC_DIGI_RESULT_BYTES) {
        adc_digi_output_data_t *item = (adc_digi_output_data_t *)&p[i];
        if (n < WAVE_SAMPLE_DEPTH) {
            dst[n++] = item->type2.data;
        }
    }

    /* 整帧入库：登记完成帧（覆盖未消费的旧帧 = 最新帧优先），再翻转写块 */
    portENTER_CRITICAL_ISR(&s_frame_mux);
    if (s_ready >= 0) {
        s_dropped++;
    }
    s_ready_depth = n;
    s_ready = s_active;
    s_active ^= 1;
    portEXIT_CRITICAL_ISR(&s_frame_mux);

    return false;  /* No need to yield */
}

esp_err_t adc_sampler_init(void)
{
    if (s_adc_handle) return ESP_OK;  /* Already initialized */

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

    /* 复位帧状态（在注册回调/启动之前，避免旧帧或脏数据被领走） */
    portENTER_CRITICAL(&s_frame_mux);
    memset(s_buf, 0, sizeof(s_buf));
    s_active = 0;
    s_ready = -1;
    s_ready_depth = 0;
    s_dropped = 0;
    portEXIT_CRITICAL(&s_frame_mux);

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

    /* 停止后清信箱：防止 stop 之后仍能领到停止前的尾帧 */
    portENTER_CRITICAL(&s_frame_mux);
    s_ready = -1;
    portEXIT_CRITICAL(&s_frame_mux);

    ESP_LOGI(TAG, "ADC sampling stopped");
}

int adc_sampler_read_frame(uint16_t *dst, int *depth)
{
    int idx, n;

    /* 临界区只做"领取 + 清信箱"（O(1)）——ISR 在此期间不会被长时间阻塞 */
    portENTER_CRITICAL(&s_frame_mux);
    idx = s_ready;
    n = s_ready_depth;
    s_ready = -1;
    portEXIT_CRITICAL(&s_frame_mux);

    if (idx < 0) {
        return 0;  /* 尚无新帧 */
    }

    /* 临界区外拷贝：ISR 已翻转到另一块，本块在两个帧周期内不会被回写 */
    memcpy(dst, s_buf[idx], (size_t)n * sizeof(uint16_t));
    if (depth) *depth = n;
    return 1;
}

uint32_t adc_sampler_get_dropped_frames(void)
{
    return s_dropped;
}
