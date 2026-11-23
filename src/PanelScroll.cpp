#include <pdfview/PanelScroll.h>

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

namespace pdfview {

static float uiScale()
{
    float s = ImGui::GetStyle().FontScaleDpi;
    return s > 0 ? s : 1.0f;
}

double autoscrollSpeed(double offset, double scale)
{
    constexpr double initialSensitivity = 5;
    constexpr double growthDistance = 200;
    constexpr double speedLimit = 5000;
    double beyondDeadZone = std::clamp(std::abs(offset) / scale - 10, 0.0, speedLimit / initialSensitivity);
    double speed = initialSensitivity * growthDistance * std::expm1(beyondDeadZone / growthDistance);
    return std::copysign(std::min(speed, speedLimit) * scale, offset);
}

void drawAutoscrollMarker(ImDrawList* dl, ImVec2 c, float scale, bool horizontal)
{
    float radius = 13 * scale;
    float arrow = 4 * scale;
    float offset = 5 * scale;
    ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddCircleFilled(c, radius, ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddCircle(c, radius, ink, 0, scale);
    dl->AddCircleFilled(c, 1.6f * scale, ink);
    dl->AddTriangleFilled({c.x, c.y - offset - arrow}, {c.x - arrow, c.y - offset}, {c.x + arrow, c.y - offset}, ink);
    dl->AddTriangleFilled({c.x, c.y + offset + arrow}, {c.x - arrow, c.y + offset}, {c.x + arrow, c.y + offset}, ink);
    if (horizontal) {
        dl->AddTriangleFilled({c.x - offset - arrow, c.y}, {c.x - offset, c.y - arrow}, {c.x - offset, c.y + arrow}, ink);
        dl->AddTriangleFilled({c.x + offset + arrow, c.y}, {c.x + offset, c.y - arrow}, {c.x + offset, c.y + arrow}, ink);
    }
}

void PanelScroll::cancel()
{
    mMotion.stop();
    mAuto = false;
    mHeld = false;
}

bool PanelScroll::needsFrames() const
{
    // a window no longer drawn has stopped
    return (mMotion.active() || mAuto) && mFrame + 1 >= ImGui::GetFrameCount();
}

void PanelScroll::update(bool animations)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImGuiIO& io = ImGui::GetIO();
    int frame = ImGui::GetFrameCount();
    // ImGui reads this at the next NewFrame, so from then on the wheel over
    // the window is ours (Begin resets the flags each frame).
    w->Flags |= ImGuiWindowFlags_NoScrollWithMouse;
    bool scrollbar = ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(w, ImGuiAxis_Y);
    if (w->ID != mWindow || mFrame + 1 != frame || io.AppFocusLost || scrollbar) cancel();
    mWindow = w->ID;
    mFrame = frame;

    float scroll = w->Scroll.y;
    float maxScroll = w->ScrollMax.y;
    // ImGui rounds the scroll; something else moved it if it differs more.
    if (std::abs(scroll - std::round(mPos)) > 1 || (!mMotion.active() && !mAuto)) {
        mMotion.stop();
        mPos = scroll;
    }
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    float sc = uiScale();
    float dt = std::min(io.DeltaTime, 0.05f);

    bool stopped = false;
    if (mAuto) {
        stopped = io.MouseWheel != 0 || io.MouseWheelH != 0 || ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart && !stopped; ++key)
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) stopped = true;
        if (mHeld && ImGui::IsMousePosValid()) {
            ImVec2 d{io.MousePos.x - mOrigin.x, io.MousePos.y - mOrigin.y};
            if (d.x * d.x + d.y * d.y > 9) mDragged = true;
        }
        if (mHeld && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            // A plain click latches autoscroll; releasing after a drag stops it.
            mHeld = false;
            if (mDragged) stopped = true;
        }
        if (stopped) mAuto = false;
    }
    if (!mAuto && !stopped && hovered && maxScroll > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        mMotion.stop();
        mAuto = mHeld = true;
        mDragged = false;
        mOrigin = io.MousePos;
        mPos = scroll;
    }
    if (mAuto) {
        if (ImGui::IsMousePosValid())
            mPos = std::clamp(mPos + autoscrollSpeed(io.MousePos.y - mOrigin.y, sc) * dt, 0.0, double(maxScroll));
        ImGui::SetScrollY(w, static_cast<float>(mPos));
        // The click that stops it goes to nothing else (buttons test ownership).
        ImGuiID id = w->GetID("##autoscroll");
        for (ImGuiKey k : {ImGuiKey_MouseLeft, ImGuiKey_MouseRight, ImGuiKey_MouseMiddle}) ImGui::SetKeyOwner(k, id);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImDrawList* dl = ImGui::GetForegroundDrawList(w->Viewport);
        dl->PushClipRect(w->Rect().Min, w->Rect().Max, true);
        drawAutoscrollMarker(dl, mOrigin, sc, false);
        dl->PopClipRect();
        return;
    }

    if (hovered && io.MouseWheel != 0 && !io.KeyCtrl && maxScroll > 0) {
        // ImGui's own wheel step for windows
        float step = ImTrunc(ImMin(5 * w->FontRefSize, w->InnerRect.GetHeight() * 0.67f));
        double from = mMotion.active() ? mMotion.target().y : mPos;
        double to = std::clamp(from - io.MouseWheel * step, 0.0, double(maxScroll));
        if (animations) {
            ScrollMotion::Params p{ScrollMotion::kStepStiffness, 1, ScrollMotion::kMaxSpeed * sc};
            mMotion.moveTo({0, mPos}, {0, to}, p);
        } else {
            mMotion.stop();
            mPos = to;
            ImGui::SetScrollY(w, static_cast<float>(mPos));
        }
    }
    if (mMotion.active()) {
        ScrollMotion::Vec pos{0, mPos};
        if (animations) {
            mMotion.step(pos, dt, 0.5);
        } else {
            pos = mMotion.target();
            mMotion.stop();
        }
        mPos = pos.y;
        ImGui::SetScrollY(w, static_cast<float>(mPos));
    }
}

} // namespace pdfview
