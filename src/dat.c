/*
 * DAT text file decoding and encoding.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "dat.h"

#include "fs.h"
#include "tables.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE __attribute__((format(printf, 2, 3)))
#else
#define PRINTF_LIKE
#endif

// control codes
#define CC_END        0x0000
#define CC_INSERT     0x0002
#define CC_INSERT_END 0x0003
#define CC_NAME_OPEN  0x0004
#define CC_NAME_CLOSE 0x0005
#define CC_WAIT       0x0007
#define CC_NEWLINE    0x000A
#define CC_SCROLL     0x000C
#define CC_CURSOR     0x000D
#define CC_SE         0x0018
#define INS_POKEMON   0x0702
#define CMD_ICON      0xFF0C
#define CMD_VOICE     0xFF0D
#define CMD_FRAME     0xFF10
#define CMD_ENDIF     0xFF20
#define CMD_IF        0xFF27
#define CMD_DIC       0xFFF4
#define CMD_SET_FLAG  0xFFF5

#define NAME_OF(t, v)            find_name(t, COUNT_OF(t), v)
#define VALUE_OF(t, s, len, out) find_value(t, COUNT_OF(t), s, len, out)

/*
 * Growable UTF-8 string, flagged once memory runs out.
 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool failed;
} StrBuf;

/*
 * Growable array of 16-bit codes, flagged once memory runs out.
 */
typedef struct {
    uint16_t *data;
    size_t len;
    size_t cap;
    bool failed;
} CodeBuf;

/*
 * Read a 16-bit little-endian integer.
 */
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/*
 * Read a 32-bit little-endian integer.
 */
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16
        | (uint32_t)p[3] << 24;
}

/*
 * Write a 16-bit little-endian integer.
 */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/*
 * Write a 32-bit little-endian integer.
 */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * Decode one UTF-8 sequence. Return its length, or 0 if it is invalid.
 */
static size_t utf8_decode(const char *s, uint32_t *outCp)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t len;
    uint32_t cp;

    if (p[0] < 0x80) {
        *outCp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        len = 2;
        cp = p[0] & 0x1F;
    } else if ((p[0] & 0xF0) == 0xE0) {
        len = 3;
        cp = p[0] & 0x0F;
    } else if ((p[0] & 0xF8) == 0xF0) {
        len = 4;
        cp = p[0] & 0x07;
    } else {
        return 0;
    }

    // continuation bytes, the null terminator stopping a truncated sequence
    for (size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = cp << 6 | (p[i] & 0x3F);
    }
    *outCp = cp;
    return len;
}

/*
 * Encode a code point as UTF-8. Return the number of bytes written.
 */
static size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/*
 * Append 'n' bytes to a string.
 */
