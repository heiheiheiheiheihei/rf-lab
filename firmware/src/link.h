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

#endif /* RFLAB_LINK_H */
