"""固定した旧texture wrapperを実Windowsで動かし、正規化しない比較基準を保存する。"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

CASES = ("Rendering3DTestSilverTextures", "Rendering3DTestSilverGltfTextures")
WRAPPER = '''param([string]$Script, [string]$Exe, [string]$Spec, [string]$Runtime, [string]$Metadata)
$ErrorActionPreference = "Stop"
$info = [ordered]@{ version = $PSVersionTable.PSVersion.ToString(); edition = $PSVersionTable.PSEdition; executable = (Get-Process -Id $PID).Path }
[System.IO.File]::WriteAllText($Metadata, ($info | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
if ($PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1) { throw "Windows PowerShell 5.1 required" }
& $Script -AssetCookExe $Exe -SpecPath $Spec -RuntimeRoot $Runtime
if (!$?) { throw "Legacy asset-set capture failed" }
'''


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def child_environment(parent: dict[str, str]) -> dict[str, str]:
    # 親や他の変数は変えず、子Windows PowerShellに標準module探索を任せる。
    return {k: v for k, v in parent.items() if k.casefold() != "psmodulepath"}


# この採取器の対象は固定2spec。recipeから比較範囲を勝手に縮められないよう集合も固定する。
REQUIRED_INPUTS = {
    "Scripts/CookTextureAssetSet.ps1",
    "Assets/AssetSets/Rendering3DTestSilverTextures.json",
    "Assets/AssetSets/Rendering3DTestSilverGltfTextures.json",
    *("Assets/Textures/Silver/" + name + ".png" for name in ("silver_albedo", "silver_normal-ogl", "silver_metallic", "silver_roughness", "silver_ao")),
    *("Assets/Models/Rendering3DTestSilverGltf/textures/" + name + ".png" for name in ("silver_albedo", "silver_normal-ogl", "silver_arm")),
}


def validate_recipe(recipe: dict) -> None:
    if not isinstance(recipe, dict) or not isinstance(recipe.get("inputs"), dict) or set(recipe["inputs"]) != REQUIRED_INPUTS:
        raise ValueError("recipeは固定11入力を過不足なく指定する必要があります")
    hashes = [(recipe.get("upstream_sha"), 40), (recipe.get("asset_cook_sha256"), 64)]
    hashes.extend((value, 40) for value in recipe["inputs"].values())
    if any(not isinstance(value, str) or re.fullmatch("[0-9a-f]{" + str(size) + "}", value) is None for value, size in hashes):
        raise ValueError("recipe hashの型または形式が不正です")


def verify_recipe(source: Path, recipe: dict) -> dict:
    validate_recipe(recipe)
    def git(*args):
        return subprocess.check_output(["git", "-C", str(source), *args])
    if git("rev-parse", "HEAD").decode().strip() != recipe["upstream_sha"]:
        raise ValueError("上流commitが固定値と一致しません")
    result = {}
    for relative, expected in recipe["inputs"].items():
        if git("rev-parse", "HEAD:" + relative).decode().strip() != expected:
            raise ValueError("上流blobが一致しません: " + relative)
        original = git("cat-file", "blob", expected)
        path = source / relative
        if path.is_symlink() or not path.is_file():
            raise ValueError("入力fileが不正です: " + relative)
        actual = path.read_bytes()
        # テキストのWindows checkoutだけを許す。画像や出力の正規化はしない。
        candidates = [original]
        if path.suffix in (".json", ".ps1") and b"\r\n" not in original:
            candidates.append(original.replace(b"\n", b"\r\n"))
        if actual not in candidates:
            raise ValueError("checkout入力が変わっています: " + relative)
        result[relative] = {"git_blob": expected, "size": len(actual), "sha256": sha(actual)}
    return result


def inventory(root: Path, spec: dict) -> dict[str, bytes]:
    expected = {"manifest.json"} | {spec["package_root"] + "/" + x["package_name"] for x in spec["textures"]}
    actual = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError("出力のsymlinkは比較できません")
        if path.is_file():
            actual[path.relative_to(root).as_posix()] = path.read_bytes()
    if actual.keys() != expected:
        raise ValueError("package/manifest一覧が一致しません")
    manifest = actual["manifest.json"]
    if manifest.startswith(b"\xef\xbb\xbf"):
        raise ValueError("旧manifestに想定外のBOMがあります")
    doc = json.loads(manifest.decode("utf-8"))
    if type(doc.get("version")) is not int or doc["version"] != 1 or len(doc.get("assets", [])) != len(spec["textures"]):
        raise ValueError("manifestのversion/asset数が一致しません")
    for row, item in zip(doc["assets"], spec["textures"]):
        logical = item["logical_path"].removeprefix("Assets/")
        if row.get("logical_path") != logical or row.get("cooked_package") != spec["package_root"] + "/" + item["package_name"]:
            raise ValueError("manifestの並び/参照が一致しません")
    return actual


def require_equal(before: dict[str, bytes], after: dict[str, bytes]) -> None:
    if before.keys() != after.keys() or any(before[p] != after[p] for p in before):
        raise ValueError("2回のlegacy cook出力が全byte一致しません")


def capture(source: Path, exe: Path, output: Path, recipe_path: Path) -> None:
    if os.name != "nt":
        raise ValueError("実Windows環境が必要です")
    recipe = json.loads(recipe_path.read_text(encoding="utf-8"))
    inputs = verify_recipe(source, recipe)
    if sha(exe.read_bytes()) != recipe["asset_cook_sha256"]:
        raise ValueError("固定AssetCook.exeのhashが一致しません")
    powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    ps_hash = sha(powershell.read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    logs = output / "logs"
    logs.mkdir()
    wrapper = output / "invoke.ps1"
    wrapper.write_text(WRAPPER, encoding="ascii")
    payloads = {}
    invocations = []
    for case in CASES:
        spec_path = source / "Assets/AssetSets" / (case + ".json")
        spec = json.loads(spec_path.read_text(encoding="utf-8-sig"))
        copies = []
        for repeat in (1, 2):
            runtime = output / ("run" + str(repeat)) / case
            runtime.parent.mkdir(exist_ok=True)
            metadata = logs / f"{case}-{repeat}.host.json"
            args = [str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(wrapper), "-Script", str(source / "Scripts/CookTextureAssetSet.ps1"), "-Exe", str(exe), "-Spec", str(spec_path), "-Runtime", str(runtime), "-Metadata", str(metadata)]
            result = subprocess.run(args, cwd=source, env=child_environment(dict(os.environ)), capture_output=True)
            (logs / f"{case}-{repeat}.stdout.log").write_bytes(result.stdout)
            (logs / f"{case}-{repeat}.stderr.log").write_bytes(result.stderr)
            if result.returncode != 0:
                raise ValueError(f"legacy cook失敗: {case}/{repeat} exit={result.returncode}")
            host = json.loads(metadata.read_text(encoding="utf-8-sig"))
            if not host["version"].startswith("5.1.") or Path(host["executable"]).resolve() != powershell.resolve():
                raise ValueError("実PowerShell hostが一致しません")
            copies.append(inventory(runtime, spec))
            invocations.append({"case": case, "repeat": repeat, "arguments": args, "exit_code": result.returncode, "host": host})
        require_equal(*copies)
        for name, data in copies[0].items():
            payloads[case + "/" + name] = data
    if len(payloads) != 10 or verify_recipe(source, recipe) != inputs or sha(exe.read_bytes()) != recipe["asset_cook_sha256"] or sha(powershell.read_bytes()) != ps_hash:
        raise ValueError("採取中に入力または実行fileが変わりました")
    rows = []
    for name, data in sorted(payloads.items()):
        dest = output / "snapshot/files" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        rows.append({"path": name, "size": len(data), "sha256": sha(data)})
    receipt = {"schema": "norves.texture-legacy-baseline.v1", "recipe": recipe, "checkout_inputs": inputs, "powershell_sha256": ps_hash, "wrapper_sha256": sha(wrapper.read_bytes()), "driver_sha256": sha(Path(__file__).read_bytes()), "runner_image": os.environ.get("ImageVersion"), "feature_sha": os.environ.get("GITHUB_SHA"), "invocations": invocations, "files": rows, "repeat_byte_equal": True}
    (output / "snapshot/snapshot.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("固定旧PS1の2spec/10fileを採取し、独立2回の全byte一致を確認しました")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "exe", "output", "recipe"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    capture(args.source.resolve(), args.exe.resolve(), args.output.resolve(), args.recipe.resolve())
