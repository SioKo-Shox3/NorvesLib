# Sampler基準の構造検査を、実Samplerを模倣しない合成byteだけで反証する。
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest import mock
import tempfile
import json

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("sampler_baseline", ROOT / "Scripts/SkeletalSamplerBaseline.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def synthetic():
    output = bytearray(struct.pack("<III", 0x4250534E, 1, 30))
    offsets = []
    for index, name in enumerate(module.NAMES, 1):
        record = {}
        offsets.append(record)
        record["id"] = len(output)
        output += struct.pack("<II", index, len(name)) + name.encode("ascii")
        success = index not in {12, 13, 14, 15, 16, 17, 27, 28, 29}
        record["success"] = len(output)
        output += struct.pack("<I", success)
        count = (2 if index in {10, 13, 17, 18, 19} else 1) if success else 0
        record["count"] = len(output)
        for _ in range(2):
            output += struct.pack("<I", count) + b"\0" * (count * 64)
        vertices = 0 if index in {11, 24, 25, 26} else 2
        record["vertices"] = len(output)
        output += struct.pack("<I", vertices) + b"\0" * (vertices * 24)
        record["bounds_flag"] = len(output)
        output += struct.pack("<I", bool(success and vertices)) + b"\0" * 24
    output += struct.pack("<I", 0x454E4442)
    return output, offsets


class SnapshotTest(unittest.TestCase):
    def test_valid_structure(self):
        data, _ = synthetic()
        self.assertEqual(len(module.inspect_snapshot(data)), 30)

    def test_truncation(self):
        data, _ = synthetic()
        for cut in (0, 1, 11, 12, len(data) - 5, len(data) - 1):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                module.inspect_snapshot(data[:cut])

    def test_header_and_trailer(self):
        original, _ = synthetic()
        for offset in (0, 4, 8, len(original) - 4):
            data = original.copy()
            struct.pack_into("<I", data, offset, 0)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                module.inspect_snapshot(data)

    def test_trailing_bytes(self):
        data, _ = synthetic()
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data + b"x")

    def test_case_identity(self):
        original, offsets = synthetic()
        for offset in (offsets[1]["id"], offsets[1]["id"] + 8):
            data = original.copy()
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                module.inspect_snapshot(data)

    def test_invalid_bool(self):
        data, offsets = synthetic()
        struct.pack_into("<I", data, offsets[21]["success"], 2)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)

    def test_known_result(self):
        data, offsets = synthetic()
        struct.pack_into("<I", data, offsets[0]["success"], 0)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)

    def test_matrix_count(self):
        data, offsets = synthetic()
        struct.pack_into("<I", data, offsets[0]["count"], 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)

    def test_nonfinite_output(self):
        original, offsets = synthetic()
        for value in (float("inf"), float("nan")):
            data = original.copy()
            struct.pack_into("<f", data, offsets[0]["count"] + 4, value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                module.inspect_snapshot(data)

    def test_vertex_count(self):
        data, offsets = synthetic()
        struct.pack_into("<I", data, offsets[0]["vertices"], 0)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)

    def test_bounds_flag(self):
        data, offsets = synthetic()
        struct.pack_into("<I", data, offsets[10]["bounds_flag"], 1)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)

    def test_failed_clear(self):
        data, offsets = synthetic()
        struct.pack_into("<f", data, offsets[11]["bounds_flag"] + 4, 7)
        with self.assertRaises(ValueError):
            module.inspect_snapshot(data)



class ReceiptTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.output = self.root / "receipt.json"
        self.compiler = self.root / "cl.exe"
        self.compiler.write_bytes(b"synthetic metadata fixture, not an executable")
        self.compiler_config = self.build / "CMakeFiles/4.0/CMakeCXXCompiler.cmake"
        self.compiler_config.parent.mkdir(parents=True)
        self.compiler_config.write_text(
            f'set(CMAKE_CXX_COMPILER "{self.compiler.as_posix()}")\n'
            'set(CMAKE_CXX_COMPILER_VERSION "19.44.1")\n', encoding="utf-8")
        (self.build / "CMakeCache.txt").write_text("CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n")
        self.projects = []
        for name in ("Library/Core/Core.vcxproj", "Test/Core/Rendering/SkeletalAnimationSamplingTest.vcxproj"):
            project = self.build / name
            project.parent.mkdir(parents=True, exist_ok=True)
            project.write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><WindowsTargetPlatformVersion>10.0.1</WindowsTargetPlatformVersion></PropertyGroup>
