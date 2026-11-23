#include "Settings.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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

}

void Settings::load(const std::string& path)
{
    std::ifstream in(u8(path), std::ios::binary);
    if (!in) return;
    std::string line, section;
    FileState* file = nullptr;
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
                file->bookmarks.push_back({static_cast<int>(toNum(v.substr(0, a))), toNum(v.substr(a + 1, b - a - 1), -1), v.substr(b + 1)});
            }
        }
    }
}

bool Settings::save(const std::string& path) const
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
        for (const auto& b : f.bookmarks)
            o << "bookmark=" << b.page << "|" << num(b.y) << "|" << oneLine(b.title) << "\n";
    }
    std::error_code ec;
    fs::path target = u8(path);
    fs::create_directories(target.parent_path(), ec);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << o.str();
        if (!out.flush()) return false;
    }
    fs::rename(tmp, target, ec);
    return !ec;
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
    it->second.used = ++clock;
    return it->second;
}
