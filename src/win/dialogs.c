#include "modal.h"

#include "apkg.h"
#include "path.h"

#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>


typedef struct WarnState {
    HWND list;
    const wchar_t *ok_label;
    FfWarnChoice *choice;
    int checked;
    RECT ok_rect;
    RECT cancel_rect;
    RECT check_rect;
} WarnState;

static wchar_t *warning_text(const FfMediaScan *scan) {
    char buffer[20 * 1024];
    int used;
    int i;
    int wide_count;
    wchar_t *wide;
    used = snprintf(
        buffer,
        sizeof(buffer),
        "This deck has media.\r\n\r\nImages: %d cards\r\nAudio: %d cards\r\n\r\n"
        "Text decks stay inside a 20 MB working-set budget. Images are decoded one card at a time and audio is streamed from disk, but the process still gets heavier. Continue only if you accept that.\r\n\r\n",
        scan->image_cards,
        scan->audio_cards);
    if (used < 0) {
        used = 0;
    }
    for (i = 0; i < scan->preview_count && used + 200 < (int)sizeof(buffer); i++) {
        used += snprintf(buffer + used, sizeof(buffer) - (size_t)used, "%s\r\n", scan->preview[i]);
    }
    if (scan->image_cards + scan->audio_cards > scan->preview_count) {
        snprintf(buffer + used, sizeof(buffer) - (size_t)used, "\r\nShowing the first %d cards with media.", scan->preview_count);
    }
    wide_count = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
    wide = (wchar_t *)calloc((size_t)wide_count + 1, sizeof(wchar_t));
    if (wide) {
        MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wide, wide_count);
    }
    return wide;
}

