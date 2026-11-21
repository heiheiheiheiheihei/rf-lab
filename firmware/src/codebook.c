#include "codebook.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "rf34dl.h"

_Static_assert(sizeof(codebook_rec_t) == 57, "codebook_rec_t must stay 57 bytes");
_Static_assert(sizeof(codebook_blob_t) == CODEBOOK_BLOB_MAX, "blob size mismatch");

void codebook_init(codebook_t *cb)
{
    if (!cb) {
        return;
    }
    memset(cb, 0, sizeof(*cb));
}

const codebook_rec_t *codebook_get(const codebook_t *cb, int id)
{
    if (!cb || id < 1 || id > cb->count) {
        return NULL;
    }
    return &cb->recs[id - 1];
}

codebook_rec_t *codebook_get_mut(codebook_t *cb, int id)
{
    if (!cb || id < 1 || id > cb->count) {
        return NULL;
    }
    return &cb->recs[id - 1];
}

int codebook_find(const codebook_t *cb, uint8_t band, const uint8_t code[4])
{
    if (!cb || !code) {
        return -1;
    }
    for (int i = 0; i < cb->count; i++) {
        if (cb->recs[i].band == band &&
            memcmp(cb->recs[i].code, code, sizeof(cb->recs[i].code)) == 0) {
            return i;
        }
    }
    return -1;
}

