#include "textutil.h"

#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ascii_lower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static int encode_utf8(unsigned long cp, char *dst) {
    if (cp == 0 || cp > 0x10FFFFul) {
        dst[0] = '?';
        return 1;
    }
    if (cp < 0x80ul) {
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800ul) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000ul) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int known_entity(const char *name, size_t len, char *dst) {
    if (len == 3 && memcmp(name, "amp", 3) == 0) {
        dst[0] = '&';
        return 1;
    }
    if (len == 2 && memcmp(name, "lt", 2) == 0) {
        dst[0] = '<';
        return 1;
    }
    if (len == 2 && memcmp(name, "gt", 2) == 0) {
        dst[0] = '>';
        return 1;
    }
    if (len == 4 && memcmp(name, "quot", 4) == 0) {
        dst[0] = '"';
        return 1;
    }
    if ((len == 4 && memcmp(name, "apos", 4) == 0) || (len == 5 && memcmp(name, "#39", 3) == 0)) {
        dst[0] = '\'';
        return 1;
    }
    if (len == 4 && memcmp(name, "nbsp", 4) == 0) {
        dst[0] = ' ';
        return 1;
    }
    return 0;
}

static void buf_newline(FfBuf *buf) {
    if (buf->len == 0) {
        return;
    }
    if (buf->data[buf->len - 1] != '\n') {
        ff_buf_putc(buf, '\n');
    }
}

static int is_block_tag(const char *name) {
    return strcmp(name, "p") == 0 || strcmp(name, "div") == 0 || strcmp(name, "li") == 0 ||
           strcmp(name, "tr") == 0 || strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
           strcmp(name, "h3") == 0 || strcmp(name, "table") == 0 || strcmp(name, "ul") == 0 ||
           strcmp(name, "ol") == 0;
}

static void collapse_breaks(FfBuf *buf) {
    char *src;
    char *dst;
    int newlines = 0;
    int space = 1;
    if (!buf->data) {
        return;
    }
    src = buf->data;
    dst = buf->data;
    while (*src) {
        if (*src == '\n') {
            if (newlines < 2) {
                *dst++ = '\n';
                newlines++;
            }
            space = 1;
            src++;
            continue;
        }
        if (*src == ' ') {
            if (!space && newlines == 0) {
                *dst++ = ' ';
                space = 1;
            }
            src++;
            continue;
        }
        *dst++ = *src++;
        space = 0;
        newlines = 0;
    }
    while (dst > buf->data && (dst[-1] == '\n' || dst[-1] == ' ')) {
        dst--;
    }
    *dst = '\0';
    buf->len = (size_t)(dst - buf->data);
}

