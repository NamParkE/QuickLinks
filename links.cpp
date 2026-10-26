#include "links.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#endif

namespace ql {

namespace {

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (std::iswspace(s[b]) || s[b] == 0x3000 || s[b] == 0xFEFF)) ++b;
    while (e > b && (std::iswspace(s[e - 1]) || s[e - 1] == 0x3000)) --e;
    return s.substr(b, e - b);
}

std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) { return s.rfind(prefix, 0) == 0; }

std::wstring StripTrailingSlash(std::wstring s) {
    // Keep drive roots ("D:\") intact.
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}

bool ContainsCi(const std::wstring& hay, const std::wstring& lowerNeedle) {
    return Lower(hay).find(lowerNeedle) != std::wstring::npos;
}

std::wstring ReadIni(const wchar_t* section, const wchar_t* key, const wchar_t* def, const std::wstring& path) {
    std::wstring buf(32768, L'\0');
    DWORD n = GetPrivateProfileStringW(section, key, def, &buf[0], (DWORD)buf.size(), path.c_str());
    buf.resize(n);
    return buf;
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def, const std::wstring& path) {
    std::wstring s = ReadIni(section, key, L"", path);
    if (s.empty()) return def;
    return _wtoi(s.c_str());
}

}  // namespace

Parsed ParseInput(const std::wstring& text) {
    // First non-empty line only.
    std::wstring s;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring::npos) end = text.size();
        s = Trim(text.substr(start, end - start));
        if (!s.empty()) break;
        start = end + 1;
    }
    // Explorer's "Copy as path" wraps the path in quotes.
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = Trim(s.substr(1, s.size() - 2));
    if (s.empty()) return {};

    std::wstring low = Lower(s);
    if ((StartsWith(low, L"http://") && s.size() > 7) || (StartsWith(low, L"https://") && s.size() > 8)) {
        if (s.find_first_of(L" \t") != std::wstring::npos) return {};
        return {InputKind::Url, s};
    }
    if (StartsWith(low, L"www.") && s.size() > 4) {
        if (s.find_first_of(L" \t") != std::wstring::npos) return {};
        return {InputKind::Url, L"https://" + s};
    }
    if (StartsWith(low, L"file:")) {
        wchar_t buf[MAX_PATH * 4];
        DWORD n = (DWORD)(sizeof buf / sizeof buf[0]);
        if (FAILED(PathCreateFromUrlW(s.c_str(), buf, &n, 0))) return {};
        s = buf;
    }

    std::replace(s.begin(), s.end(), L'/', L'\\');
    if (s.find_first_of(L"<>|\"*?") != std::wstring::npos) return {};
    bool drive = s.size() >= 2 && std::iswalpha(s[0]) && s[0] < 128 && s[1] == L':' && (s.size() == 2 || s[2] == L'\\');
    bool unc = s.size() > 2 && s[0] == L'\\' && s[1] == L'\\' && s[2] != L'\\';
    bool env = s[0] == L'%' && s.find(L'%', 1) != std::wstring::npos;
    if (!drive && !unc && !env) return {};
    if (drive && s.size() == 2) s += L'\\';
    if (drive) s[0] = (wchar_t)std::towupper(s[0]);
    return {InputKind::Path, StripTrailingSlash(s)};
}

bool IsUrl(const std::wstring& target) {
    std::wstring low = Lower(target.substr(0, 8));
    return StartsWith(low, L"http://") || StartsWith(low, L"https://");
}

std::wstring DefaultName(const std::wstring& target) {
    if (IsUrl(target)) {
        size_t b = target.find(L"://") + 3;
        size_t e = target.find_first_of(L"/?#", b);
        std::wstring host = target.substr(b, e == std::wstring::npos ? std::wstring::npos : e - b);
        size_t at = host.rfind(L'@');
        if (at != std::wstring::npos) host = host.substr(at + 1);
        if (StartsWith(Lower(host), L"www.")) host = host.substr(4);
        return host.empty() ? target : host;
    }
    std::wstring t = StripTrailingSlash(target);
    if (t.size() == 3 && t[1] == L':') return t.substr(0, 2);
    size_t pos = t.rfind(L'\\');
    std::wstring name = pos == std::wstring::npos ? t : t.substr(pos + 1);
    return name.empty() ? t : name;
}

