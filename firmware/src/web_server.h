/*
 * web_server — 内嵌单页 UI + REST/SSE 控制面。
 *
 * 页面是 gzip 后的单文件(CMake EMBED_FILES),没有文件系统;API 只做
 * "入队命令 / 读快照 / 广播事件",业务逻辑仍在 main.c。
 */
#ifndef RFLAB_WEB_SERVER_H
#define RFLAB_WEB_SERVER_H

#include "esp_err.h"

esp_err_t web_server_start(void);

#endif /* RFLAB_WEB_SERVER_H */
