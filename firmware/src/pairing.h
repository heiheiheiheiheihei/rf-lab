/*
 * pairing.h — 长期密钥与配对状态(契约 §7.1(1))
 *
 * 本批(USB-only)的配对语义:
 *   - USB 是**可信链路**:连接行为本身即证明操作者在场,故 `pair_code` 不校验
 *     (契约 §7.1(1) 定稿,NQ-01)。
 *   - `pairing_secret` 只在 USB 上、且只在**首次配对**时下发一次。
 *   - 一台设备只绑定一个 `client_id`;换绑需**设备端物理按键**(GPIO38)。
 *
 * ⚠️ 未实现(阶段 4 才会用到,现在做会产生永不执行的死代码):
 *   - 发射授权令牌的 HMAC 校验(§7.1(2)(3))
 *   - `last_accepted_seq` 的持久化(本文件只保留内存值)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define PAIRING_SECRET_LEN 32
/* base64url(32B) = ceil(32*8/6) = 43 字符,无填充。 */
#define PAIRING_SECRET_B64_LEN 43
#define PAIRING_CLIENT_ID_MAX  64

esp_err_t pairing_init(void);

bool pairing_is_bonded(void);

/* 首次配对:生成 32B 随机密钥并绑定 client_id。已有绑定时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pairing_create(const char *client_id);

bool pairing_client_matches(const char *client_id);

/* 取得 secret 的 base64url 文本(仅 USB 首次配对时下发一次)。 */
esp_err_t pairing_secret_b64url(char out[PAIRING_SECRET_B64_LEN + 1]);

uint32_t pairing_last_seq(void);
