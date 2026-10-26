#include "util.h"

#include <stdlib.h>
#include <string.h>

int ff_buf_init(FfBuf *buf, size_t limit) {
    memset(buf, 0, sizeof(*buf));
    buf->limit = limit ? limit : 65536;
    buf->cap = 64;
    if (buf->cap > buf->limit) {
        buf->cap = buf->limit;
    }
    buf->data = (char *)malloc(buf->cap + 1);
    if (!buf->data) {
        return -1;
    }
    buf->data[0] = '\0';
    return 0;
}

void ff_buf_free(FfBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void ff_buf_clear(FfBuf *buf) {
    buf->len = 0;
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

static int buf_reserve(FfBuf *buf, size_t need) {
    size_t cap;
    char *grown;
    if (need <= buf->cap) {
        return 0;
    }
    cap = buf->cap ? buf->cap : 64;
    while (cap < need) {
        if (cap > (SIZE_MAX / 2)) {
            return -1;
        }
        cap *= 2;
    }
    if (cap > buf->limit) {
        cap = buf->limit;
    }
    if (cap < need) {
        return -1;
    }
    grown = (char *)realloc(buf->data, cap + 1);
    if (!grown) {
        return -1;
    }
    buf->data = grown;
    buf->cap = cap;
    return 0;
}

int ff_buf_append(FfBuf *buf, const char *bytes, size_t len) {
    if (!buf->data) {
        return -1;
    }
    if (!bytes || len == 0) {
        return 1;
    }
    if (buf->len >= buf->limit) {
        return 0;
    }
    if (len > buf->limit - buf->len) {
        len = buf->limit - buf->len;
    }
    if (buf_reserve(buf, buf->len + len) != 0) {
        return -1;
    }
    memcpy(buf->data + buf->len, bytes, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

int ff_buf_puts(FfBuf *buf, const char *text) {
    if (!text) {
        return 1;
    }
    return ff_buf_append(buf, text, strlen(text));
}

int ff_buf_putc(FfBuf *buf, char ch) {
    return ff_buf_append(buf, &ch, 1);
}

char *ff_buf_detach(FfBuf *buf) {
    char *owned;
    if (!buf->data) {
        owned = (char *)malloc(1);
        if (owned) {
            owned[0] = '\0';
        }
        return owned;
    }
    if (buf->len > buf->cap) {
        buf->len = buf->cap;
    }
    buf->data[buf->len] = '\0';
    owned = buf->data;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return owned;
}

void ff_copy_cap(char *dst, size_t dst_len, const char *src) {
    size_t i;
    if (dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < dst_len && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

char *ff_strdup(const char *text) {
    size_t len;
    char *copy;
    if (!text) {
        return NULL;
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

char *ff_strndup(const char *text, size_t len) {
    char *copy;
    if (!text) {
        return NULL;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

int ff_ascii_ieq(const char *text, const char *lower) {
    size_t i;
    if (!text || !lower) {
        return 0;
    }
    for (i = 0; lower[i]; i++) {
        char ch = text[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch != lower[i]) {
            return 0;
        }
    }
    return text[i] == '\0';
}
