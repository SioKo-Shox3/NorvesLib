# RenderGraph Phase0 Baseline

Date: 2026-06-17 JST

## Scope

`Docs/Plans/RenderGraphWorkPlan.md` のフェーズ0として、RenderGraph 改修前の `develop` 状態を固定した。
このフェーズでは描画コード、Public API、RenderGraph 実装は変更していない。

## Git Baseline

- Branch before work: `develop`
- Working branch: `codex/rendergraph-phase0-baseline`
- `HEAD`: `b3b2daecadae17db0e0a20fd16ea7f77562b705d`
- `origin/develop`: `b3b2daecadae17db0e0a20fd16ea7f77562b705d`
- `git fetch origin` 後、`develop` と `origin/develop` の差分なし。

## Build And Test

| Item | Result | Notes |
|---|---:|---|
| `cmake --build build --config Debug --target Game` | Passed | 初回は `Game` と `RenderGraphCompileTest` を同時ビルドして同一 `Core` obj 生成の `Permission denied` に当たったため、以後はシリアル実行。 |
| `cmake --build build --config Debug --target RenderGraphCompileTest` | Passed | |
| `ctest --test-dir build -C Debug -R RenderGraphCompileTest --output-on-failure` | Passed | 1/1 |
| `ctest --test-dir build -C Debug --output-on-failure` | Passed | 58/58 |
| ST 確認後の `cmake --build build --config Debug --target Game` | Passed | 一時 ST 差分を戻した後、既定 MT バイナリへ戻すため再ビルド。 |

Build warning notes:

- `VulkanDevice.h` の `NOMINMAX` 再定義 warning。
- `VulkanDevice.cpp` の `[[nodiscard]]` 戻り値破棄 warning。
- libwebsockets `GENHDR` の MSB8065 warning。
- shaderc / SPIRV-Tools / glslang の PDB 不在による LNK4099 warning。
- Configure 時に `SLANG_SDK_DIR` 未設定のため Slang SDK は無効。

## Game Baseline

共通条件:

- Executable: `build/Game/Debug/Game.exe`
- Scene: `Rendering3DTest`
- Window: 1280 x 720
- Default model: `Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf`
- Trace option: `--trace --trace-file=<absolute path>`
- Screenshot timing: `PostProcessStack initialized`, `GBuffer resources created`, and trace `DrawCalls > 0` を確認後、約 5 秒待って撮影。

### Multi Threaded Rendering

Artifacts:

- Screenshot: `screenshots/rendering3dtest_mt.png`
- Trace sample: `traces/game_mt_trace.csv`
- Log: `logs/game_mt_log.txt`
- Run summary: `logs/game_mt_run_summary.txt`

Run result:

- Exit code: 0
- `RenderThread started (multi-threaded rendering enabled)` を確認。
- `PostProcessStack initialized (5 passes)` と `GBuffer resources created (1280x720)` を確認。
- Boulder model loaded and added to World を確認。
- Full raw trace は 129,225 frame rows / 12,266,277 bytes だったため、コミット対象は末尾 2,000 frame rows のサンプルに縮約した。

Trace values from committed sample:

| Metric | Value |
|---|---:|
| Frame rows | 2,000 |
| Last nonzero frame | 129,215 |
| Last DrawCalls | 18 |
| Last Triangles | 2,184 |
| Last GameThreadMs | 0.0463 |
| Last RenderPrepareMs | 0.024 |
| Last RenderThreadMs | 5.0982 |
| Last RenderFrameMs | 5.0878 |
| Last GPUFrameMs | 3.1248 |
| Max DrawCalls in sample | 24 |
| Max Triangles in sample | 2,184 |

### Single Threaded Rendering

ST は現時点で Game 起動引数が無いため、`Library/Core/Private/Engine/ApplicationProcessor.cpp` に `renderSettings.bEnableMultiThreadedRendering = false;` を一時的に追加して確認した。
確認後、この一時差分は戻し、`git diff --numstat` と `git diff --ignore-cr-at-eol --numstat` がともに空であることを確認した。

Artifacts:

- Screenshot: `screenshots/rendering3dtest_st.png`
- Trace: `traces/game_st_trace.csv`
- Log: `logs/game_st_log.txt`
- Run summary: `logs/game_st_run_summary.txt`

Run result:

- Exit code: 0
- `RenderThread started` は出力されないことを確認。
- `PostProcessStack initialized (5 passes)` と `GBuffer resources created (1280x720)` を確認。
- Boulder model loaded and added to World を確認。

Trace values:

