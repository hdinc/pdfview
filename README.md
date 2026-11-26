# pdfview

A PDF viewer widget for [Dear ImGui](https://github.com/ocornut/imgui) (OpenGL 3 backend), rendered with
poppler, and `pdfview-app`, a small standalone viewer built on it.

- Continuous vertical pages, zoom about the cursor (Ctrl+wheel), fit width / fit page, page input,
  keyboard navigation.
- Pages are rendered as 512 px tiles on a worker thread, with low-resolution previews underneath and an
  LRU texture budget, so large and zoomed documents stay smooth.
- Files are opened read only.

## Build

Needs CMake 3.29, a C++20 compiler and poppler-glib and cairo (`pkg-config poppler-glib cairo`).

```
git clone --recursive git@github.com:hdinc/pdfview.git
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
build/bin/pdfview-app file.pdf
```

## Releases

Pushing a `v*` tag builds `pdfview-<tag>-linux-x64.zip` (Ubuntu 24.04 or a newer glibc) and
`pdfview-<tag>-windows-x64.zip` with their libraries, and publishes them as a GitHub release
(`.github/workflows/release.yml`, `packaging/`).

## Future improvements

- Add regular expression (regex) search for PDF text.

## Embedding

Add it with `add_subdirectory` and link `pdfview` (or `pdfview_core` for the parts without GL). It uses the
host's `imgui` and `glad` targets when they exist, otherwise those of its `deps/Hd` submodule. See
`include/pdfview/Viewer.h`: `open(path)`, then `draw(id, size)` each frame inside an ImGui window; set a
wake callback (e.g. `glfwPostEmptyEvent`) so a host that waits for events redraws when tiles arrive.

Licence: GNU General Public License, version 2 (GPL-2.0-only), see [LICENSE](LICENSE); poppler is GPL too.
