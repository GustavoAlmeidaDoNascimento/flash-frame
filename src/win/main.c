#include "app.h"

#include "budget.h"
#include "path.h"

#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windowsx.h>

static void show_utf8(HWND owner, const char *text) {
    wchar_t wide[768];
    if (!text || !text[0]) {
        text = "Something went wrong.";
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 768) <= 0) {
        MessageBoxW(owner, L"Flash Frame could not start.", L"Flash Frame", MB_OK | MB_ICONWARNING);
        return;
    }
    MessageBoxW(owner, wide, L"Flash Frame", MB_OK | MB_ICONWARNING);
}

int app_init(App *app) {
    wchar_t appdata[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t db[MAX_PATH];
    wchar_t media[MAX_PATH];
    char err[256];
    char value[64];
    memset(app, 0, sizeof(*app));
    app->instance = GetModuleHandleW(NULL);
    app->warn_media = 1;
    app->dpi = 96;
    ff_sched_config_default(&app->sched);
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata))) {
        show_utf8(NULL, "Could not find the AppData folder.");
        return -1;
    }
    _snwprintf(dir, MAX_PATH, L"%s\\FlashFrame", appdata);
    _snwprintf(db, MAX_PATH, L"%s\\collection.db", dir);
    _snwprintf(media, MAX_PATH, L"%s\\media", dir);
    dir[MAX_PATH - 1] = L'\0';
    db[MAX_PATH - 1] = L'\0';
    media[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(dir, NULL);
    CreateDirectoryW(media, NULL);
    if (ff_path_narrow(db, app->db_path, sizeof(app->db_path)) != 0 || ff_path_narrow(media, app->media_root, sizeof(app->media_root)) != 0) {
        show_utf8(NULL, "The data folder path is too long.");
        return -1;
    }
    err[0] = '\0';
    if (ff_store_open(&app->store, app->db_path, err, (int)sizeof(err)) != 0) {
        show_utf8(NULL, err[0] ? err : "Could not open the card collection.");
        return -1;
    }
    if (ff_store_meta_get(app->store, "startup", value, (int)sizeof(value)) == 1) {
        startup_set(1);
        ff_store_meta_set(app->store, "startup", "1");
    }
    app->startup_on = startup_enabled();
    if (ff_store_meta_get(app->store, "warn_media", value, (int)sizeof(value)) == 0 && value[0] == '0') {
        app->warn_media = 0;
    }
    if (ff_store_meta_get(app->store, "deck", value, (int)sizeof(value)) == 0) {
        app->deck_id = strtoll(value, NULL, 10);
    }
    app->background = CreateSolidBrush(RGB(20, 20, 24));
    app->working_set = sample_working_set();
    return 0;
}

void app_shutdown(App *app) {
    if (!app) {
        return;
    }
    if (app->hwnd) {
        KillTimer(app->hwnd, 1);
    }
    tray_remove(app);
    audio_stop();
    ff_study_card_free(app->card);
    app->card = NULL;
    ff_store_deck_list_free(app->decks);
    app->decks = NULL;
    app->deck_count = 0;
    if (app->image) {
        DeleteObject(app->image);
        app->image = NULL;
    }
    if (app->font_title) {
        DeleteObject(app->font_title);
    }
    if (app->font_body) {
        DeleteObject(app->font_body);
    }
    if (app->font_small) {
        DeleteObject(app->font_small);
    }
    if (app->font_button) {
        DeleteObject(app->font_button);
    }
    app->font_title = NULL;
    app->font_body = NULL;
    app->font_small = NULL;
    app->font_button = NULL;
    if (app->background) {
        DeleteObject(app->background);
        app->background = NULL;
    }
    if (app->store) {
        ff_store_close(app->store);
        app->store = NULL;
    }
}

static void save_position(App *app) {
    RECT rect;
    char buf[32];
    if (!app->hwnd || !app->store) {
        return;
    }
    if (!GetWindowRect(app->hwnd, &rect)) {
        return;
    }
    snprintf(buf, sizeof(buf), "%ld", (long)rect.left);
    ff_store_meta_set(app->store, "win_x", buf);
    snprintf(buf, sizeof(buf), "%ld", (long)rect.top);
    ff_store_meta_set(app->store, "win_y", buf);
}

