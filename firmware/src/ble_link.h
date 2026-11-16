/*
 * ble_link — NimBLE 外设:自定义 NUS 风格 GATT,行协议,应用层 4 位 PIN。
 *
 * 只做传输:收到一行命令就 app_submit_cmd();状态/事件从 link 环与 app 快照取。
 * 规格(服务 UUID、分片、鉴权)见 docs/01-ui.md §4。
 */
#ifndef RFLAB_BLE_LINK_H
#define RFLAB_BLE_LINK_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ble_link_start(const char *name, const char *pin);

bool ble_link_is_on(void);
bool ble_link_connected(void);
bool ble_link_advertising(void);
void ble_link_set_enabled(bool on);

#endif /* RFLAB_BLE_LINK_H */
