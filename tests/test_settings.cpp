// the app's settings file and merging saves from several windows
//   pdfview_settings_tests                    the tests
//   pdfview_settings_tests --writer FILE TAG N  N merged saves, each adding a bookmark

// before doctest's implementation, which brings windows.h and its near macro
#include "../app/Settings.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "fixtures.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string self;

void write(const std::string& p, const std::string& text)
{
    std::ofstream out(p, std::ios::binary);
    out << text;
}

// a window's life: load, change, save
struct Window {
    Settings local, base;
    std::string path;
    explicit Window(std::string p)
        : path(std::move(p))
    {
        local.loadLocked(path);
        base = local;
    }
    bool save()
    {
        if (!local.saveMerged(path, base)) return false;
        base = local;
        return true;
    }
};

int run(const std::string& cmd)
{
    int r = std::system(cmd.c_str());
#ifndef _WIN32
    if (r != -1) r = WEXITSTATUS(r);
#endif
    return r;
}

} // namespace

TEST_CASE("settings: a file round-trips, bookmark ids included")
{
    std::string dir = test::scratch("settings-roundtrip");
    Settings s;
    s.theme = "light";
    s.pageGaps = false;
    s.recent = {"a.pdf"};
    FileState& f = s.file("a.pdf");
    f.bookmarks.push_back({3, 120, "Three"});
    s.tabs = {"a.pdf"};
    write(dir + "/s.ini", s.text());
    Settings t;
    t.load(dir + "/s.ini");
    CHECK(t.theme == "light");
    CHECK(!t.pageGaps);
    REQUIRE(t.files.count("a.pdf"));
    REQUIRE(t.files["a.pdf"].bookmarks.size() == 1);
    CHECK(t.files["a.pdf"].bookmarks[0] == f.bookmarks[0]);
    CHECK(t.text() == s.text());
}

TEST_CASE("settings: bookmarks from before ids get the same ids in every window")
{
    std::string dir = test::scratch("settings-legacy");
    write(dir + "/s.ini", "[file]\npath=a.pdf\nbookmark=1|-1|One\nbookmark=1|-1|One\nbookmark=2|50|Two|with bar\n");
    Settings a, b;
    a.load(dir + "/s.ini");
    b.load(dir + "/s.ini");
    auto& ab = a.files["a.pdf"].bookmarks;
    REQUIRE(ab.size() == 3);
    CHECK(ab[2].title == "Two|with bar");
    CHECK(ab[0].id != ab[1].id); // twins stay apart
    CHECK(ab == b.files["a.pdf"].bookmarks);
}

TEST_CASE("settings: two windows keep each other's changes")
{
    std::string dir = test::scratch("settings-merge");
    std::string path = dir + "/s.ini";
    {
        Window w(path);
        w.local.theme = "dark";
        w.local.file("a.pdf").bookmarks.push_back({1, -1, "Keep"});
        w.local.file("a.pdf").bookmarks.push_back({2, -1, "Remove"});
        w.local.file("a.pdf").bookmarks.push_back({3, -1, "Edit"});
        w.local.shortcuts = "view.zoom_in: Ctrl+I; \nview.zoom_out: Ctrl+O; \n";
        w.local.recent = {"a.pdf"};
        REQUIRE(w.save());
    }
    Window a(path), b(path);
    // a: preferences, a bookmark removed and another edited, a shortcut
    a.local.sidebarWidth = 420;
    a.local.pageGaps = false;
    auto& abm = a.local.files["a.pdf"].bookmarks;
    abm.erase(abm.begin() + 1);
    abm[1].title = "Edited by a";
    a.local.shortcuts = "view.zoom_in: Ctrl+J; \nview.zoom_out: Ctrl+O; \n";
    a.local.tabs = {"a.pdf"};
    // b: other preferences, a new bookmark, another shortcut, a file opened
    b.local.theme = "light";
    b.local.file("a.pdf").bookmarks.push_back({9, -1, "Added by b"});
    b.local.shortcuts = "view.zoom_in: Ctrl+I; \n";
    b.local.file("b.pdf");
    b.local.addRecent("b.pdf");
    b.local.tabs = {"b.pdf"};
    REQUIRE(a.save());
    REQUIRE(b.save());

    Settings s;
    s.load(path);
    CHECK(s.sidebarWidth == 420);
    CHECK(!s.pageGaps);
    CHECK(s.theme == "light");
    std::vector<std::string> titles;
    for (const auto& x : s.files["a.pdf"].bookmarks) titles.push_back(x.title);
    CHECK(titles == std::vector<std::string>{"Keep", "Edited by a", "Added by b"});
    CHECK(s.shortcuts == "view.zoom_in: Ctrl+J; \n"); // a's edit, b's reset
    CHECK(s.recent == std::vector<std::string>{"b.pdf", "a.pdf"});
    CHECK(s.files.count("b.pdf"));
    // the last window saved owns the session
    CHECK(s.tabs == std::vector<std::string>{"b.pdf"});

    // saving again doesn't undo the other's changes
    a.local.overviewColumns = 6;
    REQUIRE(a.save());
    s.load(path);
    CHECK(s.theme == "light");
    CHECK(s.overviewColumns == 6);
    CHECK(s.tabs == std::vector<std::string>{"a.pdf"});
    CHECK(s.files["a.pdf"].bookmarks.size() == 3);
}

