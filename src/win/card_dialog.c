#include "modal.h"

#include "path.h"

#include <commdlg.h>
#include <stdio.h>
#include <string.h>

static void sanitize_leaf(const wchar_t *path, wchar_t *leaf, int cap) {
    const wchar_t *name = path ? path : L"";
    const wchar_t *split;
    int used = 0;
    split = wcsrchr(name, L'\\');
    if (split) {
        name = split + 1;
    }
    split = wcsrchr(name, L'/');
    if (split) {
        name = split + 1;
    }
    for (; *name && used + 1 < cap; name++) {
        wchar_t ch = *name;
        if (ch < 32 || wcschr(L"<>:\"|?*\\/", ch)) {
            ch = L'_';
        }
        leaf[used++] = ch;
    }
    leaf[used] = L'\0';
    if (used == 0 || wcscmp(leaf, L".") == 0 || wcscmp(leaf, L"..") == 0) {
        wcscpy_s(leaf, (size_t)cap, L"media.bin");
    }
}

static int copy_into_deck(App *app, const wchar_t *source, char *rel, size_t rel_len, char *full, size_t full_len) {
    wchar_t leaf[200];
    wchar_t folder[FF_PATH_MAX];
    wchar_t dest[FF_PATH_MAX];
    wchar_t stem[160];
    wchar_t ext[40];
    char folder_utf8[FF_PATH_MAX];
    char leaf_utf8[240];
    const wchar_t *dot;
    int n;
    if (!source || !source[0]) {
        return 0;
    }
    sanitize_leaf(source, leaf, 200);
    snprintf(folder_utf8, sizeof(folder_utf8), "%s\\%lld", app->media_root, (long long)app->deck_id);
    if (ff_path_mkdir(app->media_root) != 0 || ff_path_mkdir(folder_utf8) != 0) {
        return -1;
    }
    if (ff_path_widen(folder_utf8, folder, FF_PATH_MAX) != 0) {
        return -1;
    }
    dot = wcsrchr(leaf, L'.');
    if (dot && dot != leaf) {
        size_t stem_len = (size_t)(dot - leaf);
        if (stem_len >= 160) {
            stem_len = 159;
        }
        memcpy(stem, leaf, stem_len * sizeof(wchar_t));
        stem[stem_len] = L'\0';
        wcscpy_s(ext, 40, dot);
    } else {
        wcscpy_s(stem, 160, leaf);
        ext[0] = L'\0';
    }
    _snwprintf(dest, FF_PATH_MAX, L"%s\\%s", folder, leaf);
    dest[FF_PATH_MAX - 1] = L'\0';
    for (n = 2; GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES && n < 100; n++) {
        _snwprintf(dest, FF_PATH_MAX, L"%s\\%s_%d%s", folder, stem, n, ext);
        dest[FF_PATH_MAX - 1] = L'\0';
    }
    if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES || !CopyFileW(source, dest, TRUE)) {
        return -1;
    }
    if (ff_path_narrow(dest, full, full_len) != 0) {
        DeleteFileW(dest);
        return -1;
    }
    sanitize_leaf(dest, leaf, 200);
    if (ff_path_narrow(leaf, leaf_utf8, sizeof(leaf_utf8)) != 0) {
        DeleteFileW(dest);
        return -1;
    }
    snprintf(rel, rel_len, "%lld/%s", (long long)app->deck_id, leaf_utf8);
    return 1;
}

static int pick_file(HWND owner, wchar_t *dst, const wchar_t *filter, const wchar_t *title) {
    OPENFILENAMEW open;
    memset(dst, 0, MAX_PATH * sizeof(wchar_t));
    memset(&open, 0, sizeof(open));
    open.lStructSize = sizeof(open);
    open.hwndOwner = owner;
    open.lpstrFilter = filter;
    open.lpstrFile = dst;
    open.nMaxFile = MAX_PATH;
    open.lpstrTitle = title;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&open) ? 1 : 0;
}

typedef struct CardState {
    App *app;
    HWND dialog;
    HWND front_edit;
    HWND back_edit;
    wchar_t image[MAX_PATH];
    wchar_t audio[MAX_PATH];
    int ok;
    RECT image_btn;
    RECT audio_btn;
    RECT ok_rect;
    RECT cancel_rect;
    RECT image_label;
    RECT audio_label;
} CardState;

static int blank_text(const wchar_t *text) {
    while (text && *text && *text == L' ') {
        text++;
    }
    return !text || !text[0];
}

