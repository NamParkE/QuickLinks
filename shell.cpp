#include "shell.h"

#include "links.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace shell {

namespace {

bool IsUnc(const std::wstring& s) { return s.size() > 2 && s[0] == L'\\' && s[1] == L'\\'; }

std::wstring BrowserExe() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = (DWORD)(sizeof buf / sizeof buf[0]);
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_EXECUTABLE, L"https", nullptr, buf, &n))) return buf;
    return L"";
}

HBITMAP ShellImage(const std::wstring& path, int px) {
    IShellItemImageFactory* f = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) return nullptr;
    HBITMAP bmp = nullptr;
    if (FAILED(f->GetImage(SIZE{px, px}, SIIGBF_ICONONLY, &bmp))) bmp = nullptr;
    f->Release();
    return bmp;
}

HICON GenericIcon(const std::wstring& target, int px) {
    SHFILEINFOW sfi{};
    UINT size = px > 16 ? SHGFI_LARGEICON : SHGFI_SMALLICON;
    if (ql::IsUrl(target)) {
        SHGetFileInfoW(L".url", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | size);
        return sfi.hIcon;
    }
    std::wstring leaf = ql::DefaultName(target);
    DWORD attr = leaf.find(L'.') != std::wstring::npos && leaf.size() > 2 ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY;
    SHGetFileInfoW(Expand(target).c_str(), attr, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | size);
    return sfi.hIcon;
}

std::wstring GlobalText(HGLOBAL h) {
    std::wstring s;
    if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
        s.assign(p, wcsnlen(p, GlobalSize(h) / sizeof(wchar_t)));
        GlobalUnlock(h);
    }
    return s;
}

void AddParsed(std::vector<Dropped>& out, const std::wstring& text, const std::wstring& name) {
    ql::Parsed p = ql::ParseInput(text);
    if (p.kind == ql::InputKind::Invalid) return;
    for (auto& d : out)
        if (ql::SameTarget(d.target, p.target)) return;
    out.push_back({p.target, name});
}

void FromText(std::vector<Dropped>& out, const std::wstring& text) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring::npos) end = text.size();
        AddParsed(out, text.substr(start, end - start), L"");
        start = end + 1;
    }
}

void FromHdrop(std::vector<Dropped>& out, HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(drop, i, &path[0], len + 1);
        AddParsed(out, path, L"");
    }
}

}  // namespace

std::wstring Expand(const std::wstring& target) {
    if (target.find(L'%') == std::wstring::npos) return target;
    wchar_t buf[MAX_PATH * 4];
    DWORD n = ExpandEnvironmentStringsW(target.c_str(), buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return n && n <= sizeof buf / sizeof buf[0] ? std::wstring(buf) : target;
}

void OpenAsync(HWND notify, UINT msg, const std::wstring& target, bool location) {
    // Let the opened window come to the front even though the call happens on another thread.
    AllowSetForegroundWindow(ASFW_ANY);
    std::thread([notify, msg, target, location] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::wstring path = ql::IsUrl(target) ? target : Expand(target);
        DWORD err = 0;
        if (location && !ql::IsUrl(target)) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
            if (SUCCEEDED(hr)) {
                hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                CoTaskMemFree(pidl);
            }
            if (FAILED(hr)) err = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : ERROR_PATH_NOT_FOUND;
        } else {
            if (!ql::IsUrl(target) && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                err = GetLastError();
                if (err == 0) err = ERROR_PATH_NOT_FOUND;
            } else {
                SHELLEXECUTEINFOW sei{sizeof sei};
                sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
                sei.lpFile = path.c_str();
                sei.nShow = SW_SHOWNORMAL;
                if (!ShellExecuteExW(&sei)) err = GetLastError();
            }
        }
        CoUninitialize();
        auto* t = new std::wstring(target);
        if (!PostMessageW(notify, msg, err, (LPARAM)t)) delete t;
    }).detach();
}

