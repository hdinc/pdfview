#include <pdfview/Panels.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pdfview {

namespace {

bool containsNoCase(const std::string& hay, const char* needle)
{
    if (!*needle) return true;
    std::string n = needle;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    auto it = std::search(hay.begin(), hay.end(), n.begin(), n.end(), [&](char a, char b) { return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

// the last item, in reading order, that starts at or before (page, y)
void findCurrent(const std::vector<OutlineItem>& items, Dest at, const OutlineItem*& best)
{
    for (const auto& it : items) {
        if (it.dest.valid() && (it.dest.page < at.page || (it.dest.page == at.page && it.dest.y <= std::max(at.y, 0.0) + 40))) best = &it;
        findCurrent(it.children, at, best);
    }
}

// the items from the top down to target
bool pathTo(const std::vector<OutlineItem>& items, const OutlineItem* target, std::vector<const OutlineItem*>& path)
{
    for (const auto& it : items) {
        path.push_back(&it);
        if (&it == target || pathTo(it.children, target, path)) return true;
        path.pop_back();
    }
    return false;
}

bool matches(const OutlineItem& it, const char* filter)
{
    if (containsNoCase(it.title, filter)) return true;
    for (const auto& c : it.children)
        if (matches(c, filter)) return true;
    return false;
}

void searchBox(const char* id, char* buf, size_t n)
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint(id, "Search...", buf, n);
}

}

// --- outline

void OutlinePanel::draw(Viewer& v)
{
    ImGui::PushID(this);
    searchBox("##filter", mFilter, sizeof mFilter);
    const auto& items = v.outline();
    if (items.empty()) {
        ImGui::TextDisabled(v.state() == Viewer::State::Ready ? "This document has no contents." : "");
        ImGui::PopID();
        return;
    }
    const OutlineItem* current = nullptr;
    Dest at = v.position();
    findCurrent(items, at.valid() ? at : Dest{v.currentPage(), 0}, current);
    bool scrollToCurrent = current != mLastCurrent;
    mLastCurrent = current;
    // like Okular: all closed, but the way to the current section opens as
    // reading moves on
    std::vector<const OutlineItem*> path;
    if (current) pathTo(items, current, path);
    auto onPath = [&](const OutlineItem* it) { return std::find(path.begin(), path.end(), it) != path.end() && it != current; };

    float labelW = ImGui::CalcTextSize("00000").x;
    if (ImGui::BeginTable("##outline", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("page", ImGuiTableColumnFlags_WidthFixed, labelW);
        bool filtering = mFilter[0] != 0;
        std::function<void(const std::vector<OutlineItem>&)> rows = [&](const std::vector<OutlineItem>& list) {
            for (const auto& it : list) {
                if (filtering && !matches(it, mFilter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (it.children.empty()) f |= ImGuiTreeNodeFlags_Leaf;
                if (&it == current) f |= ImGuiTreeNodeFlags_Selected;
                if (filtering)
                    ImGui::SetNextItemOpen(true);
                else if (scrollToCurrent && onPath(&it))
                    ImGui::SetNextItemOpen(true);
                else
                    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
                bool open = ImGui::TreeNodeEx(&it, f, "%s", it.title.c_str());
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && it.dest.valid()) v.goTo(it.dest);
                if (ImGui::IsItemHovered() && ImGui::CalcTextSize(it.title.c_str()).x > ImGui::GetItemRectSize().x - ImGui::GetTreeNodeToLabelSpacing()) ImGui::SetTooltip("%s", it.title.c_str());
                if (&it == current && scrollToCurrent && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.3f);
                ImGui::TableNextColumn();
                if (it.dest.valid()) {
                    std::string l = v.pageLabel(it.dest.page);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(l.c_str()).x);
                    ImGui::TextUnformatted(l.c_str());
                }
                if (open && !it.children.empty()) {
                    ImGui::TreePush(&it);
                    rows(it.children);
                    ImGui::TreePop();
                }
            }
        };
        rows(items);
        ImGui::EndTable();
    }
    ImGui::PopID();
}

// --- thumbnails

void ThumbnailPanel::draw(Viewer& v)
{
    ImGui::PushID(this);
    searchBox("##filter", mFilter, sizeof mFilter);
    if (ImGui::IsItemEdited()) v.find(mFilter, v.findFlags());
    if (v.state() != Viewer::State::Ready) {
        ImGui::PopID();
        return;
    }
    bool filtering = mFilter[0] && v.findText() == mFilter;
    if (filtering && v.matchCount() == 0) ImGui::TextDisabled(v.searching() ? "Searching..." : "No pages match.");

    ImGui::BeginChild("##thumbs", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& st = ImGui::GetStyle();
    // sizes are at scale 1: they follow the UI's scale
    float sc = st.FontScaleDpi > 0 ? st.FontScaleDpi : 1.0f;
    // the wheel scrolls; with Ctrl it resizes
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0) {
        if (io.KeyCtrl)
            size = std::clamp(size * std::pow(1.1f, io.MouseWheel), 60.0f, 800.0f);
        else
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * ImGui::GetTextLineHeight() * 5);
    }

    std::vector<int> pages;
    for (int p = 0; p < v.pageCount(); p++)
        if (!filtering || v.pageHasMatches(p)) pages.push_back(p);

    float avail = ImGui::GetContentRegionAvail().x;
    float gap = st.ItemSpacing.x * 2;
    float px = size * sc; // the width on screen
    int cols = std::max(1, static_cast<int>((avail + gap) / (px + gap)));
    float cellW = (avail - gap * (cols - 1)) / cols;
    float pad = 6 * sc, labelH = ImGui::GetTextLineHeightWithSpacing();
    float imgW = cellW - 2 * pad;
    // row heights: the tallest page in each
    size_t rows = (pages.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols);
    std::vector<float> rowY(rows + 1, 0);
    for (size_t r = 0; r < rows; r++) {
        float h = 0;
        for (size_t k = r * cols; k < std::min(pages.size(), (r + 1) * cols); k++) {
            PageSize s = v.pageSize(pages[k]);
            h = std::max(h, static_cast<float>(imgW * s.h / s.w));
        }
        rowY[r + 1] = rowY[r] + h + 2 * pad + labelH + st.ItemSpacing.y;
    }

    int cur = v.currentPage();
    // follow the viewer's page unless the user moved it from here
    // (or when the layout or the document changed: the old scroll offset
    // means nothing then)
    bool relaid = cols != mLastCols || &v != mLastViewer;
    mLastCols = cols;
    mLastViewer = &v;
    if ((cur != mLastCurrent || relaid) && mDragPage < 0) {
        auto it = std::find(pages.begin(), pages.end(), cur);
        if (it != pages.end()) {
            size_t r = static_cast<size_t>(it - pages.begin()) / static_cast<size_t>(cols);
            float top = rowY[r], bottom = rowY[r + 1], sy = ImGui::GetScrollY(), wh = ImGui::GetWindowHeight();
            if (top < sy || bottom > sy + wh) ImGui::SetScrollY(top - wh / 3);
        }
    }
    mLastCurrent = cur;

    ImVec2 base = ImGui::GetCursorScreenPos();
    float sy = ImGui::GetScrollY(), wh = ImGui::GetWindowHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 accent = ImGui::GetColorU32(ImGuiCol_HeaderActive);
    size_t r0 = static_cast<size_t>(std::upper_bound(rowY.begin(), rowY.end(), sy) - rowY.begin());
    r0 = r0 > 0 ? r0 - 1 : 0;
    for (size_t r = r0; r < rows && rowY[r] < sy + wh; r++) {
        for (size_t k = r * cols; k < std::min(pages.size(), (r + 1) * cols); k++) {
            int p = pages[k];
            PageSize s = v.pageSize(p);
            float h = static_cast<float>(imgW * s.h / s.w);
            float rowH = rowY[r + 1] - rowY[r] - st.ItemSpacing.y;
            ImVec2 cell{base.x + (k % cols) * (cellW + gap), base.y + rowY[r]};
            ImVec2 i0{cell.x + pad, cell.y + pad + (rowH - labelH - 2 * pad - h) / 2}, i1{i0.x + imgW, i0.y + h};
            ImGui::SetCursorScreenPos(cell);
            ImGui::PushID(p);
            ImGui::InvisibleButton("##t", {cellW, rowH});
            bool hovered = ImGui::IsItemHovered();
            if (p == cur) dl->AddRectFilled(cell, {cell.x + cellW, cell.y + rowH}, accent, 4);
            else if (hovered) dl->AddRectFilled(cell, {cell.x + cellW, cell.y + rowH}, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4);
            dl->AddRectFilled({i0.x + 1, i0.y + 2}, {i1.x + 1, i1.y + 2}, IM_COL32(0, 0, 0, 70));
            dl->AddRectFilled(i0, i1, IM_COL32_WHITE);
            int tw = 0, th = 0;
            if (ImTextureID t = v.previewTexture(p, tw, th)) dl->AddImage(t, i0, i1);
            Rect vis;
            if (v.visibleRect(p, vis)) {
                double k2 = imgW / s.w;
                dl->AddRect({i0.x + static_cast<float>(vis.x0 * k2), i0.y + static_cast<float>(vis.y0 * k2)}, {i0.x + static_cast<float>(vis.x1 * k2), i0.y + static_cast<float>(vis.y1 * k2)}, IM_COL32(230, 200, 0, 255), 0.0f, 0, 2.0f);
            }
            std::string label = v.pageLabel(p);
            ImVec2 ls = ImGui::CalcTextSize(label.c_str());
            dl->AddText({cell.x + (cellW - ls.x) / 2, cell.y + rowH - labelH + st.ItemSpacing.y / 2}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

            // a click goes to the page; a drag moves the view over it
            auto pagePoint = [&](double& px, double& py) {
                px = std::clamp((io.MousePos.x - i0.x) / imgW * s.w, 0.0, s.w);
                py = std::clamp((io.MousePos.y - i0.y) / h * s.h, 0.0, s.h);
            };
            if (ImGui::IsItemActivated()) {
                mDragPage = p;
                mDragged = false;
            }
            if (ImGui::IsItemActive() && mDragPage == p && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3)) {
                mDragged = true;
                double px, py;
                pagePoint(px, py);
                v.centerOn(p, px, py);
            }
            if (ImGui::IsItemDeactivated() && mDragPage == p) {
                if (!mDragged) v.goToPage(p);
                mDragPage = -1;
            }
            if (hovered && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy({avail, std::max(1.0f, rowY[rows])});
    ImGui::EndChild();
    ImGui::PopID();
}

// --- shortcuts

void ShortcutEditor::cancel()
{
    mTarget = nullptr;
    mCaptured = {};
}

void ShortcutEditor::finish(Actions& a)
{
    Action* t = mTarget;
    int slot = mSlot;
    KeySeq seq = mCaptured;
    cancel();
    if (!t) return;
    std::vector<Action*> with;
    for (Action* c : a.conflicts(seq, t))
        with.push_back(c);
    if (!seq.empty() && t->keys[static_cast<size_t>(1 - slot)] == seq) {
        t->keys[static_cast<size_t>(1 - slot)] = {}; // moved within the action
    }
    if (!with.empty()) {
        mAsk = t;
        mAskSlot = slot;
        mAskSeq = seq;
        mAskWith = with;
        return;
    }
    t->keys[static_cast<size_t>(slot)] = seq;
    mChanged = true;
}

bool ShortcutEditor::draw(Actions& a)
{
    mChanged = false;
    ImGui::PushID(this);
    a.capturing = mTarget != nullptr;
    double now = ImGui::GetTime();
    if (mTarget) {
        ImGuiIO& io = ImGui::GetIO();
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
            auto key = static_cast<ImGuiKey>(k);
            if (!bindable(key) || !ImGui::IsKeyPressed(key, false)) continue;
            ImGuiKeyChord mods = io.KeyMods & ImGuiMod_Mask_;
            if (mCaptured.empty() && mods == 0 && key == ImGuiKey_Escape) {
                cancel();
                break;
            }
            if (mCaptured.empty() && mods == 0 && key == ImGuiKey_Backspace) {
                mCaptured = {};
                finish(a);
                break;
            }
            mCaptured.push(key | mods);
            mLastKey = now;
            if (mCaptured.n == KeySeq::kMax) {
                finish(a);
                break;
            }
        }
        if (mTarget && !mCaptured.empty() && now - mLastKey > Actions::kTimeout) finish(a);
    }

    searchBox("##filter", mFilter, sizeof mFilter);
    float keyW = ImGui::GetFontSize() * 9;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("##keys", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV, avail)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, keyW);
        ImGui::TableSetupColumn("Alternate", ImGuiTableColumnFlags_WidthFixed, keyW);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2);
        ImGui::TableHeadersRow();
        std::string category;
        for (const auto& up : a.all()) {
            Action& act = *up;
            std::string keys = toString(act.keys[0]) + " " + toString(act.keys[1]);
            if (!containsNoCase(act.name, mFilter) && !containsNoCase(act.category, mFilter) && !containsNoCase(keys, mFilter)) continue;
            if (act.category != category) {
                category = act.category;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", category.c_str());
            }
            ImGui::PushID(&act);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Indent();
            ImGui::TextUnformatted(act.name.c_str());
            ImGui::Unindent();
            for (int slot = 0; slot < 2; slot++) {
                ImGui::TableNextColumn();
                bool cap = mTarget == &act && mSlot == slot;
                std::string label = cap ? (mCaptured.empty() ? "Press keys..." : toString(mCaptured) + ", ...") : act.keys[static_cast<size_t>(slot)].empty() ? "None" : toString(act.keys[static_cast<size_t>(slot)]);
                if (cap) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                bool none = !cap && act.keys[static_cast<size_t>(slot)].empty();
                if (none) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushID(slot);
                if (ImGui::Button(label.c_str(), {-FLT_MIN, 0})) {
                    mTarget = &act;
                    mSlot = slot;
                    mCaptured = {};
                    a.clearPending();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(cap ? "Type the keys. Esc cancels, Backspace clears." : "Click to change");
                ImGui::PopID();
                if (none) ImGui::PopStyleColor();
                if (cap) ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            if (act.keys != act.defaults && ImGui::Button("Reset")) {
                act.keys = act.defaults;
                mChanged = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Default: %s", toString(act.defaults[0]).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Restore Defaults")) {
        a.resetAll();
        cancel();
        mChanged = true;
    }

    if (mAsk) ImGui::OpenPopup("Shortcut conflict");
    if (ImGui::BeginPopupModal("Shortcut conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string names;
        for (Action* c : mAskWith)
            names += (names.empty() ? "" : ", ") + c->name;
        ImGui::Text("%s is already used by: %s.", toString(mAskSeq).c_str(), names.c_str());
        ImGui::Text("Give it to \"%s\" instead?", mAsk ? mAsk->name.c_str() : "");
        if (ImGui::Button("Reassign")) {
            for (Action* c : mAskWith)
                for (KeySeq& k : c->keys)
                    if (k == mAskSeq) k = {};
            mAsk->keys[static_cast<size_t>(mAskSlot)] = mAskSeq;
            mChanged = true;
            mAsk = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            mAsk = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    a.capturing = mTarget != nullptr || mAsk != nullptr;
    ImGui::PopID();
    return mChanged;
}

}