static int commit_new_card(CardState *state) {
    App *app = state->app;
    wchar_t front[4001];
    wchar_t back[4001];
    char front_utf8[16001];
    char back_utf8[16001];
    char image_rel[512];
    char audio_rel[512];
    char image_full[FF_PATH_MAX];
    char audio_full[FF_PATH_MAX];
    FfInsertCard card;
    FfMediaScan scan;
    FfWarnChoice choice;
    int copied_image = 0;
    int copied_audio = 0;
    GetWindowTextW(state->front_edit, front, 4001);
    GetWindowTextW(state->back_edit, back, 4001);
    if (blank_text(front)) {
        show_utf8(state->dialog, "The front of the card cannot be empty.");
        return 0;
    }
    if ((state->image[0] || state->audio[0]) && app->warn_media) {
        memset(&scan, 0, sizeof(scan));
        scan.image_cards = state->image[0] ? 1 : 0;
        scan.audio_cards = state->audio[0] ? 1 : 0;
        scan.preview_count = 1;
        snprintf(
            scan.preview[0],
            sizeof(scan.preview[0]),
            "New card - %s%s%s",
            state->image[0] ? "image" : "",
            state->image[0] && state->audio[0] ? ", " : "",
            state->audio[0] ? "audio" : "");
        if (!confirm_media(app, state->dialog, &scan, &choice, L"Add card")) {
            return 0;
        }
        remember_warning(app, &choice);
    }
    image_rel[0] = '\0';
    audio_rel[0] = '\0';
    image_full[0] = '\0';
    audio_full[0] = '\0';
    copied_image = copy_into_deck(app, state->image, image_rel, sizeof(image_rel), image_full, sizeof(image_full));
    if (copied_image < 0) {
        show_utf8(state->dialog, "Could not copy the image into the deck.");
        return 0;
    }
    copied_audio = copy_into_deck(app, state->audio, audio_rel, sizeof(audio_rel), audio_full, sizeof(audio_full));
    if (copied_audio < 0) {
        if (copied_image > 0) {
            ff_path_remove_file(image_full);
        }
        show_utf8(state->dialog, "Could not copy the audio into the deck.");
        return 0;
    }
    if (ff_path_narrow(front, front_utf8, sizeof(front_utf8)) != 0 || ff_path_narrow(back, back_utf8, sizeof(back_utf8)) != 0) {
        if (copied_image > 0) {
            ff_path_remove_file(image_full);
        }
        if (copied_audio > 0) {
            ff_path_remove_file(audio_full);
        }
        show_utf8(state->dialog, "The card text is too long.");
        return 0;
    }
    memset(&card, 0, sizeof(card));
    card.deck_id = app->deck_id;
    ff_sched_new_card(&app->sched, &card.sched);
    card.front = front_utf8;
    card.back = back_utf8;
    card.image_front = image_rel[0] ? image_rel : NULL;
    card.audio_front = audio_rel[0] ? audio_rel : NULL;
    card.has_media = copied_image > 0 || copied_audio > 0;
    if (ff_store_insert_card(app->store, &card) != 0) {
        if (copied_image > 0) {
            ff_path_remove_file(image_full);
        }
        if (copied_audio > 0) {
            ff_path_remove_file(audio_full);
        }
        show_utf8(state->dialog, "Could not save the card.");
        return 0;
    }
    if (card.has_media) {
        app->heavy = 1;
        ff_store_set_deck_heavy(app->store, app->deck_id, 1);
        ff_store_set_heap_limit(1);
    }
    return 1;
}

static void layout_card(App *app, CardState *state) {
    RECT client;
    int y;
    GetClientRect(state->dialog, &client);
    y = app_px(app, 12);
    MoveWindow(state->front_edit, app_px(app, 12), y, client.right - app_px(app, 24), app_px(app, 88), TRUE);
    y += app_px(app, 98);
    MoveWindow(state->back_edit, app_px(app, 12), y, client.right - app_px(app, 24), app_px(app, 88), TRUE);
    y += app_px(app, 100);
    state->image_btn = (RECT){app_px(app, 12), y, app_px(app, 110), y + app_px(app, 28)};
    state->audio_btn = (RECT){app_px(app, 118), y, app_px(app, 216), y + app_px(app, 28)};
    y += app_px(app, 32);
    state->image_label = (RECT){app_px(app, 12), y, client.right - app_px(app, 12), y + app_px(app, 18)};
    state->audio_label = (RECT){app_px(app, 12), y + app_px(app, 18), client.right - app_px(app, 12), y + app_px(app, 36)};
    state->cancel_rect = (RECT){client.right - app_px(app, 180), client.bottom - app_px(app, 44), client.right - app_px(app, 96), client.bottom - app_px(app, 16)};
    state->ok_rect = (RECT){client.right - app_px(app, 88), client.bottom - app_px(app, 44), client.right - app_px(app, 12), client.bottom - app_px(app, 16)};
}

