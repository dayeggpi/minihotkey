#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ---- Constants ----
#define MAX_HOTKEYS       256
#define WM_TRAYICON       (WM_USER + 1)
#define WM_SHOW_PILL      (WM_APP + 1)
#define IDM_EXIT          1001
#define IDM_RELOAD        1002
#define HOTKEY_LIST_ID    1999
#define PILL_TIMER_ID     1
#define PILL_TIMEOUT      2500
#define ITEMS_PER_PAGE    10

#define PILL_W_NORMAL     320
#define PILL_H_NORMAL     68
#define PILL_W_LIST       290
#define PILL_H_LIST       390
#define PILL_MARGIN       24
#define PILL_RADIUS       16

#define COL_BG            RGB(28,  28,  32 )
#define COL_KEY_BG        RGB(52,  52,  62 )
#define COL_TEXT          RGB(240, 240, 248)
#define COL_DIM           RGB(160, 162, 175)
#define COL_ACCENT        RGB(108, 166, 255)
#define COL_SEP           RGB(55,  55,  65 )

// ---- Types ----
typedef struct {
    char key_str[64];
    char description[256];
    UINT modifiers;
    UINT vk;
} HotkeyEntry;

// ---- Globals ----
static HotkeyEntry g_hotkeys[MAX_HOTKEYS];
static int         g_hotkey_count      = 0;
static HWND        g_pill_wnd          = NULL;
static HWND        g_main_wnd          = NULL;
static NOTIFYICONDATA g_nid;
static char        g_current_key[64]   = {0};
static char        g_current_desc[256] = {0};
static BOOL        g_list_mode         = FALSE;
static int         g_list_page         = 0;
static int         g_list_total_pages  = 1;
static HHOOK       g_kb_hook           = NULL;
static HICON       g_tray_icon         = NULL;
static UINT        g_repeat_vk         = 0;
static UINT        g_repeat_mods       = 0;
static UINT        g_list_trigger_mods = MOD_CONTROL | MOD_SHIFT;
static UINT        g_list_trigger_vk   = 'G';
static UINT        WM_TASKBARCREATED   = 0;

static const char* MAIN_CLASS = "MiniHotkeyMain";
static const char* PILL_CLASS = "MiniHotkeyPill";

// ---- VK lookup table ----
typedef struct { const char* name; UINT vk; } VKMap;
static const VKMap vk_map[] = {
    {"F1",VK_F1},   {"F2",VK_F2},   {"F3",VK_F3},   {"F4",VK_F4},
    {"F5",VK_F5},   {"F6",VK_F6},   {"F7",VK_F7},   {"F8",VK_F8},
    {"F9",VK_F9},   {"F10",VK_F10}, {"F11",VK_F11}, {"F12",VK_F12},
    {"F13",VK_F13}, {"F14",VK_F14}, {"F15",VK_F15}, {"F16",VK_F16},
    {"F17",VK_F17}, {"F18",VK_F18}, {"F19",VK_F19}, {"F20",VK_F20},
    {"F21",VK_F21}, {"F22",VK_F22}, {"F23",VK_F23}, {"F24",VK_F24},
    {"SPACE",    VK_SPACE},   {"ENTER",   VK_RETURN}, {"RETURN",VK_RETURN},
    {"TAB",      VK_TAB},     {"ESC",     VK_ESCAPE}, {"ESCAPE",VK_ESCAPE},
    {"BACKSPACE",VK_BACK},    {"DELETE",  VK_DELETE}, {"DEL",   VK_DELETE},
    {"INSERT",   VK_INSERT},  {"INS",     VK_INSERT},
    {"HOME",     VK_HOME},    {"END",     VK_END},
    {"PAGEUP",   VK_PRIOR},   {"PGUP",    VK_PRIOR},  {"PRIOR", VK_PRIOR},
    {"PAGEDOWN", VK_NEXT},    {"PGDN",    VK_NEXT},   {"NEXT",  VK_NEXT},
    {"LEFT",     VK_LEFT},    {"RIGHT",   VK_RIGHT},
    {"UP",       VK_UP},      {"DOWN",    VK_DOWN},
    {"PRINTSCREEN",VK_SNAPSHOT},{"PRTSC",VK_SNAPSHOT},
    {"SCROLLLOCK",VK_SCROLL}, {"PAUSE",   VK_PAUSE},
    {"NUMLOCK",  VK_NUMLOCK}, {"CAPSLOCK",VK_CAPITAL},
    {"MINUS",    VK_OEM_MINUS},{"PLUS",   VK_OEM_PLUS},
    {"COMMA",    VK_OEM_COMMA},{"PERIOD", VK_OEM_PERIOD},
    {NULL, 0}
};

