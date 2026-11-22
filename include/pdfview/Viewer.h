#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>

#include <imgui.h>

#include <pdfview/Actions.h>
#include <pdfview/History.h>
#include <pdfview/Layout.h>
#include <pdfview/RenderWorker.h>

namespace pdfview {

// a PDF viewer widget for Dear ImGui with the OpenGL 3 backend: pages in
// rows (single, facing, overview; continuous or a row at a time), zoom
// about the cursor, tiles rendered on a worker thread, text selection,
// links, find and back/forward history. Use it from the thread that owns
// the GL context, with that context current (the destructor frees
// textures too).
class Viewer {
public:
    struct Options {
        int tileSize = 512; // pixels
        size_t textureBudget = 384u << 20; // bytes of tiles kept on the GPU
        int uploadsPerFrame = 6;
        bool toolbar = true;
        // handle keys itself while focused, with its own Actions; a host
        // with its own registry turns it off and calls registerActions
        bool shortcuts = true;
        // around the pages; 0: a shade of the ImGui theme's window colour
        ImU32 background = 0;
        // a link to a URI was clicked; without it such links do nothing.
        // The host decides whether and how to open it.
        std::function<void(const std::string&)> onOpenUri;
        // Draws menu items for the right-clicked page. An empty callback uses the viewer's menu.
        std::function<void(Viewer&, int page)> pageMenu;
    };

    Viewer();
    explicit Viewer(Options o);
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    Options& options() { return mOpt; }

    // opens a file (read only); loading continues in the background, see
    // state(). Replaces what was open.
    void open(const std::string& path, const std::string& password = {});
    void close();
    // frees the GPU tiles and stops rendering, keeping the document and the
    // view; for a viewer that isn't shown (a background tab). The next
    // draw() asks for its tiles again.
    void releaseTextures();
    // Stops tile rendering and keeps the last displayed viewport's tiles and thumbnails.
    void suspendRendering();
    // Limit the combined texture cache of distinct viewers that are inactive this frame.
    static void trimInactiveCaches(std::span<Viewer* const> inactive, size_t budget);

    enum class State { Empty, Loading, Ready, Failed };
    State state() const;
    const std::string& path() const { return mPath; }
    // why opening failed ("locked": password protected)
    std::string error() const;
    std::string title() const;

    // --- document
    int pageCount() const { return mLayout.count(); }
    PageSize pageSize(int page) const { return mLayout.pages[static_cast<size_t>(page)]; }
    std::string pageLabel(int page) const;
    // pages whose label differs from their number
    bool hasPageLabels() const;
    // the page with this label (or number), -1 when none
    int pageForLabel(const std::string& label) const;
    const std::vector<OutlineItem>& outline() const;

    // --- view
    int currentPage() const; // 0 based
    double zoom() const { return mView.zoom; }
    void setZoom(double zoom); // about the view's centre
    void zoomIn() { setZoom(mView.zoom * 1.25); }
    void zoomOut() { setZoom(mView.zoom / 1.25); }
    // fits keep fitting as the view or the mode changes, until the zoom is
    // set by hand
    enum class ZoomMode { Free, FitWidth, FitPage };
    ZoomMode zoomMode() const { return mZoomMode; }
    void fitWidth();
    // Fit the selected page or spread, restoring the normal layout from Overview.
    void fitWidthAtPage(int page);
    void fitPage();
    ViewMode viewMode() const { return mMode; }
    void setViewMode(ViewMode m);
    // overview's column count
    int overviewColumns() const { return mOverviewColumns; }
    void setOverviewColumns(int n);

    // --- navigation. Jumps (goTo, goToPage, first, last, links, outline)
    // are recorded for back(); scrolling isn't.
    void goTo(Dest d, bool record = true);
    void goToPage(int page, bool record = true);
    void nextPage();
    void prevPage();
    void first();
    void last();
    void scrollBy(double dx, double dy); // screen pixels
    void scrollPage(int dir); // a screenful down (1) or up (-1), animated in continuous view
    bool canBack() const { return !mHistory.backs.empty(); }
    bool canForward() const { return !mHistory.forwards.empty(); }
    void back();
    void forward();
    // where the view is: the page at the top and the y on it, for saving
    // and restoring
    Dest position() const;
    // puts the view there with that zoom once the document is laid out (now
    // when it is)
    void restoreView(Dest pos, ZoomMode mode, double zoom);

