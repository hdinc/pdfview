#include "App.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <Hd/Gui.h>
#include <Hd/Icons.h>
#include <Hd/Platform.h>
#include <Hd/Theme.h>
#include <Hd/Window.h>


namespace fs = std::filesystem;
using pdfview::Viewer;

namespace {

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string fileName(const std::string& path)
{
    auto u = u8(path).filename().u8string();
    return std::string(u.begin(), u.end());
}

std::string parentName(const std::string& path)
{
    auto u = u8(path).parent_path().filename().u8string();
    return std::string(u.begin(), u.end());
}

bool stamp(const std::string& path, fs::file_time_type& t, uintmax_t& size)
{
    std::error_code ec;
    t = fs::last_write_time(u8(path), ec);
    if (ec) return false;
    size = fs::file_size(u8(path), ec);
    return !ec;
}

// links the app opens; anything else (files, programs) is only shown
bool safeUri(const std::string& uri)
{
    for (const char* s : {"http://", "https://", "mailto:"})
        if (uri.size() > strlen(s) && std::equal(s, s + strlen(s), uri.begin(), [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); })) return true;
    return false;
}

constexpr double kWatchEvery = 1.0; // seconds between looks at open files
constexpr double kSettle = 0.3; // a changed file must stay the same this long
constexpr size_t kInactiveTextureBudget = 128u << 20;

}

App::App(Hd::Window& window, Hd::Gui& gui, const std::vector<std::string>& files)
    : mWindow(window)
    , mGui(gui)
{
    Hd::setConfigName("pdfview");
    mSettings.loadLocked(settingsPath());
    mSaved = mSettings;
    // the look
    mGui.setTheme(mSettings.theme);
    mGui.setUserScale(mSettings.uiScale);
    applyFonts();
    mGui.setFontSize(mSettings.fontSize);
    Hd::setToolbarSize(static_cast<Hd::ToolbarSize>(mSettings.toolbarSize));
    if (mSettings.winW > 0) mWindow.setGeometry({mSettings.winX, mSettings.winY, mSettings.winW, mSettings.winH, mSettings.winMax});
    // Ctrl+Tab is ours (tabs), not ImGui's window switcher
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
    addActions();
    mActions.load(mSettings.shortcuts);
    mThumbs.size = mSettings.thumbSize;
    mThumbs.showLabels = mSettings.thumbLabels;
    mThumbs.gaps = mSettings.thumbGaps;
    if (!files.empty())
        open(files);
    else {
        for (const auto& p : mSettings.tabs)
            if (fs::exists(u8(p))) openOne(p, false);
        if (!mTabs.empty()) mSelect = mTabs[static_cast<size_t>(std::clamp(mSettings.activeTab, 0, static_cast<int>(mTabs.size()) - 1))]->id;
    }
}

App::~App()
{
    saveSettings();
    mTabs.clear();
    mDead.clear();
}

std::string App::settingsPath() const
{
    auto u = (Hd::configDir() / "settings.ini").u8string();
    return std::string(u.begin(), u.end());
}

void App::applyFonts() { mGui.setFonts(Hd::parseFontRef(mSettings.font), Hd::parseFontRef(mSettings.monoFont)); }

void App::saveSettings()
{
    mSettings.tabs.clear();
    mSettings.activeTab = 0;
    for (size_t i = 0; i < mTabs.size(); i++) {
        remember(*mTabs[i]);
        mSettings.tabs.push_back(mTabs[i]->path);
        if (mTabs[i]->id == mActive) mSettings.activeTab = static_cast<int>(i);
    }
    mSettings.thumbSize = mThumbs.size;
    mSettings.thumbLabels = mThumbs.showLabels;
    mSettings.thumbGaps = mThumbs.gaps;
    mSettings.toolbarSize = static_cast<int>(Hd::getToolbarSize());
    Hd::Window::Geometry g = mWindow.getGeometry();
    mSettings.winX = g.x;
    mSettings.winY = g.y;
    mSettings.winW = g.w;
    mSettings.winH = g.h;
    mSettings.winMax = g.maximized;
    mSettings.shortcuts = mActions.save();
    // Other windows may have saved since: merge into the file's latest state.
    if (mSettings.saveMerged(settingsPath(), mSaved)) mSaved = mSettings;
}

// --- tabs

