/* 协议层主机侧单元测试（不参与固件构建，用 gcc 直接跑）
 *
 * 目的：报文解析（frame_parser）和 RTU 帧构建/校验（modbus_rtu）都是纯 C、
 * 不依赖 ESP-IDF，可以在 PC 上直接跑回归 —— 不用板子也能证明协议层没坏。
 *
 * 跑法：
 *   bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "modbus_rtu.h"
#include "frame_parser.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) {
        ok++;
        printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : "");
    } else {
        bad++;
        printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : "");
    }
}

/* 参考实现：CRC16/Modbus（多项式 0xA001，初值 0xFFFF）。
 * 故意写成逐位算法，与固件里的查表法互为验证 —— 表写错了这里立刻露馅。 */
static uint16_t ref_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
            else         crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static uint16_t append_crc(uint8_t *buf, uint16_t len)
{
    uint16_t c = ref_crc16(buf, len);
    buf[len++] = (uint8_t)(c & 0xFF);        /* 低字节先发 */
    buf[len++] = (uint8_t)(c >> 8);
    return len;
}

int main(void)
{
    /* ---------- 1. CRC 实现 vs 参考实现 ---------- */
    printf("-- CRC16 --\n");
    {
        /* 经典向量：01 03 00 00 00 0A 的 CRC 寄存器值 = 0xCDC5，
         * 线上低字节先发，所以帧尾两字节是 C5 CD。 */
        const uint8_t vec[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
        uint16_t c = modbus_crc16(vec, sizeof(vec));
        char d[64];
        snprintf(d, sizeof(d), "crc=0x%04X (wire: %02X %02X)", c, c & 0xFF, c >> 8);
        check("经典向量 01 03 00 00 00 0A", c == 0xCDC5, d);
        check("查表法与逐位法一致", c == ref_crc16(vec, sizeof(vec)), NULL);
    }
    {
        /* 伪随机 + 各种长度：只看两种实现是否一致 */
        uint32_t seed = 12345;
        int mismatch = 0;
        for (int n = 1; n <= 64; n++) {
            uint8_t buf[64];
            for (int i = 0; i < n; i++) {
                seed = seed * 1103515245u + 12345u;
                buf[i] = (uint8_t)(seed >> 16);
            }
            if (modbus_crc16(buf, (uint16_t)n) != ref_crc16(buf, (uint16_t)n)) mismatch++;
        }
        char d[48];
        snprintf(d, sizeof(d), "1~64 字节共 64 组，不一致 %d 组", mismatch);
        check("随机数据 查表法 == 逐位法", mismatch == 0, d);
    }

    /* ---------- 2. 构建请求帧 ---------- */
    printf("-- 构建请求帧 --\n");
    {
        modbus_request_t req = {
            .slave_addr = 1,
            .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 0,
            .quantity = 10,
        };
        uint8_t buf[16];
        int n = modbus_rtu_build_request(&req, buf, sizeof(buf));
        char d[64];
        snprintf(d, sizeof(d), "len=%d", n);
        check("FC03 请求帧长度 = 8", n == 8, d);
        const uint8_t want[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
        check("前 6 字节与规范一致", memcmp(buf, want, 6) == 0, NULL);
        check("帧尾 CRC 正确", modbus_rtu_check_crc(buf, (uint16_t)n), NULL);
    }
    {
        modbus_request_t req = {
            .slave_addr = 0,               /* 广播地址 */
            .function_code = MODBUS_FC_WRITE_SINGLE_REG,
            .start_addr = 0x1234,
            .write_value = 0x5678,
        };
        uint8_t buf[16];
        int n = modbus_rtu_build_request(&req, buf, sizeof(buf));
        check("广播帧（addr=0）也能构建", n == 8 && buf[0] == 0x00, NULL);
        check("广播帧 CRC 正确", modbus_rtu_check_crc(buf, (uint16_t)n), NULL);
    }
    {
        uint8_t tiny[2];
        modbus_request_t req = { .slave_addr = 1, .function_code = MODBUS_FC_READ_HOLD_REGS };
        check("缓冲区不够时返回负值",
              modbus_rtu_build_request(&req, tiny, sizeof(tiny)) < 0, NULL);
    }

    /* ---------- FC16 批量写帧（FR1 要求 0x10，界面「Fill 4 regs」走这条路） ---------- */
    printf("-- FC16 批量写 --\n");
    {
        uint16_t vals[4] = {0x0001, 0x0002, 0x0003, 0x0004};
        modbus_request_t req = {
            .slave_addr = 1,
            .function_code = MODBUS_FC_WRITE_MULTI_REGS,
            .start_addr = 100,
            .quantity = 4,
            .write_values = vals,
        };
        uint8_t buf[32];
        int n = modbus_rtu_build_request(&req, buf, sizeof(buf));
        char d[64];
        snprintf(d, sizeof(d), "len=%d", n);
        check("帧长 = 1+1+2+2+1+8+2 = 17", n == 17, d);

        const uint8_t want[] = {0x01, 0x10, 0x00, 0x64, 0x00, 0x04, 0x08,
                                0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
        check("前 15 字节：FC/地址/数量/字节数/4 个值", memcmp(buf, want, sizeof(want)) == 0, NULL);
        check("字节数 = 数量 × 2", buf[6] == 8, NULL);
        check("帧尾 CRC 正确", modbus_rtu_check_crc(buf, (uint16_t)n), NULL);
    }
    {
        /* 没有 write_values 必须直接拒绝，而不是构造出半截帧 */
        modbus_request_t req = {
            .slave_addr = 1, .function_code = MODBUS_FC_WRITE_MULTI_REGS,
            .start_addr = 0, .quantity = 2, .write_values = NULL,
        };
        uint8_t buf[32];
        check("缺 write_values 时返回 -1",
              modbus_rtu_build_request(&req, buf, sizeof(buf)) < 0, NULL);
    }
    {
        /* FC15 批量写线圈：10 个线圈 → 2 字节位图 */
        uint8_t coils[2] = {0xA5, 0x02};
        modbus_request_t req = {
            .slave_addr = 3, .function_code = MODBUS_FC_WRITE_MULTI_COILS,
            .start_addr = 20, .quantity = 10, .write_coils = coils,
        };
        uint8_t buf[32];
        int n = modbus_rtu_build_request(&req, buf, sizeof(buf));
        check("FC15 帧长 = 1+1+2+2+1+2+2 = 11", n == 11, NULL);
        check("线圈数 10 → 字节数 2", buf[6] == 2, NULL);
        check("FC15 帧 CRC 正确", modbus_rtu_check_crc(buf, (uint16_t)n), NULL);
    }

    /* ---------- 3. 解析响应帧 ---------- */
    printf("-- 解析响应帧 --\n");
    {
        /* FC03 响应：01 03 04 00 01 00 02 + CRC（总长 9 = 5 + 字节数 4） */
        uint8_t f[16] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02};
        uint16_t n = append_crc(f, 7);
        parsed_frame_t p;
        check("解析成功", frame_parser_parse(f, n, &p), NULL);
        check("从站地址 = 1", p.slave_addr == 1, NULL);
        check("功能码 = 0x03", p.function_code == 0x03, NULL);
        check("CRC 有效", p.crc_valid, NULL);
        check("不是异常帧", !p.is_exception, NULL);
        check("按帧长识别为响应帧", p.is_response, NULL);
        check("响应帧给出字节数（=4）而不是假的 addr/qty", p.quantity_or_value == 4, NULL);

        char buf[64];
        frame_parser_to_string(&p, buf, sizeof(buf));
        check("字符串显示为 resp N bytes", strstr(buf, "resp 4 bytes") != NULL, buf);
    }
    {
        /* FC03 请求：01 03 00 00 00 0A + CRC（总长 8） */
        uint8_t f[16] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
        uint16_t n = append_crc(f, 6);
        parsed_frame_t p;
        frame_parser_parse(f, n, &p);
        check("请求帧不被误判成响应", !p.is_response, NULL);
        check("请求帧 addr = 0", p.start_addr == 0, NULL);
        check("请求帧 qty = 10", p.quantity_or_value == 10, NULL);
    }
    {
        /* 异常帧：01 83 02 + CRC（FC 最高位置 1） */
        uint8_t f[8] = {0x01, 0x83, 0x02};
        uint16_t n = append_crc(f, 3);
        parsed_frame_t p;
        check("异常帧解析成功", frame_parser_parse(f, n, &p), NULL);
        check("识别为异常帧", p.is_exception, NULL);
        check("异常码 = 2（非法数据地址）", p.exception_code == 2, NULL);

        char buf[64];
        frame_parser_to_string(&p, buf, sizeof(buf));
        check("转字符串含 EXC 字样", strstr(buf, "EXC") != NULL, buf);
        check("异常帧仍能看出是哪个功能码（不是 Unknown）",
              strstr(buf, "RdHoldReg") != NULL, NULL);
        check("转字符串含 Illegal Addr", strstr(buf, "Illegal Addr") != NULL, NULL);
    }
    {
        /* CRC 被改坏 */
        uint8_t f[16] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02};
        uint16_t n = append_crc(f, 7);
        f[n - 1] ^= 0xFF;
        parsed_frame_t p;
        frame_parser_parse(f, n, &p);
        check("CRC 损坏能识别", !p.crc_valid, NULL);
    }
    {
        /* 边界：小于 4 字节直接拒绝 */
        uint8_t f[3] = {0x01, 0x03, 0x02};
        parsed_frame_t p;
        check("3 字节帧被拒绝", !frame_parser_parse(f, 3, &p), NULL);
        check("空指针被拒绝", !frame_parser_parse(NULL, 8, &p), NULL);
    }

    /* ---------- 4. T3.5 静默时间 ---------- */
    printf("-- T3.5 --\n");
    {
        char d[48];
        uint32_t t9600 = modbus_rtu_t35_us(9600);
        uint32_t t115200 = modbus_rtu_t35_us(115200);
        snprintf(d, sizeof(d), "9600=%luus 115200=%luus",
                 (unsigned long)t9600, (unsigned long)t115200);
        check("波特率越高静默时间越短", t9600 > t115200, d);
        check("115200 下至少 1750us（规范下限）", t115200 >= 1750, NULL);
    }

    /* ---------- 十六进制视图（抓包页长按 OK 切换） ---------- */
    printf("-- 原始字节转十六进制 --\n");
    {
        const uint8_t f[] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02};
        char buf[64];
        int n = frame_parser_to_hex(f, sizeof(f), buf, sizeof(buf), 0);
        check("完整输出", strcmp(buf, "01 03 04 00 01 00 02") == 0, buf);
        check("返回长度正确", n == (int)strlen(buf), NULL);

        frame_parser_to_hex(f, sizeof(f), buf, sizeof(buf), 3);
        check("max_bytes=3 时截断并加 ..", strcmp(buf, "01 03 04 ..") == 0, buf);

        frame_parser_to_hex(NULL, 0, buf, sizeof(buf), 0);
        check("空帧显示 (empty)", strcmp(buf, "(empty)") == 0, buf);
    }
    {
        const uint8_t f[] = {0xAA, 0xBB, 0xCC, 0xDD};
        char small[7];                       /* 只放得下 "AA BB" (5) + NUL */
        int n = frame_parser_to_hex(f, sizeof(f), small, sizeof(small), 0);
        check("缓冲区不足时返回 -1（不写半截）", n == -1, NULL);

        char okbuf[12];
        check("刚好放得下 3 字节时成功",
              frame_parser_to_hex(f, 3, okbuf, sizeof(okbuf), 0) > 0, okbuf);
    }

    /* ---------- FC08 诊断帧（论文 5.2.4 声称支持，必须有入口与覆盖） ---------- */
    printf("-- FC08 诊断帧 --\n");
    {
        modbus_request_t req = {
            .slave_addr = 1,
            .function_code = MODBUS_FC_DIAGNOSTICS,
            .start_addr = 0x0000,      /* 子功能 0 = Return Query Data */
            .write_value = 0xA55A,
        };
        uint8_t buf[32];
        int n = modbus_rtu_build_request(&req, buf, sizeof(buf));
        check("FC08 帧长 = 1+1+2+2+2 = 8", n == 8, NULL);
        check("功能码为 0x08", buf[1] == 0x08, NULL);
        check("子功能号占地址字段", buf[2] == 0x00 && buf[3] == 0x00, NULL);
        check("数据跟在后面", buf[4] == 0xA5 && buf[5] == 0x5A, NULL);
        check("FC08 帧 CRC 正确", modbus_rtu_check_crc(buf, (uint16_t)n), NULL);

        /* 解析从站的环回复原帧：01 08 00 00 A5 5A + CRC */
        uint8_t echo[8] = {0x01, 0x08, 0x00, 0x00, 0xA5, 0x5A, 0, 0};
        uint16_t c = modbus_crc16(echo, 6);
        echo[6] = (uint8_t)(c & 0xFF);
        echo[7] = (uint8_t)(c >> 8);
        modbus_response_t r;
        memset(&r, 0, sizeof(r));
        check("环回复原可解析",
              modbus_rtu_parse_response(echo, sizeof(echo), &r) == MODBUS_OK, NULL);
        check("data_len 为 4（子功能 + 数据）", r.data_len == 4, NULL);
        uint16_t echoed = (uint16_t)((r.data[2] << 8) | r.data[3]);
        check("取到回显数据 0xA55A（不是子功能号）", echoed == 0xA55A, NULL);
    }

    /* ---------- 错误码名称完整性（枚举加了新值却忘加名字，这里会红） ---------- */
    printf("-- 错误码名称 --\n");
    {
        const modbus_err_t all[] = {
            MODBUS_OK, MODBUS_ERR_TIMEOUT, MODBUS_ERR_CRC, MODBUS_ERR_EXCEPTION,
            MODBUS_ERR_INVALID_RESPONSE, MODBUS_ERR_BUFFER_TOO_SMALL,
            MODBUS_ERR_IO, MODBUS_ERR_INVALID_PARAM,
        };
        bool all_named = true;
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
            const char *n = modbus_err_name(all[i]);
            if (!n || strcmp(n, "Unknown") == 0) all_named = false;
        }
        check("8 个错误码都有可读名称", all_named, NULL);
        check("超时名可直接显示给用户",
              strstr(modbus_err_name(MODBUS_ERR_TIMEOUT), "Timeout") != NULL,
              modbus_err_name(MODBUS_ERR_TIMEOUT));
        check("未定义取值返回 Unknown 而不是崩",
              strcmp(modbus_err_name((modbus_err_t)99), "Unknown") == 0, NULL);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}
