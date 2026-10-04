"""native texture-v1を固定旧PS1の生byteと照合し、失敗時の非公開を検査する。"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def files(root: Path) -> dict[str, bytes]:
    result = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError("検証出力にsymlinkがあります")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = path.read_bytes()
    return result


def compare(expected: dict[str, bytes], actual: dict[str, bytes]) -> None:
    if expected.keys() != actual.keys():
        raise ValueError("出力一覧が一致しません")
    for name in expected:
        if expected[name] != actual[name]:
            raise ValueError("出力byteが一致しません: " + name)


def load_reference(root: Path, receipt: dict) -> tuple[dict, dict[str, bytes]]:
    raw = (root / "snapshot.json").read_bytes()
    if digest(raw) != receipt["snapshot_sha256"]:
        raise ValueError("固定texture snapshot hash不一致")
    snapshot = json.loads(raw)
    actual = files(root / "files")
    expected = {entry["path"] for entry in snapshot["files"]}
    if len(expected) != 10 or len(snapshot["files"]) != 10 or actual.keys() != expected:
        raise ValueError("固定texture snapshot一覧不一致")
    for entry in snapshot["files"]:
        data = actual[entry["path"]]
        if len(data) != entry["size"] or digest(data) != entry["sha256"]:
            raise ValueError("固定texture payload hash不一致")
    return snapshot, actual


def run(source: Path, exe: Path, reference: Path, output: Path) -> None:
    if os.name != "nt":
        raise ValueError("実Windows検証が必要です")
    receipt = json.loads((source / "Docs/AssetCookTextureFrozenBaseline.json").read_text())
    snapshot, expected = load_reference(reference, receipt)
    input_hashes = {name: digest((source / name).read_bytes()) for name in snapshot["checkout_inputs"]}
    if any(input_hashes[name] != record["sha256"] for name, record in snapshot["checkout_inputs"].items()):
        raise ValueError("固定上流inputと現在のcheckout byteが一致しません")
    exe_hash = digest(exe.read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    logs = output / "logs"
    logs.mkdir()
    runs = []
    powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if digest(powershell.read_bytes()) != receipt["powershell_sha256"]:
        raise ValueError("固定PowerShell binaryと違います")
    ps_env = {key: value for key, value in os.environ.items() if key.casefold() != "psmodulepath"}
    junction_script = output / "junction.ps1"
    junction_script.write_text('param([string]$Link,[string]$Target)\n$ErrorActionPreference="Stop"\nNew-Item -ItemType Junction -Path $Link -Target $Target | Out-Null\n', encoding="ascii")

    def junction(link: Path, target: Path) -> None:
        command = [str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(junction_script), "-Link", str(link), "-Target", str(target)]
        result = subprocess.run(command, capture_output=True, env=ps_env)
        if result.returncode != 0 or not link.is_junction():
            raise ValueError("実junction fixtureを作成できません")

    def invoke(args: list[str], cwd: Path, label: str, success: bool) -> None:
        result = subprocess.run([str(exe), *args], cwd=cwd, capture_output=True)
        (logs / (label + ".stdout.log")).write_bytes(result.stdout)
        (logs / (label + ".stderr.log")).write_bytes(result.stderr)
        runs.append({"label": label, "arguments": args, "cwd": str(cwd), "exit_code": result.returncode})
        if (result.returncode == 0) != success:
            raise ValueError(f"想定と違う終了code: {label}={result.returncode}")

    for repeat in (1, 2):
        parent = output / ("native" + str(repeat))
        parent.mkdir()
        combined = {}
        for case in ("Rendering3DTestSilverTextures", "Rendering3DTestSilverGltfTextures"):
            runtime = parent / case
            args = ["--asset-set", str(source / "Assets/AssetSets" / (case + ".json")), "--source-root", str(source), "--runtime-root", str(runtime)]
            invoke(args, output, case + "-" + str(repeat), True)
            combined.update({case + "/" + name: data for name, data in files(runtime).items()})
        compare(expected, combined)

    fixture = output / "fixture"
    fixture.mkdir()
    (fixture / "good.ppm").write_bytes(b"P6\n2 2\n255\nabcdefghijkl")
    (fixture / "bad.ppm").write_bytes(b"not an image")
    (fixture / "犬🐺.ppm").write_bytes((fixture / "good.ppm").read_bytes())
    unicode_cwd = fixture / "日本語"
    unicode_cwd.mkdir()
    (unicode_cwd / "good.ppm").write_bytes((fixture / "good.ppm").read_bytes())
    image_hashes = {name: digest((fixture / name).read_bytes()) for name in ("good.ppm", "bad.ppm", "犬🐺.ppm", "日本語/good.ppm")}
    base = {"version": 1, "name": "probe", "package_root": "Cooked/Probe", "default_variant": "default", "textures": [
        {"logical_path": "Textures/z.ppm", "source_path": "good.ppm", "format": "nvtex.v0.rgba8.linear", "package_name": "z.nvpkg", "entry_name": "z.nvtex"},
        {"logical_path": "Textures/a.ppm", "source_path": "good.ppm", "format": "nvtex.v0.rgba8.linear", "package_name": "a.nvpkg", "entry_name": "a.nvtex", "variant": "night"}]}

    def execute(spec: dict, label: str, success: bool, extra=None, cwd=None, explicit_source=True, existing=None, source_override=None):
        spec_path = fixture / (label + ".json")
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        runtime = output / label
        if existing == "directory":
            runtime.mkdir()
            (runtime / "keep").write_bytes(b"previous root")
        elif existing == "empty":
            runtime.mkdir()
        elif existing == "file":
            runtime.write_bytes(b"previous file")
        elif existing == "junction":
            target = output / (label + "-target")
            target.mkdir()
            (target / "keep").write_bytes(b"junction target")
            junction(runtime, target)
        before = files(runtime) if runtime.is_dir() else runtime.read_bytes() if runtime.is_file() else None
        stages = set(output.glob(".assetcook-stage-*"))
        args = ["--asset-set", str(spec_path), "--runtime-root", str(runtime)]
        if explicit_source:
            args += ["--source-root", str(source_override or fixture)]
        if extra:
            args += extra(runtime)
        invoke(args, cwd or source, label, success)
        if not success:
            if before is None and os.path.lexists(runtime):
                raise ValueError("失敗したrootが公開されました: " + label)
            if isinstance(before, dict):
                compare(before, files(runtime))
            elif before is not None and runtime.read_bytes() != before:
                raise ValueError("既存fileが変わりました")
        if set(output.glob(".assetcook-stage-*")) != stages:
            raise ValueError("失敗または成功後にstageが残りました: " + label)
        return runtime

    execute(base, "default-source", True, cwd=fixture, explicit_source=False)
    execute(base, "unicode-cwd", True, cwd=unicode_cwd, explicit_source=False)
    unicode_spec = json.loads(json.dumps(base))
    unicode_spec["textures"][0]["source_path"] = "犬🐺.ppm"
    execute(unicode_spec, "unicode-source", True)
    custom = execute(base, "custom-manifest", True, extra=lambda root: ["--manifest", str(root / "custom.json")])
    if not (custom / "custom.json").is_file() or (custom / "manifest.json").exists():
        raise ValueError("custom manifestが使われていません")
    for kind in ("directory", "empty", "file", "junction"):
        execute(base, "existing-" + kind, False, existing=kind)
    linked_source = output / "linked-source"
    junction(linked_source, fixture)
    execute(base, "source-reparse", False, source_override=linked_source)
    for label, field, value in (("late-corrupt", "source_path", "bad.ppm"), ("missing-source", "source_path", "missing.ppm"),
                                ("bad-format", "format", "unknown"), ("reserved-name", "package_name", "CON.nvpkg"),
                                ("invalid-name", "package_name", "a?.nvpkg")):
        changed = json.loads(json.dumps(base))
        changed["textures"][-1][field] = value
        execute(changed, label, False)
    changed = json.loads(json.dumps(base))
    changed["textures"][0]["package_name"] = "a.nvpkg"
    changed["textures"][1]["package_name"] = "a.nvpkg/sub.nvpkg"
    changed["textures"].append(dict(base["textures"][0], logical_path="Textures/middle.ppm", package_name="a.nvpkg-other"))
    execute(changed, "prefix-collision", False)
    execute(base, "manifest-collision", False, extra=lambda root: ["--manifest", str(root / "Cooked")])
    for label, extras in (("mixed", ["--kind", "texture"]), ("skip", ["--skip-if-unchanged"]), ("duplicate", ["--asset-set", "unused"]), ("empty-value", ["--manifest="])):
        execute(base, label, False, extra=lambda root, extras=extras: extras)

    # 同じ実PS5.1のserializerで全printable ASCIIを照合する。native出力の正規化はしない。
    changed = json.loads(json.dumps(base))
    changed["textures"][0]["variant"] = "".join(chr(c) for c in range(32, 127))
    probe = execute(changed, "ascii-probe", True)
    wrapper = output / "serializer-probe.ps1"
    wrapper.write_text('''param([string]$InputFile,[string]$OutputFile)
$ErrorActionPreference="Stop"
if ($PSVersionTable.PSVersion.ToString() -ne "5.1.20348.5622") { throw "PowerShell version changed" }
$value=Get-Content -LiteralPath $InputFile -Raw | ConvertFrom-Json
$json=$value | ConvertTo-Json -Depth 16
[System.IO.File]::WriteAllText($OutputFile,$json,[System.Text.UTF8Encoding]::new($false))
''', encoding="ascii")
    ps_output = output / "ascii-ps51.json"
    env = {key: value for key, value in os.environ.items() if key.casefold() != "psmodulepath"}
    result = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(wrapper), "-InputFile", str(probe / "manifest.json"), "-OutputFile", str(ps_output)], capture_output=True, env=env)
    (logs / "ps-probe.stdout.log").write_bytes(result.stdout)
    (logs / "ps-probe.stderr.log").write_bytes(result.stderr)
    if result.returncode != 0 or ps_output.read_bytes() != (probe / "manifest.json").read_bytes():
        raise ValueError("PS5.1 ASCII escape/format probe不一致")
    if any(digest((source / name).read_bytes()) != value for name, value in input_hashes.items()) or digest(exe.read_bytes()) != exe_hash:
        raise ValueError("検証中に入力または実行fileが変わりました")
    if any(digest((fixture / name).read_bytes()) != value for name, value in image_hashes.items()):
        raise ValueError("検証中にsource imageが変わりました")
    result = {"schema": "norves.texture-native-acceptance.v1", "asset_cook_sha256": exe_hash, "driver_sha256": digest(Path(__file__).read_bytes()), "feature_sha": os.environ.get("GITHUB_SHA"), "reference_snapshot_sha256": receipt["snapshot_sha256"],
              "reference_files": 10, "repeat_byte_equal": True, "ascii_ps51_equal": True, "runs": runs}
    (output / "verification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Native texture v1: 2spec x 2repeat / 10file byte parity / rejection contracts / PS5.1 ASCII probe PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "exe", "reference", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    run(args.source.resolve(), args.exe.resolve(), args.reference.resolve(), args.output.resolve())
