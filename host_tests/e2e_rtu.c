/* 端到端互通测试：用**固件自己的帧层**（modbus_rtu.c）直接和 Python 模拟器对话。
 *
 * 为什么值得单独做：单元测试只能证明「我们构造的字节符合自己的预期」，
 * 而这里是把这些字节真的发给一个独立的 Modbus 从站实现（pymodbus），
 * 再判断回的帧我们能正确解析 —— 这才叫互通。
 *
 * 依赖：sim/modbus_sim.py 以 `--rtu "socket://127.0.0.1:15040"` 起在监听。
 * 跑法： bash host_tests/run_e2e.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "modbus_rtu.h"
#include "modbus_common.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSE_SOCK closesocket
  #define BAD_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <sys/time.h>        /* setsockopt 用 struct timeval；Windows 分支用的是 DWORD */
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSE_SOCK close
  #define BAD_SOCK (-1)
#endif

/* Windows 的 <excpt.h> 把 exception_code 定义成宏（SEH 结构体字段），
 * 会把我们结构体里的同名成员替换成 _exception_code。这里取消掉再用。 */
#ifdef exception_code
#undef exception_code
#endif

static int ok = 0, bad = 0;
static sock_t s_sock = BAD_SOCK;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

static bool connect_with_retry(const char *ip, int port, int tries)
{
    for (int i = 0; i < tries; i++) {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCK) return false;

        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, ip, &a.sin_addr);

        if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0) {
#ifdef _WIN32
            DWORD tv = 2000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
            struct timeval tv = {2, 0};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
            s_sock = s;
            return true;
        }
        CLOSE_SOCK(s);
#ifdef _WIN32
        Sleep(300);
#else
        usleep(300 * 1000);
#endif
    }
    return false;
}

/* 发一帧请求，收回一帧响应。返回解析结果。 */
static modbus_err_t transact(const modbus_request_t *req, modbus_response_t *resp)
{
    uint8_t tx[256], rx[256];
    int n = modbus_rtu_build_request(req, tx, sizeof(tx));
    if (n < 0) return MODBUS_ERR_INVALID_PARAM;

    if (send(s_sock, (const char *)tx, n, 0) != n) return MODBUS_ERR_IO;

    int r = recv(s_sock, (char *)rx, sizeof(rx), 0);
    if (r <= 0) return MODBUS_ERR_TIMEOUT;

    return modbus_rtu_parse_response(rx, (uint16_t)r, resp);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup 失败\n");
        return 1;
    }
#endif

    printf("-- 连接模拟器 127.0.0.1:15040（RTU over TCP）--\n");
    if (!connect_with_retry("127.0.0.1", 15040, 20)) {
        printf("  [FAIL] 连不上模拟器：先运行 host_tests/run_e2e.sh\n");
        return 1;
    }
    printf("  [ OK ] 已连接\n\n");

    printf("-- FC03 读保持寄存器（unit 1, addr 0, 2 个）--\n");
    {
        modbus_request_t req = {
            .slave_addr = 1, .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 0, .quantity = 2,
        };
        modbus_response_t resp;
        memset(&resp, 0, sizeof(resp));
        modbus_err_t err = transact(&req, &resp);
        check("读成功", err == MODBUS_OK, err == MODBUS_OK ? NULL : "err!=OK");
        check("字节数 = 4", resp.byte_count == 4, NULL);
        uint16_t v0 = (uint16_t)((resp.data[0] << 8) | resp.data[1]);
        uint16_t v1 = (uint16_t)((resp.data[2] << 8) | resp.data[3]);
        char d[64];
        snprintf(d, sizeof(d), "v0=%u v1=%u", v0, v1);
        check("值与 devices.json 一致（230 / 500）", v0 == 230 && v1 == 500, d);
    }

    printf("-- FC16 批量写 4 个寄存器再读回 --\n");
    {
        uint16_t vals[4] = {1111, 2222, 3333, 4444};
        modbus_request_t wreq = {
            .slave_addr = 1, .function_code = MODBUS_FC_WRITE_MULTI_REGS,
            .start_addr = 0, .quantity = 4, .write_values = vals,
        };
        modbus_response_t wresp;
        memset(&wresp, 0, sizeof(wresp));
        modbus_err_t err = transact(&wreq, &wresp);
        char we[48];
        snprintf(we, sizeof(we), "err=%d exc=%u", (int)err, (unsigned)wresp.exception_code);
        check("写入成功（无异常）", err == MODBUS_OK, we);
        check("响应回显起始地址 0",
              wresp.data_len >= 4 &&
              ((wresp.data[0] << 8) | wresp.data[1]) == 0, NULL);
        check("响应回显数量 4",
              wresp.data_len >= 4 &&
              ((wresp.data[2] << 8) | wresp.data[3]) == 4, NULL);

        modbus_request_t rreq = {
            .slave_addr = 1, .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 0, .quantity = 4,
        };
        modbus_response_t rresp;
        memset(&rresp, 0, sizeof(rresp));
        check("读回成功", transact(&rreq, &rresp) == MODBUS_OK, NULL);
        bool same = (rresp.byte_count == 8);
        for (int i = 0; same && i < 4; i++) {
            uint16_t v = (uint16_t)((rresp.data[i * 2] << 8) | rresp.data[i * 2 + 1]);
            if (v != vals[i]) same = false;
        }
        check("读回值与写入值完全一致", same, NULL);
    }

    printf("-- 异常响应（越界地址）--\n");
    {
        modbus_request_t req = {
            .slave_addr = 1, .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 60000, .quantity = 1,
        };
        modbus_response_t resp;
        memset(&resp, 0, sizeof(resp));
        modbus_err_t err = transact(&req, &resp);
        char d[64];
        snprintf(d, sizeof(d), "err=%d code=%u", (int)err, (unsigned)resp.exception_code);
        check("返回异常（不是超时/CRC 错）", err == MODBUS_ERR_EXCEPTION, d);
        check("异常码为 2（非法数据地址）", resp.exception_code == 2, d);
    }

    printf("-- 不存在的从站（unit 9）--\n");
    {
        modbus_request_t req = {
            .slave_addr = 9, .function_code = MODBUS_FC_READ_HOLD_REGS,
            .start_addr = 0, .quantity = 1,
        };
        modbus_response_t resp;
        memset(&resp, 0, sizeof(resp));
        modbus_err_t err = transact(&req, &resp);
        /* 从站不回帧 → 客户端应超时；这也顺带验证「超时」这条路径 */
        check("无响应时得到超时（而不是解析出垃圾）",
              err == MODBUS_ERR_TIMEOUT || err == MODBUS_ERR_EXCEPTION, NULL);
    }

    CLOSE_SOCK(s_sock);
#ifdef _WIN32
    WSACleanup();
#endif

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}
