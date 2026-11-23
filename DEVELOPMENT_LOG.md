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

## 2026-11-22 21:31 +03:00 — Continue thumbnail drags across pages

- Keep the thumbnail press captured by the panel when the pointer crosses a gap, leaves the
  panel, or the starting thumbnail scrolls out of view. Use the starting thumbnail's scale and
  the document's page heights to map pointer motion through pages in reading order.
- Preserve the grab offset inside the viewport indicator. Ordinary clicks still jump to the
  chosen page and preserve sidebar scroll. Release capture on mouse-up, focus loss, or tab change.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows OpenGL
  check verified mixed-height pages in a multi-column grid, dragging in both directions with
  the source thumbnail out of view, non-continuous navigation, ordinary clicks, and cancellation.
- The drag and thumbnail border fixes are both uncommitted. Both original stashes remain intact.

## 2026-11-22 21:35 +03:00 — Change thumbnail columns one notch at a time

- Ctrl+wheel now moves the grid by one column per notch within the existing thumbnail size
  limits. Fractional deltas accumulate while Ctrl and grid hover are maintained. The target
  column count maps back to the saved thumbnail size so resizing can still reflow the grid.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows
  OpenGL check verified both directions, fractional and multi-notch events, size limits,
  plain wheel scrolling, the top-thumbnail anchor, and saving the target size.
- Thumbnail borders and dragging were committed separately as d1f28a3 and d4746d6.
  The Ctrl+wheel fix remains uncommitted for review. Both original stashes remain intact.

## 2026-11-22 21:46 +03:00 — Keep thumbnail textures warm across tab switches

- Deactivating a tab cancels tile work and drops document tiles while retaining preview and
  whole-page thumbnail textures. Inactive tabs share a 128 MiB GPU budget. Trimming removes
  undrawn and least recently displayed textures first. Closing or reloading frees the old cache.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows OpenGL
  check verified texture reuse on return, document tile release, cross-tab eviction with a
  reduced test budget, re-rendering on demand, and invalidation on close and reload. The
  worker check verified cancellation of an in-flight tile and subsequent reuse of the worker.
- The Ctrl+wheel fix is committed as b638c15. Thumbnail caching is uncommitted and both
  original stashes remain intact.

## 2026-11-22 22:01 +03:00 — Prioritize visible document pages across tab switches

- The initial cache change kept thumbnails while discarding the sharp tiles used in the
  viewport. Deactivation now retains the textures drawn in the last viewport and frees
  off-screen document tiles. The shared 128 MiB budget evicts thumbnail-only textures
  first and then the oldest visible textures under pressure.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows
  OpenGL check switched between three tabs and verified that both visible pages used the
  same sharp texture IDs on return, with no new requests for those tiles. A reduced test
  budget verified eviction priority. Close, reload, and tile cancellation also passed.
- The revised cache fix remains uncommitted. Both original stashes remain intact.

## 2026-11-22 22:07 +03:00 — Debounce thumbnail filtering

- Wait 300 ms after the last thumbnail search edit and keep the existing grid until the
  replacement results are complete. Clearing the field restores all thumbnails immediately.
  Each document tab retains its own filter.
- Document Find and thumbnail queries use separate channels on the same render worker.
  A new thumbnail query invalidates older results without changing the document query or viewport.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed, including coverage
  for replacing a thumbnail query during document Find. A Windows OpenGL check verified actual
  typing and clearing, complete results, no-match results, a stable viewport and Find query,
  and tab switching.
- The visible-page cache fix is committed as b6532d7. Thumbnail filtering is uncommitted
  for review, and both stashes remain intact.

## 2026-11-22 23:01 +03:00 — Restrict multi-click selection to Text Selection mode

- Text Selection mode handles double-click word selection and triple-click line selection.
  Browse mode uses ordinary link clicks and drag-to-pan.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows OpenGL
  check verified Browse double- and triple-click behavior, panning, an internal link click,
  and word and line selection in Text Selection mode.
- The thumbnail filter fix is committed as beed698. Browse mode click handling is
  uncommitted for review, and both original stashes remain intact.

## 2026-11-22 23:06 +03:00 — Add a context menu to document pages

- The app's right-click menu shows the clicked page label and offers Copy, Add Bookmark,
  Fit Width, Previous Bookmark, and Next Bookmark. Add Bookmark saves the clicked page.
  Previous and Next follow the current document position and advance through saved offsets
  even when a jump lands above its bookmark.