static int saved_coord(App *app, const char *key, int *out) {
    char value[32];
    if (ff_store_meta_get(app->store, key, value, (int)sizeof(value)) != 0 || !value[0]) {
        return 0;
    }
    *out = atoi(value);
    return 1;
}

static void place_window(App *app) {
    RECT work;
    int width = app_px(app, 440);
    int height = app_px(app, 320);
    int x;
    int y;
    int saved_x = 0;
    int saved_y = 0;
    int has_x;
    int has_y;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = work.right - width - 16;
    y = work.bottom - height - 16;
    has_x = saved_coord(app, "win_x", &saved_x);
    has_y = saved_coord(app, "win_y", &saved_y);
    if (has_x && has_y) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        x = saved_x;
        y = saved_y;
        if (x < vx - width + 40 || y < vy - height + 40 || x > vx + vw - 40 || y > vy + vh - 40) {
            x = work.right - width - 16;
            y = work.bottom - height - 16;
        }
    }
    SetWindowPos(app->hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}

static void on_timer(App *app) {
    static unsigned long long shown_mb;
    static int shown_over = -1;
    unsigned long long mb;
    int over;
    app->working_set = sample_working_set();
    ff_store_set_heap_limit(app->heavy);
    if (ff_budget_over(app->heavy, app->working_set)) {
        DWORD now = GetTickCount();
        if (app->last_trim == 0 || now - app->last_trim >= 10000) {
            ff_store_release_memory(app->store);
            EmptyWorkingSet(GetCurrentProcess());
            app->last_trim = now;
            app->working_set = sample_working_set();
        }
    }
    if (!app->busy && !app->modal && app->mode == MODE_REVIEW && !app->card && app->next_due > 0) {
        int64_t now = (int64_t)time(NULL);
        int64_t ahead = app->sched.learn_ahead_secs;
        if (ahead < 0) {
            ahead = 0;
        }
        if (now + ahead >= app->next_due) {
            app_pull_card(app);
        } else {
            InvalidateRect(app->hwnd, NULL, FALSE);
        }
    }
    mb = app->working_set / (1024ull * 1024ull);
    over = ff_budget_over(app->heavy, app->working_set);
    if (mb != shown_mb || over != shown_over) {
        shown_mb = mb;
        shown_over = over;
        InvalidateRect(app->hwnd, NULL, FALSE);
    }
}

