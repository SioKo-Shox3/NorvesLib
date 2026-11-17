"""Blender の資産を段階的に処理し、入出力のハッシュを記録する。

使い方:
    python -B Scripts/Blender/pipeline.py --input PATH --out DIR --logical STEM
        [--stages NAMES] [--spec PATH] [--blender PATH] [--assetcook PATH]
        [--python PATH] [--settings PATH] [--dry-run]

段は rebuild、weights、export、validate、cook の順に実行する。
終了コードは、0 が成功、1 が実行前の確認または処理の失敗、2 が引数の誤り。
"""

import argparse
from dataclasses import dataclass
import json
import os
import re
import subprocess
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common


@dataclass(frozen=True)
class Stage:
    name: str
    kind: str
    script: str | None
    output: str
    produces_asset: bool


STAGES = (
    Stage("rebuild", "blender", "rebuild_on_base.py", "rebuild/{stem}.glb", True),
    Stage("weights", "blender", "transfer_weights.py", "weights/{stem}.glb", True),
    Stage("export", "blender", "export_gltf.py", "export/{stem}.gltf", True),
    Stage("validate", "python", "validate_rig.py", "validate/report.json", False),
    Stage("cook", "assetcook", None, "cooked", False),
)


@dataclass(frozen=True)
class Invocation:
    stage: Stage
    input_path: str
    output_path: str
    argv: list[str]
    config_sha256: str


class ArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, "引数の形式が正しくありません。--help で使い方を確認してください。\n")


def absolute_path(path: str) -> str:
    return os.path.abspath(path).replace("\\", "/")


def argument_error(message: str) -> int:
    print(f"引数の誤り: {message}", file=sys.stderr)
    return 2


def parse_arguments():
    parser = ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("-h", "--help", action="store_true")
    parser.add_argument("--input")
    parser.add_argument("--out")
    parser.add_argument("--logical")
    parser.add_argument("--stages")
    parser.add_argument("--spec", default=common.DEFAULT_SPEC_PATH)
    parser.add_argument("--blender", default=common.DEFAULT_BLENDER)
    parser.add_argument(
        "--assetcook",
        default=os.environ.get(
            "NORVESLIB_ASSETCOOK",
            os.path.join(
                common.REPO_ROOT, "build", "Tools", "AssetCook", "RelWithDebInfo", "AssetCook.exe"
            ),
        ),
    )
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--settings")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def read_settings(path: str | None) -> dict:
    if path is None:
        return {}

    settings = common.read_json(path)
    if not isinstance(settings, dict):
        raise ValueError("設定 JSON のルートはオブジェクトにしてください。")

    names = {stage.name for stage in STAGES}
    for name, values in settings.items():
        if name not in names:
            raise ValueError(f"設定に不明な段があります: {name}")
        if not isinstance(values, dict):
            raise ValueError(f"段の設定はオブジェクトにしてください: {name}")

    try:
        common.sha256_json(settings)
    except (TypeError, ValueError, UnicodeError):
        raise ValueError("設定 JSON に記録できない値が含まれています。") from None
    return settings


def make_invocations(args, stages: tuple[Stage, ...], settings: dict) -> list[Invocation]:
    invocations = []
    current_input = args.input
    for stage in stages:
        output = absolute_path(os.path.join(args.out, stage.output.format(stem=args.logical)))
        if stage.kind == "blender":
            config = {
                "stage": stage.name,
                "input": current_input,
                "output": output,
                "spec": args.spec,
                "record": absolute_path(os.path.join(args.out, stage.name, "record.json")),
                "settings": settings.get(stage.name, {}),
            }
            argv = [
                args.blender,
                "-b",
                "--factory-startup",
                "-P",
                absolute_path(os.path.join(common.SCRIPT_DIR, stage.script)),
                "--",
                json.dumps(config, sort_keys=True, separators=(",", ":"), ensure_ascii=False),
            ]
            config_sha256 = common.sha256_json(config)
        elif stage.kind == "python":
            argv = [
                args.python,
                "-B",
                absolute_path(os.path.join(common.SCRIPT_DIR, stage.script)),
                current_input,
                "--spec",
                args.spec,
                "--report",
                output,
            ]
            config_sha256 = common.sha256_json(argv)
        else:
            argv = [
                args.assetcook,
                "--rig-split",
                "--input",
                current_input,
                "--out",
                output,
                "--logical",
                args.logical,
            ]
            config_sha256 = common.sha256_json(argv)

        invocations.append(Invocation(stage, current_input, output, argv, config_sha256))
        if stage.produces_asset:
            current_input = output
    return invocations


def preflight(args, invocations: list[Invocation]) -> list[str]:
    errors = []
    if not (os.path.isfile(args.input) or os.path.isdir(args.input)):
        errors.append(f"入力の資産がありません: {args.input}")
    if not os.path.isfile(args.spec):
        errors.append(f"仕様ファイルがありません: {args.spec}")

    for invocation in invocations:
        stage = invocation.stage
        if stage.kind == "blender":
            script = absolute_path(os.path.join(common.SCRIPT_DIR, stage.script))
            if not os.path.isfile(script):
                errors.append(f"{stage.name} 段のスクリプトがまだありません: {script}")
            if not os.path.isfile(args.blender):
                errors.append(f"{stage.name} 段の Blender 実行ファイルがありません: {args.blender}")
        elif stage.kind == "python":
            script = absolute_path(os.path.join(common.SCRIPT_DIR, stage.script))
            if not os.path.isfile(script):
                errors.append(f"{stage.name} 段のスクリプトがまだありません: {script}")
            if not os.path.isfile(args.python):
                errors.append(f"{stage.name} 段の Python 実行ファイルがありません: {args.python}")
        else:
            if not os.path.isfile(args.assetcook):
                errors.append(f"{stage.name} 段の AssetCook 実行ファイルがありません: {args.assetcook}")
            if os.path.lexists(invocation.output_path):
                errors.append(f"{stage.name} 段の出力先が既に存在します: {invocation.output_path}")
    return errors


