#include <pdfview/Viewer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glad/glad.h>

namespace pdfview {

using Clock = std::chrono::steady_clock;
static constexpr auto kSettle = std::chrono::milliseconds(150);
static constexpr float kScrollbarW = 12;
static constexpr double kWheelStep = 80; // screen pixels per wheel notch

Viewer::Viewer()
    : Viewer(Options{})
{
}

Viewer::Viewer(Options o)
    : mOpt(o)
{
}

Viewer::~Viewer() { close(); }

void Viewer::freeTex(Tex& t)
{
    if (t.id) {
        GLuint id = t.id;
        glDeleteTextures(1, &id);
        t.id = 0;
    }
}

void Viewer::open(const std::string& path)
{
    close();
    mPath = path;
    // the wake function may change after open; call the current one
    mWorker = std::make_unique<RenderWorker>(path, mOpt.tileSize, [this] {
        if (mWake) mWake();
    });
}

void Viewer::releaseTextures()
{
    for (auto& [k, t] : mTiles)
        freeTex(t);
    mTiles.clear();
    mPending.clear();
    mBytes = 0;
    mLastWanted.clear();
    if (mWorker) mWorker->want({});
}

void Viewer::close()
{
    mWorker.reset(); // joins the thread before the textures go
    for (auto& [k, t] : mTiles)
        freeTex(t);
    mTiles.clear();
    mPending.clear();
    mBytes = 0;
    mLastWanted.clear();
    mLaidOut = false;
    mLayout = {};
    mView = {};
    mSettling = false;
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

int Viewer::currentPage() const { return mLaidOut ? mView.currentPage(mLayout) : 0; }

void Viewer::goToPage(int page)
{
    if (mLaidOut) mView.goToPage(page, mLayout);
}

void Viewer::zoomAround(double zoom, double sx, double sy)
{
    double before = mView.zoom;
    mView.zoomAt(zoom, sx, sy, mLayout);
    if (mView.zoom != before) {
        mZoomChanged = Clock::now();
        mSettling = true;
    }
}

void Viewer::setZoom(double zoom)
{
    if (mLaidOut) zoomAround(zoom, mView.w / 2, mView.h / 2);
}

void Viewer::fitWidth()
{
    if (!mLaidOut) return;
    int p = currentPage();
    zoomAround(mView.fitWidth(mLayout, p), mView.w / 2, mView.h / 2);
    mView.clamp(mLayout);
}

void Viewer::fitPage()
{
    if (!mLaidOut) return;
    int p = currentPage();
    zoomAround(mView.fitPage(mLayout, p), mView.w / 2, mView.h / 2);
    mView.goToPage(p, mLayout);
}

bool Viewer::needsFrames() const { return mSettling || !mPending.empty() || state() == State::Loading; }

double Viewer::fbScale() const
{
    ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
    return s.x > 0 ? s.x : 1.0;
}

int Viewer::targetLevel() const { return levelFor(mView.zoom * fbScale()); }

void Viewer::startLayout()
{
    mLayout.build(mWorker->pageSizes());
    // fit the first page's width, but not huge on a wide window
    mView.zoom = std::min(mView.fitWidth(mLayout, 0), 2.0);
    mView.x = 0;
    mView.y = 0;
    mView.clamp(mLayout);
    mLevel = targetLevel();
    mLaidOut = true;
}

void Viewer::uploadResults()
{
    for (auto& r : mWorker->take())
        mPending.push_back(std::move(r));
    int n = 0;
    while (!mPending.empty() && n < mOpt.uploadsPerFrame) {
        RenderWorker::Result r = std::move(mPending.front());
        mPending.pop_front();
        Tex t;
        t.used = mFrame;
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
            mBytes += static_cast<size_t>(t.w) * t.h * 4;
            n++;
        }
        auto [it, inserted] = mTiles.try_emplace(r.key, t);
        if (!inserted) { // rendered twice; keep the new one
            mBytes -= static_cast<size_t>(it->second.w) * it->second.h * 4;
            freeTex(it->second);
            it->second = t;
        }
    }
}

void Viewer::requestTiles()
{
    std::vector<TileKey> want;
    auto add = [&](const TileKey& k) {
        auto it = mTiles.find(k);
        if (it != mTiles.end()) {
            it->second.used = mFrame;
            return;
        }
        // rendered, waiting for its upload
        for (const auto& r : mPending)
            if (r.key == k) return;
        want.push_back(k);
    };
    auto [a, b] = mLayout.range(mView.y, mView.y + mView.h / mView.zoom);
    // previews first: cheap, and they cover the page while tiles come
    for (int i = a; i <= b; i++)
        add({i, TileKey::kPreview, 0, 0});
    // sharp tiles once the zoom has settled, unless the preview is sharp
    // enough already
    bool needTiles = scaleOf(mLevel) > previewScale(mLayout.pages[static_cast<size_t>(std::max(a, 0))]) * 1.05;
    if (needTiles)
        for (const auto& k : visibleTiles(mLayout, mView, mLevel, mOpt.tileSize))
            add(k);
    // then a little ahead: previews of nearby pages, tiles half a screen out
    for (int d = 1; d <= 3; d++) {
        if (b + d < mLayout.count()) add({b + d, TileKey::kPreview, 0, 0});
        if (a - d >= 0) add({a - d, TileKey::kPreview, 0, 0});
    }
    if (needTiles && !mSettling)
        for (const auto& k : visibleTiles(mLayout, mView, mLevel, mOpt.tileSize, mView.h / 2))
            if (std::find(want.begin(), want.end(), k) == want.end()) add(k);
    // mark what is on screen as used, for eviction
    for (int i = a; i <= b; i++)
        for (auto it = mTiles.lower_bound({i, TileKey::kPreview - 1, 0, 0}); it != mTiles.end() && it->first.page == i; ++it)
            it->second.used = mFrame;
    if (want != mLastWanted) {
        mLastWanted = want;
        mWorker->want(std::move(want));
    }
}

void Viewer::evict()
{
    if (mBytes <= mOpt.textureBudget) return;
    std::vector<std::pair<uint64_t, TileKey>> order;
    for (const auto& [k, t] : mTiles)
        if (t.used < mFrame) order.push_back({t.used, k});
    std::sort(order.begin(), order.end());
    for (const auto& [used, k] : order) {
        if (mBytes <= mOpt.textureBudget * 3 / 4) break;
        auto it = mTiles.find(k);
        mBytes -= static_cast<size_t>(it->second.w) * it->second.h * 4;
        freeTex(it->second);
        mTiles.erase(it);
    }
}

void Viewer::handleInput(ImVec2 origin, bool hovered, bool active)
{
    ImGuiIO& io = ImGui::GetIO();
    double sx = io.MousePos.x - origin.x, sy = io.MousePos.y - origin.y;
    if (hovered) {
        if (io.KeyCtrl && io.MouseWheel != 0)
            zoomAround(mView.zoom * std::pow(1.15, io.MouseWheel), sx, sy);
        else if (io.MouseWheel != 0 || io.MouseWheelH != 0) {
            double dy = io.MouseWheel, dx = io.MouseWheelH;
            if (io.KeyShift) std::swap(dx, dy);
            mView.y -= dy * kWheelStep / mView.zoom;
            mView.x -= dx * kWheelStep / mView.zoom;
        }
    }
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))) {
        mView.x -= io.MouseDelta.x / mView.zoom;
        mView.y -= io.MouseDelta.y / mView.zoom;
    }
    // keys while the viewer's window has focus and no text field is active
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput) {
        double page = mView.h * 0.9 / mView.zoom, line = 60 / mView.zoom;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown) || (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyShift)) mView.y += page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) || (ImGui::IsKeyPressed(ImGuiKey_Space) && io.KeyShift)) mView.y -= page;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) mView.y += line;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) mView.y -= line;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) mView.x += line;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) mView.x -= line;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) mView.y = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) mView.y = mLayout.height;
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))) setZoom(mView.zoom * 1.25);
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) setZoom(mView.zoom / 1.25);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) fitWidth();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_9)) fitPage();
    }
    mView.clamp(mLayout);
}

