#include <doctest/doctest.h>

#include <pdfview/Actions.h>
#include <pdfview/History.h>
#include <pdfview/Viewer.h>

using namespace pdfview;

TEST_CASE("key sequences: parse and print")
{
    KeySeq s;
    REQUIRE(parse("Ctrl+Shift+N", s));
    CHECK(s == KeySeq{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N});
    CHECK(toString(s) == "Ctrl+Shift+N");
    REQUIRE(parse("g, g", s));
    CHECK(s == KeySeq{ImGuiKey_G, ImGuiKey_G});
    CHECK(toString(s) == "G, G");
    REQUIRE(parse("Ctrl+=", s));
    CHECK(s == KeySeq{ImGuiMod_Ctrl | ImGuiKey_Equal});
    REQUIRE(parse("Ctrl+-", s));
    CHECK(s == KeySeq{ImGuiMod_Ctrl | ImGuiKey_Minus});
    REQUIRE(parse("Ctrl+,", s));
    CHECK(s == KeySeq{ImGuiMod_Ctrl | ImGuiKey_Comma});
    CHECK(toString(s) == "Ctrl+Comma");
    REQUIRE(parse("F12", s));
    CHECK(s == KeySeq{ImGuiKey_F12});
    REQUIRE(parse("Num5", s));
    CHECK(s == KeySeq{ImGuiKey_Keypad5});
    REQUIRE(parse("Shift+Space", s));
    CHECK(toString(s) == "Shift+Space");
    REQUIRE(parse("Alt+PageDown", s));
    CHECK(toString(s) == "Alt+PgDown");
    REQUIRE(parse("", s));
    CHECK(s.empty());
    CHECK_FALSE(parse("Ctrl+Nope", s));
    CHECK_FALSE(parse("Ctrl+", s));
    CHECK_FALSE(parse("A+B", s));
    CHECK_FALSE(parse("A, B, C, D, E", s));
    // every printed key parses back
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        if (!bindable(static_cast<ImGuiKey>(k))) continue;
        KeySeq one{ImGuiMod_Ctrl | k}, back;
        REQUIRE_MESSAGE(parse(toString(one), back), toString(one));
        CHECK(back == one);
    }
}

TEST_CASE("Actions: matching sequences")
{
    Actions a;
    int g = 0, gg = 0, sg = 0, n = 0, find = 0;
    a.add("g", "G", "", "G", "", [&] { g++; });
    a.add("gg", "GG", "", "G, G", "", [&] { gg++; });
    a.add("sg", "Shift+G", "", "Shift+G", "", [&] { sg++; });
    a.add("n", "N", "", "N", "", [&] { n++; });
    a.add("find", "Find", "", "Ctrl+F", "/", [&] { find++; });
    auto run = [](Action* x) {
        if (x) x->run();
    };

    run(a.feed(ImGuiKey_N, 0));
    CHECK(n == 1);
    run(a.feed(ImGuiKey_Slash, 0.1));
    CHECK(find == 1); // the alternate
    // G waits: it starts G, G too
    run(a.feed(ImGuiKey_G, 1));
    CHECK(g == 0);
    CHECK(a.pending().n == 1);
    run(a.feed(ImGuiKey_G, 1.2));
    CHECK(gg == 1);
    CHECK(g == 0);
    // G then nothing: G once its time is up
    run(a.feed(ImGuiKey_G, 5));
    run(a.tick(5.5));
    CHECK(g == 0);
    run(a.tick(6.1));
    CHECK(g == 1);
    // G then N: G's sequence breaks, N runs on its own
    run(a.feed(ImGuiKey_G, 10));
    run(a.feed(ImGuiKey_N, 10.1));
    CHECK(n == 2);
    CHECK(a.pending().n == 0);
    // G, too late, G: two starts
    run(a.feed(ImGuiKey_G, 20));
    run(a.feed(ImGuiKey_G, 22));
    CHECK(gg == 1);
    CHECK(a.pending().n == 1);
    a.clearPending();
    run(a.feed(ImGuiMod_Shift | ImGuiKey_G, 30));
    CHECK(sg == 1);
    // disabled actions don't run through trigger
    a.find("n")->enabled = [] { return false; };
    CHECK_FALSE(a.trigger("n"));
    CHECK(a.trigger("gg"));
    CHECK(a.keysOf("find") == "Ctrl+F");
}

