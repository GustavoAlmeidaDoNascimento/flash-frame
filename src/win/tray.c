#include "app.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HICON make_app_icon(int size) {
    BITMAPINFO info;
    ICONINFO icon_info;
    HDC screen;
    HBITMAP color = NULL;
    HBITMAP mask = NULL;
    HICON icon = NULL;
    uint8_t *bits = NULL;
    int y;
    int x;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL);
    color = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!color || !bits) {
        return NULL;
    }
    memset(bits, 0, (size_t)size * (size_t)size * 4);
    for (y = 1; y < size - 1; y++) {
        for (x = 2; x < size - 2; x++) {
            uint8_t *pixel = bits + ((y * size + x) * 4);
            int stripe = y > size / 2;
            pixel[0] = stripe ? 230 : 70;
            pixel[1] = stripe ? 236 : 140;
            pixel[2] = stripe ? 240 : 220;
            pixel[3] = 255;
        }
    }
    mask = CreateBitmap(size, size, 1, 1, NULL);
    memset(&icon_info, 0, sizeof(icon_info));
    icon_info.fIcon = TRUE;
    icon_info.hbmColor = color;
    icon_info.hbmMask = mask;
    icon = CreateIconIndirect(&icon_info);
    DeleteObject(color);
    if (mask) {
        DeleteObject(mask);
    }
    return icon;
}

int startup_enabled(void) {
    HKEY key;
    wchar_t value[MAX_PATH * 2];
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG rc;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return 0;
    }
    rc = RegQueryValueExW(key, L"FlashFrame", NULL, &type, (BYTE *)value, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_SZ && value[0];
}

void startup_set(int on) {
    HKEY key;
    wchar_t exe[MAX_PATH];
    wchar_t quoted[MAX_PATH + 8];
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return;
    }
    if (!on) {
        RegDeleteValueW(key, L"FlashFrame");
        RegCloseKey(key);
        return;
    }
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(quoted, MAX_PATH + 8, L"\"%s\"", exe);
    RegSetValueExW(key, L"FlashFrame", 0, REG_SZ, (const BYTE *)quoted, (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void tray_add(App *app) {
    HICON icon = make_app_icon(16);
    memset(&app->tray, 0, sizeof(app->tray));
    app->tray.cbSize = sizeof(app->tray);
    app->tray.hWnd = app->hwnd;
    app->tray.uID = 1;
    app->tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    app->tray.uCallbackMessage = WM_APP + 1;
    app->tray.hIcon = icon;
    wcscpy_s(app->tray.szTip, sizeof(app->tray.szTip) / sizeof(wchar_t), L"Flash Frame");
    app->tray_added = Shell_NotifyIconW(NIM_ADD, &app->tray) ? 1 : 0;
}

void tray_remove(App *app) {
    if (!app->tray_added) {
        return;
    }
    Shell_NotifyIconW(NIM_DELETE, &app->tray);
    if (app->tray.hIcon) {
        DestroyIcon(app->tray.hIcon);
        app->tray.hIcon = NULL;
    }
    app->tray_added = 0;
}

void tray_menu(App *app) {
    HMENU menu;
    POINT point;
    int command;
    if (app->busy || app->modal) {
        ShowWindow(app->hwnd, SW_SHOW);
        return;
    }
    menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Show");
    AppendMenuW(menu, MF_STRING, 2, L"Import deck...");
    AppendMenuW(menu, MF_STRING, 3, L"New deck...");
    AppendMenuW(menu, MF_STRING, 4, L"New card...");
    AppendMenuW(menu, MF_STRING | (app->startup_on ? MF_CHECKED : MF_UNCHECKED), 5, L"Launch at startup");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 6, L"Quit");
    GetCursorPos(&point);
    SetForegroundWindow(app->hwnd);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, app->hwnd, NULL);
    DestroyMenu(menu);
    if (command == 1) {
        ShowWindow(app->hwnd, SW_SHOW);
        SetForegroundWindow(app->hwnd);
    } else if (command == 2) {
        app_import(app);
    } else if (command == 3) {
        app_new_deck(app);
    } else if (command == 4) {
        app_new_card(app);
    } else if (command == 5) {
        app_toggle_startup(app);
    } else if (command == 6) {
        app->quitting = 1;
        DestroyWindow(app->hwnd);
    }
}
