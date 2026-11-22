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

## 2026-11-22 01:19 +03:00 — Add tree lines to Contents

- Contents uses ImGui's native tree stack to draw branch lines for expanded entries. Long titles
  end in an ellipsis before the page-number column. Hover shows the full title.
- Used the main `build/` folder. All six CTest targets passed. A hidden Windows OpenGL check verified
  branch lines when expanding, collapsing, and filtering nested entries. The layout is ready for
  visual review.

## 2026-11-22 01:27 +03:00 — Add a Contents context menu

- Right-click a Contents entry for section and tree expansion commands, its page destination,
  and bookmark and zoom actions. Right-click empty space for the tree-wide commands.
- Expansion uses per-entry storage IDs so a section command reaches hidden descendants. Filtering
  opens matching paths separately from the tree state saved by these commands.
- Used the main `build/` folder. All six CTest targets passed. A Windows OpenGL check verified
  recursive expansion and collapse, right-clicking the page-number column, saving the clicked
  destination as a bookmark, and navigating to it with Next Bookmark.

## 2026-11-22 01:39 +03:00 — Keep only expand and collapse in the Contents menu

- The row menu contains four expand and collapse commands. The clicked row uses ImGui selection
  highlighting while its popup is open.
- All six CTest targets passed. The Windows integration check verified the four commands, that the
  highlight stayed visible while the pointer moved into the menu, and that it cleared when the menu
  closed. Rebuilt `build/bin/pdfview-app.exe` while leaving the older instance running.

## 2026-11-22 01:51 +03:00 — Stabilize the thumbnail scrollbar during sidebar resize

- The scrollbar changed the width that determined the thumbnail layout, producing a feedback loop.
  The panel now computes the layout and content height before opening its ImGui child. It uses the
  gutter-width layout when that width alone lets all thumbnails fit, and shows the scrollbar when
  the narrower layout still overflows.
- Rebuilt in the main `build/` folder. In a two-page fixture, the previous build alternated the
  scrollbar at 14 widths. The new build showed no alternating widths from 225 to 350 pixels,
  rendered both thumbnails on transition frames, and scrolled when the bar was visible. A Windows
  OpenGL splitter test confirmed that the document stayed rendered when the bar appeared and
  disappeared. All six CTest targets and the thumbnail anchoring and options checks passed.

## 2026-11-22 01:56 +03:00 — Keep thumbnail width stable when the scrollbar changes

- Reserve the scrollbar gutter even when the bar is hidden. Thumbnail width follows the sidebar
  width continuously within the same column count, and the scrollbar appears when rows overflow.
- Rebuilt in the main `build/` folder and passed all six CTest targets. The previous fix shrank a
  thumbnail by 13 pixels when the sidebar grew one pixel. The new layout removed that jump and
  kept thumbnail width identical when changing only the panel height to show or hide the scrollbar.
  The scrollbar sweep, resize anchor, and Windows splitter checks passed.

## 2026-11-22 02:05 +03:00 — Add middle-click autoscroll

- A middle click shows an autoscroll marker. The pointer controls speed and direction, and a middle
  drag still pans. A click, key, wheel movement, or loss of focus stops autoscroll.
- In non-continuous view, movement at a page edge accumulates over a viewport's height before
  turning the page. This sets the page-turn rate by pointer distance, even when a page already fits.
- Built in the main `build/` folder. All six CTest targets and headless ImGui checks passed.
  The checks covered velocity, horizontal movement, cancellation, middle drag, paced page turns,
  and one-notch wheel navigation. The Windows build is ready for review.

## 2026-11-22 02:24 +03:00 — Support both middle-button autoscroll gestures

- A middle press starts autoscroll immediately. A release without dragging leaves it running, and
  the next click stops it. Moving the pointer while holding the middle button scrolls until release.
- Rebuilt in the main `build/` folder while leaving the older instance running. All six CTest
  targets, the headless gesture check, and the Windows OpenGL gesture check passed.

## 2026-11-22 02:28 +03:00 — Increase middle-button autoscroll sensitivity

- Increased the scroll speed for a given pointer distance by about 17% below the existing speed
  cap. The dead zone remains the same.
- Rebuilt in the main `build/` folder. All six CTest targets passed, along with the measured
  autoscroll speed check and the Windows gesture check.

## 2026-11-22 02:31 +03:00 — Accelerate autoscroll exponentially with pointer distance

- Restored the original sensitivity just outside the dead zone. Speed now increases exponentially
  as the pointer moves farther from the marker, up to the existing cap.
- Rebuilt in the main `build/` folder. All six CTest targets and the Windows gesture check passed.
  The headless check measured speed near the dead zone, farther away, and at the cap.

## 2026-11-22 15:35 +03:00 — Animate continuous page scrolling

- Space and Shift+Space glide by 90% of the viewport in continuous view. Rapid taps and held-key
  repeats advance the queued destination, which is clamped to the document. PgDn and PgUp share
  the same actions.
- Adapted the saved destination-based animation to vertical page scrolling in `Viewer`. The
  existing non-continuous page turns and middle-button autoscroll still use their original paths.
- Built the main `build/` folder with the UCRT64 tools. All six CTest targets passed. A Windows
  OpenGL run of the app with injected ImGui input verified the glide after idle, rapid and held
  repeats, reversal, end clamping, wheel interruption, immediate non-continuous page turns,
  middle-click latching, middle-drag release, and the handoff between autoscroll and page scrolling.
- The review build is `build/bin/pdfview-app.exe`. Both original stashes remain intact.

## 2026-11-22 18:23 +03:00 — Give continuous page scrolling a defined end

- Replaced the exponential approach with a 600 ms cubic ease-out. The initial speed matches
  the user's 0.20 s time constant. The animation reaches the exact destination on the first
  frame at or after the duration. Repeated presses at a clamped edge leave the timer running.
