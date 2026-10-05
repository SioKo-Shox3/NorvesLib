"""実CLIの反復・更新・入力表記を既存25呼出しとは別群で検査する。"""
from __future__ import annotations
import json
from pathlib import Path
import runpy
import subprocess
M = runpy.run_path(str(Path(__file__).with_name("TextureManagedEvidence.py")))


def run(exe: Path, output: Path, logs: Path) -> dict:
    workspace = output / "managed-cli"
    workspace.mkdir()
    source = workspace / "source"
    source.mkdir()
    image = b"P6\n2 2\n255\nabcdefghijkl"
    (source / "a.ppm").write_bytes(image)
    (source / "b.ppm").write_bytes(image)
    spec = {"version": 1, "name": "managed-cli", "package_root": "Cooked/Probe", "default_variant": "default", "textures": [
        {"logical_path": "Textures/a.ppm", "source_path": "a.ppm", "format": "nvtex.v0.rgba8.linear", "package_name": "a.nvpkg", "entry_name": "a.nvtex"},
        {"logical_path": "Textures/b.ppm", "source_path": "b.ppm", "format": "nvtex.v0.rgba8.linear", "package_name": "b.nvpkg", "entry_name": "b.nvtex"}]}
    spec_path = workspace / "set.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    runtime = workspace / "Runtime"
    store = workspace / ".norves-assetcook"
    base = ["--asset-set", str(spec_path), "--runtime-root", str(runtime), "--source-root", str(source)]
    runs = []
    snapshots = {}
    def invoke(label, args, success, status=None, cwd=workspace):
        result = subprocess.run([str(exe), *args], cwd=cwd, capture_output=True)
        (logs / (label + ".stdout.log")).write_bytes(result.stdout)
        (logs / (label + ".stderr.log")).write_bytes(result.stderr)
        M["require"]((result.returncode == 0) == success, "managed CLI exit: " + label)
        text = result.stderr.decode("utf-8")
        if status:
            M["require"](status in text, "managed CLI status: " + label)
        runs.append({"label": label, "arguments": args, "cwd": str(cwd), "exit_code": result.returncode,
                     "stderr_sha256": M["digest"](result.stderr), "expected_status": status})
    def saved():
        return {"runtime": M["snapshot"](runtime), "store": M["snapshot"](store)}
    def unchanged(before):
        M["require"](before == saved(), "NoChange/refusal changed native IDs, bytes, or write times")
    invoke("managed-create", base, True, "texture_asset_set published")
    (runtime / "unlisted.txt").write_bytes(b"unlisted")
    initial = saved()
    snapshots["initial"] = initial
    invoke("managed-nochange", base, True, "texture_asset_set unchanged")
    unchanged(initial)
    # directory表記の末尾dot/separatorと、cwd相対のsourceを保つ。
    for label, path, cwd in (("managed-source-dot", ".", source), ("managed-source-dot-tail", str(source) + "/.", workspace),
                             ("managed-source-slash", str(source) + "/", workspace)):
        invoke(label, base[:-1] + [path], True, "texture_asset_set unchanged", cwd)
        unchanged(initial)
    (source / "a.ppm").write_bytes(image[:-1] + b"Z")
    invoke("managed-update", base, True, "texture_asset_set updated")
    updated = saved()
    snapshots["updated"] = updated
    for name in (".", "Cooked", "Cooked/Probe", "Cooked/Probe/b.nvpkg", "unlisted.txt"):
        # directory write-timeは子file renameで変化する。root/親のIDとfile全証拠を分ける。
        a, b = initial["runtime"][name], updated["runtime"][name]
        if a["directory"]:
            M["require"](a["file_id"] == b["file_id"] and a["volume"] == b["volume"], "root/parent ID changed")
        else:
            M["require"](a == b, "Skip/unlisted changed")
    M["require"](initial["runtime"]["Cooked/Probe/a.nvpkg"]["file_id"] != updated["runtime"]["Cooked/Probe/a.nvpkg"]["file_id"] and
                  initial["runtime"]["Cooked/Probe/a.nvpkg"]["sha256"] != updated["runtime"]["Cooked/Probe/a.nvpkg"]["sha256"], "changed package did not update")
    invoke("managed-update-nochange", base, True, "texture_asset_set unchanged")
    unchanged(updated)
    original = spec_path.read_bytes()
    changed = json.loads(original)
    changed["textures"][0]["package_name"] = "renamed.nvpkg"
    spec_path.write_text(json.dumps(changed), encoding="utf-8")
    invoke("managed-inventory-refusal", base, False)
    unchanged(updated)
    spec_path.write_bytes(original)
    alias_spec = workspace / "other.json"
    alias_spec.write_bytes(original)
    invoke("managed-owner-refusal", ["--asset-set", str(alias_spec), *base[2:]], False)
    unchanged(updated)
    # source/specが無くても明示復旧のNoPendingは成立し、新cookしない。
    spec_path.rename(workspace / "set.held")
    source.rename(workspace / "source-held")
    invoke("managed-recover-no-pending", ["--recover", "--runtime-root", str(runtime)], True, "workspace_recovery no_pending")
    unchanged(updated)
    for label, args in (
        ("managed-recover-missing-root", ["--recover"]),
        ("managed-recover-valued", ["--recover=yes", "--runtime-root", str(runtime)]),
        ("managed-recover-duplicate", ["--recover", "--recover", "--runtime-root", str(runtime)]),
        ("managed-recover-mixed", ["--recover", "--runtime-root", str(runtime), "--asset-set", str(spec_path)]),
        ("managed-recover-source", ["--recover", "--runtime-root", str(runtime), "--source-root", str(source)]),
    ):
        invoke(label, args, False)
        unchanged(updated)
    result = {"schema": "norves.managed-cli-cases.v1", "driver_sha256": M["digest"](Path(__file__).read_bytes()), "runs": runs, "snapshots": snapshots,
              "checked": ["native_nochange", "source_directory_spellings", "one_changed_one_skip", "root_unlisted_preserved",
                          "inventory_refusal", "owner_refusal", "source_absent_no_pending", "recovery_argument_refusals"]}
    target = output / "managed/cli-receipt.json"
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
