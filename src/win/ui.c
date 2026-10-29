#include "app.h"

#include "budget.h"
#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int app_px(const App *app, int px) {
    int dpi = app->dpi > 0 ? app->dpi : 96;
    return MulDiv(px, dpi, 96);
}

static void today(char *dst, size_t dst_len) {
    time_t now = time(NULL);
    struct tm local;
    localtime_s(&local, &now);
    strftime(dst, dst_len, "%Y-%m-%d", &local);
}

static wchar_t *widen_text(const char *text) {
    int count;
    wchar_t *wide;
    if (!text) {
        text = "";
    }
    count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (count <= 0) {
        return NULL;
    }
    wide = (wchar_t *)calloc((size_t)count + 1, sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count);
    return wide;
}

static int media_file(const App *app, const char *relative, wchar_t *dst, int dst_chars) {
    char joined[FF_PATH_MAX];
    size_t i;
    if (!relative || !relative[0]) {
        return 0;
    }
    snprintf(joined, sizeof(joined), "%s\\%s", app->media_root, relative);
    for (i = 0; joined[i]; i++) {
        if (joined[i] == '/') {
            joined[i] = '\\';
        }
        if (joined[i] == '\x1f') {
            joined[i] = '\0';
            break;
        }
    }
    return ff_path_widen(joined, dst, dst_chars) == 0;
}

static const char *current_image(const App *app) {
    if (!app->card) {
        return NULL;
    }
    if (app->revealed && app->card->image_back && app->card->image_back[0]) {
        return app->card->image_back;
    }
    return app->card->image_front;
}

static const char *current_audio(const App *app) {
    if (!app->card) {
        return NULL;
    }
    if (app->revealed) {
        return app->card->audio_back;
    }
    return app->card->audio_front;
}

static void present_media(App *app) {
    wchar_t path[FF_PATH_MAX];
    const char *image = current_image(app);
    const char *audio = current_audio(app);
    if (app->image) {
        DeleteObject(app->image);
        app->image = NULL;
    }
    app->image_hidden = 0;
    app->working_set = sample_working_set();
    if (image && image[0]) {
        if (ff_budget_allow_image(app->heavy, app->working_set) && media_file(app, image, path, FF_PATH_MAX)) {
            app->image = load_image_file(path, &app->image_w, &app->image_h);
        }
        if (!app->image) {
            app->image_hidden = 1;
        }
    }
    if (audio && audio[0] && media_file(app, audio, path, FF_PATH_MAX)) {
        audio_play(path);
    } else if (!app->revealed) {
        audio_stop();
    }
}

