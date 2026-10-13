"""Guards for the production paths that caused unhandled worker exceptions."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/UI/WindowMain.cpp").read_text(encoding="utf-8-sig")


class TaskBoundaryIntegrationTests(unittest.TestCase):
    def test_startup_and_account_workers_are_guarded(self):
        self.assertNotIn('bili OA prefetch', MAIN)
        self.assertIn('MakeScannerTask("account-check: worker"', MAIN)
        self.assertIn('RunScannerTask(phase.c_str()', MAIN)
        self.assertIn('MakeScannerTask("screen: startup"', MAIN)
        self.assertIn('MakeScannerTask("stream: startup"', MAIN)

    def test_window_drains_its_owned_tasks_before_destruction(self):
        destructor = MAIN.split("WindowMain::~WindowMain()", 1)[1].split("void WindowMain::", 1)[0]
        self.assertLess(destructor.index("closing.store(true)"), destructor.index("backgroundTasks.waitForDone()"))
        self.assertIn("backgroundTasks.clear()", destructor)
        for mode, end in (("pBtstartScreen", "pBtStream"), ("pBtStream", "closeEvent")):
            code = MAIN.split(f"void WindowMain::{mode}(", 1)[1].split(f"void WindowMain::{end}(", 1)[0]
            task = code.split("backgroundTasks.start(", 1)[1]
            self.assertNotIn("userinfo[", task)
            self.assertIn("account = std::move(account)", task)
            self.assertIn("if (closing.load()) return;", task)

    def test_scan_threads_handle_all_exception_types(self):
        stream = (ROOT / "src/UI/QRCodeForStream.cpp").read_text(encoding="utf-8-sig")
        self.assertIn('LogScannerException("stream: QR worker")', stream)
        self.assertIn('LogScannerException("stream: decoder")', stream)
        self.assertNotIn("catch (const std::exception&)", stream)
        screen = (ROOT / "src/UI/QRCodeForScreen.cpp").read_text(encoding="utf-8-sig")
        self.assertIn('LogScannerException("screen: QR worker")', screen)
        self.assertIn('LogScannerException("screen: model or capture initialization")', screen)

    def test_crash_handlers_and_exe_relative_directory_are_installed(self):
        main = (ROOT / "src/UI/Main.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("InstallScannerCrashHandlers()", main)
        self.assertIn("QDir::setCurrent(QCoreApplication::applicationDirPath())", main)
        self.assertIn("logCheck == INVALID_HANDLE_VALUE", main)
        self.assertIn("existing instance detected", main)


if __name__ == "__main__":
    unittest.main()
