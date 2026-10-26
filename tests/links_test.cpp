// Unit tests for links.cpp (model, input parsing, ini round trip). Run: tests\run_tests.bat
#include "../links.h"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace ql;

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (cond) {                                                                \
            ++g_pass;                                                              \
        } else {                                                                   \
            ++g_fail;                                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                          \
    } while (0)

static void TestParseInput() {
    auto p = ParseInput(L"  D:\\업무\\견적\\2026\\  ");
    CHECK(p.kind == InputKind::Path && p.target == L"D:\\업무\\견적\\2026");
    p = ParseInput(L"\"C:\\Users\\Administrator\\Desktop\\scan\"");
    CHECK(p.kind == InputKind::Path && p.target == L"C:\\Users\\Administrator\\Desktop\\scan");
    p = ParseInput(L"d:");
    CHECK(p.kind == InputKind::Path && p.target == L"D:\\");
    p = ParseInput(L"D:\\");
    CHECK(p.kind == InputKind::Path && p.target == L"D:\\");
    p = ParseInput(L"\\\\nas\\share\\자료");
    CHECK(p.kind == InputKind::Path && p.target == L"\\\\nas\\share\\자료");
    p = ParseInput(L"%USERPROFILE%\\Desktop");
    CHECK(p.kind == InputKind::Path && p.target == L"%USERPROFILE%\\Desktop");
    p = ParseInput(L"C:/Windows/System32");
    CHECK(p.kind == InputKind::Path && p.target == L"C:\\Windows\\System32");
    p = ParseInput(L"https://example.atlassian.net/jira/boards/1?a=b|c");
    CHECK(p.kind == InputKind::Url && p.target == L"https://example.atlassian.net/jira/boards/1?a=b|c");
    p = ParseInput(L"HTTP://Example.com");
    CHECK(p.kind == InputKind::Url);
    p = ParseInput(L"www.naver.com");
    CHECK(p.kind == InputKind::Url && p.target == L"https://www.naver.com");
    p = ParseInput(L"file:///C:/Program%20Files");
    CHECK(p.kind == InputKind::Path && p.target == L"C:\\Program Files");
    p = ParseInput(L"\r\n\r\n  C:\\Temp\r\nsecond line");
    CHECK(p.kind == InputKind::Path && p.target == L"C:\\Temp");
    CHECK(ParseInput(L"javascript:alert(1)").kind == InputKind::Invalid);
    CHECK(ParseInput(L"ms-settings:display").kind == InputKind::Invalid);
    CHECK(ParseInput(L"relative\\path").kind == InputKind::Invalid);
    CHECK(ParseInput(L"그냥 글자").kind == InputKind::Invalid);
    CHECK(ParseInput(L"").kind == InputKind::Invalid);
    CHECK(ParseInput(L"https://").kind == InputKind::Invalid);
    CHECK(ParseInput(L"C:\\a|b").kind == InputKind::Invalid);
    CHECK(ParseInput(L"https://a.com/x y").kind == InputKind::Invalid);
}

static void TestNames() {
    CHECK(DefaultName(L"D:\\업무\\견적\\2026") == L"2026");
    CHECK(DefaultName(L"D:\\업무\\보고\\월간보고서.xlsx") == L"월간보고서.xlsx");
    CHECK(DefaultName(L"D:\\") == L"D:");
    CHECK(DefaultName(L"\\\\nas\\share") == L"share");
    CHECK(DefaultName(L"https://www.example.com/a/b") == L"example.com");
    CHECK(DefaultName(L"https://user@host.co.kr:8080?x") == L"host.co.kr:8080");
    CHECK(CleanName(L" a|b\r\nc ") == L"a\xFF5C" L"b  c");
    CHECK(SameTarget(L"c:\\Temp\\", L"C:\\TEMP"));
    CHECK(!SameTarget(L"https://a.com/X", L"https://a.com/x"));
}

