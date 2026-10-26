// QuickLinks - desktop widget of shortcuts to folders, files and web pages (docs/설계.md).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "links.h"
#include "shell.h"

#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shcore.lib")
#endif

namespace {

enum : UINT { WM_APP_ICON = WM_APP + 1, WM_APP_OPENED, WM_APP_TRAY, WM_APP_SHOW, WM_APP_QUIT };
enum : UINT_PTR { kNoticeTimer = 1, kScrollTimer, kExpandTimer, kRecheckTimer, kClockTimer };

// Command ids. tools\qltool.cpp posts some of these (cmd <id>); keep the numbers stable.
enum : int {
    IDM_OPEN = 100,
    IDM_LOCATION,
    IDM_COPY,
    IDM_RENAME,
    IDM_EDITPATH,
    IDM_REMOVE,
    IDM_NEWGROUP,
    IDM_RENAMEGROUP,
    IDM_GROUPUP,
    IDM_GROUPDOWN,
    IDM_DELETEGROUP,
    IDM_ADD,  // 111: open/close the add row
    IDM_PASTE,
    IDM_TOGGLE,
    IDM_TOPMOST,
    IDM_SINGLECLICK,
    IDM_DOUBLECLICK,
    IDM_EXIT,
    IDM_UNDO,  // 118
    IDM_REDO,  // 119
    IDM_SELECTALL,  // 120
    IDM_THEME_SYSTEM,  // 121
    IDM_THEME_LIGHT,   // 122
    IDM_THEME_DARK,    // 123
    IDM_TRANSPARENCY,  // 124: the transparency slider
    IDM_CLOCK_OFF,     // 125
    IDM_CLOCK_LARGE,   // 126  시안 B
    IDM_CLOCK_LINE,    // 127  시안 C
    IDM_CLOCK_CARD,    // 128  시안 D
    IDM_CLOCK_24H,     // 129
    IDM_CLOCK_SECONDS, // 130
    IDM_CLOCK_TOP,     // 131
    IDM_CLOCK_BOTTOM,  // 132
    IDM_MOVETO_NEW = 999,
    IDM_MOVETO = 1000,  // + group index
};

constexpr wchar_t kClass[] = L"QuickLinksWidget";
constexpr wchar_t kAppName[] = L"QuickLinks";

// Layout in DIPs (docs/설계.md 2.5).
constexpr float kHeaderH = 36, kFieldTop = 38, kFieldH = 30, kItemH = 32, kGroupH = 28, kGroupGap = 4;
constexpr float kInset = 6, kBorder = 6, kDragStart = 4, kNoticeInfoH = 36, kNoticeWarnH = 82;
constexpr UINT kNoticeMs = 5000, kExpandMs = 600, kRecheckMs = 5 * 60 * 1000;
constexpr float kClockLargeH = 60, kClockCardH = 64, kClockLineH = 30;
constexpr int kMaxTransparency = 50;  // percent; more and the text is hard to read on a busy desktop

// Segoe Fluent Icons code points (the same in Segoe MDL2 Assets, used on Windows 10).
constexpr wchar_t kGlyphApp = 0xE8A4, kGlyphPin = 0xE718, kGlyphPinned = 0xE840, kGlyphAdd = 0xE710,
                  kGlyphClose = 0xE711, kGlyphChromeClose = 0xE8BB, kGlyphSearch = 0xE721, kGlyphDown = 0xE96E,
                  kGlyphRight = 0xE970, kGlyphMore = 0xE712, kGlyphWarn = 0xE7BA, kGlyphGlobe = 0xE774,
                  kGlyphFolder = 0xE8B7, kGlyphFile = 0xE8A5, kGlyphUnknown = 0xE9CE, kGlyphNewFolder = 0xE8F4,
                  kGlyphLink = 0xE71B, kGlyphCalendar = 0xE787;

D2D1_COLOR_F Hex(UINT rgb, float a = 1) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}
D2D1_COLOR_F Fade(D2D1_COLOR_F c, float a) { return D2D1::ColorF(c.r, c.g, c.b, c.a * a); }
COLORREF ToRef(D2D1_COLOR_F c) { return RGB((int)(c.r * 255 + .5f), (int)(c.g * 255 + .5f), (int)(c.b * 255 + .5f)); }

struct Theme {
    bool dark;
    D2D1_COLOR_F bg, text, text2, text3, hover, selected, accent, onAccent, field, fieldBorder, underline, warn,
        info, infoBorder, warnBg, warnBorder, button, buttonBorder, ghost, drop, scroll, folder;
};

Theme LightTheme() {
    return {false,           Hex(0xF3F3F3),       Hex(0x1A1A1A),       Hex(0x5F6368),       Hex(0x8A8A8A),
            Hex(0, .037f),   Hex(0, .055f),       Hex(0x005FB8),       Hex(0xFFFFFF),       Hex(0xFBFBFB),
            Hex(0xE5E5E5),   Hex(0x8A8A8A),       Hex(0x9D5D00),       Hex(0xFBFBFB),       Hex(0xE5E5E5),
            Hex(0xFFF4CE),   Hex(0x9D5D00, .2f),  Hex(0xFBFBFB),       Hex(0xE2E2E2),       Hex(0xFFFFFF, .92f),
            Hex(0x005FB8, .06f), Hex(0, .3f),     Hex(0xD9A033)};
}

Theme DarkTheme() {
    return {true,              Hex(0x202020),      Hex(0xFFFFFF),       Hex(0xC8C8C8),       Hex(0x9A9A9A),
            Hex(0xFFFFFF, .06f), Hex(0xFFFFFF, .083f), Hex(0x60CDFF),    Hex(0x000000),       Hex(0x2B2B2B),
            Hex(0x383838),     Hex(0x9A9A9A),      Hex(0xFCE100),       Hex(0x2B2B2B),       Hex(0x383838),
            Hex(0x433519),     Hex(0xFCE100, .2f), Hex(0x2D2D2D),       Hex(0x3A3A3A),       Hex(0x2B2B2B, .95f),
            Hex(0x60CDFF, .08f), Hex(0xFFFFFF, .3f), Hex(0xE8B04A)};
}

int g_forceTheme = -1;  // --theme light|dark (test instances): 0 light, 1 dark

bool SystemUsesDark() {
    DWORD v = 1, size = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

// ---- globals --------------------------------------------------------------------------------

HINSTANCE g_inst;
HWND g_wnd, g_search, g_addEdit, g_renameEdit, g_tip;
ql::Store g_store;
std::wstring g_iniPath;
bool g_testInstance = false;
Theme g_theme = LightTheme();
UINT g_dpi = 96;
UINT g_taskbarCreated;
HICON g_iconSmall, g_iconBig;

ID2D1Factory* g_d2d;
IDWriteFactory* g_dw;
IWICImagingFactory* g_wic;
ID2D1DCRenderTarget* g_rt;
ID2D1SolidColorBrush* g_brush;
IDWriteTextFormat *g_fItem, *g_fCaption, *g_fCaptionBold, *g_fButton, *g_fGlyph16, *g_fGlyph12, *g_fGlyph22,
    *g_fEmptyTitle, *g_fEmptyBody, *g_fSliderValue, *g_fClockBig, *g_fClockCard, *g_fClockSmall;
ID2D1StrokeStyle* g_roundStroke;  // clock hands
HFONT g_editFont, g_renameFont;
HBRUSH g_fieldBrush;

shell::IconWorker g_worker;
struct IconEntry {
    IWICBitmap* wic = nullptr;
    ID2D1Bitmap* d2d = nullptr;
    bool real = false;
    shell::State state = shell::State::Unknown;
    bool directory = false;
};
std::map<std::wstring, IconEntry> g_icons;

std::vector<ql::Row> g_rows;
std::wstring g_filter;
float g_scroll = 0;

struct Selection {
    int link = -1;   // a link row, or
    int group = -1;  // a group header row (link == -1)
} g_sel;  // the focused row; change it through SetSel()
// Ctrl/Shift multi-selection, by target (indices shift when the list changes). Empty = only g_sel.
std::set<std::wstring> g_picked;
std::wstring g_anchor;  // target where a Shift range starts

enum class Zone { None, Pin, Plus, Close, Row, RowMore, AddFolder, AddFile, EmptyAdd, EmptyPaste, Notice, NoticeAction, NoticeClose, List };
struct Hit {
    Zone zone = Zone::None;
    int row = -1;  // Row/RowMore: index into g_rows; NoticeAction: action index
    bool operator==(const Hit& o) const { return zone == o.zone && row == o.row; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};
Hit g_hot, g_pressed;
POINT g_pressPt;             // client px
bool g_pressPicked = false;  // the press was a Ctrl/Shift selection click: don't open on release
bool g_collapseOnUp = false; // plain press on a multi-selected item: narrow to it on release
bool g_mouseIn = false;

struct Layout {
    float W = 0, H = 0, listTop = 0, listBottom = 0, contentH = 0, fieldTop = 0;
    D2D1_RECT_F clock{};  // empty when the clock is off
    bool clockAtTop = true;
    D2D1_RECT_F pin{}, plus{}, close{}, field{}, addFolder{}, addFile{}, hint{}, emptyAdd{}, emptyPaste{};
    std::vector<float> rowTop;
} L;

bool g_addOpen = false;
int g_editPathLink = -1;  // the add row edits this link's target instead of adding

enum class Rename { None, Link, Group };
Rename g_renameKind = Rename::None;
int g_renameIndex = -1;

struct DropAt {
    int group = 0;
    int before = -1;     // link index to insert before, -1 = end of group
    float lineY = -1;    // content y of the insertion line, -1 = none
    int headerRow = -1;  // collapsed header under the cursor
};
struct Drag {
    bool active = false;
    int link = -1;
    float grabDy = 0;  // cursor y minus row top, DIPs
    POINT pt{};        // client px
    DropAt at;
} g_drag;
struct ExternalDrop {
    bool active = false;
    std::vector<shell::Dropped> items;
    POINT pt{};
    DropAt at;
} g_ext;
int g_expandGroup = -1;

struct Notice {
    bool on = false, warn = false;
    std::wstring title, detail, target;
    std::vector<std::pair<std::wstring, int>> actions;  // label, id
} g_notice;
enum : int { kActUndo = 1, kActEditPath, kActRemoveMissing };
std::vector<std::pair<D2D1_RECT_F, int>> g_noticeHits;  // filled while painting
D2D1_RECT_F g_noticeRect{}, g_noticeClose{};
// Ctrl+Z / Ctrl+Y: whole-list snapshots taken before each change to groups or links.
struct Snapshot {
    std::vector<ql::Group> groups;
    std::vector<ql::Link> links;
    Selection sel;
    std::set<std::wstring> picked;
};
std::vector<Snapshot> g_undoStack, g_redoStack;
constexpr size_t kMaxHistory = 50;

std::wstring g_tipText;
int g_tipRow = -2;

// ---- small helpers --------------------------------------------------------------------------

float Scale() { return g_dpi / 96.0f; }
float Dip(int px) { return px / Scale(); }
int Px(float dip) { return (int)std::lround(dip * Scale()); }
bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
float RowH(const ql::Row& r) { return r.type == ql::Row::GroupHeader ? kGroupH : kItemH; }
bool HasHeaders() { return g_store.groups.size() > 1; }

void Checkpoint();

void Save() {
    if (!g_store.Save(g_iniPath)) {
        g_notice = {true, false, L"목록을 저장하지 못했어요", L"", L"", {}};
        SetTimer(g_wnd, kNoticeTimer, kNoticeMs, nullptr);
    }
}

std::wstring EditText(HWND e) {
    int n = GetWindowTextLengthW(e);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(e, &s[0], n + 1);
    return s;
}

void SetPreferredMenuTheme(bool dark) {
    // uxtheme ordinals 135 (SetPreferredAppMode) and 136 (FlushMenuThemes): undocumented but stable
    // since Windows 10 1903; without them popup menus stay light in dark mode.
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetMode = int(WINAPI*)(int);
    using Flush = void(WINAPI*)();
    auto set = (SetMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto flush = (Flush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (set) set(dark ? 2 : 3);  // ForceDark / ForceLight
    if (flush) flush();
}

// Single selection: drops any multi-selection and restarts Shift ranges here.
void SetSel(const Selection& s) {
    g_sel = s;
    g_picked.clear();
    g_anchor = s.link >= 0 && s.link < (int)g_store.links.size() ? g_store.links[s.link].target : L"";
}

bool IsPicked(int link) {
    if (g_picked.empty()) return g_sel.link == link;
    return g_picked.count(g_store.links[link].target) > 0;
}

// Selected links in screen order (the focused one alone when nothing is multi-selected). Picks hidden
// by a collapsed group or the search filter are left out, so Delete never hits rows you can't see.
std::vector<int> SelectedLinks() {
    std::vector<int> out;
    if (g_picked.empty()) {
        if (g_sel.link >= 0 && g_sel.link < (int)g_store.links.size()) out.push_back(g_sel.link);
        return out;
    }
    for (auto& r : g_rows)
        if (r.type == ql::Row::Item && IsPicked(r.link)) out.push_back(r.link);
    return out;
}

// Call right before changing groups or links.
void Checkpoint() {
    g_undoStack.push_back({g_store.groups, g_store.links, g_sel, g_picked});
    if (g_undoStack.size() > kMaxHistory) g_undoStack.erase(g_undoStack.begin());
    g_redoStack.clear();
}

// ---- Direct2D resources ---------------------------------------------------------------------

IDWriteTextFormat* MakeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, bool center, bool wrap) {
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                           L"ko-kr", &f);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject* ellipsis = nullptr;
        g_dw->CreateEllipsisTrimmingSign(f, &ellipsis);
        f->SetTrimming(&trim, ellipsis);
        if (ellipsis) ellipsis->Release();
    }
    return f;
}

// Windows 11 fonts, or their Windows 10 counterparts when missing. Segoe MDL2 Assets has every
// glyph used here at the same code point; Segoe UI with a light weight stands in for Variable Display.
struct Fonts {
    std::wstring ui, display, icons;
} g_fonts;
bool g_forceWin10Fonts = false;  // --win10-fonts (test instances): pretend the Windows 11 fonts are missing

bool HasFontFamily(const wchar_t* name) {
    if (g_forceWin10Fonts) return false;
    IDWriteFontCollection* fonts = nullptr;
    BOOL exists = FALSE;
    UINT32 index = 0;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&fonts))) {
        fonts->FindFamilyName(name, &index, &exists);
        fonts->Release();
    }
    return exists != FALSE;
}

