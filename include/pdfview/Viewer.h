#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <imgui.h>

#include <pdfview/Layout.h>
#include <pdfview/RenderWorker.h>

namespace pdfview {

// a PDF viewer widget for Dear ImGui with the OpenGL 3 backend: continuous
// vertical pages, zoom about the cursor, tiles rendered on a worker thread.
// Use it from the thread that owns the GL context, with that context
// current (the destructor frees textures too).
class Viewer {
public:
    struct Options {
        int tileSize = 512; // pixels
        size_t textureBudget = 384u << 20; // bytes of tiles kept on the GPU
        int uploadsPerFrame = 6;
        bool toolbar = true;
        ImU32 background = IM_COL32(40, 40, 44, 255);
    };

    Viewer();
    explicit Viewer(Options o);
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    // opens a file (read only); loading continues in the background, see
    // state(). Replaces what was open.
    void open(const std::string& path);
    void close();

    enum class State { Empty, Loading, Ready, Failed };
    State state() const;
    const std::string& path() const { return mPath; }
    // why opening failed ("locked": password protected)
    std::string error() const;
    std::string title() const;

    int pageCount() const { return mLayout.count(); }
    int currentPage() const; // 0 based
    void goToPage(int page);
    double zoom() const { return mView.zoom; }
    void setZoom(double zoom); // about the view's centre
    void fitWidth();
    void fitPage();

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

private:
    struct Tex {
        unsigned id = 0; // 0: rendering failed, don't ask again
        int w = 0, h = 0;
        uint64_t used = 0; // frame
    };

    void startLayout();
    void handleInput(ImVec2 origin, bool hovered, bool active);
    void drawToolbar();
    void drawPages(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawScrollbar(ImVec2 pos, ImVec2 size);
    void uploadResults();
    void requestTiles();
    void evict();
    void zoomAround(double zoom, double sx, double sy);
    double fbScale() const;
    int targetLevel() const;
    static void freeTex(Tex& t);

    Options mOpt;
    std::string mPath;
    std::unique_ptr<RenderWorker> mWorker;
    std::function<void()> mWake;
    bool mLaidOut = false;
    Layout mLayout;
    View mView;
    std::map<TileKey, Tex> mTiles;
    std::deque<RenderWorker::Result> mPending; // taken, not uploaded yet
    size_t mBytes = 0;
    uint64_t mFrame = 0;
    std::vector<TileKey> mLastWanted;
    // tiles follow the zoom once it has stopped changing for a moment
    int mLevel = 0;
    std::chrono::steady_clock::time_point mZoomChanged;
    bool mSettling = false;
    char mPageInput[16] = "";
    bool mEditingPage = false;
    float mGrab = 0; // where the scrollbar thumb was taken
};

}
