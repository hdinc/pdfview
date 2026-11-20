# pdfview: a PDF viewer widget for Dear ImGui (OpenGL 3 backend) and a small
# standalone viewer built on it. Needs the imgui and glad targets (Hd has
# them) and poppler-cpp.
if(NOT TARGET PkgConfig::POPPLER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-cpp)
endif()
find_package(Threads REQUIRED)

# the parts without GL or ImGui, which the tests use too
add_library(pdfview_core STATIC
    src/Document.cpp
    src/Layout.cpp
    src/RenderWorker.cpp
)
target_include_directories(pdfview_core PUBLIC include)
target_link_libraries(pdfview_core PUBLIC Threads::Threads PRIVATE PkgConfig::POPPLER)

add_library(pdfview STATIC src/Viewer.cpp)
target_link_libraries(pdfview PUBLIC pdfview_core imgui PRIVATE glad)

add_executable(pdfview-app app/main.cpp)
target_link_libraries(pdfview-app PRIVATE pdfview Hd)

if(COMMAND pdforg_warnings)
    pdforg_warnings(pdfview_core)
    pdforg_warnings(pdfview)
    pdforg_warnings(pdfview-app)
endif()