void PickFonts() {
    g_fonts.ui = HasFontFamily(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    g_fonts.display = HasFontFamily(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    g_fonts.icons = HasFontFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
}

void CreateFormats() {
    PickFonts();
    const wchar_t* ui = g_fonts.ui.c_str();
    const wchar_t* icons = g_fonts.icons.c_str();
    g_fItem = MakeFormat(ui, 14, DWRITE_FONT_WEIGHT_REGULAR, false, false);
    g_fCaption = MakeFormat(ui, 12, DWRITE_FONT_WEIGHT_REGULAR, false, false);
    g_fCaptionBold = MakeFormat(ui, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false);
    g_fButton = MakeFormat(ui, 13, DWRITE_FONT_WEIGHT_REGULAR, true, false);
    g_fGlyph16 = MakeFormat(icons, 16, DWRITE_FONT_WEIGHT_REGULAR, true, false);
    g_fGlyph12 = MakeFormat(icons, 12, DWRITE_FONT_WEIGHT_REGULAR, true, false);
    g_fGlyph22 = MakeFormat(icons, 22, DWRITE_FONT_WEIGHT_REGULAR, true, false);
    g_fEmptyTitle = MakeFormat(ui, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, true);
    g_fEmptyBody = MakeFormat(ui, 12, DWRITE_FONT_WEIGHT_REGULAR, true, true);
    g_fSliderValue = MakeFormat(ui, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false);
    g_fSliderValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    g_fClockBig = MakeFormat(g_fonts.display.c_str(), 38, DWRITE_FONT_WEIGHT_LIGHT, false, false);
    g_fClockCard = MakeFormat(g_fonts.display.c_str(), 20, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false);
    g_fClockSmall = MakeFormat(ui, 13, DWRITE_FONT_WEIGHT_REGULAR, false, false);
    g_d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND),
                             nullptr, 0, &g_roundStroke);
}

void ReleaseBitmaps(bool alsoWic) {
    for (auto& [k, e] : g_icons) {
        if (e.d2d) e.d2d->Release(), e.d2d = nullptr;
        if (alsoWic && e.wic) e.wic->Release(), e.wic = nullptr;
    }
    if (alsoWic) g_icons.clear();
}

void EnsureTarget() {
    if (g_rt) return;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    g_d2d->CreateDCRenderTarget(&props, &g_rt);
    g_rt->CreateSolidColorBrush(g_theme.text, &g_brush);
}

void ReleaseTarget() {
    ReleaseBitmaps(false);
    if (g_brush) g_brush->Release(), g_brush = nullptr;
    if (g_rt) g_rt->Release(), g_rt = nullptr;
}

ID2D1Bitmap* IconBitmap(IconEntry& e) {
    if (e.d2d || !e.wic) return e.d2d;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(e.wic, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)))
        g_rt->CreateBitmapFromWicBitmap(conv, nullptr, &e.d2d);
    if (conv) conv->Release();
    return e.d2d;
}

void Fill(const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius = 0) {
    g_brush->SetColor(c);
    if (radius > 0) g_rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), g_brush);
    else g_rt->FillRectangle(r, g_brush);
}

void Stroke(const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius, float width = 1) {
    g_brush->SetColor(c);
    D2D1_RECT_F in{r.left + width / 2, r.top + width / 2, r.right - width / 2, r.bottom - width / 2};
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(in, radius, radius), g_brush, width);
}

void Text(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
    if (r.right <= r.left) return;
    g_brush->SetColor(c);
    g_rt->DrawText(s.c_str(), (UINT32)s.size(), f, r, g_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Glyph(wchar_t g, IDWriteTextFormat* f, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
    // Thin icon strokes get colored fringes with ClearType.
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    Text(std::wstring(1, g), f, r, c);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

float TextWidth(const std::wstring& s, IDWriteTextFormat* f) {
    IDWriteTextLayout* lay = nullptr;
    float w = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000, 100, &lay))) {
        DWRITE_TEXT_METRICS m{};
        lay->GetMetrics(&m);
        w = m.widthIncludingTrailingWhitespace;
        lay->Release();
    }
    return w;
}

void Button(const D2D1_RECT_F& r, const std::wstring& label, wchar_t glyph, bool primary, bool hot) {
    if (primary) {
        Fill(r, hot ? Fade(g_theme.accent, .9f) : g_theme.accent, 4);
    } else {
        Fill(r, g_theme.button, 4);
        Stroke(r, g_theme.buttonBorder, 4);
        if (hot) Fill(r, g_theme.hover, 4);
    }
    D2D1_COLOR_F c = primary ? g_theme.onAccent : g_theme.text;
    if (glyph) {
        float w = TextWidth(label, g_fButton) + 22;
        float x = (r.left + r.right - w) / 2;
        Glyph(glyph, g_fGlyph12, {x, r.top, x + 14, r.bottom}, c);
        Text(label, g_fButton, {x + 20, r.top, x + w + 4, r.bottom}, c);
    } else {
        Text(label, g_fButton, r, c);
    }
}

// App and tray icon: an accent rounded square with a white bookmark, drawn at runtime.
HICON MakeAppIcon(int px) {
    IWICBitmap* bmp = nullptr;
    g_wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    ID2D1RenderTarget* rt = nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    g_d2d->CreateWicBitmapRenderTarget(bmp, &props, &rt);
    ID2D1SolidColorBrush* b = nullptr;
    rt->CreateSolidColorBrush(Hex(0x005FB8), &b);
    float s = (float)px;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(s * .03f, s * .03f, s * .97f, s * .97f), s * .22f, s * .22f), b);
    b->SetColor(Hex(0xFFFFFF));
    ID2D1PathGeometry* geo = nullptr;
    g_d2d->CreatePathGeometry(&geo);
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(D2D1::Point2F(s * .30f, s * .20f), D2D1_FIGURE_BEGIN_FILLED);
    D2D1_POINT_2F pts[] = {{s * .70f, s * .20f}, {s * .70f, s * .82f}, {s * .50f, s * .66f}, {s * .30f, s * .82f}};
    sink->AddLines(pts, 4);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(geo, b);
    rt->EndDraw();
    sink->Release();
    geo->Release();
    b->Release();
    rt->Release();

    std::vector<BYTE> pixels(px * px * 4);
    bmp->CopyPixels(nullptr, px * 4, (UINT)pixels.size(), pixels.data());
    bmp->Release();
    for (size_t i = 0; i < pixels.size(); i += 4) {  // icons want straight alpha
        BYTE a = pixels[i + 3];
        if (a && a < 255)
            for (int c = 0; c < 3; ++c) pixels[i + c] = (BYTE)std::min(255, pixels[i + c] * 255 / a);
    }
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = px;
    bi.bV5Height = -px;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    memcpy(bits, pixels.data(), pixels.size());
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

// ---- layout ---------------------------------------------------------------------------------

float ContentToClient(float y) { return L.listTop + y - g_scroll; }

void ClampScroll() {
    float maxScroll = std::max(0.f, L.contentH - (L.listBottom - L.listTop));
    g_scroll = std::clamp(g_scroll, 0.f, maxScroll);
}

void Relayout() {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    L.W = Dip(rc.right);
    L.H = Dip(rc.bottom);
    float bx = L.W - 8 - 28;
    L.close = {bx, 4, bx + 28, 32};
    L.plus = {bx - 30, 4, bx - 2, 32};
    L.pin = {bx - 60, 4, bx - 32, 32};
    // Optional clock: design and position are chosen separately (under the header or under the list).
    const ql::Settings& s = g_store.settings;
    float ch = s.clock == L"large" ? kClockLargeH : s.clock == L"card" ? kClockCardH : s.clock == L"line" ? kClockLineH : 0.f;
    L.clockAtTop = s.clockPosition != L"bottom";
    float top = L.clockAtTop ? ch : 0.f, bar = L.clockAtTop ? 0.f : ch;
    if (ch == 0) L.clock = {};
    else if (L.clockAtTop) L.clock = {0, kHeaderH, L.W, kHeaderH + ch};
    else L.clock = {0, L.H - ch, L.W, L.H};
    L.fieldTop = kFieldTop + top;
    L.field = {g_addOpen ? 8.f : 10.f, L.fieldTop, L.W - (g_addOpen ? 8.f : 10.f), L.fieldTop + kFieldH};
    float fw = TextWidth(L"폴더…", g_fButton) + 44, iw = TextWidth(L"파일…", g_fButton) + 44;
    L.addFolder = {8, top + 74, 8 + fw, top + 102};
    L.addFile = {14 + fw, top + 74, 14 + fw + iw, top + 102};
    L.hint = {10, top + 104, L.W - 10, top + 124};
    L.listTop = top + (g_addOpen ? 128.f : 74.f);
    L.listBottom = L.H - 6 - bar;

    L.rowTop.resize(g_rows.size());
    float y = 0;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        if (g_rows[i].type == ql::Row::GroupHeader && i > 0) y += kGroupGap;
        L.rowTop[i] = y;
        y += RowH(g_rows[i]);
    }
    L.contentH = y + 4;

    float cx = L.W / 2, mid = (L.listTop + L.listBottom) / 2;
    L.emptyAdd = {cx - 90, mid + 44, cx - 4, mid + 72};
    L.emptyPaste = {cx + 4, mid + 44, cx + 90, mid + 72};
    ClampScroll();
}

void PositionEdits();

