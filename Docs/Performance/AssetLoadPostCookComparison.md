# Asset Load Post-Cook Comparison

This document records scoped post-AssetCook validation runs for texture loading. It links back to the pre-AssetCook baseline in [AssetLoadBaseline.md](AssetLoadBaseline.md), but the data here is not a full engine-wide replacement baseline.

Exact millisecond values below are Debug smoke-run comparison data from this machine/configuration. They are useful for checking which runtime stages are present or absent, not as absolute performance targets.

## Scope

The comparison covers two texture paths:

- Direct cooked Silver texture loading through the runtime cooked texture path.
- glTF fixture-level prepared texture loading for `Rendering3DTestSilverGltf`.

This does not include cooked model data. Runtime glTF JSON parse, buffer read, mesh extract, clusterize, model finalization, and render-world model flush can still remain when the model itself is loaded from loose glTF.

Model cook and packed ARM cook are deferred future phases. The glTF prepared run below validates the texture path for a fixture; it is not a full cooked-model baseline.

## Direct Cooked Silver Texture Path

Command:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/RunCookedTextureGameSmoke.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe -GameExe build/Game/Debug/Game.exe
```

Observed smoke result:

| item | value |
| --- | --- |
| cooked source logs | 5 |
| cooked upload logs | 5 |
| prepared upload logs | 0 |
| prepared finalize logs | 0 |
| prepared split logs | 0 |
| Silver cooked source | `source=cooked_nvtex` |

Stage summary from the Debug smoke run:

| stage | count | total_ms | avg_ms |
| --- | --- | --- | --- |
| `texture_cooked_parse` | 5 | 179.632 | 35.926 |
| `texture_cooked_upload` | 5 | 111.154 | 22.231 |

Compared with the pre-AssetCook Silver rows in [AssetLoadBaseline.md](AssetLoadBaseline.md), the cooked Silver targets no longer use `source=loose_stbi` and should not pay runtime `texture_async_worker` image decode for those cooked texture assets.

This direct smoke still includes startup Boulder glTF loose image read/decode because model cook and startup-scene cooked model wiring are not part of this path.

## glTF Prepared Texture Path

Command:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/RunCookedTextureGameSmoke.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe -GameExe build/Game/Debug/Game.exe -SpecPath Assets/AssetSets/Rendering3DTestSilverGltfTextures.json -ModelPath Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf -ExpectedLoadMode GltfPrepared
```

Observed smoke result:

| item | value |
| --- | --- |
| cooked source logs | 3 |
| cooked upload logs | 0 |
| prepared upload logs | 2 |
| prepared finalize logs | 2 |
| prepared split logs | 1 |
| glTF model `prepared_textures` | 2 |
| glTF model `loose_texture_bytes` | 12582912 |

Stage summary from the Debug smoke run:

| stage | count | total_ms | avg_ms |
| --- | --- | --- | --- |
| `texture_prepare_cooked_parse` | 3 | 143.909 | 47.970 |
| `texture_prepare_package_parse` | 3 | 202.007 | 67.336 |
| `texture_prepare_package_read` | 3 | 126.341 | 42.114 |
| `texture_prepared_cooked_upload` | 2 | 25.369 | 12.685 |
| `gltf_texture_staging` | 1 | 681.980 | 681.980 |
| `gltf_staging_total` | 1 | 1978.117 | 1978.117 |

Texture source rows are `source=cooked_nvtex` for the model-local albedo, normal, and ARM prepared texture assets. The ARM asset uses `texture_prepared_split`, so the packed source can feed separate material channels at finalize time.

For these cooked model-local texture paths, the prepared flow should avoid `gltf_image_read`, `gltf_image_decode`, and `source=loose_stbi`. Remaining glTF model work is expected until model cook is implemented.

## Fallback And stb_image Decision

Runtime cooked texture loading bypasses `stb_image`. The library remains useful for cook-time source decode and loose/debug fallback paths.

Replacing `stb_image`, or writing self-owned PNG/JPEG decode, is deferred. It should be revisited only if future profiling shows cook-time decode dominates AssetCook time, or loose/debug fallback decode remains important enough to justify the dependency or maintenance cost. Replacing it would not improve the current hot runtime cooked texture path because that path already reads the cooked texture payload directly.

## Follow-Up Scope

- Add cooked model data before treating glTF startup loading as a full cooked asset baseline.
- Add packed ARM cook if material bandwidth/capacity profiling shows it is worth handling as a separate optimization.
- Keep raw `Game.log`, redirected output, cooked packages, manifests, and generated summaries out of commits unless a future test fixture intentionally needs them.
