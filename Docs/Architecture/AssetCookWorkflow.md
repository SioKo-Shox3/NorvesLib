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

The direct Rendering3DTest Silver asset set cooks these five textures from
`Assets/AssetSets/Rendering3DTestSilverTextures.json`:

| Logical path | Cook format |
| --- | --- |
| `Assets/Textures/Silver/silver_albedo.png` | `nvtex.v0.rgba8.srgb` |
| `Assets/Textures/Silver/silver_normal-ogl.png` | `nvtex.v0.rgba8.linear` |
| `Assets/Textures/Silver/silver_metallic.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_roughness.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_ao.png` | `nvtex.v0.r8.linear` |

`stb_image` is still used at cook time by `AssetCook` to decode source images. Runtime smoke validation expects cooked `nvtex` loads and rejects loose `stb_image` fallback for these paths.

## Direct Runtime Contract

The direct Silver workflow loads texture assets through the runtime manifest and package files before creating RHI textures. For cooked-ready entries the expected runtime profile stages are:

- `texture_asset_resolve`
- `texture_cooked_parse`
- `texture_cooked_upload`

The direct smoke rejects `source=loose_stbi` for the cooked Silver logical paths. Loose `stb_image` loads remain valid only when no manifest is loaded, a requested variant is missing, or explicit debug fallback mode permits a cooked failure fallback.

## glTF Prepared Texture Workflow

`Assets/AssetSets/Rendering3DTestSilverGltfTextures.json` describes the model-local Silver fixture used to validate glTF prepared texture loading. The fixture keeps texture URIs below `Assets/Models/Rendering3DTestSilverGltf/` so the cache keys are distinct from the direct Silver texture set:

| Logical path | Cook format | Usage |
| --- | --- | --- |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_albedo.png` | `nvtex.v0.rgba8.srgb` | `standard` |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_normal-ogl.png` | `nvtex.v0.rgba8.linear` | `standard` |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_arm.png` | `nvtex.v0.rgba8.linear` | `arm` |

For glTF model loads, `GLTFAnalyzer` preserves both the logical request path and the resolved loose fallback file path. Worker staging calls `PrepareTextureAssetForWorker()` for the logical path. If the prepared status is `CookedReady`, standard textures skip loose file read and `stb_image` decode on the worker; the main/render side later calls `FinalizePreparedTextureAsset()` and uploads the cooked payload. Packed ARM textures in this fixture use `nvtex.v0.rgba8.linear`; worker staging splits mip 0 into AO, roughness, and metallic R8 data through `texture_prepared_split`.

The expected prepared profile stages are:

- `texture_prepare_asset`
- `texture_prepared_cooked_upload`
- `texture_prepared_finalize`
- `texture_prepared_split`

Prepared cooked loads must not produce `gltf_image_read`, `gltf_image_decode`, or `source=loose_stbi` logs for the model-local cooked source paths. `stb_image` remains part of the cook-time source decode path and loose/debug fallback path, but cooked-ready runtime loads do not use it.

## Cooked Model Opt-In And Four-Entry Fixture

`Scripts/RunCookedModelGameProfile.ps1` turns the model-local Silver fixture into one
runtime root. It first uses `CookTextureAssetSet.ps1` for the three texture packages,
then invokes `AssetCook` for the glTF model and aggregates the model fragment with the
texture manifest. Before Game starts, the runner checks schema version `1` as a
mathematical integer, exactly four entries, unique `logical_path|kind|variant` keys,
normalized non-traversing package paths contained under the runtime root, package
existence, exact model fields, and the exact three glTF image URI/logical-path matches.

Cooked model loading is deliberately opt-in:

```text
--rendering3dtest-use-cooked-model
--texture-asset-root <absolute runtime root>
--texture-asset-manifest <absolute four-entry manifest>
--rendering3dtest-model Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf
```

The bare opt-in flag requires all three accompanying values and rejects duplicates.
Without it, the same root/manifest/model arguments preserve the existing
`GLTFAnalyzer` path and its `GltfPrepared` texture behavior. With it, only the Boulder
model request is issued through `MegaGeometryResources::LoadModelAsync`; the shared
callback, placeholder replacement, scope tracking, release, and cancellation behavior
remain unchanged, with cancellation returning to the same subsystem that issued the
request.

`SummarizeAssetLoadProfile.ps1 -RequireCompleteCookedModel` validates the cooked model
profile as a correlated contract. It requires worker resolve/parse rows from
`cooked_nvmesh`, the same nonzero request id and normalized path, four successful
main-render finalize stages with matching request/debug/path fields, a matching-debug
GPU upload, and a successful async flush that processed work. It rejects loose glTF
read/parse/buffer/extract/clusterize/staging/flush stages. The legacy
`-RequireCompleteModelFlush` switch remains available for loose GLTF profiles; the two
completeness switches are intentionally incompatible.
The cooked contract also requires external anchors so a self-consistent different
model cannot pass: supply `-ExpectedCookedModelLogicalPath` with the normalized logical
path and `-ExpectedCookedModelDebugName` with the original request/debug name. For the
Silver runner these are respectively
`Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf` and
`Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf`.

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

Run the glTF prepared smoke:

```powershell
.\Scripts\RunCookedTextureGameSmoke.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe `
  -SpecPath .\Assets\AssetSets\Rendering3DTestSilverGltfTextures.json `
  -ModelPath Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf `
  -ExpectedLoadMode GltfPrepared
```

Run the alternating three-pair real-Game model profile:

```powershell
.\Scripts\RunCookedModelGameProfile.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe
```

The default order is `loose,cooked`, `cooked,loose`, `loose,cooked`. Every run gets its
own working directory, `Game.log`, stdout, stderr, and generated summary beneath the
strict child `build/CookedModelGameProfile/Debug/`. `comparison.json` records all
samples and medians together with HEAD/tree, dirty status, tracked-diff and sorted
untracked hashes, input/output paths, configuration, order, timestamps, OS, and GPU.
The runner treats missing logs/markers, skips, timeouts, nonzero exits, contract
failures, or a non-shorter cooked median as failures and retains the evidence.

Generated packages, manifests, and smoke logs are build outputs. Keep them under `build/` or other ignored output locations and do not commit them.
