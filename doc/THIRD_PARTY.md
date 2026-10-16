# Third-party notices

ErYouScanner (二游扫码) retains the licenses of its existing Qt, OpenCV,
FFmpeg, CPR, OpenSSL, WebView2 and QR-Code-generator dependencies. See their
upstream distribution notices and the project LICENSE.

The Kuro SMS login and GeeTest v4 interaction flow are adapted from
[DSVVA/KuRo_Scanner](https://github.com/DSVVA/KuRo_Scanner/tree/1.0.0),
tag `1.0.0`, commit `4ccb99ce4d14160dc5872585ccaabd84ee9424ed` (GPL-3.0).
The reference was formerly hosted at Theresa-0328/KuRo_Scanner. Our GPL-3.0
implementation replaces its detached HTTP server with a cancellable WebView2
document, validates bridge input and adds bounded requests and SMS throttling.
See `licenses/KuRo_Scanner-NOTICE.txt` and the bundled `LICENSE`.

The Kuro/Wuthering Waves protocol was studied from
[Rocszzz/tide-scanner](https://github.com/Rocszzz/tide-scanner),
commit `d0e81c2d6fb36d939c0031f3d79cee9e20ff5839` (MIT).
The C++ implementation uses the existing Qt/WebView2 runtime and does not
bundle the upstream Python application. Its MIT notice is retained in
`licenses/tide-scanner-LICENSE.txt`.

Account endpoint evidence was checked against the official kurobbs.com asset
`assets/useConfig-DLqk4N_9.js` on 2026-10-16. Public profile retrieval is not
treated as authentication. Server compatibility can change; offline tests
do not establish successful login with a live account.
