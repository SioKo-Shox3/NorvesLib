#!/usr/bin/env python3
"""単体 AssetCook の smoke を固定し、生成物を正規化せずバイト比較する。"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

SMOKES = ("Texture", "Audio", "Mesh", "Glb", "Import")
SCHEMA = 1


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifacts(root: Path) -> dict[str, bytes]:
    """package と manifest を収集する。JSONの空白や順序も比較対象。"""
    if root.is_symlink():
        raise ValueError(f"シンボリックリンクは比較対象にできません: {root}")
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"シンボリックリンクは比較対象にできません: {path}")
        if not path.is_file():
            continue
        selected = path.suffix.lower() == ".nvpkg"
        if path.suffix.lower() == ".json":
            selected = "manifest" in path.stem.lower()
            if not selected:
                try:
                    value = json.loads(path.read_bytes().decode("utf-8-sig"))
                    selected = isinstance(value, dict) and "version" in value and "assets" in value
                except (ValueError, UnicodeError):
                    pass
        if selected:
            result[path.relative_to(root).as_posix()] = path.read_bytes()
    return result


def write_snapshot(root: Path, destination: Path, metadata: dict) -> None:
    values = artifacts(root)
    if not values or not any(p.endswith(".nvpkg") for p in values):
        raise ValueError("比較するpackageがありません")
    destination.mkdir(parents=True, exist_ok=False)
    entries = []
    for name, data in values.items():
        out = destination / "files" / name
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        entries.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    document = {"schema": SCHEMA, "metadata": metadata, "files": entries}
    (destination / "snapshot.json").write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_snapshot(root: Path) -> tuple[dict, dict[str, bytes]]:
    if root.is_symlink() or (root / "files").is_symlink() or (root / "snapshot.json").is_symlink():
        raise ValueError("snapshotにシンボリックリンクは使えません")
    doc = json.loads((root / "snapshot.json").read_text(encoding="utf-8"))
    if doc.get("schema") != SCHEMA or not isinstance(doc.get("files"), list):
        raise ValueError("snapshot形式が不正です")
    actual = artifacts(root / "files")
    expected = {}
    for entry in doc["files"]:
        name = entry["path"]
        # 保存された相対名だけを照合し、任意のパスを読み込まない。
        if name in expected or name not in actual:
            raise ValueError(f"snapshotの重複・欠落・不正path: {name}")
        data = actual[name]
        if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"snapshotが変更されています: {name}")
        expected[name] = data
    if not expected or set(actual) != set(expected):
        raise ValueError("snapshotの生成物一覧が一致しません")
    return doc["metadata"], expected


def compare(reference: Path, candidate: Path) -> list[str]:
    old_meta, old = load_snapshot(reference)
    new_meta, new = load_snapshot(candidate)
    errors = []
    if old_meta.get("recipe") != new_meta.get("recipe") or old_meta.get("cases") != new_meta.get("cases"):
        errors.append("smokeの手順・入力fixtureが変わっています")
    for name in sorted(old.keys() | new.keys()):
        if name not in old:
            errors.append(f"追加: {name}")
        elif name not in new:
            errors.append(f"欠落: {name}")
        elif old[name] != new[name]:
            offset = next((i for i, (a, b) in enumerate(zip(old[name], new[name])) if a != b), min(len(old[name]), len(new[name])))
            errors.append(f"不一致: {name} (先頭差byte={offset}, size={len(old[name])}/{len(new[name])})")
    return errors


def build_recipe(source: Path, scripts: list[Path]) -> dict[str, str]:
    # CMakeのビルドsource列挙は分割で変わるため固定しない。rawコマンドはdriver自体で固定する。
    recipe_files = list(scripts)
    for folder in ("Test/Core/Asset/Fixtures/AssetCook", "Assets/Models/Rendering3DTestSilverGltf", "Assets/Models/M9Skinned"):
        recipe_files.extend(p for p in (source / folder).rglob("*") if p.is_file())
    recipe = {p.relative_to(source).as_posix(): digest(p) for p in sorted(set(recipe_files))}
    recipe["driver"] = digest(Path(__file__))
    return recipe


def capture(args: argparse.Namespace) -> None:
    source = args.source.resolve(strict=True)
    exe = args.exe.resolve(strict=True)
    unit = args.skeletal_test_exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        raise ValueError("既存出力を上書きしません。新しい --output を指定してください")
    if source == output or source in output.parents:
        raise ValueError("入力保護のため出力先はリポジトリ外に指定してください")
    scripts = [source / "Tools/AssetCook" / f"AssetCook{x}Smoke.cmake.in" for x in SMOKES]
    skeletal = source / "Test/Core/Asset/CookedSkeletalAssetTest.cmake.in"
    scripts.append(skeletal)
    recipe = build_recipe(source, scripts)
    metadata = {"recipe": recipe, "asset_cook_sha256": digest(exe), "skeletal_test_sha256": digest(unit), "cases": ["Raw", *SMOKES, "Skeletal"]}
    output.mkdir(parents=True)
    run_root = output / "run"
    run_root.mkdir()
    logs = output / "logs"
    logs.mkdir()
    commands = []
    raw = run_root / "Raw"
    raw.mkdir()
    commands.append(("Raw", [str(exe), "--input", str(source / "Test/Core/Asset/Fixtures/AssetCook/raw_sample.bin"), "--out", str(raw / "Cooked/raw_sample.nvpkg"), "--manifest", str(raw / "manifest.json"), "--logical", "Raw/raw_sample.bin", "--kind", "raw", "--entry", "__raw__", "--entry-type", "Raw", "--format", "raw.v0", "--variant", "default"]))
    for case, script in zip(SMOKES, scripts):
        commands.append((case, [args.cmake, f"-DASSET_COOK_EXE={exe}", f"-DSOURCE_ROOT={source}", f"-DSMOKE_DIR={run_root / case}", "-P", str(script)]))
    commands.append(("Skeletal", [args.cmake, f"-DASSET_COOK_EXE={exe}", f"-DUNIT_TEST_EXE={unit}", f"-DSOURCE_ROOT={source}", f"-DFIXTURE_GLTF={source / 'Assets/Models/M9Skinned/ValidU8Float.gltf'}", f"-DSMOKE_DIR={run_root / 'Skeletal'}", "-P", str(skeletal)]))
    for case, command in commands:
        with (logs / f"{case}.log").open("wb") as log:
            environment = dict(os.environ)
            if case == "Skeletal":
                environment["NORVES_TEST_BUNDLE_MEMBER"] = "CookedSkeletalAssetTest"
            result = subprocess.run(command, cwd=source, env=environment, stdout=log, stderr=subprocess.STDOUT, check=False)
        if result.returncode != 0:
            raise ValueError(f"{case} smoke失敗(exit={result.returncode})。{logs / (case + '.log')} を確認してください")
        values = artifacts(run_root / case)
        if not any(p.endswith(".nvpkg") for p in values) or not any(p.endswith(".json") for p in values):
            raise ValueError(f"{case}: package/manifestが生成されていません")
    if recipe != build_recipe(source, scripts):
        raise ValueError("smoke実行中に手順または入力fixtureが変更されました")
    write_snapshot(run_root, output / "snapshot", metadata)
    print(f"smoke全7件成功、snapshot保存: {output / 'snapshot'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("capture")
    run.add_argument("--source", type=Path, required=True)
    run.add_argument("--exe", type=Path, required=True)
    run.add_argument("--skeletal-test-exe", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--cmake", default="cmake")
    diff = commands.add_parser("compare")
    diff.add_argument("reference", type=Path)
    diff.add_argument("candidate", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "capture":
            capture(args)
        else:
            errors = compare(args.reference, args.candidate)
            if errors:
                print("\n".join(errors), file=sys.stderr)
                return 1
            print("全package/manifestのバイト一致を確認しました")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"検証失敗: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
