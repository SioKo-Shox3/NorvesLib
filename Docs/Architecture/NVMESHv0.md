# NVMESH v0 Mesh Format

## Scope / Status

This document specifies the `.nvmesh` cooked mesh format implemented by M4 Phases 2–5.
`CookedMesh` parsing, AssetCook glTF-to-NVMESH/`Msh0` output, package/hash/model
resolution, synchronous and asynchronous MegaGeometry runtime finalization, generation
cache/cancel/drain, manifest snapshot and Bridge reload, and the strict real-Game
profile are landed. The loose glTF compatibility path remains available.

In-place rebind, multi-submesh/material, skin/morph, compression, and baked LOD remain
out of scope for v0.

`Docs/agent-guide/` is absent in the current repository. This specification therefore
uses `AGENTS.md`, `Docs/Architecture/AssetCookWorkflow.md`,
`Docs/Architecture/AssetFallbackPolicy.md`, and the actual code as source of truth.

## Normative Identity

- File extension: `.nvmesh`.
- File magic: exactly `{'N','V','M','E','S','H','v','0'}`.
- Magic size: 8 bytes.
- The magic has no trailing NUL byte.
- Package FourCC: `Msh0`.
- `Mod0` in `Test/Core/Asset/PackageFormatTest.cpp` is a placeholder/test value and is
  not normative for cooked mesh packages.
- Runtime manifest fields for this format:
  - `kind`: `model`
  - `format`: `nvmesh.v0.mesh3d.pnt.u32.clustered`
  - `entry_type`: `Msh0`
  - `cooked_version`: `0`
- Byte order is fixed little-endian.
- `EndianMarker` is `0x01020304`.
- Floating point fields are IEEE-754 binary32 little-endian.
- NaN and Inf are rejected in all serialized floats.
- Bounds radii are finite and non-negative.

## Manifest Example

```json
{
  "version": 1,
  "assets": [
    {
      "logical_path": "Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf",
      "kind": "model",
      "source_hash": "0123456789abcdef",
      "variant": "default",
      "format": "nvmesh.v0.mesh3d.pnt.u32.clustered",
      "cooked_package": "Cooked/Models/Rendering3DTestSilverGltf.nvpkg",
      "entry_name": "Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.nvmesh",
      "entry_type": "Msh0",
      "cooked_hash": "fedcba9876543210",
      "cooked_version": 0
    }
  ]
}
```

## String / Path Rules

The string table stores raw bytes only. Strings are not NUL-terminated by contract.
Each string is referenced by an explicit offset and length.

String table bytes must be printable ASCII only: `0x20` through `0x7e`. Control bytes,
DEL (`0x7f`), non-ASCII bytes, and embedded NUL bytes are rejected.

Path-like strings normalize separators to `/`. Logical paths drop the leading `Assets/`
prefix, matching the existing manifest convention where
`Assets/Textures/Silver/silver_albedo.png` becomes
`Textures/Silver/silver_albedo.png`.

Path validation rejects:

- empty path strings where a required path is expected
- absolute paths
- drive-relative paths such as `C:foo/bar`
- UNC paths
- `..` traversal segments

Material texture references are stored as string references for albedo, normal, and ARM
textures. An empty string reference (`StringOffset=0`, `StringLength=0`) means the
texture is unset and is allowed.

## Binary Layout

All integer fields are little-endian. All offsets are absolute file offsets unless a
field explicitly says otherwise. Section starts are 8-byte aligned. Padding bytes are
zero and are included in `PayloadHash` when they lie after the header.

The top-level section order is fixed:

```text
header -> submesh table -> material table -> cluster table -> string table -> vertex payload -> index payload
```

`FileSize` equals the end of the index payload. Trailing bytes after the index payload
are rejected. The wire format does not serialize raw C++ struct ABI such as
`MeshCluster`, `GPUClusterData`, or private staging structs.

`PayloadHash` is FNV-1a64 over the contiguous region from the first section after the
header through the end of the index payload. The hashed region includes submesh,
material, and cluster tables, string bytes, vertex and index payloads, and zero padding
between sections. The package/manifest `cooked_hash` remains separate and covers the
package entry bytes.

FNV-1a64 constants:

- offset basis: `14695981039346656037`
- prime: `1099511628211`
- empty payload hash: offset basis

### Header