- Fit Width from Overview reopens the clicked page or spread in the previous normal layout,
  keeping the current continuous setting. The viewer supplies a default menu for hosts
  and allows them to provide their own items.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows
  OpenGL check verified the page hit test, single-page and facing layouts, bookmarks and
  same-page offsets, clipboard copying, and the default widget menu.
- The Browse click fix is committed as fb22bd1. The page context menu is uncommitted for
  review. Both original stashes remain intact.

## 2026-11-22 23:18 +03:00 — Style the page menu like the review reference

- Added a rounded, centered page badge, aligned line icons, and spacing based on the reference.
  The popup follows the active theme, and unavailable bookmark actions are muted. Copy appears
  only with selected text.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. Captured and
  inspected the Windows OpenGL menu in both light and dark themes. The interaction check
  passed for bookmarks, Overview Fit Width, and copying selected text.
- The page menu remains uncommitted for review. Both original stashes remain intact.

## 2026-11-22 23:26 +03:00 — Restore standard styling to the page menu

- Replaced the custom badge, spacing, and drawn icons with the standard ImGui separator
  and menu items. The actions use existing Font Awesome glyphs, and Copy appears with a selection.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. The Windows
  OpenGL check verified menu rendering and the bookmark, Overview Fit Width, and Copy actions.
- The page menu remains uncommitted. Both original stashes remain intact.

## 2026-11-22 23:28 +03:00 — Keep non-continuous autoscroll on the current page

- Removed accumulated edge travel and automatic row turns from middle-button autoscroll.
  The view clamps to the current row in non-continuous mode, and reversing pointer movement
  scrolls back immediately when there is room to move.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. Windows OpenGL
  checks verified both edges of a zoomed page, a fitted page, immediate reversal, continuous
  autoscroll, the middle-button gestures, and ordinary wheel and Space page turns.
- The page menu is committed as ede9821. The autoscroll fix is uncommitted for review.
  Both original stashes remain intact.

## 2026-11-22 23:38 +03:00 — Unify document scroll animation and add an Animations toggle

- Wheel and line shortcuts glide over 150 ms. Space and Shift+Space, gg/G, and their alternate
  bindings use the 600 ms ease-out. Repeated inputs accumulate at the queued destination.
  In non-continuous view, zoomed rows glide to their edge before another input turns the
  page. Fitted rows and first or last commands crossing rows turn immediately.
- Settings > Animations defaults to on and is saved on change. Turning it off completes the
  queued move and makes later moves immediate. Embedding hosts can set the viewer option.
- Rebuilt build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. Windows OpenGL
  checks covered wheel and keys, repeated and reversed input, history, row turns, the Settings
  toggle and persistence, and middle-button autoscroll. Across 11 page-gap frames the viewport
  matched the easing curve with no measured displacement error. Mean frame time was 16.3 ms
  at gap crossings and 15.5 ms elsewhere in that run.
- The autoscroll fix is committed as b0a8855. Document animations remain uncommitted for
  review. Both original stashes remain intact.

## 2026-11-22 23:47 +03:00 — Soften scroll starts and make held line scrolling continuous

- Switched the 150 ms and 600 ms glides to quadratic ease-out, reducing their initial speed.
  A held scroll-up or scroll-down binding transitions after 50 ms to a steady 540 screen
  pixels per second at scale 1. Releasing the key stops movement. A zoomed non-continuous
  row stops the hold at the edge, and fitted rows still turn at the ordinary repeat rate.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. Windows OpenGL
  checks measured held J/K at variable frame intervals, verified release, a rebound shortcut
  and modifier release, zoomed row edges, and tab switching. The document navigation,
  Animations toggle, and page-gap checks also passed.
- The scroll animation fix is uncommitted. Both original stashes remain intact.

## 2026-11-22 23:57 +03:00 — Cap glide speed and restore immediate document jumps

- gg/G and Home/End jump immediately and still record navigation history. Increased
  J/K and Up/Down taps from 60 to 90 pixels at scale 1. Held vertical keys still scroll
  steadily at 540 pixels per second at scale 1.
- Longer glides extend their duration to cap total viewport speed at 1,600 pixels per
  second at scale 1, including diagonal motion.
