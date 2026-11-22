/*
 * codebook — 码库记录模型(纯 C,主机可测)。
 *
 * 0.7.1 起每条码在 band + 4 字节 payload 之外,还有:
 *   name   UTF-8,最多 24 字节(约 8 个汉字)
 *   group  单分组标签,UTF-8,最多 16 字节
 *   sym    符号/emoji,UTF-8,最多 8 字节
 *   flags  bit0 = 手工建码,bit1 = 建码后改过 band/code
 *
 * 存储格式与 NVS 解耦:main.c 只负责读写 key,这里负责 v1→v2 迁移、
 * 校验与 JSON。这样迁移/边界条件能在 host_test 里跑,不必只靠真机。
 */
#ifndef RFLAB_CODEBOOK_H
#define RFLAB_CODEBOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 128 条 × 57B = 7.3KB blob;NVS 分区固定 24KB(0x6000),
 * 留出 GC 与 Wi-Fi/BLE key 的余量。JSON 侧见 APP_CODES_JSON_MAX。 */
#define CODEBOOK_MAX 128
/* 协议字段(code[0..3]):地址2 / 地址1 / 按键 / 震荡参数。
 * 身份 = band + 前 3 字节;震荡参数不参与去重,只影响发射脉冲。 */
#define CODEBOOK_ID_LEN 3
#define CODEBOOK_OSC_MIN 0x30
#define CODEBOOK_OSC_MAX 0xF0
#define CODEBOOK_NAME_MAX 24
#define CODEBOOK_GROUP_MAX 16
#define CODEBOOK_SYM_MAX 8

#define CODEBOOK_FLAG_MANUAL 0x01
#define CODEBOOK_FLAG_EDITED 0x02

#define CODEBOOK_MAGIC0 'R'
#define CODEBOOK_MAGIC1 '3'
#define CODEBOOK_VERSION 2
#define CODEBOOK_V1_VERSION 1

typedef enum {
    CODEBOOK_OK = 0,
    CODEBOOK_ERR_ARG,
    CODEBOOK_ERR_FULL,
    CODEBOOK_ERR_DUP,
    CODEBOOK_ERR_RANGE,
    CODEBOOK_ERR_BAND,
    CODEBOOK_ERR_CODE,
    CODEBOOK_ERR_TEXT,
    CODEBOOK_ERR_UTF8,
    CODEBOOK_ERR_BLOB,
    CODEBOOK_ERR_VERSION,
} codebook_err_t;

typedef struct {
    uint8_t band;
    uint8_t flags;
    uint8_t code[4];
    char name[CODEBOOK_NAME_MAX + 1];
    char group[CODEBOOK_GROUP_MAX + 1];
    char sym[CODEBOOK_SYM_MAX + 1];
} codebook_rec_t;

typedef struct {
    codebook_rec_t recs[CODEBOOK_MAX];
    int count;
} codebook_t;

/* v2 blob = 4 字节头 + 32 条定长记录;所有成员都是 1 字节类型,无 padding。 */
typedef struct {
    uint8_t magic[2];
    uint8_t ver;
    uint8_t count;
    codebook_rec_t recs[CODEBOOK_MAX];
} codebook_blob_t;

#define CODEBOOK_BLOB_MAX (4 + CODEBOOK_MAX * sizeof(codebook_rec_t))

void codebook_init(codebook_t *cb);

const codebook_rec_t *codebook_get(const codebook_t *cb, int id);
codebook_rec_t *codebook_get_mut(codebook_t *cb, int id);
int codebook_find(const codebook_t *cb, uint8_t band, const uint8_t code[4]);

/* id 从 1 开始;成功时 out_id 可为 NULL。 */
codebook_err_t codebook_add(codebook_t *cb, uint8_t band, const uint8_t code[4],
                            const char *name, bool manual, int *out_id);
codebook_err_t codebook_set_name(codebook_t *cb, int id, const char *text);
codebook_err_t codebook_set_group(codebook_t *cb, int id, const char *text);
codebook_err_t codebook_set_sym(codebook_t *cb, int id, const char *text);
codebook_err_t codebook_set_band(codebook_t *cb, int id, uint8_t band);
codebook_err_t codebook_set_code(codebook_t *cb, int id, const uint8_t code[4]);
codebook_err_t codebook_erase(codebook_t *cb, int id);

/* 地址/按键非 0、震荡参数在 0x30..0xF0(模块手册范围),band 合法。 */
bool codebook_code_valid(uint8_t band, const uint8_t code[4]);
/* 两条记录是否同一个键:band + 前 3 字节相同(忽略震荡参数)。 */
bool codebook_same_identity(const codebook_rec_t *a, const codebook_rec_t *b);

/* 全量序列化/反序列化;成功返回 blob 字节数,失败返回 0。 */
size_t codebook_serialize(const codebook_t *cb, uint8_t *out, size_t cap);
codebook_err_t codebook_deserialize(codebook_t *cb, const uint8_t *blob, size_t len);
/* v1 blob 是 164B(4 字节头 + 32*5,尾部补零);只迁 band/code。 */
codebook_err_t codebook_migrate_v1(codebook_t *cb, const uint8_t *blob, size_t len);

/* 生成 /api/codes 的数组;cap 不足时写 "[]" 并返回 2。 */
size_t codebook_codes_json(const codebook_t *cb, char *out, size_t cap);

const char *codebook_band_name(uint8_t band);
const char *codebook_err_name(codebook_err_t err);
bool codebook_utf8_valid(const char *s);
bool codebook_text_fits(const char *s, size_t max_bytes);

#endif /* RFLAB_CODEBOOK_H */