void RebuildRows() {
    g_rows = g_store.Rows(g_filter);
    Relayout();
    PositionEdits();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

int RowOfSelection() {
    for (int i = 0; i < (int)g_rows.size(); ++i) {
        if (g_sel.link >= 0 && g_rows[i].type == ql::Row::Item && g_rows[i].link == g_sel.link) return i;
        if (g_sel.link < 0 && g_sel.group >= 0 && g_rows[i].type == ql::Row::GroupHeader && g_rows[i].group == g_sel.group)
            return i;
    }
    return -1;
}

void SelectRow(int i) {
    if (i < 0 || i >= (int)g_rows.size()) return;
    SetSel(g_rows[i].type == ql::Row::Item ? Selection{g_rows[i].link, g_rows[i].group} : Selection{-1, g_rows[i].group});
}

void EnsureVisible(int row) {
    if (row < 0) return;
    float top = L.rowTop[row], bottom = top + RowH(g_rows[row]);
    float viewH = L.listBottom - L.listTop;
    if (top < g_scroll) g_scroll = top;
    else if (bottom > g_scroll + viewH) g_scroll = bottom - viewH;
    ClampScroll();
    PositionEdits();
}

int TargetGroup() {
    if (g_sel.group >= 0 && g_sel.group < (int)g_store.groups.size()) return g_sel.group;
    return 0;
}

// ---- icons ----------------------------------------------------------------------------------

void RequestIcons() {
    std::vector<std::wstring> targets;
    for (auto& l : g_store.links) targets.push_back(l.target);
    g_worker.Request(targets, Px(16));
}

void OnIcon(shell::IconResult* r) {
    if (r->px == Px(16)) {
        IconEntry& e = g_icons[r->target];
        if (r->state != shell::State::Unknown) {
            e.state = r->state;
            e.directory = r->directory;
        }
        IWICBitmap* wic = nullptr;
        if (r->bitmap) g_wic->CreateBitmapFromHBITMAP(r->bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &wic);
        else if (r->icon && !e.real) g_wic->CreateBitmapFromHICON(r->icon, &wic);
        if (wic) {
            if (e.d2d) e.d2d->Release(), e.d2d = nullptr;
            if (e.wic) e.wic->Release();
            e.wic = wic;
            e.real = r->bitmap != nullptr;
        }
        InvalidateRect(g_wnd, nullptr, FALSE);
    }
    if (r->bitmap) DeleteObject(r->bitmap);
    if (r->icon) DestroyIcon(r->icon);
    delete r;
}

bool IsMissing(const std::wstring& target) {
    auto it = g_icons.find(target);
    return it != g_icons.end() && it->second.state == shell::State::Missing;
}

// ---- notices --------------------------------------------------------------------------------

void ShowInfo(const std::wstring& text, int action = 0) {
    g_notice = {true, false, text, L"", L"", {}};
    if (action == kActUndo) g_notice.actions.push_back({L"실행 취소", kActUndo});
    SetTimer(g_wnd, kNoticeTimer, kNoticeMs, nullptr);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void ShowMissing(const std::wstring& target) {
    KillTimer(g_wnd, kNoticeTimer);
    g_notice = {true, true, L"찾을 수 없음", target, target, {{L"경로 수정", kActEditPath}, {L"제거", kActRemoveMissing}}};
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void HideNotice() {
    KillTimer(g_wnd, kNoticeTimer);
    g_notice.on = false;
    g_noticeHits.clear();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// ---- editing --------------------------------------------------------------------------------

void PositionEdits() {
    auto place = [](HWND e, D2D1_RECT_F r, bool show) {
        if (!show) {
            if (IsWindowVisible(e)) ShowWindow(e, SW_HIDE);
            return;
        }
        SetWindowPos(e, nullptr, Px(r.left), Px(r.top), Px(r.right) - Px(r.left), Px(r.bottom) - Px(r.top),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    };
    float editH = 18;
    float ey = L.fieldTop + (kFieldH - editH) / 2;
    place(g_search, {38, ey, L.W - 18, ey + editH}, !g_addOpen);
    place(g_addEdit, {36, ey, L.W - 16, ey + editH}, g_addOpen);

    if (g_renameKind == Rename::None) {
        place(g_renameEdit, {}, false);
        return;
    }
    int row = -1;
    for (int i = 0; i < (int)g_rows.size(); ++i) {
        bool item = g_renameKind == Rename::Link && g_rows[i].type == ql::Row::Item && g_rows[i].link == g_renameIndex;
        bool header = g_renameKind == Rename::Group && g_rows[i].type == ql::Row::GroupHeader && g_rows[i].group == g_renameIndex;
        if (item || header) row = i;
    }
    if (row < 0) {
        place(g_renameEdit, {}, false);
        return;
    }
    float y = ContentToClient(L.rowTop[row]);
    float x = g_renameKind == Rename::Group ? 30.f : (HasHeaders() ? 50.f : 38.f);
    float h = 20;
    float top = y + (RowH(g_rows[row]) - h) / 2;
    bool visible = top >= L.listTop && top + h <= L.listBottom;
    place(g_renameEdit, {x + 2, top, L.W - kInset - 8, top + h}, visible);
}

void CommitRename(bool keep) {
    if (g_renameKind == Rename::None) return;
    Rename kind = g_renameKind;
    int index = g_renameIndex;
    g_renameKind = Rename::None;  // before hiding: EN_KILLFOCUS calls back here
    std::wstring name = ql::CleanName(EditText(g_renameEdit));
    ShowWindow(g_renameEdit, SW_HIDE);
    std::wstring* field = nullptr;
    if (kind == Rename::Link && index < (int)g_store.links.size()) field = &g_store.links[index].name;
    if (kind == Rename::Group && index < (int)g_store.groups.size()) field = &g_store.groups[index].name;
    if (keep && !name.empty() && field && *field != name) {
        Checkpoint();
        *field = name;
        Save();
    }
    SetFocus(g_wnd);
    RebuildRows();
}

void StartRename(Rename kind, int index) {
    CommitRename(true);
    g_renameKind = kind;
    g_renameIndex = index;
    std::wstring text = kind == Rename::Link ? g_store.links[index].name : g_store.groups[index].name;
    SendMessageW(g_renameEdit, WM_SETFONT, (WPARAM)(kind == Rename::Link ? g_renameFont : g_editFont), FALSE);
    SetWindowTextW(g_renameEdit, text.c_str());
    int row = RowOfSelection();
    for (int i = 0; i < (int)g_rows.size(); ++i)
        if ((kind == Rename::Link && g_rows[i].link == index) ||
            (kind == Rename::Group && g_rows[i].type == ql::Row::GroupHeader && g_rows[i].group == index))
            row = i;
    EnsureVisible(row);
    PositionEdits();
    SetFocus(g_renameEdit);
    SendMessageW(g_renameEdit, EM_SETSEL, 0, -1);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void SetAddOpen(bool open, int editLink = -1) {
    CommitRename(true);
    g_addOpen = open;
    g_editPathLink = open ? editLink : -1;
    SetWindowTextW(g_addEdit, editLink >= 0 ? g_store.links[editLink].target.c_str() : L"");
    Relayout();
    PositionEdits();
    if (open) {
        SetFocus(g_addEdit);
        SendMessageW(g_addEdit, EM_SETSEL, 0, -1);
    } else {
        SetFocus(g_wnd);
    }
    InvalidateRect(g_wnd, nullptr, FALSE);
}

std::wstring NameFor(const std::wstring& target, const std::wstring& given) {
    if (!given.empty()) return given;
    if (target.size() == 3 && target[1] == L':') {  // a drive: "업무 (D:)"
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(target.c_str(), 0, &sfi, sizeof sfi, SHGFI_DISPLAYNAME) && sfi.szDisplayName[0])
            return sfi.szDisplayName;
    }
    return ql::DefaultName(target);
}

// Adds in order at `before` (or the end of the group). Shows a summary when something was skipped.
void AddItems(const std::vector<shell::Dropped>& items, int group, int before) {
    Snapshot before_add{g_store.groups, g_store.links, g_sel, g_picked};
    int added = 0, skipped = 0, last = -1, existing = -1;
    for (auto& d : items) {
        int idx = g_store.Add(group, before, d.target, NameFor(d.target, d.name));
        if (idx < 0) {
            ++skipped;
            existing = g_store.Find(d.target);
            continue;
        }
        ++added;
        last = idx;
        if (before >= 0) before = idx + 1;
    }
    if (added) {
        g_undoStack.push_back(std::move(before_add));
        if (g_undoStack.size() > kMaxHistory) g_undoStack.erase(g_undoStack.begin());
        g_redoStack.clear();
        if (g_store.groups[group].collapsed) g_store.groups[group].collapsed = false;
        Save();
        SetSel({last, group});
        RequestIcons();
    } else if (existing >= 0) {
        SetSel({existing, g_store.links[existing].group});
    }
    RebuildRows();
    EnsureVisible(RowOfSelection());
    if (skipped && added) ShowInfo(std::to_wstring(added) + L"개 추가 · " + std::to_wstring(skipped) + L"개는 이미 있어요");
    else if (skipped) ShowInfo(L"이미 목록에 있어요");
}

void SubmitAddRow() {
    ql::Parsed p = ql::ParseInput(EditText(g_addEdit));
    if (p.kind == ql::InputKind::Invalid) {
        MessageBeep(MB_OK);
        return;
    }
    if (g_editPathLink >= 0 && g_editPathLink < (int)g_store.links.size()) {
        int other = g_store.Find(p.target);
        if (other >= 0 && other != g_editPathLink) {
            ShowInfo(L"이미 목록에 있어요");
            return;
        }
        Checkpoint();
        ql::Link& l = g_store.links[g_editPathLink];
        if (l.name == ql::DefaultName(l.target)) l.name = NameFor(p.target, L"");
        l.target = p.target;
        SetSel({g_editPathLink, l.group});
        Save();
        if (g_notice.on && g_notice.warn) HideNotice();
        SetAddOpen(false);
        RequestIcons();
        RebuildRows();
        return;
    }
    SetAddOpen(false);
    AddItems({{p.target, L""}}, TargetGroup(), -1);
}

void PickAndAdd(bool folder) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_ALLOWMULTISELECT));
    dlg->SetTitle(folder ? L"바로가기에 추가할 폴더" : L"바로가기에 추가할 파일");
    std::vector<shell::Dropped> items;
    if (SUCCEEDED(dlg->Show(g_wnd))) {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(dlg->GetResults(&arr))) {
            DWORD n = 0;
            arr->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* item = nullptr;
                PWSTR path = nullptr;
                if (SUCCEEDED(arr->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    ql::Parsed p = ql::ParseInput(path);
                    if (p.kind == ql::InputKind::Path) items.push_back({p.target, L""});
                    CoTaskMemFree(path);
                }
                if (item) item->Release();
            }
            arr->Release();
        }
    }
    dlg->Release();
    if (items.empty()) return;
    if (g_addOpen && g_editPathLink < 0) SetAddOpen(false);
    AddItems(items, TargetGroup(), -1);
}

void PasteItems() {
    auto items = shell::FromClipboard(g_wnd);
    if (items.empty()) {
        ShowInfo(L"클립보드에 경로나 웹 주소가 없어요");
        return;
    }
    AddItems(items, TargetGroup(), -1);
}

void RemoveLinks(std::vector<int> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [](int i) { return i < 0 || i >= (int)g_store.links.size(); }),
                  indices.end());
    if (indices.empty()) return;
    CommitRename(false);
    int row = RowOfSelection();
    Checkpoint();
    std::sort(indices.rbegin(), indices.rend());
    ql::Link removed;
    for (int i : indices) removed = g_store.Remove(i);
    Save();
    RebuildRows();
    // Keep a selection near where the removed row was.
    SetSel({});
    for (int i = std::min(row, (int)g_rows.size() - 1); i >= 0 && row >= 0; --i)
        if (g_rows[i].type == ql::Row::Item) {
            SelectRow(i);
            break;
        }
    ShowInfo(indices.size() == 1 ? L"'" + removed.name + L"' 제거함" : std::to_wstring(indices.size()) + L"개 제거함",
             kActUndo);
}

void RemoveLink(int index) { RemoveLinks({index}); }

// Moves the list to `to`, saving the current state onto `from`'s opposite stack.
void ApplySnapshot(Snapshot to) {
    // Folding is a view preference, not an edit: keep the current state for groups that still exist.
    for (auto& g : to.groups)
        for (auto& cur : g_store.groups)
            if (cur.name == g.name) g.collapsed = cur.collapsed;
    g_store.groups = std::move(to.groups);
    g_store.links = std::move(to.links);
    SetSel(to.sel);
    if (g_sel.link >= (int)g_store.links.size() || g_sel.group >= (int)g_store.groups.size()) SetSel({});
    g_picked = std::move(to.picked);
    Save();
    RequestIcons();
    RebuildRows();
    EnsureVisible(RowOfSelection());
}

void UndoRedo(bool redo) {
    CommitRename(true);
    auto& from = redo ? g_redoStack : g_undoStack;
    auto& to = redo ? g_undoStack : g_redoStack;
    if (from.empty()) {
        MessageBeep(MB_OK);
        return;
    }
    to.push_back({g_store.groups, g_store.links, g_sel, g_picked});
    Snapshot s = std::move(from.back());
    from.pop_back();
    if (g_notice.on && !g_notice.actions.empty() && g_notice.actions[0].second == kActUndo) HideNotice();
    ApplySnapshot(std::move(s));
}

std::wstring NewGroupName() {
    for (int n = 1;; ++n) {
        std::wstring name = n == 1 ? L"새 그룹" : L"새 그룹 " + std::to_wstring(n);
        bool used = false;
        for (auto& g : g_store.groups) used |= g.name == name;
        if (!used) return name;
    }
}

int NewGroup() {
    int g = g_store.AddGroup(NewGroupName());
    Save();
    return g;
}

// ---- opening --------------------------------------------------------------------------------

void OpenLink(int index, bool location) {
    if (index < 0 || index >= (int)g_store.links.size()) return;
    shell::OpenAsync(g_wnd, WM_APP_OPENED, g_store.links[index].target, location);
}

void CopyLink(int index) {
    if (index < 0 || index >= (int)g_store.links.size()) return;
    const std::wstring& t = g_store.links[index].target;
    if (shell::CopyText(g_wnd, t)) ShowInfo(ql::IsUrl(t) ? L"주소를 복사했어요" : L"경로를 복사했어요");
}