bool CopyText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (h) {
        memcpy(GlobalLock(h), text.c_str(), bytes);
        GlobalUnlock(h);
        ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

std::vector<Dropped> FromDataObject(IDataObject* data) {
    std::vector<Dropped> out;
    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM sm{};
    if (SUCCEEDED(data->GetData(&fe, &sm))) {
        FromHdrop(out, (HDROP)sm.hGlobal);
        ReleaseStgMedium(&sm);
        if (!out.empty()) return out;
    }

    // Browsers: the URL, plus the page title as the name of a virtual "<title>.url" file.
    static const CLIPFORMAT cfUrl = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_INETURLW);
    static const CLIPFORMAT cfDesc = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    fe.cfFormat = cfUrl;
    if (SUCCEEDED(data->GetData(&fe, &sm))) {
        std::wstring url = GlobalText(sm.hGlobal);
        ReleaseStgMedium(&sm);
        std::wstring title;
        fe.cfFormat = cfDesc;
        if (SUCCEEDED(data->GetData(&fe, &sm))) {
            if (auto* fgd = (FILEGROUPDESCRIPTORW*)GlobalLock(sm.hGlobal)) {
                if (fgd->cItems > 0) title = fgd->fgd[0].cFileName;
                GlobalUnlock(sm.hGlobal);
            }
            ReleaseStgMedium(&sm);
            if (title.size() > 4 && _wcsicmp(title.c_str() + title.size() - 4, L".url") == 0)
                title.resize(title.size() - 4);
        }
        AddParsed(out, url, title);
        if (!out.empty()) return out;
    }

    fe.cfFormat = CF_UNICODETEXT;
    if (SUCCEEDED(data->GetData(&fe, &sm))) {
        FromText(out, GlobalText(sm.hGlobal));
        ReleaseStgMedium(&sm);
    }
    return out;
}

std::vector<Dropped> FromClipboard(HWND owner) {
    std::vector<Dropped> out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_HDROP)) {
        FromHdrop(out, (HDROP)h);
    } else if (HANDLE t = GetClipboardData(CF_UNICODETEXT)) {
        FromText(out, GlobalText(t));
    }
    CloseClipboard();
    return out;
}

void IconWorker::Start(HWND notify, UINT msg) {
    notify_ = notify;
    msg_ = msg;
    thread_ = std::thread([this] { Run(); });
}

void IconWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    // A network check may hang for a long time; don't hold up exit for it.
    if (thread_.joinable()) thread_.detach();
}

void IconWorker::Request(const std::vector<std::wstring>& targets, int px) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = targets;
        px_ = px;
        hasWork_ = true;
    }
    cv_.notify_all();
}

void IconWorker::Run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto post = [this](IconResult* r) {
        if (!PostMessageW(notify_, msg_, 0, (LPARAM)r)) {
            if (r->bitmap) DeleteObject(r->bitmap);
            if (r->icon) DestroyIcon(r->icon);
            delete r;
        }
    };
    std::wstring browser = BrowserExe();
    for (;;) {
        std::vector<std::wstring> targets;
        int px;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return hasWork_ || stop_; });
            if (stop_) break;
            targets.swap(pending_);
            px = px_;
            hasWork_ = false;
        }
        auto interrupted = [this] {
            std::lock_guard<std::mutex> lock(mutex_);
            return hasWork_ || stop_;
        };

        for (auto& t : targets) {
            auto* r = new IconResult{t, px};
            r->icon = GenericIcon(t, px);
            post(r);
        }
        // Local paths first: a switched-off NAS can take many seconds per check.
        std::stable_sort(targets.begin(), targets.end(),
                         [](const std::wstring& a, const std::wstring& b) { return !IsUnc(a) && IsUnc(b); });
        for (auto& t : targets) {
            if (interrupted()) break;
            auto* r = new IconResult{t, px};
            if (ql::IsUrl(t)) {
                r->state = State::Exists;
                if (!browser.empty()) r->bitmap = ShellImage(browser, px);
            } else {
                std::wstring path = Expand(t);
                DWORD attr = GetFileAttributesW(path.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    r->state = State::Missing;
                } else {
                    r->state = State::Exists;
                    r->directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    r->bitmap = ShellImage(path, px);
                }
            }
            post(r);
        }
    }
    CoUninitialize();
}

}  // namespace shell
