#include <pdfview/Panels.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

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

void OutlinePanel::draw(Viewer& v, ImFont* boldFont)
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
    if (v.mOutlinePin.item && v.mOutlinePin.y == v.mView.y) {
        current = v.mOutlinePin.item;
    } else {
        v.mOutlinePin = {};
        findCurrent(items, at.valid() ? at : Dest{v.currentPage(), 0}, current);
    }
    bool scrollToCurrent = current != mLastCurrent;
    mLastCurrent = current;
    // like Okular: all closed, but the way to the current section opens as
    // reading moves on
    std::vector<const OutlineItem*> path;
    if (current) pathTo(items, current, path);
    auto onPath = [&](const OutlineItem* it) { return std::find(path.begin(), path.end(), it) != path.end() && it != current; };

    float labelW = ImGui::CalcTextSize("00000").x;
    ImGui::PushStyleVar(ImGuiStyleVar_TreeLinesSize, std::max(1.0f, ImGui::GetStyle().TreeLinesSize));
    ImGui::PushStyleVarY(ImGuiStyleVar_CellPadding, 0);
    if (ImGui::BeginTable("##outline", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("page", ImGuiTableColumnFlags_WidthFixed, labelW);
        bool filtering = mFilter[0] != 0;
        ImGuiID treeSeed = ImGui::GetID(filtering ? "##filtered-tree" : "##tree");
        auto storageId = [&](const OutlineItem* item) { return ImHashData(&item, sizeof item, treeSeed); };
        const OutlineItem* branch = nullptr;
        std::optional<bool> branchOpen;
        auto drawMenu = [&](const OutlineItem* item) {
            bool hasChildren = item && !item->children.empty();
            if (item) {
                if (ImGui::MenuItem("Expand Whole Section", nullptr, false, hasChildren && !filtering)) {
                    branch = item;
                    branchOpen = true;
                }
                if (ImGui::MenuItem("Collapse Whole Section", nullptr, false, hasChildren && !filtering)) {
                    branch = item;
                    branchOpen = false;
                }
            }
            if (ImGui::MenuItem("Expand All", nullptr, false, !filtering)) branchOpen = true;
            if (ImGui::MenuItem("Collapse All", nullptr, false, !filtering)) branchOpen = false;
        };
        std::function<void(const std::vector<OutlineItem>&)> rows = [&](const std::vector<OutlineItem>& list) {
            for (const auto& it : list) {
                if (filtering && !matches(it, mFilter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow
                    | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DrawLinesToNodes;
                if (it.children.empty()) f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                else ImGui::SetNextItemStorageID(storageId(&it));
                // BeginPopupContextItem uses the tree item's ID for the popup.
                bool menuOpen = ImGui::IsPopupOpen(ImGui::GetID(&it), ImGuiPopupFlags_None);
                bool isCurrent = &it == current;
                if (isCurrent || menuOpen) f |= ImGuiTreeNodeFlags_Selected;
                if (filtering)
                    ImGui::SetNextItemOpen(true);
                else if (scrollToCurrent && onPath(&it))
                    ImGui::SetNextItemOpen(true);
                else
                    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
                bool useBoldFont = isCurrent && boldFont;
                if (useBoldFont) ImGui::PushFont(boldFont, 0.0f);
                ImVec2 origin = ImGui::GetCursorScreenPos();
                float labelX = origin.x + ImGui::GetTreeNodeToLabelSpacing();
                float right = origin.x + ImGui::GetContentRegionAvail().x;
                ImVec2 titleSize = ImGui::CalcTextSize(it.title.c_str());
                bool truncated = titleSize.x > right - labelX;
                if (menuOpen) ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                bool open = ImGui::TreeNodeEx(&it, f, "%s", truncated ? "" : it.title.c_str());
                if (menuOpen) ImGui::PopStyleColor();
                if (truncated && labelX < right && ImGui::IsItemVisible()) {
                    const char* title = it.title.data();
                    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), {labelX, origin.y}, {right, origin.y + titleSize.y},
                                              right, title, title + it.title.size(), &titleSize);
                }
                if (useBoldFont) ImGui::PopFont();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && it.dest.valid() && v.mLaidOut) {
                    v.goTo(it.dest);
                    v.mOutlinePin = {&it, v.mView.y};
                }
                if (ImGui::BeginPopupContextItem()) {
                    drawMenu(&it);
                    ImGui::EndPopup();
                }
                if (truncated) ImGui::SetItemTooltip("%s", it.title.c_str());
                if (isCurrent && scrollToCurrent && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.3f);
                ImGui::TableNextColumn();
                if (it.dest.valid()) {
                    std::string l = v.pageLabel(it.dest.page);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(l.c_str()).x);
                    ImGui::TextUnformatted(l.c_str());
                }
                if (open && !it.children.empty()) {
                    rows(it.children);
                    ImGui::TreePop();
                }
            }
        };
        rows(items);
        if (ImGui::BeginPopupContextWindow("##outline-menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            drawMenu(nullptr);
            ImGui::EndPopup();
        }
        if (branchOpen) {
            // Storage IDs are independent of depth, so changes reach collapsed descendants.
            ImGuiStorage* storage = ImGui::GetStateStorage();
            auto setOpen = [&](auto&& self, const OutlineItem& item) -> void {
                if (item.children.empty()) return;
                storage->SetBool(storageId(&item), *branchOpen);
                for (const auto& child : item.children) self(self, child);
            };
            if (branch) setOpen(setOpen, *branch);
            else for (const auto& item : items) setOpen(setOpen, item);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

// --- thumbnails

void ThumbnailPanel::updateDrag(Viewer& v)
{
    if (!mDrag.id) return;
    int frame = ImGui::GetFrameCount();
    if (mLastViewer != &v || mDrag.frame + 1 < frame || ImGui::GetIO().AppFocusLost
        || v.state() != Viewer::State::Ready || !v.mLaidOut || ImGui::GetActiveID() != mDrag.id) {
        if (ImGui::GetActiveID() == mDrag.id) ImGui::ClearActiveID();
        mDrag = {};
        return;
    }

    // Keep the press alive even after the original thumbnail is clipped.
    ImGui::KeepAliveID(mDrag.id);
    mDrag.frame = frame;
    if (ImGui::IsMousePosValid() && ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 3)) {
        mDrag.moved = true;
        ImVec2 pointer = ImGui::GetMousePos();
        double dx = (pointer.x - mDrag.pointer.x) * mDrag.pointsPerPixel;
        double dy = (pointer.y - mDrag.pointer.y) * mDrag.pointsPerPixel;
        mDrag.pointer = pointer;
        if (dx != 0 || dy != 0) {
            int previousPage = mDrag.page;
            mDrag.y += dy;
            // Traverse paper heights in reading order, independent of the thumbnail grid.
            if (dy > 0) {
                while (mDrag.page + 1 < v.pageCount() && mDrag.y > v.pageSize(mDrag.page).h) {
                    mDrag.y -= v.pageSize(mDrag.page).h;
                    ++mDrag.page;
                }
                mDrag.y = std::min(mDrag.y, v.pageSize(mDrag.page).h);
            } else if (dy < 0) {
                while (mDrag.page > 0 && mDrag.y < 0) {
                    --mDrag.page;
                    mDrag.y += v.pageSize(mDrag.page).h;
                }
                mDrag.y = std::max(0.0, mDrag.y);
            }
            if (dx != 0 || mDrag.page != previousPage)
                mDrag.x = std::clamp(mDrag.x + dx, 0.0, v.pageSize(mDrag.page).w);
            v.centerOn(mDrag.page, mDrag.x, mDrag.y);
            mLastCurrent = v.currentPage();
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!mDrag.moved && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            v.goToPage(mDrag.pressedPage);
            // A thumbnail click leaves the grid's scroll position alone.
            mLastCurrent = v.currentPage();
        }
        ImGui::ClearActiveID();
        mDrag = {};
    }
}

void ThumbnailPanel::draw(Viewer& v)
{
    updateDrag(v);
    ImGui::PushID(this);
    searchBox("##filter", mFilter, sizeof mFilter);
    if (ImGui::IsItemEdited()) v.find(mFilter, v.findFlags());
    if (v.state() != Viewer::State::Ready) {
        ImGui::PopID();
        return;
    }
    bool filtering = mFilter[0] && v.findText() == mFilter;
    if (filtering && v.matchCount() == 0) ImGui::TextDisabled(v.searching() ? "Searching..." : "No pages match.");

    ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& st = ImGui::GetStyle();
    // sizes are at scale 1: they follow the UI's scale
    float sc = st.FontScaleDpi > 0 ? st.FontScaleDpi : 1.0f;
    std::vector<int> pages;
    for (int p = 0; p < v.pageCount(); p++)
        if (!filtering || v.pageHasMatches(p)) pages.push_back(p);

    // Keep this frame's layout consistent when a menu item changes its settings.
    bool spaced = gaps;
    bool labeled = showLabels;
    float gap = spaced ? st.ItemSpacing.x * 2 : 0;
    float rowGap = spaced ? st.ItemSpacing.y : 0;
    float px = size * sc; // the width on screen
    float pad = spaced ? 6 * sc : 0;
    float labelH = labeled ? ImGui::GetTextLineHeightWithSpacing() : 0;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // A bordered child uses the window padding. ImGui rounds its size to pixels.
    ImVec2 padding = st.ChildBorderSize > 0 ? st.WindowPadding : ImVec2{};
    float height = std::floor(std::max(IMGUI_WINDOW_HARD_MIN_SIZE, avail.y)) - 2 * padding.y;
    // Keep the gutter width even when the scrollbar is hidden so the
    // thumbnails don't jump in size as they start or stop overflowing.
    float width = std::max(1.0f, std::floor(std::max(IMGUI_WINDOW_HARD_MIN_SIZE, avail.x))
        - 2 * padding.x - st.ScrollbarSize);
    int cols = std::max(1, static_cast<int>((width + gap) / (px + gap)));
    float cellW = (width - gap * (cols - 1)) / cols;
    float imgW = std::max(1.0f, cellW - 2 * pad);
    float rowPadding = 2 * pad + labelH + rowGap;
    auto imageHeight = [&](int page) {
        PageSize s = v.pageSize(page);
        return static_cast<float>(imgW * s.h / s.w);
    };
    size_t rows = (pages.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols);
    std::vector<float> rowY(rows + 1, 0);
    for (size_t r = 0; r < rows; r++) {
        float h = 0;
        for (size_t k = r * cols; k < std::min(pages.size(), (r + 1) * cols); k++)
            h = std::max(h, imageHeight(pages[k]));
        rowY[r + 1] = rowY[r] + h + rowPadding;
    }
    float contentHeight = std::max(1.0f, std::floor(rowY.back()));
    bool scrolling = rows && contentHeight > height;
    ImGui::SetNextWindowContentSize({0, contentHeight});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse
        | (scrolling ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_NoScrollbar);
    if (!ImGui::BeginChild("##thumbs", {0, 0}, ImGuiChildFlags_Borders, flags)) {
        ImGui::EndChild();
        ImGui::PopID();
        return;
    }
    // Ctrl+wheel and the context menu update the next layout.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0) {
        if (io.KeyCtrl)
            size = std::clamp(size * std::pow(1.1f, io.MouseWheel), 60.0f, 800.0f);
        else
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * ImGui::GetTextLineHeight() * 5);
    }
    if (ImGui::BeginPopupContextWindow("##thumbmenu", ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::MenuItem("Show Page Numbers", nullptr, &showLabels);
        ImGui::MenuItem("Gaps Between Thumbnails", nullptr, &gaps);
        ImGui::EndPopup();
    }
    auto imageTop = [&](size_t row, float height) {
        float tallest = rowY[row + 1] - rowY[row] - rowPadding;
        return rowY[row] + pad + (spaced ? (tallest - height) / 2 : 0);
    };

    auto rowOf = [&](int page) {
        auto it = std::lower_bound(pages.begin(), pages.end(), page);
        return it != pages.end() && *it == page ? static_cast<size_t>(it - pages.begin()) / cols : rows;
    };

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float wh = window->InnerRect.GetHeight();
    float maxScroll = scrolling ? ImGui::GetScrollMaxY() : 0;
    float actualScroll = ImGui::GetScrollY();
    float sy = std::min(actualScroll, maxScroll);
    ImVec2 base = ImGui::GetCursorScreenPos();
    base.y += actualScroll;
    const ImRect clip = window->ClipRect;

    int cur = v.currentPage();
    bool otherDoc = &v != mLastViewer;
    bool resized = cols != mLastCols || cellW != mLastCellWidth || rowPadding != mLastRowPadding;
    bool follow = (otherDoc || cur != mLastCurrent) && !mDrag.id;
    size_t anchorRow = otherDoc ? rows : rowOf(mAnchorPage);
    bool anchored = resized && !follow && anchorRow < rows;
    if (anchored) {
        float h = imageHeight(mAnchorPage);
        float top = base.y + imageTop(anchorRow, h);
        // If the page shrank, keep at least a pixel in view where the scroll range permits.
        float targetTop = std::max(mAnchorTop, clip.Min.y - h + 1);
        sy = top - targetTop;
    } else if (follow) {
        if (size_t r = rowOf(cur); r < rows) {
            if (rowY[r] < sy || rowY[r + 1] > sy + wh)
                sy = rowY[r] - wh / 3;
        }
    }
    sy = std::clamp(std::round(sy), 0.0f, maxScroll);
    if (sy != actualScroll) ImGui::SetScrollY(sy);
    if (!anchored && (follow || anchorRow == rows || std::abs(sy - mLastScroll) >= 1))
        mAnchorPage = -1;
    mLastScroll = sy;
    mLastCurrent = cur;
    mLastCols = cols;
    mLastCellWidth = cellW;
    mLastRowPadding = rowPadding;
    mLastViewer = &v;

    // ImGui applies SetScrollY at the next BeginChild. Position this frame's
    // items at the target scroll so the grid stays anchored during the drag.
    base.y -= sy;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 accent = ImGui::GetColorU32(ImGuiCol_HeaderActive);
    ImU32 pageBorder = ImGui::GetColorU32(ImGuiCol_Border);
    float framebufferScale = std::max(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    double pixelWidth = imgW * (framebufferScale > 0 ? framebufferScale : 1);
    size_t r0 = static_cast<size_t>(std::upper_bound(rowY.begin(), rowY.end(), clip.Min.y - base.y) - rowY.begin());
    r0 = r0 > 0 ? r0 - 1 : 0;
    for (size_t r = r0; r < rows && rowY[r] < clip.Max.y - base.y; r++) {
        for (size_t k = r * cols; k < std::min(pages.size(), (r + 1) * cols); k++) {
            int p = pages[k];
            PageSize s = v.pageSize(p);
            float h = imageHeight(p);
            float rowH = rowY[r + 1] - rowY[r] - rowGap;
            ImVec2 cell{base.x + (k % cols) * (cellW + gap), base.y + rowY[r]};
            ImVec2 i0{cell.x + pad, base.y + imageTop(r, h)}, i1{i0.x + imgW, i0.y + h};
            if (mAnchorPage < 0 && i0.y < clip.Max.y && i1.y > clip.Min.y) {
                mAnchorPage = p;
                mAnchorTop = i0.y;
            }
            ImGui::SetCursorScreenPos(cell);
            ImGui::PushID(p);
            ImGui::InvisibleButton("##t", {cellW, rowH});
            bool hovered = ImGui::IsItemHovered();
            if (spaced) {
                if (p == cur) dl->AddRectFilled(cell, {cell.x + cellW, cell.y + rowH}, accent, 4);
                else if (hovered) dl->AddRectFilled(cell, {cell.x + cellW, cell.y + rowH}, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4);
                dl->AddRectFilled({i0.x + 1, i0.y + 2}, {i1.x + 1, i1.y + 2}, IM_COL32(0, 0, 0, 70));
            }
            dl->AddRectFilled(i0, i1, IM_COL32_WHITE);
            int tw = 0, th = 0;
            if (ImTextureID t = v.pageTexture(p, pixelWidth, tw, th)) dl->AddImage(t, i0, i1);
            dl->AddRect(i0, i1, pageBorder);
            Rect vis;
            bool visible = v.visibleRect(p, vis);
            if (visible) {
                double scale = imgW / s.w;
                ImVec2 a{i0.x + static_cast<float>(vis.x0 * scale), i0.y + static_cast<float>(vis.y0 * scale)};
                ImVec2 b{i0.x + static_cast<float>(vis.x1 * scale), i0.y + static_cast<float>(vis.y1 * scale)};
                dl->AddRectFilled(a, b, IM_COL32(230, 200, 0, 56));
                dl->AddRect(a, b, IM_COL32(230, 200, 0, 255), 0.0f, 0, 2.0f);
            }
            if (!spaced && (p == cur || hovered)) {
                ImU32 color = p == cur ? accent : ImGui::GetColorU32(ImGuiCol_HeaderHovered);
                float inset = std::min({sc, imgW / 2, h / 2});
                dl->AddRect({i0.x + inset, i0.y + inset}, {i1.x - inset, i1.y - inset}, color, 0, 0, 2 * inset);
            }
            if (labeled) {
                std::string label = v.pageLabel(p);
                ImVec2 ls = ImGui::CalcTextSize(label.c_str());
                dl->AddText({cell.x + (cellW - ls.x) / 2, cell.y + rowH - labelH + rowGap / 2}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            } else if (hovered) {
                ImGui::SetItemTooltip("%s", v.pageLabel(p).c_str());
            }

            if (ImGui::IsItemActivated() && v.mLaidOut) {
                double pointsPerPixel = s.w / imgW;
                double x = (io.MousePos.x - i0.x) * pointsPerPixel;
                double y = (io.MousePos.y - i0.y) * pointsPerPixel;
                int page = p;
                if (visible && vis.contains(x, y)) {
                    // Grab the current viewport, preserving the pointer's offset from its center.
                    page = cur;
                    x = v.mView.x + v.mView.w / v.mView.zoom / 2 - v.mLayout.left(page);
                    y = v.mView.y + v.mView.h / v.mView.zoom / 2 - v.mLayout.top[static_cast<size_t>(page)];
                } else {
                    x = std::clamp(x, 0.0, s.w);
                    y = std::clamp(y, 0.0, s.h);
                }
                mDrag = {ImGui::GetItemID(), ImGui::GetFrameCount(), p, page, io.MousePos, pointsPerPixel, x, y};
            }
            if (hovered && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        }
    }
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