- Rebuilt `build/bin/pdfview-app.exe` with the UCRT64 tools. All six CTest targets passed. The
  Windows OpenGL app check measured completion at 609 ms and verified monotonic travel,
  repeats at the edge, rapid and held-key scrolling, reversal, wheel interruption, immediate
  non-continuous page turns, and both middle-button autoscroll gestures.
- The fix remains uncommitted for review, with both stashes intact.

## 2026-11-22 18:28 +03:00 — Ease into and out of continuous page scrolling

- Changed the 600 ms page glide to use cubic smoothstep. It starts and ends at rest, with
  matching acceleration and deceleration.
- Rebuilt the main Windows executable. All six CTest targets passed. The OpenGL app check
  confirmed that the glide mirrored its start and finish, reached its exact destination, and
  passed the rapid-tap, held-key, reversal, wheel, non-continuous page-turn, and autoscroll checks.
- The fix remains uncommitted for review, with both stashes intact.

## 2026-11-22 18:45 +03:00 — Restore the simple continuous-scroll ease-out

- Undid symmetric easing. The animation uses a single cubic interpolation from its start
  to its queued destination and lands exactly on the target after 600 ms.
- Rebuilt the main Windows executable. All six CTest targets passed. The OpenGL app check
  verified exact landing, rapid taps and held-key repeats, reversal, wheel interruption,
  immediate non-continuous page turns, and middle-button autoscroll.
- The fix remains uncommitted for review, with both stashes intact.

## 2026-11-22 18:49 +03:00 — Keep the clicked Contents heading highlighted

- Preserve the clicked heading at the position its jump reached. This covers headings with
  identical or nearby targets and jumps clamped at the document end. When the view scrolls,
  Contents resumes its position-based highlight.
- Keep the pin on the Viewer and clear it when closing the document, so each document has its
  own selection and reloading clears the old outline reference.
- Built the main build folder with the UCRT64 tools. All six CTest targets passed. A Windows
  OpenGL app check clicked headings with identical and nearby targets, checked wheel-based
  tracking and a clamped jump, and verified that reopening clears the pin.
- The review executable is build/bin/pdfview-app.exe. The fix is uncommitted and both stashes
  remain intact.

## 2026-11-22 19:24 +03:00 — Show the current Contents title in bold

- Only the current title uses the UI family's bold face. The title is measured with the same
  face for truncation. If a bold face is unavailable, the title gets a one-pixel overprint.
  Page labels stay regular.
- Hd reloads its optional bold face with the other UI fonts. The app passes the current
  face to the Contents panel each frame.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows OpenGL
  check verified title and page-label glyphs, truncated titles, a live UI font change, simulated
  bold, and Contents highlight tracking. Platform-specific font discovery outside Windows was
  untested.
- The fix is uncommitted in pdfview and Hd. Both stashes remain intact.

## 2026-11-22 19:30 +03:00 — Use regular Contents text when bold is unavailable

- Removed the simulated bold overprint. Contents draws the current title once with the regular
  face when a bold face is unavailable. Real bold and title truncation still work.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows OpenGL
  check verified a single regular glyph draw in the fallback and confirmed real bold and live
  font switching.
- The bold-title fix remains uncommitted, with both stashes intact.

## 2026-11-22 19:32 +03:00 — Use icons for the sidebar tabs

- Replaced the Thumbnails, Contents, and Bookmarks tab labels with the images, list, and
  bookmark glyphs from the saved batch. The existing tab bar supplies selection and
  keyboard focus. The full names appear in hover tooltips.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows
  OpenGL app check verified the glyphs at minimum sidebar width, tooltips, tab clicks,
  action-driven selection including Add Bookmark, and restoring the tab from settings.
- The fix is uncommitted for review. Both stashes remain intact.

## 2026-11-22 19:38 +03:00 — Stretch the sidebar tabs evenly

- Gave the three native tabs equal shares of the available sidebar width, accounting for tab
  spacing and distributing the leftover pixels. Each glyph is centered in its tab.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows
  OpenGL app check verified width and centering at sidebar widths of 150, 200, and 280 at scale
  1, and checked tooltips, clicks, action-driven selection, and restoring the selected tab.
- The fix remains uncommitted for review. Both stashes remain intact.

## 2026-11-22 19:48 +03:00 — Remove the gaps around the Contents highlight

- Zeroed vertical cell padding in the Contents table so the current and context-menu
  highlights reach both row edges.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. Windows OpenGL
  checks verified row coverage for a truncated bold title and a context menu opened from
  the page-number column. Coverage remained flush after a scale change. Tree guides and
  the expand and collapse commands also passed.
- The last fix in the review batch is uncommitted. Both original stashes remain intact.

## 2026-11-22 20:34 +03:00 — Fill the thumbnail viewport indicator

- Added a translucent yellow fill using the existing viewport outline's bounds, so each
  thumbnail shows just the portion of its page visible in the document viewer.
- The agreed viewer interaction plan is committed as 7e2fd36. This first fix is uncommitted
  for review, and both original stashes remain intact.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows OpenGL
  app check verified both pages of a fitted spread, the clipped region of a zoomed page
  during wheel scrolling, and the fill with thumbnail gaps and page numbers disabled.

## 2026-11-22 21:25 +03:00 — Draw borders around thumbnail pages

- Drew a thin border in the theme's border color around each thumbnail, including when gaps and
  page numbers are disabled. The viewport indicator and selection highlights draw over it.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows OpenGL
  check verified page borders in light and dark themes and around adjoining white thumbnails
  with gaps and page numbers disabled.
- The border fix is uncommitted for review. Both original stashes remain intact.