// ---- Helpers ----
static void str_upper(char* s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void str_trim(char* s) {
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

static BOOL ParseHotkeyString(const char* str, UINT* mods, UINT* vk) {
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    str_upper(buf);
    str_trim(buf);

    *mods = 0;
    *vk   = 0;

    char* parts[8];
    int   nparts = 0;
    char* tok = strtok(buf, "+");
    while (tok && nparts < 8) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char* end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
        parts[nparts++] = tok;
        tok = strtok(NULL, "+");
    }
    if (nparts == 0) return FALSE;

    for (int i = 0; i < nparts - 1; i++) {
        if      (strcmp(parts[i], "CTRL")    == 0 || strcmp(parts[i], "CONTROL") == 0)
            *mods |= MOD_CONTROL;
        else if (strcmp(parts[i], "SHIFT")   == 0)
            *mods |= MOD_SHIFT;
        else if (strcmp(parts[i], "ALT")     == 0)
            *mods |= MOD_ALT;
        else if (strcmp(parts[i], "WIN")     == 0 || strcmp(parts[i], "WINDOWS") == 0)
            *mods |= MOD_WIN;
        else return FALSE;
    }

    const char* key = parts[nparts - 1];

    if (strlen(key) == 1) {
        char c = key[0];
        if (c >= 'A' && c <= 'Z') { *vk = (UINT)c; return TRUE; }
        if (c >= '0' && c <= '9') { *vk = (UINT)c; return TRUE; }
    }

    for (int i = 0; vk_map[i].name; i++) {
        if (strcmp(key, vk_map[i].name) == 0) {
            *vk = vk_map[i].vk;
            return TRUE;
        }
    }
    return FALSE;
}

// ---- Config parsing ----
static void ParseConfig(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "config.ini");
    else       strcpy(path, "config.ini");

    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';

        char* p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || *p == ';' || *p == '#') continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;

        char key_str[64]  = {0};
        char desc[256]    = {0};

        int klen = (int)(eq - p);
        if (klen >= (int)sizeof(key_str)) klen = sizeof(key_str) - 1;
        strncpy(key_str, p, klen);
        str_trim(key_str);

        char* val = eq + 1;
        while (*val && isspace((unsigned char)*val)) val++;
        if (*val == '"') {
            val++;
            char* eq2 = strchr(val, '"');
            if (eq2) *eq2 = '\0';
        }
        strncpy(desc, val, sizeof(desc) - 1);
        str_trim(desc);

        if (!key_str[0] || !desc[0]) continue;

        char key_upper[64];
        strncpy(key_upper, key_str, sizeof(key_upper) - 1);
        key_upper[sizeof(key_upper) - 1] = '\0';
        str_upper(key_upper);

        if (strcmp(key_upper, "SHORTLIST_TRIGGER") == 0) {
            UINT mods, vk;
            if (ParseHotkeyString(desc, &mods, &vk)) {
                g_list_trigger_mods = mods;
                g_list_trigger_vk   = vk;
            }
            continue;
        }

        if (g_hotkey_count >= MAX_HOTKEYS) break;

        UINT mods, vk;
        if (!ParseHotkeyString(key_str, &mods, &vk)) continue;

        strncpy(g_hotkeys[g_hotkey_count].key_str,     key_str, 63);
        strncpy(g_hotkeys[g_hotkey_count].description, desc,    255);
        g_hotkeys[g_hotkey_count].modifiers = mods;
        g_hotkeys[g_hotkey_count].vk        = vk;
        g_hotkey_count++;
    }
    fclose(f);
}

