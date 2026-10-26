// Test helper for the QuickLinks test instance (started with --config). Build: tools\build_qltool.bat
//
//   qltool capture out.png [slider|settings]   the widget's (or its open slider's / settings window's) pixels
//                                     (screen copy of that window rect only)
//   qltool cmd <id>          post WM_COMMAND <id> (ids: enum at the top of main.cpp)
//   qltool key <vk>          post WM_KEYDOWN <vk> to the open slider, else the focused widget control
//                            (no real keyboard input)
//   qltool type <text>       post WM_CHAR for each character to the list (starts a search)
//   qltool quit              close the test instance
//
// Only the test window (title "QuickLinks (test)") is targeted, never the user's own instance.
// Captures copy from the screen, so whatever really shows is what gets saved; only the widget's
// rectangle is copied to keep the user's other windows out of the file.
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>

#include <cstdio>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

static bool SavePng(HBITMAP bmp, const wchar_t* path) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    IWICBitmap* src = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = SUCCEEDED(f->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &src)) &&
              SUCCEEDED(f->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->WriteSource(src, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (src) src->Release();
    f->Release();
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 2) return 2;
    HWND w = FindWindowW(L"QuickLinksWidget", L"QuickLinks (test)");
    if (!w) {
        fwprintf(stderr, L"no test window\n");
        return 1;
    }
    const wchar_t* cmd = argv[1];
    // The transparency slider, when the test widget has one open.
    HWND slider = FindWindowW(L"QuickLinksSlider", nullptr);
    if (slider && GetWindow(slider, GW_OWNER) != w) slider = nullptr;
    if (!wcscmp(cmd, L"capture") && argc > 2) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (argc > 3 && !wcscmp(argv[3], L"slider")) {
            if (!slider) {
                fwprintf(stderr, L"no slider open\n");
                return 1;
            }
            w = slider;
        } else if (argc > 3 && !wcscmp(argv[3], L"settings")) {
            HWND s = FindWindowW(L"QuickLinksSettings", nullptr);
            if (!s || GetWindow(s, GW_OWNER) != w) {
                fwprintf(stderr, L"no settings window open\n");
                return 1;
            }
            w = s;
        }
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(w, &r);
        // Refuse when the widget is hidden or covered: the copy would show the user's screen instead.
        if (!IsWindowVisible(w) || IsIconic(w)) {
            fwprintf(stderr, L"widget not visible, nothing captured\n");
            return 1;
        }
        int ix = (r.right - r.left) / 8, iy = (r.bottom - r.top) / 8;
        POINT probes[] = {{(r.left + r.right) / 2, (r.top + r.bottom) / 2}, {r.left + ix, r.top + iy},
                          {r.right - ix, r.top + iy}, {r.left + ix, r.bottom - iy}, {r.right - ix, r.bottom - iy}};
        for (POINT p : probes) {
            HWND at = GetAncestor(WindowFromPoint(p), GA_ROOT);
            if (at != w && GetWindow(at, GW_OWNER) != w) {
                fwprintf(stderr, L"widget covered at (%ld,%ld), nothing captured\n", p.x, p.y);
                return 1;
            }
        }
        int cw = r.right - r.left, ch = r.bottom - r.top;
        HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cw, ch);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, cw, ch, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
        SelectObject(mem, old);
        bool ok = SavePng(bmp, argv[2]);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        wprintf(L"%dx%d at (%d,%d) %s\n", cw, ch, r.left, r.top, ok ? L"saved" : L"FAILED");
        return ok ? 0 : 1;
    }
    if (!wcscmp(cmd, L"cmd") && argc > 2) return PostMessageW(w, WM_COMMAND, _wtoi(argv[2]), 0) ? 0 : 1;
    if (!wcscmp(cmd, L"key") && argc > 2) {
        // To whichever widget control has focus (the search box, a rename box, or the list itself).
        GUITHREADINFO gti{sizeof gti};
        HWND target = w;
        if (slider) return PostMessageW(slider, WM_KEYDOWN, _wtoi(argv[2]), 0) ? 0 : 1;
        if (GetGUIThreadInfo(GetWindowThreadProcessId(w, nullptr), &gti) && gti.hwndFocus &&
            (gti.hwndFocus == w || IsChild(w, gti.hwndFocus)))
            target = gti.hwndFocus;
        return PostMessageW(target, WM_KEYDOWN, _wtoi(argv[2]), 0) ? 0 : 1;
    }
    if (!wcscmp(cmd, L"type") && argc > 2) {  // characters to the list (typing there starts a search)
        for (const wchar_t* c = argv[2]; *c; ++c) PostMessageW(w, WM_CHAR, *c, 0);
        return 0;
    }
    if (!wcscmp(cmd, L"quit")) return PostMessageW(w, WM_APP + 5, 0, 0) ? 0 : 1;  // WM_APP_QUIT
    return 2;
}
