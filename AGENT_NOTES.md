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
- After `git pull`, run `git submodule update`: a pull doesn't move `deps/Hd`, and the build then fails.
- pdfview-app is a GUI-subsystem program (`WIN32_EXECUTABLE`): no console, so stdout/stderr go nowhere.
- Bash heredocs mangle backslashes, so `\n` inside C++ strings breaks. Edit those with the Write/Edit
  tools. Keep files LF; CRLF breaks multi-line replacements.

## Commands (Linux: Arch under WSL)
- Same cmake commands; `build/` here is a Linux build. Settings go to `$XDG_CONFIG_HOME/pdfview/`, and they
  are written on preference changes, on quit and on SIGTERM/SIGHUP/SIGINT.
- `hd_font_test` takes about 140 s: it is also given three system fonts that Windows lacks and skips.
- A TigerVNC XFCE session runs on `DISPLAY=:1`; use it, or `Xvfb :77`, to drive the app with `xdotool`; take
  screenshots with `import -window <id>`. Click with `mousedown`/`mouseup` after moving: a move and a click in the same xdotool
  call miss ImGui tabs.

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
    `App.cpp`'s `Icon` namespace. It has three groups like Okular's: sidebar, zoom, view mode on the left;
    page navigation centred; tools, Find and Settings on the right. Group widths come from the last frame.
- Native file dialogs (Win32 / zenity / kdialog) instead of nativefiledialog-extended: no GTK build dep. Without
  zenity or kdialog (`Hd::Platform::hasFileDialog`), Open uses Hd's ImGui dialog (one file, starting in the
  active document's folder).
- SIGTERM, SIGHUP and SIGINT end the main loop (`main.cpp`, `gStop`) so `~App` saves the session; a second
  signal kills as usual. The X server dying still loses the session (Xlib exits).
- The app opens only http/https/mailto links, after asking; other links can only be copied.
- A file opened for the first time gets the last layout chosen in any tab (Overview excepted, `App::followLayout`)
  and the viewer's default zoom; known files get their saved view.
- Resizing the thumbnail sidebar anchors the top edge of the first visible thumbnail.
- The thumbnail grid reserves a fixed scrollbar gutter to avoid jumps in thumbnail width.
- Thumbnails request a whole-page render when the standard preview is smaller than their display size.
- Inactive tabs keep the last viewport's tiles in a shared 128 MiB cache that evicts thumbnail-only textures first.
- The thumbnail context menu controls page labels and gaps. Clicking a thumbnail keeps sidebar scroll.
- Thumbnail page borders use the theme's border color, including when gaps and page numbers are hidden.
- Thumbnail drags move through document pages in reading order, including across grid gaps.
- Ctrl+wheel changes the thumbnail grid by one column per notch within the existing size limits.
- Thumbnail filtering is per document and replaces the grid once after a 300 ms debounce.
- Next/Previous Page step from the row at the view's top edge (`View::topRow`), not the centre's `currentPage`.
- In non-continuous view, one wheel notch at the page edge turns the page.
- Scroll glides use `ScrollMotion` (`include/pdfview/ScrollMotion.h`), a critically damped spring: steps start at full speed and decay, screenfuls ease in and brake, and new input never makes the speed jump. The cap is 3,200 px/s at scale 1. Document-end jumps and non-continuous row turns are immediate.
- Default keys follow the table agreed on 2026-11-24 (vi-like keys first): J/K scroll, H/L or PgUp/PgDown pages,
  G G / Shift+G ends, / find, N / Shift+N (or Enter / Shift+Enter) next/previous match, O overview, I single, A fit
  page, S fit width, = actual size, Ctrl+O/Ctrl+I back/forward, Ctrl+Space sidebar. Open... has no default key.
  "Viewer: default shortcuts" in tests/test_actions.cpp holds the viewer's part of the table.
- Held scroll keys (J/K, Up/Down, Space/PgDown, Shift+Space/PgUp) share `Viewer::scrollHeld`: one step per ImGui key-repeat interval within the speed cap (lines 1,800 px/s, screenfuls 3,200 px/s), with the destination kept just over one braking distance ahead, so release stops like the end of one press. With animations off, they use plain key repeat.
- Sidebar panels (Thumbnails, Contents, Bookmarks) take the wheel from ImGui by setting `NoScrollWithMouse` on their
  window after Begin, and run it through `PanelScroll`. Autoscroll owns the mouse buttons while on, so the stopping
  click reaches no item.
- Ctrl + middle drag zooms around the press point (up zooms in, ×2 per 200 px at UI scale 1, `View::dragZoom`).
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
- Preferences live in one Settings window (`App::drawSettingsWindow`, Ctrl+Comma): Appearance, Viewing, Keyboard.
  The theme and shortcut editors stay separate windows opened from it. The Settings menu has only Settings... and
  Configure Keyboard Shortcuts....
- View modes are Single, Facing, Overview (each continuous or not). Facing Pages (Center First Page) was removed
  on request (2026-11-24); `first_alone=` in old settings files is ignored.
- The layout has no margin around the document: `Layout::gap` is only between pages and rows, so fits, page
  jumps and scroll ends touch the view's edges. The viewer reserves the scrollbar strip only when the content
  can scroll at the full width (`Viewer::needsScrollbar`), a test that can't flip between frames.
- Page gaps are one global on/off preference (`page_gaps`, `Viewer::setPageGaps`), Overview included. Off, pages
  touch, lose their shadow and get a 1-pixel border in the theme's border colour.
- Settings saves: hold `Hd::FileLock` on `settings.ini.lock`, read the file, keep local changes since the last
  load or save (`App::mSaved`), write with `Hd::replaceFile`. The open tabs are the saving window's, so a launch
  without files restores the last window saved. Bookmarks have ids (`bookmark_id=` after each `bookmark=` line,
  so older versions still read them); bookmarks without one get an id hashed from their contents. `used` is
  wall-clock ms, comparable between windows.

## Docs
- [doc/plans/](doc/plans/): implementation plans, dated.
- [doc/todo.md](doc/todo.md): known issues and ideas not planned yet (toolbar overflow menu, Linux findings).

## Current work
- Tested on Linux 2026-11-24 (at 347b053): GCC and Clang build without warnings, all tests pass (also under
  ASan/UBSan), and the app works under Xvfb (rendering, keys, find, Settings, wheel and Ctrl+wheel, copy,
  auto-reload, session restore, UTF-8 paths). The Open fallback and saving on signals were fixed after it;
  the remaining issues are in [doc/todo.md](doc/todo.md).
- [doc/plans/2026-11-21-viewer-features.md](doc/plans/2026-11-21-viewer-features.md) is still in progress.
  Next: zenity/kdialog, xdg-open and dark mode on a real Linux desktop; a locked PDF; an external link;
  build pdforg against the library.

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
