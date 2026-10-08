# 実基準byteの改変拒否と、構成別比較の契約を反証する。
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
import SkeletalSamplerCompare as compare
import SkeletalSamplerBaseline as baseline
spec = importlib.util.spec_from_file_location("baseline_tests", Path(__file__).with_name("SkeletalSamplerBaselineTest.py"))
base_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base_tests)


class SnapshotCompareTest(unittest.TestCase):
    def test_both_configurations_exact(self):
        for configuration in ("Debug", "Release"):
            reference, snapshot = compare.read_frozen(ROOT, configuration)
            compare.compare_snapshot(reference, snapshot, snapshot, snapshot)
            self.assertTrue(reference["cases"][24]["success"])
            self.assertFalse(reference["cases"][23]["success"])

    def test_finite_bit_change_rejected(self):
        reference, snapshot = compare.read_frozen(ROOT, "Debug")
        changed = bytearray(snapshot)
        changed[-8] ^= 1
        self.assertEqual(baseline.inspect_snapshot(changed), reference["cases"])
        with self.assertRaisesRegex(ValueError, "frozen bytes"):
            compare.compare_snapshot(reference, snapshot, changed, changed)

    def test_repeat_change_rejected(self):
        reference, snapshot = compare.read_frozen(ROOT, "Debug")
        with self.assertRaisesRegex(ValueError, "repeat differs"):
            compare.compare_snapshot(reference, snapshot, snapshot, snapshot + b"x")

    def test_truncation_and_trailer_rejected(self):
        reference, snapshot = compare.read_frozen(ROOT, "Debug")
        for data in (snapshot[:-1], snapshot + b"x"):
            with self.assertRaises(ValueError):
                compare.compare_snapshot(reference, snapshot, data, data)

    def test_frozen_receipt_hash_rejected(self):
        with mock.patch.object(compare, "FROZEN_RECEIPT_SHA256", "0" * 64):
            with self.assertRaisesRegex(ValueError, "receipt changed"):
                compare.read_frozen(ROOT, "Debug")

    def test_recorded_case_results_rejected(self):
        reference, snapshot = compare.read_frozen(ROOT, "Debug")
        reference["cases"][24]["success"] = False
        with self.assertRaisesRegex(ValueError, "observed bool"):
            compare.compare_snapshot(reference, snapshot, snapshot, snapshot)


class ComparisonReceiptTest(unittest.TestCase):
    def setUp(self):
        # これはsynthetic metadata fixtureでありcl.exeを実行しない。
        self.fixture = base_tests.ReceiptTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        with mock.patch("sys.argv", self.fixture.argv), mock.patch.object(baseline.subprocess, "check_output", self.fixture.git):
            baseline.main()
        self.reference = json.loads(self.fixture.output.read_text(encoding="utf-8"))
        self.snapshot = self.fixture.first.read_bytes()
        self.reference["sdk_version"] = "fixture-sdk"
        f = self.fixture
        cache = f.build / "CMakeCache.txt"
        cache.write_text(cache.read_text() + f"CMAKE_HOME_DIRECTORY:INTERNAL={f.root}\n"
                         + f"CMAKE_CACHEFILE_DIR:INTERNAL={f.build}\n")
        paths = (f.build / "Test/Core/Rendering/Debug/SkeletalAnimationSamplingTest.exe",
                 f.build / "Library/Core/Debug/Core.lib")
        for old, new in zip((f.exe, f.core), paths):
            new.parent.mkdir(parents=True, exist_ok=True)
            new.write_bytes(old.read_bytes())
            f.argv[f.argv.index(str(old))] = str(new)
        f.exe, f.core = paths
        sources = (("Library/Core/Private/Animation/SkeletalAnimationSampler.cpp",),
                   ("Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp",
                    "Test/Core/Rendering/SkeletalSamplerBaseline.cpp"))
        for project, names in zip(f.projects, sources):
            entries = ''.join(f'<ClCompile Include="{(f.root / name).as_posix()}" />' for name in names)
            project.write_text(project.read_text().replace("</Project>", "<ItemGroup>" + entries + "</ItemGroup></Project>"))


    def run_compare(self):
        with mock.patch("sys.argv", self.fixture.argv), mock.patch.object(compare.subprocess, "check_output", self.fixture.git), \
             mock.patch.object(compare, "read_frozen", return_value=(self.reference, self.snapshot)), \
             mock.patch.dict(compare.os.environ, {"SDK_VERSION": "fixture-sdk"}):
            compare.main()

    def test_complete_comparison(self):
        self.run_compare()
        receipt = json.loads(self.fixture.output.read_text(encoding="utf-8"))
        self.assertEqual(receipt["purpose"], "post_refactor_frozen_byte_comparison")
        self.assertEqual(receipt["snapshot_sha256"], self.reference["snapshot_sha256"])

    def test_harness_change(self):
        (self.fixture.root / "Test/Core/Rendering/SkeletalSamplerBaseline.cpp").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "harness changed"):
            self.run_compare()

    def test_compiler_change(self):
        self.fixture.compiler.write_bytes(b"changed compiler hash fixture")
        with self.assertRaisesRegex(ValueError, "compiler binary"):
            self.run_compare()

    def test_fp_option_change(self):
        path = self.fixture.projects[0]
        path.write_text(path.read_text().replace("Precise", "Fast"))
        with self.assertRaisesRegex(ValueError, "compiler options"):
            self.run_compare()

    def test_windows_sdk_change(self):
        path = self.fixture.projects[0]
        path.write_text(path.read_text().replace("10.0.1", "10.0.2"))
        with self.assertRaisesRegex(ValueError, "Windows SDK"):
            self.run_compare()

    def test_source_specific_override(self):
        path = self.fixture.projects[0]
        path.write_text(path.read_text().replace("</Project>",
            '<ItemGroup><ClCompile Include="sampler.cpp"><FloatingPointModel>Fast</FloatingPointModel>'
            '</ClCompile></ItemGroup></Project>'))
        with self.assertRaisesRegex(ValueError, "source-specific"):
            self.run_compare()

    def test_build_source_mismatch(self):
        path = self.fixture.build / "CMakeCache.txt"
        path.write_text(path.read_text().replace("CMAKE_HOME_DIRECTORY:INTERNAL=", "OTHER:INTERNAL="))
        with self.assertRaisesRegex(ValueError, "cache binding"):
            self.run_compare()

    def test_other_executable(self):
        f = self.fixture
        f.argv[f.argv.index(str(f.exe))] = str(f.root / "test.exe")
        with self.assertRaisesRegex(ValueError, "executable path"):
            self.run_compare()

    def test_other_project_source(self):
        path = self.fixture.projects[0]
        path.write_text(path.read_text().replace("SkeletalAnimationSampler.cpp", "OtherSampler.cpp"))
        with self.assertRaisesRegex(ValueError, "source binding"):
            self.run_compare()

    def test_sdk_change(self):
        self.reference["sdk_version"] = "another"
        with self.assertRaisesRegex(ValueError, "Vulkan SDK"):
            self.run_compare()

    def test_dirty_source(self):
        self.fixture.dirty = True
        with self.assertRaisesRegex(ValueError, "dirty"):
            self.run_compare()


if __name__ == "__main__":
    unittest.main()