App::Tab* App::active()
{
    for (auto& t : mTabs)
        if (t->id == mActive) return t.get();
    return nullptr;
}

Viewer* App::activeViewer()
{
    Tab* t = active();
    return t ? t->viewer.get() : nullptr;
}

void App::open(const std::vector<std::string>& files)
{
    for (size_t i = 0; i < files.size(); i++)
        openOne(files[i], i + 1 == files.size());
}

void App::openOne(const std::string& file, bool select)
{
    std::string path = Hd::Platform::normalPath(file);
    for (auto& t : mTabs)
        if (t->path == path) {
            if (select) mSelect = t->id;
            return;
        }
    auto t = std::make_unique<Tab>();
    t->id = mNextId++;
    t->path = path;
    Viewer::Options o;
    o.toolbar = false;
    o.shortcuts = false;
    o.animations = mSettings.animations;
    o.onOpenUri = [this](const std::string& uri) { mAskUri = uri; };
    o.pageMenu = [this, tab = t.get()](Viewer&, int page) { drawPageMenu(*tab, page); };
    t->viewer = std::make_unique<Viewer>(o);
    t->viewer->setWake([] { glfwPostEmptyEvent(); });
    t->viewer->setOverviewColumns(mSettings.overviewColumns);
    start(*t);
    mSettings.addRecent(path);
    if (select || mTabs.empty()) mSelect = t->id;
    mTabs.push_back(std::move(t));
}

void App::start(Tab& t, const std::string& password)
{
    stamp(t.path, t.mtime, t.size);
    t.changedAt = -1;
    t.bookmarkJump = {};
    bool known = mSettings.files.count(t.path) > 0;
    FileState& f = mSettings.file(t.path);
    t.viewer->open(t.path, password);
    t.viewer->setViewMode(f.mode);
    // a file seen before opens where it was left; a new one the viewer's
    // way (fit width, but not huge on a wide window)
    if (known) t.viewer->restoreView(f.pos, f.zoomMode, f.zoom);
}

void App::remember(const Tab& t)
{
    Viewer& v = *t.viewer;
    if (v.state() != Viewer::State::Ready) return;
    FileState& f = mSettings.files[t.path];
    f.pos = v.position();
    f.zoomMode = v.zoomMode();
    f.zoom = v.zoom();
    f.mode = v.viewMode();
}

void App::close(size_t i)
{
    if (i >= mTabs.size()) return;
    remember(*mTabs[i]);
    if (mTabs[i]->id == mActive) {
        // the neighbour takes its place
        if (mTabs.size() > 1) mSelect = mTabs[i + 1 < mTabs.size() ? i + 1 : i - 1]->id;
        mActive = 0;
    }
    mDead.push_back(std::move(mTabs[i]));
    mTabs.erase(mTabs.begin() + static_cast<std::ptrdiff_t>(i));
}

void App::reload(Tab& t)
{
    remember(t);
    start(t, t.password);
}

void App::watchFiles()
{
    double now = ImGui::GetTime();
    bool pending = std::any_of(mTabs.begin(), mTabs.end(), [](const auto& t) { return t->changedAt >= 0; });
    if (!pending && now - mLastWatch < kWatchEvery) return;
    mLastWatch = now;
    for (auto& t : mTabs) {
        fs::file_time_type mt;
        uintmax_t sz = 0;
        if (!stamp(t->path, mt, sz)) continue; // gone for now (being rewritten)
        if (mt != t->mtime || sz != t->size) {
            // changed: wait until it stops changing
            t->mtime = mt;
            t->size = sz;
            t->changedAt = now;
        } else if (t->changedAt >= 0 && now - t->changedAt >= kSettle) {
            reload(*t);
        }
    }
}

std::string App::tabLabel(const Tab& t) const
{
    std::string name = fileName(t.path);
    for (const auto& o : mTabs)
        if (o.get() != &t && fileName(o->path) == name) return name + " (" + parentName(t.path) + ")";
    return name;
}

// --- actions

