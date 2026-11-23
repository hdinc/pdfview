#pragma once

#include <imgui.h>

#include <pdfview/ScrollMotion.h>

namespace pdfview {

// Middle-button autoscroll speed, in pixels per second, for a pointer
// `offset` pixels from where autoscroll started: a dead zone, then an
// exponential rise to a limit. `scale` is the UI scale.
double autoscrollSpeed(double offset, double scale);

// The autoscroll marker centred on `at`: vertical arrows, plus horizontal
// ones when `horizontal`. The caller sets the clip rectangle.
void drawAutoscrollMarker(ImDrawList* dl, ImVec2 at, float scale, bool horizontal);

// Smooth wheel scrolling and middle-button autoscroll for a vertically
// scrolling ImGui window, such as a sidebar panel, with the document's
// gestures:
// - the wheel over the window glides (or jumps, without animations);
//   Ctrl+wheel is left to the caller
// - a middle click latches autoscroll until the next click, wheel, key or
//   focus loss; a middle drag autoscrolls until release. Clicks that stop
//   it don't reach the window's items.
// - dragging the scrollbar, or anything else that moves the scroll, takes
//   over at once; so does switching to another window or not drawing it
class PanelScroll {
public:
    // Call every frame the window is drawn, right after its Begin
    // (BeginChild, or BeginTable with ImGuiTableFlags_ScrollY), while it is
    // the current window.
    void update(bool animations);
    void cancel();
    // the host should keep drawing frames
    bool needsFrames() const;

private:
    ScrollMotion mMotion;
    double mPos = 0; // the scroll set last, unrounded
    ImGuiID mWindow = 0;
    int mFrame = -1;
    bool mAuto = false;
    bool mHeld = false; // the middle button that started it is down
    bool mDragged = false;
    ImVec2 mOrigin{};
};

} // namespace pdfview
