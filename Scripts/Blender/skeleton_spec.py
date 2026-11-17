"""共通骨格仕様を読み込み、検証と正準順のロールプロファイル出力を提供する。"""

import argparse
import json
import math
from pathlib import Path
import re


VOCABULARIES = {
    "quadruped_v1": (
        "root", "pelvis", "spine", "neck", "head", "jaw", "tail",
        "front_L_clavicle", "front_L_upper", "front_L_lower", "front_L_paw",
        "front_R_clavicle", "front_R_upper", "front_R_lower", "front_R_paw",
        "hind_L_thigh", "hind_L_shin", "hind_L_hock", "hind_L_paw",
        "hind_R_thigh", "hind_R_shin", "hind_R_hock", "hind_R_paw",
    ),
}
CHAIN_ROLES = {"quadruped_v1": frozenset({"spine", "neck", "tail"})}
ROOT_ROLE = {"quadruped_v1": "root"}
IDENTITY_C = (1, 0, 0, 0, 1, 0, 0, 0, 1)
DEFAULT_SPEC_PATH = Path(__file__).resolve().with_name("skeleton_spec.json")

_SPEC_KEYS = (
    "spec_version", "name", "vocabulary", "roles", "required_roles",
    "limits", "naming", "uv", "morph_targets", "interpolations",
)


class SpecError(ValueError):
    """仕様の誤りを日本語の説明一覧として保持する。"""

    def __init__(self, errors: list[str]):
        self.errors = list(errors)
        super().__init__("\n".join(self.errors))


def _check_keys(value, expected, label, errors):
    if not isinstance(value, dict):
        errors.append(f"{label} は辞書でなければなりません。")
        return False
    for key in expected:
        if key not in value:
            errors.append(f"{label} に必須の鍵 {key!r} がありません。")
    for key in value:
        if key not in expected:
            errors.append(f"{label} に未知の鍵 {key!r} があります。")
    return True


def _is_integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _is_finite_number(value):
    return _is_integer(value) or (
        isinstance(value, float) and math.isfinite(value)
    )


def validate_spec(spec) -> list[str]:
    """仕様に含まれる誤りを、検査可能な項目すべてについて集める。"""
    errors = []
    if not _check_keys(spec, _SPEC_KEYS, "仕様", errors):
        return errors

    if not _is_integer(spec.get("spec_version")) or spec["spec_version"] != 1:
        errors.append("spec_version は整数の 1 でなければなりません。")
    if not isinstance(spec.get("name"), str) or not spec["name"]:
        errors.append("name は空でない文字列でなければなりません。")

    vocabulary = spec.get("vocabulary")
    known_vocabulary = isinstance(vocabulary, str) and vocabulary in VOCABULARIES
    if not known_vocabulary:
        errors.append("vocabulary は定義済みの語彙名でなければなりません。")
    canonical_roles = VOCABULARIES[vocabulary] if known_vocabulary else ()
    chain_roles = CHAIN_ROLES[vocabulary] if known_vocabulary else frozenset()
    root_role = ROOT_ROLE[vocabulary] if known_vocabulary else None

    pattern = None
    naming = spec.get("naming")
    if _check_keys(naming, ("joint_pattern",), "naming", errors):
        expression = naming.get("joint_pattern")
        if not isinstance(expression, str):
            errors.append("naming.joint_pattern は正規表現の文字列でなければなりません。")
        else:
            try:
                pattern = re.compile(expression)
            except (re.error, OverflowError, RecursionError):
                errors.append("naming.joint_pattern を正規表現として解釈できません。")

    roles = spec.get("roles")
    joint_count = 0
    seen_joints = set()
    if not isinstance(roles, dict):
        errors.append("roles は辞書でなければなりません。")
    else:
        if root_role is not None and root_role not in roles:
            errors.append(f"roles にルートの role {root_role!r} が必要です。")
        for role, joints in roles.items():
            if known_vocabulary and role not in canonical_roles:
                errors.append(f"roles の role {role!r} は語彙にありません。")
            if not isinstance(joints, list):
                errors.append(f"roles[{role!r}] は空でない文字列のリストでなければなりません。")
                continue
            joint_count += len(joints)
            if not joints:
                errors.append(f"roles[{role!r}] は空にできません。")
            if role in canonical_roles and role not in chain_roles and len(joints) != 1:
                errors.append(f"単一の role {role!r} の関節はちょうど 1 個でなければなりません。")
            for index, joint in enumerate(joints):
                if not isinstance(joint, str) or not joint:
                    errors.append(f"roles[{role!r}][{index}] は空でない文字列でなければなりません。")
                    continue
                if pattern is not None and pattern.fullmatch(joint) is None:
                    errors.append(f"関節名 {joint!r} は naming.joint_pattern に一致しません。")
                if joint in seen_joints:
                    errors.append(f"関節名 {joint!r} が仕様全体で重複しています。")
                seen_joints.add(joint)

    required = spec.get("required_roles")
    if not isinstance(required, list):
        errors.append("required_roles は重複しない文字列のリストでなければなりません。")
    else:
        seen_required = set()
        for role in required:
            if not isinstance(role, str):
                errors.append("required_roles の各要素は文字列でなければなりません。")
                continue
            if role in seen_required:
                errors.append(f"required_roles の role {role!r} が重複しています。")
            seen_required.add(role)
            if isinstance(roles, dict) and role not in roles:
                errors.append(f"required_roles の role {role!r} が roles にありません。")
        if root_role is not None and root_role not in required:
            errors.append(f"required_roles にルートの role {root_role!r} が必要です。")

    limits = spec.get("limits")
    if _check_keys(limits, ("max_joints", "max_influences", "weight_sum_tolerance"), "limits", errors):
        max_joints = limits.get("max_joints")
        if not _is_integer(max_joints) or not 1 <= max_joints <= 256:
            errors.append("limits.max_joints は 1〜256 の整数でなければなりません。")
        if _is_integer(max_joints) and max_joints < joint_count:
            errors.append(f"limits.max_joints は関節の総数 {joint_count} 以上でなければなりません。")
        influences = limits.get("max_influences")
        if not _is_integer(influences) or not 1 <= influences <= 8:
            errors.append("limits.max_influences は 1〜8 の整数でなければなりません。")
        tolerance = limits.get("weight_sum_tolerance")
        if not _is_finite_number(tolerance) or not 0 < tolerance <= 0.1:
            errors.append("limits.weight_sum_tolerance は 0 より大きく 0.1 以下の有限数でなければなりません。")

    uv = spec.get("uv")
    if _check_keys(uv, ("sets", "min", "max"), "uv", errors):
        if uv.get("sets") != ["TEXCOORD_0"]:
            errors.append('uv.sets は ["TEXCOORD_0"] でなければなりません。')
        minimum, maximum = uv.get("min"), uv.get("max")
        finite_minimum = _is_finite_number(minimum)
        finite_maximum = _is_finite_number(maximum)
        if not finite_minimum:
            errors.append("uv.min は有限数でなければなりません。")
        if not finite_maximum:
            errors.append("uv.max は有限数でなければなりません。")
        if finite_minimum and finite_maximum and minimum >= maximum:
            errors.append("uv.min は uv.max より小さくなければなりません。")

    if not isinstance(spec.get("morph_targets"), bool):
        errors.append("morph_targets は真偽値でなければなりません。")
    interpolations = spec.get("interpolations")
    if not isinstance(interpolations, list) or not interpolations:
        errors.append("interpolations は空でないリストでなければなりません。")
    if isinstance(interpolations, list):
        seen_interpolations = set()
        for interpolation in interpolations:
            if not isinstance(interpolation, str) or interpolation not in ("LINEAR", "STEP", "CUBICSPLINE"):
                errors.append("interpolations の要素は LINEAR・STEP・CUBICSPLINE のいずれかでなければなりません。")
            if isinstance(interpolation, str):
                if interpolation in seen_interpolations:
                    errors.append(f"interpolations の値 {interpolation!r} が重複しています。")
                seen_interpolations.add(interpolation)
    return errors


