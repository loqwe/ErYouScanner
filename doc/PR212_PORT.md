# PR #212 Core Port

Reference: https://github.com/DSVVA/MHY_Scanner/pull/212

Pinned upstream head: 8dc8260a75553b3831fe5ebdbe3310917a48fd81.
Local base: loqwe/MHY_Scanner2 d07fc8018414e2b54962264517d8344601d825f2.

## Scope

This is an adapted core port, not a replacement with the upstream branch.
The upstream PR's UI, branding and dialogs are not imported. Local dialogs are
preserved; the subsequent decode repair increments the version to 1.16.2.
The main window was subsequently redesigned at the
user's request with an aligned, resizable local layout; see UI_LAYOUT.md.
The source also retains the preceding Issue #3 frame-conversion repair.
The later user-requested confirmation delay adds a seconds input;
see CONFIRMATION_DELAY.md.

- DXGI: select hardware with an attached desktop output, check HRESULTs,
  release COM objects/held frames, use read-only staging and copy by RowPitch.
- Screen scanning: bounded single-worker scanning, safe frame ownership,
  reuse the last valid image on unchanged desktops, and drain workers on exit.
- QR models: locate ScanModel relative to the EXE, validate all four files,
  report initialization failures, clear stale decoded output, and use scale 1.0.
- Live scanning: preserve safe first-frame conversion and four-plane swscale
  arguments; retain only the latest pending image; submit at most once per
  200ms, using tryStart with at most three workers and no growing queue.
- Stream IO: 5 MB probe, 2 second analysis, 1 MB receive buffer and the existing
  5 second IO timeout. A real FFmpeg interrupt callback handles cancellation
  and a 10 second initialization/no-decoded-video deadline. Audio packets do
  not refresh that deadline. The decode loop also checks it for buffered input.
- Scan status: report streaming only after a successfully converted frame.
  Preserve stop requests arriving before the stream thread starts.

## Local Behavior Preserved

- Query-based QR parsing, Panda bridge and two-stage passport confirmation.
- Existing account validation, account notes, row copying, full live-room links
  and the five-entry room history.
- The existing Bilibili parser already traverses stream/format/codec entries
  and prefers HTTP-FLV AVC. It is retained instead of the older upstream parser.
- No forced administrator manifest, raw QR/stream-address logging, desktop PNG
  dumps, upstream UI stylesheet/branding, protocol downgrade or version bump.
- Original application EXE, account files and user logs remain unchanged.

## Verification

```powershell
python -m unittest discover -s tests -p "test_*safety.py"
python tests/test_scan_ui.py
cmake -S tests/stream_frame -B build/stream-frame -G "Visual Studio 17 2022" -A x64 -DFFMPEG_ROOT="<shared FFmpeg SDK>"
cmake --build build/stream-frame --config Release
ctest --test-dir build/stream-frame -C Release --output-on-failure
cmake -S tests/screen_capture -B build/screen-capture -G "Visual Studio 17 2022" -A x64
cmake --build build/screen-capture --config Release
ctest --test-dir build/screen-capture -C Release --output-on-failure
git diff --check
```

Verified with MSVC 19.44 x64 Release:
- 9 stream source guards and 3 existing-UI integration guards.
- Frame conversion/bounds/format changes, timing boundaries and reset.
- Actual FFmpeg blocked HTTP reads interrupted by timeout and cancellation,
  tested against a localhost server that sends headers but stalls the body.
  The stream suite also passes with the FFmpeg DLLs bundled with the user's EXE.
- RowPitch padding, buffer limits, invalid DXGI state, EXE-relative model paths
  and absent models; the existing four model files pass file validation.

The full Qt/OpenCV application has subsequently been compiled as an MSVC x64
Release EXE after installing the dependencies from the existing vcpkg baseline.
See BUILD_LOCAL.md and CONFIRMATION_DELAY.md for the current build and feature.
The original and portable runtime directories pass x64 PE import checks and
isolated 3-second startup checks. Real-model synthetic QR decoding also passes.
Actual GPU capture, real live-room decoding and account login confirmation
remain unverified; no user credentials are used by these tests.

The root pr212-core-port.patch is cumulative from the local base, including
the earlier Issue #3 repair. Do not apply it on top of issue3-live-crash-fix.patch.
The earlier standalone issue patch is kept separately and is not overwritten.
The later pr212-delay-build.patch also includes the delay and build adjustments.
