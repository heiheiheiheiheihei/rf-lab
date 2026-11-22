/*
 * app — main.c 暴露给 HTTP/BLE 传输层的只读接口与命令入队。
 *
 * 传输层不直接碰码库/模块 UART:命令统一进 main 的队列,状态统一读快照。
 * 这样 0.5.0 已验证的命令语义只有一份实现。
 */
#ifndef RFLAB_APP_H
#define RFLAB_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_SRC_SERIAL = 0,
    APP_SRC_HTTP = 1,
    APP_SRC_BLE = 2,
    APP_SRC_INTERNAL = 3,
} app_src_t;

#define APP_CMD_MAX 96
/* 1280 was tight after adding the `ota` object; keep one JSON snapshot whole. */
#define APP_STATE_JSON_MAX 1536
#define APP_CODES_JSON_MAX 12288

/* 入队一行命令。成功 0,队列满 -1。线程安全。 */
int app_submit_cmd(const char *line, app_src_t src);

/* 状态/码库快照。线程安全,总是 NUL 结尾;cap 不足时截断为合法 JSON 前缀? 
 * 调用方应传 APP_*_MAX。截断只会在异常情况下发生。 */
void app_copy_state(char *out, size_t cap);
void app_copy_codes(char *out, size_t cap);
uint32_t app_state_seq(void);
uint32_t app_codes_seq(void);

void app_mark_state_dirty(void);
void app_mark_codes_dirty(void);

/* 网络事件 → OLED 开机页(线程安全;由 main 轮询落到 ui_model)。 */
void app_note_net_event(const char *line1, const char *line2);

/* 设备名 / BLE PIN / BLE 开关状态。 */
const char *app_name(void);
const char *app_ble_pin(void);
bool app_ble_enabled(void);
int64_t app_now_ms(void);

#endif /* RFLAB_APP_H */