static Store Sample() {
    Store s;
    s.groups[0].name = L"업무";
    s.AddGroup(L"프로젝트");
    s.AddGroup(L"공유");
    s.Add(0, -1, L"D:\\업무\\견적\\2026", L"2026 견적서");
    s.Add(0, -1, L"C:\\scan", L"");
    s.Add(1, -1, L"D:\\src\\NPE", L"NPE 소스");
    s.Add(1, -1, L"https://jira.example.com/boards/1?q=a|b", L"Jira 보드");
    s.Add(2, -1, L"\\\\nas\\share\\자료", L"공유 서버 자료");
    return s;
}

static void TestStore() {
    Store s = Sample();
    CHECK(s.links.size() == 5);
    CHECK(s.links[1].name == L"scan");
    CHECK(s.Add(2, -1, L"c:\\SCAN\\", L"dup") == -1);  // duplicate path
    CHECK(s.CountIn(0) == 2 && s.CountIn(1) == 2 && s.CountIn(2) == 1);

    // Adding to group 0 lands after its last item even though later groups follow in the vector.
    int i = s.Add(0, -1, L"D:\\third", L"");
    CHECK(i == 2 && s.links[2].group == 0);

    // Move the URL (index 4) to the top of group 0.
    int j = s.Move(4, 0, 0);
    CHECK(j == 0 && s.links[0].group == 0 && s.links[0].name == L"Jira 보드");
    // Move it to the end of group 2.
    j = s.Move(0, 2, -1);
    CHECK(s.links[j].group == 2 && j == s.LastIndexIn(2));

    // Remove + restore keeps position.
    Link l = s.Remove(1);
    CHECK(s.links.size() == 5);
    s.Restore(1, l);
    CHECK(s.links[1].target == l.target);

    // Removing group 1 moves its items to group 0 and renumbers group 2 → 1.
    int inG2 = s.CountIn(2);
    CHECK(s.RemoveGroup(1));
    CHECK(s.groups.size() == 2 && s.groups[1].name == L"공유");
    CHECK(s.CountIn(1) == inG2);
    CHECK(s.CountIn(0) == 4);
    CHECK(!s.RemoveGroup(0));

    s.SwapGroups(0, 1);
    CHECK(s.groups[0].name == L"공유" && s.CountIn(0) == inG2);
}

static void TestMoveMany() {
    // links: 0 견적(g0) 1 scan(g0) 2 NPE(g1) 3 Jira(g1) 4 nas(g2)
    Store s = Sample();
    auto out = s.MoveMany({4, 0}, 1, 3);  // before Jira, order kept as in the list
    CHECK(out.size() == 2);
    CHECK(s.links[out[0]].name == L"2026 견적서" && s.links[out[1]].name == L"공유 서버 자료");
    CHECK(s.links[out[0]].group == 1 && s.links[out[1]].group == 1);
    CHECK(s.links[out[1] + 1].name == L"Jira 보드");
    CHECK(s.CountIn(0) == 1 && s.CountIn(1) == 4 && s.CountIn(2) == 0);

    // `before` is itself moved: lands before the next link that stays.
    Store t = Sample();
    out = t.MoveMany({0, 1}, 0, 1);
    CHECK(t.links[0].name == L"2026 견적서" && t.links[1].name == L"scan" && t.links[2].name == L"NPE 소스");

    // To the end of an empty group.
    Store u = Sample();
    u.AddGroup(L"빈 그룹");
    out = u.MoveMany({2, 3}, 3, -1);
    CHECK(u.CountIn(3) == 2 && u.CountIn(1) == 0 && u.links[out[0]].name == L"NPE 소스");
    CHECK(u.links.size() == 5);
}

static void TestRows() {
    Store s = Sample();
    auto rows = s.Rows(L"");
    CHECK(rows.size() == 3 + 5);
    CHECK(rows[0].type == Row::GroupHeader && rows[1].type == Row::Item);

    s.groups[1].collapsed = true;
    rows = s.Rows(L"");
    CHECK(rows.size() == 3 + 3);

    // A filter ignores collapsing and hides groups without matches.
    rows = s.Rows(L"JIRA");
    CHECK(rows.size() == 2 && rows[0].group == 1 && rows[1].type == Row::Item);
    rows = s.Rows(L"nas\\share");  // matches the target, not the name
    CHECK(rows.size() == 2 && rows[0].group == 2);
    rows = s.Rows(L"없는 글자");
    CHECK(rows.empty());

    // One group: no header rows at all.
    Store one;
    one.Add(0, -1, L"C:\\a", L"");
    one.groups[0].collapsed = true;
    rows = one.Rows(L"");
    CHECK(rows.size() == 1 && rows[0].type == Row::Item);
}

