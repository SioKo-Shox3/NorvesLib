# NorvesLib 2D Rendering F0 Baseline

Date: 2026-06-21

Context: read-only F0 baseline for `Docs/Plans/2d-rendering-subsystem-plan.md`. This document records the current code/runtime state before F1-F4 work. It is not a replacement for the canonical plan.

## Gates

The baseline build/test gate is green for compile and unit tests:

| Gate | Result | Notes |
|------|--------|-------|
| Configure | Pass | `cmake -B build -S . -G "Visual Studio 17 2022"` succeeded. Slang SDK was not found; this is an optional neural-shader warning. |
| Build | Pass | `cmake --build build --config Debug --target Game` succeeded. Build emitted the known libwebsockets generated-header incremental warning. |
| CTest | Pass | `ctest --test-dir build -C Debug` passed `79/79`. |

Runtime smoke is not green; see the next section.

## Runtime Smoke Status

MT is the only current boot path exposed by Game config. `ApplicationProcessor` exposes `--exit-after-frames`, but no command-line switch for ST rendering was found. `RenderWorldSettings::bEnableMultiThreadedRendering` defaults to `true`, and `ApplicationProcessor` creates `RenderWorldSettings` without overriding it.

F0 runtime/visual evidence is incomplete. MT `--exit-after-frames` runs reached their requested frame targets, but the runner/PowerShell process result returned access violation `-1073741819` after shutdown; this exit code is not emitted inside the game log. ST startup could not be exercised because no Game CLI switch was found. No Normal representative screenshot, Vulkan validation summary, DrawCalls summary, or PSO count was captured. The F0 runtime/visual gate is blocked unless this is fixed or explicitly waived.

Runtime commands captured in the logs:

```powershell
C:\Users\KINGkawamura\Documents\NorvesLib\build\Game\Debug\Game.exe --exit-after-frames=10000 --trace --trace-file=C:\Users\KINGkawamura\Documents\NorvesLib\Docs\Plans\assets\2d-rendering\F0_runtime_mt_10000_trace.csv
C:\Users\KINGkawamura\Documents\NorvesLib\build\Game\Debug\Game.exe --exit-after-frames=1000 --trace --trace-file=C:\Users\KINGkawamura\Documents\NorvesLib\Docs\Plans\assets\2d-rendering\F0_runtime_mt_visible_1000_trace.csv
```

`F0_runtime_mt_trace.csv` exists, but no paired log in this baseline records its full command line.

Artifacts are under `Docs/Plans/assets/2d-rendering/`:

| Artifact | Purpose |
|----------|---------|
| `F0_runtime_mt_trace.csv` | Baseline MT trace. |
| `F0_runtime_mt_10000.log` | MT run with `--exit-after-frames=10000`; log reaches frame 10000. |
| `F0_runtime_mt_10000_trace.csv` | Trace for the 10000-frame MT run. |
| `F0_runtime_mt_visible_1000.log` | Visible MT run with `--exit-after-frames=1000`; log reaches frame 1000. |
| `F0_runtime_mt_visible_1000_trace.csv` | Trace for the visible 1000-frame MT run. |

`Docs/Plans/` is ignored by `.gitignore`, so these logs/traces are local ignored files unless explicitly force-added. Do not assume they will be committed by default.

The runtime logs also show Slang shader compilation errors when neural material decode is attempted without Slang SDK. This matches the optional configure warning and should not be conflated with the shutdown access violation.

## Confirmed Baseline Anchors

### Entity Tree And Component Data

The entity tree work has landed:

- `World::SpawnEntity(parent)` exists and is exercised by `EntityTreeTest`.
- `Entity` owns child entities and components via Inner ownership.
- Transform update, tick, destroy, and render sync recurse through child entities.
- `EntityTreeTest`, `ComponentDataRegistryTest`, and related tests are registered.

`ComponentDataRegistry` has landed but is opt-in/debug enabled. It is enabled by compile definition for Debug/RelWithDebInfo, can be runtime-disabled, and currently publishes transform, mesh, and mega-geometry data only. There is no Board output path yet.

### World Sync

`World::SyncToSceneView` is the GameThread sync anchor at `Library/Core/Private/Object/World.cpp:834-873`.

