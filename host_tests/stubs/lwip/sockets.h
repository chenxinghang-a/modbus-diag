#pragma once
/* 主机侧测试用的 lwip/sockets.h 桩。
 *
 * modbus_tcp.c 里真正需要 socket 的只有 connect/send/recv 那几个函数，
 * 而 MBAP 组帧与解帧（build_frame / parse_frame）是纯逻辑 —— 为了能把这两个
 * 函数搬到 PC 上测，这里只需要让编译器看到 socket API 的声明。
 * 所有桩函数都返回 -1，测试不会真的收发。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>          /* struct timeval 由系统头提供，别重复定义 */

typedef int socklen_t;

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char     sin_zero[8];
};

#define AF_INET         2
#define SOCK_STREAM     1
#define IPPROTO_TCP     6
#define SOL_SOCKET      1
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21

static inline int socket(int d, int t, int p) { (void)d; (void)t; (void)p; return -1; }
static inline int connect(int s, const struct sockaddr *a, socklen_t l)
{ (void)s; (void)a; (void)l; return -1; }
static inline int send(int s, const void *b, size_t n, int f)
{ (void)s; (void)b; (void)n; (void)f; return -1; }
static inline int recv(int s, void *b, size_t n, int f)
{ (void)s; (void)b; (void)n; (void)f; return -1; }
static inline int close(int s) { (void)s; return 0; }
static inline int setsockopt(int s, int lvl, int opt, const void *v, socklen_t l)
{ (void)s; (void)lvl; (void)opt; (void)v; (void)l; return 0; }
static inline int inet_pton(int af, const char *src, void *dst)
{ (void)af; (void)src; (void)dst; return 1; }

static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
#define ntohs(v) htons(v)