- Built build/bin/pdfview-app.exe with UCRT64. All six CTest targets passed. A Windows
  OpenGL check measured a peak of 1,599.88 pixels per second at scale 1 and an exact
  landing. Immediate jumps, larger taps, held keys, page turns, and the toggle passed.
- The animation fix is uncommitted. Both original stashes remain intact.

## 2026-11-23 22:11 +03:00 — Rewrite document scroll glides as a damped spring

- **Changes made:** Replaced the fixed-duration quadratic ease-out with `ScrollMotion`
  (new header-only `include/pdfview/ScrollMotion.h`, no ImGui dependency). The old glide
  restarted its curve from the current position on every wheel notch or key press, so the
  speed jumped with each input, and the speed cap stretched the duration, which changed
  the curve's shape.
- **How it works:** The view is pulled towards the queued target by a critically damped
  spring, integrated in 1 ms substeps so the path does not depend on the frame rate.
  - Wheel notches and line keys add an impulse equal to stiffness × step. From rest this
    is an exact exponential decay: full speed at once, 90 % of the way in 130 ms and at
    rest in about 300 ms (stiffness 18/s).
  - Space/Page Down add no impulse: they speed up from rest, cruise at the cap and brake.
    A 900-pixel screenful is 90 % done in 550 ms and at rest in about 870 ms (stiffness 20/s).
  - The pull saturates at 2 × maxSpeed / stiffness, so long moves cruise at 1,600 px/s at
    scale 1 and brake without overshooting. Velocity is also clamped to the cap.
  - Input arriving mid-glide moves the target and only adds its impulse, so speed stays
    continuous; a reversed notch turns the motion back smoothly.
  - Held J/K now feed per-frame steps into the same motion (540 px/s, about 27 px behind
    the target) and coast to a stop over about 250 ms on release, instead of switching to
    immediate movement and dropping the tap's remaining glide. With animations off they
    still move immediately.
  - The first frame of a glide counts as at most 1/60 s, because it can follow a long
    idle wait; later frames catch up by at most 50 ms.
  - `Viewer::endScroll` ends a glide; `cancelScroll` also forgets partial notches at a
    non-continuous row edge, so precision touchpads still turn rows with animations off.
  - Row-edge turns, immediate gg/G and Home/End, and the Animations toggle keep their
    previous behavior.
- **Testing:** New `tests/test_scroll_motion.cpp`: a step's initial speed and landing
  without overshoot, a screenful's ease-in, speed cap and landing, the same path at 30 and
  150 fps, continuous speed on retarget, reversal, and stop. All six CTest targets passed
  with the UCRT64 build. A scratch program measured the timings above at 60 fps. The feel
  in the running app was not checked by hand.
- **Outstanding issues:** The 1,600 px/s cap also limits fast wheel spins to about
  20 notches per second; raise it if fast spins feel like they lag.
- **Next steps:** Try the glides by hand in the app, then reuse `ScrollMotion` for the
  sidebar panels.

## 2026-11-23 22:20 +03:00 — Stop held Space scrolling on release

- **Changes made:** Holding Space/PgDown (or Shift+Space/PgUp) used key repeat, which
  queued another screenful on every repeat; after release the view kept going through all
  of them. The page actions now have a hold handler, `Viewer::pageHeld`, so key repeat no
  longer queues screenfuls while the key is down.
- **How it works:** While held, the destination is kept 1.25 × the braking distance ahead
  of the view (`ScrollMotion::brakingDistance` = 2 × maxSpeed / stiffness, about 200 px at
  scale 1 and 100 %). The view cruises at 1,600 px/s; on release the target stops moving
  and the view brakes as the end of a single press does, within about 0.4 s. The first
  press's screenful still completes. With animations off, or on a fitted non-continuous
  row, the hold falls back to ordinary key repeat (whole screenfuls, or row turns). A
  zoomed non-continuous row stops at its edge while held. `Viewer::glideParams` now holds
  the per-glide spring settings in one place.
- **Testing:** Added a ScrollMotion test: holding the target ahead reaches the cap, and
  after release the motion stops within the lead distance in under 0.5 s without
  overshoot. All six CTest targets passed. Not yet checked by hand in the app.

## 2026-11-23 22:43 +03:00 — Double the scroll speed cap

- **Changes made:** `kMaxScrollSpeed` went from 1,600 to 3,200 px/s at scale 1, and
  `kPageStiffness` from 20 to 32/s, so braking stays short at the higher speed.
