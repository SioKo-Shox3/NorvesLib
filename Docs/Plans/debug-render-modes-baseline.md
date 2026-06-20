# Debug Render Modes Baseline

This baseline records the phase 0/1 starting point for the debug render modes work.

## Repository

- `develop` is up to date with `origin/develop`.
- `cmake -B build -S . -G "Visual Studio 17 2022"` completed successfully.
- `cmake --build build --config Debug --target Game` completed successfully.

## Tests

- A normal `ctest --test-dir build -C Debug` run timed out at 10 minutes.
- `ctest --test-dir build -C Debug --timeout 20 -j 8 --output-on-failure` reported 63 passes out of 64 tests.
- The only failing test in that bounded run was the pre-existing `TextureResourcesTextureAssetTest` timeout.
- A later focused rerun of `TextureResourcesTextureAssetTest` passed, and the post-implementation full
  `ctest --test-dir build -C Debug --output-on-failure` run passed 67/67 tests. Treat the first timeout as
  transient baseline noise rather than a deterministic failure.

## Runtime Baseline

- MT launch: normal launch produced a `RenderThread started` log.
- MT screenshot: `Docs/Plans/assets/debug-render-modes/baseline-mt-normal.png` was captured.
- `Docs/Plans` is under `.gitignore`, so the baseline note and screenshot require force-add when committing.
- Trace `DrawCalls` and `TrianglesRendered` were zero for all frames, so those counters are not valid baseline statistics yet.
- ST launch: the current executable has no ST/MT switch CLI, so independent ST launch was not verified within the phase 0 read-only scope.
- Startup PSO count is not logged directly. Code inspection shows the relevant current startup paths create:
  `GBufferPass` one graphics pipeline, `MegaGeometryPass` one compute cull pipeline plus one optional Hi-Z
  compute pipeline and one draw graphics pipeline when instances exist, and `LightingPass` one graphics pipeline.

## Logs And Validation

- `SLANG_SDK_DIR` was not specified, so `neural_material_decode.slang` produced the expected warning/error and the neural decoder was disabled.
- No explicit Vulkan validation warning was found in the checked log range.

## Dataflow Baseline

- `RenderingCoordinator::BuildViewportRenderPlan` is the author site for per-viewport snapshots.
- `RenderFrameExecutor::ApplyViewportRenderPlan` consumes `ViewportRenderPlan` by assigning `ViewRenderContext::CurrentViewport`.
- `View::GetMainViewport()` returns `TSharedPtr<Viewport>` and returns null when no viewport exists.
- `DebugMode` and `GetActiveDebugMode()` were absent before the phase 1 change.
- `BuildViewportRenderPlan` is in an anonymous namespace, so phase 1 tests cover the public consume path and
  the production copy line is guarded by implementation review rather than direct unit-test linkage.

## Device, Lighting, And Pipeline Notes

- `fillModeNonSolid` is enabled in `VulkanDevice`.
- `DeviceCapabilities` has no `fillModeNonSolid`, `wideLines`, or `drawIndirectFirstInstance` fields.
- `GPULightingParams` currently includes `_pad2` and matches the shader tail better than the older plan note.
- `View` uses RenderGraph when `context.Graph` is provided.
- `GBuffer`, `Lighting`, and `MegaGeometry` have RenderGraph `Execute` variants.
