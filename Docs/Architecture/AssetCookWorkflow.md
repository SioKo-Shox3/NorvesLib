# AssetCook Workflow

This workflow separates an asset-set spec from the runtime manifest consumed by the engine.

## Asset-Set Spec

`Assets/AssetSets/Rendering3DTestSilverTextures.json` describes the cook input set. It is source-controlled and contains:

- `version`: spec schema version, currently `1`.
- `name`: human-readable asset-set name.
- `package_root`: relative runtime package directory, currently `Cooked/Silver`.
- `default_variant`: variant used when a texture entry omits `variant`, currently `default`.
- `textures`: source images plus cook metadata for each texture package.

Spec paths are resolved by `Scripts/CookTextureAssetSet.ps1`, not by the caller's current working directory. `SpecPath`, `RuntimeRoot`, `ManifestPath`, and `source_path` may be absolute or repository-relative. Manifest and logical fields such as `package_root`, `package_name`, `logical_path`, and `entry_name` must be non-empty relative paths with no absolute, drive-relative, UNC, or `..` traversal segments. They are normalized to `/` before cook validation.

`logical_path` and `entry_name` may use the Game-facing `Assets/...` spelling. The helper normalizes those fields through the same logical-path convention as `AssetCook`, so runtime manifest entries omit the leading `Assets/` prefix, for example `Assets/Textures/Silver/silver_albedo.png` becomes `Textures/Silver/silver_albedo.png`.

## Runtime Manifest

The helper runs `AssetCook` once per texture, using the same manifest path for each invocation. After every invocation it reads the single-entry manifest, verifies the entry against the spec, and collects those entries into one aggregate manifest.

The final manifest defaults to:

```text
<RuntimeRoot>/manifest.json
```

If `-ManifestPath` is supplied, its parent must be exactly `RuntimeRoot`. This keeps `cooked_package` entries relative to `RuntimeRoot`; the helper does not rebase manifests placed elsewhere.

## Current Silver Texture Formats

The Rendering3DTest Silver asset set cooks these five textures:

| Logical path | Cook format |
| --- | --- |
| `Assets/Textures/Silver/silver_albedo.png` | `nvtex.v0.rgba8.srgb` |
| `Assets/Textures/Silver/silver_normal-ogl.png` | `nvtex.v0.rgba8.linear` |
| `Assets/Textures/Silver/silver_metallic.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_roughness.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_ao.png` | `nvtex.v0.r8.linear` |

`stb_image` is still used at cook time by `AssetCook` to decode source images. Runtime smoke validation expects cooked `nvtex` loads and rejects loose `stb_image` fallback for these paths.

## Validation Commands

Build the cook tool and game:

```powershell
cmake --build build --config Debug --target AssetCook
cmake --build build --config Debug --target Game
```

Cook the Silver set into a standalone runtime root:

```powershell
.\Scripts\CookTextureAssetSet.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -SpecPath .\Assets\AssetSets\Rendering3DTestSilverTextures.json `
  -RuntimeRoot .\build\CookedAssetSets\Debug\Silver\RuntimeRoot
```

Run the game smoke:

```powershell
.\Scripts\RunCookedTextureGameSmoke.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe
```

Generated packages, manifests, and smoke logs are build outputs. Keep them under `build/` or other ignored output locations and do not commit them.
