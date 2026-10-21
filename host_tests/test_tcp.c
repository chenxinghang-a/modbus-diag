/* Modbus TCP（FR2）的 MBAP 组帧与解帧测试。
 *
 * 只测纯逻辑：帧格式、事务号校验、异常帧、长度字段越界。
 * socket 收发由 host_tests/stubs/lwip/sockets.h 里的桩函数挡住，不会真的联网。
 *
 * 跑法： bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "modbus_tcp.h"
#include "modbus_common.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

int main(void)
{
    printf("-- MBAP 组帧 --\n");
    {
        modbus_request_t req = {
            .slave_addr = 1,
            .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 0,
            .quantity = 10,
        };
        uint8_t f[32];
        int n = modbus_tcp_build_frame(0x1234, &req, f, sizeof(f));

        char d[64];
        snprintf(d, sizeof(d), "len=%d", n);
        check("帧长 = MBAP(7) + PDU(5) = 12", n == 12, d);
        check("事务号按大端写入", f[0] == 0x12 && f[1] == 0x34, NULL);
        check("协议号固定 0", f[2] == 0 && f[3] == 0, NULL);
        check("长度字段 = 后续字节数 6", f[4] == 0 && f[5] == 6, NULL);
        check("单元号、功能码正确", f[6] == 1 && f[7] == 0x03, NULL);
        check("起始地址与数量按大端写入",
              f[8] == 0 && f[9] == 0 && f[10] == 0 && f[11] == 10, NULL);
    }

    printf("-- 解帧 --\n");
    {
        /* FC03 响应：01 03 04 00 01 00 02，MBAP 长度 = 1(unit)+1(fc)+1(bc)+4 = 7 */
        uint8_t f[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x07, 0x01, 0x03, 0x04,
                       0x00, 0x01, 0x00, 0x02};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        modbus_err_t err = modbus_tcp_parse_frame(f, sizeof(f), &r, 0x1234);
        check("事务号匹配 → 解析成功", err == MODBUS_OK, NULL);
        check("从站地址 = 1", r.slave_addr == 1, NULL);
        check("功能码 = 0x03", r.function_code == 0x03, NULL);
        check("字节数 = 4", r.byte_count == 4, NULL);
    }
    {
        /* 同样的帧，但期望事务号不同：必须拒绝 —— 否则上一次超时请求的迟到响应
         * 会被当成本次结果，界面显示「另一个请求的数据」 */
        uint8_t f[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x07, 0x01, 0x03, 0x04,
                       0x00, 0x01, 0x00, 0x02};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        check("事务号不匹配 → 判无效",
              modbus_tcp_parse_frame(f, sizeof(f), &r, 0x5678) == MODBUS_ERR_INVALID_RESPONSE, NULL);
    }
    {
        /* 协议号非 0：不是 Modbus TCP，直接拒 */
        uint8_t f[] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x07, 0x01, 0x03, 0x04,
                       0x00, 0x01, 0x00, 0x02};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        check("协议号非 0 → 判无效",
              modbus_tcp_parse_frame(f, sizeof(f), &r, 0x0001) == MODBUS_ERR_INVALID_RESPONSE, NULL);
    }
    {
        /* 长度字段声称 200 字节，实际只有 13：必须拒绝，不能越界读 */
        uint8_t f[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0xC8, 0x01, 0x03, 0x04,
                       0x00, 0x01, 0x00, 0x02};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        check("长度字段与实际帧长不符 → 判无效",
              modbus_tcp_parse_frame(f, sizeof(f), &r, 0x0001) == MODBUS_ERR_INVALID_RESPONSE, NULL);
    }
    {
        /* 异常响应：01 83 02（非法数据地址） */
        uint8_t f[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x01, 0x83, 0x02};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        modbus_err_t err = modbus_tcp_parse_frame(f, sizeof(f), &r, 0x0002);
        check("异常帧返回 MODBUS_ERR_EXCEPTION", err == MODBUS_ERR_EXCEPTION, NULL);
        check("异常码 = 2", r.exception_code == 2, NULL);
    }
    {
        uint8_t f[4] = {0, 0, 0, 0};
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        check("帧长不足（< MBAP+1）→ 判无效",
              modbus_tcp_parse_frame(f, 4, &r, 0) == MODBUS_ERR_INVALID_RESPONSE, NULL);
        check("空指针 → 判无效",
              modbus_tcp_parse_frame(NULL, 13, &r, 0) == MODBUS_ERR_INVALID_RESPONSE, NULL);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}
