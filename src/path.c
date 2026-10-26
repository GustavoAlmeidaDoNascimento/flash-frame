#include "path.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int ff_path_widen(const char *utf8_path, wchar_t *dst, int dst_chars) {
    int wrote;
    if (!utf8_path || !dst || dst_chars <= 0) {
        return -1;
    }
    wrote = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, dst, dst_chars);
    return wrote > 0 ? 0 : -1;
}

int ff_path_narrow(const wchar_t *wide, char *dst, size_t dst_len) {
    int wrote;
    if (!dst || dst_len == 0) {
        return -1;
    }
    if (!wide) {
        dst[0] = '\0';
        return 0;
    }
    wrote = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dst, (int)dst_len, NULL, NULL);
    return wrote > 0 ? 0 : -1;
}

FILE *ff_path_fopen(const char *utf8_path, const char *mode) {
    wchar_t path[FF_PATH_MAX];
    wchar_t wmode[16];
    if (ff_path_widen(utf8_path, path, FF_PATH_MAX) != 0) {
        return NULL;
    }
    if (ff_path_widen(mode, wmode, 16) != 0) {
        return NULL;
    }
    return _wfopen(path, wmode);
}

int ff_path_mkdir(const char *utf8_path) {
    wchar_t path[FF_PATH_MAX];
    if (ff_path_widen(utf8_path, path, FF_PATH_MAX) != 0) {
        return -1;
    }
    if (CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    return -1;
}

int ff_path_remove_file(const char *utf8_path) {
    wchar_t path[FF_PATH_MAX];
    if (ff_path_widen(utf8_path, path, FF_PATH_MAX) != 0) {
        return -1;
    }
    return DeleteFileW(path) ? 0 : -1;
}

int ff_path_remove_empty_dir(const char *utf8_path) {
    wchar_t path[FF_PATH_MAX];
    if (ff_path_widen(utf8_path, path, FF_PATH_MAX) != 0) {
        return -1;
    }
    return RemoveDirectoryW(path) ? 0 : -1;
}

int ff_path_remove_children(const char *utf8_dir) {
    wchar_t dir[FF_PATH_MAX];
    wchar_t pattern[FF_PATH_MAX];
    WIN32_FIND_DATAW found;
    HANDLE handle;
    if (ff_path_widen(utf8_dir, dir, FF_PATH_MAX) != 0) {
        return -1;
    }
    if (swprintf(pattern, FF_PATH_MAX, L"%s\\*", dir) < 0) {
        return -1;
    }
    handle = FindFirstFileW(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        wchar_t child[FF_PATH_MAX];
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) {
            continue;
        }
        if (swprintf(child, FF_PATH_MAX, L"%s\\%s", dir, found.cFileName) < 0) {
            continue;
        }
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        DeleteFileW(child);
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
    return RemoveDirectoryW(dir) ? 0 : -1;
}

int ff_path_temp(char *dst, size_t dst_len, const char *leaf) {
    wchar_t temp[MAX_PATH];
    wchar_t full[FF_PATH_MAX];
    wchar_t wleaf[260];
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        return -1;
    }
    if (ff_path_widen(leaf ? leaf : "flash-frame.tmp", wleaf, 260) != 0) {
        return -1;
    }
    if (swprintf(full, FF_PATH_MAX, L"%s%s", temp, wleaf) < 0) {
        return -1;
    }
    return ff_path_narrow(full, dst, dst_len);
}

#else

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

FILE *ff_path_fopen(const char *utf8_path, const char *mode) {
    return fopen(utf8_path, mode);
}

int ff_path_mkdir(const char *utf8_path) {
    if (mkdir(utf8_path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

int ff_path_remove_file(const char *utf8_path) {
    return remove(utf8_path) == 0 ? 0 : -1;
}

int ff_path_remove_empty_dir(const char *utf8_path) {
    return rmdir(utf8_path) == 0 ? 0 : -1;
}

int ff_path_remove_children(const char *utf8_dir) {
    (void)utf8_dir;
    return -1;
}

int ff_path_temp(char *dst, size_t dst_len, const char *leaf) {
    int wrote = snprintf(dst, dst_len, "/tmp/%s", leaf ? leaf : "flash-frame.tmp");
    return wrote > 0 && (size_t)wrote < dst_len ? 0 : -1;
}

#endif
