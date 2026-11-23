#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include <pdfview/PanelScroll.h>

using namespace pdfview;

namespace {

// a headless ImGui with one tall scrolling child at (0, 0) - (300, 400)
struct Gui {
    ImGuiContext* ctx;
    PanelScroll scroll;
    bool animations = true;
    float scrollY = 0;
    float scrollMax = 0;
    bool buttonClicked = false;

    Gui()
    {
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        unsigned char* px;
        int w, h;
        io.Fonts->AddFontDefault();
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        io.Fonts->SetTexID(ImTextureID(1));
        frame();
    }
    ~Gui() { ImGui::DestroyContext(ctx); }

    void frame(std::function<void(ImGuiIO&)> input = {}, float dt = 1 / 60.0f)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = dt;
        if (input) input(io);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::BeginChild("panel", {300, 400});
        scroll.update(animations);
        if (ImGui::Button("button", {200, 40})) buttonClicked = true;
        ImGui::Dummy({10, 5000});
        scrollY = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    }
    void frames(int n)
    {
        for (int i = 0; i < n; i++) frame();
    }
};

} // namespace

TEST_CASE("PanelScroll: a wheel notch over the panel glides there")
{
    Gui g;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 100); });
    g.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0, -1); });
    g.frame();
    float first = g.scrollY;
    g.frames(40);
    float landed = g.scrollY;
    CHECK(first > 0);
    CHECK(first < landed); // not all at once
    CHECK(landed > 30);
    CHECK(!g.scroll.needsFrames());
    // a notch back returns to the top
    g.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0, 1); });
    g.frames(40);
    CHECK(g.scrollY == 0);
}

TEST_CASE("PanelScroll: without animations a notch jumps")
{
    Gui g;
    g.animations = false;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 100); });
    g.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0, -1); });
    g.frame();
    float jumped = g.scrollY;
    g.frames(5);
    CHECK(jumped > 30);
    CHECK(g.scrollY == jumped);
}

TEST_CASE("PanelScroll: the wheel outside the panel does nothing")
{
    Gui g;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(600, 100); });
    g.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0, -1); });
    g.frames(20);
    CHECK(g.scrollY == 0);
}

TEST_CASE("PanelScroll: a middle click latches autoscroll and a click stops it unseen")
{
    Gui g;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 200); });
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true); });
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false); });
    CHECK(g.scroll.needsFrames());
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 300); }); // below the origin
    g.frames(30);
    float moved = g.scrollY;
    CHECK(moved > 50);
    // a left click over the button stops it and doesn't press the button
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(50, 20); });
    float stopAt = g.scrollY;
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); });
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); });
    g.frames(5);
    CHECK(!g.scroll.needsFrames());
    CHECK(!g.buttonClicked);
    CHECK(g.scrollY - stopAt < 200);
    float after = g.scrollY;
    g.frames(10);
    CHECK(g.scrollY == after);
}

TEST_CASE("PanelScroll: a middle drag autoscrolls until release")
{
    Gui g;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 100); });
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true); });
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 250); });
    g.frames(20);
    CHECK(g.scrollY > 50);
    g.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false); });
    g.frames(2);
    CHECK(!g.scroll.needsFrames());
    float after = g.scrollY;
    g.frames(10);
    CHECK(g.scrollY == after);
}

TEST_CASE("PanelScroll: a scroll set elsewhere takes over a glide")
{
    Gui g;
    g.frame([](ImGuiIO& io) { io.AddMousePosEvent(100, 100); });
    g.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0, -3); });
    g.frame();
    // like a jump to the current item
    ImGui::SetCurrentContext(g.ctx);
    ImGuiWindow* panel = nullptr;
    for (ImGuiWindow* w : g.ctx->Windows)
        if (std::string(w->Name).find("panel") != std::string::npos) panel = w;
    REQUIRE(panel);
    ImGui::SetScrollY(panel, 2000);
    g.frames(30);
    CHECK(g.scrollY == 2000);
    CHECK(!g.scroll.needsFrames());
}
