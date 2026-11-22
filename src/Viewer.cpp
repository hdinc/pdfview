#include <pdfview/Viewer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>

#include <glad/glad.h>

namespace pdfview {

using Clock = std::chrono::steady_clock;
static constexpr auto kSettle = std::chrono::milliseconds(150);
// the scrollbar follows the style, so it scales with the UI
static float scrollbarW() { return ImGui::GetStyle().ScrollbarSize; }
static constexpr double kWheelStep = 80; // pixels per wheel notch at scale 1

// the UI's scale (the host's DPI scale, 1 when it sets none): steps and
// "100%" follow it, so the document looks the same size on any monitor
static double uiScale()
{
    float s = ImGui::GetStyle().FontScaleDpi;
    return s > 0 ? s : 1.0;
}
static constexpr size_t kInfoCache = 200; // pages of links and text kept
static constexpr int kMaxPanelPreviews = 96;
static constexpr double kMaxThumbnailSide = 2048;
static constexpr double kThumbnailFindDelay = 0.3;

static bool isThumbnail(const TileKey& key)
{
    return key.level == TileKey::kPreview || key.tx == TileKey::kWholePage;
}

Viewer::Viewer()
    : Viewer(Options{})
{
}

Viewer::Viewer(Options o)
    : mOpt(std::move(o))
{
}

Viewer::~Viewer() { close(); }

ImU32 Viewer::background() const
{
    if (mOpt.background) return mOpt.background;
    // around the pages: a shade off the theme's window colour, darker on a
    // light theme so white pages stand out
    ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    float lum = 0.3f * c.x + 0.59f * c.y + 0.11f * c.z;
    float k = lum > 0.5f ? 0.68f : 1.0f;
    float add = lum > 0.5f ? 0.0f : 0.08f;
    return ImGui::GetColorU32({c.x * k + add, c.y * k + add, c.z * k + add, 1});
}

void Viewer::freeTex(Tex& t)
{
    if (t.id) {
        GLuint id = t.id;
        glDeleteTextures(1, &id);
        t.id = 0;
    }
}

void Viewer::open(const std::string& path, const std::string& password)
{
    close();
    mPath = path;
    // the wake function may change after open; call the current one
    mWorker = std::make_unique<RenderWorker>(
        path, mOpt.tileSize,
        [this] {
            if (mWake) mWake();
        },
        password);
}

void Viewer::cancelTileRequests()
{
    mPageScrollTarget.reset();
    mAutoscrolling = false;
    mPending.clear();
    mLastWanted.clear();
    mThumbnailRequests.clear();
    mPreviews = mLastPreviews = {0, -1};
    if (mWorker) mWorker->cancelTiles();
}

Viewer::TileCache::iterator Viewer::eraseTexture(TileCache::iterator it)
{
    mBytes -= it->second.bytes();
    freeTex(it->second);
    return mTiles.erase(it);
}

void Viewer::releaseTextures()
{
    cancelTileRequests();
    for (auto& [k, t] : mTiles)
        freeTex(t);
    mTiles.clear();
    mBytes = 0;
    mLastDocumentFrame = -1;
}

void Viewer::suspendRendering()
{
    cancelTileRequests();
    for (auto it = mTiles.begin(); it != mTiles.end();) {
        if (isThumbnail(it->first) || (it->second.id && it->second.drawnInDocument == mLastDocumentFrame)) ++it;
        else it = eraseTexture(it);
    }
}

void Viewer::trimInactiveCaches(std::span<Viewer* const> inactive, size_t budget)
{
    struct Entry {
        Viewer* viewer;
        TileCache::iterator texture;
    };
    size_t bytes = 0;
    std::vector<Entry> entries;
    for (Viewer* viewer : inactive) {
        for (auto it = viewer->mTiles.begin(); it != viewer->mTiles.end(); ++it) {
            if (it->second.id) {
                bytes += it->second.bytes();
                entries.push_back({viewer, it});
            }
        }
    }
    if (bytes <= budget) return;
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const Tex& left = a.texture->second;
        const Tex& right = b.texture->second;
        bool leftVisible = left.drawnInDocument >= 0 && left.drawnInDocument == a.viewer->mLastDocumentFrame;
        bool rightVisible = right.drawnInDocument >= 0 && right.drawnInDocument == b.viewer->mLastDocumentFrame;
        if (leftVisible != rightVisible) return leftVisible < rightVisible;
        int leftDrawn = std::max(left.drawnInPanel, left.drawnInDocument);
        int rightDrawn = std::max(right.drawnInPanel, right.drawnInDocument);
        if (leftDrawn != rightDrawn) return leftDrawn < rightDrawn;
        return left.used < right.used;
    });
    for (const auto& entry : entries) {
        bytes -= entry.texture->second.bytes();
        entry.viewer->eraseTexture(entry.texture);
        if (bytes <= budget) break;
    }
}

void Viewer::close()
{
    mOutlinePin = {};
    mWorker.reset(); // joins the thread before the textures go
    releaseTextures();
    mLaidOut = false;
    mLayout = {};
    mView = {};
    mSettling = false;
    mFlipPush = 0;
    mInfo.clear();
    mLastInfoWanted.clear();
    mInfoNeed.clear();
    mHistory.clear();
    mPress = {};
    mSelA = mSelB = {};
    mCopyPending = false;
    mFind = {};
    mThumbnailFind = {};
    mRestore = {};
    mPath.clear();
}

Viewer::State Viewer::state() const
{
    if (!mWorker) return State::Empty;
    switch (mWorker->state()) {
    case RenderWorker::State::Loading:
        return State::Loading;
    case RenderWorker::State::Failed:
        return State::Failed;
    default:
        return State::Ready;
    }
}

std::string Viewer::error() const { return state() == State::Failed ? mWorker->error() : std::string(); }

std::string Viewer::title() const { return state() == State::Ready ? mWorker->title() : std::string(); }

// --- document

std::string Viewer::pageLabel(int page) const
{
    if (state() == State::Ready && page >= 0 && page < static_cast<int>(mWorker->pageLabels().size())) return mWorker->pageLabels()[static_cast<size_t>(page)];
    return std::to_string(page + 1);
}

bool Viewer::hasPageLabels() const
{
    if (state() != State::Ready) return false;
    const auto& l = mWorker->pageLabels();
    for (size_t i = 0; i < l.size(); i++)
        if (l[i] != std::to_string(i + 1)) return true;
    return false;
}

int Viewer::pageForLabel(const std::string& label) const
{
    if (state() != State::Ready || label.empty()) return -1;
    const auto& l = mWorker->pageLabels();
    for (size_t i = 0; i < l.size(); i++)
        if (l[i] == label) return static_cast<int>(i);
    char* end = nullptr;
    long n = strtol(label.c_str(), &end, 10);
    if (end && *end == 0 && n >= 1 && n <= static_cast<long>(l.size())) return static_cast<int>(n - 1);
    return -1;
}

const std::vector<OutlineItem>& Viewer::outline() const
{
    static const std::vector<OutlineItem> none;
    return state() == State::Ready ? mWorker->outline() : none;
}

// --- view

int Viewer::currentPage() const { return mLaidOut ? mView.currentPage(mLayout) : 0; }

void Viewer::zoomAround(double zoom, double sx, double sy)
{
    mPageScrollTarget.reset();
    double before = mView.zoom;
    mView.zoomAt(zoom, sx, sy, mLayout);
    if (mView.zoom != before) {
        mZoomChanged = Clock::now();
        mSettling = true;
    }
}

void Viewer::setZoom(double zoom)
{
    mZoomMode = ZoomMode::Free;
    if (mLaidOut) zoomAround(zoom, mView.w / 2, mView.h / 2);
}

void Viewer::applyZoomMode()
{
    if (!mLaidOut || mZoomMode == ZoomMode::Free) return;
    int p = currentPage();
    if (mZoomMode == ZoomMode::FitWidth) {
        zoomAround(mView.fitWidth(mLayout, p), mView.w / 2, mView.h / 2);
    } else {
        zoomAround(mView.fitPage(mLayout, p), mView.w / 2, mView.h / 2);
        mView.goToPage(p, mLayout);
    }
}

void Viewer::fitWidth()
{
    mZoomMode = ZoomMode::FitWidth;
    applyZoomMode();
}

void Viewer::fitPage()
{
    mZoomMode = ZoomMode::FitPage;
    applyZoomMode();
}

void Viewer::setViewMode(ViewMode m)
{
    m.columns = std::max(1, m.columns);
    if (m.columns > 2) mOverviewColumns = m.columns;
    if (m == mMode) return;
    if (mMode.columns <= 2) mPrevMode = mMode;
    if (!mLaidOut) {
        mMode = m;
        return;
    }
    relayout(m);
}

void Viewer::setOverviewColumns(int n)
{
    mOverviewColumns = std::clamp(n, 3, 12);
    if (mMode.columns > 2) {
        ViewMode m = mMode;
        m.columns = mOverviewColumns;
        relayout(m);
    }
}

