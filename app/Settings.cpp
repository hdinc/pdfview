#include "Settings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

#include <Hd/Files.h>

namespace fs = std::filesystem;

namespace {

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// the file: "[section]" lines, then "key=value" lines. A "[file]" section
// starts with its "path=" line. Shortcuts are kept as their own lines.
std::string num(double v)
{
    std::ostringstream o;
    o.precision(10);
    o << v;
    return o.str();
}

double toNum(const std::string& s, double def = 0)
{
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}

std::string oneLine(const std::string& s)
{
    std::string o;
    for (char c : s)
        o += c == '\n' || c == '\r' ? ' ' : c;
    return o;
}

std::string hex(uint64_t v)
{
    char b[17];
    snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

// Bookmarks saved before they had ids get one from what they are, so every
// window reading the same file gives them the same one.
uint64_t legacyId(const Bookmark& b, int occurrence)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= 0xff;
        h *= 1099511628211ull;
    };
    mix(std::to_string(b.page));
    mix(num(b.y));
    mix(b.title);
    mix(std::to_string(occurrence));
    return h ? h : 1;
}

uint64_t nowMs()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string lockPath(const std::string& path) { return path + ".lock"; }

// shortcut lines by action id
std::map<std::string, std::string> shortcutLines(const std::string& text)
{
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string id = line.substr(0, colon);
        while (!id.empty() && id.back() == ' ') id.pop_back();
        out[id] = line;
    }
    return out;
}

// the local value when it changed since base, else the file's
template <class T>
void pick(T& result, const T& local, const T& base)
{
    if (!(local == base)) result = local;
}

}

uint64_t newBookmarkId()
{
    static std::mt19937_64 rng(std::random_device{}() ^ nowMs());
    uint64_t v;
    do v = rng();
    while (v == 0);
    return v;
}

