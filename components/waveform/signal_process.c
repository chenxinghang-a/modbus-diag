#include "signal_process.h"
#include <string.h>

void signal_filter_sma(const uint16_t *in, uint16_t *out, int len, int window)
{
    if (!in || !out || len <= 0) return;
    if (window < 1) window = 1;
    int half = window / 2;
    for (int i = 0; i < len; i++) {
        uint32_t sum = 0;
        int cnt = 0;
        for (int j = i - half; j <= i + half; j++) {
            if (j >= 0 && j < len) {
                sum += in[j];
                cnt++;
            }
        }
        out[i] = sum / cnt;
    }
}

int signal_find_trigger(const uint16_t *data, int len, uint16_t threshold)
{
    if (!data || len <= 0) return -1;
    for (int i = 1; i < len; i++) {
        if (data[i - 1] < threshold && data[i] >= threshold) {
            return i;
        }
    }
    return -1;
}

uint16_t signal_measure_vpp(const uint16_t *data, int len)
{
    /* len<=0 时原来会返回 4095-0，一个凭空的峰峰值；null 还会直接崩 */
    if (!data || len <= 0) return 0;
    uint16_t min_val = 4095, max_val = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }
    return max_val - min_val;
}

uint16_t signal_measure_dc(const uint16_t *data, int len)
{
    /* len<=0 会除零 —— ESP32 上整数除零直接 panic，不是「返回 0」 */
    if (!data || len <= 0) return 0;
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum / len;
}

/* 总线空闲电压：取样本直方图的众数。
 * 为什么不用均值/中位数：RS-485 空闲时电平停在高位，而一帧数据里 0/1 大致各半，
 * 均值会被"数据段"拉向中点；众数落在出现次数最多的电平上，正好是空闲电平。
 * 直方图 256 桶（12bit 量化到 8bit），够用且不占栈（512B static）。 */
uint16_t signal_measure_idle(const uint16_t *data, int len)
{
    if (!data || len <= 0) return 0;

    static uint16_t hist[256];
    memset(hist, 0, sizeof(hist));

    for (int i = 0; i < len; i++) {
        uint16_t bin = data[i] >> 4;        /* 0..4095 → 0..255 */
        if (hist[bin] < 0xFFFF) hist[bin]++;
    }

    uint16_t best_bin = 0, best_cnt = 0;
    for (int b = 0; b < 256; b++) {
        if (hist[b] > best_cnt) {
            best_cnt = hist[b];
            best_bin = (uint16_t)b;
        }
    }
    return (uint16_t)((best_bin << 4) | 0x8);   /* 取桶中心 */
}

/* 上升时间：首个上升沿的 10%→90% 穿越所用采样点数，换算成微秒。
 * 返回 -1 表示没找到完整边沿（波形平坦或幅度太小）。
 * 采样率由调用方传入（固件里是 WAVE_SAMPLE_RATE=100kSPS，1 点 = 10us）。 */
int signal_measure_rise_time_us(const uint16_t *data, int len, uint32_t sample_rate_hz)
{
    if (!data || len < 4 || sample_rate_hz == 0) return -1;

    uint16_t lo = data[0], hi = data[0];
    for (int i = 1; i < len; i++) {
        if (data[i] < lo) lo = data[i];
        if (data[i] > hi) hi = data[i];
    }
    if (hi - lo < 100) return -1;           /* 幅度不足 100 LSB（约 80mV），测了也没意义 */

    uint16_t lvl10 = (uint16_t)(lo + (hi - lo) / 10);
    uint16_t lvl90 = (uint16_t)(lo + (hi - lo) * 9 / 10);

    int i10 = -1, i90 = -1;
    for (int i = 1; i < len; i++) {
        if (i10 < 0 && data[i - 1] < lvl10 && data[i] >= lvl10) i10 = i;
        if (i10 >= 0 && data[i - 1] < lvl90 && data[i] >= lvl90) { i90 = i; break; }
    }
    if (i10 < 0 || i90 < 0) return -1;

    uint32_t points = (uint32_t)(i90 - i10);
    return (int)((points * 1000000u) / sample_rate_hz);
}