void Viewer::relayout(ViewMode m)
{
    mPageScrollTarget.reset();
    Dest at = position();
    mMode = m;
    mLayout.build(mWorker->pageSizes(), m);
    mView.goToPage(at.page, mLayout, at.y);
    applyZoomMode();
    mView.goToPage(at.page, mLayout, at.y);
}

void Viewer::startLayout()
{
    mLayout.build(mWorker->pageSizes(), mMode);
    // fit the first page's width, but not huge on a wide window
    double fw = mView.fitWidth(mLayout, 0);
    if (mZoomMode == ZoomMode::FitWidth && fw > 2 * uiScale()) mZoomMode = ZoomMode::Free;
    mView.zoom = mZoomMode == ZoomMode::FitWidth ? fw : mZoomMode == ZoomMode::FitPage ? mView.fitPage(mLayout, 0) : std::min(fw, 2 * uiScale());
    mView.x = 0;
    mView.y = 0;
    mView.row = 0;
    mView.clamp(mLayout);
    mLaidOut = true;
    if (mRestore.on) {
        mRestore.on = false;
        restoreView(mRestore.pos, mRestore.mode, mRestore.zoom);
    }
    mLevel = targetLevel();
    mSettling = false;
}

void Viewer::restoreView(Dest pos, ZoomMode mode, double zoom)
{
    if (!mLaidOut) {
        mRestore = {true, pos, mode, zoom};
        return;
    }
    mZoomMode = mode;
    if (mode == ZoomMode::Free)
        zoomAround(std::clamp(zoom, View::kMinZoom, View::kMaxZoom), mView.w / 2, mView.h / 2);
    else
        applyZoomMode();
    mView.goToPage(std::clamp(pos.page, 0, pageCount() - 1), mLayout, pos.y);
}

// --- navigation

History::Entry Viewer::here() const
{
    Dest d = position();
    return {d.page, d.y};
}

Dest Viewer::position() const
{
    if (!mLaidOut) return {0, -1};
    double cx = mView.x + mView.w / mView.zoom / 2;
    int p = mMode.continuous ? mLayout.pageAt(cx, mView.y + mLayout.gap) : mView.currentPage(mLayout);
    double y = mView.y - mLayout.top[static_cast<size_t>(p)];
    return {p, y < 0 ? -1 : y};
}

void Viewer::jumpTo(History::Entry e)
{
    if (!mLaidOut) return;
    mPageScrollTarget.reset();
    mView.goToPage(e.page, mLayout, e.y);
}

void Viewer::goTo(Dest d, bool record)
{
    if (!mLaidOut || d.page < 0 || d.page >= pageCount()) return;
    mPageScrollTarget.reset();
    if (record) mHistory.push(here());
    // a little above the target, so its line shows whole
    mView.goToPage(d.page, mLayout, d.y >= 0 ? std::max(0.0, d.y - 4) : -1);
}

void Viewer::goToPage(int page, bool record) { goTo({std::clamp(page, 0, std::max(0, pageCount() - 1)), -1}, record); }

void Viewer::nextPage()
{
    if (!mLaidOut) return;
    int r = mLayout.rowOf[static_cast<size_t>(currentPage())];
    if (r + 1 < mLayout.rows()) goToPage(mLayout.rowFirst[static_cast<size_t>(r + 1)], false);
}

void Viewer::prevPage()
{
    if (!mLaidOut) return;
    int r = mLayout.rowOf[static_cast<size_t>(currentPage())];
    if (r > 0) goToPage(mLayout.rowFirst[static_cast<size_t>(r - 1)], false);
}

void Viewer::first() { goToPage(0, true); }

void Viewer::last()
{
    if (!mLaidOut) return;
    mPageScrollTarget.reset();
    mHistory.push(here());
    mView.goToPage(pageCount() - 1, mLayout);
    mView.y = 1e12; // its end
    mView.clamp(mLayout);
}

void Viewer::scrollBy(double dx, double dy)
{
    if (!mLaidOut) return;
    mPageScrollTarget.reset();
    mView.x += dx / mView.zoom;
    mView.y += dy / mView.zoom;
    mView.clamp(mLayout);
}

void Viewer::scrollPage(int dir)
{
    if (!mLaidOut) return;
    double vh = mView.h / mView.zoom;
    if (mMode.continuous) {
        // Repeated presses add to the destination, even mid-scroll.
        View target = mView;
        double previousTarget = mPageScrollTarget.value_or(mView.y);
        target.y = previousTarget + dir * vh * 0.9;
        target.clamp(mLayout);
        if (target.y == mView.y) {
            mPageScrollTarget.reset();
        } else if (target.y != previousTarget) {
            mPageScrollTarget = target.y;
            mPageScrollStart = mView.y;
            mPageScrollElapsed = 0;
        }
        return;
    }
    auto [b0, b1] = mView.band(mLayout);
    if (dir > 0 && mView.y + vh >= b1 - 0.5) {
        mView.flip(1, mLayout);
        return;
    }
    if (dir < 0 && mView.y <= b0 + 0.5) {
        mView.flip(-1, mLayout);
        return;
    }
    mView.y += dir * vh * 0.9;
    mView.clamp(mLayout);
}

void Viewer::back()
{
    History::Entry to;
    if (mLaidOut && mHistory.back(here(), to)) jumpTo(to);
}

void Viewer::forward()
{
    History::Entry to;
    if (mLaidOut && mHistory.forward(here(), to)) jumpTo(to);
}

// --- geometry

ImVec2 Viewer::toScreen(int page, double px, double py) const
{
    double dx = mLayout.left(page) + px, dy = mLayout.top[static_cast<size_t>(page)] + py;
    return {static_cast<float>(mOrigin.x + (dx - mView.x) * mView.zoom), static_cast<float>(mOrigin.y + (dy - mView.y) * mView.zoom)};
}

bool Viewer::pageAtScreen(double sx, double sy, int& page, double& px, double& py, bool nearest) const
{
    if (!mLaidOut) return false;
    double dx = mView.x + sx / mView.zoom, dy = mView.y + sy / mView.zoom;
    auto [a, b] = mView.pages(mLayout, std::max(mView.w, mView.h));
    double best = 1e300;
    for (int i = a; i <= b; i++) {
        const PageSize& s = mLayout.pages[static_cast<size_t>(i)];
        double x0 = mLayout.left(i), y0 = mLayout.top[static_cast<size_t>(i)];
        double ox = std::max({0.0, x0 - dx, dx - (x0 + s.w)}), oy = std::max({0.0, y0 - dy, dy - (y0 + s.h)});
        double d = ox * ox + oy * oy;
        if (d < best) {
            best = d;
            page = i;
            px = std::clamp(dx - x0, 0.0, s.w);
            py = std::clamp(dy - y0, 0.0, s.h);
        }
    }
    return best == 0 || (nearest && best < 1e300);
}

bool Viewer::visibleRect(int page, Rect& r) const
{
    if (!mLaidOut || page < 0 || page >= pageCount()) return false;
    auto [a, b] = mView.pages(mLayout);
    if (page < a || page > b) return false;
    const PageSize& s = mLayout.pages[static_cast<size_t>(page)];
    double x0 = mLayout.left(page), y0 = mLayout.top[static_cast<size_t>(page)];
    r.x0 = std::clamp(mView.x - x0, 0.0, s.w);
    r.y0 = std::clamp(mView.y - y0, 0.0, s.h);
    r.x1 = std::clamp(mView.x + mView.w / mView.zoom - x0, 0.0, s.w);
    r.y1 = std::clamp(mView.y + mView.h / mView.zoom - y0, 0.0, s.h);
    return r.x1 > r.x0 && r.y1 > r.y0;
}

void Viewer::centerOn(int page, double px, double py)
{
    if (!mLaidOut || page < 0 || page >= pageCount()) return;
    mPageScrollTarget.reset();
    mView.row = mLayout.rowOf[static_cast<size_t>(page)];
    mView.x = mLayout.left(page) + px - mView.w / mView.zoom / 2;
    mView.y = mLayout.top[static_cast<size_t>(page)] + py - mView.h / mView.zoom / 2;
    mView.clamp(mLayout);
}

// --- page info: links and text

const Viewer::Info* Viewer::info(int page)
{
    auto it = mInfo.find(page);
    if (it == mInfo.end()) {
        mInfoNeed.insert(page);
        return nullptr;
    }
    it->second.used = mFrame;
    return &it->second;
}