void Settings::load(const std::string& path)
{
    std::ifstream in(u8(path), std::ios::binary);
    if (!in) return;
    std::string line, section;
    FileState* file = nullptr;
    *this = Settings{};
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            file = nullptr;
            continue;
        }
        if (section == "shortcuts") {
            shortcuts += line + "\n";
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (section == "settings") {
            if (k == "sidebar")
                sidebar = v == "1";
            else if (k == "sidebar_width")
                sidebarWidth = static_cast<float>(toNum(v, sidebarWidth));
            else if (k == "sidebar_tab")
                sidebarTab = static_cast<int>(toNum(v));
            else if (k == "thumb_size")
                thumbSize = static_cast<float>(toNum(v, thumbSize));
            else if (k == "thumb_labels")
                thumbLabels = v == "1";
            else if (k == "thumb_gaps")
                thumbGaps = v == "1";
            else if (k == "animations")
                animations = v == "1";
            else if (k == "page_gaps")
                pageGaps = v == "1";
            else if (k == "overview_columns")
                overviewColumns = static_cast<int>(toNum(v, overviewColumns));
            else if (k == "columns")
                defaultMode.columns = std::clamp(static_cast<int>(toNum(v, 1)), 1, 12);
            else if (k == "first_alone")
                defaultMode.firstAlone = v == "1";
            else if (k == "continuous")
                defaultMode.continuous = v == "1";
            else if (k == "theme")
                theme = v == "System" ? "system" : v; // the first versions' name
            else if (k == "mono_font")
                monoFont = v;
            else if (k == "toolbar_size")
                toolbarSize = std::clamp(static_cast<int>(toNum(v, 1)), 0, 2);
            else if (k == "ui_scale")
                uiScale = static_cast<float>(toNum(v, 1));
            else if (k == "font")
                font = v;
            else if (k == "font_size")
                fontSize = static_cast<float>(toNum(v, fontSize));
            else if (k == "window") {
                // x,y,w,h,maximized
                int v5[5] = {};
                if (sscanf(v.c_str(), "%d,%d,%d,%d,%d", &v5[0], &v5[1], &v5[2], &v5[3], &v5[4]) == 5) {
                    winX = v5[0];
                    winY = v5[1];
                    winW = v5[2];
                    winH = v5[3];
                    winMax = v5[4] != 0;
                }
            } else if (k == "clock")
                clock = static_cast<uint64_t>(toNum(v));
        } else if (section == "session") {
            if (k == "tab")
                tabs.push_back(v);
            else if (k == "active")
                activeTab = static_cast<int>(toNum(v));
        } else if (section == "recent") {
            if (k == "file") recent.push_back(v);
        } else if (section == "file") {
            if (k == "path") {
                file = &files[v];
                continue;
            }
            if (!file) continue;
            if (k == "page")
                file->pos.page = static_cast<int>(toNum(v));
            else if (k == "y")
                file->pos.y = toNum(v, -1);
            else if (k == "zoom")
                file->zoom = toNum(v, 1);
            else if (k == "zoom_mode")
                file->zoomMode = static_cast<pdfview::Viewer::ZoomMode>(std::clamp(static_cast<int>(toNum(v)), 0, 2));
            else if (k == "columns")
                file->mode.columns = std::clamp(static_cast<int>(toNum(v, 1)), 1, 12);
            else if (k == "first_alone")
                file->mode.firstAlone = v == "1";
            else if (k == "continuous")
                file->mode.continuous = v == "1";
            else if (k == "used")
                file->used = static_cast<uint64_t>(toNum(v));
            else if (k == "bookmark") {
                // page|y|title
                size_t a = v.find('|');
                size_t b = a == std::string::npos ? a : v.find('|', a + 1);
                if (b == std::string::npos) continue;
                file->bookmarks.push_back({static_cast<int>(toNum(v.substr(0, a))), toNum(v.substr(a + 1, b - a - 1), -1), v.substr(b + 1), 0});
            } else if (k == "bookmark_id" && !file->bookmarks.empty()) {
                // its own line, so older versions still read the bookmark
                file->bookmarks.back().id = std::strtoull(v.c_str(), nullptr, 16);
            }
        }
    }
    for (auto& [p, f] : files) {
        std::map<std::pair<std::string, std::string>, int> seen;
        for (auto& b : f.bookmarks)
            if (b.id == 0) b.id = legacyId(b, seen[{std::to_string(b.page) + "|" + num(b.y), b.title}]++);
    }
}

void Settings::loadLocked(const std::string& path)
{
    Hd::FileLock lock(lockPath(path));
    load(path);
}