Header size is 256 bytes.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 8 | `uint8[8]` | `Magic` | `NVMESHv0` |
| 8 | 4 | `uint32` | `HeaderSize` | must be `256` |
| 12 | 2 | `uint16` | `VersionMajor` | must be `0` |
| 14 | 2 | `uint16` | `VersionMinor` | must be `0` |
| 16 | 4 | `uint32` | `EndianMarker` | must be `0x01020304` |
| 20 | 4 | `uint32` | `VertexRecordSize` | must be `32` |
| 24 | 4 | `uint32` | `SubmeshRecordSize` | must be `64` |
| 28 | 4 | `uint32` | `MaterialRecordSize` | must be `64` |
| 32 | 4 | `uint32` | `ClusterRecordSize` | must be `80` |
| 36 | 4 | `uint32` | `StringRefRecordSize` | must be `16` |
| 40 | 8 | `uint64` | `FileSize` | must equal end of index payload |
| 48 | 8 | `uint64` | `SubmeshTableOffset` | absolute file offset |
| 56 | 8 | `uint64` | `SubmeshTableSize` | `SubmeshCount * 64` |
| 64 | 8 | `uint64` | `MaterialTableOffset` | absolute file offset |
| 72 | 8 | `uint64` | `MaterialTableSize` | `MaterialCount * 64` |
| 80 | 8 | `uint64` | `ClusterTableOffset` | absolute file offset |
| 88 | 8 | `uint64` | `ClusterTableSize` | `ClusterCount * 80` |
| 96 | 8 | `uint64` | `StringTableOffset` | absolute file offset |
| 104 | 8 | `uint64` | `StringTableSize` | `StringByteCount` |
| 112 | 8 | `uint64` | `VertexPayloadOffset` | absolute file offset |
| 120 | 8 | `uint64` | `VertexPayloadSize` | `VertexCount * 32` |
| 128 | 8 | `uint64` | `IndexPayloadOffset` | absolute file offset |
| 136 | 8 | `uint64` | `IndexPayloadSize` | `IndexCount * 4` |
| 144 | 8 | `uint64` | `PayloadHash` | internal FNV-1a64 |
| 152 | 4 | `uint32` | `VertexCount` | total vertex records |
| 156 | 4 | `uint32` | `IndexCount` | total uint32 indices; multiple of 3 |
| 160 | 4 | `uint32` | `SubmeshCount` | must be `1` in v0 |
| 164 | 4 | `uint32` | `MaterialCount` | must be `1` in v0 |
| 168 | 4 | `uint32` | `ClusterCount` | must be greater than `0` |
| 172 | 4 | `uint32` | `StringByteCount` | string table byte count |
| 176 | 4 | `float32` | `TotalBoundsCenterX` | finite |
| 180 | 4 | `float32` | `TotalBoundsCenterY` | finite |
| 184 | 4 | `float32` | `TotalBoundsCenterZ` | finite |
| 188 | 4 | `float32` | `TotalBoundsRadius` | finite, non-negative |
| 192 | 4 | `uint32` | `ClusterAlgorithmId` | `1` = current greedy MeshClusterizer-style clustered stream |
| 196 | 4 | `uint32` | `ClusterAlgorithmVersion` | `0` for v0 data |
| 200 | 4 | `uint32` | `ClusterMaxTriangles` | must be `128` for current v0 output |
| 204 | 4 | `uint32` | `ClusterMaxVertices` | must be `128` for current v0 output |
| 208 | 4 | `uint32` | `ClusterSettingsFlags` | must be `0` in v0 |
| 212 | 4 | `uint32` | `Flags` | reserved, must be `0` |
| 216 | 8 | `uint64` | `Reserved0` | must be `0` |
| 224 | 8 | `uint64` | `Reserved1` | must be `0` |
| 232 | 8 | `uint64` | `Reserved2` | must be `0` |
| 240 | 8 | `uint64` | `Reserved3` | must be `0` |
| 248 | 8 | `uint64` | `Reserved4` | must be `0` |

### Vertex Record

Vertex record size is 32 bytes.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 12 | `float32[3]` | `Position` | finite |
| 12 | 12 | `float32[3]` | `Normal` | finite |
| 24 | 8 | `float32[2]` | `TexCoord` | finite |

This matches current `Rendering::Mesh3DVertex` semantics: position, normal, texcoord.
The wire contract is independent and must be parsed field-by-field.

### Submesh Record

Submesh record size is 64 bytes. v0 stores exactly one submesh.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | `uint32` | `IndexOffset` | index element offset, not bytes |
| 4 | 4 | `uint32` | `IndexCount` | multiple of 3 |
| 8 | 4 | `uint32` | `VertexOffset` | must be `0` in v0 |
| 12 | 4 | `uint32` | `VertexCount` | `0` allowed as unspecified, otherwise <= total vertices |
| 16 | 4 | `uint32` | `MaterialIndex` | must be `0` in v0 |
| 20 | 4 | `uint32` | `ClusterOffset` | cluster record offset, not bytes |
| 24 | 4 | `uint32` | `ClusterCount` | number of cluster records |
| 28 | 4 | `uint32` | `Flags` | reserved, must be `0` |
| 32 | 12 | `float32[3]` | `BoundsCenter` | finite |
| 44 | 4 | `float32` | `BoundsRadius` | finite, non-negative |
| 48 | 8 | `uint64` | `Reserved0` | must be `0` |
| 56 | 8 | `uint64` | `Reserved1` | must be `0` |