void Viewer::takeInfo()
{
    for (auto& r : mWorker->takeInfo()) {
        Info& i = mInfo[r.page];
        i.links = std::move(r.links);
        i.text = std::move(r.text);
        i.used = mFrame;
    }
    // ask for the pages in view and next to it, and any others needed
    auto [a, b] = mView.pages(mLayout);
    std::vector<int> want;
    for (int p = std::max(0, a - 1); p <= std::min(pageCount() - 1, b + 1); p++) {
        if (mInfo.count(p))
            mInfo[p].used = mFrame;
        else
            want.push_back(p);
    }
    for (int p : mInfoNeed)
        if (!mInfo.count(p) && std::find(want.begin(), want.end(), p) == want.end()) want.push_back(p);
    mInfoNeed.clear();
    if (want != mLastInfoWanted) {
        mLastInfoWanted = want;
        mWorker->wantInfo(want);
    }
    if (mInfo.size() > kInfoCache) {
        std::vector<std::pair<uint64_t, int>> order;
        for (const auto& [p, i] : mInfo)
            order.push_back({i.used, p});
        std::sort(order.begin(), order.end());
        for (size_t k = 0; k < order.size() && mInfo.size() > kInfoCache * 3 / 4; k++)
            if (order[k].first < mFrame) mInfo.erase(order[k].second);
    }
}

int Viewer::linkAt(int page, double px, double py)
{
    const Info* i = info(page);
    if (!i) return -1;
    for (size_t k = 0; k < i->links.size(); k++)
        if (i->links[k].kind != Link::Kind::Other && i->links[k].rect.contains(px, py)) return static_cast<int>(k);
    return -1;
}

void Viewer::follow(const Link& l)
{
    if (l.kind == Link::Kind::Goto)
        goTo(l.dest, true);
    else if (l.kind == Link::Kind::Uri && mOpt.onOpenUri)
        mOpt.onOpenUri(l.uri);
}

// --- selection

