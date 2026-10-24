"""工具 ZIP 边界回归，不接触真实设备及用户工具目录。"""
import importlib.util
import pathlib
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("safe_archive", pathlib.Path(__file__).resolve().parents[1] / "tools/safe_archive.py")
archive_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive_tool)

class ArchiveTests(unittest.TestCase):
    def test_extract_and_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            with zipfile.ZipFile(root / "good.zip", "w") as z:
                z.writestr("bin/tool.exe", b"fixture")
            archive_tool.extract(root / "good.zip", root / "output", {"entrypoints":["bin/tool.exe"]})
            self.assertEqual((root / "output/bin/tool.exe").read_bytes(), b"fixture")
            self.assertTrue((root / "output/.serialstudio-files.json").is_file())

    def test_reject_escape_and_windows_ambiguity(self):
        for name in ("../escape", "/absolute", "C:/drive", "a\\..\\escape", "CON", "x:stream", "dir/file. ", "bin/tool.exe/../../escape", ".SERIALSTUDIO-FILES.JSON"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                with zipfile.ZipFile(root / "bad.zip", "w") as z:
                    z.writestr(name, b"x")
                with self.assertRaises(ValueError):
                    archive_tool.extract(root / "bad.zip", root / "out", {})
                self.assertFalse((root / "escape").exists())

    def test_missing_entrypoint_and_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            with zipfile.ZipFile(root / "bad.zip", "w") as z:
                info = zipfile.ZipInfo("link"); info.create_system = 3; info.external_attr = 0o120777 << 16
                z.writestr(info, "../elsewhere")
            with self.assertRaises(ValueError):
                archive_tool.extract(root / "bad.zip", root / "out", {})

if __name__ == "__main__":
    unittest.main()