void Viewer::drawToolbar()
{
    int cur = currentPage();
    ImGui::AlignTextToFramePadding();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) goToPage(cur - 1);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##next", ImGuiDir_Down)) goToPage(cur + 1);
    ImGui::SameLine();
    // the page box shows the current page until it is edited
    if (!mEditingPage) snprintf(mPageInput, sizeof mPageInput, "%d", cur + 1);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::InputText("##page", mPageInput, sizeof mPageInput, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        goToPage(atoi(mPageInput) - 1);
    }
    mEditingPage = ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::Text("/ %d", pageCount());
    ImGui::SameLine(0, 24);
    if (ImGui::Button("-")) setZoom(mView.zoom / 1.25);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000%").x + ImGui::GetStyle().FramePadding.x * 2);
    char z[16];
    snprintf(z, sizeof z, "%d%%", static_cast<int>(std::lround(mView.zoom * 100)));
    ImGui::Button(z);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("+")) setZoom(mView.zoom * 1.25);
    ImGui::SameLine();
    if (ImGui::Button("Fit width")) fitWidth();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Fit page")) fitPage();
}

void Viewer::drawPages(ImDrawList* dl, ImVec2 origin, ImVec2 size)
{
    dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, mOpt.background);
    auto toScreen = [&](double dx, double dy) {
        return ImVec2(static_cast<float>(std::floor(origin.x + (dx - mView.x) * mView.zoom)), static_cast<float>(std::floor(origin.y + (dy - mView.y) * mView.zoom)));
    };
    auto [a, b] = mLayout.range(mView.y, mView.y + mView.h / mView.zoom);
    for (int i = a; i <= b; i++) {
        const PageSize& p = mLayout.pages[static_cast<size_t>(i)];
        double px = mLayout.left(i), py = mLayout.top[static_cast<size_t>(i)];
        ImVec2 p0 = toScreen(px, py), p1 = toScreen(px + p.w, py + p.h);
        dl->AddRectFilled({p0.x + 2, p0.y + 3}, {p1.x + 2, p1.y + 3}, IM_COL32(0, 0, 0, 90)); // shadow
        dl->AddRectFilled(p0, p1, IM_COL32_WHITE);
        // the preview, then tiles of other levels, then the current level
        // on top: whatever is sharpest so far ends up visible
        auto it0 = mTiles.lower_bound({i, TileKey::kPreview - 1, 0, 0});
        for (int pass = 0; pass < 3; pass++)
            for (auto it = it0; it != mTiles.end() && it->first.page == i; ++it) {
                const TileKey& k = it->first;
                const Tex& t = it->second;
                if (!t.id) continue;
                bool preview = k.level == TileKey::kPreview;
                if (pass != (preview ? 0 : k.level == mLevel ? 2 : 1)) continue;
                double s = preview ? previewScale(p) : scaleOf(k.level);
                double ts = mOpt.tileSize;
                double dx0 = preview ? px : px + k.tx * ts / s, dy0 = preview ? py : py + k.ty * ts / s;
                double dx1 = dx0 + t.w / s, dy1 = dy0 + t.h / s;
                ImVec2 q0 = toScreen(dx0, dy0), q1 = toScreen(std::min(dx1, px + p.w), std::min(dy1, py + p.h));
                // the last row and column of a tile may be part pixels of
                // the page edge: map exactly the drawn part
                float u1 = static_cast<float>((std::min(dx1, px + p.w) - dx0) * s / t.w);
                float v1 = static_cast<float>((std::min(dy1, py + p.h) - dy0) * s / t.h);
                dl->AddImage(static_cast<ImTextureID>(t.id), q0, q1, {0, 0}, {u1, v1});
            }
    }
    dl->PopClipRect();
}