Viewer::Caret Viewer::caretAt(int page, double px, double py)
{
    const Info* in = info(page);
    if (!in) return {};
    const TextLayout& t = in->text;
    // the character whose line is nearest, then nearest along it
    int best = -1;
    double bestV = 1e300, bestH = 1e300;
    for (size_t i = 0; i < t.text.size(); i++) {
        if (t.text[i] == U'\n') continue;
        const Rect& b = t.boxes[i];
        double v = std::max({0.0, b.y0 - py, py - b.y1}), h = std::max({0.0, b.x0 - px, px - b.x1});
        if (v < bestV - 0.5 || (v < bestV + 0.5 && h < bestH)) {
            bestV = v;
            bestH = h;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return {page, 0};
    const Rect& b = t.boxes[static_cast<size_t>(best)];
    return {page, best + (px > (b.x0 + b.x1) / 2 ? 1 : 0)};
}

bool Viewer::hasSelection() const { return mSelA.valid() && mSelB.valid() && mSelA != mSelB; }

void Viewer::clearSelection()
{
    mSelA = mSelB = {};
    mCopyPending = false;
}

void Viewer::selectAll()
{
    if (!mLaidOut) return;
    int p = currentPage();
    mSelA = {p, 0};
    mSelB = {p, INT_MAX};
}

static bool wordChar(char32_t c) { return c == U'_' || c > 127 || (c < 128 && std::isalnum(static_cast<int>(c))); }

void Viewer::selectWord(Caret c, bool line)
{
    if (!c.valid()) return;
    const Info* in = info(c.page);
    if (!in || in->text.text.empty()) return;
    const std::u32string& t = in->text.text;
    int n = static_cast<int>(t.size());
    int i = std::clamp(c.pos, 0, n - 1);
    if (c.pos > 0 && (c.pos >= n || t[static_cast<size_t>(c.pos)] == U'\n' || !wordChar(t[static_cast<size_t>(i)]))) i = std::clamp(c.pos - 1, 0, n - 1);
    int a = i, b = i + 1;
    if (line) {
        while (a > 0 && t[static_cast<size_t>(a - 1)] != U'\n')
            a--;
        while (b < n && t[static_cast<size_t>(b)] != U'\n')
            b++;
    } else if (wordChar(t[static_cast<size_t>(i)])) {
        while (a > 0 && wordChar(t[static_cast<size_t>(a - 1)]))
            a--;
        while (b < n && wordChar(t[static_cast<size_t>(b)]))
            b++;
    }
    mSelA = {c.page, a};
    mSelB = {c.page, b};
}

std::string Viewer::selectionText(bool& complete)
{
    complete = true;
    if (!hasSelection()) return {};
    Caret lo = std::min(mSelA, mSelB), hi = std::max(mSelA, mSelB);
    std::u32string out;
    for (int p = lo.page; p <= hi.page; p++) {
        const Info* in = info(p);
        if (!in) {
            complete = false;
            continue;
        }
        const std::u32string& t = in->text.text;
        size_t s = p == lo.page ? std::min(t.size(), static_cast<size_t>(lo.pos)) : 0;
        size_t e = p == hi.page ? std::min(t.size(), static_cast<size_t>(hi.pos)) : t.size();
        if (s < e) out += t.substr(s, e - s);
        if (p < hi.page && (out.empty() || out.back() != U'\n')) out += U'\n';
    }
    return toUtf8(out);
}

void Viewer::copy()
{
    bool complete = false;
    std::string s = selectionText(complete);
    if (!complete) {
        mCopyPending = true; // tried again each frame until the pages are read
        return;
    }
    mCopyPending = false;
    if (!s.empty()) ImGui::SetClipboardText(s.c_str());
}

// --- find

void Viewer::find(const std::string& text, unsigned flags)
{
    if (text == mFind.text && flags == mFind.flags && mFind.active) return;
    startFind(text, flags, currentPage(), 1);
}

void Viewer::startFind(std::string text, unsigned flags, int from, int direction)
{
    if (!mWorker || state() != State::Ready) return;
    mFind = {};
    mFind.text = std::move(text);
    mFind.flags = flags;
    if (mFind.text.empty()) {
        mWorker->find({}, flags, 0);
        return;
    }
    mFind.active = true;
    mFind.from = from;
    mFind.search = mWorker->find(mFind.text, flags, from);
    mFind.jump = true;
    mFind.dir = direction;
}

void Viewer::startFindFromBar(unsigned flags, int direction)
{
    int from = mFindFromCurrentPage ? currentPage() : direction > 0 ? 0 : std::max(0, pageCount() - 1);
    startFind(mFindInput, flags, from, direction);
}

void Viewer::setFindBarFlags(unsigned flags)
{
    if (mFindAsYouType || (mFind.active && mFind.text == mFindInput))
        startFindFromBar(flags, 1);
    else
        startFind({}, flags, 0, 1);
}

void Viewer::takeFind()
{
    for (auto& r : mWorker->takeFind()) {
        if (!mFind.active || r.search != mFind.search) continue;
        mFind.pagesDone++;
        if (r.matches.empty()) continue;
        mFind.total += static_cast<int>(r.matches.size());
        int n = static_cast<int>(r.matches.size());
        mFind.matches[r.page] = std::move(r.matches);
        if (mFind.jump && mFind.dir > 0) {
            mFind.jump = false;
            // the first match after the current one
            if (mFind.page >= 0 && r.page == mFind.page && mFind.index + 1 < n)
                mFind.index++;
            else {
                mFind.page = r.page;
                mFind.index = 0;
            }
            showMatch();
        }
    }
    if (mFind.jump && mFind.dir < 0 && !searching()) {
        mFind.jump = false;
        findPrev();
    }
}

void Viewer::editThumbnailFind()
{
    auto& filter = mThumbnailFind;
    if (filter.generation) mWorker->find({}, 0, 0, RenderWorker::FindChannel::Thumbnails);
    filter.generation.reset();
    filter.pendingPages.clear();
    if (filter.input[0]) {
        filter.startAt = ImGui::GetTime() + kThumbnailFindDelay;
    } else {
        filter.startAt = -1;
        filter.filtered = false;
        filter.pages.clear();
    }
}

void Viewer::updateThumbnailFind()
{
    auto& filter = mThumbnailFind;
    constexpr auto channel = RenderWorker::FindChannel::Thumbnails;
    if (filter.startAt >= 0 && ImGui::GetTime() >= filter.startAt) {
        filter.startAt = -1;
        filter.pagesLeft = pageCount();
        filter.generation = mWorker->find(filter.input, 0, 0, channel);
    }
    if (!filter.generation) return;
    for (const auto& result : mWorker->takeFind(channel)) {
        if (result.search != *filter.generation) continue;
        if (!result.matches.empty()) filter.pendingPages.push_back(result.page);
        --filter.pagesLeft;
    }
    if (filter.pagesLeft == 0) {
        filter.pages.swap(filter.pendingPages);
        filter.pendingPages.clear();
        filter.filtered = true;
        filter.generation.reset();
        // The sidebar may already have been drawn this frame.
        filter.publishedOn = mFrame;
    }
}

int Viewer::currentMatch() const
{
    if (mFind.page < 0) return -1;
    int n = 0;
    for (const auto& [p, m] : mFind.matches) {
        if (p == mFind.page) return n + mFind.index;
        n += static_cast<int>(m.size());
    }
    return -1;
}

bool Viewer::pageHasMatches(int page) const { return mFind.matches.count(page) > 0; }

void Viewer::findNext()
{
    if (!mFind.active || (mFindBar && !mFindAsYouType && mFind.text != mFindInput)) {
        if (mFindInput[0]) startFindFromBar(mFind.flags, 1);
        return;
    }
    auto& m = mFind.matches;
    if (mFind.page >= 0 && m.count(mFind.page) && mFind.index + 1 < static_cast<int>(m[mFind.page].size())) {
        mFind.index++;
        showMatch();
        return;
    }
    auto it = mFind.page >= 0 ? m.upper_bound(mFind.page) : m.lower_bound(currentPage());
    if (it == m.end()) {
        if (searching()) {
            mFind.jump = true;
            mFind.dir = 1;
            return;
        }
        it = m.begin();
    }
    if (it == m.end()) return;
    mFind.page = it->first;
    mFind.index = 0;
    showMatch();
}

void Viewer::findPrev()
{
    if (!mFind.active || (mFindBar && !mFindAsYouType && mFind.text != mFindInput)) {
        if (mFindInput[0]) startFindFromBar(mFind.flags, -1);
        return;
    }
    auto& m = mFind.matches;
    if (mFind.page >= 0 && m.count(mFind.page) && mFind.index > 0) {
        mFind.index--;
        showMatch();
        return;
    }
    if (m.empty() || searching()) {
        if (searching()) {
            mFind.jump = true;
            mFind.dir = -1;
        }
        return;
    }
    int from = mFind.page >= 0 ? mFind.page : mFind.from + 1;
    auto it = m.lower_bound(from);
    it = it == m.begin() ? std::prev(m.end()) : std::prev(it);
    mFind.page = it->first;
    mFind.index = static_cast<int>(it->second.size()) - 1;
    showMatch();
}

void Viewer::showMatch()
{
    auto it = mFind.matches.find(mFind.page);
    if (!mLaidOut || it == mFind.matches.end() || mFind.index < 0 || mFind.index >= static_cast<int>(it->second.size())) return;
    mPageScrollTarget.reset();
    const Rect& r = it->second[static_cast<size_t>(mFind.index)];
    int p = mFind.page;
    double vw = mView.w / mView.zoom, vh = mView.h / mView.zoom;
    double x0 = mLayout.left(p) + r.x0, x1 = mLayout.left(p) + r.x1;
    double y0 = mLayout.top[static_cast<size_t>(p)] + r.y0, y1 = mLayout.top[static_cast<size_t>(p)] + r.y1;
    bool rowOk = mMode.continuous || mView.row == mLayout.rowOf[static_cast<size_t>(p)];
    if (!rowOk || y0 < mView.y || y1 > mView.y + vh) {
        mView.row = mLayout.rowOf[static_cast<size_t>(p)];
        mView.y = y0 - vh / 3;
    }
    if (x0 < mView.x || x1 > mView.x + vw) mView.x = (x0 + x1) / 2 - vw / 2;
    mView.clamp(mLayout);
}

void Viewer::showFindBar()
{
    mFindBar = true;
    mFocusFind = true;
}

void Viewer::hideFindBar()
{
    mFindBar = false;
    find({}, mFind.flags);
}

bool Viewer::drawFindInput(float width)
{
    if (mFocusFind) {
        ImGui::SetKeyboardFocusHere();
        mFocusFind = false;
    }
    ImGui::SetNextItemWidth(width);
    bool enter = ImGui::InputTextWithHint("##text", "Find...", mFindInput, sizeof mFindInput, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    // InputText may deactivate itself when it consumes Escape.
    if ((ImGui::IsItemActive() || ImGui::IsItemDeactivated()) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        hideFindBar();
        return false;
    }
    if (ImGui::IsItemEdited()) {
        if (mFindAsYouType)
            startFindFromBar(mFind.flags, 1);
        else if (mFind.active)
            startFind({}, mFind.flags, 0, 1);
    }
    if (enter) {
        if (ImGui::GetIO().KeyShift)
            findPrev();
        else
            findNext();
        ImGui::SetKeyboardFocusHere(-1); // keep typing
    }
    return true;
}

void Viewer::findStatus(char* buffer, size_t size) const
{
    buffer[0] = 0;
    if (!mFind.active) return;
    bool pending = searching();
    if (mFind.total == 0) {
        if (pending)
            std::snprintf(buffer, size, "Searching... %d%%", mFind.pagesDone * 100 / pageCount());
        else
            std::snprintf(buffer, size, "No matches");
    } else if (int match = currentMatch(); match >= 0) {
        std::snprintf(buffer, size, "%d of %d%s", match + 1, mFind.total, pending ? "+" : "");
    } else {
        std::snprintf(buffer, size, "%d matches%s", mFind.total, pending ? "+" : "");
    }
}

void Viewer::drawFindBar()
{
    if (state() != State::Ready) return;
    ImGui::PushID("##findbar");
    if (!drawFindInput(ImGui::GetFontSize() * 16)) {
        ImGui::PopID();
        return;
    }
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) findPrev();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous match (Shift+Enter)");
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##next", ImGuiDir_Down)) findNext();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next match (Enter)");
    auto toggle = [&](const char* label, const char* tip, unsigned flag) {
        ImGui::SameLine(0, 2);
        bool on = mFind.flags & flag;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) setFindBarFlags(mFind.flags ^ flag);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    toggle("Aa", "Match case", FindCaseSensitive);
    toggle("W", "Whole words", FindWholeWords);
    ImGui::SameLine();
    char count[48];
    findStatus(count, sizeof count);
    ImGui::TextDisabled("%s", count[0] ? count : " ");
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) hideFindBar();
    ImGui::PopID();
}

void Viewer::drawFindFooter()
{
    if (state() != State::Ready) return;
    ImGui::PushID("##find-footer");
    const ImGuiStyle& style = ImGui::GetStyle();
    float frame = ImGui::GetFrameHeight();
    if (ImGui::InvisibleButton("##close", {frame, frame}, ImGuiButtonFlags_EnableNav)) {
        hideFindBar();
        ImGui::PopID();
        return;
    }
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        ImGuiCol color = ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered;
        draw->AddRectFilled(a, b, ImGui::GetColorU32(color), style.FrameRounding);
    }
    ImVec2 center{(a.x + b.x) / 2, (a.y + b.y) / 2};
    float radius = frame * 0.16f;
    float stroke = std::max(1.0f, frame / 24);
    ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 descending[] = {{center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}};
    const ImVec2 ascending[] = {{center.x - radius, center.y + radius}, {center.x + radius, center.y - radius}};
    draw->AddPolyline(descending, 2, ink, 0, stroke);
    draw->AddPolyline(ascending, 2, ink, 0, stroke);
    ImGui::SetItemTooltip("Close Find (Escape)");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Find:");
    ImGui::SameLine();

    constexpr const char* next = "    Next";
    constexpr const char* previous = "    Previous";
    float padding = 2 * style.FramePadding.x;
    float optionsWidth = ImGui::CalcTextSize("Options").x + padding + frame;
    float buttonWidth = std::max(ImGui::CalcTextSize(previous).x + padding, optionsWidth);
    float available = ImGui::GetContentRegionAvail().x;
    float gap = style.ItemSpacing.x;
    bool compact = available < 3 * buttonWidth + 3 * gap + 2 * frame;
    float navigationWidth = compact ? frame : buttonWidth;
    if (!compact) optionsWidth = buttonWidth;
    float inputWidth = available - 2 * navigationWidth - optionsWidth - 3 * gap;
    char count[48];
    findStatus(count, sizeof count);
    float countWidth = count[0] ? ImGui::CalcTextSize(count).x + gap : 0;
    bool showCount = countWidth > 0 && inputWidth - countWidth >= 3 * frame;
    if (!drawFindInput(std::max(1.0f, inputWidth - (showCount ? countWidth : 0)))) {
        ImGui::PopID();
        return;
    }
    if (showCount) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", count);
    } else if (count[0]) {
        ImGui::SetItemTooltip("%s", count);
    }

    auto matchButton = [&](const char* label, bool up) {
        ImGui::SameLine();
        if (compact)
            return ImGui::ArrowButton(label, up ? ImGuiDir_Up : ImGuiDir_Down);
        bool pressed = ImGui::Button(label, {navigationWidth, frame});
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        float font = ImGui::GetFontSize();
        float textInset = (navigationWidth - ImGui::CalcTextSize(label).x) / 2;
        float x = a.x + textInset + font * 0.35f, y = (a.y + b.y) / 2;
        float dx = font * 0.26f, dy = font * (up ? -0.14f : 0.14f);
        std::array<ImVec2, 3> points{{{x - dx, y - dy}, {x, y + dy}, {x + dx, y - dy}}};
        ImGui::GetWindowDrawList()->AddPolyline(points.data(), 3, ImGui::GetColorU32(ImGuiCol_Text), 0, std::max(1.0f, font / 11));
        return pressed;
    };
    if (matchButton(next, false)) findNext();
    ImGui::SetItemTooltip("Next match (Enter)");
    if (matchButton(previous, true)) findPrev();
    ImGui::SetItemTooltip("Previous match (Shift+Enter)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(optionsWidth);
    if (ImGui::BeginCombo("##options", "Options")) {
        bool caseSensitive = (mFind.flags & FindCaseSensitive) != 0;
        if (ImGui::Checkbox("Case sensitive", &caseSensitive))
            setFindBarFlags(mFind.flags ^ FindCaseSensitive);
        ImGui::Checkbox("From current page", &mFindFromCurrentPage);
        if (ImGui::Checkbox("Find as you type", &mFindAsYouType) && mFindAsYouType && mFind.text != mFindInput)
            startFindFromBar(mFind.flags, 1);
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

// --- previews for panels

ImTextureID Viewer::previewTexture(int page, int& w, int& h)
{
    if (!mLaidOut || page < 0 || page >= pageCount()) return 0;
    if (mPreviews.first > mPreviews.second)
        mPreviews = {page, page};
    else
        mPreviews = {std::min(mPreviews.first, page), std::max(mPreviews.second, page)};
    auto it = mTiles.find({page, TileKey::kPreview, 0, 0});
    if (it == mTiles.end() || !it->second.id) return 0;
    it->second.used = it->second.drawnInPanel = ImGui::GetFrameCount();
    w = it->second.w;
    h = it->second.h;
    return static_cast<ImTextureID>(it->second.id);
}

ImTextureID Viewer::pageTexture(int page, double pixelWidth, int& w, int& h)
{
    if (!mLaidOut || page < 0 || page >= pageCount()) return 0;
    PageSize size = pageSize(page);
    double scale = pixelWidth / size.w;
    if (!std::isfinite(scale) || scale <= previewScale(size))
        return previewTexture(page, w, h);

    double limit = kMaxThumbnailSide / std::max(size.w, size.h);
    int level = levelFor(std::min(scale, limit));
    if (scaleOf(level) > limit) --level;
    TileKey key{page, level, TileKey::kWholePage, 0};
    Tex* best = nullptr;
    int bestLevel = TileKey::kPreview;
    for (auto it = mTiles.lower_bound({page, TileKey::kPreview, 0, 0});
         it != mTiles.end() && it->first.page == page; ++it) {
        if (it->first.tx != TileKey::kWholePage || !it->second.id) continue;
        best = &it->second;
        bestLevel = it->first.level;
        if (bestLevel >= level) break;
    }
    if (bestLevel < level && !mTiles.contains(key)
        && std::find(mThumbnailRequests.begin(), mThumbnailRequests.end(), key) == mThumbnailRequests.end())
        mThumbnailRequests.push_back(key);
    if (!best) return previewTexture(page, w, h);
    best->used = best->drawnInPanel = ImGui::GetFrameCount();
    w = best->w;
    h = best->h;
    return static_cast<ImTextureID>(best->id);
}

// --- tiles

bool Viewer::needsFrames() const
{
    return mPageScrollTarget || mAutoscrolling || mSettling || !mPending.empty() || state() == State::Loading
        || mThumbnailFind.startAt >= 0 || mThumbnailFind.generation
        || (mThumbnailFind.filtered && mThumbnailFind.publishedOn == mFrame)
        || mCopyPending || (mPress.on && mPress.selecting) || (mActions && mActions->pending().n > 0);
}

double Viewer::fbScale() const
{
    ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
    return s.x > 0 ? s.x : 1.0;
}

int Viewer::targetLevel() const { return levelFor(mView.zoom * fbScale()); }

void Viewer::uploadResults()
{
    for (auto& r : mWorker->take())
        mPending.push_back(std::move(r));
    int n = 0;
    while (!mPending.empty() && n < mOpt.uploadsPerFrame) {
        RenderWorker::Result r = std::move(mPending.front());
        mPending.pop_front();
        Tex t;
        t.used = ImGui::GetFrameCount();
        if (!r.bitmap.bgra.empty()) {
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r.bitmap.w, r.bitmap.h, 0, GL_BGRA, GL_UNSIGNED_BYTE, r.bitmap.bgra.data());
            t.id = id;
            t.w = r.bitmap.w;
            t.h = r.bitmap.h;
            mBytes += t.bytes();
            n++;
        }
        auto [it, inserted] = mTiles.try_emplace(r.key, t);
        if (!inserted) { // rendered twice; keep the new one
            mBytes -= it->second.bytes();
            freeTex(it->second);
            it->second = t;
        }
    }
}

void Viewer::requestTiles()
{
    std::vector<TileKey> want;
    int frame = ImGui::GetFrameCount();
    auto add = [&](const TileKey& k) {
        auto it = mTiles.find(k);
        if (it != mTiles.end()) {
            it->second.used = frame;
            return;
        }
        // rendered, waiting for its upload
        for (const auto& r : mPending)
            if (r.key == k) return;
        if (std::find(want.begin(), want.end(), k) == want.end()) want.push_back(k);
    };
    auto [a, b] = mView.pages(mLayout);
    // previews first: cheap, and they cover the page while tiles come
    for (int i = a; i <= b; i++)
        add({i, TileKey::kPreview, 0, 0});
    // sharp tiles once the zoom has settled, unless the preview is sharp
    // enough already
    bool needTiles = a <= b && scaleOf(mLevel) > previewScale(mLayout.pages[static_cast<size_t>(std::max(a, 0))]) * 1.05;
    if (needTiles)
        for (const auto& k : visibleTiles(mLayout, mView, mLevel, mOpt.tileSize))
            add(k);
    for (const auto& k : mThumbnailRequests)
        add(k);
    mThumbnailRequests.clear();
    // a panel's previews
    auto [pa, pb] = mPreviews;
    if (pa <= pb) {
        pb = std::min(pb, pa + kMaxPanelPreviews);
        // nearest the panel's middle first
        int mid = (pa + pb) / 2;
        for (int d = 0; mid - d >= pa || mid + d <= pb; d++) {
            if (mid + d <= pb) add({mid + d, TileKey::kPreview, 0, 0});
            if (d && mid - d >= pa) add({mid - d, TileKey::kPreview, 0, 0});
        }
    }
    mLastPreviews = mPreviews;
    mPreviews = {0, -1};
    // then a little ahead: previews of nearby pages, tiles half a screen out
    int ahead = std::max(3, b - a + 1);
    for (int d = 1; d <= ahead; d++) {
        if (b + d < mLayout.count()) add({b + d, TileKey::kPreview, 0, 0});
        if (a - d >= 0) add({a - d, TileKey::kPreview, 0, 0});
    }
    if (needTiles && !mSettling)
        for (const auto& k : visibleTiles(mLayout, mView, mLevel, mOpt.tileSize, mView.h / 2))
            add(k);
    // mark what is on screen as used, for eviction
    for (int i = a; i <= b; i++)
        for (auto it = mTiles.lower_bound({i, TileKey::kPreview - 1, 0, 0}); it != mTiles.end() && it->first.page == i; ++it)
            if (it->first.tx != TileKey::kWholePage) it->second.used = frame;
    if (want != mLastWanted) {
        mLastWanted = want;
        mWorker->want(std::move(want));
    }
}

void Viewer::evict()
{
    if (mBytes <= mOpt.textureBudget) return;
    std::vector<std::pair<int, TileKey>> order;
    int frame = ImGui::GetFrameCount();
    for (const auto& [k, t] : mTiles)
        if (t.used < frame) order.push_back({t.used, k});
    std::sort(order.begin(), order.end());
    for (const auto& [used, k] : order) {
        if (mBytes <= mOpt.textureBudget * 3 / 4) break;
        eraseTexture(mTiles.find(k));
    }
}

// --- input

void Viewer::handleMouse(ImVec2 origin, bool hovered, bool active)
{
    ImGuiIO& io = ImGui::GetIO();
    bool stopClick = mAutoscrolling && (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    if (stopClick || io.MouseWheel != 0 || io.MouseWheelH != 0 || io.AppFocusLost)
        mAutoscrolling = false;
    double sx = io.MousePos.x - origin.x, sy = io.MousePos.y - origin.y;
    int page = -1;
    double px = 0, py = 0;
    bool onPage = (hovered || active) && pageAtScreen(sx, sy, page, px, py);
    mHoverPage = -1;
    mHoverLink = -1;
    if (!mAutoscrolling && !stopClick && hovered && onPage && !(mPress.on && mPress.dragged)) {
        mHoverLink = linkAt(page, px, py);
        if (mHoverLink >= 0) mHoverPage = page;
    }

    if (hovered) {
        if (io.MouseWheel != 0 || io.MouseWheelH != 0) mPageScrollTarget.reset();
        if (io.KeyCtrl && io.MouseWheel != 0) {
            mZoomMode = ZoomMode::Free;
            zoomAround(mView.zoom * std::pow(1.15, io.MouseWheel), sx, sy);
        } else if (io.MouseWheel != 0 || io.MouseWheelH != 0) {
            double dy = io.MouseWheel, dx = io.MouseWheelH;
            if (io.KeyShift) std::swap(dx, dy);
            if (!mMode.continuous && dy != 0) {
                // At the end of a row, one notch flips it. Precision wheels
                // can deliver a notch as several deltas in the same direction.
                auto [b0, b1] = mView.band(mLayout);
                double vh = mView.h / mView.zoom;
                bool atEnd = dy < 0 ? mView.y + vh >= b1 - 0.5 : mView.y <= b0 + 0.5;
                if (atEnd) {
                    if (mFlipPush * dy < 0) mFlipPush = 0;
                    mFlipPush += dy;
                    if (std::abs(mFlipPush) >= 1) {
                        mView.flip(dy < 0 ? 1 : -1, mLayout);
                        mFlipPush = 0;
                    }
                    dy = 0;
                } else
                    mFlipPush = 0;
            }
            mView.y -= dy * kWheelStep * uiScale() / mView.zoom;
            mView.x -= dx * kWheelStep * uiScale() / mView.zoom;
        }
    }

    if (ImGui::IsItemActivated()) {
        mPageScrollTarget.reset();
        mPress = {};
        mPress.on = !stopClick;
        mPress.button = ImGui::IsMouseDown(ImGuiMouseButton_Left) ? ImGuiMouseButton_Left : ImGuiMouseButton_Middle;
        mPress.at = io.MousePos;
        if (mPress.on && mPress.button == ImGuiMouseButton_Middle) {
            mAutoscrolling = true;
            mAutoscrollOrigin = mPress.at;
            mAutoscrollEdgeTravel = 0;
        } else if (mPress.on && onPage && mTool == Tool::Text) {
            int clicks = io.MouseClickedCount[ImGuiMouseButton_Left];
            if (clicks >= 2) {
                selectWord(caretAt(page, px, py), clicks >= 3);
                mPress.dragged = true; // not a click: keep the selection
            } else if (mHoverLink < 0) {
                Caret c = caretAt(page, px, py);
                if (c.valid()) {
                    mSelA = mSelB = c;
                    mPress.selecting = true;
                }
            }
        }
    }
    bool released = ImGui::IsItemDeactivated();
    bool middle = mPress.button == ImGuiMouseButton_Middle;
    if (mPress.on && (active || (middle && released)) && ImGui::IsMousePosValid()) {
        ImVec2 d{io.MousePos.x - mPress.at.x, io.MousePos.y - mPress.at.y};
        if (!mPress.dragged && d.x * d.x + d.y * d.y > 9) mPress.dragged = true;
    }
    if (mPress.on && active && !middle) {
        if (mPress.dragged) mPageScrollTarget.reset();
        if (mPress.dragged && mPress.selecting) {
            int p;
            double x, y;
            if (pageAtScreen(sx, sy, p, x, y, true))
                if (Caret c = caretAt(p, x, y); c.valid()) mSelB = c;
            // past the view's edge: scroll along
            if (sy < 0) mView.y += sy * 0.2 / mView.zoom;
            if (sy > mView.h) mView.y += (sy - mView.h) * 0.2 / mView.zoom;
        } else if (mPress.dragged) {
            mView.x -= io.MouseDelta.x / mView.zoom;
            mView.y -= io.MouseDelta.y / mView.zoom;
        }
    }
    if (mPress.on && released) {
        if (middle) {
            // A plain click latches autoscroll; releasing after a drag stops it.
            if (mPress.dragged || !hovered || !ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
                mAutoscrolling = false;
        } else if (!mPress.dragged) {
            int p;
            double x, y;
            int l = -1;
            if (pageAtScreen(sx, sy, p, x, y)) l = linkAt(p, x, y);
            if (l >= 0)
                follow(mInfo[p].links[static_cast<size_t>(l)]);
            else
                clearSelection();
        }
        mPress = {};
    }

    if (mPress.on && !middle && mPress.dragged && !mPress.selecting)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (mHoverLink >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if ((mTool == Tool::Text && onPage) || mPress.selecting)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (mHoverLink >= 0 && !mPress.on) {
        const Link& l = mInfo[mHoverPage].links[static_cast<size_t>(mHoverLink)];
        if (l.kind == Link::Kind::Uri)
            ImGui::SetTooltip("%s", l.uri.c_str());
        else
            ImGui::SetTooltip("Go to page %s", pageLabel(l.dest.page).c_str());
    }
    mView.clamp(mLayout);
}

void Viewer::updatePageScroll()
{
    if (!mPageScrollTarget) return;
    constexpr double duration = 0.60;
    // The first frame can follow a long wait for input.
    mPageScrollElapsed += std::min(ImGui::GetIO().DeltaTime, 0.05f);
    double remaining = std::max(0.0, 1 - mPageScrollElapsed / duration);
    // Cubic ease-out.
    mView.y = std::lerp(*mPageScrollTarget, mPageScrollStart, remaining * remaining * remaining);
    if (remaining == 0) mPageScrollTarget.reset();
}

void Viewer::updateAutoscroll()
{
    if (!mAutoscrolling) return;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
            mAutoscrolling = false;
            return;
        }
    }
    if (!ImGui::IsMousePosValid()) return;

    mPageScrollTarget.reset();
    const ImGuiIO& io = ImGui::GetIO();
    double scale = uiScale();
    auto distance = [&](double offset) {
        constexpr double initialSensitivity = 5;
        constexpr double growthDistance = 200;
        constexpr double speedLimit = 5000;
        double beyondDeadZone = std::clamp(std::abs(offset) / scale - 10, 0.0, speedLimit / initialSensitivity);
        double speed = initialSensitivity * growthDistance * std::expm1(beyondDeadZone / growthDistance);
        return std::copysign(std::min(speed, speedLimit) * scale * std::min(io.DeltaTime, 0.05f), offset);
    };
    double dx = distance(io.MousePos.x - mAutoscrollOrigin.x);
    double dy = distance(io.MousePos.y - mAutoscrollOrigin.y);
    mView.x += dx / mView.zoom;
    auto [top, bottom] = mView.band(mLayout);
    double height = mView.h / mView.zoom;
    bool pushingEdge = !mMode.continuous && dy != 0
        && (dy > 0 ? mView.y + height >= bottom - 0.5 : mView.y <= top + 0.5);
    if (pushingEdge) {
        // A fitted page is always at an edge. Let the pointer speed decide
        // how long a page stays visible instead of flipping every frame.
        if (mAutoscrollEdgeTravel * dy < 0) mAutoscrollEdgeTravel = 0;
        mAutoscrollEdgeTravel += dy;
        if (std::abs(mAutoscrollEdgeTravel) >= mView.h) {
            mView.flip(dy > 0 ? 1 : -1, mLayout);
            mAutoscrollEdgeTravel = 0;
        }
    } else {
        mAutoscrollEdgeTravel = 0;
        mView.y += dy / mView.zoom;
    }
    mView.clamp(mLayout);
}

void Viewer::drawAutoscrollMarker(ImDrawList* dl, ImVec2 origin, ImVec2 size) const
{
    float scale = static_cast<float>(uiScale());
    float radius = 13 * scale;
    float arrow = 4 * scale;
    float offset = 5 * scale;
    ImVec2 c = mAutoscrollOrigin;
    ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    dl->AddCircleFilled(c, radius, ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddCircle(c, radius, ink, 0, scale);
    dl->AddCircleFilled(c, 1.6f * scale, ink);
    dl->AddTriangleFilled({c.x, c.y - offset - arrow}, {c.x - arrow, c.y - offset}, {c.x + arrow, c.y - offset}, ink);
    dl->AddTriangleFilled({c.x, c.y + offset + arrow}, {c.x - arrow, c.y + offset}, {c.x + arrow, c.y + offset}, ink);
    if (mLayout.width > mView.w / mView.zoom) {
        dl->AddTriangleFilled({c.x - offset - arrow, c.y}, {c.x - offset, c.y - arrow}, {c.x - offset, c.y + arrow}, ink);
        dl->AddTriangleFilled({c.x + offset + arrow, c.y}, {c.x + offset, c.y - arrow}, {c.x + offset, c.y + arrow}, ink);
    }
    dl->PopClipRect();
}

// --- drawing

void Viewer::drawPageBox()
{
    int cur = currentPage();
    // the page box shows the current page until it is edited
    if (!mEditingPage) snprintf(mPageInput, sizeof mPageInput, "%s", pageLabel(cur).c_str());
    if (mFocusPageBox) {
        ImGui::SetKeyboardFocusHere();
        mFocusPageBox = false;
    }
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::InputText("##page", mPageInput, sizeof mPageInput, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        int p = pageForLabel(mPageInput);
        if (p >= 0) goToPage(p);
    }
    mEditingPage = ImGui::IsItemActive();
    ImGui::SameLine();
    if (hasPageLabels())
        ImGui::Text("(%d of %d)", cur + 1, pageCount());
    else
        ImGui::Text("of %d", pageCount());
}

void Viewer::drawZoomBox()
{
    if (ImGui::Button("-")) zoomOut();
    ImGui::SameLine(0, 2);
    drawZoomCombo();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("+")) zoomIn();
}

void Viewer::drawZoomCombo()
{
    char z[16];
    snprintf(z, sizeof z, "%d%%", static_cast<int>(std::lround(mView.zoom / uiScale() * 100)));
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000%").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight());
    if (ImGui::BeginCombo("##zoom", z, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Fit Width", mZoomMode == ZoomMode::FitWidth)) fitWidth();
        if (ImGui::Selectable("Fit Page", mZoomMode == ZoomMode::FitPage)) fitPage();
        ImGui::Separator();
        for (int p : {25, 50, 75, 100, 125, 150, 200, 300, 400, 800}) {
            char l[16];
            snprintf(l, sizeof l, "%d%%", p);
            if (ImGui::Selectable(l, mZoomMode == ZoomMode::Free && std::lround(mView.zoom / uiScale() * 100) == p)) setZoom(p / 100.0 * uiScale());
        }
        ImGui::EndCombo();
    }
}

void Viewer::drawToolbar()
{
    ImGui::AlignTextToFramePadding();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) prevPage();
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##next", ImGuiDir_Down)) nextPage();
    ImGui::SameLine();
    drawPageBox();
    ImGui::SameLine(0, 24);
    drawZoomBox();
    ImGui::SameLine();
    if (ImGui::Button("Fit width")) fitWidth();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Fit page")) fitPage();
    if (mFindBar) {
        ImGui::SameLine(0, 24);
        drawFindBar();
    }
}

void Viewer::drawPages(ImDrawList* dl, ImVec2 origin, ImVec2 size)
{
    mLastDocumentFrame = ImGui::GetFrameCount();
    dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
    dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, background());
    auto toScreenDoc = [&](double dx, double dy) {
        return ImVec2(static_cast<float>(std::floor(origin.x + (dx - mView.x) * mView.zoom)), static_cast<float>(std::floor(origin.y + (dy - mView.y) * mView.zoom)));
    };
    auto [a, b] = mView.pages(mLayout);
    for (int i = a; i <= b; i++) {
        const PageSize& p = mLayout.pages[static_cast<size_t>(i)];
        double px = mLayout.left(i), py = mLayout.top[static_cast<size_t>(i)];
        ImVec2 p0 = toScreenDoc(px, py), p1 = toScreenDoc(px + p.w, py + p.h);
        dl->AddRectFilled({p0.x + 2, p0.y + 3}, {p1.x + 2, p1.y + 3}, IM_COL32(0, 0, 0, 90)); // shadow
        dl->AddRectFilled(p0, p1, IM_COL32_WHITE);
        // the preview, then tiles of other levels, then the current level
        // on top: whatever is sharpest so far ends up visible
        auto it0 = mTiles.lower_bound({i, TileKey::kPreview - 1, 0, 0});
        for (int pass = 0; pass < 3; pass++)
            for (auto it = it0; it != mTiles.end() && it->first.page == i; ++it) {
                const TileKey& k = it->first;
                Tex& t = it->second;
                if (!t.id || k.tx == TileKey::kWholePage) continue;
                bool preview = k.level == TileKey::kPreview;
                if (pass != (preview ? 0 : k.level == mLevel ? 2 : 1)) continue;
                double s = preview ? previewScale(p) : scaleOf(k.level);
                double ts = mOpt.tileSize;
                double dx0 = preview ? px : px + k.tx * ts / s, dy0 = preview ? py : py + k.ty * ts / s;
                double dx1 = dx0 + t.w / s, dy1 = dy0 + t.h / s;
                ImVec2 q0 = toScreenDoc(dx0, dy0), q1 = toScreenDoc(std::min(dx1, px + p.w), std::min(dy1, py + p.h));
                if (q0.x >= clipMax.x || q0.y >= clipMax.y || q1.x <= clipMin.x || q1.y <= clipMin.y) continue;
                t.drawnInDocument = mLastDocumentFrame;
                // the last row and column of a tile may be part pixels of
                // the page edge: map exactly the drawn part
                float u1 = static_cast<float>((std::min(dx1, px + p.w) - dx0) * s / t.w);
                float v1 = static_cast<float>((std::min(dy1, py + p.h) - dy0) * s / t.h);
                dl->AddImage(static_cast<ImTextureID>(t.id), q0, q1, {0, 0}, {u1, v1});
            }
    }
    dl->PopClipRect();
}

void Viewer::drawOverlays(ImDrawList* dl, ImVec2 origin, ImVec2 size)
{
    dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto box = [&](int page, const Rect& r, ImU32 fill, ImU32 border = 0) {
        ImVec2 a = toScreen(page, r.x0, r.y0), b = toScreen(page, r.x1, r.y1);
        dl->AddRectFilled(a, b, fill);
        if (border) dl->AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, border, 0.0f, 0, 2.0f);
    };
    auto [a, b] = mView.pages(mLayout);
    for (int p = a; p <= b; p++) {
        auto it = mFind.matches.find(p);
        if (it == mFind.matches.end()) continue;
        for (size_t k = 0; k < it->second.size(); k++) {
            bool cur = p == mFind.page && static_cast<int>(k) == mFind.index;
            box(p, it->second[k], cur ? IM_COL32(255, 140, 0, 120) : IM_COL32(255, 220, 0, 90), cur ? IM_COL32(230, 100, 0, 255) : 0);
        }
    }
    if (hasSelection()) {
        Caret lo = std::min(mSelA, mSelB), hi = std::max(mSelA, mSelB);
        for (int p = std::max(a, lo.page); p <= std::min(b, hi.page); p++) {
            const Info* in = info(p);
            if (!in) continue;
            const TextLayout& t = in->text;
            size_t s = p == lo.page ? std::min(t.text.size(), static_cast<size_t>(lo.pos)) : 0;
            size_t e = p == hi.page ? std::min(t.text.size(), static_cast<size_t>(hi.pos)) : t.text.size();
            // merge each line's characters into one box
            bool open = false;
            Rect g;
            for (size_t i = s; i < e; i++) {
                const Rect& r = t.boxes[i];
                if (t.text[i] == U'\n' || r.x1 <= r.x0 || r.y1 <= r.y0) continue;
                if (open && r.y0 < g.y1 && r.y1 > g.y0 && r.x0 >= g.x0 - 1) {
                    g = {g.x0, std::min(g.y0, r.y0), std::max(g.x1, r.x1), std::max(g.y1, r.y1)};
                    continue;
                }
                if (open) box(p, g, IM_COL32(60, 120, 230, 90));
                g = r;
                open = true;
            }
            if (open) box(p, g, IM_COL32(60, 120, 230, 90));
        }
    }
    dl->PopClipRect();
}

