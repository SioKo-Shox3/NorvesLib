# M3 Shadow Range Fit Phase 4 Plan Summary

Status: reviewed implementation slice for one global, single-cascade directional shadow range fit. Scope excludes shader, RHI/Vulkan, layout, cascade, and atlas changes.

## F1: Snapshot Caster Range Fit

- Convert `ShadowMapPassSettings` to `DirectionalShadowMatrixSettings` through a private helper, mapping `OrthoSize`, `NearPlane`, and `FarPlane` while preserving the default `Target` and `LightDistance`.
- Fit the directional shadow target/range from `FramePacket` snapshot `MeshProxy` and `MegaGeometryProxy` caster bounds only.
- Eligible casters must be valid, shadow-casting, have valid `WorldBounds`, and pass explicit finite checks for `CenterX`, `CenterY`, `CenterZ`, and `Radius`.
- Merge valid caster spheres to an AABB, use its center as `Target`, compute fit radius from all valid spheres, then expand `OrthoSize`, `LightDistance`, and `FarPlane` without shrinking base settings.
- If no valid caster exists, return base settings unchanged.

## F2: Shared Pass Wiring

- `RenderingCoordinator` attaches snapshot mesh, light, and mega geometry proxy arrays to `ViewRenderContext` from the current `FramePacket`.
- `ShadowMapPass` records `ActiveShadowMapSettings` before building fitted matrices and remains the provenance source for the current viewport.
- `LightingPass` reuses `ActiveShadowMapSettings` when available, otherwise falls back to default directional shadow settings, then calls the same fitted helper with the same snapshot proxy arrays.
- Both passes keep identity-disabled behavior, non-reversed clip-space adjustment, and `CopyShadowMatrixToShaderData` projection upload.

## v4 Reset And Provenance Rule

`RenderFrameExecutor::ApplyViewportRenderPlan` resets `context.ActiveShadowMapSettings` near `CurrentGraphExecutionResult` before both early returns. This prevents LightingPass from carrying a ShadowMapPass settings pointer across viewports when a viewport plan is null or draw-command snapshot data is missing.

## Verification Focus

- `DirectionalShadowLightMatricesTest` pins caster filtering, non-finite rejection, fallback behavior, large/far range expansion, the mesh+mega pinned case, and fitted matrix light position.
- `DirectionalShadowPassWiringContractTest` pins context fields, coordinator assignment, executor reset ordering, ShadowMapPass provenance write, LightingPass active/default settings read, fitted helper calls, clip-space adjustment, and absence of live SceneView proxy reads.