void App::addActions()
{
    Viewer::registerActions(mActions, [this] { return activeViewer(); });
    auto has = [this] { return active() != nullptr; };
    mActions.add("file.open", "Open...", "File", "Ctrl+O", "", [this] { open(Hd::Platform::openFilesDialog(mWindow.Id(), "Open", {{"PDF documents", "*.pdf;*.PDF"}})); });
    mActions.add("file.reload", "Reload", "File", "F5", "", [this] {
        if (Tab* t = active()) reload(*t);
    }).enabled = has;
    mActions.add("file.close", "Close Tab", "File", "Ctrl+W", "", [this] {
        for (size_t i = 0; i < mTabs.size(); i++)
            if (mTabs[i]->id == mActive) {
                close(i);
                break;
            }
    }).enabled = has;
    mActions.add("app.quit", "Quit", "File", "Ctrl+Q", "", [this] { mQuit = true; });
    auto cycle = [this](int dir) {
        return [this, dir] {
            if (mTabs.size() < 2) return;
            for (size_t i = 0; i < mTabs.size(); i++)
                if (mTabs[i]->id == mActive) {
                    size_t n = mTabs.size();
                    mSelect = mTabs[(i + n + static_cast<size_t>(dir + static_cast<int>(n))) % n]->id;
                    return;
                }
        };
    };
    mActions.add("tab.next", "Next Tab", "Tabs", "Ctrl+Tab", "", cycle(1));
    mActions.add("tab.prev", "Previous Tab", "Tabs", "Ctrl+Shift+Tab", "", cycle(-1));
    mActions.add("view.sidebar", "Show Sidebar", "View", "F7", "", [this] { mSettings.sidebar = !mSettings.sidebar; }).checked = [this] { return mSettings.sidebar; };
    mActions.add("view.thumbnails", "Thumbnails", "View", "", "", [this] {
        mSettings.sidebar = true;
        mSettings.sidebarTab = 0;
        mSideForce = true;
    });
    mActions.add("view.contents", "Contents", "View", "", "", [this] {
        mSettings.sidebar = true;
        mSettings.sidebarTab = 1;
        mSideForce = true;
    });
    mActions.add("bookmark.add", "Add Bookmark", "Bookmarks", "Ctrl+B", "", [this] {
        Tab* t = active();
        if (!t || t->viewer->state() != Viewer::State::Ready) return;
        addBookmark(*t, t->viewer->position());
    }).enabled = has;
    for (int dir : {-1, 1}) {
        bool previous = dir < 0;
        mActions.add(previous ? "bookmark.prev" : "bookmark.next",
                     previous ? "Previous Bookmark" : "Next Bookmark", "Bookmarks", "", "", [this, dir] {
            if (Tab* t = active())
                if (const Bookmark* b = adjacentBookmark(*t, dir)) openBookmark(*t, *b);
        }).enabled = [this, dir] { Tab* t = active(); return t && adjacentBookmark(*t, dir); };
    }
    mActions.add("settings.shortcuts", "Configure Keyboard Shortcuts...", "Settings", "Ctrl+Alt+Comma", "", [this] { mShortcutsOpen = true; });
}

void App::menuItem(const char* id, const char* icon)
{
    pdfview::Action* a = mActions.find(id);
    if (!a) return;
    bool enabled = !a->enabled || a->enabled();
    bool checked = a->checked && a->checked();
    std::string keys = mActions.keysOf(id);
    if (ImGui::MenuItemEx(a->name.c_str(), icon, keys.empty() ? nullptr : keys.c_str(), checked, enabled) && a->run) a->run();
}

bool App::needsFrames() const
{
    if (mSelect || mActions.pending().n > 0 || mActions.holding() || mShortcutEditor.capturing()) return true;
    if (mThumbs.needsFrames() || mOutline.needsFrames() || mBookmarkScroll.needsFrames()) return true;
    for (const auto& t : mTabs)
        if (t->changedAt >= 0 || (t->id == mActive && t->viewer->needsFrames())) return true;
    return false;
}

// --- drawing

void App::frame()
{
    mDead.clear();
    if (mQuit) glfwSetWindowShouldClose(mWindow.Id(), GLFW_TRUE);
    watchFiles();
    mActions.update();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    drawMenus();
    drawToolbar();
    drawWorkspace();
    ImGui::End();
    drawDialogs();

    Tab* t = active();
    std::string title = t ? fileName(t->path) + " - pdfview" : "pdfview";
    if (title != mTitle) {
        mTitle = title;
        glfwSetWindowTitle(mWindow.Id(), title.c_str());
    }
}

