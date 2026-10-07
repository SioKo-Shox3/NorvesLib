"""独立literalで分離roleのbytesを照合する。B1のreference/goldenは変更しない。"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import unittest
from ClipBankV1Oracle import fnv, topology


def envelope(role, sections):
    value = bytearray(256 + 32 * len(sections))
    value[:8] = b"NVSKELv1"
    struct.pack_into("<IHHIIIIQ", value, 8, 256, 1, 0, 0x01020304, role, 0, len(sections), 256)
    struct.pack_into("<QII", value, 56, fnv(topology()), 1, 1)
    for i, (code, stride, data) in enumerate(sections):
        value.extend(bytes(-len(value) % 16))
        struct.pack_into("<4sIQQII", value, 256+i*32, code, 1, len(value), len(data), stride, len(data)//stride)
        value.extend(data)
    struct.pack_into("<QQ", value, 40, len(value), fnv(value[256:]))
    return bytes(value)


def joints():
    return (struct.pack("<QIIQ", 0, 5, 1, fnv(b"Child"))
            + struct.pack("<QIIQ", 5, 4, 0xffffffff, fnv(b"Root")))


def rests():
    return [struct.pack("<10f", 0, y, 0, 0, 0, 0, 1, 1, 1, 1) for y in (1, 0)]


def matrix(x=0, y=0):
    return struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, 0, 1)


def reference_skeleton():
    label = b"RigV1Fixture/rig.gltf"
    rset = struct.pack("<IIQIIQdQ", 0, 2, 9, len(label), 1, fnv(b"".join(rests())), 1, 0)
    return envelope(1, [(b"STRS", 1, b"ChildRoot"+label), (b"TJNT", 24, joints()),
                        (b"RSET", 48, rset), (b"ARST", 48, b"".join(v+bytes(8) for v in rests())),
                        (b"ROOT", 64, matrix())])


def reference_mesh():
    path, slot = b"Models/Rig.nvskel", b"Default"
    strings = b"ChildRoot" + path + slot
    sref = struct.pack("<QIIQQQQ", 9, len(path), 1, fnv(topology()), fnv(reference_skeleton()),
                       fnv(b"".join(rests())), fnv(matrix())) + bytes(16)
    positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    uv = [(0, 0), (1, 0), (0, 1)]
    indices = [(1, 0, 1, 1), (1, 0, 1, 1), (0, 1, 1, 1)]
    weights = [(0.75, 0.25, 0, 0), (0.5, 0.5, 0, 0), (1, 0, 0, 0)]
    vertices = b"".join(struct.pack("<8f4I4f", *p, 0, 0, 1, *t, *j, *w)
                        for p, t, j, w in zip(positions, uv, indices, weights))
    submesh = struct.pack("<6I4f", 0, 3, 0, 0, 0, 0, 0, 0, 0, 0) + bytes(24)
    slots = struct.pack("<QIIIIQ", 9+len(path), len(slot), 0, 0, 0, 0)
    material = bytes(64) + struct.pack("<4f3f6f3I", 1, 1, 1, 1, 0, 0, 0, 0, -1, -1, 1, 1, 0.5, 0, 0, 0)
    return envelope(2, [(b"STRS", 1, strings), (b"TJNT", 24, joints()), (b"SREF", 64, sref),
                        (b"VERT", 64, vertices), (b"INDX", 4, struct.pack("<3I", 0, 1, 2)),
                        (b"IBMS", 64, matrix(y=-1)+matrix()), (b"MNGT", 64, matrix(x=5)),
                        (b"SUBM", 64, submesh), (b"MSLT", 32, slots), (b"MATS", 128, material)])


def check(actual, role):
    expected = reference_skeleton() if role == 1 else reference_mesh()
    if actual != expected:
        raise ValueError(f"role{role}のbytesが独立literalと一致しません")
    return {"bytes": len(actual), "sha256": hashlib.sha256(actual).hexdigest()}


def check_pose(value):
    expected={"schema":1,"clips":2,"child_palette_y":1.0,"child_model_y":2.0,"vertex_x":-5.0,"vertex_y":2.0,"materials_render_staged":False}
    if set(value)!=set(expected):
        raise ValueError("split pose fields")
    for key,wanted in expected.items():
        actual=value[key]
        if isinstance(wanted,bool):
            valid=type(actual) is bool and actual==wanted
        elif isinstance(wanted,int):
            valid=type(actual) is int and actual==wanted
        else:
            valid=type(actual) in (int,float) and abs(actual-wanted)<=1e-6
        if not valid:
            raise ValueError("split pose value: "+key)


class OracleTests(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual(len(reference_skeleton()), 704)
        self.assertEqual(len(reference_mesh()), 1360)

    def test_headers(self):
        for role, value in [(1, reference_skeleton()), (2, reference_mesh())]:
            self.assertEqual(struct.unpack_from("<I", value, 20)[0], role)
            self.assertEqual(struct.unpack_from("<QQ", value, 40), (len(value), fnv(value[256:])))
            self.assertEqual(struct.unpack_from("<Q", value, 56)[0], 0x7498d74adc178547)

    def test_content_pin(self):
        value = reference_mesh()
        offset = struct.unpack_from("<Q", value, 256+2*32+8)[0]
        self.assertEqual(struct.unpack_from("<Q", value, offset+24)[0], fnv(reference_skeleton()))

    def test_all_changed_bytes(self):
        for role, value in [(1, reference_skeleton()), (2, reference_mesh())]:
            for i in range(len(value)):
                changed = bytearray(value)
                changed[i] ^= 1
                with self.assertRaises(ValueError):
                    check(changed, role)

    def test_all_truncations(self):
        for role, value in [(1, reference_skeleton()), (2, reference_mesh())]:
            for i in range(len(value)):
                with self.assertRaises(ValueError):
                    check(value[:i], role)

    def test_pose(self):
        value={"schema":1,"clips":2,"child_palette_y":1,"child_model_y":2,"vertex_x":-5,"vertex_y":2,"materials_render_staged":False}
        check_pose(value)
        for key in value:
            broken=dict(value)
            broken[key]=True if key=="materials_render_staged" else 99
            with self.assertRaises(ValueError):
                check_pose(broken)

    def test_material_stride(self):
        value = reference_mesh()
        offset = struct.unpack_from("<Q", value, 256+9*32+8)[0]
        self.assertEqual(len(value)-offset, 128)
        self.assertEqual(struct.unpack_from("<ff", value, offset+96), (-1, -1))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--skeleton", type=Path, action="append", default=[])
    parser.add_argument("--mesh", type=Path, action="append", default=[])
    parser.add_argument("--pose", type=Path, action="append", default=[])
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(OracleTests)
        if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
            raise SystemExit(1)
    result={"skeleton":[],"mesh":[],"pose":[]}
    for key,role,paths in [("skeleton",1,args.skeleton),("mesh",2,args.mesh)]:
        for path in paths:
            result[key].append(check(path.read_bytes(),role))
    for path in args.pose:
        value=json.loads(path.read_text(encoding="utf-8-sig"))
        check_pose(value)
        result["pose"].append(value)
    if any(result.values()):
        print(json.dumps(result,sort_keys=True))