// The multi-selection versions: one line per target, and a check before opening a lot at once.
void OpenSelected(bool location) {
    std::vector<int> sel = SelectedLinks();
    if (sel.size() > 10) {
        std::wstring q = std::to_wstring(sel.size()) + L"개를 한꺼번에 열까요?";
        if (MessageBoxW(g_wnd, q.c_str(), kAppName, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    }
    for (int i : sel) OpenLink(i, location);
}

void CopySelected() {
    std::vector<int> sel = SelectedLinks();
    if (sel.size() <= 1) {
        if (!sel.empty()) CopyLink(sel[0]);
        return;
    }
    std::wstring text;
    for (int i : sel) text += g_store.links[i].target + L"\r\n";
    if (shell::CopyText(g_wnd, text)) ShowInfo(std::to_wstring(sel.size()) + L"개의 경로를 복사했어요");
}

// Selects the visible items between the Shift anchor and `row` (added to the current picks when `add`).
void PickRange(int row, bool add) {
    int from = -1;
    for (int i = 0; i < (int)g_rows.size(); ++i)
        if (g_rows[i].type == ql::Row::Item && g_store.links[g_rows[i].link].target == g_anchor) from = i;
    if (from < 0) from = row;
    std::set<std::wstring> picked = add ? g_picked : std::set<std::wstring>{};
    if (add && picked.empty() && g_sel.link >= 0) picked.insert(g_store.links[g_sel.link].target);
    for (int i = std::min(from, row); i <= std::max(from, row); ++i)
        if (g_rows[i].type == ql::Row::Item) picked.insert(g_store.links[g_rows[i].link].target);
    std::wstring anchor = g_anchor.empty() && g_rows[from].type == ql::Row::Item ? g_store.links[g_rows[from].link].target : g_anchor;
    const ql::Row& r = g_rows[row];
    g_sel = {r.type == ql::Row::Item ? r.link : -1, r.group};
    g_picked = std::move(picked);
    g_anchor = anchor;
}

// Ctrl+click: adds or drops one item, keeping the rest.
void TogglePick(int link) {
    if (g_picked.empty() && g_sel.link >= 0) g_picked.insert(g_store.links[g_sel.link].target);
    const std::wstring& t = g_store.links[link].target;
    if (!g_picked.erase(t)) g_picked.insert(t);
    g_sel = {link, g_store.links[link].group};
    g_anchor = t;
}

// What a drag moves: the whole selection when it started on a selected item, else that item.
std::vector<int> DragLinks() {
    if (g_drag.link < 0) return {};
    if (IsPicked(g_drag.link)) return SelectedLinks();
    return {g_drag.link};
}

bool InDrag(int link) {
    if (g_drag.link < 0) return false;
    return IsPicked(g_drag.link) ? IsPicked(link) : link == g_drag.link;
}

void PickAll() {
    g_picked.clear();
    for (auto& r : g_rows)
        if (r.type == ql::Row::Item) g_picked.insert(g_store.links[r.link].target);
}

// ---- show / hide / window state --------------------------------------------------------------

void SaveWindowRect() {
    RECT r;
    GetWindowRect(g_wnd, &r);
    g_store.settings.x = r.left;
    g_store.settings.y = r.top;
    g_store.settings.width = (int)std::lround(Dip(r.right - r.left));
    g_store.settings.height = (int)std::lround(Dip(r.bottom - r.top));
    Save();
}

void ShowWidget(bool show) {
    if (show) {
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
        RequestIcons();
    } else {
        CommitRename(true);
        ShowWindow(g_wnd, SW_HIDE);
    }
}

void ApplyTopmost() {
    SetWindowPos(g_wnd, g_store.settings.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Whole-window transparency through a layered window; fully opaque drops the layered style again.
void ApplyOpacity() {
    int o = std::clamp(g_store.settings.opacity, 100 - kMaxTransparency, 100);
    LONG_PTR ex = GetWindowLongPtrW(g_wnd, GWL_EXSTYLE);
    if (o >= 100) {
        if (ex & WS_EX_LAYERED) SetWindowLongPtrW(g_wnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    } else {
        if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(g_wnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(g_wnd, 0, (BYTE)(o * 255 / 100), LWA_ALPHA);
    }
    RedrawWindow(g_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

int Transparency() { return 100 - g_store.settings.opacity; }

void SetTransparency(int percent, bool announce, bool save = true) {
    percent = std::clamp(percent, 0, kMaxTransparency);
    g_store.settings.opacity = 100 - percent;
    if (save) Save();
    ApplyOpacity();
    if (announce) ShowInfo(percent ? L"투명도 " + std::to_wstring(percent) + L"%" : L"투명도 끔");
}

// ---- transparency slider: a small flyout opened from the tray / title-bar menu ---------------

constexpr wchar_t kSliderClass[] = L"QuickLinksSlider";
constexpr float kSliderW = 264, kSliderH = 92;
HWND g_slider;
ID2D1DCRenderTarget* g_sliderRt;
ID2D1SolidColorBrush* g_sliderBrush;
int g_sliderStart = 0;  // value when opened; Esc goes back to it
bool g_sliderDrag = false, g_sliderHot = false;
POINT g_menuPoint{};  // where the last tray/title-bar menu opened

float SliderScale() { return GetDpiForWindow(g_slider) / 96.f; }
D2D1_RECT_F SliderTrack() { return {20, 54, kSliderW - 20, 58}; }
float SliderThumbX() {
    D2D1_RECT_F t = SliderTrack();
    return t.left + (t.right - t.left) * Transparency() / kMaxTransparency;
}

void SliderSet(int v) {
    v = std::clamp(v, 0, kMaxTransparency);
    if (v == Transparency()) return;
    SetTransparency(v, false, false);  // saved when the slider closes
    InvalidateRect(g_slider, nullptr, FALSE);
}

void SliderFromX(int px) {
    D2D1_RECT_F t = SliderTrack();
    float f = (px / SliderScale() - t.left) / (t.right - t.left);
    SliderSet((int)std::lround(std::clamp(f, 0.f, 1.f) * kMaxTransparency));
}

void CloseSlider(bool keep) {
    if (!g_slider) return;
    if (!keep) SliderSet(g_sliderStart);
    Save();
    HWND s = g_slider;
    g_slider = nullptr;  // WM_ACTIVATE from DestroyWindow comes back here
    DestroyWindow(s);
}

void PaintSlider() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_slider, &ps);
    RECT rc;
    GetClientRect(g_slider, &rc);
    if (!g_sliderRt) {
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        g_d2d->CreateDCRenderTarget(&props, &g_sliderRt);
        g_sliderRt->CreateSolidColorBrush(g_theme.text, &g_sliderBrush);
    }
    // Reuse the drawing helpers by pointing them at this window's target for the duration.
    ID2D1DCRenderTarget* mainRt = g_rt;
    ID2D1SolidColorBrush* mainBrush = g_brush;
    g_rt = g_sliderRt;
    g_brush = g_sliderBrush;
    float dpi = (float)GetDpiForWindow(g_slider);
    g_rt->BindDC(dc, &rc);
    g_rt->SetDpi(dpi, dpi);
    g_rt->BeginDraw();
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    g_rt->Clear(g_theme.info);

    int v = Transparency();
    Text(L"투명도", g_fItem, {20, 10, kSliderW - 80, 38}, g_theme.text);
    std::wstring value = v ? std::to_wstring(v) + L"%" : L"끔";
    Text(value, g_fSliderValue, {kSliderW - 90, 10, kSliderW - 20, 38}, g_theme.accent);

    D2D1_RECT_F t = SliderTrack();
    float x = SliderThumbX(), cy = (t.top + t.bottom) / 2;
    Fill(t, g_theme.underline, 2);
    Fill({t.left, t.top, x, t.bottom}, g_theme.accent, 2);
    g_brush->SetColor(g_theme.field);
    g_rt->FillEllipse(D2D1::Ellipse({x, cy}, 10, 10), g_brush);
    g_brush->SetColor(g_theme.fieldBorder);
    g_rt->DrawEllipse(D2D1::Ellipse({x, cy}, 10, 10), g_brush, 1);
    float inner = g_sliderDrag ? 5.f : g_sliderHot ? 7.f : 6.f;  // Fluent: grows on hover, shrinks while pressed
    g_brush->SetColor(g_theme.accent);
    g_rt->FillEllipse(D2D1::Ellipse({x, cy}, inner, inner), g_brush);

    Text(L"불투명", g_fCaption, {t.left - 2, 68, t.left + 80, 86}, g_theme.text3);
    std::wstring maxLabel = std::to_wstring(kMaxTransparency) + L"%";
    float mw = TextWidth(maxLabel, g_fCaption);
    Text(maxLabel, g_fCaption, {t.right - mw, 68, t.right + 2, 86}, g_theme.text3);

    HRESULT hr = g_rt->EndDraw();
    g_rt = mainRt;
    g_brush = mainBrush;
    if (hr == D2DERR_RECREATE_TARGET) {
        g_sliderBrush->Release(), g_sliderBrush = nullptr;
        g_sliderRt->Release(), g_sliderRt = nullptr;
        InvalidateRect(g_slider, nullptr, FALSE);
    }
    EndPaint(g_slider, &ps);
}

LRESULT CALLBACK SliderProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
        if (h == g_slider) PaintSlider();
        else ValidateRect(h, nullptr);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(h);
        g_sliderDrag = true;
        SliderFromX(GET_X_LPARAM(l));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (g_sliderDrag) SliderFromX(GET_X_LPARAM(l));
        float x = GET_X_LPARAM(l) / SliderScale(), y = GET_Y_LPARAM(l) / SliderScale();
        bool hot = std::hypot(x - SliderThumbX(), y - 56) < 12;
        if (hot != g_sliderHot) {
            g_sliderHot = hot;
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_sliderHot = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        g_sliderDrag = false;
        ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: SliderSet(Transparency() + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1)); return 0;
    case WM_KEYDOWN:
        switch (w) {
        case VK_LEFT:
        case VK_DOWN: SliderSet(Transparency() - 1); break;
        case VK_RIGHT:
        case VK_UP: SliderSet(Transparency() + 1); break;
        case VK_PRIOR: SliderSet(Transparency() + 10); break;
        case VK_NEXT: SliderSet(Transparency() - 10); break;
        case VK_HOME: SliderSet(0); break;
        case VK_END: SliderSet(kMaxTransparency); break;
        case VK_RETURN: CloseSlider(true); break;
        case VK_ESCAPE: CloseSlider(false); break;
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && h == g_slider) PostMessageW(h, WM_CLOSE, 0, 0);  // clicked elsewhere
        return 0;
    case WM_CLOSE:
        if (h == g_slider) CloseSlider(true);
        return 0;
    case WM_DESTROY:
        if (g_sliderBrush) g_sliderBrush->Release(), g_sliderBrush = nullptr;
        if (g_sliderRt) g_sliderRt->Release(), g_sliderRt = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowTransparencySlider(POINT at) {
    CloseSlider(true);
    if (at.x == 0 && at.y == 0) {  // no menu position (opened by a test command): under the header
        RECT r;
        GetWindowRect(g_wnd, &r);
        at = {r.left, r.top + Px(kHeaderH)};
    }
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    int w = (int)std::lround(kSliderW * dx / 96.f), h = (int)std::lround(kSliderH * dx / 96.f);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    int x = std::clamp((int)at.x, (int)mi.rcWork.left, (int)mi.rcWork.right - w);
    int y = std::clamp((int)at.y, (int)mi.rcWork.top, (int)mi.rcWork.bottom - h);
    g_sliderStart = Transparency();
    g_sliderDrag = g_sliderHot = false;
    g_slider = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSliderClass, L"투명도", WS_POPUP, x, y, w, h, g_wnd,
                               nullptr, g_inst, nullptr);
    BOOL dark = g_theme.dark;
    DwmSetWindowAttribute(g_slider, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(g_slider, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner);
    // A test instance must not take the keyboard from the user (tests post keys to it directly).
    ShowWindow(g_slider, g_testInstance ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!g_testInstance) SetForegroundWindow(g_slider);
}

// --theme (tests) wins, then the Theme setting, then Windows' app mode.
bool UseDark() {
    if (g_forceTheme >= 0) return g_forceTheme == 1;
    if (g_store.settings.theme == L"dark") return true;
    if (g_store.settings.theme == L"light") return false;
    return SystemUsesDark();
}

void ApplyTheme() {
    g_theme = UseDark() ? DarkTheme() : LightTheme();
    BOOL dark = g_theme.dark;
    DwmSetWindowAttribute(g_wnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    if (g_fieldBrush) DeleteObject(g_fieldBrush);
    g_fieldBrush = CreateSolidBrush(ToRef(g_theme.field));
    SetPreferredMenuTheme(g_theme.dark);
    if (g_tip) SetWindowTheme(g_tip, g_theme.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    for (HWND e : {g_search, g_addEdit, g_renameEdit})
        if (e) SetWindowTheme(e, g_theme.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    InvalidateRect(g_wnd, nullptr, TRUE);
}

void CreateEditFonts() {
    if (g_editFont) DeleteObject(g_editFont);
    if (g_renameFont) DeleteObject(g_renameFont);
    auto make = [](float dip) {
        return CreateFontW(-Px(dip), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                           g_fonts.ui.c_str());
    };
    g_editFont = make(13);
    g_renameFont = make(14);
    for (HWND e : {g_search, g_addEdit}) SendMessageW(e, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SendMessageW(g_renameEdit, WM_SETFONT, (WPARAM)(g_renameKind == Rename::Link ? g_renameFont : g_editFont), TRUE);
}

// ---- drag and drop --------------------------------------------------------------------------

DropAt ComputeDrop(float clientY) {
    DropAt d;
    if (g_rows.empty()) {
        d.lineY = 2;
        return d;
    }
    float cy = clientY - L.listTop + g_scroll;
    auto nextItem = [](int i) {
        return i + 1 < (int)g_rows.size() && g_rows[i + 1].type == ql::Row::Item ? g_rows[i + 1].link : -1;
    };
    for (int i = 0; i < (int)g_rows.size(); ++i) {
        const ql::Row& r = g_rows[i];
        float top = L.rowTop[i], h = RowH(r);
        if (cy >= top + h) continue;  // below this row (the gap before a header counts as that header)
        d.group = r.group;
        if (r.type == ql::Row::Item) {
            // Upper half: before this item. Lower half: after it, still in its group.
            bool upper = cy < top + h / 2;
            d.before = upper ? r.link : nextItem(i);
            d.lineY = upper ? top : top + h;
        } else if (g_store.groups[r.group].collapsed && g_filter.empty()) {
            d.headerRow = i;  // anywhere on a folded header: into that group (at its end)
        } else {
            d.before = nextItem(i);  // anywhere on an open header: top of that group
            d.lineY = top + h;
        }
        return d;
    }
    const ql::Row& last = g_rows.back();
    d.group = last.group;
    if (last.type == ql::Row::GroupHeader && g_store.groups[last.group].collapsed && g_filter.empty()) {
        d.headerRow = (int)g_rows.size() - 1;
    } else {
        d.lineY = L.rowTop.back() + RowH(last);
    }
    return d;
}

void UpdateAutoExpand(const DropAt& at) {
    int g = at.headerRow >= 0 ? g_rows[at.headerRow].group : -1;
    if (g != g_expandGroup) {
        g_expandGroup = g;
        if (g >= 0) SetTimer(g_wnd, kExpandTimer, kExpandMs, nullptr);
        else KillTimer(g_wnd, kExpandTimer);
    }
}

void UpdateAutoScroll(float clientY) {
    bool nearEdge = clientY < L.listTop + 24 || clientY > L.listBottom - 24;
    if (nearEdge) SetTimer(g_wnd, kScrollTimer, 30, nullptr);
    else KillTimer(g_wnd, kScrollTimer);
}

void EndDragUi() {
    KillTimer(g_wnd, kScrollTimer);
    KillTimer(g_wnd, kExpandTimer);
    g_expandGroup = -1;
}

class WidgetDropTarget : public IDropTarget {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override { return InterlockedDecrement(&ref_); }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) override {
        g_ext.items = shell::FromDataObject(data);
        g_ext.active = !g_ext.items.empty();
        return DragOver(0, pt, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
        if (!g_ext.active) {
            *effect = DROPEFFECT_NONE;
            return S_OK;
        }
        POINT p{pt.x, pt.y};
        ScreenToClient(g_wnd, &p);
        g_ext.pt = p;
        g_ext.at = ComputeDrop(Dip(p.y));
        UpdateAutoExpand(g_ext.at);
        UpdateAutoScroll(Dip(p.y));
        *effect = (*effect & DROPEFFECT_LINK) ? DROPEFFECT_LINK : (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        InvalidateRect(g_wnd, nullptr, FALSE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        g_ext.active = false;
        EndDragUi();
        InvalidateRect(g_wnd, nullptr, FALSE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL pt, DWORD* effect) override {
        DragOver(0, pt, effect);
        auto items = std::move(g_ext.items);
        DropAt at = g_ext.at;
        g_ext.active = false;
        EndDragUi();
        if (!items.empty()) AddItems(items, at.group, at.before);
        return S_OK;
    }

private:
    LONG ref_ = 1;
} g_dropTarget;

// ---- painting -------------------------------------------------------------------------------

void PaintHeader() {
    Glyph(kGlyphApp, g_fGlyph16, {8, 4, 30, 32}, g_theme.accent);
    Text(g_testInstance ? L"바로가기 (테스트)" : L"바로가기", g_fCaption, {34, 0, L.pin.left - 4, kHeaderH}, g_theme.text);
    struct {
        D2D1_RECT_F r;
        Zone z;
        wchar_t g;
        D2D1_COLOR_F c;
    } buttons[] = {
        {L.pin, Zone::Pin, g_store.settings.topmost ? kGlyphPinned : kGlyphPin,
         g_store.settings.topmost ? g_theme.accent : g_theme.text2},
        // While the add row is open the + stays and looks pressed, so there is only one ✕ in the bar.
        {L.plus, Zone::Plus, kGlyphAdd, g_addOpen ? g_theme.accent : g_theme.text2},
        {L.close, Zone::Close, kGlyphChromeClose, g_theme.text2},
    };
    for (auto& b : buttons) {
        bool hot = g_hot.zone == b.z;
        D2D1_COLOR_F c = b.c;
        if (b.z == Zone::Close && hot) {  // Windows caption close: red with a white mark
            Fill(b.r, g_pressed.zone == b.z ? Hex(0xC42B1C, .9f) : Hex(0xC42B1C), 4);
            c = Hex(0xFFFFFF);
        } else if (hot) {
            Fill(b.r, g_pressed.zone == b.z ? g_theme.selected : g_theme.hover, 4);
        } else if (b.z == Zone::Plus && g_addOpen) {
            Fill(b.r, g_theme.selected, 4);
        }
        Glyph(b.g, b.z == Zone::Close ? g_fGlyph12 : g_fGlyph16, b.r, c);
    }
}

void PaintField(const D2D1_RECT_F& r, bool focused) {
    Fill(r, g_theme.field, 4);
    Stroke(r, g_theme.fieldBorder, 4);
    if (focused) Fill({r.left + 1, r.bottom - 2, r.right - 1, r.bottom}, g_theme.accent);
    else Fill({r.left + 2, r.bottom - 1, r.right - 2, r.bottom}, g_theme.underline);
}

void PaintTopPanel() {
    if (!g_addOpen) {
        PaintField(L.field, GetFocus() == g_search);
        Glyph(kGlyphSearch, g_fGlyph12, {L.field.left + 6, L.field.top, L.field.left + 26, L.field.bottom}, g_theme.text3);
        return;
    }
    PaintField(L.field, GetFocus() == g_addEdit);
    std::wstring text = EditText(g_addEdit);
    ql::Parsed p = ql::ParseInput(text);
    wchar_t g = text.empty() ? kGlyphLink : kGlyphUnknown;
    D2D1_COLOR_F gc = text.empty() ? g_theme.text3 : g_theme.warn;
    std::wstring kind;
    if (p.kind == ql::InputKind::Url) {
        g = kGlyphGlobe;
        gc = g_theme.accent;
        kind = L"웹 주소";
    } else if (p.kind == ql::InputKind::Path) {
        std::wstring leaf = ql::DefaultName(p.target);
        bool file = leaf.find(L'.') != std::wstring::npos && p.target.size() > 3;
        g = file ? kGlyphFile : kGlyphFolder;
        gc = file ? g_theme.text2 : g_theme.folder;
        kind = file ? L"파일" : L"폴더";
    }
    Glyph(g, g_fGlyph16, {L.field.left + 4, L.field.top, L.field.left + 26, L.field.bottom}, gc);
    Button(L.addFolder, L"폴더…", kGlyphFolder, false, g_hot.zone == Zone::AddFolder);
    Button(L.addFile, L"파일…", kGlyphFile, false, g_hot.zone == Zone::AddFile);

    // Hint: what Enter will do, with the group name in semibold.
    std::wstring hint, bold;
    D2D1_COLOR_F hc = g_theme.text2;
    if (text.empty()) {
        hint = g_editPathLink >= 0 ? L"새 경로나 웹 주소를 넣으세요" : L"경로나 웹 주소를 붙여넣으세요";
    } else if (p.kind == ql::InputKind::Invalid) {
        hint = L"폴더·파일 경로나 웹 주소를 넣으세요";
        hc = g_theme.warn;
    } else if (g_editPathLink >= 0) {
        hint = kind + L" · Enter로 경로 수정";
    } else {
        bold = g_store.groups[TargetGroup()].name;
        hint = kind + L" · Enter로 " + bold + L"에 추가";
    }
    IDWriteTextLayout* lay = nullptr;
    if (SUCCEEDED(g_dw->CreateTextLayout(hint.c_str(), (UINT32)hint.size(), g_fCaption, L.hint.right - L.hint.left,
                                         L.hint.bottom - L.hint.top, &lay))) {
        if (!bold.empty()) {
            size_t at = hint.rfind(bold);
            lay->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, {(UINT32)at, (UINT32)bold.size()});
        }
        g_brush->SetColor(hc);
        g_rt->DrawTextLayout({L.hint.left, L.hint.top}, lay, g_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        lay->Release();
    }
}

bool DropHighlightGroup(int g) {
    return (g_ext.active && g_ext.at.group == g) || (g_drag.active && g_drag.at.group == g);
}

void PaintGroupHeader(int i, float y) {
    const ql::Row& r = g_rows[i];
    const ql::Group& grp = g_store.groups[r.group];
    D2D1_RECT_F rect{kInset, y, L.W - kInset, y + kGroupH};
    bool selected = g_sel.link < 0 && g_sel.group == r.group;
    bool hot = g_hot.zone == Zone::Row && g_hot.row == i && !g_drag.active;
    if (selected) Fill(rect, g_theme.selected, 4);
    else if (hot) Fill(rect, g_theme.hover, 4);
    if ((g_ext.active && g_ext.at.headerRow == i) || (g_drag.active && g_drag.at.headerRow == i))
        Fill(rect, g_theme.drop, 4);
    D2D1_COLOR_F c = DropHighlightGroup(r.group) ? g_theme.accent : g_theme.text2;
    bool collapsed = grp.collapsed && g_filter.empty();
    Glyph(collapsed ? kGlyphRight : kGlyphDown, g_fGlyph12, {10, y, 26, y + kGroupH}, c);
    if (g_renameKind == Rename::Group && g_renameIndex == r.group) {
        PaintField({28, y + 2, L.W - kInset - 4, y + kGroupH - 2}, true);
        return;
    }
    float w = std::min(TextWidth(grp.name, g_fCaptionBold), L.W - 80);
    Text(grp.name, g_fCaptionBold, {30, y, 30 + w + 1, y + kGroupH}, c);
    Text(std::to_wstring(g_store.CountIn(r.group)), g_fCaption, {30 + w + 6, y, L.W - kInset - 4, y + kGroupH},
         DropHighlightGroup(r.group) ? g_theme.accent : g_theme.text3);
}

void PaintItem(int i, float y, float opacity, bool ghost) {
    const ql::Row& r = g_rows[i];
    const ql::Link& link = g_store.links[r.link];
    D2D1_RECT_F rect{kInset, y, L.W - kInset, y + kItemH};
    bool selected = IsPicked(r.link);
    bool focused = g_sel.link == r.link;
    bool hot = g_hot.zone == Zone::Row || g_hot.zone == Zone::RowMore ? g_hot.row == i : false;
    if (g_drag.active || g_ext.active) hot = false;
    if (ghost) {
        Fill(rect, g_theme.ghost, 4);
        Stroke(rect, g_theme.fieldBorder, 4);
    } else {
        if (g_ext.active && g_ext.at.group == r.group) Fill(rect, g_theme.drop, 4);
        if (selected) {
            Fill(rect, Fade(g_theme.selected, opacity), 4);
            Fill({kInset, y + 8, kInset + 3, y + 24}, Fade(g_theme.accent, opacity), 1.5f);
        } else if (hot) {
            Fill(rect, g_theme.hover, 4);
        }
    }
    bool missing = IsMissing(link.target);
    float x = HasHeaders() ? 26.f : 14.f;
    auto it = g_icons.find(link.target);
    if (it != g_icons.end())
        if (ID2D1Bitmap* bmp = IconBitmap(it->second))
            g_rt->DrawBitmap(bmp, {x, y + 8, x + 16, y + 24}, opacity * (missing ? .45f : 1.f),
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    float right = L.W - kInset - 6;
    if ((hot || focused) && !ghost && !g_drag.active) {
        D2D1_RECT_F more{L.W - kInset - 30, y + 3, L.W - kInset - 4, y + kItemH - 3};
        if (g_hot.zone == Zone::RowMore && g_hot.row == i) Fill(more, g_theme.hover, 4);
        Glyph(kGlyphMore, g_fGlyph12, more, Fade(g_theme.text2, opacity));
        right = more.left - 2;
    }
    if (missing) {
        Glyph(kGlyphWarn, g_fGlyph12, {right - 18, y, right, y + kItemH}, Fade(g_theme.warn, opacity));
        right -= 22;
    }
    if (g_renameKind == Rename::Link && g_renameIndex == r.link && !ghost) {
        PaintField({x + 22, y + 3, L.W - kInset - 4, y + kItemH - 3}, true);
        return;
    }
    Text(link.name, g_fItem, {x + 24, y, right, y + kItemH}, Fade(missing ? g_theme.text2 : g_theme.text, opacity));
}

void PaintInsertionLine(const DropAt& at) {
    if (at.lineY < 0) return;
    float y = ContentToClient(at.lineY);
    Fill({12, y - 1, L.W - 12, y + 1}, g_theme.accent, 1);
    g_brush->SetColor(g_theme.accent);
    g_rt->FillEllipse(D2D1::Ellipse({10, y}, 4, 4), g_brush);
    g_brush->SetColor(g_theme.bg);
    g_rt->FillEllipse(D2D1::Ellipse({10, y}, 2, 2), g_brush);
}

void PaintEmpty() {
    float cx = L.W / 2, mid = (L.listTop + L.listBottom) / 2;
    if (!g_filter.empty()) {
        Text(L"'" + g_filter + L"'와 맞는 항목이 없어요", g_fEmptyBody, {16, mid - 40, L.W - 16, mid}, g_theme.text2);
        return;
    }
    g_brush->SetColor(Fade(g_theme.accent, g_theme.dark ? .18f : .12f));
    g_rt->FillEllipse(D2D1::Ellipse({cx, mid - 46}, 22, 22), g_brush);
    Glyph(kGlyphNewFolder, g_fGlyph22, {cx - 22, mid - 68, cx + 22, mid - 24}, g_theme.accent);
    Text(L"자주 여는 폴더를 모아 두세요", g_fEmptyTitle, {16, mid - 16, L.W - 16, mid + 4}, g_theme.text);
    Text(L"탐색기에서 끌어다 놓거나\n경로를 붙여넣으세요", g_fEmptyBody, {20, mid + 4, L.W - 20, mid + 38}, g_theme.text2);
    Button(L.emptyAdd, L"폴더 추가", 0, true, g_hot.zone == Zone::EmptyAdd);
    Button(L.emptyPaste, L"붙여넣기", 0, false, g_hot.zone == Zone::EmptyPaste);
}

// ---- clock (시안 B: large, C: bar, D: card) --------------------------------------------------

struct ClockText {
    std::wstring time, ampm, date;
};

ClockText NowText() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const ql::Settings& s = g_store.settings;
    wchar_t buf[32];
    ClockText t;
    int h = st.wHour;
    if (s.clock24) {
        swprintf(buf, 32, L"%02d:%02d", h, st.wMinute);
    } else {
        t.ampm = h < 12 ? L"오전" : L"오후";
        swprintf(buf, 32, L"%d:%02d", h % 12 ? h % 12 : 12, st.wMinute);
    }
    t.time = buf;
    if (s.clockSeconds) {
        swprintf(buf, 32, L":%02d", st.wSecond);
        t.time += buf;
    }
    static const wchar_t* days[] = {L"일", L"월", L"화", L"수", L"목", L"금", L"토"};
    swprintf(buf, 32, L"%d월 %d일 %s요일", st.wMonth, st.wDay, days[st.wDayOfWeek]);
    t.date = buf;
    return t;
}

void PaintAnalog(D2D1_POINT_2F c, float r) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    g_brush->SetColor(g_theme.bg);
    g_rt->FillEllipse(D2D1::Ellipse(c, r, r), g_brush);
    g_brush->SetColor(g_theme.fieldBorder);
    g_rt->DrawEllipse(D2D1::Ellipse(c, r, r), g_brush, 1);
    const float pi = 3.14159265f;
    for (int i = 0; i < 12; ++i) {
        float a = i * 30 * pi / 180, s = std::sin(a), co = std::cos(a);
        g_brush->SetColor(g_theme.text3);
        g_rt->DrawLine({c.x + (r - 4) * s, c.y - (r - 4) * co}, {c.x + (r - 2) * s, c.y - (r - 2) * co}, g_brush,
                       i % 3 ? 1.f : 1.6f);
    }
    auto hand = [&](float degrees, float len, float width, D2D1_COLOR_F color) {
        float a = degrees * pi / 180;
        g_brush->SetColor(color);
        g_rt->DrawLine(c, {c.x + len * std::sin(a), c.y - len * std::cos(a)}, g_brush, width, g_roundStroke);
    };
    float sec = st.wSecond, min = st.wMinute + sec / 60, hour = st.wHour % 12 + min / 60;
    hand(hour * 30, r * .5f, 2.4f, g_theme.text);
    hand(min * 6, r * .72f, 1.6f, g_theme.text);
    if (g_store.settings.clockSeconds) hand(sec * 6, r * .78f, 1, g_theme.accent);
    g_brush->SetColor(g_theme.accent);
    g_rt->FillEllipse(D2D1::Ellipse(c, 1.8f, 1.8f), g_brush);
}

void PaintClock() {
    const std::wstring& style = g_store.settings.clock;
    if (style == L"off") return;
    ClockText t = NowText();
    if (style == L"large") {
        float x = 14, y = L.clock.top + (L.clockAtTop ? 0.f : -2.f);  // at the bottom: keep the date off the edge
        float w = TextWidth(t.time, g_fClockBig);
        Text(t.time, g_fClockBig, {x, y - 2, x + w + 2, y + 44}, g_theme.text);
        if (!t.ampm.empty()) Text(t.ampm, g_fClockSmall, {x + w + 6, y + 21, L.W - 10, y + 45}, g_theme.text2);
        Text(t.date, g_fCaption, {x + 1, y + 40, L.W - 10, y + 56}, g_theme.text2);
    } else if (style == L"card") {
        D2D1_RECT_F card = L.clockAtTop ? D2D1_RECT_F{10, L.clock.top + 2, L.W - 10, L.clock.bottom - 6}
                                        : D2D1_RECT_F{10, L.clock.top + 2, L.W - 10, L.clock.bottom - 8};
        Fill(card, g_theme.field, 6);
        Stroke(card, g_theme.fieldBorder, 6);
        float cy = (card.top + card.bottom) / 2;
        PaintAnalog({card.left + 32, cy}, 21);
        float x = card.left + 64;
        if (!t.ampm.empty()) {
            float aw = TextWidth(t.ampm, g_fCaption);
            Text(t.ampm, g_fCaption, {x, cy - 21, x + aw + 1, cy + 1}, g_theme.text2);
            x += aw + 4;
        }
        Text(t.time, g_fClockCard, {x, cy - 24, card.right - 8, cy + 3}, g_theme.text);
        Text(t.date, g_fCaption, {card.left + 64, cy + 2, card.right - 8, cy + 20}, g_theme.text2);
    } else if (style == L"line") {
        // One line: date left, time right; a hairline separates it from the list side.
        D2D1_RECT_F b = L.clock;
        float lineY = L.clockAtTop ? b.bottom - 1 : b.top;
        Fill({b.left + 6, lineY, b.right - 6, lineY + 1}, g_theme.fieldBorder);
        D2D1_RECT_F row = L.clockAtTop ? D2D1_RECT_F{b.left, b.top, b.right, b.bottom - 2}
                                       : D2D1_RECT_F{b.left, b.top + 1, b.right, b.bottom - 2};
        Glyph(kGlyphCalendar, g_fGlyph12, {row.left + 10, row.top, row.left + 26, row.bottom}, g_theme.text3);
        std::wstring time = (t.ampm.empty() ? L"" : t.ampm + L" ") + t.time;
        float w = TextWidth(time, g_fCaptionBold);
        Text(t.date, g_fCaption, {row.left + 30, row.top, row.right - w - 18, row.bottom}, g_theme.text2);
        Text(time, g_fCaptionBold, {row.right - 12 - w, row.top, row.right - 10, row.bottom}, g_theme.text);
    }
}

// Next repaint at the next second (seconds shown) or the next minute.
void ScheduleClock() {
    if (g_store.settings.clock == L"off") {
        KillTimer(g_wnd, kClockTimer);
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    UINT ms = g_store.settings.clockSeconds ? 1000 - st.wMilliseconds : (60 - st.wSecond) * 1000 - st.wMilliseconds;
    SetTimer(g_wnd, kClockTimer, ms + 15, nullptr);
}

void InvalidateClock() {
    const D2D1_RECT_F& r = L.clock;
    if (r.bottom <= r.top) return;
    RECT px{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)};
    InvalidateRect(g_wnd, &px, FALSE);
}

void PaintNotice() {
    g_noticeHits.clear();
    if (!g_notice.on) return;
    float h = g_notice.warn ? kNoticeWarnH : kNoticeInfoH;
    bool clockBelow = !L.clockAtTop && L.clock.bottom > L.clock.top;
    float bottom = clockBelow ? L.clock.top - 2 : L.H - 8;  // above a clock under the list
    D2D1_RECT_F r{8, bottom - h, L.W - 8, bottom};
    g_noticeRect = r;
    Fill(r, g_notice.warn ? g_theme.warnBg : g_theme.info, 4);
    Stroke(r, g_notice.warn ? g_theme.warnBorder : g_theme.infoBorder, 4);
    if (!g_notice.warn) {
        float right = r.right - 12;
        for (auto& [label, id] : g_notice.actions) {
            float w = TextWidth(label, g_fCaptionBold);
            D2D1_RECT_F a{right - w - 8, r.top + 4, right + 4, r.bottom - 4};
            if (g_hot.zone == Zone::NoticeAction) Fill(a, g_theme.hover, 4);
            Text(label, g_fCaptionBold, {a.left + 6, a.top, a.right, a.bottom}, g_theme.accent);
            g_noticeHits.push_back({a, id});
            right = a.left - 4;
        }
        Text(g_notice.title, g_fCaption, {r.left + 12, r.top, right, r.bottom}, g_theme.text);
        return;
    }
    Glyph(kGlyphWarn, g_fGlyph12, {r.left + 8, r.top + 6, r.left + 28, r.top + 26}, g_theme.warn);
    Text(g_notice.title, g_fCaptionBold, {r.left + 32, r.top + 6, r.right - 34, r.top + 26}, g_theme.text);
    g_noticeClose = {r.right - 30, r.top + 4, r.right - 6, r.top + 28};
    if (g_hot.zone == Zone::NoticeClose) Fill(g_noticeClose, g_theme.hover, 4);
    Glyph(kGlyphClose, g_fGlyph12, g_noticeClose, g_theme.text2);
    Text(g_notice.detail, g_fCaption, {r.left + 32, r.top + 26, r.right - 10, r.top + 44}, g_theme.text2);
    float x = r.left + 32;
    for (size_t i = 0; i < g_notice.actions.size(); ++i) {
        auto& [label, id] = g_notice.actions[i];
        float w = TextWidth(label, g_fButton) + 20;
        D2D1_RECT_F b{x, r.top + 50, x + w, r.top + 74};
        Button(b, label, 0, false, g_hot.zone == Zone::NoticeAction && g_hot.row == (int)i);
        g_noticeHits.push_back({b, id});
        x += w + 6;
    }
}

void Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_wnd, &ps);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    EnsureTarget();
    g_rt->BindDC(dc, &rc);
    g_rt->SetDpi((float)g_dpi, (float)g_dpi);
    g_rt->BeginDraw();
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    g_rt->Clear(g_theme.bg);

    PaintHeader();
    PaintClock();
    PaintTopPanel();

    g_rt->PushAxisAlignedClip({0, L.listTop, L.W, L.listBottom}, D2D1_ANTIALIAS_MODE_ALIASED);
    if (g_rows.empty()) {
        PaintEmpty();
    } else {
        for (int i = 0; i < (int)g_rows.size(); ++i) {
            float y = ContentToClient(L.rowTop[i]);
            if (y + RowH(g_rows[i]) < L.listTop || y > L.listBottom) continue;
            if (g_rows[i].type == ql::Row::GroupHeader) PaintGroupHeader(i, y);
            else PaintItem(i, y, g_drag.active && InDrag(g_rows[i].link) ? .4f : 1.f, false);
        }
    }
    if (g_drag.active) PaintInsertionLine(g_drag.at);
    if (g_ext.active) PaintInsertionLine(g_ext.at);
    g_rt->PopAxisAlignedClip();

    if (g_drag.active) {
        for (int i = 0; i < (int)g_rows.size(); ++i)
            if (g_rows[i].type == ql::Row::Item && g_rows[i].link == g_drag.link) {
                float y = std::clamp(Dip(g_drag.pt.y) - g_drag.grabDy, L.listTop - 8, L.listBottom - kItemH + 8);
                g_rt->SetTransform(D2D1::Matrix3x2F::Translation(10, 0));
                PaintItem(i, y, .92f, true);
                g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
                size_t n = DragLinks().size();
                if (n > 1) {  // how many move together
                    std::wstring badge = std::to_wstring(n);
                    float w = std::max(20.f, TextWidth(badge, g_fCaption) + 12);
                    D2D1_RECT_F b{L.W - w - 4, y - 6, L.W - 4, y + 14};
                    Fill(b, g_theme.accent, 10);
                    g_fCaption->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    Text(badge, g_fCaption, b, g_theme.onAccent);
                    g_fCaption->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }
    }
    if (g_ext.active) {
        std::wstring badge = L"+" + std::to_wstring(g_ext.items.size());
        float x = Dip(g_ext.pt.x) + 14, y = Dip(g_ext.pt.y) + 6;
        float w = TextWidth(badge, g_fCaption) + 14;
        Fill({x, y, x + w, y + 20}, g_theme.accent, 10);
        Text(badge, g_fCaption, {x + 7, y, x + w, y + 20}, g_theme.onAccent);
    }

    // Thin scroll indicator while the mouse is over the widget.
    float viewH = L.listBottom - L.listTop;
    if (g_mouseIn && L.contentH > viewH) {
        float th = std::max(24.f, viewH * viewH / L.contentH);
        float ty = L.listTop + (viewH - th) * g_scroll / (L.contentH - viewH);
        Fill({L.W - 5, ty, L.W - 2, ty + th}, g_theme.scroll, 1.5f);
    }
    PaintNotice();

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
        InvalidateRect(g_wnd, nullptr, FALSE);
    }
    EndPaint(g_wnd, &ps);
}

// ---- hit testing and input ------------------------------------------------------------------

Hit HitTest(float x, float y) {
    if (g_notice.on && In(g_noticeRect, x, y)) {
        if (g_notice.warn && In(g_noticeClose, x, y)) return {Zone::NoticeClose};
        for (size_t i = 0; i < g_noticeHits.size(); ++i)
            if (In(g_noticeHits[i].first, x, y)) return {Zone::NoticeAction, (int)i};
        return {Zone::Notice};
    }
    if (In(L.pin, x, y)) return {Zone::Pin};
    if (In(L.plus, x, y)) return {Zone::Plus};
    if (In(L.close, x, y)) return {Zone::Close};
    if (g_addOpen && In(L.addFolder, x, y)) return {Zone::AddFolder};
    if (g_addOpen && In(L.addFile, x, y)) return {Zone::AddFile};
    if (y < L.listTop || y >= L.listBottom) return {};
    if (g_rows.empty() && g_filter.empty()) {
        if (In(L.emptyAdd, x, y)) return {Zone::EmptyAdd};
        if (In(L.emptyPaste, x, y)) return {Zone::EmptyPaste};
        return {Zone::List};
    }
    float cy = y - L.listTop + g_scroll;
    for (int i = 0; i < (int)g_rows.size(); ++i) {
        float top = L.rowTop[i];
        if (cy >= top && cy < top + RowH(g_rows[i]) && x >= kInset && x < L.W - kInset) {
            if (g_rows[i].type == ql::Row::Item && x >= L.W - kInset - 30) return {Zone::RowMore, i};
            return {Zone::Row, i};
        }
    }
    return {Zone::List};
}

void UpdateTooltip() {
    int row = (g_hot.zone == Zone::Row || g_hot.zone == Zone::RowMore) && !g_drag.active ? g_hot.row : -1;
    if (row >= 0 && g_rows[row].type != ql::Row::Item) row = -1;
    if (row == g_tipRow) return;
    g_tipRow = row;
    TOOLINFOW ti{sizeof ti};
    ti.hwnd = g_wnd;
    ti.uId = 1;
    if (row >= 0) {
        float y = ContentToClient(L.rowTop[row]);
        ti.rect = {Px(kInset), Px(y), Px(L.W - kInset - 30), Px(y + kItemH)};
        const std::wstring& t = g_store.links[g_rows[row].link].target;
        g_tipText = t + (IsMissing(t) ? L"\n찾을 수 없음" : L"");
    }
    SendMessageW(g_tip, TTM_POP, 0, 0);
    SendMessageW(g_tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

void SetHot(Hit h) {
    if (h == g_hot) return;
    g_hot = h;
    UpdateTooltip();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void ToggleGroup(int g) {
    if (!g_filter.empty()) return;
    g_store.groups[g].collapsed = !g_store.groups[g].collapsed;
    Save();
    RebuildRows();
}

void LinkMenu(int link, POINT screen) {
    const ql::Link& l = g_store.links[link];
    bool url = ql::IsUrl(l.target);
    size_t n = SelectedLinks().size();
    HMENU m = CreatePopupMenu();
    if (n > 1) {  // the whole selection: no single-item commands
        std::wstring count = L" (" + std::to_wstring(n) + L"개)";
        AppendMenuW(m, MF_STRING, IDM_OPEN, (L"열기" + count + L"\tEnter").c_str());
        AppendMenuW(m, MF_STRING, IDM_COPY, L"경로 복사\tCtrl+C");
    } else {
        AppendMenuW(m, MF_STRING, IDM_OPEN, L"열기\tEnter");
        if (!url) AppendMenuW(m, MF_STRING, IDM_LOCATION, L"파일 위치 열기\tCtrl+Enter");
        AppendMenuW(m, MF_STRING, IDM_COPY, url ? L"주소 복사\tCtrl+C" : L"경로 복사\tCtrl+C");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, IDM_RENAME, L"이름 바꾸기\tF2");
        AppendMenuW(m, MF_STRING, IDM_EDITPATH, url ? L"주소 수정…" : L"경로 수정…");
    }
    HMENU groups = CreatePopupMenu();
    for (int g = 0; g < (int)g_store.groups.size(); ++g)
        AppendMenuW(groups, MF_STRING | (n == 1 && g == l.group ? MF_CHECKED : 0), IDM_MOVETO + g,
                    g_store.groups[g].name.c_str());
    AppendMenuW(groups, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(groups, MF_STRING, IDM_MOVETO_NEW, L"새 그룹…");
    if (n > 1) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)groups, L"그룹 이동");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_REMOVE,
                n > 1 ? (L"목록에서 제거 (" + std::to_wstring(n) + L"개)\tDel").c_str() : L"목록에서 제거\tDel");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_wnd, WM_COMMAND, cmd, 0);
}

void GroupMenu(int g, POINT screen) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_RENAMEGROUP, L"그룹 이름 바꾸기\tF2");
    AppendMenuW(m, MF_STRING, IDM_NEWGROUP, L"새 그룹");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g == 0 ? MF_GRAYED : 0), IDM_GROUPUP, L"위로");
    AppendMenuW(m, MF_STRING | (g + 1 >= (int)g_store.groups.size() ? MF_GRAYED : 0), IDM_GROUPDOWN, L"아래로");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g == 0 ? MF_GRAYED : 0), IDM_DELETEGROUP, L"그룹 삭제");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_wnd, WM_COMMAND, cmd, 0);
}

void ListMenu(POINT screen) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_ADD, L"추가…");
    AppendMenuW(m, MF_STRING, IDM_PASTE, L"붙여넣기\tCtrl+V");
    AppendMenuW(m, MF_STRING, IDM_NEWGROUP, L"새 그룹");
    AppendMenuW(m, MF_STRING | (g_rows.empty() ? MF_GRAYED : 0), IDM_SELECTALL, L"모두 선택\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_undoStack.empty() ? MF_GRAYED : 0), IDM_UNDO, L"실행 취소\tCtrl+Z");
    AppendMenuW(m, MF_STRING | (g_redoStack.empty() ? MF_GRAYED : 0), IDM_REDO, L"다시 실행\tCtrl+Y");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_wnd, WM_COMMAND, cmd, 0);
}

