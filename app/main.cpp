// pdfview-app: a standalone PDF viewer on the pdfview widget.
//   pdfview-app file.pdf     (or drop a PDF on the window)
#include <glad/glad.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <string>

#include <GLFW/glfw3.h>
#include <Hd/Gui.h>
#include <Hd/Window.h>

#include <pdfview/Viewer.h>

namespace {

pdfview::Viewer* gViewer = nullptr;

void loadFont(float size)
{
    ImGuiIO& io = ImGui::GetIO();
    for (const char* p : {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
        if (!std::filesystem::exists(p)) continue;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(p, size)) io.FontDefault = f;
        return;
    }
}

void setTitle(GLFWwindow* w, const std::string& path)
{
    std::string t = path.empty() ? "pdfview" : std::filesystem::path(path).filename().string() + " - pdfview";
    glfwSetWindowTitle(w, t.c_str());
}

}

int main(int argc, char** argv)
{
    Hd::Window::setName("pdfview");
    Hd::Window::setSize(1100, 1000);
    auto& window = Hd::Window::getInstance();
    window.VSync(true);
    GLFWwindow* glfwWin = window.Id();

    Hd::Gui gui(&window);
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;
    loadFont(17);

    pdfview::Viewer viewer;
    gViewer = &viewer;
    viewer.setWake([] { glfwPostEmptyEvent(); });
    if (argc > 1) {
        viewer.open(argv[1]);
        setTitle(glfwWin, argv[1]);
    }
    glfwSetDropCallback(glfwWin, [](GLFWwindow* w, int n, const char** paths) {
        if (n < 1) return;
        gViewer->open(paths[0]);
        setTitle(w, paths[0]);
    });

    gui.Functions.add([&] {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        viewer.draw("viewer");
        ImGui::End();
    });

    // wait for events unless the viewer is busy. ImGui takes queued input a
    // step per frame (a click is a press, then a release), so every wake up
    // draws a few frames.
    int extra = 0;
    while (!window.ShouldClose()) {
        if (viewer.needsFrames() || extra > 0) {
            window.PollEvents();
            extra = std::max(0, extra - 1);
        } else {
            window.WaitEvents();
            extra = 3;
        }
        if (ImGui::GetIO().MouseDown[0] || ImGui::GetIO().MouseDown[2]) extra = 3;
        glClearColor(0.1f, 0.1f, 0.11f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        gui.Draw();
        window.SwapBuffers();
    }
    gViewer = nullptr;
}
