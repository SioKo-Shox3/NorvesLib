#!/usr/bin/env python3
"""独立した実CLI契約。旧25件・79出力のbaselineには混ぜない。"""
import argparse
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def buffer():
    """既存M9の416byte入力を独立したliteralから生成する。"""
    b = bytearray(416)
    def floats(offset, values):
        struct.pack_into('<' + 'f' * len(values), b, offset, *values)
    floats(0, [0, 0, 0, 1, 0, 0, 0, 1, 0])
    floats(36, [0, 0, 1] * 3)
    floats(72, [0, 0, 1, 0, 0, 1])
    b[96:108] = bytes([0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0])
    floats(132, [.75, .25, 0, 0, .5, .5, 0, 0, 1, 0, 0, 0])
    struct.pack_into('<3H', b, 216, 0, 1, 2)
    for at, y in [(224, 0), (288, -1)]:
        m = [1 if i % 5 == 0 else 0 for i in range(16)]
        m[13] = y
        floats(at, m)
    floats(352, [0, 2])
    floats(360, [0, 1, 0, 0, 3, 0])
    floats(384, [0, 0, 0, 1, 0, 0, 1, 0])
    return bytes(b)


def glb(document, binary):
    document = copy.deepcopy(document)
    document['buffers'] = [{'byteLength': len(binary)}]
    text = json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<5I', 0x46546C67, 2, 28 + len(text) + len(binary), len(text), 0x4E4F534A)
            + text + struct.pack('<2I', len(binary), 0x004E4942) + binary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    check(os.name == 'nt', 'real Windows CreateProcessW argv acceptance is required')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    inputs, results, logs = output/'inputs', output/'results', output/'logs'
    for directory in [inputs, results, logs]:
        directory.mkdir()
    # BMPと非BMPを全4入力locatorに含める。subprocessのstr引数はWindows CreateProcessWを通る。
    unicode_dir = inputs/'日本語 data_\U0001F43A'
    unicode_dir.mkdir()
    rig = unicode_dir/'骨格_\U0001F43A.glb'
    bvh = unicode_dir/'動き_\U0001F43A.bvh'
    profile = unicode_dir/'役割_\U0001F43A.json'
    sidecar = unicode_dir/'設定_\U0001F43A.json'
    base = json.loads((args.source/'Assets/Models/M9Skinned/ValidU8Float.gltf').read_text(encoding='utf-8-sig'))
    binary = buffer()
    empty = copy.deepcopy(base)
    del empty['animations']
    rig.write_bytes(glb(empty, binary))
    bvh_text = ('HIERARCHY ROOT Source { OFFSET 0 0 0 CHANNELS 3 Zrotation Xrotation Yrotation End Site { OFFSET 0 1 0 } } '
                'MOTION Frames: 3 Frame Time: 0.5\n0 0 0\n45 0 0\n90 0 0\n')
    bvh.write_text(bvh_text, encoding='utf-8')
    roles = {'version': 1, 'vocabulary': 'quadruped_v1',
             'axes': {'up': '+Y', 'forward': '+Z', 'handedness': 'right'},
             'units': {'position_scale': 1}, 'position_convention': 'additive',
             'time': {'mode': 'header_frame_time'}, 'source_roles': {'root': ['Source']},
             'target_roles': {'root': [{'joint': 'Root', 'C': [1, 0, 0, 0, 1, 0, 0, 0, 1]}]}}
    profile.write_text(json.dumps(roles), encoding='utf-8')
    settings = {'version': 1, 'units': {'scale': 1}, 'axes': {'up': '+Y', 'forward': '+Z'}, 'origin': {'mode': 'keep'}}
    sidecar.write_text(json.dumps(settings), encoding='utf-8')
    package, manifest = results/'rig.nvpkg', results/'rig.json'
    clip_name = '歩行_\U0001F43A_"\\\n'
    command = [str(args.exe.resolve()), '--input', str(rig), '--out', str(package), '--manifest', str(manifest),
               '--logical', 'Models/role', '--kind', 'model', '--entry', 'rig.nvskel', '--entry-type', 'Skl0',
               '--format', 'nvskel.v0.skinned.pnujiw.u32', '--variant', 'default',
               '--bvh', str(bvh), '--role-profile', str(profile), '--clip-operation', 'add', '--clip-name', clip_name,
               '--import-settings', str(sidecar)]
    cases = []
    def call(label, argv, success):
        before = [(p.exists(), p.read_bytes() if p.exists() else None) for p in [package, manifest]]
        completed = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180, check=False)
        (logs/(label+'.stdout')).write_bytes(completed.stdout)
        (logs/(label+'.stderr')).write_bytes(completed.stderr)
        check(completed.returncode == (0 if success else 1), f'{label}: exit {completed.returncode}: {completed.stderr!r}')
        reports = [line[len(b'role_profile_report='):] for line in completed.stderr.splitlines()
                   if line.startswith(b'role_profile_report=')]
        if success:
            check(len(reports) == 1, label+': report count')
            report = json.loads(reports[0])
            raw = profile.read_bytes()
            raw_hash = 14695981039346656037
            for byte in raw:
                raw_hash = ((raw_hash ^ byte) * 1099511628211) & ((1 << 64)-1)
            check(report['profile']['bytes'] == len(raw) and report['profile']['hash'] == f'{raw_hash:016x}', label+': independent raw profile identity')
            check(report['version'] == 1 and report['clip_name'] == clip_name, label+': Unicode report')
            check(report['stored_keys_validated'] and not report['continuous_curve_validated'], label+': validation scope')
            check(report['rotation_only'] and not report['root_motion_generated'], label+': rotation scope')
            check(report['source_frames'] == 3 and report['time']['stored_duration'] == 1, label+': sample preserving')
            check(b'AssetCook cooked role-profile' in completed.stdout and b'skipped' not in completed.stdout, label+': always cook')
            check(package.is_file() and manifest.is_file(), label+': outputs')
        else:
            check(not reports and b'AssetCook cooked role-profile' not in completed.stdout, label+': no false success')
            check(b'AssetCook error:' in completed.stderr, label+': error prefix')
            check(before == [(p.exists(), p.read_bytes() if p.exists() else None) for p in [package, manifest]], label+': original output preservation')
            report = None
        cases.append({'label': label, 'exit': completed.returncode, 'success': success})
        return report
    def changed(flag, value):
        cmd = command.copy()
        cmd[cmd.index(flag)+1] = value
        return cmd
    first = call('unicode_missing_animation_add', command, True)
    bytes1 = (package.read_bytes(), manifest.read_bytes())
    call('repeat_still_cooks', command, True)
    check(bytes1 == (package.read_bytes(), manifest.read_bytes()), 'repeat byte identity')
    empty['animations'] = []
    rig.write_bytes(glb(empty, binary))
    call('empty_animation_add', command, True)
    rig.write_bytes(glb(base, binary))
    call('add_to_existing_clip', command, True)
    # 同一入力内の選択。出力packageの編集ではない。
    selected = copy.deepcopy(base)
    selected['animations'].append(copy.deepcopy(base['animations'][0]))
    selected['animations'][1]['name'] = clip_name
    rig.write_bytes(glb(selected, binary))
    replacement = call('replace_named_clip', changed('--clip-operation', 'replace'), True)
    check(replacement['clip_index'] == 1, 'replace exact index')
    call('add_collision', command, False)
    rig.write_bytes(glb(base, binary))
    call('replace_missing', changed('--clip-operation', 'replace'), False)
    for flag in ['--bvh', '--role-profile', '--clip-operation', '--clip-name']:
        cmd = command.copy(); at = cmd.index(flag); del cmd[at:at+2]
        call('missing_'+flag[2:], cmd, False)
        call('duplicate_'+flag[2:], command+[flag, command[command.index(flag)+1]], False)
        call('empty_'+flag[2:], changed(flag, ''), False)
    for flag, value in [('--asset-set', 'x.json'), ('--inspect', str(rig)), ('--recover', None), ('--skip-if-unchanged', None)]:
        call('mixed_'+flag[2:], command+[flag]+([] if value is None else [value]), False)
    call('unknown', command+['--source-fps', '30'], False)
    call('non_model', changed('--kind', 'raw'), False)
    call('non_skeletal', changed('--format', 'nvmesh.v0.mesh3d.pnt.u32'), False)
    call('sidecar_conflict', command+['--no-sidecar'], False)
    call('required_override', command+['--require-sidecar'], True)
    no_override = command.copy(); at = no_override.index('--import-settings'); del no_override[at:at+2]
    call('disabled_sidecar', no_override+['--no-sidecar'], True)
    call('required_missing', no_override+['--require-sidecar'], False)
    # 自動sidecarの正式命名は実装のmodel.ext.import.json規約。
    sidecar_auto = Path(str(rig)+'.import.json')
    sidecar_auto.write_bytes(sidecar.read_bytes())
    call('auto_sidecar', no_override+['--require-sidecar'], True)
    sidecar_auto.unlink()
    before_raw_change = call('before_raw_profile_change', command, True)
    profile_bytes = profile.read_bytes()
    profile.write_bytes(profile_bytes+b' ')
    raw_changed = call('raw_profile_hash', command, True)
    check(raw_changed['source_hash'] != before_raw_change['source_hash'], 'raw profile hash changes')
    profile.write_bytes(b'{"version":1,"version":1}')
    call('duplicate_profile_key', command, False)
    profile.write_bytes(profile_bytes)
    saved_bvh = bvh.read_bytes(); bvh.write_bytes(b'invalid')
    call('invalid_bvh', command, False); bvh.write_bytes(saved_bvh)
    for flag, path in [('--input', rig), ('--bvh', bvh), ('--role-profile', profile), ('--import-settings', sidecar)]:
        original = path.read_bytes(); path.unlink()
        call('missing_file_'+flag[2:], command, False)
        if flag in ['--bvh', '--role-profile']:
            error_bytes = (logs/('missing_file_'+flag[2:]+'.stderr')).read_bytes()
            check(path.as_posix().encode('utf-8') in error_bytes, 'Unicode diagnostic locator')
        path.write_bytes(original)
    saved_profile = profile.read_bytes()
    for label, mutate in [
        ('unknown_role', lambda d: d['source_roles'].update({'unknown': ['Source']})),
        ('missing_source_only', lambda d: d['source_roles'].update({'head': ['Missing']})),
        ('bad_c', lambda d: d['target_roles']['root'][0].update({'C': [0]*9})),
        ('chain_mismatch', lambda d: d['target_roles'].update({'neck': [{'joint': 'Child', 'C': [1,0,0,0,1,0,0,0,1]}]})),
    ]:
        invalid = copy.deepcopy(roles); mutate(invalid)
        profile.write_text(json.dumps(invalid), encoding='utf-8')
        call(label, command, False)
    profile.write_bytes(b' ' * (1024 * 1024 + 1))
    call('profile_budget', command, False)
    profile.write_bytes(saved_profile)
    saved_rig = rig.read_bytes()
    duplicate = copy.deepcopy(selected)
    duplicate['animations'].append(copy.deepcopy(duplicate['animations'][1]))
    rig.write_bytes(glb(duplicate, binary))
    call('replace_ambiguous', changed('--clip-operation', 'replace'), False)
    rig.write_bytes(saved_rig)
    # 手で置いた壊れたmanifestと孤立packageを採用しない。
    saved_manifest = manifest.read_bytes()
    manifest.write_bytes(b'{}')
    call('invalid_existing_manifest', command, False)
    manifest.write_bytes(saved_manifest)
    manifest.unlink()
    call('orphan_package', command, False)
    manifest.write_bytes(saved_manifest)
    # file guardが新しい二依存も保護する。
    for label, path in [('source', rig), ('bvh', bvh), ('profile', profile), ('sidecar', sidecar)]:
        original = path.read_bytes()
        for flag in ['--out', '--manifest']:
            call('alias_'+label+'_'+flag[2:], changed(flag, str(path)), False)
            check(path.read_bytes() == original, 'protected input bytes')
    loose = unicode_dir/'外部 rig_\U0001F43A.gltf'
    loose.write_text(json.dumps(base), encoding='utf-8')
    (unicode_dir/'fixture.bin').write_bytes(binary)
    call('unicode_external_gltf_base', changed('--input', str(loose)), True)
    # 旧modeのoperandに新flagと同じ文字列があっても新modeへ誤dispatchしない。
    legacy = subprocess.run([str(args.exe.resolve()), '--input', '--bvh'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    check(legacy.returncode == 1 and b'role_cli:' not in legacy.stderr and b'role_profile_report=' not in legacy.stderr, 'legacy operand dispatch')
    (logs/'legacy_flag_operand.stdout').write_bytes(legacy.stdout)
    (logs/'legacy_flag_operand.stderr').write_bytes(legacy.stderr)
    cases.append({'label': 'legacy_flag_operand', 'exit': legacy.returncode, 'success': False})
    # 成功時の新mode等号構文。
    equal_command = [command[0]]+[command[i]+'='+command[i+1] for i in range(1, len(command), 2)]
    call('equals_arguments', equal_command, True)
    inventory = []
    for path in sorted(output.rglob('*')):
        if path.is_file():
            content = path.read_bytes()
            inventory.append({'path': path.relative_to(output).as_posix(), 'size': len(content), 'sha256': hashlib.sha256(content).hexdigest()})
    receipt = {'version': 1, 'argv': 'Windows_CreateProcessW', 'GetACP': ctypes.windll.kernel32.GetACP(), 'asset_cook_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
               'cases': cases, 'inventory': inventory}
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'ROLE_PROFILE_CLI result=pass cases={len(cases)} real_windows_unicode_inputs=4 always_cook=true legacy_inventory_unchanged=true')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
