#!/usr/bin/env python3
"""共有化後の実Samplerを、成功runから固定した構成別bit列と比較する。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from SkeletalSamplerBaseline import inspect_snapshot, require, digest

FROZEN_RECEIPT_SHA256 = "a6845e8822c75ec9596e9a406482158d601cc6e40e84b006070c96fb43daa369"
FROZEN_DIRECTORY = "Test/Core/Rendering/Fixtures/SamplerBaseline"


def read_frozen(source, configuration):
    directory = source / FROZEN_DIRECTORY
    receipt = directory / "receipt.json"
    require(digest(receipt) == FROZEN_RECEIPT_SHA256, "frozen receipt changed")
    data = json.loads(receipt.read_text(encoding="utf-8-sig"))
    require(data["schema"] == 1 and data["run_id"] == 37373318284 and data["attempt"] == 1,
            "wrong baseline identity")
    reference = data["configurations"][configuration]
    snapshot = (directory / (configuration + ".bin")).read_bytes()
    require(hashlib.sha256(snapshot).hexdigest() == reference["snapshot_sha256"], "frozen snapshot changed")
    require(len(snapshot) == reference["snapshot_bytes"], "frozen snapshot size changed")
    require(inspect_snapshot(snapshot) == reference["cases"], "frozen case receipt mismatch")
    return reference, snapshot


def compare_snapshot(reference, snapshot, first, second):
    require(first == second, "same-configuration repeat differs")
    require(inspect_snapshot(first) == reference["cases"], "observed bool or structure changed")
    require(first == snapshot, "sampler output differs from frozen bytes")


def project_options(path, configuration):
    tree = ET.fromstring(path.read_bytes())
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    # 受入済み基準の両projectはsource個別のcompile overrideを持たない。
    for item in tree.findall("m:ItemGroup/m:ClCompile", ns):
        require(len(item) == 0 and set(item.attrib) == {"Include"}, "source-specific compile override")
    options = []
    for group in tree.findall("m:ItemDefinitionGroup", ns):
        if "'" + configuration + "|x64'" in group.attrib.get("Condition", ""):
            cl = group.find("m:ClCompile", ns)
            require(cl is not None, "missing configuration compiler options")
            options.append({child.tag.rsplit("}", 1)[-1]: child.text for child in cl})
    require(len(options) == 1, "ambiguous configuration compiler options")
    sdk = tree.find(".//m:WindowsTargetPlatformVersion", ns)
    require(sdk is not None and sdk.text, "missing Windows SDK")
    return sdk.text, options[0]


def verify_build_binding(source, build, configuration, exe, core, cache):
    for key, expected in (("CMAKE_HOME_DIRECTORY", source), ("CMAKE_CACHEFILE_DIR", build.resolve())):
        match = re.search(r"^" + key + r":INTERNAL=(.+)$", cache, re.M)
        require(match is not None and Path(match.group(1)).resolve() == expected,
                "source/build cache binding differs: " + key)
    require(exe.resolve() == (build / "Test/Core/Rendering" / configuration /
                             "SkeletalAnimationSamplingTest.exe").resolve(), "sampler executable path differs")
    require(core.resolve() == (build / "Library/Core" / configuration / "Core.lib").resolve(),
            "Core library path differs")
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    expected_sources = (
        (build / "Library/Core/Core.vcxproj", [source / "Library/Core/Private/Animation/SkeletalAnimationSampler.cpp"], False),
        (build / "Test/Core/Rendering/SkeletalAnimationSamplingTest.vcxproj",
         [source / "Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp",
          source / "Test/Core/Rendering/SkeletalSamplerBaseline.cpp"], True),
    )
    for project, expected, exact in expected_sources:
        tree = ET.fromstring(project.read_bytes())
        includes = [(project.parent / item.attrib["Include"].replace("\\", "/")).resolve()
                    for item in tree.findall("m:ItemGroup/m:ClCompile", ns)]
        require(all(includes.count(path.resolve()) == 1 for path in expected) and
                (not exact or len(includes) == len(expected)), "project source binding differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("source", "build", "first", "second", "exe", "core", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    args = parser.parse_args()
    source = args.source.resolve()

    def git(*command):
        return subprocess.check_output(["git", "-C", str(source), *command]).decode("utf-8").strip()

    require(not git("status", "--porcelain", "--untracked-files=no"), "dirty tracked source")
    reference, snapshot = read_frozen(source, args.configuration)
    first = args.first.read_bytes()
    compare_snapshot(reference, snapshot, first, args.second.read_bytes())
    for name, sha in reference["harness"].items():
        require(digest(source / name) == sha, "frozen harness changed: " + name)
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    require(re.search(r"^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64$", cache, re.M) is not None, "not x64")
    configs = list(args.build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"))
    require(len(configs) == 1, "ambiguous compiler receipt")
    verify_build_binding(source, args.build, args.configuration, args.exe, args.core, cache)
    compiler_text = configs[0].read_text(encoding="utf-8-sig")
    compiler_match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', compiler_text)
    version_match = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', compiler_text)
    require(compiler_match is not None and version_match is not None, "missing actual compiler")
    compiler = Path(compiler_match.group(1))
    require(compiler.name.lower() == "cl.exe" and digest(compiler) == reference["compiler_sha256"],
            "compiler binary differs")
    require(version_match.group(1) == reference["compiler_version"], "compiler version differs")
    require(os.environ.get("SDK_VERSION") == reference["sdk_version"], "Vulkan SDK differs")
    paths = (args.build / "Library/Core/Core.vcxproj",
             args.build / "Test/Core/Rendering/SkeletalAnimationSamplingTest.vcxproj")
    projects = []
    for path, old in zip(paths, reference["projects"], strict=True):
        sdk, options = project_options(path, args.configuration)
        require(sdk == old["windows_sdk"] and options == old["compile_options"],
                "generated compiler options or Windows SDK differ")
        saved = args.output.parent / (args.configuration + "-" + path.name)
        saved.write_bytes(path.read_bytes())
        projects.append({"file": saved.name, "sha256": digest(saved), "windows_sdk": sdk,
                         "compile_options": options})
    receipt = {
        "schema": 1, "result": "pass", "purpose": "post_refactor_frozen_byte_comparison",
        "commit": git("rev-parse", "HEAD"), "tree": git("rev-parse", "HEAD^{tree}"),
        "configuration": args.configuration, "platform": "x64",
        "run_id": os.environ.get("GITHUB_RUN_ID"), "attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "frozen_receipt_sha256": FROZEN_RECEIPT_SHA256,
        "snapshot_sha256": digest(args.first), "snapshot_bytes": len(first),
        "repeat_sha256": digest(args.second), "cases": inspect_snapshot(first),
        "harness": reference["harness"], "compiler_version": version_match.group(1),
        "compiler_sha256": digest(compiler), "compiler_cmake": compiler_text,
        "sdk_version": os.environ.get("SDK_VERSION"), "image_version": os.environ.get("ImageVersion"),
        "projects": projects, "executable_sha256": digest(args.exe), "core_sha256": digest(args.core),
    }
    args.output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"SAMPLER_FROZEN_COMPARE result=pass configuration={args.configuration} cases=30 frozen_byte_exact=1")


if __name__ == "__main__":
    main()