bool codebook_utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");

    while (*p) {
        unsigned char c = *p;
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
        if (c < 0x80) {
            p++;
            continue;
        }

        uint32_t cp;
        int extra;
        if ((c & 0xe0) == 0xc0) {
            cp = c & 0x1f;
            extra = 1;
        } else if ((c & 0xf0) == 0xe0) {
            cp = c & 0x0f;
            extra = 2;
        } else if ((c & 0xf8) == 0xf0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            return false;
        }

        for (int i = 0; i < extra; i++) {
            p++;
            if ((*p & 0xc0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (*p & 0x3f);
        }
        if ((extra == 1 && cp < 0x80) ||
            (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            return false;
        }
        p++;
    }
    return true;
}

bool codebook_text_fits(const char *s, size_t max_bytes)
{
    if (!s) {
        return true;
    }
    return strlen(s) <= max_bytes && codebook_utf8_valid(s);
}

const char *codebook_band_name(uint8_t band)
{
    return rf34dl_band_name(band);
}

const char *codebook_err_name(codebook_err_t err)
{
    switch (err) {
    case CODEBOOK_OK:
        return "ok";
    case CODEBOOK_ERR_ARG:
        return "arg";
    case CODEBOOK_ERR_FULL:
        return "full";
    case CODEBOOK_ERR_DUP:
        return "dup";
    case CODEBOOK_ERR_RANGE:
        return "range";
    case CODEBOOK_ERR_BAND:
        return "band";
    case CODEBOOK_ERR_CODE:
        return "code";
    case CODEBOOK_ERR_TEXT:
        return "text";
    case CODEBOOK_ERR_UTF8:
        return "utf8";
    case CODEBOOK_ERR_BLOB:
        return "blob";
    case CODEBOOK_ERR_VERSION:
        return "version";
    default:
        return "unknown";
    }
}

/* 先把文本写进临时区,校验通过才提交;失败不破坏原值。 */
static codebook_err_t text_set(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return CODEBOOK_ERR_ARG;
    }
    if (!src) {
        src = "";
    }
    size_t n = strlen(src);
    if (n >= cap) {
        return CODEBOOK_ERR_TEXT;
    }
    if (!codebook_utf8_valid(src)) {
        return CODEBOOK_ERR_UTF8;
    }
    memcpy(dst, src, n + 1);
    return CODEBOOK_OK;
}

static codebook_err_t check_code(const uint8_t code[4])
{
    return code ? CODEBOOK_OK : CODEBOOK_ERR_ARG;
}

codebook_err_t codebook_add(codebook_t *cb, uint8_t band, const uint8_t code[4],
                            const char *name, bool manual, int *out_id)
{
    if (!cb) {
        return CODEBOOK_ERR_ARG;
    }
    if (!rf34dl_band_ok(band)) {
        return CODEBOOK_ERR_BAND;
    }
    codebook_err_t e = check_code(code);
    if (e != CODEBOOK_OK) {
        return e;
    }
    if (codebook_find(cb, band, code) >= 0) {
        return CODEBOOK_ERR_DUP;
    }
    if (cb->count >= CODEBOOK_MAX) {
        return CODEBOOK_ERR_FULL;
    }

    codebook_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.band = band;
    rec.flags = manual ? CODEBOOK_FLAG_MANUAL : 0;
    memcpy(rec.code, code, sizeof(rec.code));
    e = text_set(rec.name, sizeof(rec.name), name ? name : "");
    if (e != CODEBOOK_OK) {
        return e;
    }

    cb->recs[cb->count] = rec;
    cb->count++;
    if (out_id) {
        *out_id = cb->count;
    }
    return CODEBOOK_OK;
}

codebook_err_t codebook_set_name(codebook_t *cb, int id, const char *text)
{
    codebook_rec_t *rec = codebook_get_mut(cb, id);
    if (!rec) {
        return CODEBOOK_ERR_RANGE;
    }
    return text_set(rec->name, sizeof(rec->name), text ? text : "");
}

codebook_err_t codebook_set_group(codebook_t *cb, int id, const char *text)
{
    codebook_rec_t *rec = codebook_get_mut(cb, id);
    if (!rec) {
        return CODEBOOK_ERR_RANGE;
    }
    return text_set(rec->group, sizeof(rec->group), text ? text : "");
}

codebook_err_t codebook_set_sym(codebook_t *cb, int id, const char *text)
{
    codebook_rec_t *rec = codebook_get_mut(cb, id);
    if (!rec) {
        return CODEBOOK_ERR_RANGE;
    }
    return text_set(rec->sym, sizeof(rec->sym), text ? text : "");
}

codebook_err_t codebook_set_band(codebook_t *cb, int id, uint8_t band)
{
    codebook_rec_t *rec = codebook_get_mut(cb, id);
    if (!rec) {
        return CODEBOOK_ERR_RANGE;
    }
    if (!rf34dl_band_ok(band)) {
        return CODEBOOK_ERR_BAND;
    }
    if (rec->band == band) {
        return CODEBOOK_OK;
    }
    if (codebook_find(cb, band, rec->code) >= 0) {
        return CODEBOOK_ERR_DUP;
    }
    rec->band = band;
    rec->flags |= CODEBOOK_FLAG_EDITED;
    return CODEBOOK_OK;
}

codebook_err_t codebook_set_code(codebook_t *cb, int id, const uint8_t code[4])
{
    codebook_rec_t *rec = codebook_get_mut(cb, id);
    if (!rec) {
        return CODEBOOK_ERR_RANGE;
    }
    codebook_err_t e = check_code(code);
    if (e != CODEBOOK_OK) {
        return e;
    }
    if (memcmp(rec->code, code, sizeof(rec->code)) == 0) {
        return CODEBOOK_OK;
    }
    if (codebook_find(cb, rec->band, code) >= 0) {
        return CODEBOOK_ERR_DUP;
    }
    memcpy(rec->code, code, sizeof(rec->code));
    rec->flags |= CODEBOOK_FLAG_EDITED;
    return CODEBOOK_OK;
}

codebook_err_t codebook_erase(codebook_t *cb, int id)
{
    if (!cb || id < 1 || id > cb->count) {
        return CODEBOOK_ERR_RANGE;
    }
    if (id < cb->count) {
        memmove(&cb->recs[id - 1], &cb->recs[id],
                sizeof(cb->recs[0]) * (size_t)(cb->count - id));
    }
    cb->count--;
    memset(&cb->recs[cb->count], 0, sizeof(cb->recs[0]));
    return CODEBOOK_OK;
}

size_t codebook_serialize(const codebook_t *cb, uint8_t *out, size_t cap)
{
    if (!cb || !out || cap < sizeof(codebook_blob_t)) {
        return 0;
    }
    codebook_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic[0] = CODEBOOK_MAGIC0;
    blob.magic[1] = CODEBOOK_MAGIC1;
    blob.ver = CODEBOOK_VERSION;
    blob.count = (uint8_t)cb->count;
    if (cb->count > 0) {
        memcpy(blob.recs, cb->recs, sizeof(cb->recs[0]) * (size_t)cb->count);
    }
    memcpy(out, &blob, sizeof(blob));
    return sizeof(blob);
}

static void rec_sanitize(codebook_rec_t *rec)
{
    rec->name[CODEBOOK_NAME_MAX] = 0;
    rec->group[CODEBOOK_GROUP_MAX] = 0;
    rec->sym[CODEBOOK_SYM_MAX] = 0;
    rec->flags &= (uint8_t)(CODEBOOK_FLAG_MANUAL | CODEBOOK_FLAG_EDITED);
    if (!codebook_utf8_valid(rec->name)) {
        rec->name[0] = 0;
    }
    if (!codebook_utf8_valid(rec->group)) {
        rec->group[0] = 0;
    }
    if (!codebook_utf8_valid(rec->sym)) {
        rec->sym[0] = 0;
    }
}

codebook_err_t codebook_deserialize(codebook_t *cb, const uint8_t *blob, size_t len)
{
    if (!cb || !blob) {
        return CODEBOOK_ERR_ARG;
    }
    if (len != sizeof(codebook_blob_t)) {
        return CODEBOOK_ERR_BLOB;
    }
    const codebook_blob_t *b = (const codebook_blob_t *)blob;
    if (b->magic[0] != CODEBOOK_MAGIC0 || b->magic[1] != CODEBOOK_MAGIC1) {
        return CODEBOOK_ERR_BLOB;
    }
    if (b->ver != CODEBOOK_VERSION) {
        return CODEBOOK_ERR_VERSION;
    }
    if (b->count > CODEBOOK_MAX) {
        return CODEBOOK_ERR_BLOB;
    }

    codebook_init(cb);
    cb->count = b->count;
    for (int i = 0; i < cb->count; i++) {
        cb->recs[i] = b->recs[i];
        rec_sanitize(&cb->recs[i]);
        if (!rf34dl_band_ok(cb->recs[i].band)) {
            codebook_init(cb);
            return CODEBOOK_ERR_BAND;
        }
    }
    return CODEBOOK_OK;
}

codebook_err_t codebook_migrate_v1(codebook_t *cb, const uint8_t *blob, size_t len)
{
    if (!cb || !blob) {
        return CODEBOOK_ERR_ARG;
    }
    if (len < 4) {
        return CODEBOOK_ERR_BLOB;
    }
    if (blob[0] != CODEBOOK_MAGIC0 || blob[1] != CODEBOOK_MAGIC1) {
        return CODEBOOK_ERR_BLOB;
    }
    if (blob[2] != CODEBOOK_V1_VERSION) {
        return CODEBOOK_ERR_VERSION;
    }
    uint8_t count = blob[3];
    if (count > CODEBOOK_MAX) {
        return CODEBOOK_ERR_BLOB;
    }
    if (len < 4u + (size_t)count * 5u) {
        return CODEBOOK_ERR_BLOB;
    }

    codebook_init(cb);
    for (int i = 0; i < count; i++) {
        const uint8_t *p = blob + 4 + (size_t)i * 5u;
        codebook_rec_t *rec = &cb->recs[i];
        rec->band = p[0];
        memcpy(rec->code, p + 1, sizeof(rec->code));
        rec->flags = 0;
        rec->name[0] = 0;
        rec->group[0] = 0;
        rec->sym[0] = 0;
        if (!rf34dl_band_ok(rec->band)) {
            codebook_init(cb);
            return CODEBOOK_ERR_BAND;
        }
    }
    cb->count = count;
    return CODEBOOK_OK;
}

static int cb_append(char *buf, size_t cap, int off, const char *fmt, ...)
{
    va_list ap;

    if (off < 0 || (size_t)off >= cap) {
        return -1;
    }
    va_start(ap, fmt);
    int n = vsnprintf(buf + off, cap - (size_t)off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - (size_t)off) {
        return -1;
    }
    return off + n;
}

size_t codebook_codes_json(const codebook_t *cb, char *out, size_t cap)
{
    if (!out || cap < 3) {
        if (out && cap > 0) {
            out[0] = 0;
        }
        return 0;
    }
    if (!cb) {
        snprintf(out, cap, "[]");
        return 2;
    }

    char name_esc[CODEBOOK_NAME_MAX * 6 + 1];
    char group_esc[CODEBOOK_GROUP_MAX * 6 + 1];
    char sym_esc[CODEBOOK_SYM_MAX * 6 + 1];
    char hex[9];
    int o = cb_append(out, cap, 0, "[");

    for (int i = 0; i < cb->count; i++) {
        const codebook_rec_t *rec = &cb->recs[i];
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X",
                 rec->code[0], rec->code[1], rec->code[2], rec->code[3]);
        o = cb_append(out, cap, o,
                      "%s{\"id\":%d,\"band\":\"%s\",\"code\":\"%s\"",
                      i ? "," : "", i + 1, codebook_band_name(rec->band), hex);
        if (o < 0) {
            break;
        }
        if (rec->name[0]) {
            link_json_escape(rec->name, name_esc, sizeof(name_esc));
            o = cb_append(out, cap, o, ",\"name\":\"%s\"", name_esc);
            if (o < 0) {
                break;
            }
        }
        if (rec->group[0]) {
            link_json_escape(rec->group, group_esc, sizeof(group_esc));
            o = cb_append(out, cap, o, ",\"group\":\"%s\"", group_esc);
            if (o < 0) {
                break;
            }
        }
        if (rec->sym[0]) {
            link_json_escape(rec->sym, sym_esc, sizeof(sym_esc));
            o = cb_append(out, cap, o, ",\"sym\":\"%s\"", sym_esc);
            if (o < 0) {
                break;
            }
        }
        if (rec->flags & CODEBOOK_FLAG_MANUAL) {
            o = cb_append(out, cap, o, ",\"manual\":1");
            if (o < 0) {
                break;
            }
        }
        if (rec->flags & CODEBOOK_FLAG_EDITED) {
            o = cb_append(out, cap, o, ",\"edited\":1");
            if (o < 0) {
                break;
            }
        }
        o = cb_append(out, cap, o, "}");
        if (o < 0) {
            break;
        }
    }

    if (o < 0) {
        snprintf(out, cap, "[]");
        return 2;
    }
    o = cb_append(out, cap, o, "]");
    if (o < 0) {
        snprintf(out, cap, "[]");
        return 2;
    }
    return (size_t)o;
}
