# TODO

Known issues and ideas that are not planned yet. When one is picked up, give it a plan in
[plans/](plans/) if it is large, and remove it from here when it is done.

## Toolbar overflow menu

Below about 900 px wide (medium buttons), the toolbar's right group (Browse, Text Selection,
Find, Settings) runs past the window's edge and is cut off. Buttons that don't fit should move
into an overflow menu (a "»" or "⋯" button at the right end), keeping their order and
shortcuts. `App::drawToolbar` already measures the groups' widths each frame
(`mToolbarCenterW`, `mToolbarRightW`), which tells it what fits. Added 2026-11-24.