void Viewer::drawScrollbar(ImVec2 pos, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
    double total = mLayout.height * mView.zoom;
    if (total <= size.y) return;
    float thumbH = std::max(24.0f, static_cast<float>(size.y * size.y / total));
    float track = size.y - thumbH;
    double maxY = mLayout.height - mView.h / mView.zoom;
    float thumbY = pos.y + static_cast<float>(track * std::clamp(mView.y / maxY, 0.0, 1.0));

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##vscroll", size);
    bool held = ImGui::IsItemActive();
    if (held) {
        float my = ImGui::GetIO().MousePos.y;
        if (ImGui::IsItemActivated()) mGrab = (my >= thumbY && my <= thumbY + thumbH) ? my - thumbY : thumbH / 2;
        mView.y = std::clamp((my - mGrab - pos.y) / track, 0.0f, 1.0f) * maxY;
        mView.clamp(mLayout);
        thumbY = pos.y + static_cast<float>(track * std::clamp(mView.y / maxY, 0.0, 1.0));
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
        mView.w = ImGui::GetContentRegionAvail().x - kScrollbarW;
        mView.h = ImGui::GetContentRegionAvail().y - (mOpt.toolbar ? ImGui::GetFrameHeightWithSpacing() : 0);
        startLayout();
    }
    if (st != State::Ready) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(), {ImGui::GetCursorScreenPos().x + avail.x, ImGui::GetCursorScreenPos().y + avail.y}, mOpt.background);
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

    if (mOpt.toolbar) drawToolbar();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 canvas{std::max(1.0f, avail.x - kScrollbarW), std::max(1.0f, avail.y)};
    if (mView.w != canvas.x || mView.h != canvas.y) {
        // keep the page under the view's centre on resize
        double cy = mView.y + mView.h / mView.zoom / 2;
        mView.w = canvas.x;
        mView.h = canvas.y;
        mView.y = cy - mView.h / mView.zoom / 2;
        mView.clamp(mLayout);
    }

    ImGui::InvisibleButton("##canvas", canvas, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered) {
        // the wheel scrolls the document, not an outer window
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
    }
    if (active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    handleInput(origin, hovered, active);
    drawScrollbar({origin.x + canvas.x, origin.y}, {kScrollbarW, canvas.y});

    if (mSettling && Clock::now() - mZoomChanged >= kSettle) {
        mSettling = false;
        mLevel = targetLevel();
    }
    uploadResults();
    requestTiles();
    evict();
    drawPages(ImGui::GetWindowDrawList(), origin, canvas);

    ImGui::EndChild();
    ImGui::PopID();
}

}
