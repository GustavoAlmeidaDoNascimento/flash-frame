#ifndef FF_MODAL_H
#define FF_MODAL_H

#include "app.h"
#include "apkg.h"

HBRUSH field_brush(void);
void show_utf8(HWND owner, const char *text);
void hook_edit(HWND edit);
void modal_loop(App *app, HWND owner, HWND dialog, HWND focus);
ATOM popup_class(const wchar_t *name, WNDPROC proc);
void paint_dialog_button(HDC hdc, RECT rect, const wchar_t *text, COLORREF fill);
int point_in(RECT rect, LPARAM lp);
int confirm_media(App *app, HWND owner, const FfMediaScan *scan, FfWarnChoice *choice, const wchar_t *ok_label);
void remember_warning(App *app, const FfWarnChoice *choice);

#endif
