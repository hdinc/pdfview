# Plan: pdfview from a page renderer to a full viewer

**Status:** in progress (2026-11-21). Everything here is written and tested by hand on Windows (see
"Steps"). Nothing is committed yet.

## Goal
Make pdfview a viewer comparable to Okular:
- contents with page numbers, view modes, a multi-column thumbnails panel, tabs;
- text selection, page links, find, back/forward history;
- an editable shortcut map, session memory.

pdfview stays an embeddable ImGui widget (used by pdforg). Everything that only makes sense in a
standalone program goes in pdfview-app. Done when each feature works in the app on a real document
(the RISC-V privileged spec) and the core logic has unit tests.

## Context
- poppler-cpp (26.10) has no TOC destinations, no link API and no text layout, so the backend had to
  change.
- `deps/Hd` (window, GL, ImGui wrapper) is shared with pdforg. DPI, fonts, themes and the OS helpers
  went there, on branch `ui-scale-fonts-themes` of the Hd submodule.

## Approach
- **Backend:** poppler-glib, rendering through cairo. MuPDF was rejected (AGPL, packaging); poppler's
  private headers too (unstable API). Public headers carry no poppler types.
- **Render thread:** `RenderWorker` is a job queue. Page info (links, text) comes first, then tiles; a
  search runs a page at a time between tiles.
- **View modes:** `Layout` places pages in rows (columns, a first page alone, continuous or not). `View`
  keeps to one row when not continuous.
- **Input:**
  - Keys go through an `Actions` registry: key sequences, two bindings each, saved as text.
  - The widget either owns a registry (`Options::shortcuts`) or the host registers the viewer's
    actions in its own.
- **Library vs app:**
  - Library: widget, outline and thumbnail panels, shortcut editor.
  - App: tabs, sidebar, menus, settings file, session, bookmarks, reload, dialogs.
- **Look:** Hd does scale (monitor DPI times a user factor) and the event wait.
  - Fonts: the system UI font, upstream's picker, the merged icon font.
  - Themes: upstream's Dark/Light/Black and custom JSON themes with an editor, plus "system".
  - The widget follows `ImGuiStyle::FontScaleDpi` and the theme's colours, so hosts without Hd still
    work.
- File dialogs are native (Win32, zenity or kdialog), not nativefiledialog-extended, to avoid a GTK
  build dependency.

## Steps
- [x] Phase 0: AGENT_NOTES, Windows font (now Hd's system font)
- [x] Phase 1: poppler-glib `Document` (outline, links, labels, text, find, password); RenderWorker job
      queue; fixtures and tests
- [x] Phase 2: view modes in `Layout`/`View`, with tests
- [x] Phase 3: `Actions` (key sequences, matcher, save/load), with tests; shortcut editor
- [x] Phase 4: Viewer: tools, selection and copy, links, history, find, zoom modes, restore
- [x] Phase 5: outline and thumbnail panels
- [x] Phase 6: app: tabs, sidebar, toolbar, menus, settings, session, bookmarks, reload, password,
      link confirmation
- [x] Hd: content scale, fonts, themes, Platform helpers, event wait, window geometry, dark title bar
- [x] Merge upstream Hd (fonts, icons, themes, file dialog). Its Theme and Fonts replace this branch's.
      Scale, geometry, Platform, `waitEvents` and the "system" theme were re-applied on top. The toolbar
      now uses icons. Hd's tests pass.
- [x] GUI test on Windows at 4K, and fixes:
  - UI scale; splitter size; shortcut window size; new-file zoom;
  - contents collapsed like Okular; thumbnails re-centre after a reflow; look menu
- [ ] Test on Linux: zenity/kdialog dialog, xdg-open, fc-match fonts, GNOME/KDE dark mode
- [ ] Try by hand (the test tool can't do these): Ctrl+wheel zoom and thumbnail size; auto-reload while
      LaTeX rewrites a PDF (Windows file locking); a password-protected PDF; an external link
- [ ] Build pdforg against the new pdfview and Hd (Hd's `Gui` constructor now loads the system font and
      a theme)
- [ ] Unit tests for the app's `Settings` load/save

## Open questions
- Should Hd's default theme be "System" instead of "Dark"? pdforg would then change look on light
  systems.
- Bring back a Breeze (Okular) look as a custom theme JSON shipped with the app?