void Viewer::drawScrollbar(ImVec2 pos, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
    auto [b0, b1] = mView.band(mLayout);
    double total = (b1 - b0) * mView.zoom;
    if (total <= size.y) return;
    float thumbH = std::max(24.0f, static_cast<float>(size.y * size.y / total));
    float track = size.y - thumbH;
    double maxY = b1 - b0 - mView.h / mView.zoom;
    float thumbY = pos.y + static_cast<float>(track * std::clamp((mView.y - b0) / maxY, 0.0, 1.0));

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##vscroll", size);
    bool held = ImGui::IsItemActive();
    if (held) {
        mPageScrollTarget.reset();
        float my = ImGui::GetIO().MousePos.y;
        if (ImGui::IsItemActivated()) mGrab = (my >= thumbY && my <= thumbY + thumbH) ? my - thumbY : thumbH / 2;
        mView.y = b0 + std::clamp((my - mGrab - pos.y) / track, 0.0f, 1.0f) * maxY;
        mView.clamp(mLayout);
        thumbY = pos.y + static_cast<float>(track * std::clamp((mView.y - b0) / maxY, 0.0, 1.0));
    }
    ImGuiCol c = held ? ImGuiCol_ScrollbarGrabActive : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
    dl->AddRectFilled({pos.x + 2, thumbY}, {pos.x + size.x - 2, thumbY + thumbH}, ImGui::GetColorU32(c), (size.x - 4) / 2);
}

