// QuickLinks list model: groups, links, filtering, ini persistence. No GUI code here.
#pragma once

#include <string>
#include <vector>

namespace ql {

struct Group {
    std::wstring name;
    bool collapsed = false;
};

struct Link {
    int group = 0;
    std::wstring name;
    std::wstring target;  // a path (may hold %VARS%) or an http(s) URL
};

struct Settings {
    static constexpr int kUnset = -32768;
    int x = kUnset, y = kUnset;  // physical pixels; kUnset = place top-right of the primary monitor
    int width = 260, height = 440;  // DIPs
    bool topmost = true;
    bool doubleClick = false;  // OpenOn=double
    std::wstring hotkey = L"Ctrl+Alt+Space";
    std::wstring hideAfterOpen = L"hotkey";
    std::wstring theme = L"system";  // system | light | dark
    std::wstring clock = L"off";         // design: off | large | line | card (docs/설계.md 시계 시안 B/C/D)
    std::wstring clockPosition = L"top";  // top (under the header) | bottom (under the list)
    bool clock24 = false;
    bool clockSeconds = false;
    int opacity = 100;
};

// What the add-input row makes of typed or pasted text (docs/설계.md 2.6).
enum class InputKind { Path, Url, Invalid };
struct Parsed {
    InputKind kind = InputKind::Invalid;
    std::wstring target;
};
Parsed ParseInput(const std::wstring& text);

bool IsUrl(const std::wstring& target);
// Last path segment, or the host of a URL without "www.".
std::wstring DefaultName(const std::wstring& target);
// Display names may not hold '|' (the ini field separator) or line breaks.
std::wstring CleanName(const std::wstring& name);
// Same target? Paths compare case-insensitively and ignore a trailing backslash.
bool SameTarget(const std::wstring& a, const std::wstring& b);

// One visible line of the widget list.
struct Row {
    enum Type { GroupHeader, Item } type;
    int group;
    int link;  // index into Store::links, -1 for headers
};

class Store {
public:
    std::vector<Group> groups;
    std::vector<Link> links;
    Settings settings;

    Store();  // one default group named "기본"

    int Find(const std::wstring& target) const;  // index or -1
    // Inserts before link index `before` (or at the end of `group` when before < 0).
    // Returns the new index, or -1 when the target is already in the list.
    int Add(int group, int before, const std::wstring& target, const std::wstring& name);
    Link Remove(int index);
    void Restore(int index, const Link& link);  // undo of Remove
    // Moves link `from` into `group`, before link `before` (-1 = end of the group). Returns its new index.
    int Move(int from, int group, int before);
    // Moves several links together, keeping their order, into `group` before link `before`
    // (-1 = end of the group; `before` may be one of the moved links). Returns their new indices.
    std::vector<int> MoveMany(std::vector<int> indices, int group, int before);

    int AddGroup(const std::wstring& name);
    // Group 0 stays; links of a removed group go to the end of group 0.
    bool RemoveGroup(int g);
    void SwapGroups(int a, int b);
    int CountIn(int g) const;
    int LastIndexIn(int g) const;  // -1 when empty

    // Headers are hidden while there is only one group. With a filter every group is shown expanded
    // and groups without a match are left out.
    std::vector<Row> Rows(const std::wstring& filter) const;

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

private:
    void Normalize();
};

}  // namespace ql