// ---- Hotkey registration (list toggle only; config keys use LL hook) ----
static void RegisterAllHotkeys(HWND hwnd) {
    RegisterHotKey(hwnd, HOTKEY_LIST_ID, g_list_trigger_mods | MOD_NOREPEAT, g_list_trigger_vk);
}

static void UnregisterAllHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_LIST_ID);
}

// ---- Low-level keyboard hook (observational — never consumes) ----
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        UINT vk = kb->vkCode;

        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (vk == g_repeat_vk) { g_repeat_vk = 0; g_repeat_mods = 0; }
        } else if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            UINT mods = 0;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
            if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
            if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
                mods |= MOD_WIN;

            // Suppress auto-repeat: only fire once per physical key-down
            if (vk == g_repeat_vk && mods == g_repeat_mods)
                return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
            g_repeat_vk   = vk;
            g_repeat_mods = mods;

            for (int i = 0; i < g_hotkey_count; i++) {
                if (g_hotkeys[i].vk == vk && g_hotkeys[i].modifiers == mods) {
                    PostMessageA(g_main_wnd, WM_SHOW_PILL, (WPARAM)i, 0);
                    break;
                }
            }
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

// ---- Pill position + shape ----
static void PositionPill(BOOL list_mode) {
    int w = list_mode ? PILL_W_LIST : PILL_W_NORMAL;
    int h = list_mode ? PILL_H_LIST : PILL_H_NORMAL;

    RECT wa;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right  - w - PILL_MARGIN;
    int y = wa.bottom - h - PILL_MARGIN;

    SetWindowPos(g_pill_wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, PILL_RADIUS * 2, PILL_RADIUS * 2);
    SetWindowRgn(g_pill_wnd, rgn, FALSE);
}

static void HidePill(void) {
    ShowWindow(g_pill_wnd, SW_HIDE);
    KillTimer(g_main_wnd, PILL_TIMER_ID);
}

static void ShowPill(const char* key, const char* desc) {
    g_list_mode = FALSE;
    strncpy(g_current_key,  key,  sizeof(g_current_key)  - 1);
    strncpy(g_current_desc, desc, sizeof(g_current_desc) - 1);

    PositionPill(FALSE);
    InvalidateRect(g_pill_wnd, NULL, TRUE);
    ShowWindow(g_pill_wnd, SW_SHOWNOACTIVATE);
    SetTimer(g_main_wnd, PILL_TIMER_ID, PILL_TIMEOUT, NULL);
}

