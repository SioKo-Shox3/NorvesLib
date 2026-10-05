#!/usr/bin/env python3
"""共有化前のSampler出力を検査し、実行環境と旧sourceの出自を保存する。"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET

BASE = "585d7004505af6cbaf917b2e466b9061aee4853c"
BASE_LIBRARY_TREE = "59e5b593a248f555928a7f13e9939192fd263ce7"
NAMES = (
    "identity_bind", "scaled_rotated_bind", "partial_translation", "linear_before",
    "linear_middle", "linear_after", "step_before", "step_key", "step_after",
    "noncommutative_hierarchy", "empty_mesh", "nan_time", "parent_cycle",
    "duplicate_key_time", "singular_ibm", "singular_mesh", "fk_overflow",
    "parent_after_child", "multiple_roots", "empty_and_out_of_range_channels",
    "duplicate_channels", "sheared_bind", "reflected_bind", "extreme_bind",
    "extreme_bind_with_overrides", "extreme_ibm_and_clip", "nonfinite_ibm",
    "nonfinite_mesh", "empty_skeleton", "success_after_failures",
)
FAILURES = {12, 13, 14, 15, 16, 17, 27, 28, 29}
OBSERVATIONS = {22, 23, 24, 25, 26}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect_snapshot(data):
    """件数だけでなく固定case順・配列幅・Clear・有限性・末尾を照合する。"""
    offset = 0

    def take(size):
        nonlocal offset
        require(0 <= size <= len(data) - offset, "truncated snapshot")
        result = data[offset:offset + size]
        offset += size
        return result

    def u32():
        return struct.unpack("<I", take(4))[0]

    def floats(count):
        values = struct.unpack("<" + "f" * count, take(4 * count))
        require(all(math.isfinite(v) for v in values), "nonfinite result")
        return values

    require((u32(), u32(), u32()) == (0x4250534E, 1, len(NAMES)), "header/count mismatch")
    cases = []
    for index, name in enumerate(NAMES, 1):
        require(u32() == index, "case order mismatch")
        require(take(u32()) == name.encode("ascii"), "case name mismatch")
        success = u32()
        require(success in (0, 1), "invalid bool")
        if index not in OBSERVATIONS:
            require(success == (0 if index in FAILURES else 1), "known result changed")
        expected_count = 2 if index in {10, 13, 17, 18, 19} else 1
        if not success:
            expected_count = 0
        for _ in range(2):
            count = u32()
            require(count == expected_count, "matrix count mismatch")
            floats(count * 16)
        vertex_count = u32()
        require(vertex_count == (0 if index in {11, 24, 25, 26} else 2), "vertex count mismatch")
        floats(vertex_count * 6)
        has_bounds = u32()
        require(has_bounds == int(bool(success and vertex_count)), "bounds flag mismatch")
        bounds = floats(6)
        if not has_bounds:
            require(all(v == 0 for v in bounds), "Clear bounds mismatch")
        cases.append({"id": index, "name": name, "success": bool(success)})
    require(u32() == 0x454E4442 and offset == len(data), "missing trailer/trailing bytes")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("source", "build", "first", "second", "exe", "core", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    args = parser.parse_args()
    source = args.source.resolve()

    def git(*command):
        return subprocess.check_output(["git", "-C", str(source), *command]).decode("utf-8").strip()

    # 基準取り専用。refactor後の比較ではこの旧source一致を外すのではなく、別の比較手順を用いる。
    require(not git("status", "--porcelain", "--untracked-files=no"), "dirty tracked source")
    library_tree = git("rev-parse", "HEAD:Library")
    require(library_tree == BASE_LIBRARY_TREE, "runtime changed before baseline")
    first = args.first.read_bytes()
    require(first == args.second.read_bytes(), "same-configuration repeat differs")
    cases = inspect_snapshot(first)
    cache_text = (args.build / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    require(re.search(r"^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64$", cache_text, re.M) is not None,
            "not x64 generator")
    compiler_files = list(args.build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    require(len(compiler_files) == 1, "ambiguous CXX compiler receipt")
    compiler_text = compiler_files[0].read_text(encoding="utf-8-sig")
    compiler_match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', compiler_text)
    require(compiler_match is not None, "missing actual compiler")
    compiler = Path(compiler_match.group(1))
    require(compiler.is_file() and compiler.name.lower() == "cl.exe", "not actual MSVC")
    compiler_version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', compiler_text)
    require(compiler_version is not None, "missing compiler version")
    configuration = args.configuration
    projects = (args.build / "Library/Core/Core.vcxproj",
                args.build / "Test/Core/Rendering/SkeletalAnimationSamplingTest.vcxproj")
    project_receipts = []
    for project in projects:
        require(project.is_file(), "missing actual compiler options")
        # 生のprojectはinclude pathも含めて保存し、比較時には構成別FP/最適化/CRT optionを照合する。
        saved = args.output.parent / (configuration + "-" + project.name)
        saved.write_bytes(project.read_bytes())
        tree = ET.fromstring(project.read_bytes())
        ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
        options = []
        for group in tree.findall("m:ItemDefinitionGroup", ns):
            condition = group.attrib.get("Condition", "")
            if "'" + configuration + "|x64'" in condition:
                cl = group.find("m:ClCompile", ns)
                require(cl is not None, "missing configuration compile options")
                options.append({child.tag.rsplit("}", 1)[-1]: child.text for child in cl})
        require(len(options) == 1, "ambiguous configuration compile options")
        sdk = tree.find(".//m:WindowsTargetPlatformVersion", ns)
        require(sdk is not None and sdk.text, "missing Windows SDK version")
        project_receipts.append({"file": saved.name, "sha256": digest(saved),
                                 "windows_sdk": sdk.text, "compile_options": options[0]})
    harness_paths = ("Test/Core/Rendering/SkeletalSamplerBaseline.cpp",
                     "Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp",
                     "Test/Core/Rendering/CMakeLists.txt", "Scripts/SkeletalSamplerBaseline.py")
    runtime_paths = ("Library/Core/Private/Animation/SkeletalAnimationSampler.cpp",
                     "Library/Core/Private/Animation/SkeletalSamplingMath.h",
                     "Library/Core/Public/Animation/SkeletalAnimationSampler.h")
    receipt = {
        "schema": 1, "result": "pass", "purpose": "pre_refactor_characterization_only",
        "commit": git("rev-parse", "HEAD"), "tree": git("rev-parse", "HEAD^{tree}"),
        "runtime_base": BASE, "unchanged_library_tree": library_tree,
        "configuration": configuration, "platform": "x64",
        "run_id": os.environ.get("GITHUB_RUN_ID"), "attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "image_version": os.environ.get("ImageVersion"), "os": os.environ.get("RUNNER_OS"),
        "sdk_version": os.environ.get("SDK_VERSION"),
        "compiler_version": compiler_version.group(1), "compiler_sha256": digest(compiler),
        "compiler_cmake": compiler_text, "cmake_cache_sha256": digest(args.build / "CMakeCache.txt"),
        "projects": project_receipts, "submodules": git("submodule", "status", "--recursive"),
        "harness": {p: digest(source / p) for p in harness_paths},
        "runtime": {p: digest(source / p) for p in runtime_paths},
        "executable_sha256": digest(args.exe), "core_sha256": digest(args.core),
        "snapshot_sha256": digest(args.first), "snapshot_bytes": len(first),
        "repeat_sha256": digest(args.second), "cases": cases,
    }
    args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"SAMPLER_BASELINE_RECEIPT result=pass configuration={configuration} cases={len(cases)} repeat_exact=1")


if __name__ == "__main__":
    main()