void App::drawMenus()
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        menuItem("file.open");
        if (ImGui::BeginMenu("Open Recent", !mSettings.recent.empty())) {
            std::string pick;
            for (const auto& r : mSettings.recent)
                if (ImGui::MenuItem(r.c_str())) pick = r;
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List")) mSettings.recent.clear();
            ImGui::EndMenu();
            if (!pick.empty()) open({pick});
        }
        menuItem("file.reload");
        ImGui::Separator();
        menuItem("file.close");
        menuItem("app.quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        menuItem("edit.copy");
        menuItem("edit.select_all");
        ImGui::Separator();
        menuItem("find.find");
        menuItem("find.next");
        menuItem("find.prev");
        menuItem("find.whole_words");
        ImGui::Separator();
        menuItem("tool.browse");
        menuItem("tool.text");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        drawViewModeMenu();
        ImGui::Separator();
        menuItem("zoom.in");
        menuItem("zoom.out");
        menuItem("zoom.fit_width");
        menuItem("zoom.fit_page");
        menuItem("zoom.actual");
        ImGui::Separator();
        menuItem("view.sidebar");
        menuItem("view.thumbnails");
        menuItem("view.contents");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Go")) {
        menuItem("go.back");
        menuItem("go.forward");
        ImGui::Separator();
        menuItem("go.prev_page");
        menuItem("go.next_page");
        menuItem("go.first");
        menuItem("go.last");
        menuItem("go.page");
        ImGui::Separator();
        menuItem("tab.next");
        menuItem("tab.prev");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Bookmarks")) {
        menuItem("bookmark.add");
        Tab* t = active();
        if (t && !mSettings.files[t->path].bookmarks.empty()) {
            ImGui::Separator();
            for (const auto& b : mSettings.files[t->path].bookmarks)
                if (ImGui::MenuItem(b.title.c_str(), t->viewer->pageLabel(b.page).c_str())) openBookmark(*t, b);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
        menuItem("settings.shortcuts");
        ImGui::Separator();
        drawLookMenu();
        ImGui::Separator();
        if (ImGui::MenuItem("Animations", nullptr, &mSettings.animations)) {
            for (auto& t : mTabs) t->viewer->options().animations = mSettings.animations;
            saveSettings();
        }
        if (ImGui::SliderInt("Overview columns", &mSettings.overviewColumns, 3, 10))
            for (auto& t : mTabs)
                t->viewer->setOverviewColumns(mSettings.overviewColumns);
        bool def = false;
        if (Viewer* v = activeViewer(); v && ImGui::MenuItem("Use This View Mode for New Files", nullptr, &def)) mSettings.defaultMode = v->viewMode();
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::drawLookMenu()
{
    if (ImGui::BeginMenu("Theme")) {
        // "system": Dark or Light as the system is set
        if (ImGui::MenuItem("System", nullptr, mSettings.theme == "system") && mSettings.theme != "system") {
            mSettings.theme = "system";
            mGui.setTheme(mSettings.theme);
            saveSettings();
        }
        ImGui::Separator();
        if (Hd::Theme::menuItems(mSettings.theme)) {
            mGui.setTheme(mSettings.theme);
            saveSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Edit Themes...")) mThemeEditor.open = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Interface Size")) {
        for (int p : {75, 90, 100, 110, 125, 150, 175, 200}) {
            char l[16];
            snprintf(l, sizeof l, "%d%%", p);
            if (ImGui::MenuItem(l, nullptr, std::lround(mSettings.uiScale * 100) == p)) {
                mSettings.uiScale = p / 100.0f;
                mGui.setUserScale(mSettings.uiScale);
                saveSettings();
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Toolbar Size")) {
        Hd::toolbarSizeMenu();
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Fonts...")) mFontsOpen = true;
}

void App::drawFontsWindow()
{
    if (!mFontsOpen) return;
    if (!mFontListRead) {
        mFontListRead = true;
        mFontList = Hd::systemFonts(); // takes a moment, once
    }
    ImGui::SetNextWindowSize({560 * mGui.scale(), 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Fonts", &mFontsOpen, ImGuiWindowFlags_NoCollapse)) {
        Hd::FontRef ui = Hd::parseFontRef(mSettings.font), mono = Hd::parseFontRef(mSettings.monoFont);
        bool changed = mUiPicker.gui("Interface", ui, mFontList, "System default");
        changed |= mMonoPicker.gui("Monospace", mono, mFontList, "System default");
        if (changed) {
            mSettings.font = Hd::toString(ui);
            mSettings.monoFont = Hd::toString(mono);
            applyFonts();
            saveSettings();
        }
        float size = mSettings.fontSize;
        if (ImGui::SliderFloat("Size", &size, 10, 28, "%.0f px")) {
            mSettings.fontSize = std::round(size);
            mGui.setFontSize(mSettings.fontSize);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
        for (const auto& e : mGui.fontErrors())
            ImGui::TextColored({0.9f, 0.4f, 0.3f, 1}, "%s", e.c_str());
    }
    ImGui::End();
}

void App::drawViewModeMenu()
{
    menuItem("view.single");
    menuItem("view.facing");
    menuItem("view.facing_first");
    menuItem("view.overview");
    ImGui::Separator();
    menuItem("view.continuous");
}

namespace Icon {
// Font Awesome (solid) glyphs pdfview uses beyond Hd's own
constexpr const char* up = "\xef\x81\xb7"; // chevron-up
constexpr const char* down = "\xef\x81\xb8"; // chevron-down
constexpr const char* sidebar = "\xef\x83\x9b"; // table-columns
constexpr const char* fitPage = "\xef\x81\xa5"; // expand
constexpr const char* viewMode = "\xef\x8c\x82"; // images
constexpr const char* hand = "\xef\x89\x96"; // hand
constexpr const char* textCursor = "\xef\x89\x86"; // i-cursor
constexpr const char* find = "\xef\x80\x82"; // magnifying-glass
constexpr const char* bookmark = "\xef\x80\xae"; // bookmark
}

void App::drawToolbar()
{
    Viewer* v = activeViewer();
    bool ready = v && v->state() == Viewer::State::Ready;
    float gap = Hd::toolbarGap();
    auto tip = [&](const char* name, const char* id) {
        std::string k = mActions.keysOf(id);
        return k.empty() ? std::string(name) : std::string(name) + " (" + k + ")";
    };
    auto sep = [&] { ImGui::SameLine(0, gap * 4); };
    if (Hd::iconButton(Icon::sidebar, tip("Show Sidebar", "view.sidebar").c_str(), mSettings.sidebar)) mSettings.sidebar = !mSettings.sidebar;
    sep();
    if (Hd::iconButton(Hd::Icon::prev, tip("Back", "go.back").c_str(), false, ready && v->canBack())) v->back();
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Hd::Icon::next, tip("Forward", "go.forward").c_str(), false, ready && v->canForward())) v->forward();
    sep();
    if (Hd::iconButton(Icon::up, tip("Previous Page", "go.prev_page").c_str(), false, ready)) v->prevPage();
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Icon::down, tip("Next Page", "go.next_page").c_str(), false, ready)) v->nextPage();
    if (ready) {
        ImGui::SameLine(0, gap * 2);
        Hd::toolbarCenter();
        v->drawPageBox();
    }
    sep();
    if (Hd::iconButton(Hd::Icon::zoomOut, tip("Zoom Out", "zoom.out").c_str(), false, ready)) v->zoomOut();
    ImGui::SameLine(0, gap);
    if (ready) {
        Hd::toolbarCenter();
        v->drawZoomCombo();
        ImGui::SameLine(0, gap);
    }
    if (Hd::iconButton(Hd::Icon::zoomIn, tip("Zoom In", "zoom.in").c_str(), false, ready)) v->zoomIn();
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Hd::Icon::fit, tip("Fit Width", "zoom.fit_width").c_str(), ready && v->zoomMode() == Viewer::ZoomMode::FitWidth, ready)) v->fitWidth();
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Icon::fitPage, tip("Fit Page", "zoom.fit_page").c_str(), ready && v->zoomMode() == Viewer::ZoomMode::FitPage, ready)) v->fitPage();
    sep();
    if (Hd::iconButton(Icon::viewMode, "View Mode", false, ready)) ImGui::OpenPopup("##viewmode");
    if (ImGui::BeginPopup("##viewmode")) {
        drawViewModeMenu();
        ImGui::EndPopup();
    }
    sep();
    bool text = ready && v->tool() == Viewer::Tool::Text;
    if (Hd::iconButton(Icon::hand, tip("Browse", "tool.browse").c_str(), ready && !text, ready)) v->setTool(Viewer::Tool::Browse);
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Icon::textCursor, tip("Text Selection", "tool.text").c_str(), text, ready)) v->setTool(Viewer::Tool::Text);
    sep();
    if (Hd::iconButton(Icon::find, tip("Find", "find.find").c_str(), ready && v->findBarShown(), ready)) {
        if (v->findBarShown())
            v->hideFindBar();
        else
            v->showFindBar();
    }
    ImGui::SameLine(0, gap);
    if (Hd::iconButton(Icon::bookmark, tip("Add Bookmark", "bookmark.add").c_str(), false, ready)) mActions.trigger("bookmark.add");
}

