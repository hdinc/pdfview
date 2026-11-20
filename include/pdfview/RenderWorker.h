#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <pdfview/Document.h>
#include <pdfview/Layout.h>

namespace pdfview {

// opens a document and renders tiles of it on its own thread. The owner
// says what it wants, in priority order, as often as the view changes, and
// takes the finished tiles.
class RenderWorker {
public:
    struct Result {
        TileKey key;
        Bitmap bitmap; // empty when rendering failed
        std::string error;
    };

    // starts loading path; wake is called from the worker thread when the
    // document is loaded and after each tile, to wake an idle UI loop
    RenderWorker(std::string path, int tileSize, std::function<void()> wake = {});
    ~RenderWorker();
    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    enum class State { Loading, Ready, Failed };
    State state() const { return mState.load(); }
    // valid once Ready
    const std::vector<PageSize>& pageSizes() const { return mSizes; }
    const std::string& title() const { return mTitle; }
    // set when Failed ("locked" for a password protected file)
    const std::string& error() const { return mError; }

    // replaces what is wanted; tiles already rendered and not yet taken are
    // skipped
    void want(std::vector<TileKey> keys);
    // the tiles finished since the last call
    std::vector<Result> take();
    // true while something wanted is still being rendered
    bool busy();

    int tileSize() const { return mTileSize; }

private:
    void run();
    bool render(const TileKey& k, Bitmap& out, std::string& err);

    std::string mPath;
    int mTileSize;
    std::function<void()> mWake;
    std::atomic<State> mState{State::Loading};
    std::vector<PageSize> mSizes;
    std::string mTitle;
    std::string mError;
    std::unique_ptr<Document> mDoc; // only the worker thread uses it

    std::mutex mMutex;
    std::condition_variable mCv;
    std::vector<TileKey> mWanted;
    std::set<TileKey> mDone; // rendered, not taken yet
    std::vector<Result> mResults;
    bool mRendering = false;
    bool mStop = false;
    std::thread mThread;
};

}