void TrayMenu(POINT screen) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TOGGLE, IsWindowVisible(g_wnd) ? L"위젯 숨기기" : L"위젯 보이기");
    AppendMenuW(m, MF_STRING | (g_store.settings.topmost ? MF_CHECKED : 0), IDM_TOPMOST, L"항상 위");
    HMENU open = CreatePopupMenu();
    AppendMenuW(open, MF_STRING | (!g_store.settings.doubleClick ? MF_CHECKED : 0), IDM_SINGLECLICK, L"한 번 클릭");
    AppendMenuW(open, MF_STRING | (g_store.settings.doubleClick ? MF_CHECKED : 0), IDM_DOUBLECLICK, L"두 번 클릭");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)open, L"여는 방법");
    HMENU theme = CreatePopupMenu();
    const std::wstring& t = g_store.settings.theme;
    AppendMenuW(theme, MF_STRING | (t == L"system" ? MF_CHECKED : 0), IDM_THEME_SYSTEM, L"Windows 설정 따르기");
    AppendMenuW(theme, MF_STRING | (t == L"light" ? MF_CHECKED : 0), IDM_THEME_LIGHT, L"라이트");
    AppendMenuW(theme, MF_STRING | (t == L"dark" ? MF_CHECKED : 0), IDM_THEME_DARK, L"다크");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)theme, L"테마");
    HMENU clock = CreatePopupMenu();
    const std::wstring& c = g_store.settings.clock;
    // Design and position are separate choices.
    AppendMenuW(clock, MF_STRING | (c == L"off" ? MF_CHECKED : 0), IDM_CLOCK_OFF, L"끄기");
    AppendMenuW(clock, MF_STRING | (c == L"large" ? MF_CHECKED : 0), IDM_CLOCK_LARGE, L"큰 숫자");
    AppendMenuW(clock, MF_STRING | (c == L"line" ? MF_CHECKED : 0), IDM_CLOCK_LINE, L"한 줄 (날짜 · 시간)");
    AppendMenuW(clock, MF_STRING | (c == L"card" ? MF_CHECKED : 0), IDM_CLOCK_CARD, L"아날로그 카드");
    AppendMenuW(clock, MF_SEPARATOR, 0, nullptr);
    UINT off = c == L"off" ? MF_GRAYED : 0;
    bool top = g_store.settings.clockPosition != L"bottom";
    AppendMenuW(clock, MF_STRING | off | (top ? MF_CHECKED : 0), IDM_CLOCK_TOP, L"위치: 위 (제목 아래)");
    AppendMenuW(clock, MF_STRING | off | (!top ? MF_CHECKED : 0), IDM_CLOCK_BOTTOM, L"위치: 아래 (목록 아래)");
    AppendMenuW(clock, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(clock, MF_STRING | off | (g_store.settings.clock24 ? MF_CHECKED : 0), IDM_CLOCK_24H, L"24시간제");
    AppendMenuW(clock, MF_STRING | off | (g_store.settings.clockSeconds ? MF_CHECKED : 0), IDM_CLOCK_SECONDS, L"초 표시");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)clock, L"시계");
    // Shown as transparency (0% = solid); the ini keeps Opacity = 100 - transparency.
    int current = Transparency();
    std::wstring see = L"투명도… (" + (current ? std::to_wstring(current) + L"%" : std::wstring(L"끔")) + L")";
    AppendMenuW(m, MF_STRING, IDM_TRANSPARENCY, see.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"종료");
    SetForegroundWindow(g_wnd);
    g_menuPoint = screen;
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_wnd, nullptr);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) SendMessageW(g_wnd, WM_COMMAND, cmd, 0);
}

