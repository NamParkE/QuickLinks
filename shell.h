// Shell side of QuickLinks: opening targets, icons and existence checks off the UI thread,
// reading dropped or pasted paths/URLs.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace shell {

std::wstring Expand(const std::wstring& target);  // %VARS% → value

// Opens on a background thread (a missing network path can block for many seconds) and posts
// `msg` to `notify` with wParam = Win32 error (0 = ok) and lParam = new std::wstring(target).
void OpenAsync(HWND notify, UINT msg, const std::wstring& target, bool location);

bool CopyText(HWND owner, const std::wstring& text);

struct Dropped {
    std::wstring target, name;  // name may be empty (use the default)
};
std::vector<Dropped> FromDataObject(IDataObject* data);
std::vector<Dropped> FromClipboard(HWND owner);

enum class State { Unknown, Exists, Missing };

// Result posted to the UI thread; the receiver owns and frees it (and its bitmap/icon).
struct IconResult {
    std::wstring target;
    int px = 0;
    HBITMAP bitmap = nullptr;  // premultiplied 32bpp from the shell, or
    HICON icon = nullptr;      // a generic icon (no disk access)
    State state = State::Unknown;
    bool directory = false;
};

class IconWorker {
public:
    void Start(HWND notify, UINT msg);
    void Stop();
    // Replaces any pending work. First posts generic icons for all targets, then real icons and
    // existence (local paths before network paths).
    void Request(const std::vector<std::wstring>& targets, int px);

private:
    void Run();
    HWND notify_ = nullptr;
    UINT msg_ = 0;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::wstring> pending_;
    int px_ = 16;
    bool hasWork_ = false, stop_ = false;
};

}  // namespace shell
