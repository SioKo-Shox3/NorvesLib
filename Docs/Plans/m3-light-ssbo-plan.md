# M3 Phase 3: Light SSBO Migration Plan

## Purpose

Migrate `LightingPass` light-array data from fixed UBO `lights[16]` to a dynamically sized SSBO at descriptor binding `5`.

Expected behavior:
- `LightingPass` uploads every valid `LightProxy` in `context.SnapshotLightProxies`; 20 valid lights are preserved, not truncated.
- Empty or all-invalid light input packs exactly one default directional light.
- `lighting.frag` reads `LightData lights[]` from `layout(std430, set = 0, binding = 5) readonly buffer`.
- The light loop uses `params.lightCount` directly. No fixed cap and no clustered lighting.

## Execution Model

This phase is spec-complete enough for implementation through `codex exec --sandbox workspace-write`, unless the orchestrator decides ambiguity remains and routes to `implementer`. The main orchestrator must not edit production/test files directly. Plan review and implementation review still require Claude + Codex double-check, with implementation reviewer distinct from implementer.

Before implementation, the orchestrator saves this approved plan to `Docs/Plans/m3-light-ssbo-plan.md` as a doc-only planning step. The implementation delegate must not edit docs unless explicitly assigned.

## Scope

Allowed write paths:
- `Docs/Plans/m3-light-ssbo-plan.md` only for orchestrator plan persistence
- `Assets/Shaders/lighting.frag`
- `Library/Core/Private/Rendering/LightingPass.cpp`
- `Library/Core/Private/Rendering/LightingPassGpuTypes.h`
- `Library/Core/Private/Rendering/LightingPassLightPacking.h` new
- `Library/Core/Public/Rendering/LightingPass.h`
- `Test/Core/Rendering/LightingLightBufferTest.cpp` new
- `Test/Core/Rendering/RenderGraphCompileTest.cpp`
- `Test/Core/Rendering/CMakeLists.txt`

Forbidden write paths:
- `Library/Core/Private/RHI/*`
- `Library/Core/Public/RHI/*`
- `Library/Core/Public/Rendering/SceneProxy.h`
- `Library/Core/Public/Rendering/ViewRenderContext.h`
- `Library/Core/Public/Rendering/FramePacket*`
- `Game/*`
- `build/*`
- Other shaders
- Clustered lighting code

Preserve existing file line endings. New source/test files must be UTF-8 BOM + CRLF. Production code uses NorvesLib containers/pointers, not std containers/strings.

## Preflight

`Docs/agent-guide/` is absent in this checkout; use `AGENTS.md`, `Docs/Architecture/RenderingFlow.md`, and actual code as source of truth.

Rerun backend read-only checks before implementation. These regex checks were already observed valid and must remain true:
- `ResourceUsage::StorageBuffer` maps to `vk::BufferUsageFlagBits::eStorageBuffer`.
- `CPUAccessible` maps to host-visible coherent memory.
- `VulkanBuffer::Update` maps, copies, and unmaps for CPU-accessible buffers.
- `ResourceBindType::StructuredBuffer` maps to storage-buffer descriptors.
- `BindStorageBuffer` records binding and marks descriptor dirty.

## Interfaces

Move this into `Library/Core/Private/Rendering/LightingPassGpuTypes.h`:

```cpp
struct GPULightData
{
    float position[4];
    float direction[4];
    float color[4];
    float attenuation[4];
};
```

Add static assertions:
- `sizeof(GPULightData) == 64`
- offsets: `position=0`, `direction=16`, `color=32`, `attenuation=48`

Create `Library/Core/Private/Rendering/LightingPassLightPacking.h`:

```cpp
GPULightData MakeDefaultLightingPassLight();

bool PackLightingPassLight(const LightProxy& proxy, GPULightData& outLight);

uint32_t PackLightingPassLights(
    Container::Span<const LightProxy> lightProxies,
    Container::VariableArray<GPULightData>& outLights);
```

