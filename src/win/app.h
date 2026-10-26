#ifndef FF_APP_H
#define FF_APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "scheduler.h"
#include "storage.h"

enum {
    MODE_EMPTY = 0,
    MODE_DECKS = 1,
    MODE_REVIEW = 2
};

enum {
    HIT_NONE = 0,
    HIT_HIDE = 1,
    HIT_CLOSE = 2,
    HIT_DECKS = 3,
    HIT_SHOW = 4,
    HIT_AGAIN = 5,
    HIT_HARD = 6,
    HIT_GOOD = 7,
    HIT_EASY = 8,
    HIT_IMPORT = 9,
    HIT_NEW_DECK = 10,
    HIT_NEW_CARD = 11,
    HIT_PLAY = 12,
    HIT_CARD = 13,
    HIT_DECK_BASE = 100
};

typedef struct App {
    HWND hwnd;
    HINSTANCE instance;
    FfStore *store;
    FfSchedConfig sched;
    int dpi;
    int heavy;
    int image_hidden;
    unsigned long long working_set;
    int64_t deck_id;
    FfDeckInfo *decks;
    int deck_count;
    int deck_scroll;
    FfStudyCard *card;
    int revealed;
    int mode;
    int hot;
    int busy;
    int modal;
    int import_count;
    int startup_on;
    int warn_media;
    int quitting;
    char db_path[4096];
    char media_root[4096];
    HBITMAP image;
    int image_w;
    int image_h;
    HFONT font_title;
    HFONT font_body;
    HFONT font_small;
    HFONT font_button;
    HBRUSH background;
    DWORD last_trim;
    int tray_added;
    NOTIFYICONDATAW tray;
} App;

int app_init(App *app);
void app_shutdown(App *app);
void app_reload(App *app);
void app_pull_card(App *app);
void app_grade(App *app, int rating);
void app_reveal(App *app);
void app_import(App *app);
void app_new_deck(App *app);
void app_new_card(App *app);
void app_toggle_startup(App *app);
void app_select_deck(App *app, int64_t deck_id);
void app_paint(App *app, HDC hdc);
int app_hit(App *app, int x, int y);
void app_click(App *app, int x, int y);
void app_fonts(App *app);
int app_px(const App *app, int px);

void tray_add(App *app);
void tray_remove(App *app);
void tray_menu(App *app);
HICON make_app_icon(int size);

HBITMAP load_image_file(const wchar_t *path, int *width, int *height);
void audio_stop(void);
void audio_play(const wchar_t *path);
unsigned long long sample_working_set(void);

int startup_enabled(void);
void startup_set(int on);

#endif