static void sb_put(StrBuf *b, const char *s, size_t n)
{
    if (b->failed) {
        return;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *grown = realloc(b->data, cap);
        if (!grown) {
            b->failed = true;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/*
 * Append a null-terminated string to a string.
 */
static void sb_puts(StrBuf *b, const char *s)
{
    sb_put(b, s, strlen(s));
}

/*
 * Append formatted text, short enough for markup, to a string.
 */
PRINTF_LIKE static void sb_printf(StrBuf *b, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_put(b, tmp, n < (int)sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

/*
 * Append a code to an array.
 */
static void code_push(CodeBuf *b, uint16_t v)
{
    if (b->failed) {
        return;
    }
    if (b->len == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        uint16_t *grown = realloc(b->data, cap * sizeof(*b->data));
        if (!grown) {
            b->failed = true;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
    b->data[b->len++] = v;
}

/*
 * Return the name of a value in a table, or NULL.
 */
static const char *find_name(const NamedValue *t, size_t n, uint16_t value)
{
    for (size_t i = 0; i < n; ++i) {
        if (t[i].value == value) {
            return t[i].name;
        }
    }
    return NULL;
}

/*
 * Look up the value of the 'len' bytes of name 's' in a table.
 */
static bool find_value(
    const NamedValue *t, size_t n, const char *s, size_t len, uint16_t *out)
{
    for (size_t i = 0; i < n; ++i) {
        if (strlen(t[i].name) == len && !memcmp(t[i].name, s, len)) {
            *out = t[i].value;
            return true;
        }
    }
    return false;
}

/*
 * Check whether nothing but the terminator and its padding is left.
 */
static bool only_zeros(const uint16_t *u, size_t i, size_t n)
{
    for (; i < n; ++i) {
        if (u[i] != CC_END) {
            return false;
        }
    }
    return true;
}

/*
 * Append a flag, by its name if known.
 */
static void put_flag(StrBuf *b, uint16_t flag)
{
    const char *name = NAME_OF(flags, flag);
    if (name) {
        sb_puts(b, name);
    } else {
        sb_printf(b, "FLAG#%u", flag);
    }
}

/*
 * Append a value compared against or given to a flag, by its name if known.
 */
static void put_value(StrBuf *b, uint16_t flag, uint16_t value)
{
    const char *name
        = flag == FLAG_MSG_SEQUENCE ? NAME_OF(constants, value) : NULL;
    if (name) {
        sb_puts(b, name);
    } else {
        sb_printf(b, "%u", value);
    }
}

/*
 * Decode the 'n' codes of one string, its trailing terminator and padding
 * dropped. Return NULL if memory runs out.
 */
static char *decode_string(const uint16_t *u, size_t n, bool japanese)
{
    const uint32_t *chars = japanese ? japaneseChars : latinChars;
    StrBuf b = { 0 };
    uint16_t flag = 0; // flag of the last condition, naming its values
    size_t i = 0;

    sb_puts(&b, ""); // an empty string is still allocated

    while (i < n) {
        const uint16_t c = u[i];
        const size_t left = n - i - 1; // codes after this one
        const char *name;

        if (c == CC_END && only_zeros(u, i, n)) {
            break;
        }

        if (c == CC_NEWLINE) {
            sb_puts(&b, "\n");
            ++i;
            continue;
        }

        // the newline after a page break is implied, as in the original scripts
        if ((c == CC_CURSOR && left >= 1 && u[i + 1] == CC_SCROLL)
            || c == CC_SCROLL) {
            sb_puts(&b, c == CC_CURSOR ? "<$>" : "<*>");
            i += c == CC_CURSOR ? 2 : 1;
            if (!only_zeros(u, i, n)) {
                sb_puts(&b, "\n");
            }
            continue;
        }

        if (c == CC_NAME_OPEN || c == CC_NAME_CLOSE || c == CC_SE) {
            sb_puts(&b,
                c == CC_NAME_OPEN        ? "{"
                    : c == CC_NAME_CLOSE ? "}"
                                         : "<@>");
            ++i;
            continue;
        }

        // 0002 <type> [arguments] 0003, the low byte of type being their count
        if (c == CC_INSERT && left >= 2) {
            const uint16_t type = u[i + 1];
            const size_t len = type >> 8 == 0xFF ? 1 : (type & 0xFF);

            if (len >= 1 && left >= len + 1
                && u[i + 1 + len] == CC_INSERT_END) {
                if (len == 1 && (name = NAME_OF(inserts, type))) {
                    sb_printf(&b, "<%s>", name);
                    i += 3;
                    continue;
                }
                if (len == 2 && type == INS_POKEMON) {
                    const uint16_t s = u[i + 2];
                    if (s < POKEMON_COUNT && pokemonNames[s]) {
                        sb_printf(&b, "<poke%s>", pokemonNames[s]);
                    } else {
                        sb_printf(&b, "<poke#%u>", s);
                    }
                    i += 4;
                    continue;
                }
            }
        }

        if ((name = NAME_OF(commands, c))) {
            sb_printf(&b, "<%s>", name);
            ++i;
            continue;
        }

        if (c == CMD_ENDIF) {
            sb_puts(&b, "[/]");
            ++i;
            continue;
        }

        // commands with one argument, or more
        if (left >= 1) {
            const uint16_t a = u[i + 1];
            bool done = true;

            if (c == CC_WAIT) {
                sb_printf(&b, "<wf%u>", a);
            } else if (c == CMD_ICON) {
                if ((name = NAME_OF(icons, a))) {
                    sb_printf(&b, "<%s>", name);
                } else {
                    sb_printf(&b, "<icon#%u>", a);
                }
            } else if (c == CMD_VOICE) {
                if ((name = NAME_OF(voices, a))) {
                    sb_printf(&b, "<pv%s>", name);
                } else {
                    sb_printf(&b, "<pv#%u>", a);
                }
            } else if (c == CMD_FRAME) {
                if ((name = NAME_OF(frames, a))) {
                    sb_printf(&b, "<%s>", name);
                } else {
                    sb_printf(&b, "<frame#%u>", a);
                }
            } else if (c == CMD_DIC) {
                sb_printf(&b, "<dicMSG_%03u>", a);
            } else if ((name = NAME_OF(operators, c))) {
                sb_printf(&b, "[ %s ", name);
                put_value(&b, flag, a);
                sb_puts(&b, "]");
            } else if (c == CMD_IF) {
                flag = a;
                sb_puts(&b, "[");
                put_flag(&b, flag);
                // FF27 <flag> is followed by its first comparison
                if (left >= 3 && (name = NAME_OF(operators, u[i + 2]))) {
                    sb_printf(&b, " %s ", name);
                    put_value(&b, flag, u[i + 3]);
                    i += 2;
                }
                sb_puts(&b, "]");
            } else if (c == CMD_SET_FLAG && left >= 2) {
                sb_puts(&b, "<");
                put_flag(&b, a);
                sb_puts(&b, " ");
                put_value(&b, a, u[i + 2]);
                sb_puts(&b, ">");
                ++i;
            } else {
                done = false;
            }

            if (done) {
                i += 2;
                continue;
            }
        }

        if ((name = NAME_OF(superscripts, c))) {
            sb_printf(&b, "<%s>", name);
            ++i;
            continue;
        }

        if (c >= CHAR_FIRST && c < CHAR_FIRST + CHAR_COUNT
            && chars[c - CHAR_FIRST]) {
            const uint32_t cp = chars[c - CHAR_FIRST];
            char tmp[4];
            // literal markup characters are written as <x>
            if (cp < 0x80 && strchr("<>[]{}", (int)cp)) {
                sb_printf(&b, "<%c>", (char)cp);
            } else {
                sb_put(&b, tmp, utf8_encode(cp, tmp));
            }
            ++i;
            continue;
        }

        // any other code is kept as a number
        sb_printf(&b, "<0x%04X>", c);
        ++i;
    }

    if (b.failed) {
        free(b.data);
        return NULL;
    }
    return b.data;
}

/*
 * Parse a decimal or 0x-prefixed hexadecimal 16-bit number.
 */
static bool parse_number(const char *s, size_t len, uint16_t *out)
{
    uint32_t base = 10;
    uint32_t v = 0;

    if (len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        len -= 2;
    }
    if (len == 0) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return false;
        }
        v = v * base + d;
        if (v > 0xFFFF) {
            return false;
        }
    }

    *out = (uint16_t)v;
    return true;
}

/*
 * Parse a flag, by its name, as FLAG#number, or as a number.
 */
static bool parse_flag(const char *s, size_t len, uint16_t *out)
{
    if (len > 5 && !memcmp(s, "FLAG#", 5)) {
        return parse_number(s + 5, len - 5, out);
    }
    return VALUE_OF(flags, s, len, out) || parse_number(s, len, out);
}

/*
 * Parse a value, by its name or as a number.
 */
static bool parse_value(const char *s, size_t len, uint16_t *out)
{
    return VALUE_OF(constants, s, len, out) || parse_number(s, len, out);
}

/*
 * Check whether the 'len' bytes of 's' start with 'prefix'.
 */
static bool has_prefix(const char *s, size_t len, const char *prefix)
{
    size_t n = strlen(prefix);
    return len >= n && !memcmp(s, prefix, n);
}

/*
 * Encode the contents of a <...> tag. '*skipNewline' is set after a page
 * break, whose newline is implied.
 */
static bool encode_tag(const char *t, size_t len, CodeBuf *o, bool *skipNewline)
{
    uint16_t v;
    uint16_t w;

    if (len == 1) {
        switch (t[0]) {
        case '$':
            code_push(o, CC_CURSOR);
            code_push(o, CC_SCROLL);
            *skipNewline = true;
            return true;
        case '*':
            code_push(o, CC_SCROLL);
            *skipNewline = true;
            return true;
        case '@':
            code_push(o, CC_SE);
            return true;
        case '[': // literal markup characters
            code_push(o, 0x5B);
            return true;
        case ']':
            code_push(o, 0x5D);
            return true;
        case '{':
            code_push(o, 0x7B);
            return true;
        case '}':
            code_push(o, 0x7D);
            return true;
        }
    }

    if (VALUE_OF(inserts, t, len, &v)) {
        code_push(o, CC_INSERT);
        code_push(o, v);
        code_push(o, CC_INSERT_END);
        return true;
    }
    if (VALUE_OF(commands, t, len, &v) || VALUE_OF(superscripts, t, len, &v)) {
        code_push(o, v);
        return true;
    }
    if (VALUE_OF(icons, t, len, &v)
        || (has_prefix(t, len, "icon#") && parse_number(t + 5, len - 5, &v))) {
        code_push(o, CMD_ICON);
        code_push(o, v);
        return true;
    }
    if (VALUE_OF(frames, t, len, &v)
        || (has_prefix(t, len, "frame#") && parse_number(t + 6, len - 6, &v))) {
        code_push(o, CMD_FRAME);
        code_push(o, v);
        return true;
    }
    if (has_prefix(t, len, "0x") && parse_number(t, len, &v)) {
        code_push(o, v);
        return true;
    }
    if (has_prefix(t, len, "wf") && parse_number(t + 2, len - 2, &v)) {
        code_push(o, CC_WAIT);
        code_push(o, v);
        return true;
    }
    if (has_prefix(t, len, "dicMSG_") && parse_number(t + 7, len - 7, &v)) {
        code_push(o, CMD_DIC);
        code_push(o, v);
        return true;
    }
    if (has_prefix(t, len, "pv")) {
        const char *s = t + 2;
        size_t n = len - 2;
        if ((n > 1 && s[0] == '#' && parse_number(s + 1, n - 1, &v))
            || VALUE_OF(voices, s, n, &v)) {
            code_push(o, CMD_VOICE);
            code_push(o, v);
            return true;
        }
        return false;
    }
    if (has_prefix(t, len, "poke")) {
        const char *s = t + 4;
        size_t n = len - 4;
        bool found = n > 1 && s[0] == '#' && parse_number(s + 1, n - 1, &v);
        for (uint16_t i = 0; !found && i < POKEMON_COUNT; ++i) {
            if (pokemonNames[i] && strlen(pokemonNames[i]) == n
                && !memcmp(pokemonNames[i], s, n)) {
                v = i;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        code_push(o, CC_INSERT);
        code_push(o, INS_POKEMON);
        code_push(o, v);
        code_push(o, CC_INSERT_END);
        return true;
    }

    // <FLAG value> sets a flag
    if (has_prefix(t, len, "FLAG")) {
        const char *sp = memchr(t, ' ', len);
        if (!sp || !parse_flag(t, (size_t)(sp - t), &v)
            || !parse_value(sp + 1, len - (size_t)(sp + 1 - t), &w)) {
            return false;
        }
        code_push(o, CMD_SET_FLAG);
        code_push(o, v);
        code_push(o, w);
        return true;
    }

    return false;
}

/*
 * Skip spaces and return the length of the next space-delimited token.
 */
static size_t next_token(const char **s, const char *end)
{
    while (*s < end && **s == ' ') {
        ++*s;
    }
    const char *p = *s;
    while (p < end && *p != ' ') {
        ++p;
    }
    return (size_t)(p - *s);
}

/*
 * Encode the contents of a [...] condition: "/", "FLAG", "FLAG op value" or
 * " op value".
 */
static bool encode_condition(const char *t, size_t len, CodeBuf *o)
{
    const char *end = t + len;
    const char *s = t;
    uint16_t v;
    size_t n;

    if (len == 1 && t[0] == '/') {
        code_push(o, CMD_ENDIF);
        return true;
    }

    n = next_token(&s, end);
    if (n == 0) {
        return false;
    }

    // a flag first, unless the condition is a further comparison
    if (!VALUE_OF(operators, s, n, &v)) {
        if (!parse_flag(s, n, &v)) {
            return false;
        }
        code_push(o, CMD_IF);
        code_push(o, v);
        s += n;
        n = next_token(&s, end);
        if (n == 0) {
            return true;
        }
        if (!VALUE_OF(operators, s, n, &v)) {
            return false;
        }
    }

    code_push(o, v);
    s += n;
    n = next_token(&s, end);
    if (n == 0 || !parse_value(s, n, &v)) {
        return false;
    }
    code_push(o, v);
    s += n;
    return next_token(&s, end) == 0;
}

/*
 * Encode one string without its terminator. Return NULL, or the text that
 * cannot be encoded.
 */
static const char *encode_string(const char *s, bool japanese, CodeBuf *o)
{
    const uint32_t *chars = japanese ? japaneseChars : latinChars;
    bool skipNewline = false;

    while (*s) {
        const char *start = s;

        if (*s == '\n') {
            if (!skipNewline) {
                code_push(o, CC_NEWLINE);
            }
            skipNewline = false;
            ++s;
            continue;
        }
        skipNewline = false;

        if (*s == '{' || *s == '}') {
            code_push(o, *s == '{' ? CC_NAME_OPEN : CC_NAME_CLOSE);
            ++s;
            continue;
        }

        // literal '<' and '>'
        if (!strncmp(s, "<<>", 3) || !strncmp(s, "<>>", 3)) {
            code_push(o, s[1] == '<' ? 0x3C : 0x3E);
            s += 3;
            continue;
        }

        if (*s == '<' || *s == '[') {
            const char *close = strchr(s + 1, *s == '<' ? '>' : ']');
            if (!close) {
                return start;
            }
            const size_t len = (size_t)(close - s - 1);
            const bool ok = *s == '<' ? encode_tag(s + 1, len, o, &skipNewline)
                                      : encode_condition(s + 1, len, o);
            if (!ok) {
                return start;
            }
            s = close + 1;
            continue;
        }

        // any other character must have a glyph in the font
        uint32_t cp;
        size_t n = utf8_decode(s, &cp);
        if (!n) {
            return start;
        }
        size_t c = 0;
        while (c < CHAR_COUNT && chars[c] != cp) {
            ++c;
        }
        if (c == CHAR_COUNT) {
            return start;
        }
        code_push(o, (uint16_t)(CHAR_FIRST + c));
        s += n;
    }

    return NULL;
}

/*
 * Return the number of strings listed by the offset table of a DAT file. An odd
 * count is padded with a zero offset, dropped here.
 */
static uint32_t string_count(const uint8_t *data)
{
    uint32_t count = get_u32(data) / 2;
    if (count > 1 && get_u16(data + 4 + (count - 1) * 2) == 0) {
        --count;
    }
    return count;
}

/*
 * Return the number of strings of a DAT file, or -1 if 'data' is not one.
 */
int64_t dat_check(const uint8_t *data, size_t size)
{
    if (size < 4) {
        return -1;
    }

    // u32 table size, padded to 4 bytes, then u16 offsets and u16 codes
    uint32_t tableSize = get_u32(data);
    if (tableSize % 4 || tableSize > size - 4 || (size - 4 - tableSize) % 2) {
        return -1;
    }

    /*
     * Offsets in 4-byte units, ascending from the first string. Each string
     * ends with a terminator padded to 4 bytes, which tells text files apart.
     */
    uint32_t count = string_count(data);
    if (count == 0 && size != 4) {
        return -1; // an empty text file is its header alone
    }
    const uint8_t *text = data + 4 + tableSize;
    size_t textSize = size - 4 - tableSize;
    for (uint32_t i = 0; i < count; ++i) {
        size_t start = (size_t)get_u16(data + 4 + i * 2) * 4;
        size_t end
            = i + 1 < count ? (size_t)get_u16(data + 6 + i * 2) * 4 : textSize;
        if ((i == 0 && start != 0) || end > textSize || end < start + 4
            || get_u16(text + end - 2) != CC_END) {
            return -1;
        }
    }
    return count;
}

/*
 * Decode string 'i' of a DAT file accepted by dat_check to UTF-8, its control
 * codes written as markup. Return NULL if memory runs out.
 */
char *dat_decode(const uint8_t *data, size_t size, uint32_t i, bool japanese)
{
    uint32_t tableSize = get_u32(data);
    const uint8_t *table = data + 4;
    const uint8_t *text = table + tableSize;
    size_t textSize = size - 4 - tableSize;

    // a string ends where the next one starts, the last one at the file end
    uint32_t count = string_count(data);
    size_t start = (size_t)get_u16(table + i * 2) * 4;
    size_t end
        = i + 1 < count ? (size_t)get_u16(table + i * 2 + 2) * 4 : textSize;

    size_t n = (end - start) / 2;
    uint16_t *u = malloc((n ? n : 1) * sizeof(*u));
    if (!u) {
        return NULL;
    }
    for (size_t j = 0; j < n; ++j) {
        u[j] = get_u16(text + start + j * 2);
    }
    char *s = decode_string(u, n, japanese);
    free(u);
    return s;
}

/*
 * Build a DAT file of 'count' strings encoded from UTF-8 and markup. Return
 * NULL on failure after printing the reason, 'name' naming the input.
 */
uint8_t *dat_build(const char *const *strings,
    uint32_t count,
    bool japanese,
    const char *name,
    size_t *outSize)
{
    CodeBuf text = { 0 };
    uint8_t *out = NULL;
    uint16_t *offsets = malloc((count ? count : 1) * sizeof(*offsets));
    if (!offsets) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        return NULL;
    }

    for (uint32_t i = 0; i < count; ++i) {
        // offsets are 16-bit counts of 4-byte units
        if (text.len / 2 > 0xFFFF) {
            fprintf(stderr,
                "%s: '%s': too much text, from string %03u on\n",
                programName,
                name,
                i);
            goto done;
        }
        offsets[i] = (uint16_t)(text.len / 2);

        const char *bad = encode_string(strings[i], japanese, &text);
        if (bad) {
            // show up to 24 bytes, stopping at a newline or a UTF-8 boundary
            int n = 0;
            while (n < 24 && bad[n] && bad[n] != '\n') {
                ++n;
            }
            while (n > 0 && bad[n] && (bad[n] & 0xC0) == 0x80) {
                --n;
            }
            fprintf(stderr,
                "%s: '%s': string %03u: cannot encode \"%.*s\"\n",
                programName,
                name,
                i,
                n,
                bad);
            goto done;
        }

        // terminator, then padding to a 4-byte boundary
        code_push(&text, CC_END);
        if (text.len % 2) {
            code_push(&text, CC_END);
        }
    }
    if (text.failed) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        goto done;
    }

    // the offset table is padded to 4 bytes too
    const size_t tableSize = ((size_t)count * 2 + 3) & ~(size_t)3;
    const size_t size = 4 + tableSize + text.len * 2;
    out = calloc(size, 1);
    if (!out) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        goto done;
    }
    put_u32(out, (uint32_t)tableSize);
    for (uint32_t i = 0; i < count; ++i) {
        put_u16(out + 4 + i * 2, offsets[i]);
    }
    for (size_t i = 0; i < text.len; ++i) {
        put_u16(out + 4 + tableSize + i * 2, text.data[i]);
    }
    *outSize = size;

done:
    free(text.data);
    free(offsets);
    return out;
}