static LRESULT CALLBACK warn_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WarnState *state = (WarnState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *created = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!state) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)wp, RGB(230, 230, 234));
        SetBkColor((HDC)wp, RGB(20, 20, 24));
        return (LRESULT)field_brush();
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC hdc = BeginPaint(hwnd, &paint);
        paint_dialog_button(hdc, state->ok_rect, state->ok_label, RGB(52, 140, 86));
        paint_dialog_button(hdc, state->cancel_rect, L"Cancel", RGB(90, 64, 64));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 226));
        DrawTextW(hdc, state->checked ? L"[x]  Don't show this again" : L"[ ]  Don't show this again", -1, &state->check_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        if (point_in(state->check_rect, lp)) {
            state->checked = !state->checked;
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (point_in(state->ok_rect, lp)) {
            state->choice->load = 1;
            state->choice->dont_show_again = state->checked;
            DestroyWindow(hwnd);
        } else if (point_in(state->cancel_rect, lp)) {
            state->choice->load = 0;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        state->choice->load = 0;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void layout_warning(App *app, HWND dialog, WarnState *state) {
    RECT client;
    int width;
    int height;
    GetClientRect(dialog, &client);
    width = client.right;
    height = client.bottom;
    MoveWindow(state->list, app_px(app, 12), app_px(app, 12), width - app_px(app, 24), height - app_px(app, 110), TRUE);
    state->check_rect = (RECT){app_px(app, 12), height - app_px(app, 86), width - app_px(app, 12), height - app_px(app, 62)};
    state->cancel_rect = (RECT){width - app_px(app, 188), height - app_px(app, 48), width - app_px(app, 104), height - app_px(app, 20)};
    state->ok_rect = (RECT){width - app_px(app, 96), height - app_px(app, 48), width - app_px(app, 12), height - app_px(app, 20)};
}

int confirm_media(App *app, HWND owner, const FfMediaScan *scan, FfWarnChoice *choice, const wchar_t *ok_label) {
    WarnState state;
    HWND dialog;
    wchar_t *text;
    RECT window_rect;
    memset(&state, 0, sizeof(state));
    memset(choice, 0, sizeof(*choice));
    state.choice = choice;
    state.ok_label = ok_label;
    if (!popup_class(L"FlashFrameWarn", warn_proc)) {
        return 0;
    }
    GetWindowRect(owner, &window_rect);
    dialog = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FlashFrameWarn",
        L"Media makes Flash Frame heavier",
        WS_POPUP | WS_BORDER,
        window_rect.left,
        window_rect.top,
        app_px(app, 460),
        app_px(app, 420),
        owner,
        NULL,
        app->instance,
        &state);
    if (!dialog) {
        return 0;
    }
    text = warning_text(scan);
    state.list = CreateWindowExW(
        0,
        L"EDIT",
        text ? text : L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
        0,
        0,
        10,
        10,
        dialog,
        (HMENU)101,
        app->instance,
        NULL);
    SendMessageW(state.list, WM_SETFONT, (WPARAM)app->font_small, TRUE);
    hook_edit(state.list);
    layout_warning(app, dialog, &state);
    free(text);
    modal_loop(app, owner, dialog, state.list);
    return choice->load;
}

void remember_warning(App *app, const FfWarnChoice *choice) {
    if (choice->load && choice->dont_show_again) {
        app->warn_media = 0;
        ff_store_meta_set(app->store, "warn_media", "0");
    }
}

static void import_progress(int cards, void *user) {
    App *app = (App *)user;
    MSG message;
    app->import_count = cards;
    InvalidateRect(app->hwnd, NULL, FALSE);
    UpdateWindow(app->hwnd);
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            PostQuitMessage((int)message.wParam);
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static int import_warn(const FfMediaScan *scan, void *user, FfWarnChoice *choice) {
    App *app = (App *)user;
    confirm_media(app, app->hwnd, scan, choice, L"Load deck");
    return 0;
}

void app_import(App *app) {
    wchar_t file[MAX_PATH];
    char utf8[FF_PATH_MAX];
    FfImportRequest request;
    FfImportResult result;
    char err[512];
    OPENFILENAMEW open;
    int i;
    int64_t newest = 0;
    if (!app || app->busy || app->modal) {
        return;
    }
    memset(file, 0, sizeof(file));
    memset(&open, 0, sizeof(open));
    open.lStructSize = sizeof(open);
    open.hwndOwner = app->hwnd;
    open.lpstrFilter = L"Anki package (*.apkg)\0*.apkg\0";
    open.lpstrFile = file;
    open.nMaxFile = MAX_PATH;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&open)) {
        return;
    }
    if (ff_path_narrow(file, utf8, sizeof(utf8)) != 0) {
        return;
    }
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    err[0] = '\0';
    request.store = app->store;
    request.apkg_path = utf8;
    request.media_root = app->media_root;
    request.skip_warn = !app->warn_media;
    request.warn = import_warn;
    request.warn_user = app;
    request.progress = import_progress;
    request.progress_user = app;
    app->busy = 1;
    app->import_count = 0;
    if (ff_import_apkg(&request, &result, err, (int)sizeof(err)) != 0) {
        app->busy = 0;
        show_utf8(app->hwnd, err);
        return;
    }
    app->busy = 0;
    {
        FfWarnChoice choice;
        choice.load = 1;
        choice.dont_show_again = result.dont_show_again;
        remember_warning(app, &choice);
    }
    if (result.cancelled) {
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    app_reload(app);
    for (i = 0; i < app->deck_count; i++) {
        if (app->decks[i].id > newest) {
            newest = app->decks[i].id;
        }
    }
    if (newest) {
        app_select_deck(app, newest);
    } else {
        app_pull_card(app);
    }
}

typedef struct NameState {
    HWND edit;
    int ok;
    wchar_t value[240];
    RECT ok_rect;
    RECT cancel_rect;
} NameState;

static LRESULT CALLBACK name_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    NameState *state = (NameState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *created = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!state) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)wp, RGB(230, 230, 234));
        SetBkColor((HDC)wp, RGB(20, 20, 24));
        return (LRESULT)field_brush();
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC hdc = BeginPaint(hwnd, &paint);
        paint_dialog_button(hdc, state->ok_rect, L"Create", RGB(52, 140, 86));
        paint_dialog_button(hdc, state->cancel_rect, L"Cancel", RGB(90, 64, 64));
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        if (point_in(state->ok_rect, lp)) {
            GetWindowTextW(state->edit, state->value, 240);
            state->ok = 1;
            DestroyWindow(hwnd);
        } else if (point_in(state->cancel_rect, lp)) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        GetWindowTextW(state->edit, state->value, 240);
        state->ok = 1;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void app_new_deck(App *app) {
    NameState state;
    HWND dialog;
    RECT window_rect;
    RECT client;
    char name[512];
    int64_t id = 0;
    if (!app || app->busy || app->modal) {
        return;
    }
    memset(&state, 0, sizeof(state));
    if (!popup_class(L"FlashFrameName", name_proc)) {
        return;
    }
    GetWindowRect(app->hwnd, &window_rect);
    dialog = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FlashFrameName",
        L"New deck",
        WS_POPUP | WS_BORDER,
        window_rect.left + app_px(app, 24),
        window_rect.top + app_px(app, 48),
        app_px(app, 360),
        app_px(app, 140),
        app->hwnd,
        NULL,
        app->instance,
        &state);
    if (!dialog) {
        return;
    }
    GetClientRect(dialog, &client);
    state.edit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        app_px(app, 12),
        app_px(app, 16),
        client.right - app_px(app, 24),
        app_px(app, 24),
        dialog,
        (HMENU)201,
        app->instance,
        NULL);
    SendMessageW(state.edit, WM_SETFONT, (WPARAM)app->font_body, TRUE);
    SendMessageW(state.edit, EM_SETLIMITTEXT, 200, 0);
    hook_edit(state.edit);
    state.cancel_rect = (RECT){client.right - app_px(app, 180), client.bottom - app_px(app, 44), client.right - app_px(app, 96), client.bottom - app_px(app, 16)};
    state.ok_rect = (RECT){client.right - app_px(app, 88), client.bottom - app_px(app, 44), client.right - app_px(app, 12), client.bottom - app_px(app, 16)};
    modal_loop(app, app->hwnd, dialog, state.edit);
    if (!state.ok) {
        return;
    }
    if (ff_path_narrow(state.value[0] ? state.value : L"Deck", name, sizeof(name)) != 0) {
        show_utf8(app->hwnd, "That deck name is too long.");
        return;
    }
    if (ff_store_create_deck(app->store, name, 0, &id) != 0) {
        show_utf8(app->hwnd, "Could not create the deck.");
        return;
    }
    app_select_deck(app, id);
}