Behavior:
- `PackLightingPassLights` clears `outLights`, appends valid proxies, and if none were appended, appends exactly one default directional light.
- Default matches current fallback: directional type, direction `(-0.577, -0.577, -0.577)`, white color, intensity `1`, range `100`.
- Return value is final `outLights.size()`.
- Therefore `EnsureLightArrayBufferCapacity` is always called with at least `1`.

Add to `LightingPass`:
- `bool UpdateLightBuffer(ViewRenderContext& context, bool bShadowAvailable, bool bSSAOAvailable);`
- `bool EnsureLightArrayBufferCapacity(uint32_t requiredLightCount);`
- `uint32_t GetLightArrayBufferSizeBytes() const;`
- `uint32_t m_LightArrayCapacity = 0;`
- `Container::VariableArray<RHI::BufferPtr> m_RetiredLightArrayBuffers;`

Capacity policy:
- If `requiredLightCount <= m_LightArrayCapacity`, do not reallocate.
- Else grow to next power of two, minimum `1`; equivalently double current capacity until it satisfies required count.
- New buffer size is `capacity * sizeof(GPULightData)`.
- On growth, push the old active buffer to `m_RetiredLightArrayBuffers`, then replace active buffer.
- `Shutdown()` resets active buffer, clears retired buffers, and sets capacity to `0`.

Failure path:
- If capacity allocation fails, `UpdateLightBuffer` returns `false` before updating params or SSBO.
- `ExecuteWithInputs` logs an error, does not bind/update lighting descriptors, and does not enqueue the lighting draw. This avoids stale `GPULightingParams.lightCount` larger than the active buffer capacity.

## TDD Plan

1. Add `LightingLightBufferTest` and CMake registration.
- Use `__has_include("Rendering/LightingPassLightPacking.h")` so RED is observed before helper exists.
- Cover `GPULightData` layout.
- Pack 20 valid point lights and assert count `20`, type `Point`, positions/colors/ranges preserved.
- Pack invalid lights (`bVisible=false` or `Intensity=0`) and assert they are skipped.
- Empty input and all-invalid input each produce exactly one default directional light.
- Source-contract checks:
  - binding `5` descriptor in `LightingPass.cpp` is `ResourceBindType::StructuredBuffer`.
  - `BindStorageBuffer(5, ...)` exists.
  - `BindConstantBuffer(5, ...)` does not exist.
  - light buffer creation uses `ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead`, CPUAccessible `true`, debug name `LightArraySSBO`.
  - `MAX_LIGHTS`, `LIGHT_BUFFER_SIZE`, `GPULightData lightArray[...]`, and `lightCount >= MAX_LIGHTS` are absent.
  - shader has `layout(std430, set = 0, binding = 5) readonly buffer LightBuffer`.
  - shader has unsized `LightData lights[]`.
  - shader has no sized `lights[N]` regex and no `min(params.lightCount` in the light loop.

Run RED:

```powershell
cmake -B build -S . -G "Visual Studio 17 2022"
cmake --build build --config Debug --target LightingLightBufferTest
```

Expected: fails because helper/types are not available.

2. Add helper surface and layout only.
- Move `GPULightData`, add static assertions.
- Add `LightingPassLightPacking.h`.
- Do not migrate `LightingPass.cpp` or shader yet.

Run:

```powershell
cmake --build build --config Debug --target LightingLightBufferTest
ctest --test-dir build -C Debug -R LightingLightBufferTest --output-on-failure --timeout 60
```

Expected: packing/layout portions pass, source-contract checks still fail on old UBO/cap implementation.

