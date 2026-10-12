# v1.16.3 floating-point desktop capture fix

Base: 14531cf348acf8a90b28e09e070a2b7134880cfc (v1.16.2).

The affected RTX 5090 returned a 2560x1440 desktop texture with
DXGI_FORMAT_R16G16B16A16_FLOAT (10), one sample, one mip and one array element.
Device creation, DuplicateOutput and AcquireNextFrame all succeeded. v1.16.2
rejected the texture before creating or mapping its staging texture because it
accepted only DXGI_FORMAT_B8G8R8A8_UNORM (87). This produced E_INVALIDARG
(-2147024809). The report does not establish that Windows HDR was enabled.

## Change

- Read RGBA16F staging textures using their real RowPitch and eight bytes per
  source pixel; convert linear half-float RGB to sRGB BGRA8 for the existing QR
  scanner. Clamp negative/over-range values and handle infinity/NaN; use opaque
  output alpha. A 64 KiB lookup table avoids per-frame gamma calculations.
- Retain the original BGRA copy path and bounds/overflow checks. Recreate staging
  textures on format changes as well as size changes.
- Size the four-byte output buffer and OpenCV image from the acquired texture,
  rather than assuming initial duplication dimensions always remain current.
- Log texture format/dimensions when they change and include the failed step,
  RowPitch and buffer capacity in copy errors.
- Set CMake, displayed version, UI regression fixture and PE version metadata to
  1.16.3. No UI layout, account configuration or live decoding behavior changes.

## Verification

Before the fix, the regression compiled and failed with
`RGBA16F texture rejected by screen readback`. After the fix, the same WARP
D3D11 texture readback passed. Tests check channel order, 0.5 linear -> 188 sRGB,
padding, alpha, negative values, over-range values, infinity/NaN, undersized
buffers and float -> BGRA -> float staging changes at identical dimensions.

The real QR models decode a synthetic QR after RGBA16F conversion, including
unaligned input, padded rows and a white level of approximately 0.2 linear.
No real account login or screen image persistence is used by these tests.

Reproduction commands, from repair-source:

```powershell
python -m unittest discover -s tests -p 'test_*.py'
& $cmake --build build/screen-capture --config Release --parallel 2
& $ctest --test-dir build/screen-capture -C Release --output-on-failure
& $cmake --build build/qr-scanner --config Release --parallel 2
& $ctest --test-dir build/qr-scanner -C Release --output-on-failure
& $cmake --build build/confirmation-delay --config Release --parallel 2
& $ctest --test-dir build/confirmation-delay -C Release --output-on-failure
& $ctest --test-dir build/stream-frame -C Release --output-on-failure
& $cmake --build build/app-release --config Release --parallel 4
& $cmake --install build/app-release --config Release
python tests/verify_windows_package.py build/package-v1.16.3 MHY_Scanner_v1.16.3.exe
python tests/verify_windows_package.py .. MHY_Scanner_v1.16.3.exe
```

Use the cmake/ctest binaries in the CMake 3.31.10 installation documented in
BUILD_LOCAL.md. Toolchain: MSVC 19.44.35227, Windows SDK 10.0.26100.0, x64 Release,
C++23, /MD; Qt 6.8.0; unchanged vcpkg baseline and FFmpeg SDK.

All 19 Python checks and ten C++/Qt tests passed. The application built and
installed successfully. PE imports resolved for 14 local modules in both the
new portable directory and the existing root runtime directory. EXE FileVersion
and ProductVersion both report 1.16.3.

The portable EXE survived an isolated 3.5-second offscreen startup with an empty
account configuration. Only the smoke-test process was closed. The ZIP contains
20 entries with a versioned EXE and no Config, logs, PDB or crash dumps.

## Artifacts and limits

- ../MHY_Scanner_v1.16.3.exe
- ../MHY_Scanner_v1.16.3.zip
- install-ui/MHY_Scanner_1.16.3 (matching PDB retained locally)
- build/package-v1.16.3 (portable staging without personal data or PDB)

EXE SHA-256:
A8A05DBBAD3317243F67B20248B5065C6ABCA2DDC1667761CDB39B769D34E721

ZIP SHA-256 (77,749,870 bytes):
C00DF6696C1530B120F0BF4EED6B9345D9EA0ABBA3506B761081B968C39029E7

The affected computer's actual GPU/driver capture has not been rerun here.
The verified reproduction uses a real D3D11 WARP texture with the reported format
through the production readback method. Existing EXEs/packages and Config are
preserved. This fix addresses GitHub issue #1. Publication uses the already
verified local EXE/ZIP; the tag-triggered auto-release run is cancelled to avoid
replacing the tested ZIP with a separately built artifact.
