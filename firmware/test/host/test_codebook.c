/*
 * codebook.c 的主机单测:记录校验、v1→v2 迁移、JSON、UTF-8 边界。
 *
 * 迁移是 0.7.1 最大的数据风险;这里用合成 v1 blob 覆盖真实布局(164B),
 * 真机 9 条码的迁移仍要单独验收。
 */
#include <stdio.h>
#include <string.h>

#include "codebook.h"
#include "rf34dl.h"

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_fails++;                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static const uint8_t CODE_A[4] = {0x55, 0x55, 0x03, 0x3c};
static const uint8_t CODE_B[4] = {0x55, 0x55, 0x03, 0x4b};
static const uint8_t CODE_C[4] = {0xa1, 0xb2, 0xc3, 0xd4};

static void test_add_find(void)
{
    codebook_t cb;
    int id = 0;

    codebook_init(&cb);
    CHECK(cb.count == 0);
    CHECK(codebook_get(&cb, 1) == NULL);
    CHECK(codebook_find(&cb, RF34DL_BAND_315, CODE_A) == -1);

    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, "大门", false, &id) == CODEBOOK_OK);
    CHECK(id == 1);
    CHECK(cb.count == 1);
    CHECK(codebook_find(&cb, RF34DL_BAND_315, CODE_A) == 0);
    CHECK(codebook_find(&cb, RF34DL_BAND_433, CODE_A) == -1);
    CHECK(strcmp(codebook_get(&cb, 1)->name, "大门") == 0);

    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, NULL, false, NULL) == CODEBOOK_ERR_DUP);
    CHECK(codebook_add(&cb, RF34DL_BAND_433, CODE_B, NULL, true, &id) == CODEBOOK_OK);
    CHECK(id == 2);
    CHECK((codebook_get(&cb, 2)->flags & CODEBOOK_FLAG_MANUAL) != 0);
    CHECK(codebook_add(&cb, 0, CODE_C, NULL, false, NULL) == CODEBOOK_ERR_BAND);

    for (int i = 3; i <= CODEBOOK_MAX; i++) {
        uint8_t code[4] = {0, 0, (uint8_t)i, 0};
        CHECK(codebook_add(&cb, RF34DL_BAND_315, code, NULL, false, NULL) == CODEBOOK_OK);
    }
    CHECK(cb.count == CODEBOOK_MAX);
    CHECK(codebook_add(&cb, RF34DL_BAND_433, CODE_C, NULL, false, NULL) == CODEBOOK_ERR_FULL);
}

static void test_text_limits(void)
{
    codebook_t cb;
    int id = 0;
    char name[CODEBOOK_NAME_MAX + 1];
    char too_long[CODEBOOK_NAME_MAX + 2];

    codebook_init(&cb);
    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, NULL, false, &id) == CODEBOOK_OK);

    /* 8 个汉字 = 24B,正好是上限。 */
    memset(name, 0, sizeof(name));
    for (int i = 0; i < 8; i++) {
        memcpy(name + i * 3, "码", 3);
    }
    CHECK(codebook_set_name(&cb, id, name) == CODEBOOK_OK);
    CHECK(strlen(codebook_get(&cb, id)->name) == 24);

    memset(too_long, 'a', CODEBOOK_NAME_MAX + 1);
    too_long[CODEBOOK_NAME_MAX + 1] = 0;
    CHECK(codebook_set_name(&cb, id, too_long) == CODEBOOK_ERR_TEXT);
    CHECK(strlen(codebook_get(&cb, id)->name) == 24);

    CHECK(codebook_set_name(&cb, id, "a\x01" "b") == CODEBOOK_ERR_UTF8);
    CHECK(codebook_set_name(&cb, id, "\xc0\x80") == CODEBOOK_ERR_UTF8);       /* overlong */
    CHECK(codebook_set_name(&cb, id, "\xe4\xb8") == CODEBOOK_ERR_UTF8);       /* truncated */
    CHECK(codebook_set_name(&cb, id, "\xed\xa0\x80") == CODEBOOK_ERR_UTF8);   /* surrogate */
    CHECK(codebook_set_name(&cb, id, "\xf4\x90\x80\x80") == CODEBOOK_ERR_UTF8); /* > U+10FFFF */
    CHECK(codebook_set_name(&cb, id, "Front Gate") == CODEBOOK_OK);
    CHECK(strcmp(codebook_get(&cb, id)->name, "Front Gate") == 0);
    CHECK(codebook_set_name(&cb, id, "") == CODEBOOK_OK);
    CHECK(codebook_get(&cb, id)->name[0] == 0);
}