3. Extend `RenderGraphCompileTest` fake RHI and behavior tests before migration.
- Add `#include <cstring>` for `std::strcmp`.
- Add fake buffer update byte capture. `FakeBuffer` records desc, last update size/offset, and update bytes.
- Capture binding `4` constant-buffer binding. Decode `GPULightingParams` from the actual `LightingParamsUBO` bytes bound at descriptor binding `4`.
- Capture binding `5` storage-buffer binding separately. Decode `GPULightData` bytes from the buffer bound at descriptor binding `5`.
- Store descriptor layout inputs:
  - `FakeDevice::CreateDescriptorSet` records the full `DescriptorSetDesc`.
  - `FakeDevice::CreateGraphicsPipeline` records `desc.descriptorSetLayouts`.
  - Assert binding `5` specifically is `ResourceBindType::StructuredBuffer` in both places. Do not use global `StructuredBuffer` checks because neural binding `11` can mask failure.
- Add ordered event log:
  - `LightSsboUpdate`
  - `BindStorageBuffer5`
  - `DescriptorSetUpdate`
  - `CommandSetDescriptorSet` or `Draw`
  - Assert `LightSsboUpdate < BindStorageBuffer5 < DescriptorSetUpdate < CommandSetDescriptorSet/Draw`.

Add `TestLightingNativeExecuteBindsExpandedLightStorageBuffer()`:
- Fixture uses `FakeDevice`, `ShaderManager`, `MockAllocator`, `TransientResourcePool`, `RenderResources`, `SceneRenderer`, `RenderGraph`, `GBufferPass`, `LightingPass`, `FakeCommandList`.
- `GBufferPass.SetSceneRenderer(&renderer)`.
- Add GBuffer pass and Lighting pass to the graph so named GBuffer inputs exist and `ExecuteWithInputs` reaches `UpdateLightBuffer`.
- Create 20 valid `LightProxy` entries, set `context.SnapshotLightProxies = &lightProxies`.
- Set usual context resources: command list, device, pool, shader manager, renderer, pending frame commands, render size, `Resources.Textures/Materials/Meshes`.
- Compile/execute graph, cleanup.
- Assert:
  - `LightArraySSBO` created with size `>= 20 * sizeof(GPULightData)`.
  - usage has `StorageBuffer | ShaderRead`.
  - CPUAccessible is true.
  - binding `5` is storage buffer, offset `0`, size `>= expected bytes`.
  - SSBO last update size equals `20 * sizeof(GPULightData)`.
  - decoded binding `4` `GPULightingParams.lightCount == 20`.
  - first and last decoded SSBO lights match injected proxies.
  - event order is correct.

Add growth/retention test:
- First execute with 1 valid light, then execute with 20 valid lights on the same `LightingPass`.
- Fake buffer lifetime tracking must not keep strong buffer refs in `FakeDevice`; store descriptor/debug-name records plus lifetime trackers. `FakeBuffer` destructor marks its tracker destroyed.
- Assert old 1-light SSBO is still not destroyed after growth because it is retained in `m_RetiredLightArrayBuffers`.
- Execute again with 12 lights and assert no new `LightArraySSBO` allocation occurs because required count is within capacity.
- After `lightingPass.Shutdown()`, assert old retired and active SSBO trackers are destroyed.

Add failure-path test if practical in the same file:
- Configure fake device to fail the second `LightArraySSBO` creation during growth.
- Assert `UpdateLightBuffer` failure path causes no binding `5`, no `DescriptorSet::Update`, and no lighting draw event after the failure point.

Call new tests from `main()` near other Lighting execute tests.

Run pre-migration RED:

```powershell
cmake --build build --config Debug --target RenderGraphCompileTest
ctest --test-dir build -C Debug -R RenderGraphCompileTest --output-on-failure --timeout 120
```

Expected: fails before production migration because current code uses constant buffer binding `5`, fixed size, and truncates at 16.

4. Migrate production.
- `CreateLightingDescriptorSetDesc`: binding `5` becomes `ResourceBindType::StructuredBuffer`.
- `Initialize`: create minimum capacity `1` SSBO through `EnsureLightArrayBufferCapacity(1)`, not fixed `LightArrayUBO`.
- `EnsureLightingDescriptorSet`: bind only params UBO at binding `4`; do not bind light buffer there.
- `UpdateLightBuffer`: pack into `Container::VariableArray<GPULightData>`, ensure capacity, update params UBO, update SSBO.
- `ExecuteWithInputs`: if `UpdateLightBuffer(...)` returns false, log and return before texture/light descriptor binds or descriptor update. Otherwise bind `BindStorageBuffer(5, m_LightArrayBuffer, 0, GetLightArrayBufferSizeBytes())` before `m_LightingDescriptorSet->Update()`.
- Remove all fixed cap code and constants.