    // --- tools and selection
    enum class Tool { Browse, Text };
    Tool tool() const { return mTool; }
    void setTool(Tool t) { mTool = t; }
    bool hasSelection() const;
    void clearSelection();
    void selectAll(); // the current page's text
    // copies the selection to the clipboard (waits for pages' text to
    // load when needed)
    void copy();

    // --- find
    // searches the document from the current page; empty text stops
    void find(const std::string& text, unsigned flags = FindDefault);
    void findNext();
    void findPrev();
    const std::string& findText() const { return mFind.text; }
    unsigned findFlags() const { return mFind.flags; }
    int matchCount() const { return mFind.total; }
    int currentMatch() const; // 0 based, -1: none
    bool searching() const { return mFind.active && mFind.pagesDone < pageCount(); }
    bool pageHasMatches(int page) const;
    // a find bar: text, previous/next, options, count; place it anywhere
    void drawFindBar();
    // full-width layout for a search row below the document
    void drawFindFooter();
    void showFindBar(); // and focus its text
    bool findBarShown() const { return mFindBar; }
    void hideFindBar();

    // --- toolbar pieces, for hosts with their own toolbar
    void drawPageBox(); // "[12] / 300"
    void drawZoomBox(); // "- 100% +"
    void drawZoomCombo(); // "100%", with presets and fits
    void focusPageBox() { mFocusPageBox = true; }

    // --- for panels
    // the page's small preview, asking for it (0 until it is ready); call
    // each frame while it is shown
    ImTextureID previewTexture(int page, int& w, int& h);
    // call before draw() to request a whole-page thumbnail in framebuffer
    // pixels. Returns a cached thumbnail or preview while the render is pending.
    ImTextureID pageTexture(int page, double pixelWidth, int& w, int& h);
    // the part of the page in view, in page points; false when none
    bool visibleRect(int page, Rect& r) const;
    // moves the view so page point (px, py) is at its centre
    void centerOn(int page, double px, double py);

    // draws the viewer filling size (0 = the rest of the window) inside the
    // current window, and handles its input
    void draw(const char* id, ImVec2 size = {0, 0});

    // the host should keep drawing frames (a zoom settling, textures left to
    // upload); otherwise it may wait for events
    bool needsFrames() const;
    // called from the worker thread when a tile is ready; hosts that wait
    // for events post an empty event here (glfwPostEmptyEvent). Set it
    // before open(): the worker thread reads it.
    void setWake(std::function<void()> wake) { mWake = std::move(wake); }

    // adds the viewer's actions (scrolling, zoom, view modes, find, ...) to
    // a registry; they act on current() when it isn't null
    static void registerActions(Actions& a, std::function<Viewer*()> current);
    // its own registry, used when Options::shortcuts is on
    Actions& actions();

private:
    friend class OutlinePanel;
    friend class ThumbnailPanel;

    struct Tex {
        unsigned id = 0; // 0: rendering failed, don't ask again
        int w = 0, h = 0;
        int used = -1; // ImGui frame when requested or drawn
        int drawnInPanel = -1;
        int drawnInDocument = -1;

        size_t bytes() const { return static_cast<size_t>(w) * h * 4; }
    };
    using TileCache = std::map<TileKey, Tex>;
    struct Caret {
        int page = -1;
        int pos = 0; // before character pos of the page's text

        bool valid() const { return page >= 0; }
        auto operator<=>(const Caret&) const = default;
    };
    struct Info {
        std::vector<Link> links;
        TextLayout text;
        uint64_t used = 0;
    };