std::string Settings::text() const
{
    std::ostringstream o;
    o << "# pdfview settings\n[settings]\n";
    o << "sidebar=" << (sidebar ? 1 : 0) << "\n";
    o << "sidebar_width=" << num(sidebarWidth) << "\n";
    o << "sidebar_tab=" << sidebarTab << "\n";
    o << "thumb_size=" << num(thumbSize) << "\n";
    o << "thumb_labels=" << (thumbLabels ? 1 : 0) << "\n";
    o << "thumb_gaps=" << (thumbGaps ? 1 : 0) << "\n";
    o << "animations=" << (animations ? 1 : 0) << "\n";
    o << "page_gaps=" << (pageGaps ? 1 : 0) << "\n";
    o << "overview_columns=" << overviewColumns << "\n";
    o << "columns=" << defaultMode.columns << "\n";
    o << "first_alone=" << (defaultMode.firstAlone ? 1 : 0) << "\n";
    o << "continuous=" << (defaultMode.continuous ? 1 : 0) << "\n";
    o << "theme=" << theme << "\n";
    o << "ui_scale=" << num(uiScale) << "\n";
    o << "font=" << font << "\n";
    o << "mono_font=" << monoFont << "\n";
    o << "toolbar_size=" << toolbarSize << "\n";
    o << "font_size=" << num(fontSize) << "\n";
    o << "window=" << winX << "," << winY << "," << winW << "," << winH << "," << (winMax ? 1 : 0) << "\n";
    o << "clock=" << clock << "\n";
    o << "[shortcuts]\n" << shortcuts;
    if (!shortcuts.empty() && shortcuts.back() != '\n') o << "\n";
    o << "[session]\n";
    for (const auto& t : tabs)
        o << "tab=" << t << "\n";
    o << "active=" << activeTab << "\n";
    o << "[recent]\n";
    for (const auto& r : recent)
        o << "file=" << r << "\n";
    // the most recently used files only
    std::vector<std::pair<uint64_t, const std::string*>> order;
    for (const auto& [p, f] : files)
        order.push_back({f.used, &p});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (order.size() > kFiles) order.resize(kFiles);
    for (const auto& [used, p] : order) {
        const FileState& f = files.at(*p);
        o << "[file]\npath=" << *p << "\n";
        o << "page=" << f.pos.page << "\n";
        o << "y=" << num(f.pos.y) << "\n";
        o << "zoom=" << num(f.zoom) << "\n";
        o << "zoom_mode=" << static_cast<int>(f.zoomMode) << "\n";
        o << "columns=" << f.mode.columns << "\n";
        o << "first_alone=" << (f.mode.firstAlone ? 1 : 0) << "\n";
        o << "continuous=" << (f.mode.continuous ? 1 : 0) << "\n";
        o << "used=" << f.used << "\n";
        for (const auto& b : f.bookmarks) {
            o << "bookmark=" << b.page << "|" << num(b.y) << "|" << oneLine(b.title) << "\n";
            o << "bookmark_id=" << hex(b.id) << "\n";
        }
    }
    return o.str();
}

