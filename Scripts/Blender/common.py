"""Blender の生成、検査、cook に共通する設定と記録を扱う。"""

import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys


SCRIPT_DIR: str = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT: str = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir, os.pardir))
DEFAULT_SPEC_PATH: str = os.path.join(SCRIPT_DIR, "skeleton_spec.json")
DEFAULT_BLENDER: str = os.environ.get(
    "NORVESLIB_BLENDER",
    r"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe",
)

_VERSION_UNSET = object()
_TAG_PATTERN = re.compile(r"[A-Z][A-Z0-9_]*")
_KEY_PATTERN = re.compile(r"[a-z][a-z0-9_]*")
_PLAIN_VALUE_PATTERN = re.compile(r"[A-Za-z0-9_.:/\\+\-]+")


class ConfigError(ValueError):
    """設定や JSON の内容が不正な場合に送出する。"""


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigError(f"JSON の鍵が重複しています: {key}")
        result[key] = value
    return result


def _parse_json(text):
    try:
        return json.loads(text, object_pairs_hook=_unique_object)
    except json.JSONDecodeError as error:
        raise ConfigError(
            f"JSON の形式が正しくありません: {error.lineno} 行目、{error.colno} 列目"
        ) from None


def read_json(path) -> object:
    try:
        with open(path, "r", encoding="utf-8-sig") as file:
            text = file.read()
    except UnicodeDecodeError:
        raise ConfigError("JSON ファイルを UTF-8 として読み取れません。") from None
    return _parse_json(text)


def write_json(path, obj) -> None:
    parent = os.path.dirname(os.path.abspath(path))
    os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(obj, file, indent=2, ensure_ascii=False)
        file.write("\n")


def canonical_json_bytes(obj) -> bytes:
    return json.dumps(
        obj,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_json(obj) -> str:
    return sha256_bytes(canonical_json_bytes(obj))


def config_args(argv=None) -> list[str]:
    args = list(sys.argv if argv is None else argv)
    try:
        separator = args.index("--")
    except ValueError:
        return []
    return args[separator + 1 :]


def load_config(argv=None) -> dict:
    args = config_args(argv)
    if not args:
        return {}
    if len(args) != 1:
        raise ConfigError("設定は 1 個だけ指定してください。")

    config = _parse_json(args[0]) if args[0].lstrip().startswith("{") else read_json(args[0])
    if not isinstance(config, dict):
        raise ConfigError("設定の JSON はオブジェクトで指定してください。")
    return config


def _format_value(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("有限でない浮動小数点数は指定できません。")
        return repr(value)
    if isinstance(value, str):
        if _PLAIN_VALUE_PATTERN.fullmatch(value):
            return value
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, (list, tuple, dict)):
        try:
            return json.dumps(
                value, sort_keys=False, separators=(",", ":"), ensure_ascii=False
            )
        except (TypeError, ValueError):
            raise ValueError("値を JSON に変換できません。") from None
    raise ValueError("対応していない値の型です。")


def format_line(tag: str, fields) -> str:
    if not isinstance(tag, str) or not _TAG_PATTERN.fullmatch(tag):
        raise ValueError("タグの形式が正しくありません。")

    parts = [tag]
    entries = fields.items() if isinstance(fields, dict) else fields
    for key, value in entries:
        if not isinstance(key, str) or not _KEY_PATTERN.fullmatch(key):
            raise ValueError("鍵の形式が正しくありません。")
        parts.append(f"{key}={_format_value(value)}")
    return " ".join(parts)


def log_line(tag, fields, stream=None) -> None:
    print(format_line(tag, fields), file=sys.stdout if stream is None else stream, flush=True)


def blender_version() -> str | None:
    try:
        import bpy
    except ImportError:
        return None
    return bpy.app.version_string


def _absolute_path(path) -> str:
    return os.path.abspath(os.fspath(path)).replace("\\", "/")


def file_entry(path) -> dict:
    return {"path": _absolute_path(path), "sha256": sha256_file(path)}


def _raise_walk_error(error):
    raise error


def tree_entry(path) -> dict:
    root = Path(os.path.abspath(path))
    paths = []
    for directory, _, names in os.walk(root, onerror=_raise_walk_error):
        for name in names:
            entry = Path(directory, name)
            if entry.is_file():
                paths.append(entry)
    paths.sort(key=lambda entry: entry.relative_to(root).as_posix())
    files = [
        {"path": entry.relative_to(root).as_posix(), "sha256": sha256_file(entry)}
        for entry in paths
    ]
    return {"path": _absolute_path(root), "files": files, "sha256": sha256_json(files)}


def path_entry(path) -> dict:
    if os.path.isdir(path):
        return tree_entry(path)
    if os.path.isfile(path):
        return file_entry(path)
    raise FileNotFoundError(f"ファイルまたはディレクトリが見つかりません: {path}")


def make_record(stage: str, inputs, config: dict, outputs, blender_version=_VERSION_UNSET) -> dict:
    # 引数名と関数名が重なるため、関数はモジュールから参照する。
    version = globals()["blender_version"]() if blender_version is _VERSION_UNSET else blender_version
    return {
        "record_version": 1,
        "stage": stage,
        "blender_version": version,
        "inputs": [path_entry(path) for path in inputs],
        "config": config,
        "config_sha256": sha256_json(config),
        "outputs": [path_entry(path) for path in outputs],
    }


def write_record(path, record) -> None:
    write_json(path, record)