std::wstring CleanName(const std::wstring& name) {
    std::wstring s = name;
    for (auto& c : s) {
        if (c == L'|') c = 0xFF5C;  // fullwidth vertical line
        else if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    }
    return Trim(s);
}

bool SameTarget(const std::wstring& a, const std::wstring& b) {
    if (IsUrl(a) || IsUrl(b)) return a == b;
    std::wstring x = StripTrailingSlash(a), y = StripTrailingSlash(b);
    return CompareStringOrdinal(x.c_str(), (int)x.size(), y.c_str(), (int)y.size(), TRUE) == CSTR_EQUAL;
}

Store::Store() { groups.push_back({L"기본", false}); }

int Store::Find(const std::wstring& target) const {
    for (size_t i = 0; i < links.size(); ++i)
        if (SameTarget(links[i].target, target)) return (int)i;
    return -1;
}

int Store::CountIn(int g) const {
    return (int)std::count_if(links.begin(), links.end(), [g](const Link& l) { return l.group == g; });
}

int Store::LastIndexIn(int g) const {
    for (int i = (int)links.size() - 1; i >= 0; --i)
        if (links[i].group == g) return i;
    return -1;
}

int Store::Add(int group, int before, const std::wstring& target, const std::wstring& name) {
    if (target.empty() || Find(target) >= 0) return -1;
    if (group < 0 || group >= (int)groups.size()) group = 0;
    Link l{group, CleanName(name.empty() ? DefaultName(target) : name), target};
    if (l.name.empty()) l.name = DefaultName(target);
    int pos;
    if (before >= 0 && before <= (int)links.size()) {
        pos = before;
    } else {
        int last = LastIndexIn(group);
        pos = last < 0 ? (int)links.size() : last + 1;
    }
    links.insert(links.begin() + pos, l);
    return pos;
}

Link Store::Remove(int index) {
    Link l = links[index];
    links.erase(links.begin() + index);
    return l;
}

void Store::Restore(int index, const Link& link) {
    Link l = link;
    if (l.group >= (int)groups.size()) l.group = 0;
    index = std::clamp(index, 0, (int)links.size());
    links.insert(links.begin() + index, l);
}

int Store::Move(int from, int group, int before) {
    Link l = links[from];
    l.group = group;
    links.erase(links.begin() + from);
    if (before > from) --before;
    int pos;
    if (before >= 0 && before <= (int)links.size()) {
        pos = before;
    } else {
        int last = LastIndexIn(group);
        pos = last < 0 ? (int)links.size() : last + 1;
    }
    links.insert(links.begin() + pos, l);
    return pos;
}

std::vector<int> Store::MoveMany(std::vector<int> indices, int group, int before) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    auto moved = [&indices](int i) { return std::binary_search(indices.begin(), indices.end(), i); };
    // Anchor on the first link at/after `before` that stays put; remember it by target.
    std::wstring anchor;
    if (before >= 0) {
        while (before < (int)links.size() && moved(before)) ++before;
        if (before < (int)links.size()) anchor = links[before].target;
    }
    std::vector<Link> taken;
    for (int i : indices) {
        taken.push_back(links[i]);
        taken.back().group = group;
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) links.erase(links.begin() + *it);
    int pos;
    if (!anchor.empty()) {
        pos = Find(anchor);
    } else {
        int last = LastIndexIn(group);
        pos = last < 0 ? (int)links.size() : last + 1;
    }
    links.insert(links.begin() + pos, taken.begin(), taken.end());
    std::vector<int> out;
    for (size_t k = 0; k < taken.size(); ++k) out.push_back(pos + (int)k);
    return out;
}

