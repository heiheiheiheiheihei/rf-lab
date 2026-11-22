/*
 * link — 小型事件环 + JSON 转义。纯 C,主机可单测。
 *
 * 为什么要环形缓冲而不是每客户端一条队列:
 *   · 事件是广播语义(串口/HTTP/BLE 都该看到同一条 LEARN/TX);
 *   · 只有一个生产者(主循环),消费者各自保存 cursor;
 *   · 慢客户端最多丢掉旧事件,不会拖死主循环,也不会阻塞模块 UART。
 *
 * 只放"小事件"(≤ LINK_LINE_MAX)。state/codes 快照由 main 的带序号快照提供,
 * 不走这里 —— 否则 25Hz 长发心跳会把环刷满。
 */
#ifndef RFLAB_LINK_H
#define RFLAB_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINK_LINE_MAX 768
#define LINK_RING_SLOTS 12

void link_init(void);

/* 发布一行完整 JSON(不含换行)。超长/非法直接丢弃,返回 0。 */
int link_publish(const char *line);
int link_publishf(const char *fmt, ...);

/* 当前已发布的最后序号(0 = 还没有事件)。 */
uint32_t link_head(void);

/*
 * 读 cursor 之后的下一条。
 * 返回  1: 读到,out/seq 有效,cursor 前移
 * 返回  0: 暂无新事件
 * 返回 -1: 这条已被覆盖(客户端太慢),cursor 已跳到下一可读位置
 */
int link_read(uint32_t *cursor, char *out, size_t cap, uint32_t *seq);

/* JSON 字符串内容转义(不含两侧引号)。返回写入长度,保证 NUL 结尾。 */
size_t link_json_escape(const char *src, char *dst, size_t cap);

/*
 * 下行分帧:把一条消息(json,len)连同帧结束符 '\n' 按 chunk 字节切片。
 * offset 从 0 起,每次调用填一片到 out(最多 cap 字节)并返回本片长度;
 * 返回 0 表示整帧已发完。不变式:所有片拼起来恰好是 json + '\n',
 * 每片 ≤ chunk,'\n' 只出现在最后一个字节 —— BLE/TUI 客户端都按 '\n'
 * 切行再整行解析,终止符漏发或提前出现都会让两条消息拼成一行。
 * 纯函数,主机可单测(见 test/host/test_link.c)。
 */
size_t link_frame_fill(const char *json, size_t len, size_t chunk, size_t offset,
                       uint8_t *out, size_t cap);

/*
 * codes 快照信封(SSE/BLE 共用):
 *   {"t":"codes","seq":N,"items":<json>}
 * link_codes_header() 只写头,返回头长度;link_codes_wrap() 把 buf+json_off 的
 * JSON 前移,补上头和结尾 '}',返回总长度(不含 NUL)。两者都返回 0 表示失败。
 * 客户端按整行 JSON 解析,少一个 '}' 就会整块不显示 —— 2026-11-22 的
 * APP 码库空白就是这里漏了闭合括号,所以信封由纯函数生成并进主机测试。
 */
size_t link_codes_header(char *out, size_t cap, uint32_t seq);
size_t link_codes_wrap(char *buf, size_t cap, size_t json_off, size_t json_len, uint32_t seq);

#endif /* RFLAB_LINK_H */
