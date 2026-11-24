cmake_minimum_required(VERSION 3.29)

# pdfview: a PDF viewer widget for Dear ImGui (OpenGL 3 backend) and a small
# standalone viewer built on it. Needs poppler-cpp, and the imgui and glad
# targets: from the host project when it has them (pdforg adds Hd first),
# otherwise from the deps/Hd submodule.
project(pdfview VERSION 0.1.0 LANGUAGES C CXX)

option(PDFVIEW_BUILD_APP "Build the standalone viewer" ON)
option(PDFVIEW_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    # no C++20 modules are used; scanning slows the build
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(NOT TARGET imgui OR NOT TARGET glad OR (PDFVIEW_BUILD_APP AND NOT TARGET Hd))
    add_subdirectory(deps/Hd)
endif()

if(NOT TARGET PkgConfig::PDFVIEW_POPPLER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PDFVIEW_POPPLER REQUIRED IMPORTED_TARGET poppler-glib cairo)
endif()
find_package(Threads REQUIRED)

function(pdfview_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endif()
endfunction()

# the parts without GL or ImGui, which the tests use too
add_library(pdfview_core STATIC
    src/Document.cpp
    src/Layout.cpp
    src/RenderWorker.cpp
)
target_include_directories(pdfview_core PUBLIC include)
target_link_libraries(pdfview_core PUBLIC Threads::Threads PRIVATE PkgConfig::PDFVIEW_POPPLER)
pdfview_warnings(pdfview_core)

add_library(pdfview STATIC
    src/Actions.cpp
    src/PanelScroll.cpp
    src/Panels.cpp
    src/Viewer.cpp
)
target_link_libraries(pdfview PUBLIC pdfview_core imgui PRIVATE glad)
pdfview_warnings(pdfview)

if(PDFVIEW_BUILD_APP)
    add_executable(pdfview-app
        app/App.cpp
        app/Settings.cpp
        app/main.cpp
    )
    target_link_libraries(pdfview-app PRIVATE pdfview Hd)
    pdfview_warnings(pdfview-app)
    # a GUI program on Windows: no console window at launch; keep main()
    set_target_properties(pdfview-app PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(pdfview-app PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

if(PDFVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