void App::drawWorkspace()
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (mTabs.empty()) {
        const char* msg = "Open a PDF with Ctrl+O, or drop one here.";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::SetCursorScreenPos({origin.x + std::max(0.0f, (avail.x - ts.x) / 2),
                                   origin.y + std::max(0.0f, (avail.y - ts.y) / 2)});
        ImGui::TextDisabled("%s", msg);
        mActive = 0;
        mSelect = 0;
        return;
    }

    float scale = mGui.scale();
    float maxSidebarWidth = std::max(0.0f, avail.x * 0.7f);
    float sidebarWidth = mSettings.sidebar
        ? std::floor(std::clamp(mSettings.sidebarWidth * scale, std::min(150 * scale, maxSidebarWidth), maxSidebarWidth))
        : 0;
    float splitterWidth = std::max(1.0f, std::floor(std::min(8 * scale, avail.x * 0.1f)));
    float documentX = origin.x + (sidebarWidth > 0 ? sidebarWidth + splitterWidth : 0);

    // Resolve the visible tab first so the sidebar and document use the same viewer.
    ImGui::SetCursorScreenPos({documentX, origin.y});
    Tab* tab = drawTabs();
    if (!tab) return;
    float documentY = ImGui::GetCursorScreenPos().y;
    if (sidebarWidth > 0) {
        ImGui::SetCursorScreenPos(origin);
        drawSidebar(*tab, sidebarWidth, splitterWidth);
    }

    ImGui::SetCursorScreenPos({documentX, documentY});
    ImGui::BeginGroup();
    ImGui::PushID(tab);
    if (tab->viewer->state() == Viewer::State::Failed && tab->viewer->error() == "locked")
        drawLocked(*tab);
    else {
        Viewer& viewer = *tab->viewer;
        bool showFind = viewer.findBarShown() && viewer.state() == Viewer::State::Ready;
        const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
        ImVec2 padding{spacing.x / 2, spacing.y / 2};
        float findHeight = ImGui::GetFrameHeight() + 2 * padding.y;
        viewer.draw("viewer", {0, showFind ? -(findHeight + spacing.y) : 0});
        if (showFind) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
            bool visible = ImGui::BeginChild("##find-footer", {0, findHeight}, ImGuiChildFlags_AlwaysUseWindowPadding,
                                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (visible) viewer.drawFindFooter();
            ImGui::EndChild();
        }
    }
    ImGui::PopID();
    ImGui::EndGroup();
}

