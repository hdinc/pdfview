#include <pdfview/RenderWorker.h>

#include <algorithm>

namespace pdfview {

RenderWorker::RenderWorker(std::string path, int tileSize, std::function<void()> wake)
    : mPath(std::move(path))
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

bool RenderWorker::busy()
{
    std::lock_guard lk(mMutex);
    if (mRendering) return true;
    for (const auto& k : mWanted)
        if (!mDone.contains(k)) return true;
    return false;
}

bool RenderWorker::render(const TileKey& k, Bitmap& out, std::string& err)
{
    if (k.page < 0 || k.page >= mDoc->pages()) {
        err = "no such page";
        return false;
    }
    if (k.level == TileKey::kPreview) return mDoc->render(k.page, previewScale(mDoc->pageSize(k.page)), out, err);
    return mDoc->render(k.page, scaleOf(k.level), k.tx * mTileSize, k.ty * mTileSize, mTileSize, mTileSize, out, err);
}

void RenderWorker::run()
{
    std::string err;
    mDoc = Document::open(mPath, err);
    if (!mDoc) {
        mError = err;
        mState = State::Failed;
        if (mWake) mWake();
        return;
    }
    mSizes = mDoc->pageSizes();
    mTitle = mDoc->title();
    mState = State::Ready; // publishes mSizes and mTitle
    if (mWake) mWake();

    for (;;) {
        TileKey key;
        {
            std::unique_lock lk(mMutex);
            mRendering = false;
            const TileKey* next = nullptr;
            mCv.wait(lk, [&] {
                if (mStop) return true;
                for (const auto& k : mWanted)
                    if (!mDone.contains(k)) {
                        next = &k;
                        return true;
                    }
                return false;
            });
            if (mStop) return;
            key = *next;
            mDone.insert(key);
            mRendering = true;
        }
        Result r;
        r.key = key;
        if (!render(key, r.bitmap, r.error)) r.bitmap = {};
        {
            std::lock_guard lk(mMutex);
            mResults.push_back(std::move(r));
        }
        if (mWake) mWake();
    }
}

}
