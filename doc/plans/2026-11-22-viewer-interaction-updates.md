# Viewer interaction updates

## Status

In progress since 2026-11-22.

## Goal

Improve thumbnail navigation and rendering, document and sidebar scrolling, page actions, and session persistence. Each change is built and tested on its own, then left uncommitted for review.

## Context

The previous five fixes are committed. Both stashes named `Previous viewer changes for individual review` remain available for reference.

Thumbnail dragging is tied to its starting thumbnail and clamps at the page edge. Switching document tabs releases the outgoing viewer's textures. Each thumbnail search edit clears the shared Find results before new matches arrive. The current scroll animation interpolates a document coordinate across page gaps. The reported change in apparent speed needs a runtime check.

## Approach

Keep thumbnail layout and filtering in the panel, document navigation in the viewer, and settings and app-specific menu actions in the standalone app. Move through the steps in order and review each before starting the next. Adapt only the relevant changes from the saved batch.

Extract the shared scroll motion code into Hd after the document and sidebar both use it. Put desktop file locking and atomic replacement in Hd when implementing concurrent session saves. Keep PDF navigation, settings merge policy, and the icon tab bar in pdfview. Move shortcut editing to Hd as a separate follow-up after the viewer fixes.

## Steps

- [x] **Fill the thumbnail viewport indicator.** Add a translucent fill inside the existing outline. Clip the indicator to each thumbnail's visible portion and draw it on every visible page when the viewport spans multiple pages.
- [x] **Border thumbnail pages.** Draw a thin, theme-aware border around each thumbnail. Check that adjacent pages remain distinct with gaps and page numbers disabled.
- [x] **Let thumbnail drags cross pages.** Capture the drag at the panel level so it survives leaving the starting thumbnail or scrolling it out of view. Convert vertical pointer displacement at the starting thumbnail's scale into movement through document pages in reading order, including when the pointer crosses empty space. Preserve the grab offset when dragging the indicator and keep ordinary clicks as page jumps. Release capture on mouse-up, focus loss, or tab change.
- [x] **Change thumbnail columns one step at a time.** One Ctrl+wheel notch changes the column count by exactly one in the existing direction. Accumulate fractional input until it reaches a notch. Keep plain wheel for panel scrolling and preserve the first visible thumbnail's anchor during layout changes.
- [x] **Keep visible document tiles across tab switches.** When a tab deactivates, keep the textures drawn in its last viewport and free off-screen document tiles. Retain thumbnails as budget permits. Inactive tabs share 128 MiB, with thumbnail-only textures evicted first and older viewports evicted when more space is needed. Keep the existing active-view budget and invalidate textures on close or reload. During review, the reported re-render was traced to the document viewport, so visible page tiles now take priority.
- [x] **Debounce thumbnail filtering.** Wait 300 ms after the last edit. Keep the current grid visible during the search and replace the results together when it completes. Clearing the field restores the full grid immediately. Keep the query and results per document. Use a separate worker search channel and a request generation to discard stale results. Keep the document position and Find bar independent of this filter.
- [x] **Keep Browse double-clicks from selecting text.** Limit double-click word selection and triple-click line selection to Text Selection mode. Preserve Browse link clicks and drag-to-pan.
- [x] **Add the page context menu.** Adapt the stashed menu for the page under the pointer. Use standard ImGui styling, a page label, and existing Font Awesome icons. Show Add Bookmark, Fit Width, Previous Bookmark, and Next Bookmark. Show Copy when text is selected. Reuse the viewer's page hit test and the app's bookmark actions. In Overview, Fit Width restores the last normal layout, preserves the continuous setting, and fits the clicked page or spread.
- [x] **Keep non-continuous autoscroll on the current page.** Clamp at the current row edge and keep autoscroll engaged so pointer movement can reverse it. Preserve the existing gestures and cancellation behavior.
- [x] **Unify document scroll animations and add a toggle.** Add a persisted **Animations** setting enabled by default and a viewer option for embedding hosts. Glides follow `ScrollMotion` (`include/pdfview/ScrollMotion.h`), a critically damped spring with a saturated pull. Wheel and line steps start at full speed and decay (90 % in 130 ms). Screenfuls speed up from rest, cruise and brake. Speed stays continuous when input arrives mid-glide and is capped at 3,200 pixels per second at scale 1 (raised from 1,600 on 2026-11-23). (Revised 2026-11-23: the fixed-duration quadratic ease-out restarted on every input, so the speed jumped and the motion felt unnatural.) J/K and their alternate bindings move 90 pixels per tap at scale 1. Held line and page keys share one rule: one step per key-repeat interval within the speed cap, braking on release as a single press ends. In non-continuous view, zoomed rows stop at the edge until another press. Fitted rows turn immediately and repeat at the normal keyboard rate. gg/G and Home/End jump immediately. Turning animations off finishes the queued move and makes subsequent steps immediate. A Windows OpenGL run measured no displacement jump across 11 page gaps.
- [ ] **Add smooth scrolling and autoscroll to the sidebar.** Apply smooth wheel scrolling and the document's middle-button gestures to Thumbnails, Contents, and Bookmarks within each panel's scroll range. Route input to the hovered panel, keep scrollbar dragging immediate, and cancel motion on panel switch or focus loss. The **Animations** setting controls smooth wheel movement. Middle-button autoscroll remains available with animations off. Reuse `ScrollMotion` for the panels. (2026-11-23: `PanelScroll` does this for all three panels and is unit-tested with a headless ImGui; awaiting a hand check before the extraction.) Once both consumers work, move it, the autoscroll speed curve and the gesture state into a header-only Hd component. Give it input samples, elapsed time, and bounds. Keep ImGui input ownership, marker rendering, page turns, and panel policy in the callers. The embedded pdfview target must still build with host-provided ImGui and Glad without linking the full Hd library.
- [ ] **Make session saves safe across two instances.** Keep each window's live tabs independent. A launch without file arguments restores the most recently saved window's tabs and active tab. Implement the desktop OS-backed file lock and atomic file replacement as separate primitives in Hd. Keep the lock held in pdfview across reading the latest settings, merging local changes since the last successful save, and replacing the settings file. Preserve independent preferences, per-file view changes, recent files, and bookmark edits. The later successful save wins conflicts on the same field or bookmark. Give bookmarks stable identities for merging and keep reading existing settings files. Explicit file arguments open the supplied files. Hd's theme editor can adopt atomic replacement in a separate change.
- [ ] **Move shortcut editing to Hd.** Extract the editor, action registry, and key-sequence parser together into a standalone ImGui-only Hd component that can build against host-provided ImGui. Keep action definitions, default bindings, and settings storage in their current app or viewer. Preserve existing pdfview source callers through forwarding headers and aliases, and keep action IDs and the binding file format compatible with saved settings. Run the existing shortcut tests and verify keyboard capture, conflict resolution, resets, and binding persistence in the app.

### Verification and review

- Use the main `build/` folder with `C:\msys64\ucrt64\bin` first on PATH. Run `cmake --build build` and `ctest --test-dir build --output-on-failure` after each step. Report the results and the path to `build/bin/pdfview-app.exe`.
- Exercise the actual interactions where practical. Cover mixed-size pages, multi-column and filtered thumbnail grids, tab switches and document reloads, repeated inputs and reversals, fitted and zoomed rows, and sidebar input routing. Test session saves with two real processes sharing an isolated settings directory and closing in both orders.
- Verify that a host can still build the widget with `PDFVIEW_BUILD_APP=OFF` and link it using its ImGui and Glad targets. Test that a failed settings replacement leaves the prior file readable and that a waiting writer can acquire the lock after the first writer exits.
- Keep this checklist, `AGENT_NOTES.md`, and `DEVELOPMENT_LOG.md` current as work proceeds. Commit each fix on approval and push only when requested.

## Open questions

None.