static void test_text_limits_clean(void)
{
    codebook_t cb;
    int id = 0;

    codebook_init(&cb);
    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, NULL, false, &id) == CODEBOOK_OK);
    CHECK(codebook_set_group(&cb, id, "家门") == CODEBOOK_OK);
    CHECK(strcmp(codebook_get(&cb, id)->group, "家门") == 0);
    CHECK(codebook_set_sym(&cb, id, "🚪") == CODEBOOK_OK);
    CHECK(strcmp(codebook_get(&cb, id)->sym, "🚪") == 0);
    CHECK(codebook_set_sym(&cb, id, "012345678") == CODEBOOK_ERR_TEXT);
    CHECK(codebook_set_group(&cb, id, "0123456789abcdefg") == CODEBOOK_ERR_TEXT);
}

static void test_edit_erase(void)
{
    codebook_t cb;
    int id = 0;

    codebook_init(&cb);
    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, "A", false, &id) == CODEBOOK_OK);
    CHECK(codebook_add(&cb, RF34DL_BAND_433, CODE_B, "B", false, &id) == CODEBOOK_OK);

    CHECK(codebook_set_code(&cb, 1, CODE_B) == CODEBOOK_OK);
    CHECK((codebook_get(&cb, 1)->flags & CODEBOOK_FLAG_EDITED) != 0);
    CHECK(codebook_set_band(&cb, 1, RF34DL_BAND_433) == CODEBOOK_ERR_DUP);
    CHECK(codebook_set_code(&cb, 1, CODE_C) == CODEBOOK_OK);
    CHECK(memcmp(codebook_get(&cb, 1)->code, CODE_C, 4) == 0);

    /* 同值不算编辑。 */
    CHECK(codebook_set_code(&cb, 2, CODE_B) == CODEBOOK_OK);
    CHECK((codebook_get(&cb, 2)->flags & CODEBOOK_FLAG_EDITED) == 0);

    CHECK(codebook_erase(&cb, 1) == CODEBOOK_OK);
    CHECK(cb.count == 1);
    CHECK(strcmp(codebook_get(&cb, 1)->name, "B") == 0);
    CHECK(codebook_erase(&cb, 2) == CODEBOOK_ERR_RANGE);
}

static void test_blob_roundtrip(void)
{
    codebook_t cb;
    codebook_t out;
    int id = 0;
    uint8_t blob[CODEBOOK_BLOB_MAX];

    codebook_init(&cb);
    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, "客厅大门", false, &id) == CODEBOOK_OK);
    CHECK(codebook_set_group(&cb, id, "家门") == CODEBOOK_OK);
    CHECK(codebook_set_sym(&cb, id, "🚪") == CODEBOOK_OK);
    CHECK(codebook_add(&cb, RF34DL_BAND_433, CODE_B, "车库", true, &id) == CODEBOOK_OK);

    size_t n = codebook_serialize(&cb, blob, sizeof(blob));
    CHECK(n == CODEBOOK_BLOB_MAX);
    CHECK(codebook_deserialize(&out, blob, n) == CODEBOOK_OK);
    CHECK(out.count == 2);
    CHECK(strcmp(codebook_get(&out, 1)->name, "客厅大门") == 0);
    CHECK(strcmp(codebook_get(&out, 1)->group, "家门") == 0);
    CHECK(strcmp(codebook_get(&out, 1)->sym, "🚪") == 0);
    CHECK((codebook_get(&out, 2)->flags & CODEBOOK_FLAG_MANUAL) != 0);

    blob[2] = 99;
    CHECK(codebook_deserialize(&out, blob, n) == CODEBOOK_ERR_VERSION);
    blob[2] = CODEBOOK_VERSION;
    blob[0] = 'X';
    CHECK(codebook_deserialize(&out, blob, n) == CODEBOOK_ERR_BLOB);
    blob[0] = CODEBOOK_MAGIC0;
    CHECK(codebook_deserialize(&out, blob, n - 1) == CODEBOOK_ERR_BLOB);
}

