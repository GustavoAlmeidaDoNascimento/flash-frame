#include "modal.h"

#include <stdlib.h>
#include <string.h>
#include <windowsx.h>

static HBRUSH dialog_brush(void) {
    static HBRUSH brush;
    if (!brush) {
        brush = CreateSolidBrush(RGB(28, 28, 32));
    }
    return brush;
}

HBRUSH field_brush(void) {
    static HBRUSH brush;
    if (!brush) {
        brush = CreateSolidBrush(RGB(20, 20, 24));
    }
    return brush;
}

void show_utf8(HWND owner, const char *text) {
    wchar_t wide[768];
    if (!text || !text[0]) {
        text = "Something went wrong.";
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 768) <= 0) {
        MessageBoxW(owner, L"Something went wrong.", L"Flash Frame", MB_OK | MB_ICONWARNING);
        return;
    }
    MessageBoxW(owner, wide, L"Flash Frame", MB_OK | MB_ICONWARNING);
}

static WNDPROC edit_original;

static LRESULT CALLBACK edit_key_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        SendMessageW(GetParent(hwnd), WM_KEYDOWN, VK_ESCAPE, lp);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN && (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) == 0) {
        SendMessageW(GetParent(hwnd), WM_KEYDOWN, VK_RETURN, lp);
        return 0;
    }
    return CallWindowProcW(edit_original, hwnd, msg, wp, lp);
}

void hook_edit(HWND edit) {
    WNDPROC previous;
    if (!edit) {
        return;
    }
    previous = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)edit_key_proc);
    if (!edit_original) {
        edit_original = previous;
    }
}

void modal_loop(App *app, HWND owner, HWND dialog, HWND focus) {
    MSG message;
    app->modal++;
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    if (focus) {
        SetFocus(focus);
    }
    while (IsWindow(dialog)) {
        int rc = GetMessageW(&message, NULL, 0, 0);
        if (rc <= 0) {
            if (rc == 0) {
                PostQuitMessage((int)message.wParam);
            }
            break;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    app->modal--;
    if (IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

ATOM popup_class(const wchar_t *name, WNDPROC proc) {
    WNDCLASSW window_class;
    ATOM atom;
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = proc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.lpszClassName = name;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = dialog_brush();
    atom = RegisterClassW(&window_class);
    if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        atom = 1;
    }
    return atom;
}

void paint_dialog_button(HDC hdc, RECT rect, const wchar_t *text, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    RECT inner = rect;
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, text, -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

int point_in(RECT rect, LPARAM lp) {
    POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    return PtInRect(&rect, point);
}