5. Migrate shader.
- Replace binding `5` with:

```glsl
layout(std430, set = 0, binding = 5) readonly buffer LightBuffer
{
    LightData lights[];
} lightBuffer;
```

- Replace light loop with:

```glsl
for (uint i = 0u; i < params.lightCount; i++)
```

6. GREEN focused tests.

```powershell
cmake --build build --config Debug --target LightingLightBufferTest
ctest --test-dir build -C Debug -R LightingLightBufferTest --output-on-failure --timeout 60

cmake --build build --config Debug --target RenderGraphCompileTest
ctest --test-dir build -C Debug -R RenderGraphCompileTest --output-on-failure --timeout 120
```

## LightingParamsLayoutTest Note

Keep `LightingParamsLayoutTest` as the unchanged `GPULightingParams` and shader debug-mode contract. `LightingLightBufferTest` owns `GPULightData` and shader `LightData`/binding `5` synchronization. Only extend `LightingParamsLayoutTest.cpp` if implementation discovers it already owns `LightData` layout assertions.

Include it in verification:

```powershell
cmake --build build --config Debug --target LightingParamsLayoutTest
ctest --test-dir build -C Debug -R LightingParamsLayoutTest --output-on-failure --timeout 60
```

## Full Verification

Focused:

```powershell
cmake --build build --config Debug --target LightingLightBufferTest RenderGraphCompileTest LightingParamsLayoutTest
ctest --test-dir build -C Debug -R "LightingLightBufferTest|RenderGraphCompileTest|LightingParamsLayoutTest" --output-on-failure --timeout 180
```

Game build:

```powershell
cmake --build build --config Debug --target Game
```

Required Game smoke. Do not use `--trace-file`; `--trace-file` is Stats CSV, not logger evidence.

```powershell
$gameDir = Join-Path (Resolve-Path build).Path 'Game\Debug'
$log = Join-Path $gameDir 'Game.log'
Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue

$p = Start-Process -FilePath (Join-Path $gameDir 'Game.exe') `
    -ArgumentList @('--exit-after-frames=5','--render-thread=st') `
    -WorkingDirectory $gameDir `
    -PassThru

if (-not $p.WaitForExit(45000)) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw 'Game smoke timed out'
}
if ($p.ExitCode -ne 0) { throw "Game smoke exit code $($p.ExitCode)" }
if (-not (Test-Path $log)) { throw 'Game.log not produced' }

$text = Get-Content -Raw $log
if ($text -notmatch 'exit-after-frames reached') { throw 'missing exit-after-frames reached' }
if ($text -notmatch 'lighting\.frag|LightingPass initialized') { throw 'missing lighting evidence' }

$forbidden = @(
    '(?i)lighting\.frag.*(error|fail)',
    '(?i)(descriptor|vulkan|validation|pipeline|buffer).*(error|fail)',
    '(?i)(error|fail).*(descriptor|vulkan|validation|pipeline|buffer)',
    '(?i)unexpected shader'
)
foreach ($pattern in $forbidden) {
    foreach ($m in [regex]::Matches($text, $pattern)) {
        if ($m.Value -notmatch '(?i)(slang|neural_material_decode\.slang)') {
            throw "forbidden log evidence: $($m.Value)"
        }
    }
}
```

Full regression:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --timeout 180
```

Static guards:

```powershell
rg -n "RHI/Vulkan" Library/Core/Private/Rendering Library/Core/Public/Rendering Test/Core/Rendering
rg -n "MAX_LIGHTS|LIGHT_BUFFER_SIZE|BindConstantBuffer\(5|lightCount >= MAX_LIGHTS|GPULightData\s+\w+\s*\[" Library/Core/Private/Rendering/LightingPass.cpp
rg -n "min\(params\.lightCount|lights\s*\[\s*[0-9]+u?\s*\]" Assets/Shaders/lighting.frag
git diff --numstat
git diff --ignore-cr-at-eol --numstat
```

Success:
- No Rendering include references to `RHI/Vulkan`.
- No fixed light cap artifacts remain.
- No shader-sized light array or `min(params.lightCount...)`.
- Numstat outputs match for edited existing files.

New source/test BOM + CRLF:

```powershell
$files = @(
    'Library/Core/Private/Rendering/LightingPassLightPacking.h',
    'Test/Core/Rendering/LightingLightBufferTest.cpp'
)
foreach ($file in $files) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path $file))
    if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
        throw "$file missing UTF-8 BOM"
    }
    for ($i = 0; $i -lt $bytes.Length; ++$i) {
        if ($bytes[$i] -eq 0x0A -and ($i -eq 0 -or $bytes[$i - 1] -ne 0x0D)) {
            throw "$file has LF without CR"
        }
    }
}
```

## Risk And Rollback

Risk: medium-high.

Reasons:
- Shader descriptor layout changes from uniform buffer to storage buffer.
- Lighting buffer ownership and growth behavior changes.
- Runtime Vulkan descriptor/pipeline mismatches may only appear in Game smoke.

Containment:
- No RHI backend changes.
- Binding number remains `5`.
- `GPULightingParams` layout remains unchanged.
- Old SSBOs are retained until `Shutdown()` after growth.

Rollback:

```powershell
git revert <phase3_commit>
```

Expected rollback: restores fixed UBO `lights[16]` behavior and removes this phase’s helper/tests.

## Commit Boundary

Single phase commit after plan is saved, implementation reviews pass, and all verification passes.

Stage only:
- `Docs/Plans/m3-light-ssbo-plan.md` if the orchestrator saved it in this commit
- `Assets/Shaders/lighting.frag`
- `Library/Core/Private/Rendering/LightingPass.cpp`
- `Library/Core/Private/Rendering/LightingPassGpuTypes.h`
- `Library/Core/Private/Rendering/LightingPassLightPacking.h`
- `Library/Core/Public/Rendering/LightingPass.h`
- `Test/Core/Rendering/LightingLightBufferTest.cpp`
- `Test/Core/Rendering/RenderGraphCompileTest.cpp`
- `Test/Core/Rendering/CMakeLists.txt`

Suggested commit subject:

```text
LightingPassのライト配列をSSBOへ移行する
```

After commit, push current branch if remote allows:

```powershell
git push
```

## Self-Review Checklist

- Phase goal is dynamic light SSBO migration only; clustered lighting is excluded.
- Allowed and forbidden paths are explicit.
- Implementation routing matches current AGENTS.md: Codex CLI or implementer by task shape, no main-agent production edits.
- TDD includes RED for missing helper and pre-migration RED for `RenderGraphCompileTest`.
- Binding `5` checks are binding-specific in source, descriptor set desc, and pipeline descriptor layout.
- Fake RHI captures binding `4` params UBO and binding `5` SSBO separately.
- RenderGraph test injects 20 valid `LightProxy` entries through `context.SnapshotLightProxies` and drives real GBuffer-to-Lighting execution.
- Event ordering proves SSBO update before binding, descriptor update before draw setup.
- Growth policy and no-reallocation-within-capacity behavior are specified and tested.
- Retired buffer lifetime is tested through shutdown.
- Failure path avoids stale params, descriptor updates, and lighting draw enqueue.
- Shader source contracts are cap-agnostic.
- Game smoke uses `Game.log`, visible/normal process launch, exit code 0, required success evidence, and forbidden error checks.
- Verification includes focused tests, `Game`, required smoke, full CTest, include grep, numstat line-ending check, and BOM/CRLF checks.
