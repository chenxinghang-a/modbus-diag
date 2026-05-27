#pragma once

#include "scan_result.h"
#include "esp_err.h"

/* Classify registers for a given device.
 * Scans address space and determines register types.
 * Results stored in dev->reg_groups.
 */
esp_err_t register_classify(uint8_t slave_addr, device_info_t *dev);

/* Probe a specific register address for its type */
esp_err_t register_probe_type(uint8_t slave_addr, uint16_t reg_addr, reg_type_t *type);