def report_io_error(message: str, error: OSError) -> None:
    detail = f"（エラー番号: {error.errno}）" if error.errno is not None else ""
    print(f"{message}{detail}", file=sys.stderr)


def run_pipeline(args, invocations: list[Invocation]) -> int:
    errors = preflight(args, invocations)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    try:
        spec_entry = common.file_entry(args.spec)
        input_entry = common.path_entry(args.input)
    except OSError as error:
        report_io_error("入力または仕様ファイルを記録できません。", error)
        return 1

    record = {
        "record_version": 1,
        "logical": args.logical,
        "spec": spec_entry,
        "input": input_entry,
        "stages": [],
        "status": "failed",
    }
    succeeded = True
    for index, invocation in enumerate(invocations, 1):
        stage = invocation.stage
        try:
            stage_input = common.path_entry(invocation.input_path)
            os.makedirs(os.path.dirname(invocation.output_path), exist_ok=True)
        except OSError as error:
            report_io_error(f"{stage.name} 段の入力または出力先を準備できません。", error)
            succeeded = False
            break

        common.log_line("PIPELINE_STAGE", {"index": index, "name": stage.name, "status": "start"})
        try:
            exit_code = subprocess.run(invocation.argv, shell=False).returncode
        except OSError as error:
            report_io_error(f"{stage.name} 段を起動できません。", error)
            exit_code = 1
        stage_status = "ok" if exit_code == 0 else "failed"
        common.log_line(
            "PIPELINE_STAGE",
            {"index": index, "name": stage.name, "status": stage_status, "exit_code": exit_code},
        )

        stage_record = {
            "name": stage.name,
            "kind": stage.kind,
            "argv": invocation.argv,
            "exit_code": exit_code,
            "inputs": [stage_input],
            "config_sha256": invocation.config_sha256,
            "outputs": [],
            "blender_version": None,
        }
        try:
            if os.path.exists(invocation.output_path):
                stage_record["outputs"].append(common.path_entry(invocation.output_path))
            if stage.kind == "blender":
                version_path = absolute_path(os.path.join(args.out, stage.name, "record.json"))
                if os.path.exists(version_path):
                    stage_details = common.read_json(version_path)
                    if not isinstance(stage_details, dict):
                        raise ValueError("段の記録はオブジェクトではありません。")
                    stage_record["blender_version"] = stage_details.get("blender_version")
        except OSError as error:
            report_io_error(f"{stage.name} 段の出力または記録を読み取れません。", error)
            succeeded = False
        except ValueError:
            print(f"{stage.name} 段の記録の形式が正しくありません。", file=sys.stderr)
            succeeded = False

        record["stages"].append(stage_record)
        if exit_code != 0 or not succeeded:
            succeeded = False
            break

    record["status"] = "ok" if succeeded else "failed"
    record_path = absolute_path(os.path.join(args.out, "record.json"))
    try:
        common.write_json(record_path, record)
    except OSError as error:
        report_io_error(f"パイプラインの記録を書き込めません: {record_path}", error)
        return 1
    common.log_line("PIPELINE_RESULT", {"status": record["status"], "record": record_path})
    return 0 if succeeded else 1


def main() -> int:
    args = parse_arguments()
    if args.help:
        print(__doc__.strip())
        return 0
    for name in ("input", "out", "logical"):
        if not getattr(args, name):
            return argument_error(f"--{name} を指定してください。")
    if re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", args.logical) is None:
        return argument_error("--logical の形式が正しくありません。")

    names = {stage.name for stage in STAGES}
    selected = [stage.name for stage in STAGES] if args.stages is None else args.stages.split(",")
    unknown = [name for name in selected if name not in names]
    if unknown:
        return argument_error(f"不明な段があります: {', '.join(unknown)}")
    if len(set(selected)) != len(selected):
        return argument_error("同じ段を複数回指定できません。")
    stages = tuple(stage for stage in STAGES if stage.name in selected)

    for name in ("input", "out", "spec", "blender", "assetcook", "python"):
        setattr(args, name, absolute_path(getattr(args, name)))
    if args.settings is not None:
        args.settings = absolute_path(args.settings)
    try:
        settings = read_settings(args.settings)
    except OSError as error:
        report_io_error(f"設定ファイルを読み取れません: {args.settings}", error)
        return 2
    except ValueError as error:
        return argument_error(str(error))

    invocations = make_invocations(args, stages, settings)
    if args.dry_run:
        for index, invocation in enumerate(invocations, 1):
            common.log_line(
                "PIPELINE_STAGE",
                {"index": index, "name": invocation.stage.name, "argv": invocation.argv},
            )
        return 0
    return run_pipeline(args, invocations)


if __name__ == "__main__":
    sys.exit(main())
