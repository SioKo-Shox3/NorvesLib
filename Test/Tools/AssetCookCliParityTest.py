"""バイト比較器の反証。AssetCook本体やWindows smokeの代替試験ではない。"""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import json

MODULE = Path(__file__).resolve().parents[2] / "Scripts/AssetCookCliParity.py"
# 単体実行時はrepositoryのScriptsを参照する。
spec = importlib.util.spec_from_file_location("parity", MODULE)
parity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parity)


class ParityTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.input = self.root / "input"
        self.input.mkdir()
        (self.input / "one.nvpkg").write_bytes(b"\x00\xffPACKAGE")
        (self.input / "manifest.json").write_bytes(b'{"version":1,"assets":[]}\n')
        (self.input / "log.txt").write_text("not an artifact")
        self.meta = {"recipe": {"script": "fixed"}, "cases": ["test"]}
        self.a = self.root / "a"
        self.b = self.root / "b"

    def tearDown(self):
        self.temp.cleanup()

    def snapshots(self):
        parity.write_snapshot(self.input, self.a, self.meta)
        parity.write_snapshot(self.input, self.b, self.meta)

    def test_windows_child_module_path(self):
        parent = {"PSModulePath": "ps7", "PSMODULEPATH": "other", "psmodulepath": "", "PATH": "keep",
                  "PSModulePathExtra": "keep-too", "NORVES_TEST_BUNDLE_MEMBER": "original"}
        held = dict(parent)
        child = parity.smoke_environment(parent, "Glb", True)
        self.assertEqual(parent, held)
        self.assertEqual(child, {"PATH": "keep", "PSModulePathExtra": "keep-too",
                                 "NORVES_TEST_BUNDLE_MEMBER": "original"})
        child["PATH"] = "changed"
        self.assertEqual(parent["PATH"], "keep")

    def test_posix_and_skeletal_environment(self):
        parent = {"PSMODULEPATH": "keep", "NORVES_TEST_BUNDLE_MEMBER": "old"}
        self.assertEqual(parity.smoke_environment(parent, "Raw", False), parent)
        self.assertEqual(parity.smoke_environment(parent, "Skeletal", True),
                         {"NORVES_TEST_BUNDLE_MEMBER": "CookedSkeletalAssetTest"})
        self.assertEqual(parent["NORVES_TEST_BUNDLE_MEMBER"], "old")

    def test_identical(self):
        self.snapshots()
        self.assertEqual(parity.compare(self.a, self.b), [])
        self.assertEqual(len(parity.load_snapshot(self.a)[1]), 2)

    def test_byte_and_json_whitespace_change(self):
        parity.write_snapshot(self.input, self.a, self.meta)
        (self.input / "one.nvpkg").write_bytes(b"\x00\xfePACKAGE")
        (self.input / "manifest.json").write_bytes(b'{ "version":1,"assets":[]}\n')
        parity.write_snapshot(self.input, self.b, self.meta)
        errors = parity.compare(self.a, self.b)
        self.assertEqual(len(errors), 2)
        self.assertTrue(all("先頭差byte=" in e for e in errors))

    def test_added_and_missing(self):
        parity.write_snapshot(self.input, self.a, self.meta)
        (self.input / "one.nvpkg").rename(self.input / "two.nvpkg")
        parity.write_snapshot(self.input, self.b, self.meta)
        self.assertEqual(len(parity.compare(self.a, self.b)), 2)

    def test_recipe_change(self):
        parity.write_snapshot(self.input, self.a, self.meta)
        parity.write_snapshot(self.input, self.b, {"recipe": {}, "cases": ["test"]})
        self.assertEqual(len(parity.compare(self.a, self.b)), 1)

    def test_tampered_snapshot(self):
        self.snapshots()
        (self.b / "files/one.nvpkg").write_bytes(b"modified")
        with self.assertRaises(ValueError):
            parity.compare(self.a, self.b)

    def test_empty_and_no_overwrite(self):
        empty = self.root / "empty"
        empty.mkdir()
        with self.assertRaises(ValueError):
            parity.write_snapshot(empty, self.a, self.meta)
        self.snapshots()
        with self.assertRaises(FileExistsError):
            parity.write_snapshot(self.input, self.a, self.meta)

    def test_bad_path_and_duplicate(self):
        self.snapshots()
        file = self.b / "snapshot.json"
        doc = json.loads(file.read_text())
        original = json.loads(file.read_text())
        doc["files"][0]["path"] = "../../outside.nvpkg"
        file.write_text(json.dumps(doc))
        with self.assertRaises(ValueError):
            parity.load_snapshot(self.b)
        original["files"].append(original["files"][0])
        file.write_text(json.dumps(original))
        with self.assertRaises(ValueError):
            parity.load_snapshot(self.b)

    def test_manifest_without_named_manifest(self):
        (self.input / "result.json").write_bytes(b'{"version":1,"assets":[]}')
        self.assertIn("result.json", parity.artifacts(self.input))

    def test_build_sources_can_change_but_smoke_cannot(self):
        source = self.root / "source"
        source.mkdir()
        script = source / "smoke.cmake"
        script.write_text("fixed smoke")
        cmake = source / "Tools/AssetCook/CMakeLists.txt"
        cmake.parent.mkdir(parents=True)
        cmake.write_text("add_executable(AssetCook Main.cpp)")
        before = parity.build_recipe(source, [script])
        cmake.write_text("add_executable(AssetCook Main.cpp Commands.cpp)")
        self.assertEqual(before, parity.build_recipe(source, [script]))
        script.write_text("changed smoke")
        self.assertNotEqual(before, parity.build_recipe(source, [script]))

    def test_symlink(self):
        link = self.input / "linked.nvpkg"
        try:
            link.symlink_to(self.input / "one.nvpkg")
        except OSError:
            self.skipTest("この環境ではsymlink作成が許可されていない")
        with self.assertRaises(ValueError):
            parity.artifacts(self.input)


if __name__ == "__main__":
    unittest.main()