void app_fonts(App *app) {
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
    app->font_title = CreateFontW(-app_px(app, 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app->font_body = CreateFontW(-app_px(app, 18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app->font_small = CreateFontW(-app_px(app, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app->font_button = CreateFontW(-app_px(app, 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void app_reload(App *app) {
    int i;
    int64_t now = (int64_t)time(NULL);
    int found = 0;
    ff_store_deck_list_free(app->decks);
    app->decks = NULL;
    app->deck_count = 0;
    if (ff_store_deck_list(app->store, &app->decks, &app->deck_count, now) != 0) {
        app->mode = MODE_EMPTY;
        return;
    }
    for (i = 0; i < app->deck_count; i++) {
        if (app->decks[i].id == app->deck_id) {
            found = 1;
            app->heavy = app->decks[i].heavy;
        }
    }
    if (!found) {
        app->deck_id = app->deck_count ? app->decks[0].id : 0;
        app->heavy = app->deck_count ? app->decks[0].heavy : 0;
    }
    ff_store_set_heap_limit(app->heavy);
    if (app->deck_scroll >= app->deck_count) {
        app->deck_scroll = 0;
    }
}

void app_select_deck(App *app, int64_t deck_id) {
    char id_text[32];
    app->deck_id = deck_id;
    snprintf(id_text, sizeof(id_text), "%lld", (long long)deck_id);
    ff_store_meta_set(app->store, "deck", id_text);
    app_reload(app);
    app_pull_card(app);
}

void app_pull_card(App *app) {
    char day[16];
    int64_t now = (int64_t)time(NULL);
    ff_study_card_free(app->card);
    app->card = NULL;
    app->revealed = 0;
    audio_stop();
    if (app->image) {
        DeleteObject(app->image);
        app->image = NULL;
    }
    if (app->deck_count == 0 || app->deck_id == 0) {
        app->mode = MODE_EMPTY;
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    today(day, sizeof(day));
    app->next_due = 0;
    if (ff_store_next_card(app->store, app->deck_id, now, day, app->sched.learn_ahead_secs, &app->card) != 0) {
        app->card = NULL;
    }
    if (!app->card) {
        int64_t ahead = app->sched.learn_ahead_secs;
        if (ahead < 0) {
            ahead = 0;
        }
        ff_store_next_learn_due(app->store, app->deck_id, now + ahead, &app->next_due);
    }
    app->mode = MODE_REVIEW;
    present_media(app);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

void app_reveal(App *app) {
    if (!app->card || app->revealed || app->busy) {
        return;
    }
    app->revealed = 1;
    present_media(app);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

void app_grade(App *app, int rating) {
    char day[16];
    int64_t now = (int64_t)time(NULL);
    int state_before;
    int64_t interval_before;
    if (!app->card || !app->revealed || app->busy) {
        return;
    }
    state_before = app->card->sched.state;
    interval_before = app->card->sched.interval_secs;
    ff_sched_answer(&app->sched, &app->card->sched, rating, now);
    today(day, sizeof(day));
    ff_store_save_answer(app->store, app->card, rating, state_before, interval_before, now, day);
    app_reload(app);
    app_pull_card(app);
}

typedef struct Layout {
    RECT header;
    RECT memory;
    RECT hide_btn;
    RECT close_btn;
    RECT card;
    RECT footer;
    RECT show_btn;
    RECT again_btn;
    RECT hard_btn;
    RECT good_btn;
    RECT easy_btn;
    RECT import_btn;
    RECT deck_btn;
    RECT play_btn;
} Layout;

static RECT rect_make(int x, int y, int w, int h) {
    RECT rect;
    rect.left = x;
    rect.top = y;
    rect.right = x + w;
    rect.bottom = y + h;
    return rect;
}

static int contains(RECT rect, int x, int y) {
    POINT point;
    point.x = x;
    point.y = y;
    return PtInRect(&rect, point);
}

static void layout_of(const App *app, Layout *layout) {
    RECT client;
    int width;
    int height;
    int pad;
    int header;
    int footer;
    int gap;
    int button_w;
    GetClientRect(app->hwnd, &client);
    width = client.right;
    height = client.bottom;
    pad = app_px(app, 10);
    header = app_px(app, 36);
    footer = app_px(app, 58);
    layout->header = rect_make(0, 0, width, header);
    layout->close_btn = rect_make(width - app_px(app, 28), app_px(app, 6), app_px(app, 22), app_px(app, 22));
    layout->hide_btn = rect_make(width - app_px(app, 54), app_px(app, 6), app_px(app, 22), app_px(app, 22));
    layout->memory = rect_make(width - app_px(app, 150), 0, app_px(app, 90), header);
    layout->card = rect_make(pad, header + pad, width - pad * 2, height - header - footer - pad * 2);
    layout->footer = rect_make(pad, height - footer, width - pad * 2, footer - pad);
    gap = app_px(app, 6);
    button_w = (layout->footer.right - layout->footer.left - gap * 3) / 4;
    layout->show_btn = layout->footer;
    layout->again_btn = rect_make(layout->footer.left, layout->footer.top, button_w, layout->footer.bottom - layout->footer.top);
    layout->hard_btn = layout->again_btn;
    OffsetRect(&layout->hard_btn, button_w + gap, 0);
    layout->good_btn = layout->hard_btn;
    OffsetRect(&layout->good_btn, button_w + gap, 0);
    layout->easy_btn = layout->good_btn;
    OffsetRect(&layout->easy_btn, button_w + gap, 0);
    layout->import_btn = rect_make(layout->card.left + pad, layout->card.bottom - app_px(app, 72), layout->card.right - layout->card.left - pad * 2, app_px(app, 28));
    layout->deck_btn = layout->import_btn;
    OffsetRect(&layout->deck_btn, 0, app_px(app, 34));
    layout->play_btn = rect_make(layout->card.right - app_px(app, 64), layout->card.top + app_px(app, 8), app_px(app, 56), app_px(app, 22));
}

static void paint_button(HDC hdc, RECT rect, const wchar_t *text, COLORREF fill, COLORREF text_color, HFONT font, int hot) {
    HBRUSH brush;
    HGDIOBJ old_font;
    RECT inner = rect;
    if (hot) {
        fill = RGB(
            (GetRValue(fill) + 24 > 255) ? 255 : GetRValue(fill) + 24,
            (GetGValue(fill) + 24 > 255) ? 255 : GetGValue(fill) + 24,
            (GetBValue(fill) + 24 > 255) ? 255 : GetBValue(fill) + 24);
    }
    brush = CreateSolidBrush(fill);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    old_font = SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old_font);
}

static void paint_utf8(HDC hdc, const char *text, RECT rect, COLORREF color, HFONT font, UINT format) {
    wchar_t *wide = widen_text(text);
    HGDIOBJ old_font;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    old_font = SelectObject(hdc, font);
    if (wide) {
        DrawTextW(hdc, wide, -1, &rect, format);
        free(wide);
    }
    SelectObject(hdc, old_font);
}

static const FfDeckInfo *current_deck(const App *app) {
    int i;
    for (i = 0; i < app->deck_count; i++) {
        if (app->decks[i].id == app->deck_id) {
            return &app->decks[i];
        }
    }
    return NULL;
}

static void paint_header(App *app, HDC hdc, const Layout *layout) {
    const FfDeckInfo *deck = current_deck(app);
    char memory[32];
    RECT title = layout->header;
    unsigned long long mb = app->working_set / (1024ull * 1024ull);
    FillRect(hdc, &layout->header, app->background);
    title.left = app_px(app, 12);
    title.right = layout->memory.left;
    paint_utf8(hdc, deck ? deck->name : "Flash Frame", title, RGB(236, 236, 240), app->font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    snprintf(memory, sizeof(memory), "%llu MB%s", mb, app->heavy ? " +" : "");
    paint_utf8(
        hdc,
        memory,
        layout->memory,
        ff_budget_over(app->heavy, app->working_set) ? RGB(230, 170, 70) : RGB(150, 150, 160),
        app->font_small,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    paint_button(hdc, layout->hide_btn, L"-", RGB(46, 46, 54), RGB(220, 220, 226), app->font_button, app->hot == HIT_HIDE);
    paint_button(hdc, layout->close_btn, L"x", RGB(46, 46, 54), RGB(220, 220, 226), app->font_button, app->hot == HIT_CLOSE);
}

static void paint_decks(App *app, HDC hdc, const Layout *layout) {
    int row_h = app_px(app, 36);
    int visible = (layout->card.bottom - layout->card.top) / row_h;
    int i;
    if (visible < 1) {
        visible = 1;
    }
    for (i = app->deck_scroll; i < app->deck_count && i < app->deck_scroll + visible; i++) {
        RECT row = layout->card;
        RECT label;
        char line[320];
        int new_left;
        HBRUSH row_brush = CreateSolidBrush(app->decks[i].id == app->deck_id ? RGB(48, 64, 88) : RGB(36, 36, 42));
        row.top = layout->card.top + (i - app->deck_scroll) * row_h;
        row.bottom = row.top + row_h - app_px(app, 4);
        FillRect(hdc, &row, row_brush);
        DeleteObject(row_brush);
        new_left = app->decks[i].new_waiting;
        if (new_left > app->decks[i].new_per_day) {
            new_left = app->decks[i].new_per_day;
        }
        label = row;
        label.left += app_px(app, 8);
        label.right -= app_px(app, 8);
        snprintf(line, sizeof(line), "%s   %d review  %d new%s", app->decks[i].name, app->decks[i].review_due + app->decks[i].learn_due, new_left, app->decks[i].heavy ? "  media" : "");
        paint_utf8(hdc, line, label, RGB(236, 236, 240), app->font_small, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

static void paint_review_hint(App *app, HDC hdc, const Layout *layout);

static void paint_card(App *app, HDC hdc, const Layout *layout) {
    HBRUSH card_brush = CreateSolidBrush(RGB(36, 36, 42));
    RECT text = layout->card;
    const char *body = "Import an Anki deck or create your own.";
    FillRect(hdc, &layout->card, card_brush);
    DeleteObject(card_brush);
    InflateRect(&text, -app_px(app, 12), -app_px(app, 12));
    if (app->busy) {
        char status[64];
        snprintf(status, sizeof(status), "Importing %d cards...", app->import_count);
        paint_utf8(hdc, status, text, RGB(236, 236, 240), app->font_body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (app->mode == MODE_DECKS) {
        paint_decks(app, hdc, layout);
        return;
    }
    if (app->mode == MODE_EMPTY) {
        paint_utf8(hdc, body, text, RGB(200, 200, 206), app->font_body, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        paint_button(hdc, layout->import_btn, L"Import deck", RGB(64, 130, 196), RGB(255, 255, 255), app->font_button, app->hot == HIT_IMPORT);
        paint_button(hdc, layout->deck_btn, L"New deck", RGB(58, 58, 66), RGB(236, 236, 240), app->font_button, app->hot == HIT_NEW_DECK);
        return;
    }
    text.bottom -= app_px(app, 20);
    if (!app->card) {
        char line[64];
        int64_t left = app->next_due - (int64_t)time(NULL);
        if (app->next_due > 0 && left > 0) {
            char span[16];
            ff_format_span(left, span, sizeof(span));
            snprintf(line, sizeof(line), "Next card in %s", span);
        } else {
            snprintf(line, sizeof(line), "You're caught up.");
        }
        paint_utf8(hdc, line, text, RGB(200, 200, 206), app->font_body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        paint_review_hint(app, hdc, layout);
        return;
    }
    if (app->image && app->image_w > 0 && app->image_h > 0) {
        int max_h = app_px(app, 110);
        int draw_w = text.right - text.left;
        int draw_h = app->image_h * draw_w / app->image_w;
        HDC memory = CreateCompatibleDC(hdc);
        HGDIOBJ old;
        if (draw_h > max_h) {
            draw_h = max_h;
            draw_w = app->image_w * draw_h / app->image_h;
        }
        old = SelectObject(memory, app->image);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, text.left, text.top, draw_w, draw_h, memory, 0, 0, app->image_w, app->image_h, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
        text.top += draw_h + app_px(app, 8);
    } else if (app->image_hidden) {
        paint_utf8(hdc, "Image skipped to stay inside the memory budget.", text, RGB(230, 170, 70), app->font_small, DT_LEFT | DT_TOP | DT_SINGLELINE);
        text.top += app_px(app, 18);
    }
    if (current_audio(app)) {
        paint_button(hdc, layout->play_btn, L"Play", RGB(58, 58, 66), RGB(236, 236, 240), app->font_small, app->hot == HIT_PLAY);
        if (!(app->image && app->image_w > 0)) {
            text.top += app_px(app, 28);
        }
    }
    body = app->revealed ? (app->card->back ? app->card->back : "") : (app->card->front ? app->card->front : "");
    paint_utf8(hdc, body, text, RGB(236, 236, 240), app->font_body, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    paint_review_hint(app, hdc, layout);
}

static void paint_review_hint(App *app, HDC hdc, const Layout *layout) {
    RECT hint = layout->card;
    const char *text;
    hint.left += app_px(app, 12);
    hint.right -= app_px(app, 8);
    hint.top = hint.bottom - app_px(app, 20);
    if (!app->card) {
        text = "D decks    I import    N new card";
    } else if (!app->revealed) {
        text = "space shows the answer    click the deck name for decks";
    } else {
        text = "space good    1 again  2 hard  3 good  4 easy";
    }
    paint_utf8(hdc, text, hint, RGB(140, 140, 150), app->font_small, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void paint_footer(App *app, HDC hdc, const Layout *layout) {
    FfDelays delays;
    char again[16];
    char hard[16];
    char good[16];
    char easy[16];
    wchar_t again_w[24];
    wchar_t hard_w[24];
    wchar_t good_w[24];
    wchar_t easy_w[24];
    if (app->mode == MODE_DECKS || app->busy) {
        return;
    }
    if (app->mode == MODE_REVIEW && app->card && !app->revealed) {
        paint_button(hdc, layout->show_btn, L"Show answer", RGB(64, 130, 196), RGB(255, 255, 255), app->font_button, app->hot == HIT_SHOW);
        return;
    }
    if (!app->card) {
        paint_button(hdc, layout->show_btn, L"Decks", RGB(58, 58, 66), RGB(236, 236, 240), app->font_button, app->hot == HIT_DECKS);
        return;
    }
    ff_sched_preview(&app->sched, &app->card->sched, (int64_t)time(NULL), &delays);
    ff_format_span(delays.again_secs, again, sizeof(again));
    ff_format_span(delays.hard_secs, hard, sizeof(hard));
    ff_format_span(delays.good_secs, good, sizeof(good));
    ff_format_span(delays.easy_secs, easy, sizeof(easy));
    _snwprintf(again_w, 24, L"Again %hs", again);
    _snwprintf(hard_w, 24, L"Hard %hs", hard);
    _snwprintf(good_w, 24, L"Good %hs", good);
    _snwprintf(easy_w, 24, L"Easy %hs", easy);
    again_w[23] = L'\0';
    hard_w[23] = L'\0';
    good_w[23] = L'\0';
    easy_w[23] = L'\0';
    paint_button(hdc, layout->again_btn, again_w, RGB(176, 64, 64), RGB(255, 255, 255), app->font_small, app->hot == HIT_AGAIN);
    paint_button(hdc, layout->hard_btn, hard_w, RGB(176, 112, 56), RGB(255, 255, 255), app->font_small, app->hot == HIT_HARD);
    paint_button(hdc, layout->good_btn, good_w, RGB(52, 140, 86), RGB(255, 255, 255), app->font_button, app->hot == HIT_GOOD);
    paint_button(hdc, layout->easy_btn, easy_w, RGB(52, 112, 176), RGB(255, 255, 255), app->font_button, app->hot == HIT_EASY);
}

void app_paint(App *app, HDC hdc) {
    RECT client;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    Layout layout;
    GetClientRect(app->hwnd, &client);
    memory = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    old = SelectObject(memory, bitmap);
    FillRect(memory, &client, app->background);
    layout_of(app, &layout);
    paint_header(app, memory, &layout);
    paint_card(app, memory, &layout);
    paint_footer(app, memory, &layout);
    BitBlt(hdc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

int app_hit(App *app, int x, int y) {
    Layout layout;
    int row_h;
    int visible;
    int i;
    layout_of(app, &layout);
    if (contains(layout.close_btn, x, y)) {
        return HIT_CLOSE;
    }
    if (contains(layout.hide_btn, x, y)) {
        return HIT_HIDE;
    }
    if (contains(layout.header, x, y) && app->deck_count > 0 && x < layout.memory.left) {
        return HIT_DECKS;
    }
    if (app->mode == MODE_EMPTY) {
        if (contains(layout.import_btn, x, y)) {
            return HIT_IMPORT;
        }
        if (contains(layout.deck_btn, x, y)) {
            return HIT_NEW_DECK;
        }
    }
    if (app->mode == MODE_DECKS) {
        row_h = app_px(app, 36);
        visible = (layout.card.bottom - layout.card.top) / row_h;
        for (i = app->deck_scroll; i < app->deck_count && i < app->deck_scroll + visible; i++) {
            RECT row = layout.card;
            row.top = layout.card.top + (i - app->deck_scroll) * row_h;
            row.bottom = row.top + row_h;
            if (contains(row, x, y)) {
                return HIT_DECK_BASE + i;
            }
        }
        return HIT_NONE;
    }
    if (current_audio(app) && contains(layout.play_btn, x, y)) {
        return HIT_PLAY;
    }
    if (!app->card && contains(layout.show_btn, x, y)) {
        return HIT_DECKS;
    }
    if (app->card && !app->revealed && contains(layout.show_btn, x, y)) {
        return HIT_SHOW;
    }
    if (app->card && app->revealed) {
        if (contains(layout.again_btn, x, y)) {
            return HIT_AGAIN;
        }
        if (contains(layout.hard_btn, x, y)) {
            return HIT_HARD;
        }
        if (contains(layout.good_btn, x, y)) {
            return HIT_GOOD;
        }
        if (contains(layout.easy_btn, x, y)) {
            return HIT_EASY;
        }
    }
    if (contains(layout.card, x, y)) {
        return HIT_CARD;
    }
    return HIT_NONE;
}

void app_click(App *app, int x, int y) {
    int hit;
    wchar_t path[FF_PATH_MAX];
    if (app->busy) {
        return;
    }
    hit = app_hit(app, x, y);
    if (hit == HIT_HIDE || hit == HIT_CLOSE) {
        ShowWindow(app->hwnd, SW_HIDE);
        return;
    }
    if (hit == HIT_IMPORT) {
        app_import(app);
        return;
    }
    if (hit == HIT_NEW_DECK) {
        app_new_deck(app);
        return;
    }
    if (hit == HIT_DECKS) {
        app->mode = app->mode == MODE_DECKS ? MODE_REVIEW : MODE_DECKS;
        InvalidateRect(app->hwnd, NULL, FALSE);
        return;
    }
    if (hit >= HIT_DECK_BASE && hit - HIT_DECK_BASE < app->deck_count) {
        app_select_deck(app, app->decks[hit - HIT_DECK_BASE].id);
        return;
    }
    if (hit == HIT_SHOW) {
        app_reveal(app);
        return;
    }
    if (hit == HIT_PLAY && current_audio(app) && media_file(app, current_audio(app), path, FF_PATH_MAX)) {
        audio_play(path);
        return;
    }
    if (hit == HIT_AGAIN) {
        app_grade(app, FF_AGAIN);
    } else if (hit == HIT_HARD) {
        app_grade(app, FF_HARD);
    } else if (hit == HIT_GOOD) {
        app_grade(app, FF_GOOD);
    } else     if (hit == HIT_EASY) {
        app_grade(app, FF_EASY);
    }
}

void app_toggle_startup(App *app) {
    app->startup_on = !app->startup_on;
    startup_set(app->startup_on);
    ff_store_meta_set(app->store, "startup", app->startup_on ? "1" : "0");
}