- **How it works:** Measured at 60 fps with the ScrollMotion model: a single 900-pixel
  screenful is 90 % done in 283 ms (was 550 ms) and at rest in 500 ms (was 867 ms). Held
  Space cruises at 3,200 px/s and stops about 200 px (267 ms) after release (was 173 px,
  383 ms). Wheel and line steps are unchanged for single notches; fast wheel spins can now
  reach about 40 notches per second before the cap.
- **Testing:** All six CTest targets passed. Not yet checked by hand in the app.

## 2026-11-23 22:49 +03:00 — Match held J/K speed to repeated presses

- **Changes made:** Held scroll-up/scroll-down (J/K, Up/Down) moved at a fixed 540 px/s,
  slower than tapping, while held Space already matched repeated presses. The held speed
  is now one 90-pixel line per `ImGuiIO::KeyRepeatRate` interval: 1,800 px/s at scale 1
  with ImGui's 50 ms default, the speed key repeat would give, but continuous.
- **How it works:** The hold lambdas in `Viewer::registerActions` read the repeat rate
  each frame (falling back to 50 ms if it is unset). On release the view coasts about
  100 px (one line) over roughly 300 ms, as the step glide's stiffness of 18/s implies.
- **Testing:** All six CTest targets passed. Not yet checked by hand in the app.

## 2026-11-23 23:00 +03:00 — Simplify the document scroll code

- **Changes made:** Removed duplicate glide state and repeated logic without changing
  behavior.
  - `ScrollMotion` is now the only glide state; the `std::optional<View> mScroll` copy of
    its target is gone. A glide never leaves its row (row turns cancel it), so
    `Viewer::destination()` rebuilds the target view from `mView` and the motion's target.
  - The first-frame time cap (`mScrollFresh`) moved into `ScrollMotion`, so the sidebar
    gets it for free when it adopts the class.
  - New helpers `rowFits()` (three copies of the fitted-row test) and `glideBy()` (the
    add-at-destination, clamp and glide sequence, shared by wheel, line and held-key steps).
  - `endScroll` merged into `cancelScroll`; `scrollHeld` lost its own animations-off
    branch, since `scrollTo` already moves immediately then.
  - Removed the new `Viewer::setAnimations`: the app sets `options().animations`, and
    `updateScrollAnimation` finishes a running glide on the next frame when it is off.
- **Testing:** Added a test for the first-step cap and moved the frame-rate test past it.
  All six CTest targets passed. Not checked by hand in the app.

## 2026-11-23 23:04 +03:00 — Held J/K jump by lines when animations are off

- **Changes made:** With Animations off, a held scroll-up/scroll-down key still moved a
  little every frame, which looked smooth. `Viewer::scrollHeld` now declines the hold
  when animations are off, as `pageHeld` already did, so ImGui's key repeat runs the
  action and the view jumps one 90-pixel line per repeat.
- **Testing:** All six CTest targets passed. Not checked by hand in the app.

## 2026-11-23 23:08 +03:00 — One hold rule for line and page keys

- **Changes made:** Held J/K (per-frame impulse steps) and held Space (destination kept
  ahead) had separate code. `Viewer::pageHeld` is gone; `Viewer::scrollHeld(step)` now
  serves both, given one press's signed step in screen pixels. The registry's hold
  lambdas come from two small factories, and `Viewer::screenful()` names the 0.9-screen
  page step that `scrollPage` and the holds share.
- **How it works:** A held key uses the screenful glide's spring (stiffness 32/s, no
  impulse) with its speed limited to step ÷ `KeyRepeatRate`, under the 3,200 px/s cap:
  1,800 px/s for 90-pixel lines at ImGui's 50 ms, the cap for screenfuls. The destination
  stays 1.25 braking distances ahead, so release brakes like the end of a single press, and
  a press's own step still completes. Measured at 60 fps: held J moves 1,783 px in the
  first second and coasts 111 px in 233 ms on release (was about 100 px in 300 ms); held
  Space is unchanged (3,162 px/s, 197 px in 267 ms).
- **Testing:** All six CTest targets passed. Not checked by hand in the app.

## 2026-11-23 23:16 +03:00 — Smooth wheel scrolling and autoscroll in the sidebar

- **Features implemented:** Thumbnails, Contents and Bookmarks glide on the wheel with the
  document's step spring (stiffness 18/s, 3,200 px/s cap) and jump when Animations is off.
  The document's middle-button gestures work in each panel: a middle click latches
  autoscroll until the next click, wheel, key or focus loss; a middle drag autoscrolls
  until release. Autoscroll works with animations off.
