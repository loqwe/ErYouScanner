"""Keep the window version and Windows EXE metadata in sync."""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReleaseVersionTests(unittest.TestCase):
    def test_cmake_and_windows_resource_versions_match(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        version = re.search(r"project\(ErYouScanner\s+VERSION\s+(\d+\.\d+(?:\.\d+)?)", cmake).group(1)
        resource = (ROOT / "src/Resources/MHY_Scanner.rc").read_text(encoding="utf-8-sig")
        numeric = ",".join(str(int(part)) for part in version.split(".") + ["0"] * (4 - len(version.split("."))))
        for name in ("FILEVERSION", "PRODUCTVERSION"):
            self.assertRegex(resource, rf"{name}\s+{re.escape(numeric)}\b")
        for name in ("FileVersion", "ProductVersion"):
            self.assertIn(f'VALUE "{name}", "{version}"', resource)
        fixture = (ROOT / "tests/confirmation_delay/test_confirmation_ui.cpp").read_text(encoding="utf-8-sig")
        self.assertIn(f'ui.label_3->setText("{version}")', fixture)

    def test_smoke_startup_derives_the_version(self):
        """The startup smoke test must read the version, not carry a stale literal."""
        smoke = (ROOT / "tests/smoke_startup.py").read_text(encoding="utf-8-sig")
        self.assertIn("expected_version()", smoke)
        self.assertNotRegex(smoke, r"version=\d+\.\d+\.\d+")

    def test_screenshot_names_use_the_version_macro(self):
        """Artifact names must interpolate the version, not freeze one in a literal."""
        offenders = []
        for path in sorted((ROOT / "tests").rglob("*")):
            if not path.is_file() or path.suffix not in {".cpp", ".h", ".py"}:
                continue
            text = path.read_text(encoding="utf-8-sig", errors="ignore")
            for match in re.finditer(r"-[vV]\d+\.\d+\.\d+\.(?:png|jpg|jpeg)", text):
                offenders.append(f"{path.relative_to(ROOT)}: {match.group(0)}")
        self.assertEqual(offenders, [], "use MHY_Scanner_VERSION / expected_version() instead")


if __name__ == "__main__":
    unittest.main()
