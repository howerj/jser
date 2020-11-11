/* Author:  Richard James Howe
 * Project: JSON Serialization Routines
 * License: MIT (for jsmn.h), Proprietary for everything else.
 *
 * This library takes a list of pointers to objects of various types,
 * such as strings, (long) integers, buffers, booleans, and other
 * lists and then serializes the result into JSON. View the file
 * 'jser.md' for more information. */

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include "jsmn.h"
#include "jser.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifndef JSER_ENABLE_TESTS
#define JSER_ENABLE_TESTS    (1)
#endif

#ifndef JSER_ENABLE_ESCAPE
#define JSER_ENABLE_ESCAPE   (1)
#endif

#ifndef JSER_ENABLE_USED_SET
#define JSER_ENABLE_USED_SET (1)
#endif

#ifndef JSER_MAX_DEPTH
#define JSER_MAX_DEPTH (0) /* 0 = unlimited */
#endif

#ifndef JSER_PRETTY_STRING
#define JSER_PRETTY_STRING "\t"
#endif

#ifndef JSER_VERSION
#define JSER_VERSION (0x000000ul) /* set by build system */
#endif

#define implies(X, Y)           (assert(!(X) || (Y)))
#define ELEMENTS(X)             (sizeof(X) / sizeof(X[0]))
#define UNUSED(X)               ((void)(X))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

/* it would be nice, but not required, to turn this into a string for debugging */
typedef enum {
    JSER_OK           =   0,
    JSER_ERR_UNKNOWN  =  -1, /**< some unspecified error */
    JSER_ERR_DEPTH    =  -2, /**< recursion limit exceeded */
    JSER_ERR_BASE64   =  -3, /**< base-64 codec failed */
    JSER_ERR_SPACE    =  -4, /**< not enough space to carry out operation */
    JSER_ERR_DISABLED =  -5, /**< feature is disabled */
    JSER_ERR_PARSE    =  -6, /**< deserialization; not valid json */
    JSER_ERR_MORE_DAT =  -7, /**< deserialization; need more data, not a full JSON packet */
    JSER_ERR_TYPE     =  -8, /**< deserialization; type mismatch */
    JSER_ERR_NUMBER   =  -9, /**< deserialization; invalid numeric format */
    JSER_ERR_VERSION  = -10, /**< version not set */
    JSER_ERR_CONFIG   = -11, /**< invalid configuration structure */
    JSER_ERR_LENGTH   = -12, /**< deserialization; length too short */
} jsonify_error_e;

typedef struct {
    unsigned max;
    jsonify_error_e error;
    unsigned pretty : 1, dry_run: 1;
} jser_opts_t;

int jser_version(unsigned long *version)
{
    assert(version);
    BUILD_BUG_ON(JSER_ENABLE_TESTS    != 0 && JSER_ENABLE_TESTS    != 1);
    BUILD_BUG_ON(JSER_ENABLE_ESCAPE   != 0 && JSER_ENABLE_ESCAPE   != 1);
    BUILD_BUG_ON(JSER_ENABLE_USED_SET != 0 && JSER_ENABLE_USED_SET != 1);
    unsigned long options =
        JSER_ENABLE_TESTS    << 0 |
        JSER_ENABLE_ESCAPE   << 1 |
        JSER_ENABLE_USED_SET << 2 ;
    *version = (options << 24) | JSER_VERSION;
    return JSER_VERSION == 0 ? JSER_ERR_VERSION : 0;
}

static inline int boolify(int x)
{
    return !!x;
}

static inline int within(uint64_t value, uint64_t lo, uint64_t hi)
{
    return (value >= lo) && (value <= hi);
}

static inline void reverse(char *const r, const size_t length)
{
    assert(r);
    const size_t last = length - 1;
    for (size_t i = 0; i < length / 2ul; i++) {
        const size_t t = r[i];
        r[i] = r[last - i];
        r[last - i] = t;
    }
}

static inline void u64_to_str(char b[64], uint64_t u, const uint64_t base)
{
    assert(b);
    assert(within(base, 2, 16));
    unsigned i = 0;
    do {
        const uint64_t q = u % base;
        const uint64_t r = u / base;
        b[i++] = q["0123456789ABCDEF"];
        u = r;
    } while (u);
    reverse(b, i);
    b[i] = '\0';
    assert(i >= 1);
}

static inline void i64_to_str(char b[65], int64_t s, const uint64_t base)
{
    assert(b);
    if (s < 0) {
        b[0] = '-';
        s = -s;
        u64_to_str(b + 1, s, base);
        return;
    }
    u64_to_str(b, s, base);
}

static inline int digit(int ch, int base)
{
    int r = -1;
    if (ch >= '0' && ch <= '9') {
        r = ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        r = ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        r = ch - 'A' + 10;
    }
    if (r >= base) {
        r = -1;
    }
    return r;
}

static int str_to_u64(const char *str, const size_t length, const uint64_t base, uint64_t *out)
{
    assert(str);
    assert(out);
    assert(within(base, 2, 16));
    *out = 0;
    if (length == 0) {
        return -1;
    }
    uint64_t t = 0;
    for (size_t i = 0; i < length; i++) {
        const int dg = digit(str[i], base);
        if (dg < 0) {
            return -1;
        }
        const uint64_t nt1 = t * base;
        if (nt1 < t) { /* overflow */
            return -1;
        }
        const uint64_t nt2 = nt1 + dg;
        if (nt2 < t) {/* overflow */
            return -1;
        }
        t = nt2;
    }
    *out = t;
    return 0;
}