- **How it works:** New `PanelScroll` (`include/pdfview/PanelScroll.h`,
  `src/PanelScroll.cpp`), called right after the panel window's Begin (the thumbnail
  child, or the Contents and Bookmarks tables' scrolling inner window).
  - It sets `ImGuiWindowFlags_NoScrollWithMouse` on the current window after Begin. ImGui
    reads the flag at the next NewFrame, so its own wheel handling skips the panel and
    only the hovered panel takes the wheel.
  - It keeps an unrounded position, since ImGui rounds the scroll. When the window's
    scroll differs from it by more than a pixel (a jump to the current item, the
    thumbnail grid following the document or keeping its anchor), it lets go at once.
    Dragging the scrollbar, a different window ID (panel or document tab switch), a
    skipped frame and focus loss cancel too.
  - The wheel step is ImGui's own (5 × font size, at most two thirds of the panel), so
    distances are unchanged; Ctrl+wheel stays with the thumbnail grid's column change.
  - While autoscrolling, it owns the left, right and middle mouse buttons, so the click
    that stops it doesn't also open a thumbnail or heading. The marker is drawn on the
    foreground draw list, clipped to the panel.
  - `autoscrollSpeed` and `drawAutoscrollMarker` are now shared with the document view,
    and the glide constants live in `ScrollMotion` (`kStepStiffness`, `kPageStiffness`,
    `kMaxSpeed`).
  - The panels and the app's bookmark list report `needsFrames()`; a panel no longer
    drawn stops asking.
- **Testing:** New `tests/test_panel_scroll.cpp` runs a headless ImGui context: a notch
  glides and returns, no animation jumps, the wheel outside the panel is ignored, a latched
  autoscroll moves and a left click stops it without pressing the button underneath, a
  middle drag stops on release, and an outside SetScrollY takes over a glide. All six CTest
  targets passed. The app was launched for five seconds with no errors; the sidebar was
  not tried by hand.
- **Next steps:** Check the panels by hand, then move `ScrollMotion` and the gesture
  state into a header-only Hd component.

## 2026-11-23 23:24 +03:00 — Move the scroll spring and autoscroll gesture into Hd

- **Changes made:** `include/pdfview/ScrollMotion.h` is gone; the spring, the autoscroll
  speed curve and the middle-button gesture state are Hd's header-only `Hd/Scroll.h`
  (`deps/Hd/scroll/`, target `Hd_scroll`). The viewer's `mAutoscrolling` and
  `mAutoscrollOrigin` became an `Hd::Autoscroll`, and `PanelScroll` uses the same class, so
  latching and drag-release share one implementation. pdfview's ScrollMotion tests moved to
  Hd (`hd_scroll_test`).
- **How it works:** The viewer still handles the canvas press. A middle press starts the
  gesture; `updateAutoscroll` calls `track` each frame, which latches on a release without
  movement and stops after a drag. The viewer adds its own rule: a release outside the
  canvas, or a deactivation without a release, stops it. The drag threshold is now 3 px ×
  UI scale (was 3 px). Input ownership, the marker and page turns stay in pdfview.
  `pdfview` links `Hd_scroll` publicly. When no target of that name exists (a host without
  Hd, or with an older Hd), CMake adds `deps/Hd/scroll` on its own, which exposes no other
  Hd headers.
- **Testing:** All seven CTest targets passed with `HD_BUILD_TESTS=ON`, including the new
  `hd_scroll_test` and the PanelScroll tests. A scratch host project with only Hd's
  `deps/imgui` and `deps/glad` as its own targets, `PDFVIEW_BUILD_APP=OFF`, built and linked
  the widget; its build files never reference Hd's main include directory. The app was not
  tried by hand after this change.

## 2026-11-23 23:28 +03:00 — Keep the scroll code in pdfview; defer the Hd extraction

- **Changes made:** Reverted the uncommitted move of the scroll code into Hd (the previous
  entry). Hd is unchanged. `include/pdfview/ScrollMotion.h` is back in the `pdfview`
  namespace and now also holds `autoscrollSpeed` and the `Autoscroll` gesture, so the
  document view and `PanelScroll` still share one gesture implementation. The
  `Hd_scroll` target and the CMake fallback that added `deps/Hd/scroll` are gone.
