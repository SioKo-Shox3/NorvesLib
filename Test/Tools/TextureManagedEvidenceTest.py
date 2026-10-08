"""管理metadata証拠の純値検査が差分・不正JSONを拒否することを確認する。"""
import json
from pathlib import Path
import runpy
import unittest
import tempfile
M = runpy.run_path(str(Path(__file__).resolve().parents[2] / "Scripts/TextureManagedEvidence.py"))


class ManagedEvidenceTest(unittest.TestCase):
    def test_strict_json(self):
        self.assertEqual(M["strict_json"](b'{"a":1}'), {"a": 1})
        for raw in (b'{"a":1,"a":2}', b'[]', b'\xff'):
            with self.assertRaises((ValueError, UnicodeError)):
                M["strict_json"](raw)

    def test_shape(self):
        M["shape"]({"a": 1}, ("a",))
        for value in ({}, {"a": 1, "b": 2}, []):
            with self.assertRaises(ValueError):
                M["shape"](value, ("a",))

    def test_hex(self):
        self.assertEqual(M["hex_value"]("000000000000000f", 16), 15)
        for value in ("F" * 16, "0" * 15, "g" * 16, 1):
            with self.assertRaises(ValueError):
                M["hex_value"](value, 16)

    def test_fnv(self):
        self.assertEqual(M["fnv"](b""), "cbf29ce484222325")
        self.assertEqual(M["fnv"](b"hello"), "a430d84680aabd0b")

    def test_volume_guid_identity(self):
        value = "ABCDEF12-1234-5678-9ABC-1234567890AB"
        self.assertEqual(M["volume_guid_identity"]("\\\\?\\Volume{" + value + "}\\Runtime"), value.lower())
        with self.assertRaises(ValueError):
            M["volume_guid_identity"]("C:/Runtime")

    def test_capture_store_schema(self):
        # native ABIの代用試験ではなく、取得済みidentityとwireの対応を検査する純値fixture。
        with tempfile.TemporaryDirectory() as folder:
            workspace = Path(folder) / "workspace"
            workspace.mkdir()
            store = workspace / ".norves-assetcook"
            store.mkdir()
            guid = "abcdef12-1234-5678-9abc-1234567890ab"
            prefix = "\\\\?\\Volume{" + guid + "}\\"
            def identity(path):
                return {"volume": "0000000000000001", "file_id": M["digest"](str(path).encode())[:32],
                        "canonical": prefix + str(path).replace("/", "\\"), "creation": 1, "write": 1,
                        "directory": path.is_dir()}
            header = {"schema": 1, "producer": "NorvesLib.AssetCook", "store_id": "1" * 32,
                      "volume_guid": guid, "volume_serial": identity(workspace)["volume"],
                      "workspace_id": identity(workspace)["file_id"], "store_directory_id": identity(store)["file_id"]}
            roots, specs = [], {}
            for i in (1, 2):
                runtime = workspace / ("Case" + str(i))
                runtime.mkdir()
                spec_path = workspace / ("case" + str(i) + ".json")
                spec_path.write_text("{}")
                specs[runtime.name] = spec_path
                owner = M["owner_id"](identity(spec_path)["canonical"], identity(runtime)["canonical"], "manifest.json")
                claim = str(i + 1) * 32
                roots.append({"claim_id": claim, "leaf": runtime.name, "directory_id": identity(runtime)["file_id"], "owner_id": owner})
                payload = b"package bytes"
                (runtime / "a.nvpkg").write_bytes(payload)
                key = {"logical": "a", "kind": "texture", "variant": "default"}
                output = {"key": key, "source_hash": "0" * 16, "format": "format", "package": "a.nvpkg", "entry": "a",
                          "entry_type": "0000000030786554", "cooked_hash": "1" * 16, "cooked_version": 0,
                          "package_size": f"{len(payload):016x}", "package_hash": M["fnv"](payload), "skeletal": None}
                manifest = {"version": 1, "assets": [{"logical_path": "a", "variant": "default", "format": "format",
                            "cooked_package": "a.nvpkg", "entry_name": "a", "source_hash": "0" * 16,
                            "cooked_hash": "1" * 16, "cooked_version": 0, "entry_type": "Tex0"}]}
                (runtime / "manifest.json").write_text(json.dumps(manifest))
                state = {"schema": 1, "producer": "NorvesLib.AssetCook", "owner": owner,
                         "root": runtime.as_posix()[0].upper() + runtime.as_posix()[1:], "manifest": "manifest.json",
                         "generation": "0000000000000001", "records": [{"primary": key, "schema": 1, "dependency_schema": 1,
                         "revision": "0000000000000001", "dependency_hash": "2" * 16, "outputs": [output]}]}
                (store / ("state-" + claim + ".json")).write_text(json.dumps(state))
            (store / "header.json").write_text(json.dumps(header))
            (store / "roots.json").write_text(json.dumps({"schema": 1, "store_id": "1" * 32,
                "generation": "0000000000000003", "roots": roots}))
            globals_ = M["capture_store"].__globals__
            original = globals_["native"]
            globals_["native"] = identity
            try:
                result = M["capture_store"](workspace, specs, Path(folder) / "valid")
                self.assertEqual(len(result["files"]), 4)
                header["volume_guid"] = prefix
                (store / "header.json").write_text(json.dumps(header))
                with self.assertRaises(ValueError):
                    M["capture_store"](workspace, specs, Path(folder) / "wrong-guid")
                header["volume_guid"] = guid
                (store / "header.json").write_text(json.dumps(header))
                (store / "unexpected").write_bytes(b"x")
                with self.assertRaises(ValueError):
                    M["capture_store"](workspace, specs, Path(folder) / "extra")
            finally:
                globals_["native"] = original

    def test_owner_boundaries(self):
        self.assertNotEqual(M["owner_id"]("ab", "c", "x"), M["owner_id"]("a", "bc", "x"))
        self.assertEqual(len(M["owner_id"]("spec", "root", "manifest.json")), 32)
        self.assertNotEqual(M["owner_id"]("spec", "root", "a"), M["owner_id"]("spec", "root", "b"))


if __name__ == "__main__":
    unittest.main()
