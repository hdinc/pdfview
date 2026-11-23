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
  - `ScrollMotion.h`: the spring behind all scroll glides, the autoscroll speed curve and the middle-button
    `Autoscroll` gesture (no ImGui). `PanelScroll`: smooth wheel and autoscroll for a scrolling ImGui
    window, and the autoscroll marker.
  - `History.h`: the back/forward model.
- `app/`: pdfview-app.
  - `App`: tabs, sidebar, toolbar, menus, reload and dialogs.
  - `Settings`: `settings.ini` (prefs, look, window, shortcuts, session, per-file state, bookmarks). Saves are
    three-way merges under a lock (`saveMerged`), so several app windows can share the file.
- `deps/Hd`: submodule (its own repo, with its own AGENT_NOTES.md).
  - Window, ImGui setup, and the look: scale, fonts, Font Awesome icons, themes with an editor.
  - `Hd::Platform`: native file dialog, open URI, dark mode, the system's UI font.
  - `C:\dev\github\Hd` is an older, separate checkout: don't edit that one.
- `tests/`: doctest. `fixtures.h` writes the test PDFs (text, links, outline, labels); no PDFs are stored.
  `test_settings.cpp` (built with the app) compiles `app/Settings.cpp` and runs copies of itself as writers.
- `build/` is output. `doc/plans/` holds plans.

## Commands (MSYS2 ucrt64)
- Git Bash puts Git's own `/mingw64/bin` (old libstdc++) first. Prefix `PATH=/c/msys64/ucrt64/bin:$PATH`,
  or hdres and the tests fail with 0xc0000139.
- `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`
- Use the main `build/` folder for changes under review.
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
- The thumbnail grid reserves a fixed scrollbar gutter to avoid jumps in thumbnail width.
- Thumbnails request a whole-page render when the standard preview is smaller than their display size.
- Inactive tabs keep the last viewport's tiles in a shared 128 MiB cache that evicts thumbnail-only textures first.
- The thumbnail context menu controls page labels and gaps. Clicking a thumbnail keeps sidebar scroll.
- Thumbnail page borders use the theme's border color, including when gaps and page numbers are hidden.
- Thumbnail drags move through document pages in reading order, including across grid gaps.
- Ctrl+wheel changes the thumbnail grid by one column per notch within the existing size limits.
- Thumbnail filtering is per document and replaces the grid once after a 300 ms debounce.
- In non-continuous view, one wheel notch at the page edge turns the page.
- Scroll glides use `ScrollMotion` (`include/pdfview/ScrollMotion.h`), a critically damped spring: steps start at full speed and decay, screenfuls ease in and brake, and new input never makes the speed jump. The cap is 3,200 px/s at scale 1. Document-end jumps and non-continuous row turns are immediate.
- Held scroll keys (J/K, Up/Down, Space/PgDown, Shift+Space/PgUp) share `Viewer::scrollHeld`: one step per ImGui key-repeat interval within the speed cap (lines 1,800 px/s, screenfuls 3,200 px/s), with the destination kept just over one braking distance ahead, so release stops like the end of one press. With animations off, they use plain key repeat.
- Sidebar panels (Thumbnails, Contents, Bookmarks) take the wheel from ImGui by setting `NoScrollWithMouse` on their
  window after Begin, and run it through `PanelScroll`. Autoscroll owns the mouse buttons while on, so the stopping
  click reaches no item.
- Middle-click toggles autoscroll. A middle drag autoscrolls until release. In non-continuous view, autoscroll stays in the current row.
- Double-click word selection and triple-click line selection are limited to Text Selection mode.
- The page context menu uses standard ImGui styling and Font Awesome icons. Fit Width leaves Overview for the previous normal layout.
- The sidebar starts to the left of the document tabs and follows the active document.
- Sidebar tabs share the available width evenly, with centered Font Awesome icons and names on hover.
- The standalone Find bar sits below the document and has its own Options popup.
- Contents uses native tree lines and truncates titles to fit the sidebar.
- Contents keeps the clicked heading highlighted at its landing position and follows the viewport when scrolling resumes.
- Contents draws the current title in the UI font's bold face when available.
- Contents rows use zero vertical cell padding so the highlight reaches both row edges.
- The Contents context menu expands and collapses branches and highlights its target row.
- Annotations are out of scope; documents open read-only.
- The layout has no margin around the document: `Layout::gap` is only between pages and rows, so fits, page
  jumps and scroll ends touch the view's edges. The viewer reserves the scrollbar strip only when the content
  can scroll at the full width (`Viewer::needsScrollbar`), a test that can't flip between frames.
- Settings saves: hold `Hd::FileLock` on `settings.ini.lock`, read the file, keep local changes since the last
  load or save (`App::mSaved`), write with `Hd::replaceFile`. The open tabs are the saving window's, so a launch
  without files restores the last window saved. Bookmarks have ids (`bookmark_id=` after each `bookmark=` line,
  so older versions still read them); bookmarks without one get an id hashed from their contents. `used` is
  wall-clock ms, comparable between windows.

## Docs
- [doc/plans/](doc/plans/): implementation plans, dated.

## Current work
- [View fit, page gaps, drag zoom, settings window and toolbar](doc/plans/2026-11-24-view-fit-zoom-settings.md):
  step 1 (fits fill the view) done, uncommitted, for review. Next: the page gaps setting.
- [Viewer interaction updates](doc/plans/2026-11-22-viewer-interaction-updates.md) is done (last commits: Hd
  3191ff6, pdfview bd53f4d). Not yet tested on Linux.

[doc/plans/2026-11-21-viewer-features.md](doc/plans/2026-11-21-viewer-features.md).
- All phases are written and tested on Windows, including after merging upstream Hd's themes and fonts.
- Committed and pushed to pdfview `main` (b5dbca4) and Hd `master` (41b19a8).
- Next: the unticked steps. Test on Linux; try Ctrl+wheel, auto-reload, a locked PDF and an external
  link by hand; build pdforg against it.

## Ruled out
- poppler's core private headers (unstable API).
- nativefiledialog-extended (GTK build dependency on Linux).
- Fixed-duration ease-out scroll glides: restarting the curve on each input makes the speed jump.
- Moving shortcut editing (editor, registry, key parser) into Hd (dropped 2026-11-23): the library would then
  depend on Hd, or keep only action descriptions and lose its own keyboard mode; nothing else needs it yet.
- Moving the scroll code into Hd now (tried 2026-11-23): a host that brings its own ImGui and Glad
  would then need the Hd submodule checked out, only pdfview uses it, and every tuning tweak would
  need two commits and a submodule bump. Revisit when a second project needs it.
- pdfview's own Breeze themes: dropped for upstream Hd's theme system. A Breeze look can come back as a
  custom JSON theme.
