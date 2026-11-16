/*
 * net — Wi-Fi 状态机:STA 优先接局域网;失败/掉线自动回 AP。
 *
 * 模式:
 *   ap          仅 AP(无凭据,或用户 WIFI CLEAR)
 *   connecting  仅 STA,启动时尝试连接(AP 关闭)
 *   sta         STA 已连,AP 关闭 —— 局域网访问 Web
 *   apsta       AP + STA(连接失败后的回退/在 AP 里重新配网);AP 始终可用
 *
 * 失败路径:connecting 15s 无 IP → apsta;STA 连接中断 → apsta;
 * AP 里重新配网 → apsta,连接成功后保持 AP 以免踢掉正在配网的浏览器。
 */
#ifndef RFLAB_NET_H
#define RFLAB_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define RFLAB_AP_PASS "rflab1234"
#define RFLAB_AP_PREFIX "RF-Lab-"

typedef struct {
    char mode[12];       /* "ap" / "connecting" / "sta" / "apsta" */
    bool ap_on;
    bool has_creds;
    char ap_ssid[33];
    char ap_ip[16];
    int ap_clients;
    bool sta_connected;
    char sta_ssid[33];
    char sta_ip[16];
    int rssi;
} net_info_t;

esp_err_t net_init(void);

/* 保存 STA 凭据并尝试连接;AP 保持可用(apsta)。 */
esp_err_t net_set_sta(const char *ssid, const char *pass);

/* 删除 STA 凭据,回到 AP-only。 */
esp_err_t net_clear_sta(void);

void net_get_info(net_info_t *out);
const char *net_ap_ssid(void);
/* STA 已连返回 STA IP;否则返回 AP IP;两者都没有返回 0.0.0.0。 */
const char *net_ip(void);

#endif /* RFLAB_NET_H */