static const wchar_t *file_leaf(const wchar_t *path) {
    const wchar_t *leaf = path;
    const wchar_t *split = wcsrchr(path, L'\\');
    if (split) {
        leaf = split + 1;
    }
    return leaf[0] ? leaf : L"none";
}

static LRESULT CALLBACK card_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CardState *state = (CardState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
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
        wchar_t image_line[MAX_PATH + 16];
        wchar_t audio_line[MAX_PATH + 16];
        paint_dialog_button(hdc, state->image_btn, L"Add image", RGB(58, 58, 66));
        paint_dialog_button(hdc, state->audio_btn, L"Add audio", RGB(58, 58, 66));
        paint_dialog_button(hdc, state->ok_rect, L"Add", RGB(52, 140, 86));
        paint_dialog_button(hdc, state->cancel_rect, L"Cancel", RGB(90, 64, 64));
        _snwprintf(image_line, MAX_PATH + 16, L"Image: %s", state->image[0] ? file_leaf(state->image) : L"none");
        _snwprintf(audio_line, MAX_PATH + 16, L"Audio: %s", state->audio[0] ? file_leaf(state->audio) : L"none");
        image_line[MAX_PATH + 15] = L'\0';
        audio_line[MAX_PATH + 15] = L'\0';
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(180, 180, 188));
        DrawTextW(hdc, image_line, -1, &state->image_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        DrawTextW(hdc, audio_line, -1, &state->audio_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        wchar_t picked[MAX_PATH];
        if (point_in(state->image_btn, lp)) {
            if (pick_file(hwnd, picked, L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0\0", L"Add image")) {
                wcscpy_s(state->image, MAX_PATH, picked);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (point_in(state->audio_btn, lp)) {
            if (pick_file(hwnd, picked, L"Audio\0*.mp3;*.wav;*.ogg;*.m4a;*.flac\0\0", L"Add audio")) {
                wcscpy_s(state->audio, MAX_PATH, picked);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (point_in(state->ok_rect, lp)) {
            if (commit_new_card(state)) {
                state->ok = 1;
                DestroyWindow(hwnd);
            }
        } else if (point_in(state->cancel_rect, lp)) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void app_new_card(App *app) {
    CardState state;
    RECT window_rect;
    if (!app || app->busy || app->modal) {
        return;
    }
    if (app->deck_id == 0 || app->deck_count == 0) {
        show_utf8(app->hwnd, "Create or import a deck before adding a card.");
        return;
    }
    memset(&state, 0, sizeof(state));
    state.app = app;
    if (!popup_class(L"FlashFrameCard", card_proc)) {
        return;
    }
    GetWindowRect(app->hwnd, &window_rect);
    state.dialog = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FlashFrameCard",
        L"New card",
        WS_POPUP | WS_BORDER,
        window_rect.left + app_px(app, 12),
        window_rect.top + app_px(app, 12),
        app_px(app, 420),
        app_px(app, 360),
        app->hwnd,
        NULL,
        app->instance,
        &state);
    if (!state.dialog) {
        return;
    }
    state.front_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 10, 10, state.dialog, (HMENU)301, app->instance, NULL);
    state.back_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 10, 10, state.dialog, (HMENU)302, app->instance, NULL);
    SendMessageW(state.front_edit, WM_SETFONT, (WPARAM)app->font_body, TRUE);
    SendMessageW(state.back_edit, WM_SETFONT, (WPARAM)app->font_body, TRUE);
    SendMessageW(state.front_edit, EM_SETLIMITTEXT, 4000, 0);
    SendMessageW(state.back_edit, EM_SETLIMITTEXT, 4000, 0);
    hook_edit(state.front_edit);
    hook_edit(state.back_edit);
    layout_card(app, &state);
    modal_loop(app, app->hwnd, state.dialog, state.front_edit);
    if (state.ok) {
        app_reload(app);
        app_pull_card(app);
    }
}