static int str_to_i64(const char *str, const size_t length, const uint64_t base, int64_t *out)
{
    assert(str);
    assert(out);
    assert(within(base, 2, 16));
    const int negative = length > 0 && str[0] == '-';
    uint64_t t = 0;
    if (str_to_u64(str + negative, length - negative, base, &t) < 0) {
        return -1;
    }
    int64_t o = t;
    if (negative) {
        o = -o;
    }
    *out = o;
    return 0;
}

static inline size_t base64_decoded_size(const size_t sz)
{
    assert((sz * 3ull) >= sz);
    return (sz * 3ull) / 4ull;
}

int jser_base64_decode(const unsigned char *ibuf, const size_t ilen, unsigned char *obuf, size_t *olen)
{
    assert(ibuf);
    assert(obuf);
    assert(olen);

    enum { WS = 64u, /* white space */ EQ = 65u, /*equals*/ XX = 66u, /* invalid */ };

    static const unsigned char d[] = { /* 0-63 = valid chars, 64-66 = special */
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, WS, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, 62, XX, XX, XX, 63, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, XX, XX, XX, EQ, XX, XX, XX, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, XX, XX, XX, XX, XX, XX, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
        XX, XX, XX, XX, XX, XX
    };

    unsigned iter = 0;
    uint32_t buf = 0;
    size_t len = 0;

    for (const unsigned char *end = ibuf + ilen; ibuf < end; ibuf++) {
        const unsigned char c = d[(int)(*ibuf)];

        switch (c) {
        case WS: continue;   /* skip whitespace */
        case XX: return -1;  /* invalid input, return error */
        case EQ:             /* pad character, end of data */
            ibuf = end;
            continue;
        default:
            assert(c < 64);
            buf = (buf << 6) | c;
            iter++;
            /* If the buffer is full, split it into bytes */
            if (iter == 4) {
                if ((len += 3) > *olen) { /* buffer overflow */
                    return -1;
                }
                *(obuf++) = (buf >> 16) & 0xFFul;
                *(obuf++) = (buf >> 8) & 0xFFul;
                *(obuf++) = buf & 0xFFul;
                buf  = 0;
                iter = 0;
            }
        }
    }

    if (iter == 3) {
        if ((len += 2) > *olen) {
            return -1;    /* buffer overflow */
        }
        *(obuf++) = (buf >> 10) & 0xFFul;
        *(obuf++) = (buf >>  2) & 0xFFul;
    } else if (iter == 2) {
        if (++len > *olen) {
            return -1;    /* buffer overflow */
        }
        *(obuf++) = (buf >> 4) & 0xFFul;
    }

    *olen = len; /* modify to reflect the actual size */
    return 0;
}

static inline size_t base64_encoded_size(const size_t sz)
{
    return 4ull * ((sz + 2ull) / 3ull);
}

