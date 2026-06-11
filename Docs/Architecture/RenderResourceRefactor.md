# Render Resource Responsibility Refactor

## Current Public API

`RenderResources` is the owner aggregate for render resources. External callers reach
render-resource operations through:

```cpp
auto &renderResources = GEngine->GetRenderResources();
```

`RenderWorld` owns the aggregate and exposes `RenderWorld::GetRenderResources()` as an
owner/internal accessor. Code outside the owner path should prefer
`GEngine->GetRenderResources()` so render-resource access does not depend on `RenderWorld`
layout details.

Operational APIs are grouped by domain facade:

- `Gpu()`: low-level GPU buffers, textures, shaders, samplers, and vertex layouts.
- `Textures()`: texture asset resolve, load, prepared payload, cache, async load, and
  fallback handling.
- `Materials()`: material and neural material registration, update, lookup, release,
  and resource lifetime.
- `Meshes()`: mesh GPU registration, lookup, release, and descriptor access.
- `MegaGeometry()`: MegaMesh/MegaGeometry registration, lookup, release, and related
  GPU data.

Private stores behind the `RenderResources` pimpl are implementation details. New code
should depend on the narrow domain facade that owns the operation, not on a private store
class or a broad compatibility name.

## Why Rename-Only Was Insufficient

Historically, render-resource code accumulated GPU resource factory behavior, handle
registry behavior, texture asset resolution, sync and async texture loading, material
registration, neural material ownership, mesh/MegaMesh/model registration, and lifetime
cleanup. A rename alone would have preserved that broad responsibility and kept inviting
unrelated behavior into the same type.

The refactor goal is to make each responsibility hard to misplace. Names should describe
what the type owns or decides, and broad access should be replaced by explicit domain
facades.

## Responsibility Split

- `GpuResourceTypes`: low-level GPU resource create info and statistics types such as
  `BufferCreateInfo`, `TextureCreateInfo`, `ShaderCreateInfo`, and `ResourceStats`.
- `Gpu()` facade: creation, ownership, release, and handle lookup for buffers, textures,
  shaders, samplers, and vertex layouts.
- `TextureAssetTypes`: texture asset fallback mode, source, prepared status, prepared
  payload, and prepared split result types.
- `Textures()` facade: asset root, manifest state, fallback mode, generation, path
  normalization, variant lookup, cooked reference resolution, texture I/O flow,
  cooked/loose upload, prepared finalize/split, texture cache, async queue, flush, and
  callbacks.
- `Materials()` facade: material and neural material registration, update, lookup,
  release, and neural material resource lifetime.
- `Meshes()` facade: mesh GPU data registration, lookup, release, and descriptor access.
- `MegaGeometry()` facade: MegaMesh GPU data registration, lookup, release, and related
  descriptor access.
- `RenderResources`: owning aggregate used by `RenderWorld`; it initializes, shuts down,
  and exposes domain facade accessors.
- `RenderResourceAccess`: non-owning dependency injection bundle passed to frame, pass,
  loader, and analyzer code where narrow dependencies are needed.

New behavior should be added to the specific domain facade and hidden implementation that
own the data and lifetime rules, not to the aggregate.

## Resolver Boundary

Texture resolution must not own package read, `Package` parse, cooked entry hash
checking, or `ParseCookedTexture`. Those operations are I/O and payload preparation work
and belong to texture loading/preparation code behind `Textures()`.

The resolver should answer only: what logical asset is being requested, what generation
and fallback policy apply, and what cooked or loose reference should the loader attempt.

## RenderResources Boundary

`RenderResources` owns the split implementation and exposes initialize, shutdown, and
domain facade accessors. It is the aggregate boundary, not a place to add unrelated domain
behavior.

Shutdown order should be:

1. cancel or flush async texture work
2. destroy texture loading/runtime state
3. destroy material, mesh, and MegaGeometry state
4. destroy low-level GPU resource state
5. destroy texture asset resolver state

## Stable AssetLoadProfile Stages

These stage names are part of the refactor contract and should remain stable unless the
profiling schema is intentionally migrated:

- `texture_asset_resolve`
- `texture_cooked_parse`
- `texture_cooked_upload`
- `texture_prepare_asset`
- `texture_prepared_cooked_upload`
- `texture_prepared_finalize`
- `texture_prepared_split`

Internal stages such as package read or package parse may move as the implementation is
split and are not locked by this contract.

## Async Texture Contract

Worker-side texture work may perform file read, asset resolution, image decode, package
parse, cooked texture parse, and prepared channel split. GPU resource creation, GPU
upload, cache publication, and user callbacks happen during main/render-thread flush.

This keeps RHI lifetime and callback side effects out of worker tasks and gives later
stores/loaders a clear synchronization boundary.
