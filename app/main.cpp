// pdfview-app: a standalone PDF viewer on the pdfview widget.
//   pdfview-app [file.pdf ...]     (or drop PDFs on the window)
#include <glad/glad.h>

#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <Hd/Gui.h>
#include <Hd/Platform.h>
#include <Hd/Window.h>

#include "App.h"

namespace {
App* gApp = nullptr;
}

int main(int argc, char** argv)
{
    Hd::Window::setName("pdfview");
    Hd::Window::setSize(1300, 1000);
    auto& window = Hd::Window::getInstance();
    window.VSync(true);
    GLFWwindow* glfwWin = window.Id();

    // the look (scale, font, theme) comes from Hd; App sets it from its
    // settings
    Hd::Gui gui(&window);
    ImGui::GetIO().IniFilename = nullptr;

    App app(window, gui, Hd::Platform::args(argc, argv));
    gApp = &app;
    glfwSetDropCallback(glfwWin, [](GLFWwindow*, int n, const char** paths) {
        // UTF-8 paths from GLFW
        if (gApp && n > 0) gApp->open(std::vector<std::string>(paths, paths + n));
    });
    gui.Functions.add([&] { app.frame(); });

    // wait for events unless something is moving; wake once a second to
    // look for changed files (and the system's theme)
    while (!window.ShouldClose()) {
        gui.waitEvents(app.needsFrames(), 1.0);
        glClearColor(0.1f, 0.1f, 0.11f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        gui.Draw();
        window.SwapBuffers();
    }
    gApp = nullptr;
}
