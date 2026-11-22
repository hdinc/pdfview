#pragma once

#include <array>
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

// opens a document and works on it on its own thread: renders tiles, reads
// pages' links and text, and searches. The owner says what it wants, in
// priority order, as often as the view changes, and takes what is done.
class RenderWorker {
public:
    struct Result {
        TileKey key;
        Bitmap bitmap; // empty when rendering failed
        std::string error;
    };
    struct PageInfo {
        int page = 0;
        std::vector<Link> links;
        TextLayout text;
    };
    struct FindResult {
        unsigned search = 0; // which find() call
        int page = 0;
        std::vector<Rect> matches;
    };

    // starts loading path; wake is called from the worker thread when the
    // document is loaded and after each piece of work, to wake an idle UI
    // loop
    RenderWorker(std::string path, int tileSize, std::function<void()> wake = {}, std::string password = {});
    ~RenderWorker();
    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    enum class State { Loading, Ready, Failed };
    State state() const { return mState.load(); }
    // valid once Ready
    const std::vector<PageSize>& pageSizes() const { return mSizes; }
    const std::vector<std::string>& pageLabels() const { return mLabels; }
    const std::vector<OutlineItem>& outline() const { return mOutline; }
    const std::string& title() const { return mTitle; }
    // set when Failed ("locked" for a password protected file)
    const std::string& error() const { return mError; }

    // replaces the tiles wanted; tiles already rendered and not yet taken
    // are skipped
    void want(std::vector<TileKey> keys);
    // the tiles finished since the last call
    std::vector<Result> take();
    // Cancels tile requests and discards results that have not been taken.
    void cancelTiles();

    // replaces the pages whose links and text are wanted; they go before
    // tiles
    void wantInfo(std::vector<int> pages);
    std::vector<PageInfo> takeInfo();

    enum class FindChannel { Document, Thumbnails };
    // Searches every page, starting at from. An empty query stops that channel.
    unsigned find(std::string utf8, unsigned flags, int from, FindChannel channel = FindChannel::Document);
    // One result per page searched on this channel, including pages without matches.
    std::vector<FindResult> takeFind(FindChannel channel = FindChannel::Document);

    // true while something wanted is still being worked on
    bool busy();

    int tileSize() const { return mTileSize; }

private:
    void run();
    bool render(const TileKey& k, Bitmap& out, std::string& err);

    std::string mPath;
    std::string mPassword;
    int mTileSize;
    std::function<void()> mWake;
    std::atomic<State> mState{State::Loading};
    std::vector<PageSize> mSizes;
    std::vector<std::string> mLabels;
    std::vector<OutlineItem> mOutline;
    std::string mTitle;
    std::string mError;
    std::unique_ptr<Document> mDoc; // only the worker thread uses it

    std::mutex mMutex;
    std::condition_variable mCv;
    std::vector<TileKey> mWanted;
    std::set<TileKey> mDone; // rendered, not taken yet
    std::vector<Result> mResults;
    std::vector<int> mWantedInfo;
    std::set<int> mInfoDone; // read, not taken yet
    std::vector<PageInfo> mInfo;
    struct Search {
        unsigned generation = 0;
        std::string query;
        unsigned flags = 0;
        int nextPage = 0;
        int pagesLeft = 0;
        std::vector<FindResult> results;
    };
    std::array<Search, 2> mSearches;
    bool mBusy = false;
    bool mStop = false;
    std::thread mThread;
};

}
