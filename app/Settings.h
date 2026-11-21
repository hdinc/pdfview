#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <pdfview/Viewer.h>

// what the app remembers between runs, in one text file in the config dir:
// preferences, shortcuts, the open tabs, recent files and each file's view
// and bookmarks
struct Bookmark {
    int page = 0;
    double y = -1;
    std::string title;
};

struct FileState {
    pdfview::Dest pos{0, -1};
    pdfview::Viewer::ZoomMode zoomMode = pdfview::Viewer::ZoomMode::FitWidth;
    double zoom = 1;
    pdfview::ViewMode mode;
    std::vector<Bookmark> bookmarks;
    uint64_t used = 0; // when it was last opened, for forgetting old files
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
    uint64_t clock = 0;

    // nothing happens when the file is missing or unreadable
    void load(const std::string& path);
    // written to a temporary file, then renamed over the old one
    bool save(const std::string& path) const;

    void addRecent(const std::string& path);
    // the file's state, created with the default mode; marks it used
    FileState& file(const std::string& path);
};