static std::wstring TempIni(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"ql_test_" + name + L".ini";
}

static void TestIni() {
    Store s = Sample();
    s.groups[2].collapsed = true;
    s.settings.x = -900;
    s.settings.y = 120;
    s.settings.doubleClick = true;
    s.settings.topmost = false;
    s.settings.theme = L"light";
    s.settings.clock = L"card";
    s.settings.clockPosition = L"bottom";
    s.settings.clockSeconds = true;
    std::wstring path = TempIni(L"roundtrip");
    CHECK(s.Save(path));

    Store r;
    CHECK(r.Load(path));
    CHECK(r.groups.size() == 3 && r.groups[0].name == L"업무" && r.groups[2].collapsed && !r.groups[1].collapsed);
    CHECK(r.links.size() == 5);
    for (size_t i = 0; i < 5 && i < r.links.size(); ++i) {
        CHECK(r.links[i].target == s.links[i].target);
        CHECK(r.links[i].name == s.links[i].name);
        CHECK(r.links[i].group == s.links[i].group);
    }
    CHECK(r.settings.x == -900 && r.settings.y == 120 && r.settings.doubleClick && !r.settings.topmost);
    CHECK(r.settings.theme == L"light");
    CHECK(r.settings.clock == L"card" && r.settings.clockSeconds && !r.settings.clock24);
    CHECK(r.settings.clockPosition == L"bottom");
    DeleteFileW(path.c_str());

    // Missing file: defaults stay.
    Store m;
    CHECK(!m.Load(TempIni(L"missing")));
    CHECK(m.groups.size() == 1 && m.links.empty() && m.settings.x == Settings::kUnset);
    CHECK(m.settings.theme == L"system");
    CHECK(m.settings.clock == L"off" && m.settings.clockPosition == L"top");

    // The first clock version wrote Clock=bar: now the one-line design at the bottom.
    std::wstring old = TempIni(L"oldclock");
    const wchar_t* oldText = L"\xFEFF[Settings]\r\nClock=bar\r\n";
    HANDLE of = CreateFileW(old.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD ow;
    WriteFile(of, oldText, (DWORD)(wcslen(oldText) * 2), &ow, nullptr);
    CloseHandle(of);
    Store oc;
    CHECK(oc.Load(old));
    CHECK(oc.settings.clock == L"line" && oc.settings.clockPosition == L"bottom");
    DeleteFileW(old.c_str());

    // Damaged file: bad lines are skipped, bad group numbers go to group 0, duplicates dropped.
    std::wstring bad = TempIni(L"bad");
    const wchar_t* text =
        L"\xFEFF[Groups]\r\nCount=2\r\n0=0|업무\r\n1=x\r\n"
        L"[Links]\r\nCount=6\r\n0=0|ok|C:\\ok\r\n1=garbage\r\n2=9|far group|C:\\far\r\n"
        L"3=0||C:\\noname\\leaf\r\n4=0|dup|c:\\OK\r\n5=0|empty|\r\n";
    HANDLE f = CreateFileW(bad.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, text, (DWORD)(wcslen(text) * 2), &w, nullptr);
    CloseHandle(f);
    Store b;
    CHECK(b.Load(bad));
    CHECK(b.groups.size() == 2);
    CHECK(b.links.size() == 3);
    CHECK(b.links.size() == 3 && b.links[1].group == 0 && b.links[2].name == L"leaf");
    DeleteFileW(bad.c_str());
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    TestParseInput();
    TestNames();
    TestStore();
    TestMoveMany();
    TestRows();
    TestIni();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