void OnCommand(int id);

void MoveSelection(int delta, bool extend) {
    if (g_rows.empty()) return;
    int row = RowOfSelection();
    row = row < 0 ? (delta > 0 ? 0 : (int)g_rows.size() - 1) : std::clamp(row + delta, 0, (int)g_rows.size() - 1);
    if (extend) PickRange(row, false);  // Shift+arrows: range from the anchor
    else SelectRow(row);
    EnsureVisible(row);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// Keys for the list; the search box forwards its navigation keys here. Returns true if handled.
bool OnKey(WPARAM vk) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    bool shift = GetKeyState(VK_SHIFT) < 0;
    int row = RowOfSelection();
    bool onItem = g_sel.link >= 0;
    bool many = SelectedLinks().size() > 1;
    switch (vk) {
    case VK_UP: MoveSelection(-1, shift); return true;
    case VK_DOWN: MoveSelection(1, shift); return true;
    case VK_RETURN:
        if (many) OpenSelected(ctrl);
        else if (onItem) OpenLink(g_sel.link, ctrl);
        else if (g_sel.group >= 0 && row >= 0) ToggleGroup(g_sel.group);
        else if (!g_rows.empty()) {
            for (int i = 0; i < (int)g_rows.size(); ++i)
                if (g_rows[i].type == ql::Row::Item) {
                    SelectRow(i);
                    OpenLink(g_rows[i].link, ctrl);
                    break;
                }
        }
        return true;
    case VK_LEFT:
        if (row >= 0 && onItem && HasHeaders()) {
            SetSel({-1, g_sel.group});
            EnsureVisible(RowOfSelection());
        } else if (row >= 0 && !onItem && !g_store.groups[g_sel.group].collapsed) {
            ToggleGroup(g_sel.group);
        }
        InvalidateRect(g_wnd, nullptr, FALSE);
        return true;
    case VK_RIGHT:
        if (row >= 0 && !onItem && g_store.groups[g_sel.group].collapsed) ToggleGroup(g_sel.group);
        return true;
    case VK_F2:
        if (onItem) StartRename(Rename::Link, g_sel.link);
        else if (g_sel.group >= 0 && row >= 0) StartRename(Rename::Group, g_sel.group);
        return true;
    case VK_DELETE:
        RemoveLinks(SelectedLinks());
        return true;
    case VK_ESCAPE:
        if (many) {
            SetSel(g_sel);  // back to just the focused row
            InvalidateRect(g_wnd, nullptr, FALSE);
        } else if (!g_filter.empty()) SetWindowTextW(g_search, L"");
        else if (g_addOpen) SetAddOpen(false);
        else ShowWidget(false);
        return true;
    }
    if (ctrl && vk == 'C') {
        CopySelected();
        return true;
    }
    if (ctrl && vk == 'A') {
        OnCommand(IDM_SELECTALL);
        return true;
    }
    if (ctrl && vk == 'V') {
        PasteItems();
        return true;
    }
    if (ctrl && vk == 'Z') {
        OnCommand(GetKeyState(VK_SHIFT) < 0 ? IDM_REDO : IDM_UNDO);  // Ctrl+Shift+Z also redoes
        return true;
    }
    if (ctrl && vk == 'Y') {
        OnCommand(IDM_REDO);
        return true;
    }
    if (ctrl && vk == 'F') {
        if (g_addOpen) SetAddOpen(false);
        SetFocus(g_search);
        return true;
    }
    return false;
}

