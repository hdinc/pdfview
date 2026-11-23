#pragma once

#include <string>
#include <vector>

#include <pdfview/Actions.h>
#include <pdfview/PanelScroll.h>
#include <pdfview/Viewer.h>

namespace pdfview {

// the document's outline: a tree with page labels, filtered by a search
// box; the section holding the current page is highlighted; clicking an
// entry goes there
class OutlinePanel {
public:
    // Fills the rest of the current window. Pass the current UI bold face if available.
    void draw(Viewer& v, ImFont* boldFont = nullptr);
    bool needsFrames() const { return mScroll.needsFrames(); }

private:
    PanelScroll mScroll;
    char mFilter[128] = "";
    const OutlineItem* mLastCurrent = nullptr;
};

// page thumbnails in as many columns as fit, with the current page and the
// part in view marked. Click to go to a page, drag to move the view,
// Ctrl+wheel to resize and right-click for display options. The search box
// filters to matching pages without moving the document view.
class ThumbnailPanel {
public:
    void draw(Viewer& v);
    bool needsFrames() const { return mScroll.needsFrames(); }
    float size = 150; // thumbnail width in pixels at scale 1 (the style's FontScaleDpi)
    bool showLabels = true;
    bool gaps = true;

private:
    void updateDrag(Viewer& v);
    PanelScroll mScroll;
    int mLastCurrent = -1;
    int mLastCols = 0;
    float mLastCellWidth = 0;
    float mLastRowPadding = 0;
    float mLastScroll = 0;
    float mColumnWheelRemainder = 0;
    const Viewer* mLastViewer = nullptr;
    int mAnchorPage = -1;
    float mAnchorTop = 0; // screen Y of the anchor thumbnail's top edge
    struct {
        ImGuiID id = 0;
        int frame = -1;
        int pressedPage = -1;
        int page = -1;
        ImVec2 pointer{};
        double pointsPerPixel = 0;
        double x = 0;
        double y = 0;
        bool moved = false;
    } mDrag;
};

// edits an Actions registry's keys: search, a table of actions with their
// keys and alternates, a click on a cell captures a new sequence (Esc
// cancels, Backspace clears), conflicts are asked about
class ShortcutEditor {
public:
    // true when a binding changed (for the host to save)
    bool draw(Actions& a);
    // capturing keys: the host should keep drawing frames
    bool capturing() const { return mTarget != nullptr; }
    void cancel();

private:
    void finish(Actions& a);

    char mFilter[128] = "";
    Action* mTarget = nullptr;
    int mSlot = 0;
    KeySeq mCaptured;
    double mLastKey = 0;
    // a sequence waiting for the user's word on a conflict
    Action* mAsk = nullptr;
    int mAskSlot = 0;
    KeySeq mAskSeq;
    std::vector<Action*> mAskWith;
    bool mChanged = false;
};

}