void Viewer::draw(const char* id, ImVec2 size)
{
    mFrame++;
    ImGui::PushID(id);
    ImGui::BeginChild("##viewer", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    State st = state();
    if (st == State::Ready && !mLaidOut) {
        // the view size is needed for the first fit: measure, lay out below
        mView.w = ImGui::GetContentRegionAvail().x - scrollbarW();
        mView.h = ImGui::GetContentRegionAvail().y - (mOpt.toolbar ? ImGui::GetFrameHeightWithSpacing() : 0);
        startLayout();
    }
    if (st != State::Ready) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(), {ImGui::GetCursorScreenPos().x + avail.x, ImGui::GetCursorScreenPos().y + avail.y}, background());
        const char* msg = st == State::Empty ? "No document" : st == State::Loading ? "Loading..." : nullptr;
        std::string err;
        if (st == State::Failed) {
            err = mWorker->error() == "locked" ? "This PDF is password protected." : mWorker->error();
            msg = err.c_str();
        }
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos({std::max(0.0f, (avail.x - ts.x) / 2), std::max(0.0f, (avail.y - ts.y) / 2)});
        ImGui::TextUnformatted(msg);
        ImGui::EndChild();
        ImGui::PopID();
        return;
    }

    if (mOpt.shortcuts && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) actions().update();
    if (mOpt.toolbar) drawToolbar();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    mOrigin = origin;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 canvas{std::max(1.0f, avail.x - scrollbarW()), std::max(1.0f, avail.y)};
    if (mView.w != canvas.x || mView.h != canvas.y) {
        mPageScrollTarget.reset();
        // keep the page under the view's centre on resize
        double cy = mView.y + mView.h / mView.zoom / 2;
        mView.w = canvas.x;
        mView.h = canvas.y;
        mView.y = cy - mView.h / mView.zoom / 2;
        mView.clamp(mLayout);
        applyZoomMode();
    }

    ImGui::InvisibleButton("##canvas", canvas, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered) {
        // the wheel scrolls the document, not an outer window
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
    }
    handleMouse(origin, hovered, active);
    updateAutoscroll();
    updatePageScroll();
    if (mAutoscrolling && hovered)
        ImGui::SetMouseCursor(mLayout.width > mView.w / mView.zoom ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_ResizeNS);
    drawScrollbar({origin.x + canvas.x, origin.y}, {scrollbarW(), canvas.y});

    if (mSettling && Clock::now() - mZoomChanged >= kSettle) {
        mSettling = false;
        mLevel = targetLevel();
    }
    takeFind();
    updateThumbnailFind();
    if (mCopyPending) copy();
    takeInfo();
    uploadResults();
    requestTiles();
    evict();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawPages(dl, origin, canvas);
    drawOverlays(dl, origin, canvas);
    if (mAutoscrolling) drawAutoscrollMarker(dl, origin, canvas);

    ImGui::EndChild();
    ImGui::PopID();
}

