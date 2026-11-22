#include <pdfview/RenderWorker.h>

#include <algorithm>
#include <utility>
#include <variant>

namespace pdfview {

RenderWorker::RenderWorker(std::string path, int tileSize, std::function<void()> wake, std::string password)
    : mPath(std::move(path))
    , mPassword(std::move(password))
    , mTileSize(tileSize)
    , mWake(std::move(wake))
{
    mThread = std::thread([this] { run(); });
}

RenderWorker::~RenderWorker()
{
    {
        std::lock_guard lk(mMutex);
        mStop = true;
    }
    mCv.notify_all();
    mThread.join();
}

void RenderWorker::want(std::vector<TileKey> keys)
{
    {
        std::lock_guard lk(mMutex);
        mWanted = std::move(keys);
    }
    mCv.notify_all();
}

std::vector<RenderWorker::Result> RenderWorker::take()
{
    std::lock_guard lk(mMutex);
    std::vector<Result> out = std::move(mResults);
    mResults.clear();
    // the owner has these now: neither render them again nor count them as
    // still wanted
    for (const auto& r : out) {
        mDone.erase(r.key);
        std::erase(mWanted, r.key);
    }
    return out;
}

void RenderWorker::cancelTiles()
{
    std::lock_guard lk(mMutex);
    mWanted.clear();
    mDone.clear();
    mResults.clear();
}

void RenderWorker::wantInfo(std::vector<int> pages)
{
    {
        std::lock_guard lk(mMutex);
        mWantedInfo = std::move(pages);
    }
    mCv.notify_all();
}

std::vector<RenderWorker::PageInfo> RenderWorker::takeInfo()
{
    std::lock_guard lk(mMutex);
    std::vector<PageInfo> out = std::move(mInfo);
    mInfo.clear();
    for (const auto& r : out) {
        mInfoDone.erase(r.page);
        std::erase(mWantedInfo, r.page);
    }
    return out;
}

unsigned RenderWorker::find(std::string utf8, unsigned flags, int from, FindChannel channel)
{
    unsigned id;
    {
        std::lock_guard lk(mMutex);
        Search& search = mSearches[static_cast<size_t>(channel)];
        id = ++search.generation;
        search.query = std::move(utf8);
        search.flags = flags;
        search.results.clear();
        int n = mState == State::Ready ? static_cast<int>(mSizes.size()) : 0;
        search.pagesLeft = search.query.empty() ? 0 : n;
        search.nextPage = n > 0 ? std::clamp(from, 0, n - 1) : 0;
    }
    mCv.notify_all();
    return id;
}

std::vector<RenderWorker::FindResult> RenderWorker::takeFind(FindChannel channel)
{
    std::lock_guard lk(mMutex);
    return std::exchange(mSearches[static_cast<size_t>(channel)].results, {});
}

bool RenderWorker::busy()
{
    std::lock_guard lk(mMutex);
    if (mBusy || std::any_of(mSearches.begin(), mSearches.end(), [](const Search& s) { return s.pagesLeft > 0; })) return true;
    for (const auto& k : mWanted)
        if (!mDone.contains(k)) return true;
    for (int p : mWantedInfo)
        if (!mInfoDone.contains(p)) return true;
    return false;
}

bool RenderWorker::render(const TileKey& k, Bitmap& out, std::string& err)
{
    if (k.page < 0 || k.page >= mDoc->pages()) {
        err = "no such page";
        return false;
    }
    if (k.level == TileKey::kPreview) return mDoc->render(k.page, previewScale(mDoc->pageSize(k.page)), out, err);
    if (k.tx == TileKey::kWholePage) return mDoc->render(k.page, scaleOf(k.level), out, err);
    return mDoc->render(k.page, scaleOf(k.level), k.tx * mTileSize, k.ty * mTileSize, mTileSize, mTileSize, out, err);
}

void RenderWorker::run()
{
    std::string err;
    mDoc = Document::open(mPath, err, mPassword);
    if (!mDoc) {
        mError = err;
        mState = State::Failed;
        if (mWake) mWake();
        return;
    }
    mSizes = mDoc->pageSizes();
    mLabels.resize(mSizes.size());
    for (int i = 0; i < mDoc->pages(); i++)
        mLabels[static_cast<size_t>(i)] = mDoc->pageLabel(i);
    mOutline = mDoc->outline();
    mTitle = mDoc->title();
    {
        // find() before Ready couldn't know the page count
        std::lock_guard lk(mMutex);
        mState = State::Ready; // publishes the fields above
        for (auto& search : mSearches)
            if (!search.query.empty()) search.pagesLeft = mDoc->pages();
    }
    if (mWake) mWake();

    struct FindJob {
        size_t channel;
        unsigned search;
        int page;
        std::string query;
        unsigned flags;
    };
    bool lastWasTile = false;
    size_t nextSearch = 0;
    for (;;) {
        std::variant<std::monostate, TileKey, int, FindJob> job;
        {
            std::unique_lock lk(mMutex);
            mBusy = false;
            mCv.wait(lk, [&] {
                if (mStop) return true;
                // page info first: it is small and the view needs it to
                // react to the mouse
                for (int p : mWantedInfo)
                    if (!mInfoDone.contains(p)) {
                        job = p;
                        return true;
                    }
                const TileKey* tile = nullptr;
                for (const auto& k : mWanted)
                    if (!mDone.contains(k)) {
                        tile = &k;
                        break;
                    }
                // tiles and search pages take turns
                if (!tile || lastWasTile) {
                    for (size_t i = 0; i < mSearches.size(); ++i) {
                        size_t channel = (nextSearch + i) % mSearches.size();
                        const Search& search = mSearches[channel];
                        if (search.pagesLeft <= 0) continue;
                        job = FindJob{channel, search.generation, search.nextPage, search.query, search.flags};
                        return true;
                    }
                }
                if (tile) {
                    job = *tile;
                    return true;
                }
                return false;
            });
            if (mStop) return;
            if (auto* p = std::get_if<int>(&job)) mInfoDone.insert(*p);
            if (auto* k = std::get_if<TileKey>(&job)) mDone.insert(*k);
            if (auto* f = std::get_if<FindJob>(&job)) {
                Search& search = mSearches[f->channel];
                search.nextPage = (search.nextPage + 1) % mDoc->pages();
                --search.pagesLeft;
                nextSearch = (f->channel + 1) % mSearches.size();
            }
            lastWasTile = std::holds_alternative<TileKey>(job);
            mBusy = true;
        }
        if (auto* k = std::get_if<TileKey>(&job)) {
            Result r;
            r.key = *k;
            if (!render(*k, r.bitmap, r.error)) r.bitmap = {};
            std::lock_guard lk(mMutex);
            if (mDone.contains(*k)) mResults.push_back(std::move(r));
        } else if (auto* p = std::get_if<int>(&job)) {
            PageInfo info;
            info.page = *p;
            if (*p >= 0 && *p < mDoc->pages()) {
                info.links = mDoc->links(*p);
                info.text = mDoc->text(*p);
            }
            std::lock_guard lk(mMutex);
            mInfo.push_back(std::move(info));
        } else if (auto* f = std::get_if<FindJob>(&job)) {
            FindResult r;
            r.search = f->search;
            r.page = f->page;
            r.matches = mDoc->find(f->page, f->query, f->flags);
            std::lock_guard lk(mMutex);
            Search& search = mSearches[f->channel];
            if (r.search == search.generation) search.results.push_back(std::move(r));
        }
        if (mWake) mWake();
    }
}

}