App::Tab* App::drawTabs()
{
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) return nullptr;
    Tab* shown = nullptr;
    size_t closeIdx = SIZE_MAX;
    for (size_t i = 0; i < mTabs.size(); i++) {
        Tab& t = *mTabs[i];
        bool open = true;
        std::string label = tabLabel(t) + "###tab" + std::to_string(t.id);
        ImGuiTabItemFlags f = t.id == mSelect ? ImGuiTabItemFlags_SetSelected : 0;
        bool sel = ImGui::BeginTabItem(label.c_str(), &open, f);
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) open = false;
            ImGui::SetTooltip("%s", t.path.c_str());
        }
        if (sel) {
            shown = &t;
            ImGui::EndTabItem();
        }
        if (!open) closeIdx = i;
    }
    ImGui::EndTabBar();
    mSelect = 0;
    uint64_t shownId = shown ? shown->id : 0;
    if (shownId != mActive) {
        mActions.clearHeld();
        std::vector<Viewer*> inactive;
        for (auto& t : mTabs) {
            if (t->id == mActive) t->viewer->suspendRendering();
            if (t->id != shownId) inactive.push_back(t->viewer.get());
        }
        Viewer::trimInactiveCaches(inactive, kInactiveTextureBudget);
        mActive = shownId;
    }
    // close() keeps the tab alive through the current frame in mDead.
    if (closeIdx != SIZE_MAX) close(closeIdx);
    return shown;
}

