# Main Window Layout Update

The user requested aligned delay controls and a cleaner main window. This is a
local UI update, not a port of the PR's UI.

- Replace fixed child coordinates with a vertical layout and a shared settings
  grid. Account, live room and confirmation delay inputs share a left edge.
- Use 32-pixel input heights, equal-width 40-pixel scan buttons, consistent
  margins and spacing, a compact project footer and a neutral table palette.
- Default size: 660x620 logical pixels; minimum: 620x560. Growing the window
  grows the account table, not the input heights. Table columns can be resized.
- Preserve widget names, account actions, room history, delay range/persistence,
  cancellation, scan state wiring and all existing dialogs.
- Keep native combo/spin arrows, focus states and keyboard tab order.

## Verification

The layout regression initially failed on the old absolute-positioned UI.
It now passes with the real generated Qt UI on the Windows platform plugin.
Synthetic accounts, long usernames and room links are used; no account data is
read for screenshots. Text fields scroll internally for long input.

Checked at 620x560, 660x620 and 900x740, each with idle, cancel-confirmation and
confirmation-in-progress button states, at 125%, 150% and 200% DPI. Tests check
shared alignment, input heights, equal button widths, parent bounds, pairwise
overlap, label/button text fit and absence of a default table horizontal scroll.
Screenshots were visually inspected for native arrows and Chinese text.

Preview: build/confirmation-delay/confirmation-ui-preview-1.25.png.
The fixture uses the same UI stylesheet and initial column widths as the app,
but is not a live-account capture. Full application build and PE dependency
checks pass. A new startup smoke check is deferred while the user's previous
EXE holds the single-instance mutex. No user process is stopped.

Current outputs: ../MHY_Scanner_v1.16.2.exe,
../MHY_Scanner_v1.16.2.zip and ../MHY_Scanner_v1.16.2.patch.
The displayed and Windows resource versions are both 1.16.2.
All previous local artifacts are retained; the faulty GitHub test release
was removed at the user's request.