- **How it works:** Why: a host building only the library with its own ImGui and Glad
  could do so without the Hd submodule checked out, and the move would have made the
  checkout mandatory for one header. Only pdfview uses the code, and while the feel is
  still being tuned each change would have needed an Hd commit, a pdfview commit and a
  submodule bump. The header stays free of ImGui, so a later move is mechanical.
- **Testing:** `tests/test_scroll_motion.cpp` is back, with new cases for the speed curve
  and the gesture (latch on click, stop after a drag, threshold following the UI scale).
  All CTest targets passed. Not tried by hand in the app.

## 2026-11-23 23:37 +03:00 — Merge settings saves from several app windows

- **Changes made:** Each pdfview-app process loaded `settings.ini` once and saved its whole
  state, so with two windows open the later save erased the other's bookmarks, recent files,
  views and preferences. On Windows the rename could also fail while the other read the file.
- **Features implemented:** Saves now merge. Preferences, the window's geometry, each
  shortcut, each file's view and each bookmark keep the local value only where it changed
  locally since the last load or successful save; everything else comes from the file's
  latest contents. The later save wins when both changed the same field or bookmark. The
  open tabs and active tab are always the saving window's, so a launch without file
  arguments restores the most recently saved window. Explicit file arguments still open
  just those files.
- **How it works:**
  - `Settings::saveMerged(path, base)` takes `Hd::FileLock` on `settings.ini.lock` (5 s
    timeout), loads the file, builds `Settings::merge(disk, local, base)` and writes it with
    `Hd::replaceFile`. On failure the file is unchanged and `App::mSaved` (the base) stays
    as it was, so the next save retries the same changes. Startup reads under the lock
    (`loadLocked`).
  - Merge rules: scalar preferences one by one; window geometry as one value; shortcuts by
    action id (a reset removes the line); files present only on one side are kept; a file's
    view (position, zoom, mode) as one value; `used` is the maximum. Bookmarks by id:
    local additions, removals and edits since base win; a bookmark removed elsewhere is
    dropped unless edited here. Recent files: the file's list minus entries removed here
    (Clear List), plus entries opened here since base, ordered newest first by `used`.
  - Bookmark ids: random 64-bit values, saved as `bookmark_id=<hex>` after each
    `bookmark=` line, which older versions ignore and still read the bookmark. Bookmarks
    from older files get an FNV-1a hash of page, y, title and their position among twins,
    so every window reading the same file agrees.
  - `used` and `clock` are now wall-clock milliseconds (always increasing within a
    window), so "newest" compares across windows. Older small counters sort as older.
  - `Settings::load` resets to defaults first; `text()` is the old serialiser.
- **Testing:**
  - New `pdfview_settings_tests` (with the app; compiles `app/Settings.cpp`): a round trip
    with ids; legacy bookmarks getting identical ids in two loads, twins kept apart and a
    title with `|`; two windows merging preferences, a removal, an edit, an addition,
    shortcuts and recent files, then a second save keeping the other's changes; the later
    save winning on the same theme, bookmark and view; Clear List keeping a file opened
    elsewhere since; four writer processes making 15 merged saves each at the same time,
    losing none of the 60 bookmarks and leaving no temporaries.
  - Two real pdfview-app processes with a separate APPDATA, each opening a different PDF,
    closed by window message in both orders: the settings kept both files in the recent
    list and file states, the session held the last-closed window's tab, and no temporary
    files were left.
  - All eight CTest targets passed (UCRT64, Windows). Not tested on Linux.
- **Outstanding issues:** A window doesn't pick up another window's changes while it runs
  (its UI keeps its own preferences until restarted); only its saves merge.
- **Recommended features:** Hd's theme editor can adopt `Hd::replaceFile`.

## 2026-11-24 00:25 +03:00 — Fitted pages reach the view's edges

- **Changes made:** Fit Width and Fit Page left visible gaps. The fit zooms divided by the
  page plus a 10-point margin on each side (a margin in document points, so it grew with the
  zoom), page jumps placed the view half a gap above and left of the page, and the scrollbar
  strip stayed reserved and filled even when nothing could scroll.