LRESULT CALLBACK EditSubclass(HWND e, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;  // no beep
    if (m == WM_KEYDOWN) {
        if (e == g_renameEdit) {
            if (w == VK_RETURN) return CommitRename(true), 0;
            if (w == VK_ESCAPE) return CommitRename(false), 0;
        } else if (e == g_addEdit) {
            if (w == VK_RETURN) return SubmitAddRow(), 0;
            if (w == VK_ESCAPE) return SetAddOpen(false), 0;
        } else if (e == g_search) {
            if (w == VK_UP || w == VK_DOWN || w == VK_RETURN) return OnKey(w), 0;
            // Ctrl+Z/Y in an empty search box act on the list (with text, the box undoes its typing).
            if ((w == 'Z' || w == 'Y') && GetKeyState(VK_CONTROL) < 0 && !GetWindowTextLengthW(e))
                return OnKey(w), 0;
            if (w == VK_ESCAPE) {
                if (GetWindowTextLengthW(e)) SetWindowTextW(e, L"");
                else SetFocus(g_wnd);
                return 0;
            }
        }
    }
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) InvalidateRect(g_wnd, nullptr, FALSE);
    return DefSubclassProc(e, m, w, l);
}

HWND MakeEdit(const wchar_t* cue) {
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, nullptr, g_inst, nullptr);
    if (cue) SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    SetWindowSubclass(e, EditSubclass, 1, 0);
    return e;
}

// Moves `links` (call Checkpoint() first) and keeps them selected afterwards.
void MoveLinks(const std::vector<int>& links, int group, int before) {
    if (links.empty()) return;
    int focus = g_sel.link;
    std::wstring focusTarget = focus >= 0 ? g_store.links[focus].target : L"";
    std::set<std::wstring> picked;
    for (int i : links) picked.insert(g_store.links[i].target);
    std::vector<int> moved = g_store.MoveMany(links, group, before);
    int newFocus = focusTarget.empty() ? -1 : g_store.Find(focusTarget);
    if (newFocus < 0 || !picked.count(focusTarget)) newFocus = moved.front();
    SetSel({newFocus, group});
    if (picked.size() > 1) g_picked = std::move(picked);
    Save();
    RebuildRows();
    EnsureVisible(RowOfSelection());
}

void MoveSelected(int group, int before) { MoveLinks(SelectedLinks(), group, before); }

void OnCommand(int id) {
    int link = g_sel.link;
    int group = g_sel.group;
    switch (id) {
    case IDM_OPEN: OpenSelected(false); break;
    case IDM_LOCATION: OpenSelected(true); break;
    case IDM_COPY: CopySelected(); break;
    case IDM_RENAME:
        if (link >= 0) StartRename(Rename::Link, link);
        break;
    case IDM_EDITPATH:
        if (link >= 0) SetAddOpen(true, link);
        break;
    case IDM_REMOVE: RemoveLinks(SelectedLinks()); break;
    case IDM_NEWGROUP: {
        Checkpoint();
        int g = NewGroup();
        SetSel({-1, g});
        RebuildRows();
        StartRename(Rename::Group, g);
        break;
    }
    case IDM_RENAMEGROUP:
        if (group >= 0) StartRename(Rename::Group, group);
        break;
    case IDM_GROUPUP:
    case IDM_GROUPDOWN: {
        int other = group + (id == IDM_GROUPUP ? -1 : 1);
        if (group < 0 || other < 0 || other >= (int)g_store.groups.size()) break;
        Checkpoint();
        g_store.SwapGroups(group, other);
        g_sel.group = other;
        Save();
        RebuildRows();
        break;
    }
    case IDM_DELETEGROUP:
        if (group > 0 && group < (int)g_store.groups.size()) Checkpoint();
        if (group > 0 && g_store.RemoveGroup(group)) {
            SetSel({});
            Save();
            RebuildRows();
        }
        break;
    case IDM_ADD: SetAddOpen(!g_addOpen); break;
    case IDM_PASTE: PasteItems(); break;
    case IDM_TOGGLE: ShowWidget(!IsWindowVisible(g_wnd)); break;
    case IDM_TOPMOST:
        g_store.settings.topmost = !g_store.settings.topmost;
        ApplyTopmost();
        Save();
        InvalidateRect(g_wnd, nullptr, FALSE);
        break;
    case IDM_SINGLECLICK:
    case IDM_DOUBLECLICK:
        g_store.settings.doubleClick = id == IDM_DOUBLECLICK;
        Save();
        break;
    case IDM_EXIT: DestroyWindow(g_wnd); break;
    case IDM_UNDO: UndoRedo(false); break;
    case IDM_REDO: UndoRedo(true); break;
    case IDM_SELECTALL:
        PickAll();
        InvalidateRect(g_wnd, nullptr, FALSE);
        break;
    case IDM_CLOCK_OFF:
    case IDM_CLOCK_LARGE:
    case IDM_CLOCK_LINE:
    case IDM_CLOCK_CARD:
    case IDM_CLOCK_24H:
    case IDM_CLOCK_SECONDS:
    case IDM_CLOCK_TOP:
    case IDM_CLOCK_BOTTOM: {
        ql::Settings& s = g_store.settings;
        if (id == IDM_CLOCK_24H) s.clock24 = !s.clock24;
        else if (id == IDM_CLOCK_SECONDS) s.clockSeconds = !s.clockSeconds;
        else if (id == IDM_CLOCK_TOP || id == IDM_CLOCK_BOTTOM) s.clockPosition = id == IDM_CLOCK_TOP ? L"top" : L"bottom";
        else s.clock = id == IDM_CLOCK_LARGE ? L"large" : id == IDM_CLOCK_LINE ? L"line" : id == IDM_CLOCK_CARD ? L"card" : L"off";
        Save();
        Relayout();
        PositionEdits();
        ScheduleClock();
        InvalidateRect(g_wnd, nullptr, FALSE);
        break;
    }
    case IDM_TRANSPARENCY:
        ShowTransparencySlider(g_menuPoint);
        g_menuPoint = {};
        break;
    case IDM_THEME_SYSTEM:
    case IDM_THEME_LIGHT:
    case IDM_THEME_DARK:
        g_store.settings.theme = id == IDM_THEME_LIGHT ? L"light" : id == IDM_THEME_DARK ? L"dark" : L"system";
        Save();
        ApplyTheme();
        break;
    case IDM_MOVETO_NEW:
        if (link >= 0) {
            Checkpoint();
            int g = NewGroup();
            MoveSelected(g, -1);
            StartRename(Rename::Group, g);
        }
        break;
    default:
        if (id >= IDM_MOVETO && id < IDM_MOVETO + (int)g_store.groups.size() && link >= 0) {
            int g = id - IDM_MOVETO;
            std::vector<int> sel = SelectedLinks();
            if (std::all_of(sel.begin(), sel.end(), [g](int i) { return g_store.links[i].group == g; })) break;
            Checkpoint();
            g_store.groups[g].collapsed = false;
            MoveSelected(g, -1);
        }
    }
}

void OnNoticeAction(int id) {
    if (id == kActUndo) {
        UndoRedo(false);
        return;
    }
    int link = g_store.Find(g_notice.target);
    if (link < 0) {
        HideNotice();
        return;
    }
    if (id == kActEditPath) {
        SetSel({link, g_store.links[link].group});
        HideNotice();
        SetAddOpen(true, link);
    } else if (id == kActRemoveMissing) {
        HideNotice();
        RemoveLink(link);
    }
}

void OnLButtonDown(POINT p) {
    float x = Dip(p.x), y = Dip(p.y);
    Hit h = HitTest(x, y);
    if (g_renameKind != Rename::None) CommitRename(true);
    if (GetFocus() != g_wnd && h.zone != Zone::None) SetFocus(g_wnd);
    g_pressed = h;
    g_pressPt = p;
    g_pressPicked = false;
    g_collapseOnUp = false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (h.zone == Zone::Row || h.zone == Zone::RowMore) {
        const ql::Row& r = g_rows[h.row];
        bool item = r.type == ql::Row::Item;
        if (item && h.zone == Zone::Row && shift) {
            PickRange(h.row, ctrl);  // Shift: range, Ctrl+Shift: add a range
            g_pressPicked = true;
        } else if (item && h.zone == Zone::Row && ctrl) {
            TogglePick(r.link);  // Ctrl: one at a time
            g_pressPicked = true;
        } else if (item && IsPicked(r.link) && SelectedLinks().size() > 1) {
            // Keep the group selected so it can be dragged; a plain click narrows it on release.
            g_sel = {r.link, r.group};
            g_collapseOnUp = h.zone == Zone::Row;
        } else {
            SelectRow(h.row);
        }
        if (item && h.zone == Zone::Row && !g_pressPicked) {
            g_drag.link = r.link;
            g_drag.grabDy = y - ContentToClient(L.rowTop[h.row]);
        }
    }
    SetCapture(g_wnd);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void FinishDrag() {
    DropAt at = g_drag.at;
    std::vector<int> links = DragLinks();
    g_drag = {};
    EndDragUi();
    ReleaseCapture();
    if (links.empty() || (links.size() == 1 && at.before == links[0])) {
        RebuildRows();
        return;
    }
    Checkpoint();
    MoveLinks(links, at.group, at.before);
}

void OnLButtonUp(POINT p) {
    // Before releasing capture: WM_CAPTURECHANGED cancels a drag that is still marked active.
    if (g_drag.active) {
        FinishDrag();
        return;
    }
    if (GetCapture() == g_wnd) ReleaseCapture();
    g_drag.link = -1;
    Hit h = HitTest(Dip(p.x), Dip(p.y));
    Hit pressed = g_pressed;
    g_pressed = {};
    InvalidateRect(g_wnd, nullptr, FALSE);
    if (h != pressed) return;
    switch (h.zone) {
    case Zone::Pin: OnCommand(IDM_TOPMOST); break;
    case Zone::Plus: SetAddOpen(!g_addOpen); break;
    case Zone::Close: ShowWidget(false); break;
    case Zone::AddFolder: PickAndAdd(true); break;
    case Zone::AddFile: PickAndAdd(false); break;
    case Zone::EmptyAdd: PickAndAdd(true); break;
    case Zone::EmptyPaste: PasteItems(); break;
    case Zone::NoticeClose: HideNotice(); break;
    case Zone::NoticeAction: OnNoticeAction(g_noticeHits[h.row].second); break;
    case Zone::RowMore: {
        POINT s = p;
        ClientToScreen(g_wnd, &s);
        LinkMenu(g_rows[h.row].link, s);
        break;
    }
    case Zone::Row:
        if (g_rows[h.row].type == ql::Row::GroupHeader) {
            ToggleGroup(g_rows[h.row].group);
        } else if (!g_pressPicked) {  // Ctrl/Shift clicks only select
            if (g_collapseOnUp) SelectRow(h.row);
            if (!g_store.settings.doubleClick) OpenLink(g_rows[h.row].link, false);
        }
        break;
    default: break;
    }
}

void OnMouseMove(POINT p, WPARAM keys) {
    if (!g_mouseIn) {
        g_mouseIn = true;
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, g_wnd, 0};
        TrackMouseEvent(&t);
        InvalidateRect(g_wnd, nullptr, FALSE);
    }
    if ((keys & MK_LBUTTON) && g_drag.link >= 0 && !g_drag.active && g_filter.empty()) {
        float dx = Dip(p.x - g_pressPt.x), dy = Dip(p.y - g_pressPt.y);
        if (std::hypot(dx, dy) > kDragStart) {
            g_drag.active = true;
            SendMessageW(g_tip, TTM_POP, 0, 0);
        }
    }
    if (g_drag.active) {
        g_drag.pt = p;
        g_drag.at = ComputeDrop(Dip(p.y));
        UpdateAutoExpand(g_drag.at);
        UpdateAutoScroll(Dip(p.y));
        InvalidateRect(g_wnd, nullptr, FALSE);
        return;
    }
    Hit h = HitTest(Dip(p.x), Dip(p.y));
    if (g_notice.on && !g_notice.warn) {
        // Keep the notice while the mouse is on it.
        if (h.zone == Zone::Notice || h.zone == Zone::NoticeAction) KillTimer(g_wnd, kNoticeTimer);
        else if (g_hot.zone == Zone::Notice || g_hot.zone == Zone::NoticeAction)
            SetTimer(g_wnd, kNoticeTimer, kNoticeMs, nullptr);
    }
    SetHot(h);
}