static void test_v1_migration(void)
{
    codebook_t cb;
    uint8_t v1[164];

    memset(v1, 0, sizeof(v1));
    v1[0] = CODEBOOK_MAGIC0;
    v1[1] = CODEBOOK_MAGIC1;
    v1[2] = CODEBOOK_V1_VERSION;
    v1[3] = 3;
    v1[4] = RF34DL_BAND_315;
    memcpy(v1 + 5, CODE_A, 4);
    v1[9] = RF34DL_BAND_315;
    memcpy(v1 + 10, CODE_B, 4);
    v1[14] = RF34DL_BAND_433;
    memcpy(v1 + 15, CODE_C, 4);

    CHECK(codebook_migrate_v1(&cb, v1, sizeof(v1)) == CODEBOOK_OK);
    CHECK(cb.count == 3);
    CHECK(codebook_get(&cb, 1)->band == RF34DL_BAND_315);
    CHECK(memcmp(codebook_get(&cb, 1)->code, CODE_A, 4) == 0);
    CHECK(codebook_get(&cb, 3)->band == RF34DL_BAND_433);
    CHECK(codebook_get(&cb, 1)->name[0] == 0);
    CHECK(codebook_get(&cb, 1)->flags == 0);

    v1[2] = 2;
    CHECK(codebook_migrate_v1(&cb, v1, sizeof(v1)) == CODEBOOK_ERR_VERSION);
    v1[2] = CODEBOOK_V1_VERSION;
    v1[3] = CODEBOOK_MAX + 1;
    CHECK(codebook_migrate_v1(&cb, v1, sizeof(v1)) == CODEBOOK_ERR_BLOB);
    v1[3] = 3;
    CHECK(codebook_migrate_v1(&cb, v1, 10) == CODEBOOK_ERR_BLOB);
    v1[4] = 0;
    CHECK(codebook_migrate_v1(&cb, v1, sizeof(v1)) == CODEBOOK_ERR_BAND);
}

static void test_json(void)
{
    codebook_t cb;
    char json[6144];
    int id = 0;

    codebook_init(&cb);
    CHECK(codebook_add(&cb, RF34DL_BAND_315, CODE_A, "客厅\"门", false, &id) == CODEBOOK_OK);
    CHECK(codebook_set_group(&cb, id, "家门") == CODEBOOK_OK);
    CHECK(codebook_set_sym(&cb, id, "🚪") == CODEBOOK_OK);
    CHECK(codebook_add(&cb, RF34DL_BAND_433, CODE_B, NULL, true, &id) == CODEBOOK_OK);
    CHECK(codebook_set_code(&cb, 2, CODE_C) == CODEBOOK_OK);

    size_t n = codebook_codes_json(&cb, json, sizeof(json));
    CHECK(n > 0 && n == strlen(json));
    CHECK(json[0] == '[' && json[n - 1] == ']');
    CHECK(strstr(json, "\"id\":1") != NULL);
    CHECK(strstr(json, "\"code\":\"5555033C\"") != NULL);
    CHECK(strstr(json, "\"name\":\"客厅\\\"门\"") != NULL);
    CHECK(strstr(json, "\"group\":\"家门\"") != NULL);
    CHECK(strstr(json, "\"sym\":\"🚪\"") != NULL);
    CHECK(strstr(json, "\"manual\":1") != NULL);
    CHECK(strstr(json, "\"edited\":1") != NULL);

    codebook_init(&cb);
    n = codebook_codes_json(&cb, json, sizeof(json));
    CHECK(n == 2 && strcmp(json, "[]") == 0);
}

static void test_json_max(void)
{
    codebook_t cb;
    char json[6144];
    char name[CODEBOOK_NAME_MAX + 1];
    char group[CODEBOOK_GROUP_MAX + 1];
    char sym[CODEBOOK_SYM_MAX + 1];

    memset(name, 'n', CODEBOOK_NAME_MAX);
    name[CODEBOOK_NAME_MAX] = 0;
    memset(group, 'g', CODEBOOK_GROUP_MAX);
    group[CODEBOOK_GROUP_MAX] = 0;
    memset(sym, 's', CODEBOOK_SYM_MAX);
    sym[CODEBOOK_SYM_MAX] = 0;

    codebook_init(&cb);
    for (int i = 0; i < CODEBOOK_MAX; i++) {
        uint8_t code[4] = {0, 0, 0, (uint8_t)(i + 1)};
        int id = 0;
        CHECK(codebook_add(&cb, RF34DL_BAND_315, code, name, false, &id) == CODEBOOK_OK);
        CHECK(codebook_set_group(&cb, id, group) == CODEBOOK_OK);
        CHECK(codebook_set_sym(&cb, id, sym) == CODEBOOK_OK);
    }
    size_t n = codebook_codes_json(&cb, json, sizeof(json));
    CHECK(n > 0 && n < sizeof(json));
    CHECK(json[0] == '[' && json[n - 1] == ']');
}

int main(void)
{
    test_add_find();
    test_text_limits();
    test_text_limits_clean();
    test_edit_erase();
    test_blob_roundtrip();
    test_v1_migration();
    test_json();
    test_json_max();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