### Material Record

Material record size is 64 bytes. v0 stores exactly one material.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 16 | `StringRef` | `AlbedoTexture` | logical texture path or empty |
| 16 | 16 | `StringRef` | `NormalTexture` | logical texture path or empty |
| 32 | 16 | `StringRef` | `ArmTexture` | logical texture path or empty |
| 48 | 4 | `uint32` | `Flags` | reserved, must be `0` |
| 52 | 4 | `uint32` | `Reserved0` | must be `0` |
| 56 | 8 | `uint64` | `Reserved1` | must be `0` |

### Cluster Record

Cluster record size is 80 bytes.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 12 | `float32[3]` | `BoundsCenter` | finite |
| 12 | 4 | `float32` | `BoundsRadius` | finite, non-negative |
| 16 | 12 | `float32[3]` | `ConeAxis` | finite |
| 28 | 4 | `float32` | `ConeCutoff` | finite |
| 32 | 4 | `uint32` | `IndexOffset` | index element offset, not bytes |
| 36 | 4 | `uint32` | `IndexCount` | multiple of 3 |
| 40 | 4 | `uint32` | `VertexOffset` | must be `0` in v0 |
| 44 | 4 | `uint32` | `VertexCount` | `0` allowed as unspecified |
| 48 | 4 | `uint32` | `MaterialIndex` | must be `0` in v0 |
| 52 | 4 | `uint32` | `LODLevel` | must be `0` in v0 |
| 56 | 4 | `float32` | `LODError` | must be `0.0` in v0 |
| 60 | 4 | `uint32` | `ParentStart` | must be `0` in v0 |
| 64 | 4 | `uint32` | `ParentCount` | must be `0` in v0 |
| 68 | 4 | `uint32` | `Flags` | reserved, must be `0` |
| 72 | 8 | `uint64` | `Reserved0` | must be `0` |

### String Reference Record

String reference record size is 16 bytes. It may appear embedded in other records.

| Offset | Size | Type | Field | Rule |
| ---: | ---: | --- | --- | --- |
| 0 | 8 | `uint64` | `StringOffset` | byte offset relative to start of string table |
| 8 | 4 | `uint32` | `StringLength` | byte length, no NUL terminator |
| 12 | 4 | `uint32` | `Reserved0` | must be `0` |

For a non-empty reference, `StringLength > 0` and
`StringOffset + StringLength <= StringTableSize`. For an unset optional reference,
both `StringOffset` and `StringLength` are `0`.

## Data Model

The vertex payload is an array of fixed 32-byte vertex records:

- `float32 Position[3]`
- `float32 Normal[3]`
- `float32 TexCoord[2]`

The index payload is a `uint32` little-endian clusterized index stream.
`IndexCount % 3 == 0` is required.

v0 supports exactly one submesh and exactly one material. Multi-submesh,
multi-material, and multi-primitive behavior are out of scope.

Baked clusters are required. Runtime finalization uses the existing MegaGeometry
creation behavior equivalent to `bBuildLODHierarchy=false`; cooked v0 data must not
trigger automatic LOD hierarchy building.

The material stores albedo, normal, and ARM texture string references only.

## Cluster Semantics

`IndexOffset` and `IndexCount` are uint32 index element ranges, not byte offsets.

Cluster ranges must be within the index payload and triangle-count aligned. v0 writers
should emit contiguous, exhaustive cluster ranges covering `[0, IndexCount)`. Parsers
should reject overlaps, holes, and out-of-order packing for v0 cooked data unless a
future version explicitly relaxes this.

`VertexOffset` must be `0` in v0. Nonzero values are rejected.

`VertexCount == 0` is valid and means the per-cluster vertex range is unspecified; the
parser validates indices against the full vertex buffer. This matches the current
multi-cluster `MeshClusterizer` output.

`VertexCount > 0` is valid only when `VertexOffset + VertexCount <= total VertexCount`
and every index in the cluster range fits the declared vertex range. Because v0 requires
`VertexOffset == 0`, this means cluster indices must be `< VertexCount` for that
cluster.

All indices in the index payload must be `< total VertexCount`.

Only one material exists in v0. `MaterialIndex` is stored for forward compatibility but
must be `0`.