static void on_key(App *app, WPARAM key) {
    if (app->busy || app->modal) {
        return;
    }
    if (key == VK_ESCAPE) {
        ShowWindow(app->hwnd, SW_HIDE);
        return;
    }
    if (key == 'D') {
        if (app->deck_count == 0) {
            return;
        }
        app->mode = app->mode == MODE_DECKS ? MODE_REVIEW : MODE_DECKS;
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    if (key == 'I') {
        app_import(app);
        return;
    }
    if (key == 'N') {
        app_new_card(app);
        return;
    }
    if (!app->card || app->mode != MODE_REVIEW) {
        return;
    }
    if (key == VK_SPACE) {
        if (!app->revealed) {
            app_reveal(app);
        } else {
            app_grade(app, FF_GOOD);
        }
        return;
    }
    if (!app->revealed) {
        return;
    }
    if (key == '1' || key == VK_NUMPAD1) {
        app_grade(app, FF_AGAIN);
    } else if (key == '2' || key == VK_NUMPAD2) {
        app_grade(app, FF_HARD);
    } else if (key == '3' || key == VK_NUMPAD3) {
        app_grade(app, FF_GOOD);
    } else if (key == '4' || key == VK_NUMPAD4) {
        app_grade(app, FF_EASY);
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App *app = (App *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *created = (CREATESTRUCTW *)lp;
        app = (App *)created->lpCreateParams;
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!app) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC hdc = BeginPaint(hwnd, &paint);
        app_paint(app, hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int hit = app_hit(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        TRACKMOUSEEVENT track;
        if (hit != app->hot) {
            app->hot = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        memset(&track, 0, sizeof(track));
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        app->hot = HIT_NONE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        app_click(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (app->mode == MODE_DECKS && !app->busy && !app->modal) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            int row = app_px(app, 36);
            RECT client;
            int visible;
            int max_scroll;
            GetClientRect(hwnd, &client);
            visible = (client.bottom - app_px(app, 36) - app_px(app, 58) - app_px(app, 20)) / row;
            if (visible < 1) {
                visible = 1;
            }
            if (delta > 0 && app->deck_scroll > 0) {
                app->deck_scroll--;
            }
            if (delta < 0 && app->deck_scroll + 1 < app->deck_count) {
                app->deck_scroll++;
            }
            max_scroll = app->deck_count - visible;
            if (max_scroll < 0) {
                max_scroll = 0;
            }
            if (app->deck_scroll > max_scroll) {
                app->deck_scroll = max_scroll;
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((lp & (1u << 30)) == 0) {
            on_key(app, wp);
        }
        return 0;
    case WM_TIMER:
        on_timer(app);
        return 0;
    case WM_NCHITTEST: {
        POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hit;
        ScreenToClient(hwnd, &point);
        hit = app_hit(app, point.x, point.y);
        if (point.y >= 0 && point.y < app_px(app, 36) && hit == HIT_NONE) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        RECT *suggested = (RECT *)lp;
        app->dpi = HIWORD(wp);
        if (app->dpi < 96) {
            app->dpi = 96;
        }
        app_fonts(app);
        SetWindowPos(hwnd, HWND_TOPMOST, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOACTIVATE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_EXITSIZEMOVE:
        save_position(app);
        return 0;
    case WM_CLOSE:
        if (!app->quitting) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_APP + 1:
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            if (IsWindowVisible(hwnd)) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            tray_menu(app);
        }
        return 0;
    case WM_DESTROY:
        save_position(app);
        app_shutdown(app);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static void wake_existing_window(void) {
    HWND existing = FindWindowW(L"FlashFrameWindow", NULL);
    if (!existing) {
        return;
    }
    ShowWindow(existing, SW_SHOW);
    SetForegroundWindow(existing);
}

static void enable_dpi(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    BOOL(WINAPI * set_context)(HANDLE) = NULL;
    if (user32) {
        set_context = (BOOL(WINAPI *)(HANDLE))GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    }
    if (!set_context || !set_context((HANDLE)-4)) {
        SetProcessDPIAware();
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR cmdline, int show) {
    HANDLE mutex;
    WNDCLASSW window_class;
    App app;
    HWND hwnd;
    MSG message;
    HICON icon;
    (void)instance;
    (void)previous;
    (void)cmdline;
    (void)show;
    mutex = CreateMutexW(NULL, TRUE, L"Local\\FlashFrameSingleton");
    if (!mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        wake_existing_window();
        CloseHandle(mutex);
        return 0;
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    enable_dpi();
    if (app_init(&app) != 0) {
        CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }
    icon = make_app_icon(32);
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = wndproc;
    window_class.hInstance = app.instance;
    window_class.lpszClassName = L"FlashFrameWindow";
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = icon;
    if (!RegisterClassW(&window_class)) {
        app_shutdown(&app);
        if (icon) {
            DestroyIcon(icon);
        }
        CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }
    hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FlashFrameWindow",
        L"Flash Frame",
        WS_POPUP,
        0,
        0,
        app_px(&app, 440),
        app_px(&app, 320),
        NULL,
        NULL,
        app.instance,
        &app);
    if (!hwnd) {
        app_shutdown(&app);
        if (icon) {
            DestroyIcon(icon);
        }
        CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }
    app.dpi = GetDpiForWindow(hwnd);
    if (app.dpi < 96) {
        app.dpi = 96;
    }
    app_fonts(&app);
    place_window(&app);
    app_reload(&app);
    if (app.deck_count == 0) {
        app.mode = MODE_EMPTY;
    } else {
        app_pull_card(&app);
    }
    tray_add(&app);
    SetTimer(hwnd, 1, 2000, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    app_shutdown(&app);
    if (icon) {
        DestroyIcon(icon);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    CoUninitialize();
    return (int)message.wParam;
}