int jser_base64_encode(const unsigned char *ibuf, size_t ilen, unsigned char *obuf, size_t *olen)
{
    assert(ibuf);
    assert(olen);

    static int mod_table[] = {
        0, 2, 1
    };

    static char lookup[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
        'w', 'x', 'y', 'z', '0', '1', '2', '3',
        '4', '5', '6', '7', '8', '9', '+', '/'
    };

    const size_t osz = base64_encoded_size(ilen);

    if (*olen < osz) {
        return -1;
    }

    *olen = osz;

    for (size_t i = 0, j = 0; i < ilen;) {
        const uint32_t a = i < ilen ? ibuf[i++] : 0;
        const uint32_t b = i < ilen ? ibuf[i++] : 0;
        const uint32_t c = i < ilen ? ibuf[i++] : 0;
        const uint32_t triple = (a << 0x10) | (b << 0x08) | c;

        obuf[j++] = lookup[(triple >> 3 * 6) & 0x3F];
        obuf[j++] = lookup[(triple >> 2 * 6) & 0x3F];
        obuf[j++] = lookup[(triple >> 1 * 6) & 0x3F];
        obuf[j++] = lookup[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[ilen % 3]; i++) {
        obuf[*olen - 1 - i] = '=';
    }

    return 0;
}

static int on_error(jser_opts_t *sp, jsonify_error_e error)
{
    assert(sp);
    sp->error = sp->error ? sp->error : error;
    return error;
}

static int add_ch(jser_opts_t *sp, jser_buffer_t *b, int ch)
{
    assert(sp);
    assert(b);
    assert(b->used <= b->length);
    if ((1 + b->used) > b->length) {
        return on_error(sp, JSER_ERR_SPACE);
    }
    if (sp->dry_run == 0) {
        b->buf[b->used] = ch;
    }
    b->used++;
    return 0;
}

static int addescaped(jser_opts_t *sp, jser_buffer_t *b, const char esc)
{
    assert(sp);
    assert(b);
    if (add_ch(sp, b, '\\') < 0) {
        return -1;
    }
    if (add_ch(sp, b, esc) < 0) {
        return -1;
    }
    return 0;
}

static int addstr(jser_opts_t *sp, jser_buffer_t *b, const char *str)
{
    assert(sp);
    assert(b);
    assert(str);
    assert(b->used <= b->length);
    if (JSER_ENABLE_ESCAPE) {
        int ch = 0;
        for (size_t i = 0; (ch = str[i]); i++) {
            switch (ch) {
            case '\b': if (addescaped(sp, b, 'b') < 0) {
                    return -1;
                } break;
            case '\f': if (addescaped(sp, b, 'f') < 0) {
                    return -1;
                } break;
            case '\n': if (addescaped(sp, b, 'n') < 0) {
                    return -1;
                } break;
            case '\r': if (addescaped(sp, b, 'r') < 0) {
                    return -1;
                } break;
            case '\t': if (addescaped(sp, b, 't') < 0) {
                    return -1;
                } break;
            case '\\':
            case '"':
                if (addescaped(sp, b, ch) < 0) {
                    return -1;
                }
                break;
            default:
                if (add_ch(sp, b, ch) < 0) {
                    return -1;
                }
                break;
            }
        }
    } else {
        const size_t slen = strlen(str);
        if ((slen + b->used) > b->length) {
            return on_error(sp, JSER_ERR_SPACE);
        }
        implies(sp->dry_run == 0, b->buf);
        if (sp->dry_run == 0) {
            memcpy(&b->buf[b->used], str, slen);
        }
        b->used += slen;
    }
    return 0;
}

static int add_quote(jser_opts_t *sp, jser_buffer_t *b, const char *quoteme)
{
    assert(sp);
    assert(b);
    assert(quoteme);
    if (add_ch(sp, b, '"') < 0) {
        return -1;
    }
    if (addstr(sp, b, quoteme) < 0) {
        return -1;
    }
    if (add_ch(sp, b, '"') < 0) {
        return -1;
    }
    return 0;
}

static int add_attr(jser_opts_t *sp, jser_buffer_t *b, const char *attr)
{
    assert(sp);
    assert(b);
    assert(attr);
    if (add_quote(sp, b, attr) < 0) {
        return -1;
    }
    if (add_ch(sp, b, ':') < 0) {
        return -1;
    }
    return 0;
}

static int add_indent(jser_opts_t *sp, jser_buffer_t *b, size_t count)
{
    assert(sp);
    assert(b);
    if (sp->pretty == 0) {
        return 0;
    }
    for (size_t i = 0; i < count; i++)
        for (size_t j = 0; JSER_PRETTY_STRING[j]; j++)
            if (add_ch(sp, b, JSER_PRETTY_STRING[j]) < 0) {
                return -1;
            }
    return 0;
}

static int add_space(jser_opts_t *sp, jser_buffer_t *b)
{
    assert(sp);
    assert(b);
    if (sp->pretty == 0) {
        return 0;
    }
    return add_ch(sp, b, ' ');
}

static int add_newline(jser_opts_t *sp, jser_buffer_t *b)
{
    assert(sp);
    assert(b);
    if (sp->pretty == 0) {
        return 0;
    }
    return add_ch(sp, b, '\n');
}

static int add_i64(jser_opts_t *sp, jser_buffer_t *b, const jser_long_t ld)
{
    assert(sp);
    assert(b);
    char str[65] = { 0 };
    i64_to_str(str, ld, 10);
    if (addstr(sp, b, str) < 0) {
        return -1;
    }
    return 0;
}

static int add_u64(jser_opts_t *sp, jser_buffer_t *b, const jser_ulong_t lu)
{
    assert(sp);
    assert(b);
    char str[64] = { 0 };
    u64_to_str(str, lu, 10);
    if (addstr(sp, b, str) < 0) {
        return -1;
    }
    return 0;
}

static int add_bool(jser_opts_t *sp, jser_buffer_t *b, bool tf)
{
    assert(sp);
    assert(b);
    const char *add = tf ? "true" : "false";
    if (addstr(sp, b, add) < 0) {
        return -1;
    }
    return 0;
}

static int add_buffer(jser_opts_t *sp, jser_buffer_t *b, jser_buffer_t *buf)
{
    assert(sp);
    assert(b);
    assert(buf);
    const size_t osz = base64_encoded_size(buf->length);
    if (add_ch(sp, b, '"') < 0) {
        return -1;
    }
    assert(b->length >= b->used);
    if ((b->length - b->used) < osz) {
        return on_error(sp, JSER_ERR_SPACE);
    }
    if (sp->dry_run == 0) {
        size_t nl = buf->length ? b->length : 0;
        implies(buf->length, buf->buf);
        if (buf->length) {
            if (jser_base64_encode(buf->buf, buf->used, &b->buf[b->used], &nl) < 0) {
                return on_error(sp, JSER_ERR_BASE64);
            }
        }
        b->used += base64_encoded_size(buf->used);
    } else {
        b->used += base64_encoded_size(buf->used);
    }
    if (add_ch(sp, b, '"') < 0) {
        return -1;
    }
    return 0;
}

static int add_value(jser_opts_t *sp, jser_buffer_t *b, jser_type_e type, const jser_type_u *u, size_t index)
{
    assert(sp);
    assert(b);
    assert(u->ld);
    switch (type) {
    case JSER_LONG_E:
        if (add_i64(sp, b, * &u->ld[index]) < 0) {
            return -1;
        }
        break;
    case JSER_ULONG_E:
        if (add_u64(sp, b, * &u->lu[index]) < 0) {
            return -1;
        }
        break;
    case JSER_BOOL_E:
        if (add_bool(sp, b, * &u->b[index]) < 0) {
            return -1;
        }
        break;
    case JSER_ASCIIZ_E:
        if (index != 0) {/* cannot be indexed */
            return on_error(sp, JSER_ERR_CONFIG);
        }
        if (add_quote(sp, b, u->asciiz) < 0) {
            return -1;
        }
        break;
    case JSER_BUFFER_E:
        if (add_buffer(sp, b, &u->buf[index]) < 0) {
            return -1;
        }
        break;
    default:
        return on_error(sp, JSER_ERR_TYPE);
    }
    return 0;
}

static int jsonify(jser_opts_t *sp, const jser_t *j, const size_t jlen, jser_buffer_t *b, const int is_array, size_t depth);

static int addj(jser_opts_t *sp, jser_buffer_t *b, const jser_t *e, size_t depth)
{
    assert(sp);
    assert(b);
    assert(e);

    if (e->type == JSER_OBJECT_E) {
        if (e->is_array) {
            return on_error(sp, JSER_ERR_CONFIG);
        }
        if (add_newline(sp, b)) {
            return -1;
        }
        if (jsonify(sp, e->data.jser, e->length, b, 0, depth + 1) < 0) {
            return -1;
        }
        return 0;
    }

    if (e->type == JSER_ARRAY_E) {
        /* do not care if 'e->is_array' is set, as this is obviously an array */
        if (add_newline(sp, b)) {
            return -1;
        }
        if (jsonify(sp, e->data.array, e->length, b, 1, depth + 1) < 0) {
            return -1;
        }
        return 0;
    }

    if (e->is_array) {
        if (add_ch(sp, b, '[') < 0) {
            return -1;
        }
        for (size_t i = 0; i < e->used; i++) {
            const int last = i == e->used - 1;
            if (add_value(sp, b, e->type, &e->data, i) < 0) {
                return -1;
            }
            if (!last) {
                if (add_ch(sp, b, ',') < 0) {
                    return -1;
                }
            }
        }
        if (add_ch(sp, b, ']') < 0) {
            return -1;
        }
    } else {
        if (add_value(sp, b, e->type, &e->data, 0) < 0) {
            return -1;
        }
    }

    return 0;
}

static int jsonify(jser_opts_t *sp, const jser_t *j, const size_t jlen, jser_buffer_t *b, const int is_array, size_t depth)
{
    assert(sp);
    assert(j);
    assert(b);

    if (sp->max != 0 && depth > sp->max) {
        return on_error(sp, JSER_ERR_DEPTH);
    }
    if (add_indent(sp, b, depth)) {
        return -1;
    }
    if (add_ch(sp, b, is_array ? '[' : '{') < 0) {
        return -1;
    }
    if (add_newline(sp, b)) {
        return -1;
    }

    for (size_t i = 0; i < jlen; i++) {
        const jser_t *e = &j[i];
        const int last = i == jlen - 1;
        if (e->data.lu == NULL) {
            return on_error(sp, JSER_ERR_CONFIG);
        }

        if (add_indent(sp, b, depth + 1)) {
            return -1;
        }

        if (is_array == 0) {
            if (add_attr(sp, b, e->attr) < 0) {
                return -1;
            }
            if (add_space(sp, b)) {
                return -1;
            }
        }

        if (addj(sp, b, e, depth) < 0) {
            return -1;
        }

        if (!last) {
            if (add_ch(sp, b, ',') < 0) {
                return -1;
            }
        }
        if (add_newline(sp, b)) {
            return -1;
        }
    }

    if (add_indent(sp, b, depth)) {
        return -1;
    }
    if (add_ch(sp, b, is_array ? ']' : '}') < 0) {
        return -1;
    }
    return 0;
}

int jser_serialize_to_buffer(const jser_t *j, size_t jlen, const int pretty, jser_buffer_t *b)
{
    assert(j);
    assert(b);
    jser_opts_t sp = {
        .max     = JSER_MAX_DEPTH,
        .pretty  = boolify(pretty),
        .dry_run = boolify(0),
        .error   = JSER_OK,
    };
    return jsonify(&sp, j, jlen, b, 0, 0) < 0 ? sp.error : JSER_OK;
}

int jser_serialized_length(const jser_t *j, const size_t jlen, const int pretty, size_t *sz)
{
    assert(j);
    assert(sz);
    jser_opts_t sp = {
        .max     = JSER_MAX_DEPTH,
        .pretty  = boolify(pretty),
        .dry_run = boolify(1),
        .error   = JSER_OK,
    };
    jser_buffer_t b = {
        .length = SIZE_MAX,
        .used   = 0,
        .buf    = NULL,
    };
    *sz = 0;
    if (jsonify(&sp, j, jlen, &b, 0, 0) < 0) {
        assert(sp.error < 0);
        return sp.error;
    }
    assert(sp.error == 0);
    *sz = b.used;
    return 0;
}

int jser_serialize_to_asciiz(const jser_t *j, size_t jlen, int pretty, char *asciiz, size_t length)
{
    assert(j);
    assert(asciiz);
    if (length < 1) {
        return -1;
    }
    jser_opts_t sp = {
        .max     = JSER_MAX_DEPTH,
        .pretty  = boolify(pretty),
        .dry_run = boolify(0),
    };
    jser_buffer_t b = {
        .length = length - 1ull,
        .used   = 0,
        .buf    = (unsigned char *)asciiz,
    };
    if (jsonify(&sp, j, jlen, &b, 0, 0) < 0) {
        asciiz[0] = '\0';
        return sp.error;
    }
    b.buf[b.used++] = '\0';
    return JSER_OK;
}

/* ~~~ Deserialization ~~~ */

static int find_element(const jser_t *j, size_t jlen, const char *json, jsmntok_t *t)
{
    assert(j);
    assert(t);
    assert(json);
    assert(j->used <= INT_MAX);
    const int l = t->end - t->start;
    assert(l >= 0);
    for (size_t i = 0; i < jlen; i++) {
        const jser_t *e = &j[i];
        const char *attr = e->attr;
        const size_t al = strlen(attr);
        assert(al <= INT_MAX);
        if ((int)al != l) {
            continue;
        }
        if (memcmp(&json[t->start], attr, l)) {
            continue;
        }
        return i;
    }
    return -1;
}

/* A pair of mutually recursive functions, they return an error or an amount to increment through the
 * parse tokens by. */
static int dejsonify(jser_opts_t *sp, jser_t *j, size_t jlen, jsmntok_t *token, const size_t tokens, const char *json);

/* TODO: Allow deserialization of arrays specified with the 'is_array' flag */
static int json_to_element(jser_opts_t *sp, jser_t *e, jsmntok_t *token, const size_t tokens, const char *json)
{
    assert(sp);
    assert(e);
    assert(token);
    assert(json);

    int increment = 1;
    jsmntok_t *p = &token[0];
    const int plen = p->end - p->start;
    if (plen < 0) {
        return on_error(sp, JSER_ERR_UNKNOWN);
    }

    switch (p->type) {
    case JSMN_OBJECT:
        assert(p->size >= 0);
        assert((p->size * 2) <= (int)tokens);
        increment = dejsonify(sp, e->data.jser, e->used, p, p->size * 2u, json);
        if (increment < 0) {
            return -1;
        }
        break;
    case JSMN_ARRAY: {
        int i = 0, k = 0;
        assert(p->size >= 0);
        if (e->type != JSER_ARRAY_E) {
            return -1;
        }
        if (JSER_ENABLE_USED_SET) {
            e->used = 0;
        }
        // TODO: Fix this temporary hack
        //for (i = 1, k = 0; i < (p->size + 1); k++) {
        for (i = 1, k = 0; i < (int)tokens; k++) {
            assert(e->length < INT_MAX);
            if (k > (int)e->length) {
                return on_error(sp, JSER_ERR_SPACE);
            }

            const int r = json_to_element(sp, &e->data.jser[k], &p[i], tokens - i, json);
            if (r < 0) {
                return -1;
            }
            if (r == 0) {
                break;
            }
            i += r;
        }
        if (JSER_ENABLE_USED_SET) {
            e->used = k;
        }
        increment = i;
        break;
    }
    case JSMN_STRING: {
        if (e->type == JSER_BUFFER_E) {
            jser_buffer_t *buf = e->data.buf;
            buf->used = 0;
            size_t olen = buf->length;
            if (jser_base64_decode((unsigned char*)&json[p->start], plen, buf->buf, &olen)) {
                return on_error(sp, JSER_ERR_BASE64);
            }
            buf->used = olen;
            assert(buf->used <= buf->length);
        } else if (e->type == JSER_ASCIIZ_E) {
            if (e->is_array) {
                return on_error(sp, JSER_ERR_CONFIG);
            }
            if (e->length == 0) { /* A zero length ASCIIZ is invalid for deserialization, as this is a byte count */
                return on_error(sp, JSER_ERR_TYPE); /* cannot deserialize to an ASCIIZ string without a length */
            }
            if (plen >= (int)e->length) {
                return on_error(sp, JSER_ERR_TYPE);
            }
            memcpy(e->data.asciiz, &json[p->start], plen);
            e->data.asciiz[plen] = '\0';
        } else {
            return on_error(sp, JSER_ERR_TYPE);
        }
        break;
    }
    case JSMN_PRIMITIVE:
        switch (json[p->start]) {
        case 'n': /* 'null' not supported */
            return on_error(sp, JSER_ERR_TYPE);
        case 't':
            if (e->type != JSER_BOOL_E) {
                return on_error(sp, JSER_ERR_TYPE);
            }
            if (plen < 4 || memcmp(&json[p->start], "true", 4)) {
                return on_error(sp, JSER_ERR_TYPE);
            }
            *e->data.b = true;
            break;
        case 'f':
            if (e->type != JSER_BOOL_E) {
                return on_error(sp, JSER_ERR_TYPE);
            }
            if (plen < 5 || memcmp(&json[p->start], "false", 5)) {
                return on_error(sp, JSER_ERR_TYPE);
            }
            *e->data.b = false;
            break;
        case '-': case '0':
        case '1': case '2': case '3':
        case '4': case '5': case '6':
        case '7': case '8': case '9': {
            if (e->type == JSER_ULONG_E) {
                uint64_t ud = 0;
                if (str_to_u64(&json[p->start], plen, 10, &ud) < 0) {
                    return on_error(sp, JSER_ERR_NUMBER);
                }
                *e->data.lu = ud;
            } else if (e->type == JSER_LONG_E) {
                int64_t ld = 0;
                if (str_to_i64(&json[p->start], plen, 10, &ld) < 0) {
                    return on_error(sp, JSER_ERR_NUMBER);
                }
                *e->data.ld = ld;
            } else {
                return on_error(sp, JSER_ERR_TYPE);
            }
            break;
        }
        default:
            return on_error(sp, JSER_ERR_PARSE);
        }
        break;
    default:
        // TODO: Fix this temporary hack
        return 0;
        return on_error(sp, JSER_ERR_UNKNOWN);
    }
    return increment;
}

/* We need to be able to calculate the number of tokens to skip for the parser. */
static int distance(const jsmntok_t *t, const int ts)
{
    assert(t);
    assert(ts >= 0);
    int r = 1;
    if (ts == 0) {
        return -1;
    }
    switch (t->type) {
    case JSMN_STRING:
    case JSMN_PRIMITIVE:
        break;
    case JSMN_OBJECT:
        for (int i = 1; i < ts; i += 2) {
            if (t[i].type != JSMN_STRING) {
                return -1;
            }
            if ((i + 1) >= ts) {
                return -1;
            }
            if (t[i + 1].type == JSMN_OBJECT || t[i + 1].type == JSMN_ARRAY) {
                /* BUG: distance is not calculated correctly */
                const int st = distance(&t[i + 1], t[i + 1].size);
                if (st < 0) {
                    return -1;
                }
                i += st;
                r += st + 1;
            } else {
                r += 2;
            }
        }
        break;
    case JSMN_ARRAY:
        for (int i = 1; i < ts; i++) {
            if (t[i].type == JSMN_OBJECT || t[i].type == JSMN_ARRAY) {
                const int st = distance(&t[i + 1], t[i + 1].size);
                if (st < 0) {
                    return -1;
                }
                i += st;
                r += st;
            }
        }
        break;
    case JSMN_UNDEFINED:
    default:
        return -1;
    }
    return r;
}

static int dejsonify(jser_opts_t *sp, jser_t *j, const size_t jlen, jsmntok_t*token, const size_t tokens, const char *json)
{
    assert(sp);
    assert(j);
    assert(token);

    if (token->type == JSMN_UNDEFINED) {
        return 0; /* End of Input */
    }
    if (token->type != JSMN_OBJECT && token->type != JSMN_ARRAY) {
        return on_error(sp, JSER_ERR_PARSE);
    }
    size_t i = 0;
    for (i = 1; i < tokens; i++) {
        jsmntok_t *t = &token[i];

        switch (t->type) {
        case JSMN_UNDEFINED:
            return 0; /* done */
        case JSMN_STRING: {
            jsmntok_t *p = &token[i + 1];
            const int element = find_element(j, jlen, json, t);
            if (element < 0) { /* value not found, skip next tokens */
                if ((i + 1) >= tokens) {
                    return JSER_ERR_LENGTH;
                }
                assert((i + 1) < tokens);
                assert(token[i + 1].type != JSMN_UNDEFINED);
                i += distance(t, t->size);
                break;
            }
            jser_t *e = &j[element];
            const int increment = json_to_element(sp, e, p, tokens - i, json);
            if (increment < 1) {
                return on_error(sp, JSER_ERR_UNKNOWN);
            }
            i += increment;
            break;
        }
        case JSMN_OBJECT: case JSMN_ARRAY:  case JSMN_PRIMITIVE: /* none of these values can be an attribute, only strings */
        default:
            return on_error(sp, JSER_ERR_UNKNOWN);
        }
    }

    assert(i < INT_MAX);
    return i;
}

int jser_deserialize_from_buffer(jser_t *j, size_t jlen, jsmntok_t *t, const size_t tokens, jser_buffer_t *b)
{
    assert(j);
    assert(t);
    assert(b);
    assert(tokens <= UINT_MAX);
    jser_opts_t sp = { .max = JSER_MAX_DEPTH, };
    jsmn_parser jp = { 0, 0, 0 };
    jsmn_init(&jp);
    memset(t, 0, sizeof (*t) * tokens);
    const int rv = jsmn_parse(&jp, (const char *)b->buf, b->used, t, tokens);
    if (rv < 0)
        switch (rv) {
        case JSMN_ERROR_NOMEM: return JSER_ERR_SPACE;
        case JSMN_ERROR_INVAL: return JSER_ERR_PARSE;
        case JSMN_ERROR_PART:  return JSER_ERR_MORE_DAT;
        default:               return JSER_ERR_UNKNOWN;
        }
    const int r = dejsonify(&sp, j, jlen, t, tokens, (char *)(b->buf));
    if (sp.error < 0) {
        return sp.error;
    }
    return r;
}

int jser_deserialize_from_asciiz(jser_t *j, size_t jlen, jsmntok_t *t, const size_t tokens, const char *asciiz)
{
    assert(j);
    assert(t);
    assert(asciiz);
    const size_t length = strlen(asciiz);
    jser_buffer_t b = {
        .length = length,
        .used   = length,
        .buf    = (unsigned char *)asciiz,
    };
    return jser_deserialize_from_buffer(j, jlen, t, tokens, &b);
}

/* ~~~ Node retrieval and Tree Walking ~~~ */

static long copy(const jser_t *src, size_t slen, jser_t *pool, size_t plen)
{
    assert(src);
    assert(pool);
    size_t k = 0;
    for (size_t i = 0; i < slen; i++) {
        const jser_t *s = &src[i];
        if (k >= plen) {
            return -1;
        }
        pool[k] = *s;
        switch (s->type) {
        case JSER_OBJECT_E:
        case JSER_ARRAY_E: {
            const long st = copy(s->data.jser, s->used, &pool[k], plen);
            if (st < 0) {
                return -1;
            }
            if (k < k + st) { /* overflow */
                return -1;
            }
            if (st != 0) {
                pool[k].data.jser = &pool[k + 1];
            }
            k += st;
            break;
        }
        default:
            k++;
            break;
        }
    }
    return k;
}

int jser_copy(const jser_t *src, const size_t slen, jser_t *pool, size_t *plen)
{
    assert(src);
    assert(pool);
    assert(plen);
    const long st = copy(src, slen, pool, *plen);
    if (st < 0) {
        *plen = 0;
        return -1;
    }
    *plen = st;
    return 0;
}

int jser_walk_tree(const jser_t *j, size_t jlen, int (*fn)(const jser_t *e, void *param), void *param)
{
    assert(j);
    assert(fn);
    for (size_t i = 0; i < jlen; i++) {
        const jser_t *e = &j[i];
        switch (e->type) {
        case JSER_OBJECT_E:
        case JSER_ARRAY_E:
            if (fn(e, param) < 0) {
                return -1;
            }
            if (jser_walk_tree(e->data.jser, e->used, fn, param) < 0) {
                return -1;
            }
            break;
        default:
            if (fn(e, param) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

typedef struct {
    size_t nodes;
} counter_t;

static int fn_walk_tree_count(const jser_t *e, void *param)
{
    assert(e);
    assert(param);
    ((counter_t *)param)->nodes++;
    return 0;
}

int jser_node_count(const jser_t *j, size_t *jlen)
{
    assert(j);
    assert(jlen);
    counter_t c = { .nodes = 0 };
    const int r = jser_walk_tree(j, *jlen, fn_walk_tree_count, &c);
    if (r < 0) {
        *jlen = 0;
        return -1;
    }
    *jlen = c.nodes;
    return 0;
}

static int jser_node_finder(jser_opts_t *sp, const jser_t *j, size_t jlen, jser_t **const found, const char *path, size_t depth)
{
    assert(j);
    assert(found);
    assert(path);

    if (sp->max != 0 && depth > JSER_MAX_DEPTH) {
        return on_error(sp, JSER_ERR_DEPTH);
    }

    *found = NULL;
    if (*path == '\0') {
        return 0;
    }
    while (*path == '/') {
        path++;
    }

    const char *end = strchr(path, '/');
    if (end == NULL) {
        end = path + strlen(path);
    }
    const size_t nl = end - path;
    for (size_t i = 0; i < jlen; i++) {
        const jser_t *e = &j[i];
        const char *attr = e->attr;
        const size_t al = strlen(attr);
        if (al != nl) {
            continue;
        }
        if (memcmp(attr, path, al)) {
            continue;
        }
        if (*end == '\0') {
            *found = (jser_t *)e;
            return 1;
        }
        if (e->type != JSER_OBJECT_E) {
            return 0;
        }
        /* NB. We could retrieve an array element by processing
         * a node name as a number.  */
        return jser_node_finder(sp, e->data.jser, e->used, found, end + 1, depth + 1);
    }
    return 0;
}

int jser_retrieve_node(const jser_t *j, const size_t jlen, jser_t **found, const char *path)
{
    assert(j);
    assert(found);
    assert(path);
    jser_opts_t sp = {
        .max     = JSER_MAX_DEPTH,
    };
    return jser_node_finder(&sp, j, jlen, found, path, 0);
}

/* ~~~ Tests ~~~ */

/* NB. Might want to export this function under a different name */
static inline int test_json_element(const jser_t *j, const size_t jlen, const int pretty, char *asciiz, size_t asciiz_length)
{
    assert(j);
    assert(asciiz);
    jser_opts_t sp = {
        .pretty  = boolify(pretty),
        .max     = JSER_MAX_DEPTH,
        .dry_run = 0,
    };
    jser_buffer_t buf = {
        .used   = 0,
        .length = asciiz_length,
        .buf    = (unsigned char *)asciiz,
    };
    return jsonify(&sp, j, jlen, &buf, 0, 0);
}

static inline int test_json_length(jser_t *j, const size_t jlen, const int pretty, const char *expected)
{
    assert(j);
    assert(expected);
    size_t sz = 0;
    if (jser_serialized_length(j, jlen, pretty, &sz) < 0) {
        return -1;
    }
    const size_t elen = strlen(expected);
    if (sz != elen) {
        return -1;
    }
    return 0;
}

static inline int test_json_serializer(jser_t *j, size_t jlen, const int pretty, const char *expected)
{
    assert(j);
    assert(expected);
    char t[512] = { 0 };
    if (strlen(expected) > ((sizeof t) - 1)) {
        return -1;
    }
    if (test_json_element(j, jlen, pretty, t, sizeof t) < 0) {
        return -2;
    }
    return strcmp(expected, t) ? -3 : 0;
}

static inline int test_json_serialization(void)
{
    int r = 0;

    typedef struct {
        jser_t *element;
        const char *expect;
        int pretty;
    } test_t;

    jser_long_t l1 = 123, l2 = 0, l3 = -123;
    char str1[] = "HELLO", str2[] = "";
    jser_buffer_t buf1 = { .length = sizeof str1, .used = sizeof str1, .buf = (unsigned char *)str1};
    jser_buffer_t buf2 = { .length = 0, .used = 0, .buf = NULL };

    test_t tests[] = {
        {  &(jser_t)MK_LONG(l1),     "{\"l1\":123}",             0,  },
        {  &(jser_t)MK_LONG(l2),     "{\"l2\":0}",               0,  },
        {  &(jser_t)MK_LONG(l3),     "{\"l3\":-123}",            0,  },
        {  &(jser_t)MK_BUF(buf1),    "{\"buf1\":\"SEVMTE8A\"}",  0,  },
        {  &(jser_t)MK_BUF(buf2),    "{\"buf2\":\"\"}",          0,  },
        {  &(jser_t)MK_ASCIIZ(str2), "{\"str2\":\"\"}",          0,  },
        {  &(jser_t)MK_ASCIIZ(str1), "{\"str1\":\"HELLO\"}",     0,  },
    };

    for (size_t i = 0; i < ELEMENTS(tests); i++) {
        test_t *t = &tests[i];
        jser_t *ele = t->element;
        const char *exp = t->expect;
        if (test_json_serializer(ele, 1, t->pretty, exp) < 0) {
            r = -1;
        }
        if (test_json_length(ele, 1, t->pretty, exp) < 0) {
            r = -1;
        }
    }

    /* some minimal path retrieval tests as well...*/
    jser_t *found = NULL;
    if (jser_retrieve_node(tests[0].element, 1, &found, "l1") != 1) {
        return -1;
    }

    if (jser_retrieve_node(tests[0].element, 1, &found, "l2") == 1) {
        return -1;
    }

    return r;
}

static inline int test_json_deserialization(void)
{

    jser_long_t a = 0, b = 0, c = 0;

    jser_t j[] = { MK_LONG(a), MK_LONG(b), MK_LONG(c), };

    jsmntok_t t[16];

    static const char *i1 = "{\"a\":1,\"b\":2,\"c\":3}";
    if (jser_deserialize_from_asciiz(j, ELEMENTS(j), t, ELEMENTS(t), i1) < 0) {
        return -1;
    }

    if (a != 1 || b != 2 || c != 3) {
        return -1;
    }

    static const char *i2 = "{}";
    if (jser_deserialize_from_asciiz(j, ELEMENTS(j), t, ELEMENTS(t), i2) < 0) {
        return -1;
    }

    if (a != 1 || b != 2 || c != 3) {
        return -1;
    }

    static const char *i3 = "{\"a\":4"; /* an invalid string */
    if (jser_deserialize_from_asciiz(j, ELEMENTS(j), t, ELEMENTS(t), i3) == 0) { /* should fail */
        return -1;
    }

    if (a != 1 || b != 2 || c != 3) { /* variables are untouched */
        return -1;
    }

    static const char *i4 = "{\"a\":4}";
    if (jser_deserialize_from_asciiz(j, ELEMENTS(j), t, ELEMENTS(t), i4) < 0) {
        return -1;
    }
    if (a != 4 || b != 2 || c != 3) { /* only 'a' has been changed */
        return -1;
    }

    return 0;
}

static inline int test_jser_complex(void)
{
    jser_long_t l1 = 123, l2 = -456, l3 = -1;
    jser_ulong_t lu1 = 123, lu2 = 456, ul3 = 0, ul4 = 999;

    char str1[] = "HI";
    char str2[] = "BYE";
    char str3[] = "ABC";
    char str4[] = "A\tB\n\rC\\  \" escaped";

    bool b1 = true, b2 = false, b3 = false;
    unsigned char buffer[512] = { 0 };
    jser_buffer_t b = { .length = sizeof buffer, .used = 0, .buf = buffer, };

    jser_t jnested[] = {
        MK_ULONG(ul3),
        MK_ULONG(ul4),
        {  .attr  =  "l2",    .type  =  JSER_LONG_E,    .data.ld      =  &l3   },
        MK_ASCIIZ(str3),
    };

    jser_t jarray[] = { MK_ULONG(lu1), MK_ULONG(lu2), MK_LONG(l2), MK_ASCIIZ(str3), };

    unsigned char bstr1[] = "HELLO";
    jser_buffer_t buf1 = { .used = sizeof (bstr1) - 1, .length = sizeof (bstr1), .buf = bstr1, };

    jser_t js[] = {
        MK_ULONG(lu1),
        {  .attr  =  "lu2",   .type  =  JSER_ULONG_E,   .data.lu      =  &lu2      },
        {  .attr  =  "ld1",   .type  =  JSER_LONG_E,    .data.ld      =  &l1       },
        {  .attr  =  "ld2",   .type  =  JSER_LONG_E,    .data.ld      =  &l2       },
        {  .attr  =  "j1",    .type  =  JSER_OBJECT_E,  .data.jser    =  jnested,  .length  =  ELEMENTS(jnested),  .used  =  ELEMENTS(jnested)  },
        {  .attr  =  "s1",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  str1      },
        {  .attr  =  "s2",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  str2      },
        {  .attr  =  "a1",    .type  =  JSER_ARRAY_E,   .data.array   =  jarray,   .length  =  ELEMENTS(jarray),   .used  =  ELEMENTS(jarray),  },
        {  .attr  =  "b1",    .type  =  JSER_BOOL_E,    .data.b       =  &b1       },
        {  .attr  =  "b2",    .type  =  JSER_BOOL_E,    .data.b       =  &b2       },
        {  .attr  =  "b3",    .type  =  JSER_BOOL_E,    .data.b       =  &b3       },
        {  .attr  =  "s4",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  str4      },
        {  .attr  =  "buf1",  .type  =  JSER_BUFFER_E,  .data.buf     =  &buf1     },
    };

    size_t nodes = ELEMENTS(js);
    if (jser_node_count(js, &nodes) < 0) {
        return -1;
    }

    if (nodes != (ELEMENTS(js) + ELEMENTS(jarray) + ELEMENTS(jnested))) {
        return -1;
    }

    if (jser_serialize_to_buffer(js, ELEMENTS(js), 0, &b) < 0) {
        return -1;
    }

    static const char result[] = "{\"lu1\":123,\"lu2\":456,\"\
ld1\":123,\"ld2\":-456,\"j1\":{\"ul3\":0,\"ul4\":999,\"\
l2\":-1,\"str3\":\"ABC\"},\"s1\":\"HI\",\"s2\":\"BYE\",\
\"a1\":[123,456,-456,\"ABC\"],\"b1\":true,\"b2\":false,\
\"b3\":false,\"s4\":\"A\\tB\\n\\rC\\\\  \\\" escaped\",\
\"buf1\":\"SEVMTE8=\"}";

    if (memcmp(result, buffer, sizeof (result) - 1)) {
        return -1;
    }

    static const char *new_data = "{\n\
	\"lu1\" : 888,\n\
	\"lu2\" : 111,\n\
	\"not-found\"  : { \"ul3\" : 222 }\n\
	\"also-not-found\" : 111,\n\
	\"j1\"  : { \"ul3\" : 222 }\n\
}";

    jsmntok_t tokens[16] = { { 0 }, };
    if (jser_deserialize_from_asciiz(js, ELEMENTS(js), tokens, ELEMENTS(tokens), new_data) < 0) {
        return -1;
    }

    if (lu1 != 888  || lu2 != 111 || ul3 != 222) {
        return -1;
    }

    jser_t *found = NULL;
    const int st = jser_retrieve_node(js, ELEMENTS(js), &found, "j1/ul3");
    if (st != 1) {
        return -1;
    }

    return 0;
}

int jser_tests(void)
{
	if (JSER_ENABLE_TESTS) {
		test_json_serialization();
		test_json_deserialization();
		test_jser_complex();
	}
	return 0;
}

