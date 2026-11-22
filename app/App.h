#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <Hd/Fonts.h>
#include <Hd/Theme.h>

#include <pdfview/Actions.h>
#include <pdfview/Panels.h>
#include <pdfview/Viewer.h>

#include "Settings.h"

namespace Hd {
class Gui;
class Window;
}

// the standalone viewer: documents in tabs, a sidebar with thumbnails,
// contents and bookmarks, a toolbar and menus, configurable shortcuts, and
// a session kept between runs
class App {
public:
    App(Hd::Window& window, Hd::Gui& gui, const std::vector<std::string>& files);
    ~App();

    // builds this frame's UI
    void frame();
    // keep drawing frames rather than waiting for events
    bool needsFrames() const;
    void open(const std::vector<std::string>& files);

private:
    struct Tab {
        uint64_t id = 0;
        std::string path; // normal
        std::unique_ptr<pdfview::Viewer> viewer;
        // the file on disk, to reload it when it changes
        std::filesystem::file_time_type mtime{};
        uintmax_t size = 0;
        double changedAt = -1; // when a change was seen, waiting for it to settle
        char password[128] = "";
        struct {
            pdfview::Dest target;
            pdfview::Dest landing;
        } bookmarkJump;
    };

    void addActions();
    Tab* active();
    pdfview::Viewer* activeViewer();
    void openOne(const std::string& path, bool select);
    void start(Tab& t, const std::string& password = {});
    void close(size_t i);
    void reload(Tab& t);
    void remember(const Tab& t);
    void watchFiles();
    void saveSettings();
    std::string settingsPath() const;

    void drawMenus();
    void drawToolbar();
    void drawViewModeMenu();
    void drawWorkspace();
    Tab* drawTabs();
    void drawSidebar(Tab& t, float width, float splitterWidth);
    void drawPageMenu(Tab& t, int page);
    void addBookmark(Tab& t, pdfview::Dest dest);
    const Bookmark* adjacentBookmark(const Tab& t, int dir) const;
    void openBookmark(Tab& t, const Bookmark& bookmark);
    void drawBookmarks(Tab& t);
    void drawLocked(Tab& t);
    void drawDialogs();
    void drawLookMenu();
    void drawFontsWindow();
    void applyFonts();
    void menuItem(const char* id, const char* icon = nullptr);
    std::string tabLabel(const Tab& t) const;

    Hd::Window& mWindow;
    Hd::Gui& mGui;
    Settings mSettings;
    pdfview::Actions mActions;
    pdfview::OutlinePanel mOutline;
    pdfview::ThumbnailPanel mThumbs;
    pdfview::ShortcutEditor mShortcutEditor;
    std::vector<std::unique_ptr<Tab>> mTabs;
    std::vector<std::unique_ptr<Tab>> mDead; // closed: freed next frame, after their textures were drawn
    uint64_t mNextId = 1;
    uint64_t mActive = 0; // the shown tab's id
    uint64_t mSelect = 0; // a tab to bring to the front
    bool mShortcutsOpen = false;
    Hd::ThemeEditor mThemeEditor;
    bool mFontsOpen = false;
    std::vector<Hd::FontFace> mFontList; // read when the fonts window first opens
    bool mFontListRead = false;
    Hd::FontPicker mUiPicker, mMonoPicker{true};
    bool mSideForce = true; // select the sidebar tab in the settings
    bool mQuit = false;
    std::string mAskUri; // a link waiting to be confirmed
    int mRenaming = -1; // the bookmark being renamed
    char mRenameBuf[256] = "";
    double mLastWatch = 0;
    std::string mTitle;
};
