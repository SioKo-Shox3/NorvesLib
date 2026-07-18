# Asset Load Post-Cook Comparison

This document records scoped post-AssetCook validation runs for texture loading. It links back to the pre-AssetCook baseline in [AssetLoadBaseline.md](AssetLoadBaseline.md), but the data here is not a full engine-wide replacement baseline.

Exact millisecond values below are Debug smoke-run comparison data from this machine/configuration. They are useful for checking which runtime stages are present or absent, not as absolute performance targets.

## Scope

The comparison covers two texture paths and one model-path pair:

- Direct cooked Silver texture loading through the runtime cooked texture path.
- glTF fixture-level prepared texture loading for `Rendering3DTestSilverGltf`.
- The same `Rendering3DTestSilverGltf` model loaded through the compatible loose
  glTF path and through the explicitly opted-in cooked `NVMESHv0` path.

The texture-only rows remain useful for their original scoped checks. The cooked-model
comparison later in this document is the M4 post-cook model measurement; it is not a
general engine benchmark or a release-build performance target.

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

This direct texture smoke still uses the normal startup model path unless the dedicated
cooked-model opt-in is supplied.

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

For these cooked model-local texture paths, the prepared flow should avoid `gltf_image_read`, `gltf_image_decode`, and `source=loose_stbi`. The flagless compatibility path still performs glTF model work; the dedicated cooked-model opt-in below bypasses those model stages.

## Cooked Model Real-Game Comparison

The opt-in comparison uses one four-entry runtime manifest: the three texture entries
from `Rendering3DTestSilverGltfTextures.json` plus the model entry
`Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf` whose cooked entry is
`Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.nvmesh`. Both sides receive
the same asset root, manifest, model request, and cooked model-local textures. The loose
side omits the opt-in flag and therefore remains on `GLTFAnalyzer` with prepared
textures; the cooked side adds `--rendering3dtest-use-cooked-model` and enters
`MegaGeometryResources::LoadModelAsync`.

Command:

```powershell
.\Scripts\RunCookedModelGameProfile.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe
```

The runner alternates order to reduce a fixed warm-up bias:
`loose,cooked`, `cooked,loose`, `loose,cooked`. Model-ready latency is the difference
between the exact logger timestamps on the single expected-path
`Boulder model async load started: ...` line and the later single
`Boulder model loaded and added to World` line. Process wall time is retained as an
auxiliary diagnostic and is not substituted for model-ready latency.

| pair/order | path | model-ready ms | wall ms |
| --- | --- | ---: | ---: |
| 1/1 | loose | 10069 | 11516.874 |
| 1/2 | cooked | 9545 | 10999.586 |
| 2/1 | cooked | 9680 | 11131.43 |
| 2/2 | loose | 10098 | 11522.438 |
| 3/1 | loose | 9981 | 11397.864 |
| 3/2 | cooked | 10069 | 11652.719 |

| path | samples (model-ready ms) | median ms |
| --- | --- | ---: |
| loose glTF + prepared textures | 10069, 10098, 9981 | 10069 |
| cooked NVMESH + same textures | 9545, 9680, 10069 | 9680 |

For this captured Debug run, the cooked median is shorter. The completeness contract
requires the correlated worker `model_asset_resolve` and `model_cooked_parse` records,
all four correlated `model_finalize_*` records, the matching-debug GPU upload, and a
successful non-empty async flush. It rejects every legacy `gltf_*` model stage on the
cooked side. These checks intentionally contain no numeric performance threshold.

### M4 Closure Acceptance

Current measurement provenance is
`build/CookedModelGameProfile/M4Closure-2d59d56e/comparison.json`, generated
`2026-07-18 19:49:12.337`. It records HEAD
`2d59d56efe78fffccf7fff34cecc2efa9441fffe`, tree
`e7158f8d4d5642d050a8727896310b6b6092c2ea`, `dirty=false`, and `status=[]`.
The configuration used three rendered frames after asset settle, a 600-second timeout,
and three ordered pairs: `loose,cooked`, `cooked,loose`, `loose,cooked`.

| acceptance item | result |
| --- | --- |
| build | `AssetCook`, `Game`, and `ALL_BUILD` PASS; CTest 137/137 PASS |
| strict real-Game profile | 3 pairs PASS; strict cooked 3/3 PASS |
| controlled address-binding gate | `build/Diagnostics/AddressBindingFaultGate-20260718-194943`; 48/48 PASS (24/24 per mode), 0 faults, 115344 binds and 115344 unbinds |
| overlay coexistence | `build/Diagnostics/OverlayCohab-20260718-200110`; 1/1 PASS, Game exit 0, ExceptionStream absent, Event153=0; present modules: `graphics-hook64.dll`, `ow-graphics-vulkan.dll`, `ow-graphics-hook64.dll`, `nvspcap64.dll` |

The measured `Game.exe` SHA-256 was
`360f17d9599f01da0c9be9e961d589d2361361bee2e19341d7bf17ea01c38482`; the
`AssetCook.exe` SHA-256 was
`a1d401ebfecce61b05d04775a7da0e77efbb68a3f2278b8ca400c5e4157f7cc5`.
The host was `Microsoft Windows NT 10.0.22631.0` with Meta Virtual Monitor driver
`17.12.55.198`, NVIDIA GeForce RTX 4080 driver `32.0.15.9186`, and Virtual Desktop
Monitor driver `13.50.53.699`.

**M4 status: CLOSED (2026-07-18).** The recorded acceptance above and CADENCE
repayment are complete.

#### Known accepted risks

Both risks are non-blocking and do not reopen M4.

1. `InstanceBufferRing` uses `uint32` instance/capacity arithmetic, so a theoretical
   `>UINT32_MAX` instance / roughly 576 GiB overflow/narrowing boundary exists. A
   read-only audit found no reachable independent overflow at current scale; normal
   allocation/bounds checks fail earlier, and permanent `VK_EXT_device_fault` plus
   device-address-binding instrumentation remains the tripwire. No new max/failure API
   is added because it would be a behavior/RHI design change disproportionate to the
   current risk.
2. The profile allowlist accepts any positive ASCII decimal physical source line via
   `[1-9][0-9]*`. Leading-zero and digit-count restrictions intentionally remain
   unspecified because records are internal logger output, and should be revisited only
   if external/untrusted logs become input.

Raw logs, summaries, packages, manifests, and diagnostic artifacts remain under
`build/` only and are not committed.

## Fallback And stb_image Decision

Runtime cooked texture loading bypasses `stb_image`. The library remains useful for cook-time source decode and loose/debug fallback paths.

Replacing `stb_image`, or writing self-owned PNG/JPEG decode, is deferred. It should be revisited only if future profiling shows cook-time decode dominates AssetCook time, or loose/debug fallback decode remains important enough to justify the dependency or maintenance cost. Replacing it would not improve the current hot runtime cooked texture path because that path already reads the cooked texture payload directly.

## Follow-Up Scope

- Add packed ARM cook if material bandwidth/capacity profiling shows it is worth handling as a separate optimization.
- Repeat the model-ready comparison in Release and on representative target hardware
  before using it for product performance claims.
- Keep raw `Game.log`, redirected output, cooked packages, manifests, and generated summaries out of commits unless a future test fixture intentionally needs them.