    void startLayout();
    void relayout(ViewMode m);
    void leaveOverview();
    void applyZoomMode();
    void handleMouse(ImVec2 origin, bool hovered, bool active);
    void drawPageMenu();
    void updatePageScroll();
    void updateAutoscroll();
    void drawAutoscrollMarker(ImDrawList* dl, ImVec2 origin, ImVec2 size) const;
    void drawToolbar();
    void drawPages(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawOverlays(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawScrollbar(ImVec2 pos, ImVec2 size);
    void uploadResults();
    void cancelTileRequests();
    TileCache::iterator eraseTexture(TileCache::iterator it);
    void requestTiles();
    void takeInfo();
    void takeFind();
    void editThumbnailFind();
    void updateThumbnailFind();
    void evict();
    void zoomAround(double zoom, double sx, double sy);
    double fbScale() const;
    int targetLevel() const;
    static void freeTex(Tex& t);
    ImU32 background() const;
    // screen point to the page under it and the point on it; false off pages
    bool pageAtScreen(double sx, double sy, int& page, double& px, double& py, bool nearest = false) const;
    ImVec2 toScreen(int page, double px, double py) const;
    const Info* info(int page);
    Caret caretAt(int page, double px, double py);
    std::string selectionText(bool& complete);
    void selectWord(Caret c, bool line);
    int linkAt(int page, double px, double py);
    void follow(const Link& l);
    void showMatch();
    void startFind(std::string text, unsigned flags, int from, int direction);
    void startFindFromBar(unsigned flags, int direction);
    void setFindBarFlags(unsigned flags);
    bool drawFindInput(float width);
    void findStatus(char* buffer, size_t size) const;
    History::Entry here() const;
    void jumpTo(History::Entry e);

    Options mOpt;
    std::string mPath;
    std::unique_ptr<RenderWorker> mWorker;
    std::function<void()> mWake;
    std::unique_ptr<Actions> mActions;
    bool mLaidOut = false;
    Layout mLayout;
    View mView;
    ViewMode mMode;
    int mOverviewColumns = 4;
    ZoomMode mZoomMode = ZoomMode::FitWidth;
    TileCache mTiles;
    std::deque<RenderWorker::Result> mPending; // taken, not uploaded yet
    size_t mBytes = 0;
    int mLastDocumentFrame = -1;
    uint64_t mFrame = 0;
    std::vector<TileKey> mLastWanted;
    std::pair<int, int> mPreviews{0, -1}; // asked for by a panel this frame
    std::pair<int, int> mLastPreviews{0, -1};
    std::vector<TileKey> mThumbnailRequests;
    // tiles follow the zoom once it has stopped changing for a moment
    int mLevel = 0;
    std::chrono::steady_clock::time_point mZoomChanged;
    bool mSettling = false;
    // fractional wheel delta at the edge of a non-continuous row
    double mFlipPush = 0;
    std::optional<double> mPageScrollTarget; // vertical position in document units
    double mPageScrollStart = 0;
    double mPageScrollElapsed = 0;
    bool mAutoscrolling = false;
    int mMenuPage = -1;
    ImVec2 mAutoscrollOrigin{};

    // pages' links and text, read on the worker as pages come into view
    std::map<int, Info> mInfo;
    std::vector<int> mLastInfoWanted;

    History mHistory;
    // The clicked heading remains current at the position its jump reached.
    struct {
        const OutlineItem* item = nullptr;
        double y = 0;
    } mOutlinePin;
    Tool mTool = Tool::Browse;
    // the mouse press being handled
    struct Press {
        bool on = false;
        ImGuiMouseButton button = 0;
        ImVec2 at;
        bool dragged = false;
        bool selecting = false;
    } mPress;
    Caret mSelA, mSelB; // the selection, either way round
    bool mCopyPending = false;
    int mHoverPage = -1; // the link under the mouse: page and index
    int mHoverLink = -1;

    struct Find {
        std::string text;
        unsigned flags = 0;
        bool active = false;
        unsigned search = 0;
        int from = 0;
        int pagesDone = 0;
        std::map<int, std::vector<Rect>> matches;
        int total = 0;
        int page = -1, index = -1; // the current match
        bool jump = false; // go to the first match found
        int dir = 1; // of findNext/Prev waiting for results
    } mFind;
    struct {
        char input[128] = "";
        double startAt = -1;
        std::optional<unsigned> generation;
        int pagesLeft = 0;
        std::vector<int> pendingPages;
        std::vector<int> pages;
        bool filtered = false;
        uint64_t publishedOn = 0;
    } mThumbnailFind;
    bool mFindBar = false;
    bool mFocusFind = false;
    bool mFindFromCurrentPage = true;
    bool mFindAsYouType = true;
    char mFindInput[256] = "";

    char mPageInput[32] = "";
    bool mEditingPage = false;
    bool mFocusPageBox = false;
    float mGrab = 0; // where the scrollbar thumb was taken
    ImVec2 mOrigin{}; // the canvas' top left on screen
    ViewMode mPrevMode; // before overview, to toggle back
    std::set<int> mInfoNeed; // pages whose info is needed beyond the view
    struct Restore {
        bool on = false;
        Dest pos;
        ZoomMode mode = ZoomMode::FitWidth;
        double zoom = 1;
    } mRestore;
};

}
