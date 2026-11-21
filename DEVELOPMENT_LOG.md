# Development log

## 2026-11-21 20:30 +03:00 — Viewer features: backend, view modes, shortcuts, panels, app shell, Hd look

### Changes made
- **Backend:** `Document` moved from poppler-cpp to poppler-glib. poppler-cpp has no TOC destinations,
  links or text layout. Rendering goes through cairo; the `Bitmap` format is unchanged.
- **Render thread:** `RenderWorker` became a job queue: page info, then tiles, with find steps between
  tiles.
- **Layout:** `Layout` places pages in rows; `View` gained a row, flipping and band clamping.
- **Viewer:** rewritten around actions. It gained selection, links, history, find, zoom modes,
  `restoreView`, preview access for panels, and theme and DPI awareness.
- **App:** rebuilt as `App` (tabs, sidebar, toolbar, menus, dialogs) with `Settings` (settings.ini).
  The local Platform files moved to Hd.
- **Hd submodule** (branch `ui-scale-fonts-themes`): content scale and callback, `WaitEventsTimeout`,
  window geometry, dark title bar. `Gui` now handles scale, fonts and themes and has `waitEvents`. New
  `Theme` and `Platform` modules.
- **Fixes from the GUI test on a 4K monitor:**
  - The whole UI was tiny (no DPI handling).
  - The sidebar splitter was 6 px, which was invisible and impossible to hit.
  - The shortcut window opened unscaled and squeezed.
  - A new file opened at a huge fit-width zoom.
  - Contents started fully expanded.
  - Thumbnails kept a stale scroll offset after a reflow.
  - The look menu was missing.
- **Repo files:** `AGENT_NOTES.md` is no longer git-ignored; the knowledge files belong in the repo.

### Features implemented
- **Contents panel:** a tree with page labels (i, ii, 1…) and a search filter. It starts collapsed, with
  the way to the current section open (like Okular), and clicking an entry jumps to the exact spot.
- **View modes:** Single, Facing, Facing with the first page centred, Overview (3–12 columns), each
  continuous or not. Space/PgDown and a few wheel notches past a row's end flip rows.
- **Thumbnails panel:** as many columns as fit, Ctrl+wheel to resize. It marks the current page and the
  visible area (which can be dragged), and its search filter shows only pages with matches.
- **Tabs:** reorderable, middle-click close, Ctrl+Tab. Duplicate file names get their folder added.
  Background tabs free their textures.
- **Text selection:** Browse and Text Selection tools. Drag, double-click (word), triple-click (line),
  Ctrl+A (page), Ctrl+C; selections can span pages.
- **Links:** a hand cursor and a tooltip ("Go to page 16" or the URI). Internal links jump exactly;
  external ones ask first (http/https/mailto only).
- **History:** back/forward with Alt+Left/Right, the mouse's back/forward buttons and toolbar arrows.
- **Find:** Ctrl+F bar with incremental search across all pages and all matches highlighted. N/Shift+N
  and Enter/Shift+Enter move through them; match-case and whole-word toggles; "27 of 145".
- **Shortcuts:**
  - Okular-like defaults, including sequences like `G, G`.
  - Editor: search, a cell captures a new sequence, conflicts ask before reassigning, per-action reset
    and Restore Defaults.
  - Saved in settings.
- **Session memory:**
  - Open tabs, the active tab, window geometry and maximized state, recent files.
  - Per file: page, position, zoom, zoom mode, view mode and bookmarks.
- **Other app features:** bookmarks (Ctrl+B, rename, remove), auto-reload when a file changes on disk,
  password prompt.
