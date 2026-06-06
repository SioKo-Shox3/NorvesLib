# Asset Fallback Policy

This document fixes the Phase 6 manifest schema and cooked/loose fallback rules.
Runtime loaders are not switched in Phase 6.

## Manifest Schema

The manifest is a JSON object:

```json
{
  "version": 1,
  "assets": [
    {
      "logical_path": "Textures/Silver.png",
      "kind": "texture",
      "source_hash": "0123456789abcdef",
      "variant": "default",
      "format": "nvtex.v0.rgba8.srgb",
      "cooked_package": "Cooked/Textures.nvpkg",
      "entry_name": "Textures/Silver.nvtex",
      "entry_type": "Tex0",
      "cooked_hash": "fedcba9876543210",
      "cooked_version": 0
    }
  ]
}
```

`version` must be the integer value `1`. Non-integral values such as `1.5`, strings, and other numeric versions are rejected. `assets` must be an array of flat records. Duplicate records with the same normalized `(logical_path, kind, variant)` are rejected.

## Fields

`kind` accepts `texture`, `model`, and `raw`. Unknown kinds are rejected in manifest parsing.

`source_hash` and `cooked_hash` are FNV-1a64 values encoded as exactly 16 lowercase hex characters. Uppercase hex and non-hex characters are rejected.

`entry_type` is a FourCC string. It must contain exactly 4 ASCII bytes. Literal spaces are allowed, for example `Raw `. It is converted using `MakeAssetPackageFourCC(s[0], s[1], s[2], s[3])`.

Manifest strings must be ASCII without control bytes. Decoded control characters below `0x20` and `0x7f` are rejected, including values introduced through JSON escape sequences.

`logical_path`, `cooked_package`, and `entry_name` are relative logical paths. Empty paths, absolute paths, UNC paths, drive-relative paths, and paths that escape through `..` are rejected. Separators are normalized before duplicate checks.

`format` must be non-empty. Phase 6 stores it as metadata; texture/model loaders interpret it in later phases.

## Fallback Rules

Manifest missing or never loaded:
Use loose fallback.

Manifest parse or validation failure:
Resolve requests fail with `InvalidManifest`. A broken manifest is not treated as missing and does not silently fallback to loose assets.

Manifest loaded but requested variant missing:
Use loose fallback.

Cooked reference found:
Use the cooked reference. Phase 7 AssetSystem validates package read, package parse, entry lookup, and entry hash.

Cooked package/read/parse/entry/hash failure:
The default mode fails. It does not silently fallback to loose assets.

Debug fallback mode:
Cooked failure may fallback to loose assets only when the decision requires an explicit log. The decision preserves failure kind, logical path, cooked package, entry name, and reason so Phase 7 can log the exact fallback.

## Ownership

`AssetManifest` owns metadata only. It does not own package objects, file bytes, decoded images, or RHI resources.

`Package`, `AssetFileReader`, runtime loaders, and AssetCook are not switched in Phase 6.
