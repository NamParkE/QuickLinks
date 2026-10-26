// Mica spike: checks the open questions in docs/설계.md 2.5 before the real widget is written.
//
//   mica_spike.exe <popup|thick|caption> <x> <y> <out-prefix>
//
// Shows one 280x200 topmost window, captures it from the screen while active and again after a
// helper window takes activation, writes <prefix>_active.png, <prefix>_inactive.png and
// <prefix>.log, then exits. No mouse or keyboard input is sent.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite_2.h>
#include <uxtheme.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr DWORD kBackdropType = 38;  // DWMWA_SYSTEMBACKDROP_TYPE
constexpr DWORD kCornerPref = 33;    // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr int kMainWindowBackdrop = 2;  // DWMSBT_MAINWINDOW (Mica)
constexpr int kRound = 2;               // DWMWCP_ROUND

std::wstring g_variant, g_prefix;
FILE* g_log = nullptr;
HWND g_main = nullptr, g_helper = nullptr;
ID2D1Factory* g_d2d = nullptr;
IDWriteFactory2* g_dw = nullptr;
ID2D1DCRenderTarget* g_rt = nullptr;
int g_step = 0;

void Log(const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfwprintf(g_log, fmt, a);
    va_end(a);
    fputwc(L'\n', g_log);
    fflush(g_log);
}

float Scale(HWND h) { return GetDpiForWindow(h) / 96.0f; }

D2D1_COLOR_F Rgba(UINT rgb, float a = 1) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}

void Text(const wchar_t* s, float x, float y, float size, DWRITE_FONT_WEIGHT w, D2D1_COLOR_F c, float k) {
    IDWriteTextFormat* fmt = nullptr;
    g_dw->CreateTextFormat(L"Segoe UI Variable Text", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                           DWRITE_FONT_STRETCH_NORMAL, size * k, L"ko-kr", &fmt);
    ID2D1SolidColorBrush* b = nullptr;
    g_rt->CreateSolidColorBrush(c, &b);
    g_rt->DrawText(s, (UINT32)wcslen(s), fmt, D2D1::RectF(x * k, y * k, 280 * k, (y + 40) * k), b);
    b->Release();
    fmt->Release();
}

void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    float k = Scale(h);
    BP_PAINTPARAMS pp{sizeof pp, BPPF_ERASE, nullptr, nullptr};
    HDC mem = nullptr;
    HPAINTBUFFER pb = BeginBufferedPaint(hdc, &rc, BPBF_TOPDOWNDIB, &pp, &mem);
    if (!pb) {
        EndPaint(h, &ps);
        return;
    }

    // GDI strip first: opaque white, then GDI text on it (font linking check). Alpha fixed below.
    RECT strip{(LONG)(8 * k), (LONG)(150 * k), rc.right - (LONG)(8 * k), (LONG)(186 * k)};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &strip, white);
    DeleteObject(white);
    HFONT f = CreateFontW(-(int)(14 * k), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Segoe UI Variable Text");
    HGDIOBJ old = SelectObject(mem, f);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(26, 26, 26));
    RECT tr = strip;
    tr.left += (LONG)(8 * k);
    DrawTextW(mem, L"GDI: 2026 견적서 · 월간 보고서.xlsx", -1, &tr, DT_SINGLELINE | DT_VCENTER);
    SelectObject(mem, old);
    DeleteObject(f);
    BufferedPaintSetAlpha(pb, &strip, 255);

    // Everything else with Direct2D in premultiplied alpha, so Mica shows where alpha is 0.
    g_rt->BindDC(mem, &rc);
    g_rt->BeginDraw();
    // Keep the GDI strip: only clear above it.
    g_rt->PushAxisAlignedClip(D2D1::RectF(0, 0, (float)rc.right, 148 * k), D2D1_ANTIALIAS_MODE_ALIASED);
    g_rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    Text((L"바로가기 · " + g_variant).c_str(), 12, 10, 12, DWRITE_FONT_WEIGHT_REGULAR, Rgba(0x1A1A1A), k);

    ID2D1SolidColorBrush* b = nullptr;
    g_rt->CreateSolidColorBrush(Rgba(0x000000, 0.055f), &b);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(6 * k, 38 * k, 274 * k, 70 * k), 4 * k, 4 * k), b);
    b->SetColor(Rgba(0x005FB8));
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(6 * k, 46 * k, 9 * k, 62 * k), 1.5f * k, 1.5f * k), b);
    b->Release();
    Text(L"스캔 받은 파일 (D2D, 선택 줄)", 32, 44, 14, DWRITE_FONT_WEIGHT_REGULAR, Rgba(0x1A1A1A), k);
    Text(L"월간 보고서.xlsx · 2026 견적서", 32, 78, 14, DWRITE_FONT_WEIGHT_REGULAR, Rgba(0x1A1A1A), k);
    Text(L"업무  3", 12, 112, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD, Rgba(0x5F6368), k);
    g_rt->PopAxisAlignedClip();
    HRESULT hr = g_rt->EndDraw();
    if (FAILED(hr)) Log(L"EndDraw failed 0x%08X", hr);

    EndBufferedPaint(pb, TRUE);
    EndPaint(h, &ps);
}