| Metric | Value |
|---|---:|
| Frame rows | 820 |
| Last nonzero frame | 819 |
| Last DrawCalls | 12 |
| Last Triangles | 2,184 |
| Last GameThreadMs | 6.4489 |
| Last RenderPrepareMs | 0.0515 |
| Last RenderThreadMs | 0 |
| Last RenderFrameMs | 6.2999 |
| Last GPUFrameMs | 2.57747 |
| Max DrawCalls | 14 |
| Max Triangles | 4,104 |

## Validation And Errors

Search pattern:

```powershell
ERROR|WARNING|Validation|VUID|RenderGraph compile failed|RenderGraph execution failed
```

Results:

- Vulkan validation / VUID: no matches in MT or ST logs.
- RenderGraph compile/execution failure: no matches in MT or ST logs.
- MT and ST each contain the same 2 Slang-related errors because `SLANG_SDK_DIR` was not set:
  - `Cannot compile [neural_material_decode.slang]: Slang SDK not available.`
  - `Failed to compile shader [neural_material_decode.slang]: Slang SDK not available.`

## RenderGraph Pass Count

Current success-path logs do not emit `RenderGraph::GetLastExecutedPassCount()`.
The count is stored by `RenderGraph::Execute()` and exposed through `GetLastExecutedPassCount()`, but `View::Render()` only includes it in the failure log path.
Therefore Phase0 records the expected pass composition from source and leaves exact runtime pass count as not directly logged.

Expected default deferred pipeline from `SceneView::SetupDeferredPipeline()`:

1. `ShadowMapPass`
2. `NeuralMaterialDecodePass`
3. `GBufferPass`
4. `MegaGeometryPass`
5. `SSAOPass`
6. `LightingPass`
7. `ForwardPass` with transparent-only enabled
8. `SSRPass`
9. `BloomPass`
10. `ToneMappingPass`
11. `FXAAPass`
12. `UpscalePass`

## Native And Legacy Pass Inventory

Production passes that directly implement `IRenderGraphPass`:

| Pass | Evidence |
|---|---|
| `ShadowMapPass` | `Library/Core/Public/Rendering/ShadowMapPass.h:51` |
| `NeuralMaterialDecodePass` | `Library/Core/Public/Rendering/NeuralMaterialDecodePass.h:28` |
| `GBufferPass` | `Library/Core/Public/Rendering/GBufferPass.h:68` |
| `MegaGeometryPass` | `Library/Core/Public/Rendering/MegaGeometryPass.h:45` |
| `SSAOPass` | `Library/Core/Public/Rendering/SSAOPass.h:44` |
| `LightingPass` | `Library/Core/Public/Rendering/LightingPass.h:63` |
| `ForwardPass` | `Library/Core/Public/Rendering/ForwardPass.h:43` |
| `SSRPass` | `Library/Core/Public/Rendering/SSRPass.h:58` |
| `BloomPass` | `Library/Core/Public/Rendering/BloomPass.h:53` |
| `ToneMappingPass` | `Library/Core/Public/Rendering/ToneMappingPass.h:105` |
| `FXAAPass` | `Library/Core/Public/Rendering/FXAAPass.h:43` |
| `UpscalePass` | `Library/Core/Public/Rendering/UpscalePass.h:27` |

Legacy adapter:

- `LegacyViewPassAdapter`: `Library/Core/Public/Rendering/RenderGraph/LegacyViewPassAdapter.h:10`

Selection path:

- `RenderingCoordinator::Initialize()` calls `SceneView::SetupDeferredPipeline()`: `Library/Core/Private/Rendering/RenderingCoordinator.cpp:535`
- Default pass composition is built in `SceneView::SetupDeferredPipeline()`: `Library/Core/Private/Rendering/SceneView.cpp:392-519`
- `View::Render()` uses `dynamic_cast<IRenderGraphPass*>` and adds native passes directly: `Library/Core/Private/Rendering/View.cpp:203`
- Non-native `IViewPass` instances are wrapped by `LegacyViewPassAdapter`: `Library/Core/Private/Rendering/View.cpp:193-223`
- PostProcessStack children use the same native/adapter split: `Library/Core/Private/Rendering/View.cpp:225-269`

Current default deferred pipeline is fully native; `LegacyViewPassAdapter` remains fallback for non-native custom passes or legacy stacks.

## Notes For Later Phases

- `GetLastExecutedPassCount()` should be surfaced in a normal debug dump or stats path in a later phase.
- ST rendering currently requires a local source toggle; adding a debug-only runtime switch would make future regression checks safer.
- Slang SDK remains optional. NeuralMaterialDecode reports errors without `SLANG_SDK_DIR`, but the representative scene still renders.