int Store::AddGroup(const std::wstring& name) {
    groups.push_back({CleanName(name), false});
    return (int)groups.size() - 1;
}

bool Store::RemoveGroup(int g) {
    if (g <= 0 || g >= (int)groups.size()) return false;
    std::vector<Link> moved, kept;
    for (auto& l : links) {
        if (l.group == g) {
            moved.push_back(l);
            moved.back().group = 0;
        } else {
            kept.push_back(l);
            if (kept.back().group > g) --kept.back().group;
        }
    }
    kept.insert(kept.end(), moved.begin(), moved.end());
    links = std::move(kept);
    groups.erase(groups.begin() + g);
    return true;
}

void Store::SwapGroups(int a, int b) {
    if (a < 0 || b < 0 || a >= (int)groups.size() || b >= (int)groups.size() || a == b) return;
    std::swap(groups[a], groups[b]);
    for (auto& l : links) {
        if (l.group == a) l.group = b;
        else if (l.group == b) l.group = a;
    }
}

std::vector<Row> Store::Rows(const std::wstring& filter) const {
    std::vector<Row> rows;
    std::wstring needle = Lower(Trim(filter));
    bool headers = groups.size() > 1;
    for (int g = 0; g < (int)groups.size(); ++g) {
        std::vector<int> items;
        for (int i = 0; i < (int)links.size(); ++i) {
            if (links[i].group != g) continue;
            if (needle.empty() || ContainsCi(links[i].name, needle) || ContainsCi(links[i].target, needle))
                items.push_back(i);
        }
        if (!needle.empty() && items.empty()) continue;
        if (headers) rows.push_back({Row::GroupHeader, g, -1});
        if (needle.empty() && headers && groups[g].collapsed) continue;
        for (int i : items) rows.push_back({Row::Item, g, i});
    }
    return rows;
}

void Store::Normalize() {
    if (groups.empty()) groups.push_back({L"기본", false});
    for (auto& l : links)
        if (l.group < 0 || l.group >= (int)groups.size()) l.group = 0;
}

bool Store::Load(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    Settings d;
    settings.x = ReadInt(L"Window", L"X", d.x, path);
    settings.y = ReadInt(L"Window", L"Y", d.y, path);
    settings.width = std::clamp(ReadInt(L"Window", L"Width", d.width, path), 180, 2000);
    settings.height = std::clamp(ReadInt(L"Window", L"Height", d.height, path), 160, 3000);
    settings.topmost = ReadInt(L"Window", L"Topmost", 1, path) != 0;
    settings.hotkey = ReadIni(L"Settings", L"Hotkey", d.hotkey.c_str(), path);
    settings.doubleClick = Lower(ReadIni(L"Settings", L"OpenOn", L"single", path)) == L"double";
    settings.hideAfterOpen = ReadIni(L"Settings", L"HideAfterOpen", d.hideAfterOpen.c_str(), path);
    settings.opacity = std::clamp(ReadInt(L"Settings", L"Opacity", 100, path), 50, 100);
    settings.theme = Lower(ReadIni(L"Settings", L"Theme", L"system", path));
    if (settings.theme != L"light" && settings.theme != L"dark") settings.theme = L"system";
    settings.clock = Lower(ReadIni(L"Settings", L"Clock", L"off", path));
    settings.clockPosition = Lower(ReadIni(L"Settings", L"ClockPosition", L"", path));
    if (settings.clock == L"bar") {  // the first version tied the one-line design to the bottom
        settings.clock = L"line";
        if (settings.clockPosition.empty()) settings.clockPosition = L"bottom";
    }
    if (settings.clock != L"large" && settings.clock != L"line" && settings.clock != L"card") settings.clock = L"off";
    if (settings.clockPosition != L"bottom") settings.clockPosition = L"top";
    settings.clock24 = ReadInt(L"Settings", L"Clock24", 0, path) != 0;
    settings.clockSeconds = ReadInt(L"Settings", L"ClockSeconds", 0, path) != 0;

    groups.clear();
    int ng = ReadInt(L"Groups", L"Count", 0, path);
    for (int i = 0; i < ng; ++i) {
        std::wstring v = ReadIni(L"Groups", std::to_wstring(i).c_str(), L"", path);
        size_t bar = v.find(L'|');
        if (bar == std::wstring::npos) {
            if (!v.empty()) groups.push_back({CleanName(v), false});
            continue;
        }
        Group g{CleanName(v.substr(bar + 1)), _wtoi(v.substr(0, bar).c_str()) != 0};
        if (g.name.empty()) g.name = L"그룹";
        groups.push_back(g);
    }
    if (groups.empty()) groups.push_back({L"기본", false});

    links.clear();
    int nl = ReadInt(L"Links", L"Count", 0, path);
    for (int i = 0; i < nl; ++i) {
        std::wstring v = ReadIni(L"Links", std::to_wstring(i).c_str(), L"", path);
        size_t a = v.find(L'|');
        if (a == std::wstring::npos) continue;
        size_t b = v.find(L'|', a + 1);
        if (b == std::wstring::npos) continue;
        Link l{_wtoi(v.substr(0, a).c_str()), CleanName(v.substr(a + 1, b - a - 1)), Trim(v.substr(b + 1))};
        if (l.target.empty() || Find(l.target) >= 0) continue;
        if (l.name.empty()) l.name = DefaultName(l.target);
        links.push_back(l);
    }
    Normalize();
    return true;
}