- Component data capture starts at `841-848`.
- World transforms update at `850`.
- Live sets are currently `liveMeshObjectIds`, `liveMegaGeometryObjectIds`, and `liveLightIds` at `852-854`.
- Stale removal is mesh/mega/light only at `870-872`.

`World::SyncEntityRecursive` is the per-entity recursion anchor at `Library/Core/Private/Object/World.cpp:921-1062`.

- Mesh dispatch: `943-984`.
- Mega-geometry dispatch: `986-1024`.
- Light dispatch: `1026-1047`.
- Child recursion: `1051-1061`.

`MeshComponent` is the closest component/proxy template:

- `RefreshRenderTransformCache` and `BuildMeshProxy`: `Library/Core/Private/Component/MeshComponent.cpp:177-247`.
- Matrix translation convention: `Library/Core/Private/Component/MeshComponent.cpp:325-345`; rendering consumes translation from `m30/m31/m32`, and `m03/m13/m23` are cleared.

Existing `SceneView` proxy stores are not ready for multiple same-kind components per entity:

- Mesh and mega-geometry stores are ObjectId-keyed only.
- Lights are ComponentId-keyed through `LightId`.
- Plan mismatch to resolve before Board work: if multiple Board components per Entity are expected, Board storage/stale-removal should explicitly use an ObjectId + ComponentId key rather than copying mesh/mega's ObjectId-only pattern.

### Missing 2D Features

The following are not implemented in source code and remain F1-F4/F6 targets:

- `BoardComponent`
- `BoardProxy`
- `BoardSpace`
- `CanvasView`
- `CompositePass`
- `Canvas.Color`
- `Composite.Color`
- `RenderingCoordinator::RegisterCamera` / `FindCamera` camera table
- `Viewport` `CameraId` reference
- `RenderFrameExecutor` two-stage execution
- `ProceduralMeshGenerator::GenerateQuad2D`
- `UVRect`

`BlendMode` already exists in `Library/Core/Public/Rendering/MaterialTypes.h:198`.

### Rendering And Frame Flow

Current rendering is still single-stage per viewport:

- `View::Render` resets the render graph at `Library/Core/Private/Rendering/View.cpp:195`.
- Base `View::Render` appends `PresentationGraphPass` at `View.cpp:281-284`; `CanvasView` must fully override base `Render` to suppress presentation.
- `PresentationPass::Declare` currently prioritizes only `PresentationColor`, then `ToneMappedColor` (`Library/Core/Private/Rendering/PresentationPass.cpp:22-40`). It does not try `Composite.Color`.
- `RenderGraphResourceNames` has no Canvas or Composite names (`Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h:7-20`).
- `RenderFrameExecutor::Execute` is a single-stage view/viewport loop at `Library/Core/Private/Rendering/RenderFrameExecutor.cpp:29-75`, with fallback at `77-96`.
- `RenderingCoordinator::GenerateDrawCommands` has the view loop at `Library/Core/Private/Rendering/RenderingCoordinator.cpp:802-907`.
- `BuildViewportRenderPlan` copies camera/debug/rect data at `RenderingCoordinator.cpp:40-115`.
- The current `SceneView` branch is `RenderingCoordinator.cpp:872-900`; future `CanvasView` logic should sit as a parallel branch after `BuildViewportRenderPlan` at `865-870`.
- `FramePacket` remains flat: `DrawCommands`, `InstanceData`, and `Views` live at `Library/Core/Public/Rendering/FramePacket.h:59-79`.
- Draw/instance rebasing is centralized in `RenderingCoordinator.cpp:117-166`: `AppendInstanceDataToPacket` appends view-local `InstanceData` and returns `baseInstance`; `AppendRebasedDrawCommands` appends draw commands and rebases graphics `FirstInstance` and `InstanceDataOffset` by that base.
- The `FramePacket` state machine is `Empty -> Writing -> Ready -> Queued/Reading -> Recycling -> Empty` at `Library/Core/Public/Rendering/FramePacket.h:17-24`, with write/read transitions in `AcquireForWrite`, `FinishWrite`, `AcquireForRead`, and `FinishRead` (`205-240`, `268-325`). `RenderingCoordinator::RenderFrame` also accepts `Queued` or `Ready` packets by CAS to `Reading` before rendering (`RenderingCoordinator.cpp:954-959`) and releases through `FinishRead` (`1235-1240`).

