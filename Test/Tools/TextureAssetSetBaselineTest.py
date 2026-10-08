"""legacy texture基準の改変・不足を成功にしない契約を検証する。"""
import json
from pathlib import Path
import runpy
import tempfile
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[2] / "Scripts/CaptureTextureAssetSetBaseline.py"))


class BaselineTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = {"package_root": "Cooked/Silver", "textures": [{"logical_path": "Assets/a.png", "package_name": "a.nvpkg"}]}
        self.package = self.root / "Cooked/Silver/a.nvpkg"
        self.package.parent.mkdir(parents=True)
        self.package.write_bytes(b"package\0\xff")
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(json.dumps({"version": 1, "assets": [{"logical_path": "a.png", "cooked_package": "Cooked/Silver/a.nvpkg"}]}), encoding="utf-8")

    def test_byte_inventory(self):
        values = API["inventory"](self.root, self.spec)
        self.assertEqual(values["manifest.json"], self.manifest.read_bytes())
        self.assertEqual(values["Cooked/Silver/a.nvpkg"], b"package\0\xff")

    def test_missing_package(self):
        self.package.unlink()
        with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_extra_output(self):
        (self.root / "unexpected.txt").write_text("extra")
        with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_bom_rejected(self):
        self.manifest.write_bytes(b"\xef\xbb\xbf" + self.manifest.read_bytes())
        with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_wrong_reference(self):
        self.manifest.write_text('{"version":1,"assets":[{"logical_path":"wrong","cooked_package":"Cooked/Silver/a.nvpkg"}]}')
        with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_empty_assets(self):
        self.manifest.write_text('{"version":1,"assets":[]}')
        with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_exact_repeat(self):
        API["require_equal"]({"a": b"x\r\n"}, {"a": b"x\r\n"})
        for other in ({"a": b"x\n"}, {"b": b"x\r\n"}, {"a": b"x\r\n", "b": b""}):
            with self.assertRaises(ValueError): API["require_equal"]({"a": b"x\r\n"}, other)

    def test_non_integer_version(self):
        for value in (True, 1.0, "1"):
            doc = {"version": value, "assets": [{"logical_path": "a.png", "cooked_package": "Cooked/Silver/a.nvpkg"}]}
            self.manifest.write_text(json.dumps(doc))
            with self.assertRaises(ValueError): API["inventory"](self.root, self.spec)

    def test_complete_recipe(self):
        recipe = {"upstream_sha": "a" * 40, "asset_cook_sha256": "b" * 64, "inputs": {name: "c" * 40 for name in API["REQUIRED_INPUTS"]}}
        API["validate_recipe"](recipe)
        self.assertEqual(len(recipe["inputs"]), 11)
        for invalid in ({}, {k: v for k, v in recipe["inputs"].items() if k != "Scripts/CookTextureAssetSet.ps1"}, dict(recipe["inputs"], **{"../escape": "c" * 40})):
            with self.assertRaises(ValueError): API["validate_recipe"](dict(recipe, inputs=invalid))

    def test_invalid_recipe_hash(self):
        recipe = {"upstream_sha": "a" * 40, "asset_cook_sha256": "b" * 64, "inputs": {name: "c" * 40 for name in API["REQUIRED_INPUTS"]}}
        for key, value in (("upstream_sha", "main"), ("asset_cook_sha256", 1)):
            with self.assertRaises(ValueError): API["validate_recipe"](dict(recipe, **{key: value}))
        recipe["inputs"]["Scripts/CookTextureAssetSet.ps1"] = "not-a-hash"
        with self.assertRaises(ValueError): API["validate_recipe"](recipe)

    def test_child_environment(self):
        parent = {"PSModulePath": "bad", "psmodulepath": "bad2", "PATH": "keep", "VALUE": "ok"}
        self.assertEqual(API["child_environment"](parent), {"PATH": "keep", "VALUE": "ok"})
        self.assertEqual(len(parent), 4)


if __name__ == "__main__":
    unittest.main()
