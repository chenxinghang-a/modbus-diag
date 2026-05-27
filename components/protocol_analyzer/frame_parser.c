#include "frame_parser.h"
#include "modbus_rtu.h"
#include "modbus_common.h"
#include <string.h>
#include <stdio.h>

bool frame_parser_parse(const uint8_t *data, uint16_t len, parsed_frame_t *out)
{
    if (!data || !out || len < 4) return false;
    memset(out, 0, sizeof(parsed_frame_t));

    out->crc_valid = modbus_rtu_check_crc(data, len);
    out->slave_addr = data[0];
    out->function_code = data[1];
    out->fc_name = modbus_fc_name(out->function_code);

    if (modbus_is_exception(out->function_code)) {
        out->is_exception = true;
        if (len >= 3) {
            out->exception_code = data[2];
        }
        return true;
    }

    switch (out->function_code) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISC_INPUTS:
        case MODBUS_FC_READ_HOLD_REGS:
        case MODBUS_FC_READ_INPUT_REGS:
            if (len >= 6) {
                out->start_addr = (data[2] << 8) | data[3];
                out->quantity_or_value = (data[4] << 8) | data[5];
            }
            break;

        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REG:
            if (len >= 6) {
                out->start_addr = (data[2] << 8) | data[3];
                out->quantity_or_value = (data[4] << 8) | data[5];
            }
            break;

        case MODBUS_FC_WRITE_MULTI_COILS:
        case MODBUS_FC_WRITE_MULTI_REGS:
            if (len >= 7) {
                out->start_addr = (data[2] << 8) | data[3];
                out->quantity_or_value = (data[4] << 8) | data[5];
            }
            break;

        default:
            break;
    }
    return true;
}

int frame_parser_to_string(const parsed_frame_t *frame, char *buf, size_t buf_size)
{
    if (!frame || !buf) return 0;

    if (frame->is_exception) {
        return snprintf(buf, buf_size, "%d: EXC[%s] %s",
                        frame->slave_addr, frame->fc_name,
                        modbus_exception_name(frame->exception_code));
    }

    switch (frame->function_code) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISC_INPUTS:
        case MODBUS_FC_READ_HOLD_REGS:
        case MODBUS_FC_READ_INPUT_REGS:
            return snprintf(buf, buf_size, "%d: %s addr=%d qty=%d%s",
                            frame->slave_addr, frame->fc_name,
                            frame->start_addr, frame->quantity_or_value,
                            frame->crc_valid ? "" : " [CRC!]");

        case MODBUS_FC_WRITE_SINGLE_COIL:
            return snprintf(buf, buf_size, "%d: WrCoil %d=%s",
                            frame->slave_addr, frame->start_addr,
                            frame->quantity_or_value ? "ON" : "OFF");

        case MODBUS_FC_WRITE_SINGLE_REG:
            return snprintf(buf, buf_size, "%d: WrReg %d=%d",
                            frame->slave_addr, frame->start_addr,
                            frame->quantity_or_value);

        default:
            return snprintf(buf, buf_size, "%d: %s [%d bytes]",
                            frame->slave_addr, frame->fc_name,
                            frame->quantity_or_value);
    }
}
