"""native比較器が差分や基準破損を正規化せず拒否することを検証する。"""
import json
from pathlib import Path
import runpy
import tempfile
import unittest
API=runpy.run_path(str(Path(__file__).resolve().parents[2]/"Scripts/VerifyTextureAssetSet.py"))


class VerificationTest(unittest.TestCase):
    def test_equal(self):
        API["compare"]({"a":b"\0\xff\r\n"},{"a":b"\0\xff\r\n"})

    def test_changed(self):
        for actual in ({"a":b"x\n"},{"a":b"x\r\n\n"},{"a":b"\xef\xbb\xbfx\r\n"}):
            with self.assertRaises(ValueError): API["compare"]({"a":b"x\r\n"},actual)

    def test_inventory(self):
        for actual in ({},{"b":b"x"},{"a":b"x","b":b"x"}):
            with self.assertRaises(ValueError): API["compare"]({"a":b"x"},actual)

    def test_reference(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/"files").mkdir();rows=[]
            for i in range(10):
                data=bytes([i]);name=str(i);(root/"files"/name).write_bytes(data)
                rows.append({"path":name,"size":len(data),"sha256":API["digest"](data)})
            raw=json.dumps({"files":rows}).encode();(root/"snapshot.json").write_bytes(raw)
            receipt={"snapshot_sha256":API["digest"](raw)}
            self.assertEqual(len(API["load_reference"](root,receipt)[1]),10)
            (root/"files/0").write_bytes(b"changed")
            with self.assertRaises(ValueError): API["load_reference"](root,receipt)
            (root/"snapshot.json").write_bytes(raw+b" ")
            with self.assertRaises(ValueError): API["load_reference"](root,receipt)


if __name__=="__main__": unittest.main()
