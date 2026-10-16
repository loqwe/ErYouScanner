# v1.16.4 official-only login and logging update

Base: 3c25c3c28bbdad377761b1259169c116eaed26ba (v1.16.3).
The repository owner explicitly requested retaining v1.16.4 for this final build.

## Evidence and scope

Another user's report describes 0xc0000409, ucrtbase!abort and Qt6Core!QThreadPool
on a different computer. The original dump was not supplied here; the exact
old crashing call remains unconfirmed.

The user's v1.16.4 test screenshot shows successful stoken checks (retcode=0,
valid=1), followed by a B-server OA prefetch failure (http=0,
network_error=21 / CPR SSL_CONNECT_ERROR). The exception was caught and screen
capture subsequently continued. This confirms that screenshot's account check
succeeded and the separate prefetch failed; it does not prove the old crash.

The owner then requested removing B-server login. The final build removes that
entire request path, not merely its UI entry point. Bilibili live-room scanning
remains available for official accounts.

## Final changes

- Remove the B-server login tab, password/captcha callbacks, BSGameSDK.hpp,
  B-server account requests, external-login/signing/scan-confirm APIs and OA
  startup prefetch. Remove B-server enum values and scanner dispatch branches.
- Keep SMS, QR and Cookie login for official accounts, and official screen/live
  scanning. Legacy B-server accounts remain in configuration, display
  unsupported status, and cannot start scanning or export an official Cookie.
  Live startup rejects unsupported accounts before requesting a live stream.
- Guard account checks, screen/live startup, scanner workers and confirmation
  against all C++ exception types. Check accounts individually and drain owned
  startup/check tasks before destroying the main window.
- Snapshot selected account state on the UI thread and save new account state
  there, rather than accessing mutable UI-owned JSON from untracked workers.
- Validate stoken response shape and integer range; bound the request to 10
  seconds. Preserve existing official login, capture and stream recovery paths.
- Serialize log appends, include timestamp/thread/task/error category/code,
  close each write and prevent logging failures from throwing. Never persist
  exception.what() or full response bodies.
- Set the working directory to the executable directory before initialization.
  Log startup and config-load failures; warn when the log is not writable.
- Handle terminate, SIGABRT and Windows unhandled exceptions with a separate
  Win32 fatal append/FlushFileBuffers path and best-effort versioned minidumps.

Direct fail-fast, forced process termination, errors before main, disk failures
and process corruption can bypass logging. Dumps can contain private account
memory. Existing WindowLogin official tasks were not comprehensively reworked
by this patch.

## Verification

- 23 Python source/version checks and 12 C++/Qt regressions passed.
- Wrong-shape JSON originally reproduced json.type_error.306; the parser fix
  rejects wrong types and overflow without that conversion exception.
- Real QThreadPool tests cover runtime/JSON/unknown exceptions, failure callback
  exceptions and successful work after failures; synthetic secrets are not
  written to logs.
- Four child crash cases (terminate, main C++ exception, worker C++ exception,
  raised Windows exception) produced fatal logs and nonempty minidumps.
- Final packaged smoke tests use three independent runtime copies: empty state,
  a synthetic official account and a legacy B-server account with auto-start.
  Each stayed alive for 12 seconds from a different caller working directory.
  Empty and legacy cases sent zero startup requests; the official case sent
  exactly one stoken request through a failing local proxy and logged failure.
  The legacy auto-start was rejected; all synthetic account records were
  preserved and no token or fatal event appeared in logs.
- Full x64 Release build/install passed. PE versions are 1.16.4 and imports
  resolve for 14 local modules. The ZIP passed integrity checks and its EXE
  hash matches the installed/package EXE. No Config/log/dmp/PDB is packaged.
- README, CHANGELOG, Cookie instructions, build notes and package README describe
  the official-only behavior, recent functionality and diagnostic limits.

Toolchain: MSVC 19.44.35227, SDK 10.0.26100.0, C++23, /MD, Qt 6.8.0.
The vcpkg baseline and FFmpeg SDK are unchanged; see BUILD_LOCAL.md.

```powershell
python -m unittest discover -s tests -p 'test_*.py'
& $cmake -S tests/task_boundary -B build/task-boundary -A x64 `
  -DCMAKE_PREFIX_PATH=D:/mhy-build-deps/qt/6.8.0/msvc2022_64
& $cmake --build build/task-boundary --config Release --parallel 2
& $ctest --test-dir build/task-boundary -C Release --output-on-failure
& $ctest --test-dir build/screen-capture -C Release --output-on-failure
& $ctest --test-dir build/qr-scanner -C Release --output-on-failure
& $ctest --test-dir build/stream-frame -C Release --output-on-failure
& $ctest --test-dir build/confirmation-delay -C Release --output-on-failure
& $cmake --build build/app-release --config Release --parallel 4
& $cmake --install build/app-release --config Release
python tests/verify_windows_package.py build/package-v1.16.4-official-only MHY_Scanner_v1.16.4.exe
# Use a fresh smoke directory and ensure no other scanner instance is running.
python tests/smoke_startup.py build/package-v1.16.4-official-only build/smoke-v1.16.4-replay MHY_Scanner_v1.16.4.exe
```

## Final artifact

Distribute MHY_Scanner_v1.16.4.zip only: 20 entries, 77,738,438 bytes.
The final package directory is build/package-v1.16.4-official-only.
The matching EXE/PDB remain in install-ui/MHY_Scanner_1.16.4.

ZIP SHA-256: 35336AAF0634157206E97D7C396C5D3EEC70745314242FC004E24765FB062767.

EXE SHA-256: 1FF91220836868F7EE38E29E2CAEF1B78668275C2D08597493DA09FA63C37896.

Earlier local pre-removal v1.16.4 binaries are preserved separately as
../MHY_Scanner_v1.16.4_before_bili_removal.zip and
install-ui/MHY_Scanner_1.16.4_before_bili_removal. They are not the release asset.
The original v1.16.3 EXE and ZIP remain unchanged.
