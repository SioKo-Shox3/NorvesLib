"""合成祖先の独立行列と既存独立literalからprofile2だけを構成し、実bytes/poseを照合する。"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import unittest
from ClipBankV1Oracle import fnv, reference_bank as old_bank
from RigSplitV1Oracle import reference_skeleton as old_skeleton, reference_mesh as old_mesh


def multiply(a, b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]


def translation(x=0, y=0, z=0):
    return [1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1]


ARM = [-2,0,0,0,0,-2,0,0,0,0,2,0,2,3,0,1]
WORLD = [1,0,0,0,0,-1,0,0,0,0,-1,0,-1,4,1,1]
FRAME = multiply(ARM, WORLD)
FRAME_BYTES = struct.pack('<16f', *FRAME)


def sections(value):
    result = []
    for i in range(struct.unpack_from('<I', value, 28)[0]):
        code, flags, offset, size, stride, count = struct.unpack_from('<4sIQQII', value, 256+i*32)
        if flags != 1 or size != stride*count:
            raise ValueError('元の独立literalの表が不正です')
        result.append((code, stride, value[offset:offset+size]))
    return result


def envelope(role, entries, skeleton_id):
    value = bytearray(256+32*len(entries))
    value[:8] = b'NVSKELv1'
    struct.pack_into('<IHHIIIIQ', value, 8, 256, 1, 0, 0x01020304, role, 0, len(entries), 256)
    struct.pack_into('<QII', value, 56, skeleton_id, 2, 1)
    for i, (code, stride, data) in enumerate(entries):
        value.extend(bytes(-len(value)%16))
        struct.pack_into('<4sIQQII', value, 256+i*32, code, 1, len(value), len(data), stride, len(data)//stride)
        value.extend(data)
    struct.pack_into('<QQ', value, 40, len(value), fnv(value[256:]))
    return bytes(value)


def reference(role):
    # 元データはC++ writerの出力でなく、既存のPython独立fixture literal。
    old = {1:old_skeleton, 2:old_mesh, 3:old_bank}[role]()
    entries = []
    for code, stride, data in sections(old):
        data = bytearray(data)
        if code == b'RSET':
            for o in range(0, len(data), 48):
                struct.pack_into('<I', data, o+20, 2)
        elif code == b'ROOT':
            data[:] = FRAME_BYTES
        elif code == b'MATS':
            # このfixtureは直接builderではなくglTF cookerを通る。
            # 材質省略時のglTF既定はmetallic=roughness=1。単一三角形は閉じておらず、
            # 既定doubleSided=autoは両面にする。旧profile1 builderの-1 sentinelは引き継がない。
            struct.pack_into('<ff', data, 96, 1, 1)
            struct.pack_into('<I', data, 116, 1)
        elif code == b'SREF':
            struct.pack_into('<I', data, 12, 2)
            struct.pack_into('<Q', data, 24, fnv(reference(1)))
            struct.pack_into('<Q', data, 40, fnv(FRAME_BYTES))
        entries.append((code, stride, bytes(data)))
    if role == 3:
        # 旧7節の意味を保ち、必須作者frame節を末尾へ追加する。
        entries.append((b'AFRM', 64, FRAME_BYTES))
    return envelope(role, entries, struct.unpack_from('<Q', old, 56)[0])


def expected_pose():
    inverse_mesh = translation(-5)
    root_model = multiply(FRAME, inverse_mesh)
    child_model = multiply(multiply(translation(y=2), FRAME), inverse_mesh)
    child_palette = multiply(multiply(multiply(translation(y=-1), translation(y=2)), FRAME), inverse_mesh)
    vertex = [sum([0,1,0,1][k]*child_palette[k*4+c] for k in range(4)) for c in range(3)]
    return {'profile':2, 'clips':1, 'palettes':[child_palette, root_model],
            'models':[child_model, root_model], 'vertex':vertex,
            'frame_comparison_complete':True, 'no_render_lease':True, 'manual_clip_rejected':True}


def expected_general_pose():
    arm = [0,2,0,0,-2,0,0,0,0,0,2,0,2,3,0,1]
    world = [1,0,0,0,0,0,1,0,0,-1,0,0,-1,4,1,1]
    frame = multiply(arm, world)
    root = [-2,0,0,0,0,-3,0,0,0,0,4,0,0,3,0,1]
    child = multiply(translation(y=3), root)
    inv_mesh = translation(-5)
    models = [multiply(multiply(child, frame), inv_mesh), multiply(multiply(root, frame), inv_mesh)]
    palettes = [multiply(multiply(multiply(translation(y=-1), child), frame), inv_mesh), models[1]]
    vertex = [sum([0,1,0,1][k]*palettes[0][k*4+c] for k in range(4)) for c in range(3)]
    return {'palettes':palettes, 'models':models, 'vertex':vertex}


def check_wire(actual, role):
    if actual != reference(role):
        raise ValueError(f'profile2 role{role}が独立literalと一致しません')
    return {'bytes':len(actual), 'sha256':hashlib.sha256(actual).hexdigest()}


def check_pose(actual, general=False):
    def compare(a, e):
        if isinstance(e, dict):
            return type(a) is dict and set(a) == set(e) and all(compare(a[k],v) for k,v in e.items())
        if isinstance(e, list):
            return type(a) is list and len(a) == len(e) and all(compare(x,y) for x,y in zip(a,e))
        if type(e) is bool:
            return type(a) is bool and a == e
        return type(a) in (int,float) and math.isfinite(a) and abs(a-e) <= (1e-4 if general else 1e-6)
    if not compare(actual, expected_general_pose() if general else expected_pose()):
        raise ValueError('profile2の実姿勢/束縛診断が独立期待値と一致しません')


class Tests(unittest.TestCase):
    def test_parent_product(self):
        self.assertEqual(FRAME, [-2,0,0,0,0,2,0,0,0,0,-2,0,1,1,1,1])
        self.assertNotEqual(FRAME, multiply(WORLD, ARM))

    def test_wire_sizes(self):
        self.assertEqual([len(reference(i)) for i in (1,2,3)], [704,1360,1088])

    def test_headers(self):
        for role in (1,2,3):
            b = reference(role)
            self.assertEqual(struct.unpack_from('<QQ',b,40), (len(b),fnv(b[256:])))
            self.assertEqual(struct.unpack_from('<II',b,64), (2,1))
            self.assertEqual(struct.unpack_from('<I',b,20)[0],role)

    def test_required_author_frame(self):
        code, flags, offset, size, stride, count = struct.unpack_from('<4sIQQII',reference(3),256+7*32)
        self.assertEqual((code,flags,size,stride,count),(b'AFRM',1,64,64,1))
        self.assertEqual(reference(3)[offset:offset+size],FRAME_BYTES)

    def test_mesh_pins(self):
        sref = next(data for code,_,data in sections(reference(2)) if code==b'SREF')
        self.assertEqual(struct.unpack_from('<Q',sref,24)[0],fnv(reference(1)))
        self.assertEqual(struct.unpack_from('<Q',sref,40)[0],fnv(FRAME_BYTES))

    def test_cooked_material_defaults(self):
        material = next(data for code,_,data in sections(reference(2)) if code==b'MATS')
        self.assertEqual(struct.unpack_from('<ff',material,96),(1,1))
        self.assertEqual(struct.unpack_from('<I',material,116)[0],1)
        legacy = next(data for code,_,data in sections(old_mesh()) if code==b'MATS')
        self.assertEqual(struct.unpack_from('<ff',legacy,96),(-1,-1))
        self.assertEqual(struct.unpack_from('<I',legacy,116)[0],0)

    def test_mutations(self):
        for role in (1,2,3):
            b = reference(role)
            for i in range(len(b)):
                broken = bytearray(b);broken[i]^=1
                with self.assertRaises(ValueError):check_wire(broken,role)

    def test_truncations(self):
        for role in (1,2,3):
            b=reference(role)
            for i in range(len(b)):
                with self.assertRaises(ValueError):check_wire(b[:i],role)

    def test_pose(self):
        pose=expected_pose();check_pose(pose)
        self.assertEqual(pose['vertex'],[-4,5,1])
        self.assertEqual(pose['palettes'][0][12:15],[-4,3,1])
        self.assertEqual(pose['models'][0][12:15],[-4,5,1])
        for field in pose:
            bad=json.loads(json.dumps(pose));bad[field]=False
            with self.assertRaises(ValueError):check_pose(bad)

    def test_pose_wrong_order_controls(self):
        expected=expected_pose()['models'][0]
        no_frame=multiply(translation(y=2),translation(-5))
        wrong_order=multiply(multiply(translation(-5),translation(y=2)),FRAME)
        double_frame=multiply(multiply(multiply(translation(y=2),FRAME),FRAME),translation(-5))
        for wrong in (no_frame,wrong_order,double_frame):self.assertNotEqual(wrong,expected)

    def test_general_root_trs(self):
        expected=expected_general_pose();check_pose(expected,general=True)
        self.assertEqual(expected['vertex'],[8,4,4])
        self.assertEqual(expected['palettes'][0][12:15],[2,4,4])
        self.assertEqual(expected['models'][0][12:15],[8,4,4])
        for field in expected:
            broken=json.loads(json.dumps(expected));broken[field][0]=999
            with self.assertRaises(ValueError):check_pose(broken,general=True)

    def test_profile1_separate(self):
        for role,old in ((1,old_skeleton()),(2,old_mesh()),(3,old_bank())):
            with self.assertRaises(ValueError):check_wire(old,role)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--self-test',action='store_true')
    for name in ('skeleton','mesh','bank','pose'):parser.add_argument('--'+name,type=Path,action='append',default=[])
    parser.add_argument('--general-pose',type=Path,action='append',default=[])
    args=parser.parse_args()
    if args.self_test and not unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests)).wasSuccessful():raise SystemExit(1)
    result={}
    for name,role in (('skeleton',1),('mesh',2),('bank',3)):
        result[name]=[check_wire(path.read_bytes(),role) for path in getattr(args,name)]
    result['pose']=[]
    for path in args.pose:
        value=json.loads(path.read_text(encoding='utf-8'));check_pose(value);result['pose'].append(value)
    result['general_pose']=[]
    for path in args.general_pose:
        value=json.loads(path.read_text(encoding='utf-8'));check_pose(value,general=True);result['general_pose'].append(value)
    print(json.dumps(result,ensure_ascii=False,sort_keys=True))