bool Store::Save(const std::wstring& path) const {
    std::wstring t;
    auto line = [&t](const std::wstring& s) { t += s + L"\r\n"; };
    line(L"[Window]");
    if (settings.x != Settings::kUnset) line(L"X=" + std::to_wstring(settings.x));
    if (settings.y != Settings::kUnset) line(L"Y=" + std::to_wstring(settings.y));
    line(L"Width=" + std::to_wstring(settings.width));
    line(L"Height=" + std::to_wstring(settings.height));
    line(std::wstring(L"Topmost=") + (settings.topmost ? L"1" : L"0"));
    line(L"");
    line(L"[Settings]");
    line(L"Hotkey=" + settings.hotkey);
    line(std::wstring(L"OpenOn=") + (settings.doubleClick ? L"double" : L"single"));
    line(L"HideAfterOpen=" + settings.hideAfterOpen);
    line(L"Opacity=" + std::to_wstring(settings.opacity));
    line(L"Theme=" + settings.theme);
    line(L"Clock=" + settings.clock);
    line(L"ClockPosition=" + settings.clockPosition);
    line(std::wstring(L"Clock24=") + (settings.clock24 ? L"1" : L"0"));
    line(std::wstring(L"ClockSeconds=") + (settings.clockSeconds ? L"1" : L"0"));
    line(L"");
    line(L"[Groups]");
    line(L"Count=" + std::to_wstring(groups.size()));
    for (size_t i = 0; i < groups.size(); ++i)
        line(std::to_wstring(i) + L"=" + (groups[i].collapsed ? L"1|" : L"0|") + CleanName(groups[i].name));
    line(L"");
    line(L"[Links]");
    line(L"Count=" + std::to_wstring(links.size()));
    for (size_t i = 0; i < links.size(); ++i)
        line(std::to_wstring(i) + L"=" + std::to_wstring(links[i].group) + L"|" + CleanName(links[i].name) + L"|" +
             links[i].target);

    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    // UTF-16 LE with BOM: the *PrivateProfile* functions read it as Unicode.
    wchar_t bom = 0xFEFF;
    DWORD written = 0;
    bool ok = WriteFile(f, &bom, sizeof bom, &written, nullptr) &&
              WriteFile(f, t.data(), (DWORD)(t.size() * sizeof(wchar_t)), &written, nullptr);
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace ql
