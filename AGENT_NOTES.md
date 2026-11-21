# pdfview — agent notes

## What this is
A PDF viewer widget for Dear ImGui (OpenGL 3) rendered with poppler-glib/cairo, and `pdfview-app`, a
standalone viewer built on it. C++20, CMake. pdforg (a separate repo) embeds the widget, so library code
does no file I/O of its own and launches no processes.

## Map
- `include/pdfview/`, `src/`: the library.
  - `Document`: poppler-glib, used on the worker thread only.
  - `Layout`: rows, view modes and tile maths.
  - `RenderWorker`: a thread that renders tiles, reads page info and runs find.
  - `Viewer`: the widget, with selection, links, find, history and zoom modes.
  - `Actions`: key sequences and the action registry.
  - `Panels`: outline, thumbnails and the shortcut editor.
  - `History.h`: the back/forward model.
- `app/`: pdfview-app.
  - `App`: tabs, sidebar, toolbar, menus, reload and dialogs.
  - `Settings`: `settings.ini` (prefs, look, window, shortcuts, session, per-file state, bookmarks).
- `deps/Hd`: submodule (its own repo, with its own AGENT_NOTES.md).
  - Window, ImGui setup, and the look: scale, fonts, Font Awesome icons, themes with an editor.
  - `Hd::Platform`: native file dialog, open URI, dark mode, the system's UI font.
  - `C:\dev\github\Hd` is an older, separate checkout: don't edit that one.
- `tests/`: doctest. `fixtures.h` writes the test PDFs (text, links, outline, labels); no PDFs are stored.
- `build/` is output. `doc/plans/` holds plans.

## Commands (MSYS2 ucrt64)
- Git Bash puts Git's own `/mingw64/bin` (old libstdc++) first. Prefix `PATH=/c/msys64/ucrt64/bin:$PATH`,
  or hdres and the tests fail with 0xc0000139.
- `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`
- Hd's tests run too with `-DHD_BUILD_TESTS=ON`. Its font test takes about 10 s.
- `build/bin/pdfview-app file.pdf`. Settings go to `%APPDATA%\pdfview\settings.ini`; set APPDATA to keep
  test runs out of the real one.
- Close the running app before building: Windows locks the .exe and the link fails.
- Bash heredocs mangle backslashes, so `\n` inside C++ strings breaks. Edit those with the Write/Edit
  tools. Keep files LF; CRLF breaks multi-line replacements.

## Decisions
- Backend is poppler-glib. poppler-cpp has no TOC targets or links; MuPDF is AGPL and harder to package.
  Its pkg-config target is `PkgConfig::PDFVIEW_POPPLER`, so it doesn't clash with a host's.
- No poppler types in public headers, so the backend stays swappable.
- Library = widget, panels and action registry. App = tabs, files, settings and session.
- `Viewer::Options::shortcuts`: on, the widget handles keys itself. The app turns it off and calls
  `Viewer::registerActions` on one shared registry.
- The widget's sizes and "100%" follow `ImGuiStyle::FontScaleDpi`. Its background derives from the theme
  (`Options::background` 0).
- Look (DPI, fonts, themes), OS helpers, the event wait and window geometry live in Hd, not the app.
  - Themes are upstream Hd's (keys "dark", "light", "black", custom), plus "system".
  - Settings store fonts as `Hd::toString(FontRef)`.
  - The toolbar uses `Hd::iconButton` with Font Awesome glyphs. pdfview's own glyphs are in
    `App.cpp`'s `Icon` namespace.
- Native file dialogs (Win32 / zenity / kdialog) instead of nativefiledialog-extended: no GTK build dep.
- The app opens only http/https/mailto links, after asking; other links can only be copied.
- A file opened for the first time gets the viewer's default view; known files get their saved one.
- Resizing the thumbnail sidebar anchors the top edge of the first visible thumbnail.
- Thumbnails request a whole-page render when the standard preview is smaller than their display size.
- The thumbnail context menu controls page labels and gaps. Clicking a thumbnail keeps sidebar scroll.
- Annotations are out of scope; documents open read-only.

## Docs
- [doc/plans/](doc/plans/): implementation plans, dated.

## Current work
- Single-page wheel navigation is next. The original batch is saved in the pdfview and Hd stashes
  named `Previous viewer changes for individual review`.

[doc/plans/2026-11-21-viewer-features.md](doc/plans/2026-11-21-viewer-features.md).
- All phases are written and tested on Windows, including after merging upstream Hd's themes and fonts.
- Committed and pushed to pdfview `main` (b5dbca4) and Hd `master` (41b19a8).
- Next: the unticked steps. Test on Linux; try Ctrl+wheel, auto-reload, a locked PDF and an external
  link by hand; build pdforg against it.

## Ruled out
- poppler's core private headers (unstable API).
- nativefiledialog-extended (GTK build dependency on Linux).
- pdfview's own Breeze themes: dropped for upstream Hd's theme system. A Breeze look can come back as a
  custom JSON theme.
