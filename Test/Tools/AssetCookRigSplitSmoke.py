"""実AssetCookの分離rig CLIを小さい合成入力で確認する。CIの起動は行わない。"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def fixture_bytes():
    data = bytearray(416)
    def floats(offset, values):
        struct.pack_into('<' + 'f' * len(values), data, offset, *values)
    floats(0, [0, 0, 0, 1, 0, 0, 0, 1, 0])
    floats(36, [0, 0, 1] * 3)
    floats(72, [0, 0, 1, 0, 0, 1])
    data[96:108] = bytes([0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0])
    floats(132, [.75, .25, 0, 0, .5, .5, 0, 0, 1, 0, 0, 0])
    struct.pack_into('<3H', data, 216, 0, 1, 2)
    for offset, y in ((224, 0), (288, -1)):
        matrix = [1 if i % 5 == 0 else 0 for i in range(16)]
        matrix[13] = y
        floats(offset, matrix)
    floats(352, [0, 2])
    floats(360, [0, 1, 0, 0, 3, 0])
    floats(384, [0, 0, 0, 1, 0, 0, 1, 0])
    return data


def run(executable, arguments, expected_success):
    result = subprocess.run([str(executable), *map(str, arguments)], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    if (result.returncode == 0) != expected_success:
        raise RuntimeError(f'終了コードが不正: {result.returncode}\n{result.stdout}\n{result.stderr}')
    if expected_success and 'RIG_SPLIT_COOK result=pass' not in result.stdout:
        raise RuntimeError('成功出力がありません')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--assetcook', type=Path, required=True)
    args = parser.parse_args()
    executable = args.assetcook.resolve(strict=True)
    repository = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='norves-rig-cli-') as temporary:
        root = Path(temporary)
        source_dir = root / '作者骨格'
        source_dir.mkdir()
        source = source_dir / '犬.gltf'
        source.write_bytes((repository / 'Assets/Models/M9Skinned/ValidU8Float.gltf').read_bytes())
        (source_dir / 'fixture.bin').write_bytes(fixture_bytes())
        output = root / '出力の親' / '出力'
        command = ['--rig-split', '--input', source, '--out', str(output.parent / 'unused' / '..' / output.name) + '/' ,
                   '--logical', 'Models/Dog', '--root-joint', 'Root']
        run(executable, command, True)
        manifest = output / 'manifest.json'
        before = manifest.read_bytes()
        entries = json.loads(before)['assets']
        if len(entries) != 3 or any(e['metadata']['profile'] != 3 for e in entries):
            raise RuntimeError('3資産/profile3ではありません')
        for entry in entries:
            if not (output / entry['cooked_package']).is_file():
                raise RuntimeError('packageがありません')
        run(executable, command, False)
        if manifest.read_bytes() != before:
            raise RuntimeError('拒否時に既存manifestが変更されました')
        bad = root / 'bad'
        run(executable, ['--rig-split', '--input', source, '--out', bad, '--logical', 'Models/Dog',
                         '--root-joint', 'missing'], False)
        if bad.exists():
            raise RuntimeError('失敗した出力が公開されました')
        run(executable, ['--rig-split', '--input', source, '--out', bad, '--logical', 'Models/Dog',
                         '--source-fps', '16'], False)
    print('RIG_SPLIT_CLI_SMOKE result=pass unicode_three_assets_no_replace_bad_root_fps_pair')


if __name__ == '__main__':
    main()
