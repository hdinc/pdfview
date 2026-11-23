# TODO

Known issues and ideas that are not planned yet. When one is picked up, give it a plan in
[plans/](plans/) if it is large, and remove it from here when it is done.

## Toolbar overflow menu

Below about 900 px wide (medium buttons), the toolbar's right group (Browse, Text Selection,
Find, Settings) runs past the window's edge and is cut off. Buttons that don't fit should move
into an overflow menu (a "»" or "⋯" button at the right end), keeping their order and
shortcuts. `App::drawToolbar` already measures the groups' widths each frame
(`mToolbarCenterW`, `mToolbarRightW`), which tells it what fits. Added 2026-11-24.

## MSYS-style paths open the wrong file on Windows

Opening `/c/Users/hd/Downloads/riscv-privileged.pdf` fails with "can't open
C:\c\Users\hd\Downloads\riscv-privileged.pdf". `Hd::Platform::normalPath` passes the path to
`std::filesystem::absolute`, which on Windows reads `/c/...` as a folder `c` on the current
drive. Git Bash usually converts such arguments before starting a Windows program, so the path
came another way (a launcher, a script with `MSYS_NO_PATHCONV`, or a file manager). A fix:
on Windows, turn `/x/...` (one letter, then a slash) into `X:\...` before normalising, in
`normalPath`. Seen 2026-11-24.
