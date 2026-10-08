"""実Windowsの管理storeを、runtime payloadと別の有限inventoryで保存・検証する。"""
from __future__ import annotations
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def strict_json(data: bytes) -> dict:
    def pairs(items):
        out = {}
        for key, value in items:
            require(key not in out, "JSON duplicate: " + key)
            out[key] = value
        return out
    out = json.loads(data.decode("utf-8"), object_pairs_hook=pairs)
    require(isinstance(out, dict), "JSON object required")
    return out


def shape(value: dict, fields) -> None:
    require(isinstance(value, dict) and set(value) == set(fields), "exact JSON shape required")


def hex_value(value: str, width: int) -> int:
    require(isinstance(value, str) and re.fullmatch("[0-9a-f]{" + str(width) + "}", value) is not None, "canonical hex required")
    return int(value, 16)


def volume_guid_identity(canonical: str) -> str:
    require(canonical.startswith("\\\\?\\Volume{"), "volume path prefix")
    require(len(canonical) >= 49 and canonical[47:49] == "}\\", "volume path delimiter")
    value = canonical.split("{", 1)[1].split("}", 1)[0].lower()
    require(re.fullmatch("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", value) is not None,
            "volume UUID shape")
    return value


def owner_id(spec_identity: str, root_identity: str, manifest: str) -> str:
    data = struct.pack("<I", 1)
    for field in ("NorvesLib.AssetCook", spec_identity, root_identity, manifest):
        encoded = field.encode("utf-8")
        data += struct.pack("<I", len(encoded)) + encoded
    return hashlib.sha256(data).digest()[:16].hex()


def native(path: Path) -> dict:
    require(os.name == "nt", "real Windows identity required")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.GetFileInformationByHandleEx.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
    kernel.GetFileInformationByHandleEx.restype = wintypes.BOOL
    kernel.GetFinalPathNameByHandleW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD]
    kernel.GetFinalPathNameByHandleW.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    class FileId(ctypes.Structure):
        _fields_ = [("volume", ctypes.c_uint64), ("identifier", ctypes.c_ubyte * 16)]
    class Basic(ctypes.Structure):
        _fields_ = [("creation", ctypes.c_int64), ("access", ctypes.c_int64), ("write", ctypes.c_int64),
                    ("change", ctypes.c_int64), ("attributes", wintypes.DWORD)]
    handle = kernel.CreateFileW(str(path), 0x80, 7, None, 3, 0x02200000, None)
    require(handle not in (None, 0, ctypes.c_void_p(-1).value), "open native identity: " + str(path))
    try:
        ident, basic = FileId(), Basic()
        require(bool(kernel.GetFileInformationByHandleEx(handle, 18, ctypes.byref(ident), ctypes.sizeof(ident))), "native FILE_ID_INFO")
        require(bool(kernel.GetFileInformationByHandleEx(handle, 0, ctypes.byref(basic), ctypes.sizeof(basic))), "native FILE_BASIC_INFO")
        require(not basic.attributes & 0x400, "reparse identity rejected")
        buffer = ctypes.create_unicode_buffer(32768)
        used = kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 1)
        require(0 < used < len(buffer), "native volume GUID path")
        canonical = buffer.value
        require(canonical.startswith("\\\\?\\Volume{") and "}\\" in canonical, "physical GUID path required")
        return {"volume": f"{ident.volume:016x}", "file_id": bytes(ident.identifier).hex(),
                "canonical": canonical, "creation": basic.creation, "write": basic.write,
                "directory": bool(basic.attributes & 0x10)}
    finally:
        require(bool(kernel.CloseHandle(handle)), "native identity close")


def snapshot(root: Path) -> dict:
    result = {}
    for path in [root, *sorted(root.rglob("*"))]:
        require(not path.is_symlink() and not path.is_junction(), "snapshot reparse")
        item = native(path)
        if not item["directory"]:
            data = path.read_bytes()
            item.update(size=len(data), sha256=digest(data))
        result["." if path == root else path.relative_to(root).as_posix()] = item
    return result


