# Local Windows Build

Current local build: **v1.16.4** (official accounts only, by owner request).
See FIX_v1.16.4.md for B-server removal, worker exception protection,
fatal logging, verification and checksums. Distribute only
../MHY_Scanner_v1.16.4.zip; its entry point is MHY_Scanner_v1.16.4.exe.
The matching installed runtime/PDB is in install-ui/MHY_Scanner_1.16.4.
No account configuration, logs or crash dumps are packaged. GitHub releases
should carry the ZIP only, as requested by the repository owner.
The earlier v1.16.2/v1.16.3 artifacts and verification records are preserved.
The earlier local v1.16.4 test package/runtime are backed up separately with
the suffix before_bili_removal. Build the final ZIP from
build/package-v1.16.4-official-only; see CHANGELOG.md for user-facing changes.

Toolchain: MSVC 19.44.35227, Windows SDK 10.0.26100.0, x64 Release, C++23, /MD.
Qt: 6.8.0 win64_msvc2022_64 (the existing CI version).
vcpkg baseline: 57987637aac17d62bac535bc839baca3754490e6.
Dependencies: x64-windows-static-md, as in the existing Windows build workflow.

Local dependencies were installed in D:/mhy-build-deps to avoid non-ASCII SDK
relocation issues and the limited free space on C:. These are development files,
not account data. The application source remains in repair-source.

The Qt installer initially failed to relocate qmake under the Chinese workspace
path. Installation to D:/mhy-build-deps/qt completed successfully; no application
source or UI theme was changed to work around this installer issue.

After dependency installation, use a clean build directory when supplying the
vcpkg toolchain. Do not reuse the earlier build/app cache without a toolchain.

```powershell
$cmake = "D:/mhy-build-deps/vcpkg/downloads/tools/cmake-3.31.10-windows/cmake-3.31.10-windows-x86_64/bin/cmake.exe"
& $cmake -S . -B build/app-release -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=D:/mhy-build-deps/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DVCPKG_INSTALLED_DIR=D:/mhy-build-deps/installed `
  -DVCPKG_MANIFEST_INSTALL=OFF `
  -DCMAKE_PREFIX_PATH=D:/mhy-build-deps/qt/6.8.0/msvc2022_64 `
  -DQT6_INSTALL_PREFIX=D:/mhy-build-deps/qt/6.8.0/msvc2022_64 `
  -DCMAKE_INSTALL_PREFIX=./install-ui -DUNIT_TESTS=OFF -DDEV=OFF
& $cmake --build build/app-release --config Release --parallel 4
& $cmake --install build/app-release --config Release
```

FFmpeg's headers, import libraries and DLLs must come from the same SDK directory
selected by cmake/install_ffmpeg.cmake. Do not mix the old FFmpeg 5 development
files with the new application DLLs. QtConcurrent is now an explicit linked and
packaged Qt dependency for the tracked login-confirmation task. Its current
template-only usage does not add a Qt6Concurrent.dll import to the EXE.

## Generated Artifacts

The following entries describe the preserved v1.16.2 build. Current v1.16.4
artifacts and startup verification are recorded in FIX_v1.16.4.md.

- ../MHY_Scanner_v1.16.2.exe: versioned EXE with malformed live-packet recovery.
  It can use the original compatible Qt 6.8.0 and FFmpeg 63/61/10 DLLs and ScanModel.
- install-ui/MHY_Scanner_1.16.2: independent directory with SDK-matched runtime
  DLLs, Qt plugins, models and the matching PDB. No account configuration is copied.
- ../MHY_Scanner_v1.16.2.zip: portable package without the large
  PDB. The matching PDB remains in the install directory.
- ../MHY_Scanner_v1.16.2.patch: incremental source diff from commit 96ef29d,
  fixing decode recovery and updating versions, tests and build documentation.

EXE SHA-256: 69DBB11CCC033D9386962FACFAECE9FB1E030E866C10120FC34DBCBCF6AFBEB8.
The original EXE SHA-256 is unchanged:
85894EF7545F632AD5EF6FF4BB112C9A11B88B9D4A4CF8E6C866A577EBCBAEF4.

Verified for v1.16.2: full application configure/build/install; 19 source
integration guards; ten C++/Qt regression tests; real models decoding synthetic
QR codes; PE imports resolving for 14 local modules in each runtime directory.
UI checks and screenshots cover three window sizes and three confirmation states
at 125%, 150% and 200% DPI. See UI_LAYOUT.md.

Actual H.264 encode/decode reproduces error -1094995529 and resumes decoding
valid keyframes with the same decoder. This also passes with the original
application's DLLs, which differ in hash from the packaged SDK DLLs. The bounded
no-valid-frame timeout is retained. See FIX_v1.16.2.md.

Every subsequent redistributed build must increment the version and keep CMake,
Windows resources, displayed version, artifact names and release tag aligned.
tests/test_release_version.py guards the source metadata and UI test fixture.
For this manual release, the tag-triggered build is cancelled so it cannot
replace the verified package. The publishing credential cannot edit workflows.

The earlier delay build passed isolated 3-second startup checks. This build's
startup smoke check is deferred because the user's earlier instance is running;
it is not stopped or replaced. Real GPU capture, live-room scanning and account
confirmation are not tested against user accounts. Old EXEs, packages, patches
and the previous install directory remain unchanged.
