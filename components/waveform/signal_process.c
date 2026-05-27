#include "signal_process.h"
#include <string.h>

void signal_filter_sma(const uint16_t *in, uint16_t *out, int len, int window)
{
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
    for (int i = 1; i < len; i++) {
        if (data[i - 1] < threshold && data[i] >= threshold) {
            return i;
        }
    }
    return -1;
}

uint16_t signal_measure_vpp(const uint16_t *data, int len)
{
    uint16_t min_val = 4095, max_val = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }
    return max_val - min_val;
}

uint16_t signal_measure_dc(const uint16_t *data, int len)
{
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum / len;
}