- **Look:**
  - Follows the monitor's DPI (live when moving between monitors), times a user Interface Size.
  - System UI font (Segoe UI or fc-match) with a symbol fallback, a chosen font file or size.
  - Themes: System (follows the OS's dark/light setting), Breeze Light, Breeze Dark, Dark, Light,
    Classic. A dark title bar on Windows with dark themes.

### How it works
- **Document:**
  - `Document.cpp` keeps a 24-page LRU of PopplerPage objects; poppler keeps text per page.
  - Link and find rectangles are converted from bottom-up to top-left page points.
  - Dest y uses the page height. On quarter-turned pages (crop box swapped) a dest goes to the page top.
- **RenderWorker** (`RenderWorker::run`) picks jobs under the lock:
  - page info first;
  - tiles and find pages alternate;
  - results from a superseded search are dropped by id.
- **Layout** (`Layout::build`):
  - Columns as wide as the widest page; facing pages meet at the middle; rows centred vertically.
  - `View::band` limits the view to the current row when not continuous.
  - `View::pages` restricts drawing and tiles to that row.
- **Actions:**
  - Chords are ImGui chords, with our own key names, so saved text stays valid across ImGui versions.
  - `Actions::feed` waits when the typed keys are a prefix of a longer sequence. `tick` fires the
    shorter one after 1 s. An unmatched sequence retries its last key alone.
- **Viewer:**
  - Selection is a caret range hit-tested on cached per-page text boxes. It is synchronous, with no
    worker round trip.
  - Copy waits for pages whose text isn't loaded yet.
  - Find keeps matches per page and the current (page, index).
- **Panels:**
  - Thumbnails reuse the viewer's 320 px preview textures through `previewTexture`; requests are
    ordered after the main view's.
  - The outline opens the path to the current item (`pathTo`) only when the current item changes.
- **Hd `Gui`:**
  - Restyles between frames: theme → `FontSizeBase` → `ScaleAllSizes(scale)` → `FontScaleDpi`.
  - ImGui 1.92's dynamic font atlas rasterizes fonts at the drawn size, so scaling needs no rebuild.
  - `waitEvents(busy, timeout)` replaces the hand-written "draw 3 frames per wake" loops.
- **Rejected:**
  - MuPDF: AGPL, packaging.
  - poppler private headers: unstable API.
  - nativefiledialog-extended: GTK build dependency.
  - Rebuilding the font atlas on scale changes: not needed with dynamic fonts.

### Testing
- **Unit tests** (doctest, 13 cases, all pass) cover:
  - Document: sizes and rotation, labels, outline, links (including turned pages), text order, find
    flags.
  - RenderWorker: info, find order and cancelling, a search started before loading.
  - Layout modes and row flipping.
  - Key-sequence parse and print for every key, sequence matching and timeouts, conflicts, save/load.
  - History.
- **By hand** on Windows 11 at 4K (150% scale), with screen control, on riscv-privileged.pdf and
  riscv-unprivileged.pdf. All worked after the fixes:
  - Contents: jumps, labels, current-section highlight.
  - TOC links: hover tooltip, exact jump; Back/Forward.
  - View modes: Overview, Facing (center first), non-continuous flipping by Space and by wheel.
  - Selection: drag across lines and copy (the clipboard holds the right text); double- and
    triple-click.
  - Find: incremental search, Enter/N/Shift+N, the count.
  - Thumbnails: reflow and following the current page.
  - Tabs: open through the native dialog; Ctrl+Tab keeps each tab's state.
  - Shortcut editor: capture, the conflict dialog, a new binding working and saved.
  - Look: theme switching live, the dark title bar.
  - Session: restored after a restart, including maximized; bookmarks.
- **Not tested:**
  - Ctrl+wheel zoom and thumbnail resize: the test tool doesn't send modifiers with scrolls.
  - Auto-reload, a password-protected PDF, opening an external link.
  - Linux (dialogs, fonts, dark mode detection).
  - pdforg built against the new library.

### Recommended features
- **Display modes:** rotation, inverted/sepia colours, presentation mode, trimmed margins.
- An icon font for the toolbar and sidebar.
- A properties dialog; form fields, attachments, layers; SyncTeX.

### Outstanding issues
- On Windows, poppler may keep the PDF open in a way that stops LaTeX rewriting it, so auto-reload may
  not get a chance. Unverified.
- "100%" zoom follows the UI scale, including the user's Interface Size, not the monitor's physical DPI
  alone.
- The window opens at its default place, then jumps to the saved one.
- Theme "System" on Linux asks gsettings at most every 10 s.

### Next steps
- Test on Linux.
- Try the items listed under "Not tested" by hand.
- Build pdforg against the new Hd `Gui`. Its constructor now loads the system font and the Dark theme,
  so check its look.
- Unit tests for `Settings`.

## 2026-11-21 21:35 +03:00 — Merge upstream Hd's themes, fonts and icons; icon toolbar

### Changes made
- Upstream Hd (origin/master a282281) added fonts, icons, themes and a file dialog. The `deps/Hd` branch
  `ui-scale-fonts-themes` now sits on top of it, so the submodule points at a282281 plus uncommitted
  changes.
  - Hd's own Theme module (Breeze Light/Dark, Classic) and font loading from the earlier entry are gone,
    replaced by upstream's `Theme` and `Fonts`.
  - Scale, window geometry, the dark title bar, `Platform` (without `configDir`), `waitEvents` and a
    "system" theme key were re-applied on top. The pre-merge work is kept in a git stash in `deps/Hd`.
- pdfview-app follows the merged API:
  - Settings now keep the theme as a key ("system", "dark", "light", "black" or a custom one; the old
    "System" is read as "system").
  - Fonts are saved as `Hd::toString(FontRef)`, with a new monospace font and the toolbar size.
  - The config dir comes from `Hd::configDir()` after `Hd::setConfigName("pdfview")`.
- Settings menu:
  - Theme: System, the built-in and custom themes, Edit Themes… (upstream's `ThemeEditor`).
  - Interface Size.
  - Toolbar Size (upstream's menu).
  - Fonts…: a window with upstream's `FontPicker` for interface and monospace fonts, plus size.
- The toolbar is icon buttons (`Hd::iconButton`, Font Awesome): sidebar, back/forward, previous/next
  page, page box, zoom out/combo/in, fit width/page, view mode, browse/text tool, find, bookmark.
  Tooltips show each action's current shortcut; the active state shows the tool, fit mode, find bar and
  sidebar.
- `Viewer::drawZoomCombo()` split out of `drawZoomBox()`, so hosts can put their own buttons around it.

### How it works
- Hd's `Gui::setTheme("system")` resolves to "dark" or "light" through `Platform::prefersDarkTheme`.
- The theme editor gets the resolved key, and its live edits reach the screen through
  `Gui::refreshStyle`.
- An empty FontRef (the built-in choice, shown as "System default") means the system's interface font.
- pdfview's own Font Awesome codepoints live in `App.cpp` (`Icon` namespace), as Icons.h suggests for
  apps.

### Testing
- Builds clean. pdfview's tests and Hd's tests (search, vt100, zoom, theme, font) all pass, the latter
  with `-DHD_BUILD_TESTS=ON`.
- By hand on Windows at 4K, with screen control:
  - The icon toolbar and its active states.
  - Theme menu: Dark → Light live, with the title bar following.
  - Fonts window: the installed-font list, switching to Calibri and back live.
  - Session, settings and geometry restored.
- Not tested: the theme editor's save/duplicate, the toolbar size menu, Linux.

### Outstanding issues
- A settings file from the earlier version may name "Breeze Dark", which no longer exists: it falls back
  to Dark.
- Launching the app through the screen-control tool started a second instance with the real
  `%APPDATA%`. It wrote a defaults-only `%APPDATA%\pdfview\settings.ini`.

### Next steps
- Possibly ship a Breeze-like custom theme JSON.
- The earlier entry's next steps still stand: Linux, the by-hand checks, pdforg.

## 2026-11-21 23:58 +03:00 — Keep thumbnails in view during sidebar resize

### Changes made

- Saved the original batch in separate pdfview and Hd stashes named `Previous viewer changes for
  individual review`. The thumbnail resize fix is in the working tree for review before committing.
- Recovered the previous agent's page-and-row-offset approach in `ThumbnailPanel::draw`. The anchor now
  survives width changes within the same column count and repeated column reflows. The grid renders at
  the requested scroll position on the resize frame, and the result clamps to the new scroll range.

### Testing

- Built the Windows app in `build/thumb-resize-review`. All six CTest targets passed, including Hd.
- A headless ImGui check with a 61-page PDF verified the anchor page, relative row offset, and hit
  testing through same-column resizes and a 2→3→4→1→2 column sequence. It also covered clamping near
  the bottom and following viewer navigation. The interactive splitter drag awaits user review.

## 2026-11-22 00:28 +03:00 — Render sharper thumbnails at their displayed size

- Recovered the stashed whole-page render approach. The sidebar requests a sharper texture when the
  320-pixel preview would be enlarged at the framebuffer's resolution. Small thumbnails reuse the
  preview, and larger thumbnails use a cached lower-resolution image while their render is pending.
  Whole-page thumbnail renders are capped at 2048 pixels on the long side and stay out of the main
  document's draw list. The cache protects textures used by the sidebar for the current ImGui frame.
- The Windows build in `build/thumb-sharpness-review` passed all six CTest targets including a worker
  check for rendering a rotated thumbnail in full. Headless checks confirmed the sidebar selected a
  sharper texture at 2× scale and verified fallback, cache reuse under a reduced texture budget, the
  size cap, and the previous resize behavior. Visual review of the Windows build is pending.

## 2026-11-22 00:33 +03:00 — Add thumbnail display options and preserve scroll on selection

- The thumbnail context menu now has separate `Show Page Numbers` and `Gaps Between Thumbnails`
  options, both saved in the app's settings. Hover shows the page label when numbers are hidden.
  Compact thumbnails omit the gutters, padding, and shadows, with an inset outline for selection.
- Clicking a thumbnail jumps to the page and leaves the sidebar scroll position in place, including
  when the selected thumbnail is partly visible.
- Built the Windows app in `build/thumb-options-review` and passed all six CTest targets. Headless
  checks exercised selection without sidebar movement, both context menu toggles, touching thumbnail
  edges, settings round-trip, and the previous resize behavior. The options and click behavior are
  ready for visual review.

## 2026-11-22 00:10 +03:00 — Set thumbnail width independently of sidebar width

### Changes made

- Ctrl+wheel changes the preferred thumbnail image width. Thumbnails keep that width and wrap to the
  next row when the sidebar runs out of room. If one thumbnail would be too wide, it shrinks to fit
  and returns to the preferred width when there is room. The resize anchor keeps the visible row in
  view during reflow.
- The combined thumbnail resize change is pending user review before committing.

### Testing

- Built the Windows app in `build/thumb-width-review`. All six CTest targets passed.
- A headless ImGui check verified the drawn image width after Ctrl+wheel with and without column
  changes, the narrow sidebar fallback, and the anchor during repeated resizes. Interactive review
  is pending.

## 2026-11-22 00:13 +03:00 — Restore the previous thumbnail sizing behavior

- Reverted independent thumbnail sizing at the user's request. Thumbnails again fill the available
  width across their columns. The original sidebar resize anchoring fix is still in the working
  tree for review.
- Rebuilt the Windows app in `build/thumb-resize-review`. All six CTest targets and the headless
  ImGui resize check passed.

## 2026-11-22 00:22 +03:00 — Preserve the first visible thumbnail's screen position

- Sidebar resize now anchors the top edge of the first thumbnail image that intersects the viewport,
  including a partially visible image. The previous proportional row offset shifted that edge as the
  thumbnail height changed. The new scroll target uses the measured screen Y and respects the scroll
  range and the thumbnail's visibility.
- The Windows build in `build/thumb-position-review` passed all six CTest targets. A headless ImGui
  test measured the drawn top edge of a partially visible thumbnail in a mixed-size PDF. The old
  build moved it 28 pixels during a same-column resize. The updated build held it within half a pixel
  through same-column resizes and a 2→3→4→1→2 column sequence. The test also covered scrolling near
  the bottom and following viewer navigation. The interactive splitter drag awaits user review.

## 2026-11-22 00:38 +03:00 — Turn the page with one wheel notch

- A single wheel notch now flips the page in non-continuous single-page view when the viewport is at
  the relevant page edge. Zoomed-in pages still scroll to the edge first. Fractional wheel input
  accumulates toward one notch in the same direction.
- Built the Windows app in `build/wheel-page-review`. All six CTest targets passed. A headless ImGui
  check covered both directions, split deltas, in-page scrolling and flipping at high zoom, along
  with continuous scrolling and Ctrl+wheel zoom. The Windows build is ready for user review.

## 2026-11-22 00:45 +03:00 — Put the sidebar to the left of the document tabs

- The sidebar and splitter start alongside the tab strip and span the full document area. The tab
  strip resolves the visible document before drawing the sidebar, so both panes use the same viewer
  in each frame. The sidebar retains its per-document scroll state.
- Built the Windows app in `build/sidebar-layout-review` and passed all six CTest targets. A hidden
  OpenGL integration check measured the geometry and exercised tab switching, dragging the splitter,
  hiding and showing the sidebar, and closing the active and last tabs. The layout is ready for
  visual review.

## 2026-11-22 00:50 +03:00 — Move the Find bar below the document

- The standalone app reserves one row beneath the document for the Find bar. Opening it keeps the
  top of the document area in place. Escape now closes Find even when the text field consumes the
  key and deactivates itself.
- Built the Windows app in `build/find-bar-review`. A hidden OpenGL integration check confirmed
  that Ctrl+F focused the bar at the bottom, typing highlighted matches, Enter advanced, and Escape
  restored the full viewport. All six CTest targets passed. The layout is ready for visual review.

## 2026-11-22 01:00 +03:00 — Match the Find bar layout to the reference

- The bottom bar has a close control, a Find label and expanding field, labeled Next and Previous
  buttons, and an Options popup that opens above the bar. The popup controls case sensitivity, the
  starting page, and whether typing starts a search. Whole Words remains available in the main Find
  menu. The embeddable widget retains its compact Find bar.
- Built the Windows app in `build/find-layout-review`. All six CTest targets passed. A hidden OpenGL
  integration check and screenshot confirmed the alignment and popup placement. The check also
  covered Ctrl+F, moving to the next match, Escape and the close button, starting on the current or
  first page, waiting for Enter with live search disabled, and case-sensitive matching.

## 2026-11-22 01:07 +03:00 — Center the Find bar close icon

- Draw the close icon from the center of its button so its placement is independent of font metrics.
  The button keeps hover feedback, its tooltip, and keyboard navigation.
- Added regular expression search to the future improvements in `README.md`.
- Built the Windows app in `build/find-icon-review`. All six CTest targets and the Windows integration
  check passed, including measuring the icon's center and clicking it to close the bar.