- **How it works:**
  - `Layout::build` puts no margin around the document: the first page starts at 0, `width`
    and `height` end at the last page's edges, and `gap` is only between pages and rows.
    `rowWidth` and the non-continuous `View::band` lose their gap terms, so a row's scroll
    range is exactly the row.
  - `View::fitWidth` uses the page's width (one column) or the layout's width; `fitPage`
    the page's or row's size. `goToPage` puts the row's top and, when bringing a page in
    sideways, its left edge exactly at the view's.
  - `Viewer::needsScrollbar` decides the strip from the full width only, with the zoom the
    current fit mode would give there, so the decision can't oscillate as the strip comes
    and goes. Without it, the canvas takes the whole width and no scrollbar is drawn.
  - Rejected: a margin of a few screen pixels (the review asked for touching edges).
  - With facing pages of mixed widths, each column is as wide as the widest page, so a row
    of narrow pages still leaves space beside them; same-size pages fill the width exactly.
- **Testing:** Updated "Layout and View geometry" and "Layout: view modes" for the missing
  margin. New "fitted pages reach the view's edges": Fit Width makes each of three mixed
  pages exactly the view's width with its left edge at the view's; Fit Page fills the width
  or height without exceeding either, top at the view's top; the scroll ends are the first
  page's top and the last page's bottom; two columns and first-alone fill the width; a
  non-continuous fitted page leaves nothing to scroll. All eight CTest targets passed.
  Screenshots of pdfview-app (separate APPDATA, 1100 × 800): Fit Width flush at the left
  and top with the page's right edge at the scrollbar; non-continuous Fit Page touching top
  and bottom, centred, with no scrollbar strip; continuous facing with mixed sizes.
- **Next steps:** The page gaps setting (plan step 2).

## 2026-11-24 00:29 +03:00 — Turn the gaps between pages on and off

- **Features implemented:** View Mode > Gaps Between Pages (action `view.page_gaps`, no
  default key) turns the gaps between pages and rows on or off in every tab and view mode,
  Overview included. It is a global preference, saved as `page_gaps=` in `[settings]` and
  merged like the others.
- **How it works:** `Viewer::setPageGaps(bool)` sets `Layout::gap` to 10 points or 0
  (`kPageGap`) and relays out through `Viewer::relayout`, which keeps the reading position;
  `startLayout` applies it to a new document. With gaps off, pages draw without their drop
  shadow (it would fall on the next page) and get a 1-pixel border in `ImGuiCol_Border` over
  their contents, as thumbnails have. The app applies the preference to each new tab and to
  all tabs when toggled, then saves.
  - Rejected: an adjustable size and per-document gaps (decided in review).
- **Testing:** New "Layout without gaps: pages touch" (pages and rows meet; a join belongs to
  the page below; facing and overview columns meet). The settings round trip and the
  two-window merge now include `page_gaps`. All eight CTest targets passed. Screenshots of
  pdfview-app with gaps off and on in continuous single view, and off in Overview; the
  preference was still set after each run.
- **Outstanding issues:** In facing and Overview, columns are as wide as the widest page, so
  documents with mixed page widths still show space beside narrower pages with gaps off.
- **Next steps:** Ctrl + middle-button drag zoom (plan step 3).

## 2026-11-24 00:35 +03:00 — Ctrl + middle-button drag zooms

- **Features implemented:** Holding Ctrl while pressing the middle button on a document
  starts a zoom drag instead of autoscroll. Dragging up zooms in and down zooms out, around
  the point where the press began. Release, focus loss or Escape ends it. It sets Free zoom,
  as Ctrl+wheel does. Plain middle-button gestures are unchanged; the sidebar panels ignore
  Ctrl + middle.
- **How it works:** `View::dragZoom(start, dy, scale)` = start × 2^(−dy / (200 × scale)),
  clamped to the zoom limits, so equal drags give equal ratios and the distance follows the
  UI scale. `Viewer::mZoomDrag` records the starting zoom, the press point in the view and
  the press's screen y; each frame while the middle button is down, `handleMouse` applies
  `zoomAround` at the press point, so the document point under it stays put, and tiles
  re-render once the zoom settles as after Ctrl+wheel. The cursor shows ResizeNS. Opening or
  closing a document ends a drag. `PanelScroll` doesn't start autoscroll with Ctrl held.