`View` override impact for `CanvasView`: base `View` owns `m_Viewports`, `m_OutputTexture`, `m_Passes`, `m_PostProcessStack`, `m_bEnabled`, and `m_bInitialized` (`Library/Core/Public/Rendering/View.h:256-281`). A full `CanvasView::Render` override must preserve the necessary setup/state/output behavior for these members while avoiding the base presentation append at `View.cpp:281-284`.

### Camera And Viewport

`CameraProxy` already has `CameraId` and `CullingMask` (`Library/Core/Public/Rendering/SceneProxy.h:257-280`), but no shared camera table uses `CameraId` yet.

`RenderLayer` and `HasFlag` exist in `Library/Core/Public/Rendering/RenderTypes.h:193-217`. No current SceneView culling path applies `RenderLayer x CameraProxy::CullingMask` logic; F3 must add the Board routing filter intentionally.

`Viewport` currently owns a `CameraProxy` value. It does not reference a shared `CameraId`. `ViewportRenderPlan::DebugMode` is landed at `Library/Core/Public/Rendering/ViewRenderPlan.h:17-62`.

### Math And 2D

`ProceduralMeshGenerator::GeneratePlane` uses clockwise indices `i0,i1,i2` and `i1,i3,i2` (`Library/Core/Public/Rendering/ProceduralMeshGenerator.h:201-213`). Active 3D passes use `FrontFace::Clockwise` for GBuffer/Forward/Mega/Shadow. F3 `GenerateQuad2D` should therefore follow the engine Clockwise convention unless culling is deliberately disabled or overridden for the 2D pipeline.

`VertexLayout` has position-only, standard, extended, skinned, and color layouts, but no 2D preset. F3 should add an explicit 2D layout rather than reusing a 3D layout accidentally.

`MatrixUtils::CreateOrthographic(width, height, near, far)` is centered and has no left/top translation (`Library/Core/Public/Math/MatrixUtils.h:381-401`). `Viewport::UpdateMatrices` ignores `CameraProxy::OrthoHeight` and derives height from `OrthoWidth / aspect` (`Library/Core/Private/Rendering/Viewport.cpp:117-127`; same pattern in `CameraViewConstants.cpp:71-77`).

For ScreenSpace px top-left semantics, F2/F3 must choose one of:

- add an off-center orthographic helper, or
- keep the centered orthographic helper and apply an explicit shader/transform offset such as `x - W/2` plus a chosen Y convention.

Existing shaders use `projection * view * worldPos`. View/projection matrices are transposed before GLSL upload, while world instance matrices are copied directly after `MeshComponent` places translation in `m30/m31/m32`.

`VariableArray` derives from `std::vector`, so `std::stable_sort` works on it. Existing `DrawCommandSorter` uses non-stable `std::sort` at `Library/Core/Private/Rendering/DrawCommand.cpp:364-425`; Board ordering must use Canvas-specific `std::stable_sort`, not the existing sorter.

Recommended Board sort key: `uint64_t(uint64_t(LayerPriority) << 32) | OrderInLayer`, with stable tie by append index.

### Shader And Encoding Conventions

`NORVES_SHADER_DIR` is injected by `Library/Core/CMakeLists.txt:461-473`.

The Vulkan shader compiler strips UTF-8 BOM before shaderc (`Library/Core/Private/RHI/Vulkan/VulkanShaderCompiler.cpp:134-145`), and the Slang compiler has the same BOM skip (`VulkanSlangCompiler.cpp:242-251`). Even so, keep the plan convention for new shaders: UTF-8 + CRLF without BOM.

This baseline document is plain UTF-8 markdown with no BOM requirement.

### Docs Cleanup

No old 2D plan files were found under `Docs/Plans/`. There is no `Docs/Archive/` directory. Stale `LayerView` / component-bypass wording appears only in the canonical plan history/transition text, not as a separate old plan.

## Decisions For F1-F4

### F1: Composition Skeleton

- Add `Canvas.Color` and `Composite.Color` named resources.
- Add `CanvasView`, but make it fully override base `View::Render` so it does not append the presentation pass.
- Add `CompositePass` as a second-stage pass that imports physical output textures from View-owned render targets, not named resources from the per-View graph.
- Extend `PresentationPass` priority to `Composite.Color -> PresentationColor -> ToneMappedColor`.
- Split `RenderFrameExecutor` into stage 1 View rendering and stage 2 composite/presentation, with a fallback that preserves the current single SceneView path when composition is not needed.

