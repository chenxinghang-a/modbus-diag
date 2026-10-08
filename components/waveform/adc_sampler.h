#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t adc_sampler_init(void);
esp_err_t adc_sampler_start(void);
void adc_sampler_stop(void);

/**
 * 领取一帧完整采样（双缓冲 · 最新帧优先）。
 *
 * @param dst    目标缓冲（容量 >= WAVE_SAMPLE_DEPTH 个 uint16_t）
 * @param depth  输出：本帧实际采样点数
 * @return 1 = 拿到一帧；0 = 尚无新帧（调用方可跳过本次刷新、保持画面）
 *
 * 线程安全：ISR 写 / 任务读。帧信箱由 portMUX 临界区保护；
 * memcpy 在临界区外完成（帧间隔 ~10ms，2KB memcpy ~20µs，余量 ~1000×）。
 *
 * 改造背景（2026-10-08）：原 API `adc_sampler_get_buffer()` 返回"正在被 ISR
 * 写入"的裸指针 —— UI 读满 1024 点远超一帧时间，必然读到撕裂波形。该 API
 * 已移除，调用方统一改走本函数。
 */
int adc_sampler_read_frame(uint16_t *dst, int *depth);

/** 自上次 start 以来，因"最新帧优先"被覆盖丢弃的未消费帧数（诊断用）。 */
uint32_t adc_sampler_get_dropped_frames(void);