int ff_html_to_text(const char *html, char **out) {
    FfBuf buf;
    const unsigned char *p;
    int space = 1;
    if (!out) {
        return -1;
    }
    *out = NULL;
    if (!html) {
        *out = ff_strdup("");
        return *out ? 0 : -1;
    }
    if (ff_buf_init(&buf, 48 * 1024) != 0) {
        return -1;
    }
    p = (const unsigned char *)html;
    while (*p) {
        if (p[0] == '[' && strncmp((const char *)p, "[sound:", 7) == 0) {
            const char *end = strchr((const char *)p, ']');
            if (end) {
                p = (const unsigned char *)end + 1;
                continue;
            }
        }
        if (*p == '<') {
            const char *tag = (const char *)p + 1;
            int closing = 0;
            char name[32];
            int ni = 0;
            const char *gt;
            if (*tag == '/') {
                closing = 1;
                tag++;
            } else if (*tag == '!') {
                gt = strchr((const char *)p, '>');
                if (!gt) {
                    break;
                }
                p = (const unsigned char *)gt + 1;
                continue;
            }
            while (*tag && *tag != '>' && *tag != ' ' && *tag != '/' && *tag != '\n' && ni < 31) {
                name[ni++] = (char)ascii_lower((unsigned char)*tag);
                tag++;
            }
            name[ni] = '\0';
            gt = strchr((const char *)p, '>');
            if (!gt) {
                break;
            }
            if (!closing && (strcmp(name, "script") == 0 || strcmp(name, "style") == 0)) {
                char closer[16];
                const char *found;
                snprintf(closer, sizeof(closer), "</%s", name);
                found = strstr(gt + 1, closer);
                if (!found) {
                    break;
                }
                gt = strchr(found, '>');
                p = (const unsigned char *)(gt ? gt + 1 : found + strlen(closer));
                continue;
            }
            p = (const unsigned char *)gt + 1;
            if (strcmp(name, "br") == 0 || strcmp(name, "hr") == 0) {
                buf_newline(&buf);
                space = 1;
            } else if (closing && is_block_tag(name)) {
                buf_newline(&buf);
                space = 1;
            } else if (!closing && strcmp(name, "li") == 0) {
                buf_newline(&buf);
                ff_buf_puts(&buf, "- ");
                space = 0;
            }
            continue;
        }
        if (*p == '&') {
            const char *semi = strchr((const char *)p, ';');
            char encoded[4];
            int produced = 0;
            if (semi && semi - (const char *)p <= 12) {
                const char *body = (const char *)p + 1;
                size_t n = (size_t)(semi - body);
                if (body[0] == '#') {
                    unsigned long cp = 0;
                    if (body[1] == 'x' || body[1] == 'X') {
                        cp = strtoul(body + 2, NULL, 16);
                    } else {
                        cp = strtoul(body + 1, NULL, 10);
                    }
                    produced = encode_utf8(cp, encoded);
                } else {
                    produced = known_entity(body, n, encoded);
                }
                if (produced > 0) {
                    if (!(produced == 1 && encoded[0] == ' ' && space)) {
                        ff_buf_append(&buf, encoded, (size_t)produced);
                        space = (produced == 1 && encoded[0] == ' ');
                    }
                    p = (const unsigned char *)semi + 1;
                    continue;
                }
            }
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        if (*p == '\n') {
            buf_newline(&buf);
            space = 1;
            p++;
            continue;
        }
        if (*p == ' ' || *p == '\t') {
            if (!space) {
                ff_buf_putc(&buf, ' ');
                space = 1;
            }
            p++;
            continue;
        }
        ff_buf_putc(&buf, (char)*p);
        space = 0;
        p++;
    }
    collapse_breaks(&buf);
    *out = ff_buf_detach(&buf);
    return *out ? 0 : -1;
}

void ff_rendered_clear(FfRendered *rendered) {
    memset(rendered, 0, sizeof(*rendered));
}

void ff_rendered_free(FfRendered *rendered) {
    free(rendered->text);
    free(rendered->image);
    free(rendered->audio);
    ff_rendered_clear(rendered);
}

static int contains_token(const char *text, const char *token) {
    const char *p;
    size_t n;
    if (!text || !token) {
        return 0;
    }
    n = strlen(token);
    for (p = text; *p; p++) {
        size_t i = 0;
        while (i < n && ascii_lower((unsigned char)p[i]) == token[i]) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

int ff_note_has_image(const char *fields) {
    return contains_token(fields, "<img");
}

int ff_note_has_audio(const char *fields) {
    return contains_token(fields, "[sound:");
}

static int append_media_name(char **slot, int *count, int store_max, const char *name, size_t len) {
    char *next;
    size_t old = 0;
    (*count)++;
    if (*count > store_max || len == 0 || len > 500) {
        return 0;
    }
    if (len >= 7 && strncmp(name, "http://", 7) == 0) {
        return 0;
    }
    if (len >= 8 && strncmp(name, "https://", 8) == 0) {
        return 0;
    }
    if (*slot) {
        old = strlen(*slot);
    }
    next = (char *)malloc(old + 1 + len + 1);
    if (!next) {
        return -1;
    }
    if (*slot) {
        memcpy(next, *slot, old);
        next[old] = '\x1f';
        memcpy(next + old + 1, name, len);
        next[old + 1 + len] = '\0';
        free(*slot);
    } else {
        memcpy(next, name, len);
        next[len] = '\0';
    }
    *slot = next;
    return 0;
}

static void take_attr(const char *tag, const char *attr, char **slot, int *count, int store_max) {
    size_t an = strlen(attr);
    const char *p = tag;
    while (*p && *p != '>') {
        size_t i = 0;
        while (i < an && ascii_lower((unsigned char)p[i]) == attr[i]) {
            i++;
        }
        if (i == an) {
            const char *v = p + an;
            char quote;
            const char *end;
            while (*v == ' ') {
                v++;
            }
            if (*v != '=') {
                p++;
                continue;
            }
            v++;
            while (*v == ' ') {
                v++;
            }
            quote = *v;
            if (quote != '"' && quote != '\'') {
                return;
            }
            v++;
            end = strchr(v, quote);
            if (!end) {
                return;
            }
            append_media_name(slot, count, store_max, v, (size_t)(end - v));
            return;
        }
        p++;
    }
}

static void extract_media(const char *html, FfRendered *out) {
    const char *p = html ? html : "";
    while (*p) {
        if (strncmp(p, "[sound:", 7) == 0) {
            const char *end = strchr(p + 7, ']');
            if (end) {
                append_media_name(&out->audio, &out->audio_count, 4, p + 7, (size_t)(end - (p + 7)));
                p = end + 1;
                continue;
            }
        }
        if (*p == '<') {
            const char *gt = strchr(p, '>');
            int image = 0;
            size_t i;
            if (!gt) {
                break;
            }
            for (i = 1; p + i < gt && i < 6; i++) {
                char ch = (char)ascii_lower((unsigned char)p[i]);
                if (i == 1 && ch != 'i') {
                    break;
                }
                if (i == 2 && ch != 'm') {
                    break;
                }
                if (i == 3 && ch != 'g') {
                    break;
                }
                if (i == 4 && (ch == ' ' || ch == '\n' || ch == '/')) {
                    image = 1;
                    break;
                }
            }
            if (image) {
                char *saved = out->image;
                int saved_count = out->image_count;
                take_attr(p, "src", &out->image, &out->image_count, 1);
                (void)saved;
                (void)saved_count;
            }
            p = gt + 1;
            continue;
        }
        p++;
    }
}

static void trim_inplace(char *text) {
    char *start = text;
    char *end;
    size_t n;
    while (*start == ' ') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    n = strlen(text);
    end = text + n;
    while (end > text && end[-1] == ' ') {
        end--;
    }
    *end = '\0';
}

static const char *read_mustache(const char *p, char *inner, size_t inner_len) {
    const char *end;
    size_t n;
    if (p[0] != '{' || p[1] != '{') {
        return NULL;
    }
    end = strstr(p + 2, "}}");
    if (!end || inner_len < 2) {
        return NULL;
    }
    n = (size_t)(end - (p + 2));
    if (n >= inner_len) {
        n = inner_len - 1;
    }
    memcpy(inner, p + 2, n);
    inner[n] = '\0';
    trim_inplace(inner);
    return end + 2;
}

static const char *skip_section(const char *p) {
    int depth = 1;
    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            char inner[160];
            const char *next = read_mustache(p, inner, sizeof(inner));
            if (!next) {
                return p + 1;
            }
            if (inner[0] == '#' || inner[0] == '^') {
                depth++;
            } else if (inner[0] == '/') {
                depth--;
                if (depth == 0) {
                    return next;
                }
            }
            p = next;
            continue;
        }
        p++;
    }
    return p;
}

typedef struct RenderIn {
    char **fields;
    const char *const *names;
    int nfields;
    int cloze_ord;
    int reveal;
    const char *front_side;
} RenderIn;

static int field_index(const RenderIn *in, const char *name) {
    int i;
    for (i = 0; i < in->nfields; i++) {
        if (in->names && in->names[i] && strcmp(in->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *field_value(const RenderIn *in, int index) {
    if (index < 0 || index >= in->nfields || !in->fields || !in->fields[index]) {
        return "";
    }
    return in->fields[index];
}

static int field_nonempty(const RenderIn *in, const char *name) {
    return field_value(in, field_index(in, name))[0] != '\0';
}

static int append_cloze(FfBuf *buf, const char *src, int active, int reveal) {
    const char *p = src ? src : "";
    while (*p) {
        if (p[0] == '{' && p[1] == '{' && p[2] == 'c' && isdigit((unsigned char)p[3])) {
            const char *nptr = p + 3;
            int num = 0;
            while (isdigit((unsigned char)*nptr)) {
                num = num * 10 + (*nptr - '0');
                nptr++;
            }
            if (nptr[0] == ':' && nptr[1] == ':') {
                const char *body = nptr + 2;
                const char *end = strstr(body, "}}");
                const char *hint_at = NULL;
                const char *scan;
                if (!end) {
                    ff_buf_putc(buf, *p);
                    p++;
                    continue;
                }
                for (scan = body; scan + 1 < end; scan++) {
                    if (scan[0] == ':' && scan[1] == ':') {
                        hint_at = scan;
                        break;
                    }
                }
                if (num == active) {
                    if (reveal) {
                        size_t n = hint_at ? (size_t)(hint_at - body) : (size_t)(end - body);
                        ff_buf_append(buf, body, n);
                    } else if (hint_at) {
                        ff_buf_putc(buf, '[');
                        ff_buf_append(buf, hint_at + 2, (size_t)(end - (hint_at + 2)));
                        ff_buf_putc(buf, ']');
                    } else {
                        ff_buf_puts(buf, "[...]");
                    }
                } else if (hint_at) {
                    ff_buf_append(buf, body, (size_t)(hint_at - body));
                } else {
                    ff_buf_append(buf, body, (size_t)(end - body));
                }
                p = end + 2;
                continue;
            }
        }
        ff_buf_putc(buf, *p);
        p++;
    }
    return 0;
}

static const char *field_name_of(char *inner) {
    if (strncmp(inner, "cloze:", 6) == 0) {
        return inner + 6;
    }
    if (strncmp(inner, "type:", 5) == 0) {
        return inner + 5;
    }
    if (strncmp(inner, "hint:", 5) == 0) {
        return inner + 5;
    }
    if (strncmp(inner, "text:", 5) == 0) {
        return inner + 5;
    }
    return inner;
}

static int render_fmt(const char *fmt, const RenderIn *in, FfBuf *buf) {
    const char *p = fmt ? fmt : "";
    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            char inner[160];
            const char *next = read_mustache(p, inner, sizeof(inner));
            const char *fname;
            int cloze;
            if (!next) {
                ff_buf_putc(buf, *p);
                p++;
                continue;
            }
            if (inner[0] == '#' || inner[0] == '^') {
                int filled = field_nonempty(in, inner + 1);
                int show = inner[0] == '#' ? filled : !filled;
                trim_inplace(inner + 1);
                filled = field_nonempty(in, inner + 1);
                show = inner[0] == '#' ? filled : !filled;
                p = show ? next : skip_section(next);
                continue;
            }
            if (inner[0] == '/') {
                p = next;
                continue;
            }
            if (strcmp(inner, "FrontSide") == 0) {
                ff_buf_puts(buf, in->front_side ? in->front_side : "");
                p = next;
                continue;
            }
            cloze = strncmp(inner, "cloze:", 6) == 0;
            fname = field_name_of(inner);
            if (cloze) {
                FfBuf cloze_buf;
                if (ff_buf_init(&cloze_buf, buf->limit) == 0) {
                    append_cloze(&cloze_buf, field_value(in, field_index(in, fname)), in->cloze_ord, in->reveal);
                    ff_buf_puts(buf, cloze_buf.data);
                    ff_buf_free(&cloze_buf);
                }
            } else {
                ff_buf_puts(buf, field_value(in, field_index(in, fname)));
            }
            p = next;
            continue;
        }
        ff_buf_putc(buf, *p);
        p++;
    }
    return 0;
}

int ff_render_side(
    const char *fmt,
    char **fields,
    const char **names,
    int nfields,
    int cloze_ord,
    int reveal,
    const char *front_side,
    FfRendered *out) {
    FfBuf raw;
    RenderIn in;
    char *plain = NULL;
    if (!out) {
        return -1;
    }
    ff_rendered_clear(out);
    if (ff_buf_init(&raw, 48 * 1024) != 0) {
        return -1;
    }
    in.fields = fields;
    in.names = (const char *const *)names;
    in.nfields = nfields;
    in.cloze_ord = cloze_ord > 0 ? cloze_ord : 1;
    in.reveal = reveal;
    in.front_side = front_side;
    render_fmt(fmt, &in, &raw);
    extract_media(raw.data, out);
    if (ff_html_to_text(raw.data, &plain) != 0) {
        ff_buf_free(&raw);
        ff_rendered_free(out);
        return -1;
    }
    ff_buf_free(&raw);
    out->text = plain;
    return 0;
}

void ff_snippet(const char *text, char *dst, int dst_len) {
    int i = 0;
    int o = 0;
    if (!dst || dst_len <= 0) {
        return;
    }
    if (!text) {
        dst[0] = '\0';
        return;
    }
    while (text[i] && o + 1 < dst_len) {
        char ch = text[i++];
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
        if (ch == ' ' && (o == 0 || dst[o - 1] == ' ')) {
            continue;
        }
        dst[o++] = ch;
    }
    while (o > 0 && dst[o - 1] == ' ') {
        o--;
    }
    dst[o] = '\0';
}