### F2: Shared Camera And Screen-Space Ortho

- Introduce a `RenderingCoordinator` camera table using `CameraProxy::CameraId`; keep `ViewportRenderPlan.Camera` as a per-frame value copy for RenderThread safety.
- Add `Viewport` `CameraId` reference while preserving current value-owned camera behavior for compatibility.
- Do not treat existing `CullingMask` as implemented routing. The mask-crossing filter must be added explicitly where Board draw commands are generated.
- Decide ScreenSpace convention before shader work: top-left pixel coordinates require either off-center ortho or centered ortho plus explicit offset/Y handling.

### F3: Board Minimum Path

- Add `BoardComponent`, `BoardProxy`, and `BoardSpace`.
- Reuse the MeshComponent proxy-building pattern, but do not copy SceneView's ObjectId-only mesh/mega keying if multiple Board components per Entity are valid. Prefer an ObjectId + ComponentId key.
- Insert Board sync at `World::SyncEntityRecursive` near the existing mesh/mega/light dispatch.
- Insert `CanvasView` command generation as a sibling branch to the existing `SceneView` branch in `RenderingCoordinator::GenerateDrawCommands`.
- Add `GenerateQuad2D` with engine Clockwise winding unless the 2D pipeline intentionally disables/overrides culling.
- Add an explicit 2D vertex layout and Board shaders using the decided ScreenSpace convention.

### F4: Multiple Boards And Stable Ordering

- Keep Board painter ordering out of `DrawCommandSorter`; that sorter is non-stable and also optimizes for 3D sorting concerns.
- In Canvas-specific command prep, use `std::stable_sort` over `VariableArray`.
- Use sort key `(uint64_t(LayerPriority) << 32) | OrderInLayer`.
- Preserve ties by append index for deterministic same-key order.

## Plan-Vs-Code Mismatches And Risks

| Item | Status / risk | Follow-up |
|------|---------------|-----------|
| Runtime smoke | MT reaches frame targets but exits with access violation `-1073741819` after shutdown. | Treat as blocker/risk; do not call F0 runtime green. |
| ST smoke path | ST render path exists internally when `bEnableMultiThreadedRendering=false`, but Game config exposes no CLI switch. | Add or document an ST test path before future MT/ST screenshot gates rely on it. |
| Runtime/visual evidence | No Normal representative screenshot, Vulkan validation summary, DrawCalls summary, or PSO count was captured. | F0 runtime/visual gate remains blocked unless fixed or explicitly waived. |
| Ignored artifacts | `Docs/Plans/` is ignored, including the local F0 logs/traces. | Force-add artifacts if they must be committed, or store accepted evidence elsewhere. |
| Quad winding | Canonical plan text still mentions CCW in places, but current engine plane and active 3D passes use Clockwise. | F3 should use Clockwise unless culling is intentionally disabled/overridden. |
| Board keying | Existing mesh/mega stores are ObjectId-keyed only; lights are ComponentId-keyed. | Board storage must explicitly decide ObjectId + ComponentId if same Entity can own multiple Boards. |
| Culling mask | `CameraProxy::CullingMask` and `RenderLayer` exist, but no current SceneView mask filter was found. | F3 adds explicit Board routing; do not assume this is already implemented. |
| Orthographic px space | Current ortho is centered and ignores `OrthoHeight`. | F2/F3 must define top-left px origin and Y direction. |
| Frame flow | `RenderFrameExecutor` and `FramePacket` are flat/single-stage today; draw/instance rebasing assumes one flat packet array. | F1 must preserve legacy fallback and rebase correctness while adding composition. |
| CanvasView override | Base `View::Render` handles graph reset/pass execution/presentation append over shared View members. | CanvasView must preserve required setup/output state while suppressing presentation. |
| Shader encoding | Compiler strips BOM, but plan asks new shaders to be BOM-less UTF-8+CRLF. | Follow the plan to avoid shaderc/editor ambiguity. |

## Source References

