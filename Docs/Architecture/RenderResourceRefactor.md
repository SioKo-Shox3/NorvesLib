# Render Resource Responsibility Refactor

## Why Rename-Only Is Insufficient

`RenderResourceManager` currently acts as a GPU resource factory, handle registry, texture
asset resolver, sync and async texture loader, material registry, neural material owner,
mesh/MegaMesh/model registry, and lifetime cleanup point. Renaming the class would preserve
that broad responsibility and keep inviting unrelated behavior into the same type.

The refactor goal is to make each responsibility hard to misplace. Names should describe
what the type owns or decides, and broad access should be replaced by explicit dependencies.

## Target Split

- `GpuResourceTypes`: low-level GPU resource create info and statistics types such as
  `BufferCreateInfo`, `TextureCreateInfo`, `ShaderCreateInfo`, and `ResourceStats`.
- `GpuResourceStore`: creation, ownership, release, and handle lookup for buffers,
  textures, shaders, samplers, and vertex layouts.
- `TextureAssetTypes`: texture asset fallback mode, source, prepared status, prepared
  payload, and prepared split result types.
- `TextureAssetResolver`: asset root, manifest state, fallback mode, generation,
  path normalization, variant lookup, and cooked reference resolution.
- `TextureAssetLoader`: texture I/O flow, `stb_image` loose decode, cooked texture
  package read/parse/upload, prepared finalize, prepared split, texture cache,
  async queue, flush, and callbacks.
- `RenderMaterialStore`: material and neural material registration, update, lookup,
  release, and neural material resource lifetime.
- `RenderGeometryStore`: mesh GPU data, MegaMesh GPU data, model registry, and related
  lookup/release paths.
- `RenderResourceSet`: owning aggregate used by `RenderWorld`; it may initialize,
  shut down, and expose accessors only.
- `RenderResourceAccess`: non-owning dependency injection bundle passed to frame,
  pass, loader, and analyzer code where narrow dependencies are needed.

## Resolver Boundary

`TextureAssetResolver` must not own package read, `Package` parse, cooked entry hash
checking, or `ParseCookedTexture`. Those operations are I/O and payload preparation
work and belong in `TextureAssetLoader` or a narrowly named preparer used by the loader.

The resolver should answer only: what logical asset is being requested, what generation
and fallback policy apply, and what cooked or loose reference should the loader attempt.

## RenderResourceSet Boundary

`RenderResourceSet` is not a new facade. It owns the split components and exposes
initialize, shutdown, and accessors. New behavior should be added to a specific store,
loader, or resolver, not to the aggregate.

Shutdown order should be:

1. cancel or flush async texture work
2. destroy the texture loader
3. destroy material and geometry stores
4. destroy the GPU resource store
5. destroy the texture asset resolver

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