Settings Settings::merge(const Settings& disk, const Settings& local, const Settings& base)
{
    Settings r = disk;
    // preferences, one by one
    pick(r.sidebar, local.sidebar, base.sidebar);
    pick(r.sidebarWidth, local.sidebarWidth, base.sidebarWidth);
    pick(r.sidebarTab, local.sidebarTab, base.sidebarTab);
    pick(r.thumbSize, local.thumbSize, base.thumbSize);
    pick(r.thumbLabels, local.thumbLabels, base.thumbLabels);
    pick(r.thumbGaps, local.thumbGaps, base.thumbGaps);
    pick(r.animations, local.animations, base.animations);
    pick(r.pageGaps, local.pageGaps, base.pageGaps);
    pick(r.overviewColumns, local.overviewColumns, base.overviewColumns);
    pick(r.defaultMode, local.defaultMode, base.defaultMode);
    pick(r.theme, local.theme, base.theme);
    pick(r.uiScale, local.uiScale, base.uiScale);
    pick(r.font, local.font, base.font);
    pick(r.monoFont, local.monoFont, base.monoFont);
    pick(r.fontSize, local.fontSize, base.fontSize);
    pick(r.toolbarSize, local.toolbarSize, base.toolbarSize);
    auto window = [](const Settings& s) { return std::tuple{s.winX, s.winY, s.winW, s.winH, s.winMax}; };
    if (window(local) != window(base)) std::tie(r.winX, r.winY, r.winW, r.winH, r.winMax) = window(local);
    r.clock = std::max(disk.clock, local.clock);

    // shortcuts, action by action (an action at its defaults has no line)
    {
        auto d = shortcutLines(disk.shortcuts), l = shortcutLines(local.shortcuts), b = shortcutLines(base.shortcuts);
        std::set<std::string> ids;
        for (const auto* m : {&d, &l, &b})
            for (const auto& [id, line] : *m) ids.insert(id);
        r.shortcuts.clear();
        for (const auto& id : ids) {
            auto get = [&](const auto& m) { auto it = m.find(id); return it == m.end() ? std::string() : it->second; };
            std::string line = get(d);
            pick(line, get(l), get(b));
            if (!line.empty()) r.shortcuts += line + "\n";
        }
    }

    // the open tabs are this window's: the last window saved restores
    r.tabs = local.tabs;
    r.activeTab = local.activeTab;

    // each file's view and bookmarks
    for (const auto& [path, l] : local.files) {
        auto bi = base.files.find(path);
        const FileState* b = bi == base.files.end() ? nullptr : &bi->second;
        auto [di, fresh] = r.files.try_emplace(path, l);
        if (fresh) continue; // only here, or forgotten in the file
        FileState& f = di->second;
        if (!b || !l.sameView(*b)) {
            f.pos = l.pos;
            f.zoomMode = l.zoomMode;
            f.zoom = l.zoom;
            f.mode = l.mode;
        }
        f.used = std::max(f.used, l.used);
        // bookmarks by id: local additions, removals and edits since base
        // win, the rest is the file's
        auto find = [](const std::vector<Bookmark>& v, uint64_t id) -> const Bookmark* {
            for (const auto& x : v)
                if (x.id == id) return &x;
            return nullptr;
        };
        static const std::vector<Bookmark> none;
        const std::vector<Bookmark>& bb = b ? b->bookmarks : none;
        std::vector<Bookmark> out;
        for (const auto& d : f.bookmarks) {
            const Bookmark* lb = find(l.bookmarks, d.id);
            const Bookmark* was = find(bb, d.id);
            if (!lb && was) continue; // removed here
            out.push_back(lb && (!was || !(*lb == *was)) ? *lb : d);
        }
        for (const auto& x : l.bookmarks) {
            if (find(f.bookmarks, x.id)) continue;
            const Bookmark* was = find(bb, x.id);
            // new here, or removed elsewhere after an edit here
            if (!was || !(x == *was)) out.push_back(x);
        }
        std::stable_sort(out.begin(), out.end(), [](const Bookmark& a, const Bookmark& c) {
            return a.page != c.page ? a.page < c.page : a.y < c.y;
        });
        f.bookmarks = std::move(out);
    }

    // recent files: opened or removed here since base, the rest the file's,
    // newest first
    {
        auto usedIn = [](const Settings& s, const std::string& p) -> uint64_t {
            auto it = s.files.find(p);
            return it == s.files.end() ? 0 : it->second.used;
        };
        auto has = [](const std::vector<std::string>& v, const std::string& p) { return std::find(v.begin(), v.end(), p) != v.end(); };
        std::vector<std::string> out;
        for (const auto& p : disk.recent)
            if (!(has(base.recent, p) && !has(local.recent, p))) out.push_back(p);
        for (const auto& p : local.recent)
            if (!has(out, p) && (!has(base.recent, p) || usedIn(local, p) != usedIn(base, p))) out.push_back(p);
        std::stable_sort(out.begin(), out.end(), [&](const std::string& a, const std::string& c) { return usedIn(r, a) > usedIn(r, c); });
        if (out.size() > kRecent) out.resize(kRecent);
        r.recent = std::move(out);
    }
    return r;
}

bool Settings::saveMerged(const std::string& path, const Settings& base) const
{
    std::error_code ec;
    fs::create_directories(u8(path).parent_path(), ec);
    Hd::FileLock lock(lockPath(path));
    if (!lock.locked()) return false;
    Settings disk;
    disk.load(path);
    return Hd::replaceFile(path, merge(disk, *this, base).text());
}

void Settings::addRecent(const std::string& path)
{
    std::erase(recent, path);
    recent.insert(recent.begin(), path);
    if (recent.size() > kRecent) recent.resize(kRecent);
}

FileState& Settings::file(const std::string& path)
{
    auto [it, fresh] = files.try_emplace(path);
    if (fresh) it->second.mode = defaultMode;
    // the wall clock orders uses across windows; always forward here
    clock = std::max(clock + 1, nowMs());
    it->second.used = clock;
    return it->second;
}
