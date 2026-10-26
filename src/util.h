#ifndef FF_UTIL_H
#define FF_UTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct FfBuf {
    char *data;
    size_t len;
    size_t cap;
    size_t limit;
} FfBuf;

int ff_buf_init(FfBuf *buf, size_t limit);
void ff_buf_free(FfBuf *buf);
void ff_buf_clear(FfBuf *buf);
int ff_buf_append(FfBuf *buf, const char *bytes, size_t len);
int ff_buf_puts(FfBuf *buf, const char *text);
int ff_buf_putc(FfBuf *buf, char ch);
char *ff_buf_detach(FfBuf *buf);

void ff_copy_cap(char *dst, size_t dst_len, const char *src);
char *ff_strdup(const char *text);
char *ff_strndup(const char *text, size_t len);
int ff_ascii_ieq(const char *text, const char *lower);

#endif
