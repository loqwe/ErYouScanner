"""Exercise the packaged application with isolated state and a failing local proxy."""
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import time


class FailingProxy(http.server.BaseHTTPRequestHandler):
    requests = 0

    def do_CONNECT(self):
        type(self).requests += 1
        self.send_error(502, "Injected startup network failure")

    do_GET = do_CONNECT

    def log_message(self, *_args):
        pass


def run(package, root, executable):
    if root.exists():
        raise RuntimeError("Use a fresh smoke directory; existing state is preserved")
    root.mkdir(parents=True)
    caller = root / "different-working-directory"
    caller.mkdir()
    proxy = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FailingProxy)
    thread = threading.Thread(target=proxy.serve_forever, daemon=True)
    thread.start()
    try:
        for name, accounts, expected_requests in (
            ("empty", [], 0),
            ("synthetic-account", [{"uid": "12345", "name": "Synthetic smoke account",
                "type": "\u5b98\u670d", "note": "", "mid": "1", "access_key": "fake-smoke-token"}], 1),
            ("legacy-bili-account", [{"uid": "12345", "name": "Legacy synthetic account",
                "type": "\u5d29\u574f3B\u670d", "note": "", "mid": "", "access_key": "fake-smoke-token"}], 0),
        ):
            request_count = FailingProxy.requests
            runtime = root / name
            shutil.copytree(package, runtime)
            config = runtime / "Config"
            config.mkdir()
            state = {"auto_exit": False, "auto_login": False, "auto_start": False,
                "confirm_delay_seconds": 0, "account": accounts, "last_account": 0,
                "live_room_id": "", "live_room_ids": [], "num": len(accounts)}
            if name == "legacy-bili-account":
                state.update(auto_start=True, last_account=1)
            (config / "userinfo.json").write_text(json.dumps(state), encoding="utf-8")
            env = os.environ.copy()
            address = f"http://127.0.0.1:{proxy.server_port}"
            for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
                env[key] = address
            env["NO_PROXY"] = env["no_proxy"] = ""
            with (runtime / "stdout.txt").open("wb") as stdout, (runtime / "stderr.txt").open("wb") as stderr:
                process = subprocess.Popen([str(runtime / executable), "-platform", "offscreen"],
                    cwd=caller, env=env, stdout=stdout, stderr=stderr,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                try:
                    time.sleep(12)
                    if process.poll() is not None:
                        raise RuntimeError(f"{name}: application exited with {process.returncode}")
                    log = (config / "scanner.log").read_text(encoding="utf-8")
                    required = ["startup: initializing application version=1.16.4"]
                    if accounts:
                        required += ["account-check: row=1 begin", "account-check: row=1 complete"]
                        required += ["check stoken: http="] if expected_requests else ["unsupported account type; skipped"]
                    if name == "legacy-bili-account":
                        required += ["screen: unsupported account type; startup rejected"]
                    if any(item not in log for item in required):
                        raise RuntimeError(f"{name}: expected failure evidence missing in scanner.log")
                    if "fake-smoke-token" in log or "fatal:" in log:
                        raise RuntimeError(f"{name}: credentials disclosed or fatal event recorded")
                    if "bili OA" in log or FailingProxy.requests - request_count != expected_requests:
                        raise RuntimeError(f"{name}: unexpected startup requests; B-server prefetch may still be active")
                    if (caller / "Config").exists() or list(config.glob("*.dmp")):
                        raise RuntimeError(f"{name}: wrong log directory or unexpected crash dump")
                    saved = json.loads((config / "userinfo.json").read_text(encoding="utf-8"))
                    if saved["account"] != accounts:
                        raise RuntimeError(f"{name}: synthetic account data changed")
                    print(f"{name}: alive for 12 seconds; {expected_requests} startup requests; logs and account preservation verified")
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
        if FailingProxy.requests != 1:
            raise RuntimeError("Expected requests did not reach the isolated local proxy")
        print(f"Local proxy handled {FailingProxy.requests} injected failures; no real accounts used")
    finally:
        proxy.shutdown()
        proxy.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    run(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve(), sys.argv[3])