- **Testing:** New "drag zoom" test: no movement keeps the zoom, 200 px up doubles it and
  down halves it, equal drags give equal ratios at different zooms, the distance follows the
  scale, and the limits clamp. New PanelScroll test: Ctrl + middle click starts nothing. All
  eight CTest targets passed. In pdfview-app, a synthesized Ctrl + middle drag of 200 px up
  from the red/blue boundary of a test page zoomed from 40 % to 57 % (×√2 at UI scale 2)
  with the boundary at the same screen x; a plain middle click afterwards latched autoscroll
  and scrolled the view (y 100 → 351), and a left click stopped it.
- **Next steps:** Remove Facing Pages (Center First Page) (plan step 4).

## 2026-11-24 00:37 +03:00 — Remove Facing Pages (Center First Page)

- **Changes made:** Removed the view mode that put the first page alone on its own row
  (requested in review). `ViewMode` is now `{columns, continuous}`; `Layout::build` lost the
  first-alone branch; the `view.facing_first` action and its View Mode menu item are gone.
  Plain Facing Pages is unchanged.
- **How it works:** Settings files still load: `first_alone=` in `[settings]` and `[file]` is
  read and ignored, so documents saved in that mode open as plain Facing Pages, and saves no
  longer write the key. A saved shortcut for `view.facing_first` is skipped as an unknown id,
  as `Actions::load` already does. Removing the middle member turned every three-value
  `ViewMode` initialiser into a compile error, so none could change meaning silently; no
  two-value initialiser existed before.
- **Testing:** Removed the first-alone layout and fit checks. New settings test: a file with
  `first_alone=1` loads as facing (keeping its continuous setting) and saves without the key.
  All eight CTest targets passed.
- **Next steps:** The Settings window (plan step 5).

## 2026-11-24 00:39 +03:00 — A Settings window

- **Features implemented:** Settings > Settings... (`settings.open`, Ctrl+Comma) opens one
  window with three sections, each a label column and controls:
  - Appearance: theme (System, the built-in and custom themes) with Edit Themes..., interface
    size, toolbar size, interface and monospace fonts, font size.
  - Viewing: the view mode new files open in (Single Page, Facing Pages, Overview, and
    Continuous), gaps between pages, overview columns, smooth scrolling (the Animations
    setting). The old "Use This View Mode for New Files" (copy the active document's mode)
    was dropped in review: the combo and checkbox set the same thing.
  - Keyboard: Configure Keyboard Shortcuts..., which stays its own window.
- **Changes made:** The Settings menu shrank to Settings... and Configure Keyboard
  Shortcuts.... Its Theme, Interface Size, Toolbar Size, Fonts..., Animations, Overview
  columns and Use This View Mode for New Files entries moved into the window; the separate
  Fonts window is gone (`drawLookMenu` and `drawFontsWindow` replaced by
  `drawSettingsWindow`). The installed fonts are listed when the window first opens.
- **How it works:** Every control applies at once and saves through `saveSettings` (so it
  merges with other windows); sliders save when released. The Overview columns slider now
  saves too; the old menu slider only reached the file at exit. The theme combo is the app's
  own, because `Hd::Theme::combo` has no "System" entry: it lists System, then
  `Hd::Theme::menuItems`, and shows the current theme's name through `Hd::Theme::find`.
  - Rejected: a modal dialog with OK/Cancel (settings apply live everywhere else).
- **Testing:** Built; all eight CTest targets passed. In pdfview-app, a synthesized Ctrl+Comma
  opened the window with every section and current value shown (screenshot).
- **Next steps:** The toolbar in three groups, with a Settings button (plan step 6).

## 2026-11-24 00:44 +03:00 — New files open in the last layout used

- **Changes made:** Replaced the Settings window's "New files open in" choice, its Continuous
  checkbox and the button that copied the current document's mode (requested in review). New
  files now open in the layout last chosen in any tab: Single or Facing Pages, continuous or
  not.
- **How it works:** `App::followLayout`, run each frame, compares each tab's view mode with
  the one it last saw (`Tab::mode`, set when the file opens). A change becomes
  `Settings::defaultMode`, which `Settings::file` gives new files, and is saved. Overview is
  ignored, and leaving it returns to the earlier layout without counting as a change, so a
  quick look at Overview doesn't decide how the next file opens. Restoring a known file's
  own layout isn't a change either. Files opened before keep their saved layout.
- **Testing:** All eight CTest targets passed. In pdfview-app with a separate APPDATA, a file
  saved in Facing Pages, with Facing Pages as the default: pressing I (Single Page), then O
  twice (into Overview and back), left the default at Single Page; opening a file not seen
  before then started it in Single Page.