static void ShowListPill(void) {
    if (IsWindowVisible(g_pill_wnd) && g_list_mode) {
        HidePill();
        return;
    }
    KillTimer(g_main_wnd, PILL_TIMER_ID);
    g_list_mode        = TRUE;
    g_list_total_pages = (g_hotkey_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (g_list_total_pages < 1) g_list_total_pages = 1;
    if (g_list_page >= g_list_total_pages) g_list_page = 0;

    PositionPill(TRUE);
    InvalidateRect(g_pill_wnd, NULL, TRUE);
    ShowWindow(g_pill_wnd, SW_SHOWNOACTIVATE);
}

// ---- GDI helpers ----
static HFONT MakeFont(int size, int weight, const char* face) {
    return CreateFontA(size, 0, 0, 0, weight, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ---- Drawing ----
static void DrawNormalPill(HDC hdc, int w, int h) {
    RECT rc = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HFONT fk = MakeFont(15, FW_BOLD,   "Segoe UI");
    HFONT fd = MakeFont(14, FW_NORMAL, "Segoe UI");

    SetBkMode(hdc, TRANSPARENT);

    // Measure key badge
    SelectObject(hdc, fk);
    SIZE ks;
    GetTextExtentPoint32A(hdc, g_current_key, (int)strlen(g_current_key), &ks);

    int pad  = 8;
    int padx = 10;
    int bw   = ks.cx + padx * 2;
    int bh   = h - pad * 2;
    int bx   = pad;
    int by   = pad;

    HBRUSH kb = CreateSolidBrush(COL_KEY_BG);
    RECT br = {bx, by, bx + bw, by + bh};
    FillRect(hdc, &br, kb);
    DeleteObject(kb);

    SetTextColor(hdc, COL_ACCENT);
    TextOutA(hdc, bx + padx, by + (bh - ks.cy) / 2,
             g_current_key, (int)strlen(g_current_key));

    // Description
    SelectObject(hdc, fd);
    SetTextColor(hdc, COL_TEXT);
    int dx = bx + bw + 12;
    RECT dr = {dx, 0, w - pad, h};
    DrawTextA(hdc, g_current_desc, -1, &dr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DeleteObject(fk);
    DeleteObject(fd);
}

static void DrawListPill(HDC hdc, int w, int h) {
    RECT rc = {0, 0, w, h};
    HBRUSH bg = CreateSolidBrush(COL_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);

    HFONT ft  = MakeFont(14, FW_BOLD,   "Segoe UI");
    HFONT fk  = MakeFont(12, FW_BOLD,   "Segoe UI");
    HFONT fd  = MakeFont(12, FW_NORMAL, "Segoe UI");
    HFONT fn  = MakeFont(13, FW_BOLD,   "Segoe UI");

    int mx = 14;
    int y  = 12;

    // Title
    SelectObject(hdc, ft);
    SetTextColor(hdc, COL_TEXT);
    RECT tr = {mx, y, w - mx, y + 20};
    DrawTextA(hdc, "Shortcuts", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    y += 24;

    // Separator
    HPEN sp = CreatePen(PS_SOLID, 1, COL_SEP);
    HPEN op = (HPEN)SelectObject(hdc, sp);
    MoveToEx(hdc, mx, y, NULL);
    LineTo(hdc, w - mx, y);
    SelectObject(hdc, op);
    DeleteObject(sp);
    y += 8;

    // Items
    int start = g_list_page * ITEMS_PER_PAGE;
    int end   = start + ITEMS_PER_PAGE;
    if (end > g_hotkey_count) end = g_hotkey_count;

    for (int i = start; i < end; i++) {
        SelectObject(hdc, fk);
        SIZE ks;
        GetTextExtentPoint32A(hdc, g_hotkeys[i].key_str,
                              (int)strlen(g_hotkeys[i].key_str), &ks);

        int bw = ks.cx + 8;
        int ih = 24;

        HBRUSH kb = CreateSolidBrush(COL_KEY_BG);
        RECT br = {mx, y + 2, mx + bw, y + ih - 2};
        FillRect(hdc, &br, kb);
        DeleteObject(kb);

        SetTextColor(hdc, COL_ACCENT);
        TextOutA(hdc, mx + 4, y + (ih - ks.cy) / 2,
                 g_hotkeys[i].key_str, (int)strlen(g_hotkeys[i].key_str));

        SelectObject(hdc, fd);
        SetTextColor(hdc, COL_DIM);
        int dx = mx + bw + 8;
        RECT dr = {dx, y, w - mx, y + ih};
        DrawTextA(hdc, g_hotkeys[i].description, -1, &dr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        y += ih + 3;
    }

    if (g_hotkey_count == 0) {
        SelectObject(hdc, fd);
        SetTextColor(hdc, COL_DIM);
        RECT er = {mx, y, w - mx, y + 30};
        DrawTextA(hdc, "No shortcuts in config.ini", -1, &er,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Pagination footer
    if (g_list_total_pages > 1) {
        HPEN sp2 = CreatePen(PS_SOLID, 1, COL_SEP);
        HPEN op2 = (HPEN)SelectObject(hdc, sp2);
        MoveToEx(hdc, mx, h - 32, NULL);
        LineTo(hdc, w - mx, h - 32);
        SelectObject(hdc, op2);
        DeleteObject(sp2);

        char ps[32];
        snprintf(ps, sizeof(ps), "\x3c  %d / %d  \x3e",
                 g_list_page + 1, g_list_total_pages);
        SelectObject(hdc, fn);
        SetTextColor(hdc, COL_DIM);
        RECT nr = {0, h - 30, w, h - 6};
        DrawTextA(hdc, ps, -1, &nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(ft);
    DeleteObject(fk);
    DeleteObject(fd);
    DeleteObject(fn);
}

// ---- Pill window proc ----
LRESULT CALLBACK PillWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC     mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

        if (g_list_mode) DrawListPill(mem, rc.right, rc.bottom);
        else             DrawNormalPill(mem, rc.right, rc.bottom);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (g_list_mode) {
            if (g_list_total_pages > 1) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int x = (int)(short)LOWORD(lParam);
                int y = (int)(short)HIWORD(lParam);
                if (y > rc.bottom - 32) {
                    if (x < rc.right / 2) {
                        g_list_page = (g_list_page > 0)
                                      ? g_list_page - 1
                                      : g_list_total_pages - 1;
                    } else {
                        g_list_page = (g_list_page + 1) % g_list_total_pages;
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
        } else {
            HidePill();
        }
        return 0;

    case WM_RBUTTONDOWN:
        HidePill();
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---- Tray ----
static void TrayInit(HWND hwnd) {
    g_tray_icon = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1),
                                    IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!g_tray_icon) g_tray_icon = LoadIconA(NULL, IDI_APPLICATION);

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATA);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_tray_icon;
    strcpy(g_nid.szTip, "MiniHotkey");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void TrayRemove(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

// ---- Main window proc ----
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (WM_TASKBARCREATED && msg == WM_TASKBARCREATED) {
        Shell_NotifyIconA(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
    case WM_HOTKEY:
        if ((int)wParam == HOTKEY_LIST_ID) ShowListPill();
        return 0;

    case WM_SHOW_PILL: {
        int idx = (int)wParam;
        if (idx >= 0 && idx < g_hotkey_count)
            ShowPill(g_hotkeys[idx].key_str, g_hotkeys[idx].description);
        return 0;
    }
    case WM_TIMER:
        if (wParam == PILL_TIMER_ID) HidePill();
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING,    IDM_RELOAD, "Reload config");
            AppendMenuA(menu, MF_SEPARATOR, 0,          NULL);
            AppendMenuA(menu, MF_STRING,    IDM_EXIT,   "Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_RELOAD) {
            UnregisterAllHotkeys(hwnd);
            g_hotkey_count      = 0;
            g_list_trigger_mods = MOD_CONTROL | MOD_SHIFT;
            g_list_trigger_vk   = 'G';
            memset(g_hotkeys, 0, sizeof(g_hotkeys));
            ParseConfig();
            RegisterAllHotkeys(hwnd);
        } else if (LOWORD(wParam) == IDM_EXIT) {
            HidePill();
            TrayRemove();
            UnregisterAllHotkeys(hwnd);
            if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = NULL; }
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---- Entry ----
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd; (void)nShow;

    // Single instance guard
    HANDLE mutex = CreateMutexA(NULL, TRUE, "MiniHotkeyMutex_v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();
    ParseConfig();

    // Register window classes
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassA(&wc);

    WNDCLASSA pc = {0};
    pc.lpfnWndProc   = PillWndProc;
    pc.hInstance     = hInst;
    pc.hbrBackground = NULL;
    pc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    pc.lpszClassName = PILL_CLASS;
    RegisterClassA(&pc);

    // Hidden message-pump window
    g_main_wnd = CreateWindowExA(0, MAIN_CLASS, "MiniHotkey", WS_POPUP,
                                 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    // Pill window (hidden initially)
    g_pill_wnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        PILL_CLASS, "",
        WS_POPUP,
        0, 0, PILL_W_NORMAL, PILL_H_NORMAL,
        NULL, NULL, hInst, NULL);

    TrayInit(g_main_wnd);
    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");
    RegisterAllHotkeys(g_main_wnd);
    g_kb_hook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    CloseHandle(mutex);
    return 0;
}