bool SavePng(HBITMAP bmp, const std::wstring& path) {
    CLSID png{0x557CF406, 0x1A04, 0x11D3, {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}};
    Gdiplus::Bitmap b(bmp, nullptr);
    return b.Save(path.c_str(), &png) == Gdiplus::Ok;
}

void Capture(const wchar_t* tag) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(g_main, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(g_main, &r);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, hgt);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, hgt, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    std::wstring path = g_prefix + L"_" + tag + L".png";
    bool ok = SavePng(bmp, path);
    Log(L"capture %s: %dx%d at (%d,%d) foreground=%s saved=%d", tag, w, hgt, r.left, r.top,
        GetForegroundWindow() == g_main ? L"main" : (GetForegroundWindow() == g_helper ? L"helper" : L"other"), ok);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK HelperProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(GRAY_BRUSH));
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCALCSIZE:
        if (w && g_variant != L"popup") return 0;  // client area = whole window
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        static HBRUSH field = CreateSolidBrush(RGB(255, 255, 255));
        SetBkColor((HDC)w, RGB(255, 255, 255));
        SetTextColor((HDC)w, RGB(26, 26, 26));
        return (LRESULT)field;
    }
    case WM_PAINT:
        Paint(h);
        return 0;
    case WM_TIMER:
        if (g_step == 0) {
            Capture(L"active");
            RECT r;
            GetWindowRect(h, &r);
            g_helper = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"SpikeHelper", L"spike", WS_POPUP | WS_VISIBLE,
                                       r.left, r.bottom + 12, 120, 30, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            SetForegroundWindow(g_helper);
            g_step = 1;
        } else {
            Capture(L"inactive");
            KillTimer(h, 1);
            DestroyWindow(g_helper);
            DestroyWindow(h);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 5) return 2;
    g_variant = argv[1];
    int x = _wtoi(argv[2]), y = _wtoi(argv[3]);
    g_prefix = argv[4];
    g_log = _wfopen((g_prefix + L".log").c_str(), L"w, ccs=UTF-8");
    if (!g_log) return 3;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);
    BufferedPaintInit();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), (IUnknown**)&g_dw);
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                              96, 96);
    HRESULT hr = g_d2d->CreateDCRenderTarget(&props, &g_rt);
    Log(L"variant=%s CreateDCRenderTarget=0x%08X", g_variant.c_str(), hr);

    // Which font DirectWrite falls back to for Hangul when asked for Segoe UI Variable Text.
    IDWriteFontFallback* fb = nullptr;
    g_dw->GetSystemFontFallback(&fb);
    IDWriteFontCollection* sys = nullptr;
    g_dw->GetSystemFontCollection(&sys);
    UINT32 idx = 0;
    BOOL exists = FALSE;
    sys->FindFamilyName(L"Segoe UI Variable Text", &idx, &exists);
    Log(L"family 'Segoe UI Variable Text' in system collection: %d", exists);
    struct Src : IDWriteTextAnalysisSource {
        const wchar_t* t;
        HRESULT __stdcall QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
        ULONG __stdcall AddRef() override { return 1; }
        ULONG __stdcall Release() override { return 1; }
        HRESULT __stdcall GetTextAtPosition(UINT32 p, const WCHAR** s, UINT32* n) override {
            UINT32 len = (UINT32)wcslen(t);
            *s = p < len ? t + p : nullptr;
            *n = p < len ? len - p : 0;
            return S_OK;
        }
        HRESULT __stdcall GetTextBeforePosition(UINT32, const WCHAR** s, UINT32* n) override {
            *s = nullptr;
            *n = 0;
            return S_OK;
        }
        DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() override {
            return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }
        HRESULT __stdcall GetLocaleName(UINT32, UINT32* n, const WCHAR** l) override {
            *n = 1000;
            *l = L"ko-kr";
            return S_OK;
        }
        HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32* n, IDWriteNumberSubstitution** s) override {
            *n = 1000;
            *s = nullptr;
            return S_OK;
        }
    } src;
    src.t = L"견적서";
    UINT32 mapped = 0;
    IDWriteFont* font = nullptr;
    FLOAT scale = 1;
    fb->MapCharacters(&src, 0, 3, sys, L"Segoe UI Variable Text", DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                      DWRITE_FONT_STRETCH_NORMAL, &mapped, &font, &scale);
    if (font) {
        IDWriteFontFamily* fam = nullptr;
        font->GetFontFamily(&fam);
        IDWriteLocalizedStrings* names = nullptr;
        fam->GetFamilyNames(&names);
        wchar_t name[100] = L"?";
        names->GetString(0, name, 100);
        Log(L"DirectWrite fallback for Hangul: '%s' (mapped %u chars)", name, mapped);
    } else {
        Log(L"DirectWrite fallback for Hangul: none");
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MicaSpike";
    RegisterClassW(&wc);
    wc.lpfnWndProc = HelperProc;
    wc.lpszClassName = L"SpikeHelper";
    RegisterClassW(&wc);

    DWORD style = WS_POPUP;
    if (g_variant == L"thick") style |= WS_THICKFRAME;
    if (g_variant == L"caption") style = WS_OVERLAPPEDWINDOW;
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"MicaSpike", L"Mica spike", style, x, y, 280, 200,
                             nullptr, nullptr, inst, nullptr);
    g_main = h;
    float k = Scale(h);
    SetWindowPos(h, nullptr, x, y, (int)(280 * k), (int)(200 * k), SWP_NOZORDER | SWP_FRAMECHANGED);

    MARGINS mg{-1, -1, -1, -1};
    hr = DwmExtendFrameIntoClientArea(h, &mg);
    Log(L"DwmExtendFrameIntoClientArea=0x%08X dpi=%u", hr, GetDpiForWindow(h));
    int v = kMainWindowBackdrop;
    hr = DwmSetWindowAttribute(h, kBackdropType, &v, sizeof v);
    Log(L"DWMWA_SYSTEMBACKDROP_TYPE(Mica)=0x%08X", hr);
    v = kRound;
    hr = DwmSetWindowAttribute(h, kCornerPref, &v, sizeof v);
    Log(L"DWMWA_WINDOW_CORNER_PREFERENCE(round)=0x%08X", hr);

    // A standard EDIT (needed for Korean IME) drawn by GDI on top of the extended frame.
    HWND edit = CreateWindowExW(0, L"EDIT", L"EDIT: 검색 견적서", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                (int)(150 * k), (int)(108 * k), (int)(120 * k), (int)(24 * k), h, nullptr, inst, nullptr);
    HFONT ef = CreateFontW(-(int)(13 * k), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                           L"Segoe UI Variable Text");
    SendMessageW(edit, WM_SETFONT, (WPARAM)ef, TRUE);

    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    UpdateWindow(h);
    SetTimer(h, 1, 1200, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Log(L"done");
    fclose(g_log);
    return 0;
}