void App::drawSidebar(Tab& t, float width, float splitterWidth)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float sc = mGui.scale();
    ImGui::PushID(&t);
    ImGui::BeginChild("##sidebar", {width, 0});
    if (ImGui::BeginTabBar("##sidetabs")) {
        constexpr const char* icons[] = {Icon::viewMode, Hd::Icon::list, Icon::bookmark};
        constexpr const char* labels[] = {"##Thumbnails", "##Contents", "##Bookmarks"};
        float available = ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemInnerSpacing.x;
        float edge = 0;
        for (int i = 0; i < 3; i++) {
            float nextEdge = std::floor(available * (i + 1) / 3);
            ImGui::SetNextItemWidth(std::max(1.0f, nextEdge - edge));
            edge = nextEdge;
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_NoTooltip;
            if (mSideForce && mSettings.sidebarTab == i) flags |= ImGuiTabItemFlags_SetSelected;
            bool selected = ImGui::BeginTabItem(labels[i], nullptr, flags);
            if (ImGui::IsItemVisible()) {
                ImRect rect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
                ImGui::RenderTextClipped(rect.Min, rect.Max, icons[i], nullptr, nullptr, {0.5f, 0.5f}, &rect);
            }
            ImGui::SetItemTooltip("%s", labels[i] + 2);
            if (selected) {
                if (!mSideForce) mSettings.sidebarTab = i;
                if (t.viewer->state() == Viewer::State::Ready) {
                    if (i == 0) mThumbs.draw(*t.viewer);
                    if (i == 1) mOutline.draw(*t.viewer, mGui.boldFont());
                    if (i == 2) drawBookmarks(t);
                }
                ImGui::EndTabItem();
            }
        }
        mSideForce = false;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::PopID();
    // the splitter
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##split", {splitterWidth, std::max(1.0f, avail.y)});
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        float x = (a.x + b.x) / 2;
        ImGui::GetWindowDrawList()->AddLine({x, a.y}, {x, b.y}, ImGui::GetColorU32(hot ? ImGuiCol_SeparatorActive : ImGuiCol_Separator), hot ? 2 * sc : sc);
    }
    if (ImGui::IsItemActive()) mSettings.sidebarWidth = std::max(150.0f, (width + ImGui::GetIO().MouseDelta.x) / sc);
}

void App::drawPageMenu(Tab& t, int page)
{
    Viewer& v = *t.viewer;
    ImGui::SeparatorText(("Page " + v.pageLabel(page)).c_str());
    if (v.hasSelection() && ImGui::MenuItemEx("Copy", Hd::Icon::copy)) v.copy();
    if (ImGui::MenuItemEx("Add Bookmark", Icon::bookmark)) addBookmark(t, {page, -1});
    if (ImGui::MenuItemEx("Fit Width", Hd::Icon::fit)) v.fitWidthAtPage(page);
    menuItem("bookmark.prev", Icon::up);
    menuItem("bookmark.next", Icon::down);
}

void App::addBookmark(Tab& t, pdfview::Dest dest)
{
    if (dest.page < 0 || dest.page >= t.viewer->pageCount()) return;
    auto& bookmarks = mSettings.files[t.path].bookmarks;
    bookmarks.push_back({dest.page, dest.y, "Page " + t.viewer->pageLabel(dest.page)});
    std::stable_sort(bookmarks.begin(), bookmarks.end(), [](const Bookmark& a, const Bookmark& b) {
        return a.page != b.page ? a.page < b.page : a.y < b.y;
    });
    mSettings.sidebar = true;
    mSettings.sidebarTab = 2;
    mSideForce = true;
}