def fnv(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def capture_store(workspace: Path, specs: dict[str, Path], destination: Path) -> dict:
    """2件のbaseline workspaceのみ。実体4fileを原ID/byte receipt付きで別保存する。"""
    require(len(specs) == 2, "baseline requires exactly two roots")
    store = workspace / ".norves-assetcook"
    workspace_id, store_id = native(workspace), native(store)
    header_raw = (store / "header.json").read_bytes()
    index_raw = (store / "roots.json").read_bytes()
    header, index = strict_json(header_raw), strict_json(index_raw)
    shape(header, ("producer", "schema", "store_id", "volume_guid", "volume_serial", "workspace_id", "store_directory_id"))
    require(type(header["schema"]) is int and header["schema"] == 1 and header["producer"] == "NorvesLib.AssetCook", "header producer/schema")
    require(hex_value(header["store_id"], 32) != 0, "store token")
    require(header["volume_serial"] == workspace_id["volume"] == store_id["volume"], "header volume identity")
    require(header["workspace_id"] == workspace_id["file_id"] and header["store_directory_id"] == store_id["file_id"], "header native directory identities")
    require(header["volume_guid"] == volume_guid_identity(workspace_id["canonical"]), "header volume GUID")
    shape(index, ("schema", "store_id", "generation", "roots"))
    require(type(index["schema"]) is int and index["schema"] == 1 and index["store_id"] == header["store_id"] and hex_value(index["generation"], 16) == 3, "baseline index header")
    require(isinstance(index["roots"], list) and len(index["roots"]) == 2, "baseline root count")
    expected_files = {"header.json": header_raw, "roots.json": index_raw}
    claims, roots = set(), {}
    for claim in index["roots"]:
        shape(claim, ("claim_id", "leaf", "directory_id", "owner_id"))
        require(hex_value(claim["claim_id"], 32) != 0 and claim["claim_id"] not in claims, "unique claim")
        claims.add(claim["claim_id"])
        leaf = claim["leaf"]
        require(leaf in specs and leaf not in roots, "exact root leaf")
        runtime = workspace / leaf
        root_id, spec_id = native(runtime), native(specs[leaf])
        expected_owner = owner_id(spec_id["canonical"], root_id["canonical"], "manifest.json")
        require(claim["directory_id"] == root_id["file_id"] and claim["owner_id"] == expected_owner, "claim identity/owner")
        state_name = "state-" + claim["claim_id"] + ".json"
        raw = (store / state_name).read_bytes()
        state = strict_json(raw)
        shape(state, ("schema", "producer", "owner", "root", "manifest", "generation", "records"))
        binding_root = runtime.as_posix()
        binding_root = binding_root[0].upper() + binding_root[1:]
        require(type(state["schema"]) is int and state["schema"] == 1 and state["producer"] == "NorvesLib.AssetCook" and state["owner"] == expected_owner and
                state["root"] == binding_root and state["manifest"] == "manifest.json" and hex_value(state["generation"], 16) == 1, "state independent binding")
        # 呼出側がfrozen manifestと生byte比較済み。parserの正規化をここに複製しない。
        manifest = strict_json((runtime / "manifest.json").read_bytes())
        shape(manifest, ("version", "assets"))
        require(type(manifest["version"]) is int and manifest["version"] == 1, "manifest schema")
        entries = manifest["assets"]
        require(len(state["records"]) == len(entries), "state record count")
        packages = []
        for row, entry in zip(state["records"], entries):
            shape(row, ("primary", "schema", "dependency_schema", "revision", "dependency_hash", "outputs"))
            key = {"logical": entry["logical_path"], "kind": "texture", "variant": entry["variant"]}
            require(row["primary"] == key and type(row["schema"]) is int and row["schema"] == 1 and type(row["dependency_schema"]) is int and row["dependency_schema"] == 1 and hex_value(row["revision"], 16) == 1, "state primary/revision")
            hex_value(row["dependency_hash"], 16)
            require(isinstance(row["outputs"], list) and len(row["outputs"]) == 1, "single texture output")
            value = row["outputs"][0]
            shape(value, ("key", "source_hash", "format", "package", "entry", "entry_type", "cooked_hash", "cooked_version", "package_size", "package_hash", "skeletal"))
            package = entry["cooked_package"]
            require(value["key"] == key and value["format"] == entry["format"] and value["package"] == package and
                    value["entry"] == entry["entry_name"] and value["skeletal"] is None and
                    value["source_hash"] == entry["source_hash"] and value["cooked_hash"] == entry["cooked_hash"] and
                    type(value["cooked_version"]) is int and value["cooked_version"] == entry["cooked_version"] == 0 and
                    value["entry_type"] == "0000000030786554" and entry["entry_type"] == "Tex0", "state output identity")
            for field in ("source_hash", "entry_type", "cooked_hash", "package_hash"):
                hex_value(value[field], 16)
            require(isinstance(package, str) and package and "\\" not in package and ":" not in package and
                    all(part not in ("", ".", "..") for part in package.split("/")), "safe package relative name")
            data = (runtime / package).read_bytes()
            require(hex_value(value["package_size"], 16) == len(data) and value["package_hash"] == fnv(data), "state full package bytes")
            packages.append({"path": package, "native": native(runtime / package), "size": len(data), "sha256": digest(data)})
        expected_files[state_name] = raw
        roots[leaf] = {"claim": claim, "root_native": root_id, "spec_native": spec_id, "expected_owner": expected_owner,
                       "binding_root": binding_root, "state_file": state_name, "packages": packages}
    require({path.name for path in store.iterdir()} == set(expected_files), "exact successful store inventory; no pending/orphans")
    require(all(path.is_file() and not path.is_symlink() and not path.is_junction() for path in store.iterdir()), "regular store files")
    destination.mkdir(parents=True, exist_ok=False)
    receipts = {}
    for name, data in expected_files.items():
        original = native(store / name)
        (destination / name).write_bytes(data)
        require((destination / name).read_bytes() == data, "metadata copy byte parity")
        receipts[name] = {"size": len(data), "sha256": digest(data), "original_native": original}
    result = {"schema": "norves.managed-texture-evidence.v1", "helper_sha256": digest(Path(__file__).read_bytes()), "workspace_native": workspace_id,
              "store_native": store_id, "files": receipts, "roots": roots}
    (destination / "receipt.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
