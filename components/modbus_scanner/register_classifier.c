#include "register_classifier.h"
#include "modbus_master.h"
#include "modbus_common.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "reg_class";

esp_err_t register_probe_type(uint8_t slave_addr, uint16_t reg_addr, reg_type_t *type)
{
    uint16_t values[1];
    uint16_t count = 0;
    modbus_err_t err;

    /* Try holding register (FC03) */
    err = modbus_read_holding_regs(slave_addr, reg_addr, 1, values, &count);
    if (err == MODBUS_OK) {
        *type = REG_TYPE_HOLDING;
        return ESP_OK;
    }

    /* Try input register (FC04) */
    err = modbus_read_input_regs(slave_addr, reg_addr, 1, values, &count);
    if (err == MODBUS_OK) {
        *type = REG_TYPE_INPUT;
        return ESP_OK;
    }

    /* Try coil (FC01) */
    uint8_t coil_data[1];
    uint16_t coil_count = 0;
    err = modbus_read_coils(slave_addr, reg_addr, 1, coil_data, &coil_count);
    if (err == MODBUS_OK) {
        *type = REG_TYPE_COIL;
        return ESP_OK;
    }

    /* Try discrete input (FC02) */
    uint8_t di_data[1];
    uint16_t di_count = 0;
    err = modbus_read_discrete_inputs(slave_addr, reg_addr, 1, di_data, &di_count);
    if (err == MODBUS_OK) {
        *type = REG_TYPE_DISC_INPUT;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t register_classify(uint8_t slave_addr, device_info_t *dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Classifying registers for addr=%d", slave_addr);
    dev->reg_group_count = 0;

    /* Scan key register ranges (sampling strategy) */
    static const uint16_t sample_addrs[] = {
        0, 1, 2, 3, 4, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000
    };
    int sample_count = sizeof(sample_addrs) / sizeof(sample_addrs[0]);

    for (int i = 0; i < sample_count && dev->reg_group_count < SCANNER_MAX_REG_GROUPS; i++) {
        reg_type_t type;
        if (register_probe_type(slave_addr, sample_addrs[i], &type) == ESP_OK) {
            /* Check if this type already exists */
            bool found = false;
            for (int g = 0; g < dev->reg_group_count; g++) {
                if (dev->reg_groups[g].reg_type == type) {
                    /* Extend range */
                    if (sample_addrs[i] > dev->reg_groups[g].end_addr) {
                        dev->reg_groups[g].end_addr = sample_addrs[i];
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                reg_group_t *grp = &dev->reg_groups[dev->reg_group_count];
                grp->start_addr = sample_addrs[i];
                grp->end_addr = sample_addrs[i];
                grp->reg_type = type;
                /* Test writability for holding registers */
                grp->writable = false;
                if (type == REG_TYPE_HOLDING) {
                    uint16_t orig_val = 0;
                    modbus_read_holding_regs(slave_addr, sample_addrs[i], 1, &orig_val, NULL);
                    modbus_err_t werr = modbus_write_single_reg(slave_addr, sample_addrs[i], orig_val);
                    grp->writable = (werr == MODBUS_OK);
                }
                dev->reg_group_count++;
            }
        }
    }

    ESP_LOGI(TAG, "Found %d register groups", dev->reg_group_count);
    return ESP_OK;
}