<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
<ClCompile><FloatingPointModel>Precise</FloatingPointModel><Optimization>Disabled</Optimization></ClCompile>
</ItemDefinitionGroup></Project>''', encoding="utf-8")
            self.projects.append(project)
        for name in ("Test/Core/Rendering/SkeletalSamplerBaseline.cpp",
                     "Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp",
                     "Test/Core/Rendering/CMakeLists.txt", "Scripts/SkeletalSamplerBaseline.py",
                     "Library/Core/Private/Animation/SkeletalAnimationSampler.cpp",
                     "Library/Core/Private/Animation/SkeletalSamplingMath.h",
                     "Library/Core/Public/Animation/SkeletalAnimationSampler.h"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic source receipt")
        self.first = self.root / "first.bin"
        self.second = self.root / "second.bin"
        self.first.write_bytes(synthetic()[0])
        self.second.write_bytes(synthetic()[0])
        self.exe = self.root / "test.exe"
        self.core = self.root / "Core.lib"
        self.exe.write_bytes(b"synthetic executable hash input")
        self.core.write_bytes(b"synthetic library hash input")
        self.argv = ["baseline", "--source", str(self.root), "--build", str(self.build),
                     "--first", str(self.first), "--second", str(self.second),
                     "--exe", str(self.exe), "--core", str(self.core), "--output", str(self.output),
                     "--configuration", "Debug"]
        self.dirty = False
        self.tree = module.BASE_LIBRARY_TREE

    def git(self, command):
        args = command[3:]
        if args[0] == "status":
            return b" M source" if self.dirty else b""
        if args == ["rev-parse", "HEAD:Library"]:
            return self.tree.encode()
        return b"synthetic"

    def run_receipt(self):
        with mock.patch("sys.argv", self.argv), mock.patch.object(module.subprocess, "check_output", self.git):
            module.main()

    def test_receipt_records_configuration_options(self):
        self.run_receipt()
        receipt = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(receipt["configuration"], "Debug")
        self.assertEqual(receipt["projects"][0]["compile_options"]["FloatingPointModel"], "Precise")
        self.assertEqual(receipt["snapshot_sha256"], receipt["repeat_sha256"])

    def test_dirty_source_rejected(self):
        self.dirty = True
        with self.assertRaisesRegex(ValueError, "dirty"):
            self.run_receipt()

    def test_changed_runtime_rejected(self):
        self.tree = "0" * 40
        with self.assertRaisesRegex(ValueError, "runtime changed"):
            self.run_receipt()

    def test_repeat_difference_rejected(self):
        self.second.write_bytes(self.first.read_bytes() + b"x")
        with self.assertRaisesRegex(ValueError, "repeat differs"):
            self.run_receipt()

    def test_missing_compiler_version_rejected(self):
        text = self.compiler_config.read_text()
        self.compiler_config.write_text(text.splitlines()[0])
        with self.assertRaisesRegex(ValueError, "compiler version"):
            self.run_receipt()

    def test_missing_configuration_options_rejected(self):
        text = self.projects[0].read_text().replace("Debug|x64", "Release|x64")
        self.projects[0].write_text(text)
        with self.assertRaisesRegex(ValueError, "configuration compile options"):
            self.run_receipt()

    def test_wrong_platform_rejected(self):
        (self.build / "CMakeCache.txt").write_text("CMAKE_GENERATOR_PLATFORM:INTERNAL=Win32\n")
        with self.assertRaisesRegex(ValueError, "not x64"):
            self.run_receipt()


if __name__ == "__main__":
    unittest.main()