LOD fields are stored for future compatibility. v0 requires:

- `LODLevel = 0`
- `LODError = 0.0`
- `ParentStart = 0`
- `ParentCount = 0`

## Parser Validation Contract

The parser mirrors the NVTEX-style status-driven contract. Expected
statuses/cases include:

- success
- invalid blob
- empty blob
- header too small
- bad magic
- unsupported version
- endian mismatch
- header size mismatch
- record size mismatch
- file size mismatch
- nonzero reserved field
- nonzero padding byte
- section out of range
- section misalignment
- section overlap or packing mismatch
- payload hash mismatch
- invalid counts
- invalid float or bounds value
- invalid index range
- invalid cluster range
- invalid string table
- invalid path
- invalid material texture reference
- unsupported v0 feature value
- integer overflow

Validation requirements:

- Header and record sizes must match this document exactly.
- Counts and section sizes must be mutually consistent.
- Section offsets must follow the fixed order and 8-byte alignment.
- Padding bytes must be zero.
- `FileSize` must match the physical blob size and the end of the index payload.
- `PayloadHash` must match the hash over the post-header payload region.
- All floats must be finite; all radii must be non-negative.
- All indices must be in range.
- Cluster ranges must be triangle-aligned, in range, and v0-packed.
- String and path rules must be enforced for material references.
- v0-only fields must reject unsupported future values.

## API / Layer Boundary

The public Asset parser API is `CookedMeshParseResult ParseCookedMesh(AssetBlob)`.
Its status-driven result exposes parsed `CookedMeshData` as `.Mesh`; the result and
mesh retain the original `AssetBlob SourceBlob`, matching the texture parser ownership
pattern.

The Asset API must not expose `GLTFAnalyzer.cpp` private `ModelStagingData`.
`ModelStagingData` is a Resource-layer private staging detail and must remain private.

The Asset layer must not include Rendering or RHI types. There is no Asset -> Rendering
dependency in this design.

The Resource/Rendering-side adapter converts `CookedMeshData` into shared private
model staging data and calls a `FinalizeModelStaging` equivalent. The allowed dependency
direction is:

```text
Resource/Rendering -> Asset
```

not:

```text
Asset -> Resource/Rendering
```

## Version / Invalidation Policy

Wire-incompatible changes require a `format` string and/or `cooked_version` bump and
invalidate existing cooked mesh packages.

Cluster algorithm or cluster settings changes that can alter cluster ranges, index
packing, bounds, cones, or LOD metadata require invalidation. `ClusterAlgorithmId`,
`ClusterAlgorithmVersion`, `ClusterMaxTriangles`, `ClusterMaxVertices`, and
`ClusterSettingsFlags` are serialized so format revisions can detect incompatible cooked
data explicitly.

For v0:

- `ClusterAlgorithmId = 1`
- `ClusterAlgorithmVersion = 0`
- `ClusterMaxTriangles = 128`
- `ClusterMaxVertices = 128`
- `ClusterSettingsFlags = 0`

Changing any of those fields for produced content requires either a new format string,
a `cooked_version` bump, or both, depending on whether the wire schema itself changed.

## Implemented Tests and Acceptance Gates

Focused coverage includes:

- `CookedMeshTest` parser validation.
- `AssetCookMeshSmoke` for glTF -> `.nvmesh` -> `Msh0` package/manifest output.
- `ModelResourcesAssetRuntimeTest`, `ModelAsyncLoadQueueTest`, and
  `MegaGeometryResourcesTest` for cooked model resolution, runtime finalization, and
  cancellation/drain behavior.
- `ModelResourcesAssetLoadProfileContractTest` for the cooked-ready profile contract.
- `AssetManifestReloadBridgeLoopbackTest` for manifest snapshot/Bridge reload.

Helpful focused commands:

```powershell
cmake --build build --config Debug --target CookedMeshTest
ctest --test-dir build -C Debug -R CookedMeshTest
cmake --build build --config Debug --target AssetCook
ctest --test-dir build -C Debug -R AssetCookMeshSmoke
cmake --build build --config Debug --target MegaGeometryResourcesTest
ctest --test-dir build -C Debug -R MegaGeometryResourcesTest
```

The strict real-Game profile proves that cooked-ready model loading uses cooked mesh
parse/finalize stages and does not fall back to loose glTF parsing or clustering.

## Out of Scope

- generic `VertexLayout`
- 16-bit indices
- multi-primitive or multi-submesh behavior
- multi-material support
- animation, skinning, or morph targets
- compression or quantization
- baked LOD hierarchy
- hot reload in-place rebinding
- material hot-rebind
- replacing the loose glTF path