TEST_CASE("settings: the later save wins on the same field or bookmark")
{
    std::string dir = test::scratch("settings-conflict");
    std::string path = dir + "/s.ini";
    {
        Window w(path);
        w.local.file("a.pdf").bookmarks.push_back({1, -1, "Start"});
        REQUIRE(w.save());
    }
    Window a(path), b(path);
    a.local.theme = "dark";
    a.local.files["a.pdf"].bookmarks[0].title = "a";
    a.local.files["a.pdf"].pos = {4, 10};
    b.local.theme = "light";
    b.local.files["a.pdf"].bookmarks[0].title = "b";
    b.local.files["a.pdf"].pos = {7, 20};
    REQUIRE(a.save());
    REQUIRE(b.save());
    Settings s;
    s.load(path);
    CHECK(s.theme == "light");
    REQUIRE(s.files["a.pdf"].bookmarks.size() == 1);
    CHECK(s.files["a.pdf"].bookmarks[0].title == "b");
    CHECK(s.files["a.pdf"].pos.page == 7);
}

TEST_CASE("settings: clearing the recent list keeps files opened elsewhere since")
{
    std::string dir = test::scratch("settings-recent");
    std::string path = dir + "/s.ini";
    {
        Window w(path);
        for (const char* p : {"1.pdf", "2.pdf"}) {
            w.local.file(p);
            w.local.addRecent(p);
        }
        REQUIRE(w.save());
    }
    Window a(path), b(path);
    a.local.recent.clear();
    b.local.file("3.pdf");
    b.local.addRecent("3.pdf");
    REQUIRE(b.save());
    REQUIRE(a.save());
    Settings s;
    s.load(path);
    CHECK(s.recent == std::vector<std::string>{"3.pdf"});
}

TEST_CASE("settings: concurrent processes lose no saves")
{
    std::string dir = test::scratch("settings-processes");
    std::string path = dir + "/s.ini";
    constexpr int writers = 4, saves = 15;
    std::vector<std::thread> threads;
    std::vector<int> codes(writers, -1);
    for (int i = 0; i < writers; i++)
        threads.emplace_back([&, i] {
            std::string cmd = "\"" + self + "\" --writer \"" + path + "\" w" + std::to_string(i) + " " + std::to_string(saves);
#ifdef _WIN32
            cmd = "\"" + cmd + "\"";
#endif
            codes[static_cast<size_t>(i)] = run(cmd);
        });
    for (auto& t : threads) t.join();
    for (int c : codes) CHECK(c == 0);
    Settings s;
    s.load(path);
    CHECK(s.files["doc.pdf"].bookmarks.size() == writers * saves);
    for (const auto& e : fs::directory_iterator(dir)) CHECK(e.path().filename().string().find(".tmp") == std::string::npos);
}

int main(int argc, char** argv)
{
    self = argv[0];
    if (argc == 5 && std::string(argv[1]) == "--writer") {
        // one window, saving after each new bookmark
        Window w(argv[2]);
        int n = std::atoi(argv[4]);
        for (int i = 0; i < n; i++) {
            w.local.file("doc.pdf").bookmarks.push_back({i, -1, std::string(argv[3]) + "-" + std::to_string(i)});
            if (!w.save()) return 1;
        }
        return 0;
    }
    doctest::Context ctx(argc, argv);
    return ctx.run();
}