const Bookmark* App::adjacentBookmark(const Tab& t, int dir) const
{
    auto file = mSettings.files.find(t.path);
    if (file == mSettings.files.end() || t.viewer->state() != Viewer::State::Ready) return nullptr;
    auto here = t.viewer->position();
    // Jumps can land just above the bookmark or be clamped by the end of the document.
    if (t.bookmarkJump.target.valid() && here.page == t.bookmarkJump.landing.page && here.y == t.bookmarkJump.landing.y)
        here = t.bookmarkJump.target;
    auto location = [](int page, double y) { return std::pair{page, std::max(y, 0.0)}; };
    const auto current = location(here.page, here.y);
    const Bookmark* best = nullptr;
    for (const auto& b : file->second.bookmarks) {
        if (b.page < 0 || b.page >= t.viewer->pageCount()) continue;
        auto at = location(b.page, b.y);
        if (dir > 0 && at > current) return &b;
        if (dir < 0 && at < current) best = &b;
    }
    return best;
}

void App::openBookmark(Tab& t, const Bookmark& bookmark)
{
    pdfview::Dest dest{bookmark.page, bookmark.y};
    t.viewer->goTo(dest);
    t.bookmarkJump = {dest, t.viewer->position()};
}

void App::drawBookmarks(Tab& t)
{
    Viewer& v = *t.viewer;
    auto& bms = mSettings.files[t.path].bookmarks;
    std::string keys = mActions.keysOf("bookmark.add");
    if (ImGui::Button("Add Bookmark")) mActions.trigger("bookmark.add");
    if (!keys.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", keys.c_str());
    }
    if (bms.empty()) {
        ImGui::TextDisabled("No bookmarks in this document.");
        return;
    }
    int remove = -1;
    if (ImGui::BeginTable("##bms", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter, ImGui::GetContentRegionAvail())) {
        mBookmarkScroll.update(mSettings.animations);
        ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("page", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00000").x);
        for (int i = 0; i < static_cast<int>(bms.size()); i++) {
            Bookmark& b = bms[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (mRenaming == i) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##name", mRenameBuf, sizeof mRenameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    b.title = mRenameBuf;
                    mRenaming = -1;
                }
                if (ImGui::IsItemDeactivated()) mRenaming = -1;
            } else {
                if (ImGui::Selectable(b.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        mRenaming = i;
                        snprintf(mRenameBuf, sizeof mRenameBuf, "%s", b.title.c_str());
                    } else
                        openBookmark(t, b);
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Rename")) {
                        mRenaming = i;
                        snprintf(mRenameBuf, sizeof mRenameBuf, "%s", b.title.c_str());
                    }
                    if (ImGui::MenuItem("Remove")) remove = i;
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete)) remove = i;
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", v.pageLabel(b.page).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) bms.erase(bms.begin() + remove);
}

void App::drawLocked(Tab& t)
{
    ImGui::Spacing();
    ImGui::TextUnformatted("This PDF is password protected.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("Password", t.password, sizeof t.password, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Open") || enter) start(t, t.password);
}

void App::drawDialogs()
{
    if (mThemeEditor.open) {
        // the editor works on a real theme's key: what "system" stands for
        std::string key = mSettings.theme == "system" ? (mGui.lightTheme() ? "light" : "dark") : mSettings.theme;
        std::string before = key;
        mThemeEditor.gui(key, [this] { mGui.refreshStyle(); }, [this, &key] { mGui.setTheme(key); });
        if (key != before) {
            mSettings.theme = key;
            mGui.setTheme(key);
            saveSettings();
        }
    }
    drawFontsWindow();
    if (mShortcutsOpen) {
        ImGui::SetNextWindowSize({720 * mGui.scale(), 560 * mGui.scale()}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Configure Keyboard Shortcuts", &mShortcutsOpen, ImGuiWindowFlags_NoCollapse)) {
            if (mShortcutEditor.draw(mActions)) saveSettings();
        }
        ImGui::End();
        if (!mShortcutsOpen) {
            mShortcutEditor.cancel();
            mActions.capturing = false;
        }
    }

    if (!mAskUri.empty()) ImGui::OpenPopup("Open link?");
    if (ImGui::BeginPopupModal("Open link?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool safe = safeUri(mAskUri);
        ImGui::TextUnformatted(safe ? "The document links to:" : "The document links to something that isn't a web or mail address:");
        ImGui::TextUnformatted(mAskUri.c_str());
        ImGui::Spacing();
        if (safe && ImGui::Button("Open")) {
            Hd::Platform::openUri(mAskUri);
            mAskUri.clear();
            ImGui::CloseCurrentPopup();
        }
        if (safe) ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            ImGui::SetClipboardText(mAskUri.c_str());
            mAskUri.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            mAskUri.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