TEST_CASE("Actions: conflicts, save and load")
{
    Actions a;
    a.add("find", "Find", "", "Ctrl+F", "/");
    a.add("next", "Next", "", "N", "F3");
    a.add("first", "First", "", "Home", "G, G");
    KeySeq f3;
    parse("F3", f3);
    REQUIRE(a.conflicts(f3).size() == 1);
    CHECK(a.conflicts(f3)[0]->id == "next");
    CHECK(a.conflicts(f3, a.find("next")).empty());

    CHECK(a.save().empty()); // all defaults
    parse("Ctrl+K, Ctrl+F", a.find("find")->keys[0]);
    a.find("next")->keys[1] = {};
    std::string saved = a.save();
    CHECK(saved == "find: Ctrl+K, Ctrl+F; /\nnext: N; \n");

    Actions b;
    b.add("find", "Find", "", "Ctrl+F", "/");
    b.add("next", "Next", "", "N", "F3");
    b.add("first", "First", "", "Home", "G, G");
    b.load(saved + "unknown: Ctrl+X; \nfirst: Bogus+Key; \n");
    CHECK(toString(b.find("find")->keys[0]) == "Ctrl+K, Ctrl+F");
    CHECK(b.find("next")->keys[1].empty());
    CHECK(toString(b.find("first")->keys[1]) == "G, G"); // bad line skipped
    b.resetAll();
    CHECK(b.save().empty());
}

TEST_CASE("Viewer: default shortcuts")
{
    Actions a;
    Viewer::registerActions(a, [] { return static_cast<Viewer*>(nullptr); });
    // the viewer's part of the agreed table (2026-11-24); the app adds its own
    struct Row {
        const char* id;
        const char* primary;
        const char* alternate;
    };
    const Row table[] = {
        {"view.scroll_up", "K", "Up"},
        {"view.scroll_down", "J", "Down"},
        {"view.scroll_left", "", "Left"},
        {"view.scroll_right", "", "Right"},
        {"view.page_down", "Space", ""},
        {"view.page_up", "Shift+Space", ""},
        {"go.next_page", "L", "PgDown"},
        {"go.prev_page", "H", "PgUp"},
        {"go.first", "G, G", "Home"},
        {"go.last", "Shift+G", "End"},
        {"go.back", "Ctrl+O", "Alt+Left"},
        {"go.forward", "Ctrl+I", "Alt+Right"},
        {"go.page", "Ctrl+G", ""},
        {"zoom.in", "Ctrl+=", "Ctrl+NumPlus"},
        {"zoom.out", "Ctrl+-", "Ctrl+Num-"},
        {"zoom.fit_width", "S", ""},
        {"zoom.fit_page", "A", ""},
        {"zoom.actual", "=", ""},
        {"view.single", "I", ""},
        {"view.facing", "", ""},
        {"view.overview", "O", ""},
        {"view.continuous", "C", ""},
        {"tool.browse", "Ctrl+1", ""},
        {"tool.text", "Ctrl+2", ""},
        {"edit.copy", "Ctrl+C", ""},
        {"edit.select_all", "Ctrl+A", ""},
        {"edit.deselect", "Esc", ""},
        {"find.find", "/", "Ctrl+F"},
        {"find.next", "N", "Enter"},
        {"find.prev", "Shift+N", "Shift+Enter"},
        {"find.whole_words", "", ""},
    };
    for (const Row& r : table) {
        Action* act = a.find(r.id);
        REQUIRE_MESSAGE(act, r.id);
        CHECK_MESSAGE(toString(act->defaults[0]) == r.primary, r.id);
        CHECK_MESSAGE(toString(act->defaults[1]) == r.alternate, r.id);
    }
    // no two actions share a default sequence
    for (const auto& act : a.all())
        for (const KeySeq& k : act->defaults)
            if (!k.empty()) CHECK_MESSAGE(a.conflicts(k, act.get()).empty(), act->id << " " << toString(k));
}

TEST_CASE("History: back and forward")
{
    History h;
    History::Entry to;
    CHECK_FALSE(h.back({0, 0}, to));
    h.push({0, 0}); // jumped from page 1 to 10
    h.push({10, 0}); // then from 10 to 30
    REQUIRE(h.back({30, 5}, to));
    CHECK(to.page == 10);
    REQUIRE(h.back({10, 0}, to));
    CHECK(to.page == 0);
    CHECK_FALSE(h.back({0, 0}, to));
    REQUIRE(h.forward({0, 0}, to));
    CHECK(to.page == 10);
    REQUIRE(h.forward({10, 0}, to));
    CHECK(to.page == 30);
    CHECK(to.y == 5);
    // a new jump drops the forward ones
    h.back({30, 5}, to);
    h.push({10, 0});
    CHECK(h.forwards.empty());
    // the same place twice is kept once
    h.push({10, 0});
    CHECK(h.backs.size() == 2);
}