LRESULT HitTestNc(LPARAM lp) {
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(g_wnd, &p);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int b = Px(kBorder);
    bool left = p.x < b, right = p.x >= rc.right - b, top = p.y < b, bottom = p.y >= rc.bottom - b;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    float x = Dip(p.x), y = Dip(p.y);
    if (y < kHeaderH && !In(L.pin, x, y) && !In(L.plus, x, y) && !In(L.close, x, y)) return HTCAPTION;
    if (In(L.clock, x, y)) return HTCAPTION;  // the clock moves the widget too
    return HTCLIENT;
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = g_wnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_iconSmall;
    wcscpy_s(nid.szTip, g_testInstance ? L"QuickLinks (테스트)" : L"QuickLinks");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = g_wnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCALCSIZE:
        if (w) return 0;  // the client area covers the whole window; DWM still draws border and shadow
        break;
    case WM_NCHITTEST: return HitTestNc(l);
    case WM_NCRBUTTONDOWN: return 0;
    case WM_NCRBUTTONUP:  // the title bar: same menu as the tray icon
        if (w == HTCAPTION) TrayMenu({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(); return 0;
    case WM_SIZE:
        Relayout();
        PositionEdits();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {Px(200), Px(180)};
        return 0;
    }
    case WM_EXITSIZEMOVE: SaveWindowRect(); return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        auto* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        CreateEditFonts();
        ReleaseBitmaps(true);
        RequestIcons();
        Relayout();
        PositionEdits();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (l && !lstrcmpW((LPCWSTR)l, L"ImmersiveColorSet")) ApplyTheme();
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, ToRef(g_theme.text));
        SetBkColor(dc, ToRef(g_theme.field));
        return (LRESULT)g_fieldBrush;
    }
    case WM_COMMAND:
        if ((HWND)l == g_search && HIWORD(w) == EN_CHANGE) {
            g_filter = EditText(g_search);
            g_scroll = 0;
            RebuildRows();
            if (!g_filter.empty() && RowOfSelection() < 0) SetSel({});
            return 0;
        }
        if ((HWND)l == g_addEdit && HIWORD(w) == EN_CHANGE) {
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if ((HWND)l == g_renameEdit && HIWORD(w) == EN_KILLFOCUS) {
            CommitRename(true);
            return 0;
        }
        if (!l) OnCommand(LOWORD(w));
        return 0;
    case WM_NOTIFY: {
        auto* nm = (NMHDR*)l;
        if (nm->hwndFrom == g_tip && nm->code == TTN_GETDISPINFOW) {
            ((NMTTDISPINFOW*)l)->lpszText = (LPWSTR)g_tipText.c_str();
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE: OnMouseMove({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, w); return 0;
    case WM_MOUSELEAVE:
        g_mouseIn = false;
        SetHot({});
        if (g_notice.on && !g_notice.warn) SetTimer(h, kNoticeTimer, kNoticeMs, nullptr);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: OnLButtonDown({GET_X_LPARAM(l), GET_Y_LPARAM(l)}); return 0;
    case WM_LBUTTONDBLCLK: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        Hit hit = HitTest(Dip(p.x), Dip(p.y));
        bool modifier = GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0;
        bool item = hit.zone == Zone::Row && g_rows[hit.row].type == ql::Row::Item;
        if (item && g_store.settings.doubleClick && !modifier) {
            SelectRow(hit.row);
            OpenLink(g_rows[hit.row].link, false);
        } else if (!item || modifier) {
            OnLButtonDown(p);  // buttons, headers and Ctrl/Shift selection react to every click
        }
        return 0;
    }
    case WM_LBUTTONUP: OnLButtonUp({GET_X_LPARAM(l), GET_Y_LPARAM(l)}); return 0;
    case WM_CAPTURECHANGED:
        if (g_drag.active && (HWND)l != h) {
            g_drag = {};
            EndDragUi();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MBUTTONUP: {
        Hit hit = HitTest(Dip(GET_X_LPARAM(l)), Dip(GET_Y_LPARAM(l)));
        if (hit.zone == Zone::Row && g_rows[hit.row].type == ql::Row::Item) CopyLink(g_rows[hit.row].link);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        Hit hit = HitTest(Dip(p.x), Dip(p.y));
        CommitRename(true);
        ClientToScreen(h, &p);
        if (hit.zone == Zone::Row || hit.zone == Zone::RowMore) {
            const ql::Row& r = g_rows[hit.row];
            // Right-clicking inside a multi-selection keeps it; elsewhere selects just that row.
            if (r.type == ql::Row::Item && IsPicked(r.link) && SelectedLinks().size() > 1) g_sel = {r.link, r.group};
            else SelectRow(hit.row);
            InvalidateRect(h, nullptr, FALSE);
            if (r.type == ql::Row::Item) LinkMenu(r.link, p);
            else GroupMenu(r.group, p);
        } else if (hit.zone == Zone::List) {
            ListMenu(p);
        } else if (Dip(GET_Y_LPARAM(l)) < kHeaderH) {
            TrayMenu(p);  // header buttons area: same as the title bar
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) {  // Ctrl+wheel: 1% per notch, up = more see-through (as the slider)
            SetTransparency(Transparency() + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1), true);
            return 0;
        }
        CommitRename(true);
        g_scroll -= GET_WHEEL_DELTA_WPARAM(w) / 120.f * 48;
        ClampScroll();
        PositionEdits();
        g_tipRow = -2;
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &p);
        SetHot(HitTest(Dip(p.x), Dip(p.y)));
        UpdateTooltip();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (OnKey(w)) return 0;
        break;
    case WM_CHAR:
        // Typing on the list starts a search.
        if (w >= 32 && w != 127 && GetKeyState(VK_CONTROL) >= 0) {
            if (g_addOpen) SetAddOpen(false);
            SetFocus(g_search);
            SendMessageW(g_search, WM_CHAR, w, l);
            return 0;
        }
        break;
    case WM_TIMER:
        switch (w) {
        case kNoticeTimer: HideNotice(); break;
        case kExpandTimer:
            KillTimer(h, kExpandTimer);
            if (g_expandGroup >= 0 && g_expandGroup < (int)g_store.groups.size()) {
                g_store.groups[g_expandGroup].collapsed = false;
                g_expandGroup = -1;
                Save();
                RebuildRows();
            }
            break;
        case kScrollTimer: {
            POINT p = g_drag.active ? g_drag.pt : g_ext.pt;
            float y = Dip(p.y);
            float step = y < L.listTop + 24 ? -8.f : y > L.listBottom - 24 ? 8.f : 0.f;
            if (step == 0 || (!g_drag.active && !g_ext.active)) {
                KillTimer(h, kScrollTimer);
                break;
            }
            g_scroll += step;
            ClampScroll();
            if (g_drag.active) g_drag.at = ComputeDrop(y);
            else g_ext.at = ComputeDrop(y);
            InvalidateRect(h, nullptr, FALSE);
            break;
        }
        case kRecheckTimer:
            if (IsWindowVisible(h)) RequestIcons();
            break;
        case kClockTimer:
            if (IsWindowVisible(h)) InvalidateClock();
            ScheduleClock();
            break;
        }
        return 0;
    case WM_APP_ICON: OnIcon((shell::IconResult*)l); return 0;
    case WM_APP_OPENED: {
        auto* target = (std::wstring*)l;
        if (w) {
            auto it = g_icons.find(*target);
            if (it != g_icons.end()) it->second.state = shell::State::Missing;
            ShowMissing(*target);
        } else if (IsMissing(*target)) {
            RequestIcons();
        }
        delete target;
        return 0;
    }
    case WM_APP_TRAY:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP:
        case NIN_SELECT:
        case NIN_KEYSELECT: ShowWidget(!IsWindowVisible(h)); break;
        case WM_CONTEXTMENU: TrayMenu({GET_X_LPARAM(w), GET_Y_LPARAM(w)}); break;
        }
        return 0;
    case WM_APP_SHOW: ShowWidget(true); return 0;
    case WM_TIMECHANGE:
        InvalidateClock();
        ScheduleClock();
        return 0;
    case WM_APP_QUIT: DestroyWindow(h); return 0;
    case WM_CLOSE: ShowWidget(false); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (w) SaveWindowRect();
        return 0;
    case WM_DESTROY:
        SaveWindowRect();
        RevokeDragDrop(h);
        RemoveTrayIcon();
        g_worker.Stop();
        PostQuitMessage(0);
        return 0;
    default:
        if (m == g_taskbarCreated && m) {
            AddTrayIcon();
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

std::wstring DefaultIniPath() {
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        path = std::wstring(appData) + L"\\QuickLinks\\QuickLinks.ini";
        CoTaskMemFree(appData);
    }
    return path;
}

RECT InitialRect() {
    const ql::Settings& s = g_store.settings;
    POINT at{s.x, s.y};
    HMONITOR mon = s.x == ql::Settings::kUnset ? MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY)
                                               : MonitorFromPoint(at, MONITOR_DEFAULTTONULL);
    bool fallback = !mon;
    if (fallback) mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    g_dpi = dx;
    int w = Px((float)s.width), hgt = Px((float)s.height);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    RECT r;
    if (s.x == ql::Settings::kUnset || fallback) {
        r.left = mi.rcWork.right - w - Px(24);
        r.top = mi.rcWork.top + Px(24);
    } else {
        r.left = s.x;
        r.top = s.y;
    }
    r.right = r.left + w;
    r.bottom = r.top + hgt;
    // Keep the header reachable when the saved spot is partly off-screen.
    RECT header{r.left, r.top, r.right, r.top + Px(kHeaderH)};
    if (!MonitorFromRect(&header, MONITOR_DEFAULTTONULL)) {
        OffsetRect(&r, mi.rcWork.right - w - Px(24) - r.left, mi.rcWork.top + Px(24) - r.top);
    }
    return r;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i)
        if (!wcscmp(argv[i], L"--config")) {
            wchar_t full[MAX_PATH];
            GetFullPathNameW(argv[i + 1], MAX_PATH, full, nullptr);
            g_iniPath = full;
            g_testInstance = true;
        } else if (!wcscmp(argv[i], L"--theme")) {
            g_forceTheme = !wcscmp(argv[i + 1], L"dark") ? 1 : 0;
        }
    for (int i = 1; i < argc; ++i)
        if (!wcscmp(argv[i], L"--win10-fonts")) g_forceWin10Fonts = true;
    LocalFree(argv);

    HANDLE mutex = nullptr;
    if (!g_testInstance) {
        mutex = CreateMutexW(nullptr, TRUE, L"QuickLinks.SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND other = FindWindowW(kClass, kAppName)) PostMessageW(other, WM_APP_SHOW, 0, 0);
            return 0;
        }
    }

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    if (!g_d2d || !g_dw || !g_wic) {
        MessageBoxW(nullptr, L"그래픽 구성 요소(Direct2D)를 초기화하지 못했어요.", kAppName, MB_ICONERROR);
        return 1;
    }
    CreateFormats();

    if (g_iniPath.empty()) g_iniPath = DefaultIniPath();
    g_store.Load(g_iniPath);
    g_rows = g_store.Rows(L"");

    // ("small" is a macro from rpcndr.h.)
    g_iconSmall = MakeAppIcon(GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()));
    g_iconBig = MakeAppIcon(GetSystemMetricsForDpi(SM_CXICON, GetDpiForSystem()));

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_iconBig;
    wc.hIconSm = g_iconSmall;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    wc.style = 0;
    wc.lpfnWndProc = SliderProc;
    wc.lpszClassName = kSliderClass;
    RegisterClassExW(&wc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    RECT r = InitialRect();
    const ql::Settings& s = g_store.settings;
    DWORD ex = WS_EX_TOOLWINDOW | (s.topmost ? WS_EX_TOPMOST : 0);
    g_wnd = CreateWindowExW(ex, kClass, g_testInstance ? L"QuickLinks (test)" : kAppName,
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, r.left, r.top, r.right - r.left,
                            r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) return 1;
    g_dpi = GetDpiForWindow(g_wnd);
    ApplyOpacity();
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(g_wnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner);

    g_search = MakeEdit(L"검색");
    g_addEdit = MakeEdit(L"경로나 웹 주소 붙여넣기");
    g_renameEdit = MakeEdit(nullptr);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0,
                            0, g_wnd, nullptr, inst, nullptr);
    TOOLINFOW ti{sizeof ti};
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = g_wnd;
    ti.uId = 1;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 600);
    SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);

    CreateEditFonts();
    ApplyTheme();
    Relayout();
    PositionEdits();
    SendMessageW(g_wnd, WM_SETICON, ICON_SMALL, (LPARAM)g_iconSmall);
    SendMessageW(g_wnd, WM_SETICON, ICON_BIG, (LPARAM)g_iconBig);

    RegisterDragDrop(g_wnd, &g_dropTarget);
    AddTrayIcon();
    g_worker.Start(g_wnd, WM_APP_ICON);
    RequestIcons();
    SetTimer(g_wnd, kRecheckTimer, kRecheckMs, nullptr);
    ScheduleClock();

    // A test instance must not take the keyboard from whatever the user is typing into.
    ShowWindow(g_wnd, g_testInstance ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(g_wnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