| Topic | Source |
|-------|--------|
| Canonical 2D plan | `Docs/Plans/2d-rendering-subsystem-plan.md` |
| Build/runtime artifacts | `Docs/Plans/assets/2d-rendering/` |
| Runtime option parsing | `Library/Core/Private/Engine/ApplicationProcessor.cpp:26`, `152-169`, `459-465` |
| RenderWorld MT default/path | `Library/Core/Public/Rendering/RenderWorld.h:43-55`, `Library/Core/Private/Rendering/RenderWorld.cpp:40-76`, `248-273` |
| RenderWorld settings construction | `Library/Core/Private/Engine/ApplicationProcessor.cpp:208-224` |
| World sync | `Library/Core/Private/Object/World.cpp:834-873`, `921-1062` |
| Mesh proxy template | `Library/Core/Private/Component/MeshComponent.cpp:177-247` |
| Mesh matrix convention | `Library/Core/Private/Component/MeshComponent.cpp:325-345` |
| SceneView keying | `Library/Core/Public/Rendering/SceneView.h:139-155`, `Library/Core/Private/Rendering/SceneView.cpp:85-179`, `270-298` |
| Render graph reset/presentation append | `Library/Core/Private/Rendering/View.cpp:195`, `281-284` |
| View member state affected by override | `Library/Core/Public/Rendering/View.h:256-281` |
| Presentation priority | `Library/Core/Private/Rendering/PresentationPass.cpp:22-40` |
| RenderFrameExecutor loop/fallback | `Library/Core/Private/Rendering/RenderFrameExecutor.cpp:29-96` |
| Render command view loop | `Library/Core/Private/Rendering/RenderingCoordinator.cpp:802-907` |
| Draw/instance rebasing helpers | `Library/Core/Private/Rendering/RenderingCoordinator.cpp:117-166`, `828-887` |
| FramePacket state machine | `Library/Core/Public/Rendering/FramePacket.h:17-24`, `205-240`, `268-325`, `392-407`; `Library/Core/Private/Rendering/RenderingCoordinator.cpp:954-959`, `1235-1240` |
| Viewport plan build | `Library/Core/Private/Rendering/RenderingCoordinator.cpp:40-115` |
| FramePacket layout | `Library/Core/Public/Rendering/FramePacket.h:59-79` |
| CameraProxy | `Library/Core/Public/Rendering/SceneProxy.h:257-280` |
| RenderLayer/HasFlag | `Library/Core/Public/Rendering/RenderTypes.h:193-217` |
| ViewportRenderPlan DebugMode | `Library/Core/Public/Rendering/ViewRenderPlan.h:17-62` |
| BlendMode | `Library/Core/Public/Rendering/MaterialTypes.h:198-205` |
| GeneratePlane winding | `Library/Core/Public/Rendering/ProceduralMeshGenerator.h:158-216` |
| Orthographic helper | `Library/Core/Public/Math/MatrixUtils.h:381-401` |
| Viewport orthographic path | `Library/Core/Private/Rendering/Viewport.cpp:117-127` |
| Camera constants orthographic path | `Library/Core/Private/Rendering/CameraViewConstants.cpp:71-77` |
| Matrix transpose upload | `Library/Core/Private/Rendering/CameraViewConstants.cpp:95-103` |
| Shader MVP convention | `Assets/Shaders/gbuffer.vert:38-51`, `Assets/Shaders/forward_transparent.vert:36-47` |
| VertexLayout presets | `Library/Core/Public/Rendering/VertexLayout.h:267-372` |
| DrawCommandSorter non-stable sort | `Library/Core/Private/Rendering/DrawCommand.cpp:364-425` |
| VariableArray vector inheritance | `Library/Core/Public/Container/VariableArray.h:15-40` |
| ComponentDataRegistry compile/runtime flags | `Library/Core/CMakeLists.txt:418-424`, `Library/Core/Private/Engine/ComponentDataRegistry.cpp:7-17`, `Library/Core/Private/Engine/NorvesEngine.cpp:136` |
| Shader directory and BOM handling | `Library/Core/CMakeLists.txt:461-473`, `Library/Core/Private/RHI/Vulkan/VulkanShaderCompiler.cpp:134-145`, `Library/Core/Private/RHI/Vulkan/VulkanSlangCompiler.cpp:242-251` |