def load_spec(path=None) -> dict:
    """BOM 付き UTF-8 の仕様も受け付け、重複した鍵と仕様の誤りを拒否する。"""
    errors = []

    def collect_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                errors.append(f"JSON の鍵 {key!r} が重複しています。")
            result[key] = value
        return result

    try:
        with open(DEFAULT_SPEC_PATH if path is None else path, encoding="utf-8-sig") as stream:
            spec = json.load(stream, object_pairs_hook=collect_pairs)
    except json.JSONDecodeError as error:
        errors.append(f"仕様の JSON を解釈できません（{error.lineno} 行、{error.colno} 列）。")
        raise SpecError(errors) from error
    except (OSError, UnicodeError) as error:
        raise SpecError(["仕様ファイルを UTF-8 で読み込めません。パス・権限・文字コードを確認してください。"]) from error
    errors.extend(validate_spec(spec))
    if errors:
        raise SpecError(errors)
    return spec


def role_joints(spec) -> list[tuple[str, list[str]]]:
    """検証済みの仕様から、存在する role と関節の写しを正準順で返す。"""
    return [
        (role, list(spec["roles"][role]))
        for role in VOCABULARIES[spec["vocabulary"]]
        if role in spec["roles"]
    ]


def joint_roles(spec) -> dict[str, str]:
    """検証済みの仕様から、関節名を role に引く辞書を返す。"""
    return {joint: role for role, joints in role_joints(spec) for joint in joints}


def emit_role_profile(spec) -> dict:
    """正準順とチェーンの関節順を保ち、恒等補正を明示したプロファイルを返す。"""
    return {
        "vocabulary": spec["vocabulary"],
        "target_roles": {
            role: [{"joint": joint, "C": list(IDENTITY_C)} for joint in joints]
            for role, joints in role_joints(spec)
        },
    }


def main() -> int:
    """仕様の検査またはロールプロファイルの JSON 出力を実行する。"""
    parser = argparse.ArgumentParser(description="共通骨格仕様の検査とロールプロファイル出力")
    parser.add_argument("--spec", metavar="PATH", help="読み込む仕様ファイル")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="仕様を検査する")
    mode.add_argument("--emit-role-profile", action="store_true", help="ロールプロファイルを出力する")
    parser.add_argument("--out", metavar="PATH", help="ロールプロファイルの出力先")
    args = parser.parse_args()
    if args.out is not None and not args.emit_role_profile:
        parser.error("--out は --emit-role-profile と組み合わせて指定してください。")
    try:
        spec = load_spec(args.spec)
    except SpecError as error:
        for message in error.errors:
            print(f"SPEC_CHECK status=error message={json.dumps(message, ensure_ascii=False)}")
        return 1
    if args.check:
        joints = sum(len(joints) for _, joints in role_joints(spec))
        print(
            f"SPEC_CHECK status=ok name={spec['name']} vocabulary={spec['vocabulary']} "
            f"roles={len(spec['roles'])} joints={joints} required={len(spec['required_roles'])} "
            f"max_joints={spec['limits']['max_joints']}"
        )
    else:
        output = json.dumps(emit_role_profile(spec), indent=2, ensure_ascii=False) + "\n"
        if args.out is None:
            print(output, end="")
        else:
            with open(args.out, "w", encoding="utf-8", newline="\n") as stream:
                stream.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
