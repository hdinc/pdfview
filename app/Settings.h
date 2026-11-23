#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <pdfview/Viewer.h>

// what the app remembers between runs, in one text file in the config dir:
// preferences, shortcuts, the open tabs, recent files and each file's view
// and bookmarks. Several app windows (processes) can share it: each save
// merges its own changes into the file's latest contents, see saveMerged.

uint64_t newBookmarkId();

struct Bookmark {
    int page = 0;
    double y = -1;
    std::string title;
    uint64_t id = newBookmarkId(); // stable across saves, for merging

    bool operator==(const Bookmark&) const = default;
};

struct FileState {
    pdfview::Dest pos{0, -1};
    pdfview::Viewer::ZoomMode zoomMode = pdfview::Viewer::ZoomMode::FitWidth;
    double zoom = 1;
    pdfview::ViewMode mode;
    std::vector<Bookmark> bookmarks;
    uint64_t used = 0; // when it was last opened (ms since 1970), for forgetting old files

    bool sameView(const FileState& o) const
    {
        return pos.page == o.pos.page && pos.y == o.pos.y && zoomMode == o.zoomMode && zoom == o.zoom && mode == o.mode;
    }
};

struct Settings {
    static constexpr size_t kRecent = 20;
    static constexpr size_t kFiles = 500;

    // preferences
    bool sidebar = true;
    float sidebarWidth = 300;
    int sidebarTab = 0; // thumbnails, contents, bookmarks
    float thumbSize = 150;
    bool thumbLabels = true;
    bool thumbGaps = true;
    bool animations = true;
    bool pageGaps = true;
    int overviewColumns = 4;
    pdfview::ViewMode defaultMode;
    std::string shortcuts; // Actions::save()
    std::string theme = "system"; // a key of Hd::Theme::find, or "system"
    float uiScale = 1; // times the monitor's
    std::string font; // Hd::toString(FontRef); empty: the system's
    std::string monoFont; // the same
    float fontSize = 16; // pixels at scale 1
    int toolbarSize = 1; // Hd::ToolbarSize
    // the window's normal place and size (w 0: none saved), maximized
    int winX = 0, winY = 0, winW = 0, winH = 0;
    bool winMax = false;

    // session
    std::vector<std::string> tabs;
    int activeTab = 0;
    std::vector<std::string> recent;
    std::map<std::string, FileState> files; // by normal path
    uint64_t clock = 0; // the last `used` given out

    // nothing happens when the file is missing or unreadable
    void load(const std::string& path);
    // the same while holding the file's lock, so no save is half done
    void loadLocked(const std::string& path);
    // the file's text
    std::string text() const;
    // Saves `*this` (the local state) safely with other processes: holding
    // the file's lock, reads its latest contents, keeps from `*this` what
    // changed since `base` (the local state at the last load or successful
    // save), keeps the rest from the file, and replaces the file in one step.
    // The open tabs are always this window's. False when the lock or the
    // write failed; the file is then unchanged.
    bool saveMerged(const std::string& path, const Settings& base) const;
    // the three-way merge saveMerged writes
    static Settings merge(const Settings& disk, const Settings& local, const Settings& base);

    void addRecent(const std::string& path);
    // the file's state, created with the default mode; marks it used
    FileState& file(const std::string& path);
};
