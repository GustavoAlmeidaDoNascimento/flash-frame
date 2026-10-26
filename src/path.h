#ifndef FF_PATH_H
#define FF_PATH_H

#include <stdio.h>

#define FF_PATH_MAX 4096

FILE *ff_path_fopen(const char *utf8_path, const char *mode);
int ff_path_mkdir(const char *utf8_path);
int ff_path_remove_file(const char *utf8_path);
int ff_path_remove_empty_dir(const char *utf8_path);
int ff_path_remove_children(const char *utf8_dir);
int ff_path_temp(char *dst, size_t dst_len, const char *leaf);

#ifdef _WIN32
int ff_path_widen(const char *utf8_path, wchar_t *dst, int dst_chars);
int ff_path_narrow(const wchar_t *wide, char *dst, size_t dst_len);
#endif

#endif
