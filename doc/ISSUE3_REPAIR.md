# Issue #3: live-stream crash repair

Base commit: d07fc8018414e2b54962264517d8344601d825f2.

This document records the earlier standalone repair. The subsequent PR #212
core port changes probing and adds regression tests; see PR212_PORT.md for
the current cumulative source and verification status.

## Changes

- Initialize the scaler from the first decoded AVFrame, not incomplete codec metadata.
- Reject invalid dimensions, pixel formats, source planes and strides before entering swscale.
- Refresh the cached scaler when frame dimensions or format change.
- Supply four destination plane pointers and strides to sws_scale.
- Check allocation, decoder and conversion return values; record non-sensitive stream diagnostics.
- Catch QR worker and decoder exceptions and report STREAMERROR instead of terminating the process.
- Release skipped audio packets and wait for QR workers before cleanup or another scan session.
- Increase probing to 1 MiB with a 1 second analyze duration; retain the 5 second IO timeout.

## Regression Tests

The conversion test is independent of Qt, OpenCV and account credentials:

```powershell
cmake -S tests/stream_frame -B build/stream-frame -G "Visual Studio 17 2022" -A x64 -DFFMPEG_ROOT="<shared FFmpeg SDK>"
cmake --build build/stream-frame --config Release
ctest --test-dir build/stream-frame -C Release --output-on-failure
python tests/test_stream_safety.py
```

The five source guards failed on the original code and pass on the repaired code.
The MSVC Release conversion test passes with the downloaded shared FFmpeg SDK and
with the swscale-10.dll / avutil-61.dll bundled with the existing application.
It covers invalid format/size/planes/strides, correct RGB-to-BGR output, output
buffer bounds, resolution and format changes, and recovery after invalid input.

## Remaining Verification

The initial full-application configure attempt failed on missing development
dependencies. They have subsequently been installed and a new application EXE
has been built; see BUILD_LOCAL.md and PR212_PORT.md for the cumulative result.
Do not mix unrelated SDK headers, import libraries and runtime DLLs when
packaging the result.

The exact failing room, original stream data and crash dump from Issue #3 are
not available, so an end-to-end reproduction of that user's crash remains open.
The original EXE, account configuration and user logs are unchanged.