// --- actions

Actions& Viewer::actions()
{
    if (!mActions) {
        mActions = std::make_unique<Actions>();
        registerActions(*mActions, [this] { return this; });
    }
    return *mActions;
}

void Viewer::registerActions(Actions& acts, std::function<Viewer*()> current)
{
    auto ready = [current] {
        Viewer* v = current();
        return v && v->state() == State::Ready;
    };
    auto add = [&](const char* id, const char* name, const char* cat, const char* keys, const char* alt, std::function<void(Viewer&)> f) -> Action& {
        Action& a = acts.add(id, name, cat, keys, alt, [current, f] {
            if (Viewer* v = current()) f(*v);
        });
        a.enabled = ready;
        return a;
    };
    static constexpr double line = 60; // pixels at scale 1
    add("view.scroll_up", "Scroll Up", "Navigation", "Up", "K", [](Viewer& v) { v.scrollBy(0, -line * uiScale()); });
    add("view.scroll_down", "Scroll Down", "Navigation", "Down", "J", [](Viewer& v) { v.scrollBy(0, line * uiScale()); });
    add("view.scroll_left", "Scroll Left", "Navigation", "Left", "", [](Viewer& v) { v.scrollBy(-line * uiScale(), 0); });
    add("view.scroll_right", "Scroll Right", "Navigation", "Right", "", [](Viewer& v) { v.scrollBy(line * uiScale(), 0); });
    add("view.page_down", "Scroll Page Down", "Navigation", "Space", "PgDown", [](Viewer& v) { v.scrollPage(1); });
    add("view.page_up", "Scroll Page Up", "Navigation", "Shift+Space", "PgUp", [](Viewer& v) { v.scrollPage(-1); });
    add("go.next_page", "Next Page", "Navigation", "Ctrl+PgDown", "", [](Viewer& v) { v.nextPage(); });
    add("go.prev_page", "Previous Page", "Navigation", "Ctrl+PgUp", "", [](Viewer& v) { v.prevPage(); });
    add("go.first", "Beginning of the Document", "Navigation", "Home", "G, G", [](Viewer& v) { v.first(); });
    add("go.last", "End of the Document", "Navigation", "End", "Shift+G", [](Viewer& v) { v.last(); });
    add("go.back", "Back", "Navigation", "Alt+Left", "Back", [](Viewer& v) { v.back(); });
    add("go.forward", "Forward", "Navigation", "Alt+Right", "Forward", [](Viewer& v) { v.forward(); });
    add("go.page", "Go to Page...", "Navigation", "Ctrl+G", "", [](Viewer& v) { v.focusPageBox(); });

    add("zoom.in", "Zoom In", "Zoom", "Ctrl+=", "Ctrl+NumPlus", [](Viewer& v) { v.zoomIn(); });
    add("zoom.out", "Zoom Out", "Zoom", "Ctrl+-", "Ctrl+Num-", [](Viewer& v) { v.zoomOut(); });
    add("zoom.fit_width", "Fit Width", "Zoom", "S", "Ctrl+0", [](Viewer& v) { v.fitWidth(); }).checked = [current] { return current() && current()->zoomMode() == ZoomMode::FitWidth; };
    add("zoom.fit_page", "Fit Page", "Zoom", "Ctrl+9", "", [](Viewer& v) { v.fitPage(); }).checked = [current] { return current() && current()->zoomMode() == ZoomMode::FitPage; };
    add("zoom.actual", "Actual Size", "Zoom", "", "", [](Viewer& v) { v.setZoom(uiScale()); });

    auto mode = [current](int cols, bool firstAlone) {
        return [current, cols, firstAlone] {
            Viewer* v = current();
            if (!v) return false;
            ViewMode m = v->viewMode();
            return cols > 2 ? m.columns > 2 : m.columns == cols && (cols == 1 || m.firstAlone == firstAlone);
        };
    };
    add("view.single", "Single Page", "View Mode", "I", "", [](Viewer& v) { v.setViewMode({1, false, v.viewMode().continuous}); }).checked = mode(1, false);
    add("view.facing", "Facing Pages", "View Mode", "", "", [](Viewer& v) { v.setViewMode({2, false, v.viewMode().continuous}); }).checked = mode(2, false);
    add("view.facing_first", "Facing Pages (Center First Page)", "View Mode", "", "", [](Viewer& v) { v.setViewMode({2, true, v.viewMode().continuous}); }).checked = mode(2, true);
    add("view.overview", "Overview", "View Mode", "O", "", [](Viewer& v) {
        ViewMode m = v.viewMode();
        if (m.columns > 2) {
            ViewMode p = v.mPrevMode;
            p.continuous = m.continuous;
            v.setViewMode(p); // toggles back
        } else
            v.setViewMode({v.overviewColumns(), false, m.continuous});
    }).checked = mode(3, false);
    add("view.continuous", "Continuous", "View Mode", "C", "", [](Viewer& v) {
        ViewMode m = v.viewMode();
        m.continuous = !m.continuous;
        v.setViewMode(m);
    }).checked = [current] { return current() && current()->viewMode().continuous; };

    add("tool.browse", "Browse", "Tools", "Ctrl+1", "", [](Viewer& v) { v.setTool(Tool::Browse); }).checked = [current] { return current() && current()->tool() == Tool::Browse; };
    add("tool.text", "Text Selection", "Tools", "Ctrl+4", "", [](Viewer& v) { v.setTool(Tool::Text); }).checked = [current] { return current() && current()->tool() == Tool::Text; };

    add("edit.copy", "Copy", "Edit", "Ctrl+C", "Ctrl+Ins", [](Viewer& v) { v.copy(); });
    add("edit.select_all", "Select All (Page)", "Edit", "Ctrl+A", "", [](Viewer& v) { v.selectAll(); });
    add("edit.deselect", "Clear Selection", "Edit", "Esc", "", [](Viewer& v) {
        if (v.findBarShown())
            v.hideFindBar();
        else
            v.clearSelection();
    });

    add("find.find", "Find...", "Find", "Ctrl+F", "/", [](Viewer& v) { v.showFindBar(); });
    add("find.next", "Find Next", "Find", "N", "F3", [](Viewer& v) { v.findNext(); });
    add("find.prev", "Find Previous", "Find", "Shift+N", "Shift+F3", [](Viewer& v) { v.findPrev(); });
    add("find.whole_words", "Whole Words", "Find", "", "", [](Viewer& v) {
        v.setFindBarFlags(v.findFlags() ^ FindWholeWords);
    }).checked = [current] { return current() && (current()->findFlags() & FindWholeWords) != 0; };
}

}
