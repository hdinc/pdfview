# View fit, page gaps, drag zoom, settings window and toolbar

## Status

In progress since 2026-11-24. Steps 1 and 2 done; step 2 awaits review.

## Goal

Six changes from the 2026-11-24 review, each built, tested and left uncommitted for review before
the next:

1. Fit Width makes the page (or row) exactly as wide as the view, and Fit Page makes it exactly fill
   the view's width or height. Fitted pages touch the view's edges.
2. A global on/off setting for the gaps between pages, Overview included.
3. Ctrl + middle-button drag zooms around the point where the drag started: up zooms in.
4. The Facing Pages (Center First Page) view mode is removed; plain Facing Pages stays.
5. A Settings window that gathers the preferences now spread over the Settings menu and the Fonts
   window. The shortcut editor stays its own window, opened from there.
6. The toolbar in three groups, left, centre and right, laid out like Okular's (the reference
   screenshot from the review) in our icon style, with only features pdfview has.

Success: the checks under each step pass, all CTest targets pass, and the behaviour holds in the
app on Windows.

## Context

- **Fit modes.** `View::fitWidth` and `View::fitPage` (`src/Layout.cpp`) divide the view by the page
  size plus `2 * Layout::gap`, so a fitted page keeps a 10-point margin on each side. That margin
  is in document points, so it grows with the zoom. `View::goToPage` also places the view
  `gap / 2` above the row and `gap / 2` left of the page, so Fit Page doesn't start at the page's
  top edge. The viewer's canvas always reserves the scrollbar strip (`Viewer::draw`, `canvas.x =
  avail.x - scrollbarW()`), which may read as a second gap on the right when nothing scrolls.
- **Gaps.** `Layout::gap` (10 points) is used around and between pages and rows
  (`Layout::build`, `rowAt`, `band`, `rowWidth`), in `View::clamp` and `goToPage`, and in
  `Viewer::position` (`pageAt(cx, mView.y + mLayout.gap)`). Changing it needs a relayout that keeps
  the reading position (`Viewer::relayout` already keeps it for mode changes).
- **Middle button.** The canvas `InvisibleButton` takes the left and middle buttons. A middle press
  starts `Autoscroll` (`Viewer::handleMouse`, `updateAutoscroll`). Ctrl+wheel already zooms with
  `zoomAround` and sets `ZoomMode::Free`; tiles re-render after the zoom settles (`mSettling`).
- **Settings today.** The Settings menu holds Configure Keyboard Shortcuts, Theme (with Edit
  Themes...), Interface Size, Toolbar Size, Fonts..., Animations, Overview columns and Use This
  View Mode for New Files (`App::drawMenuBar`, `drawLookMenu`). Fonts, the theme editor and the
  shortcut editor are separate windows. `Settings` persists them; saves merge across windows
  (`Settings::saveMerged`).
- **Toolbar today** (`App::drawToolbar`), left to right: Sidebar | Back, Forward | Previous Page,
  Next Page, page box | Zoom Out, zoom combo, Zoom In, Fit Width, Fit Page | View Mode | Browse,
  Text Selection | Find, Add Bookmark. That is 14 buttons and 2 fields, in 7 groups, all left
  aligned. The zoom combo already lists Fit Width and Fit Page (`Viewer::drawZoomCombo`).
- **Reference layout** (Okular, from the review): left: sidebar toggle | zoom in, zoom out, zoom
  combo, View Mode with a drop-down; centre: previous page, page box, "1 of 213", next page; right:
  Browse, Text Selection, an annotation tool, the main menu. Buttons there carry text labels.
- **Center first page.** `ViewMode::firstAlone` (`include/pdfview/Layout.h`) puts the first page on a
  row of its own in `Layout::build`. It is the `view.facing_first` action and View Mode menu item,
  and `first_alone=` in the settings' `[settings]` (default mode) and `[file]` sections. The
  "Layout: view modes" test covers it.
- Related: [doc/plans/2026-11-22-viewer-interaction-updates.md](2026-11-22-viewer-interaction-updates.md)
  (autoscroll, the page context menu, settings merging).

## Approach

Work in the order below. The fit fix comes first because the gap setting changes the same
geometry, removing the first-alone mode comes before the settings window so the window doesn't
offer it, and the toolbar comes last because it gets the button for the new Settings window.

- **Fit without margins.** Fit zooms use the page or row size alone, and fitted pages touch the
  view's edges. The gaps stay between pages and rows. Fit Page positions the page's top edge at the
  view's top, and a fitted width puts the page's left edge at the view's left. Find out whether the
  reserved scrollbar strip is part of the reported gap, and if so draw the scrollbar over the
  canvas or drop the strip when nothing can scroll.
  - Rejected: keeping a margin of a few screen pixels (decided in review).
- **Gaps setting.** A global on/off preference. Off sets `Layout::gap` to 0 in every view mode,
  Overview included, applied to every tab with a relayout that keeps the position. It is offered
  in the Settings window and in the View Mode popup. With gaps off, adjacent pages get a thin
  border, as thumbnails have, so pages stay distinct.
  - Rejected: an adjustable gap size, and per-document gaps (decided in review).
- **Ctrl + middle drag zoom.** A middle press with Ctrl held starts a zoom drag instead of
  autoscroll. Vertical movement sets the zoom exponentially from the starting zoom, so equal drags
  give equal zoom ratios, about ×2 per 200 pixels at scale 1. Dragging up zooms in. The document point under the press
  stays in place. Release ends it, and it sets Free zoom like Ctrl+wheel. Plain middle-button
  gestures stay as they are. The sidebar ignores Ctrl + middle.
- **Remove center first page.** Drop `ViewMode::firstAlone`, its layout branch, the
  `view.facing_first` action and its menu item. Settings files keep loading: `first_alone=` is
  read and ignored, so such documents open as plain Facing Pages. Saved shortcuts naming
  `view.facing_first` are skipped as unknown ids, as `Actions::load` already does.
- **Settings window.** One ImGui window (Ctrl+Comma, and Settings > Settings...) with sections:
  - Appearance: theme (and Edit Themes...), interface size, toolbar size, interface and monospace
    fonts and size (from the Fonts window, which goes away).
  - Viewing: default view mode for new files, page gaps, overview columns, animations.
  - Keyboard: a button that opens the shortcut editor, which stays its own window.

  Changes apply at once and save as they do now. The Settings menu shrinks to Settings... and
  Configure Keyboard Shortcuts....
  - Rejected: a modal dialog with OK/Cancel. Settings apply live everywhere else in the app, and a
    modal would block seeing the effect.
- **Toolbar.** Three groups after the reference layout, icon buttons with tooltips (our style, no
  text labels), drop-downs shown with a small arrow:
  - Left: Sidebar | Zoom In, Zoom Out, zoom combo (which holds Fit Width and Fit Page) | View Mode
    with its drop-down.
  - Centre, in the middle of the toolbar: Previous Page, page box, "of N", Next Page.
  - Right: Browse, Text Selection | Find | Settings (opens the Settings window).
  - Off the toolbar, still in menus and on their shortcuts: Back, Forward (Go menu, Alt+Left/Right),
    Fit Width and Fit Page buttons (zoom combo, View menu), Add Bookmark (Bookmarks menu, page
    context menu, Bookmarks sidebar, Ctrl+B).
  - When the window is too narrow for the centre group to sit in the middle without overlapping,
    it follows the left group instead, and the right group stays right aligned.

## Steps

- [x] **Fit fills the view.** (Done: the layout lost its outer margin altogether, so gaps are only between pages and rows; the scrollbar strip is dropped when nothing scrolls at the full width.) Change `View::fitWidth`, `View::fitPage` and the fitted placement in
  `goToPage`/`clamp` to drop the outer margin, for one column, several columns and non-continuous
  rows. Check the scrollbar strip's part. Tests in `tests/test_pdfview.cpp` ("Layout and View
  geometry"): a fitted page's screen width equals the view width; Fit Page equals the view's width
  or height, with the page's top at the view's top; this holds for mixed page sizes, two columns
  and first-alone. Check in the app at 100 % and 150 % interface size.
- [x] **Page gaps setting.** (Done: `Viewer::setPageGaps`, `page_gaps=` and the "Gaps Between Pages" toggle in the View Mode menu; the Settings window gets it in step 5.) Make the gap a viewer setting (`Viewer::setPageGap`, points) that relays
  out and keeps the position. Persist it (`page_gap=` in `[settings]`, merged like other
  preferences) and apply it to all tabs. Draw a thin page border when the gap is 0. Tests: Layout
  with gap 0 (pages touch, `rowAt`/`pageAt` still right at the joins), position kept across a gap
  change, the settings round trip and merge.
- [ ] **Ctrl + middle drag zoom.** Add the zoom drag to `Viewer::handleMouse`: Ctrl at the middle
  press chooses zoom over autoscroll; the press point stays fixed; Free zoom; cursor ResizeNS;
  release, focus loss or Escape ends it. Tiles settle as after Ctrl+wheel. Unit-test the zoom curve
  (equal drags give equal ratios, clamped to `kMinZoom`/`kMaxZoom`); check in the app that the
  point under the cursor stays still and plain middle-button autoscroll is unchanged.
- [ ] **Remove center first page.** Remove `firstAlone` from `ViewMode`, `Layout::build`, the
  actions, the View Mode menu and `Settings` (reading `first_alone=` still works and is ignored).
  Update "Layout: view modes". Check an old settings file with `first_alone=1` loads as plain
  Facing Pages and saves without the key.
- [ ] **Settings window.** Add `App::drawSettingsWindow` with the sections above, the
  `settings.open` action (Ctrl+Comma), and the reduced Settings menu. Fold the Fonts window in.
  Check each control applies live, saves, and survives a restart; check two windows' saves still
  merge.
- [ ] **Toolbar in three groups.** Rearrange `App::drawToolbar` into the left, centre and right
  groups above, with the Settings button. Check every removed button's action is still reachable
  from a menu and its shortcut, the centre group is centred on a wide window, and the groups don't
  overlap at 100 % on a 1,300-pixel window at every toolbar size, or when the sidebar is open.

## Open questions

None. Answered in review (2026-11-24): toolbar in left, centre and right groups like the reference,
our style, existing features only; fitted pages touch the edges; gaps on/off, global, Overview
included; drag up zooms in; the shortcut editor stays its own window. Removing Facing Pages (Center
First Page) was added in the same review.
