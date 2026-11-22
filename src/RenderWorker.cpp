#include <pdfview/RenderWorker.h>

#include <algorithm>
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

unsigned RenderWorker::find(std::string utf8, unsigned flags, int from)
{
    unsigned id;
    {
        std::lock_guard lk(mMutex);
        id = ++mSearch;
        mQuery = std::move(utf8);
        mFlags = flags;
        mFound.clear();
        int n = static_cast<int>(mSizes.size());
        mFindLeft = mQuery.empty() || mState != State::Ready ? 0 : n;
        mFindNext = n > 0 ? std::clamp(from, 0, n - 1) : 0;
    }
    mCv.notify_all();
    return id;
}

std::vector<RenderWorker::FindResult> RenderWorker::takeFind()
{
    std::lock_guard lk(mMutex);
    std::vector<FindResult> out = std::move(mFound);
    mFound.clear();
    return out;
}

bool RenderWorker::busy()
{
    std::lock_guard lk(mMutex);
    if (mBusy || mFindLeft > 0) return true;
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
        if (!mQuery.empty()) mFindLeft = mDoc->pages();
    }
    if (mWake) mWake();

    struct FindJob {
        unsigned search;
        int page;
        std::string query;
        unsigned flags;
    };
    bool lastWasTile = false;
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
                if (mFindLeft > 0 && (!tile || lastWasTile)) {
                    job = FindJob{mSearch, mFindNext, mQuery, mFlags};
                    return true;
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
            if (std::holds_alternative<FindJob>(job)) {
                mFindNext = (mFindNext + 1) % mDoc->pages();
                mFindLeft--;
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
            // a newer search started meanwhile: drop it
            if (r.search == mSearch) mFound.push_back(std::move(r));
        }
        if (mWake) mWake();
    }
}

}
