# AssetCook Workflow

This workflow separates an asset-set spec from the runtime manifest consumed by the engine.

## Asset-Set Spec

`Assets/AssetSets/Rendering3DTestSilverTextures.json` describes the cook input set. It is source-controlled and contains:

- `version`: spec schema version, currently `1`.
- `name`: human-readable asset-set name.
- `package_root`: relative runtime package directory, currently `Cooked/Silver`.
- `default_variant`: variant used when a texture entry omits `variant`, currently `default`.
- `textures`: source images plus cook metadata for each texture package.

Spec paths are resolved by `Scripts/CookTextureAssetSet.ps1`, not by the caller's current working directory. `SpecPath`, `RuntimeRoot`, `ManifestPath`, and `source_path` may be absolute or repository-relative. Manifest and logical fields such as `package_root`, `package_name`, `logical_path`, and `entry_name` must be non-empty relative paths with no absolute, drive-relative, UNC, or `..` traversal segments. They are normalized to `/` before cook validation.

`logical_path` and `entry_name` may use the Game-facing `Assets/...` spelling. The helper normalizes those fields through the same logical-path convention as `AssetCook`, so runtime manifest entries omit the leading `Assets/` prefix, for example `Assets/Textures/Silver/silver_albedo.png` becomes `Textures/Silver/silver_albedo.png`.

## Runtime Manifest

The helper runs `AssetCook` once per texture, using the same manifest path for each invocation. After every invocation it reads the single-entry manifest, verifies the entry against the spec, and collects those entries into one aggregate manifest.

The final manifest defaults to:

```text
<RuntimeRoot>/manifest.json
```

If `-ManifestPath` is supplied, its parent must be exactly `RuntimeRoot`. This keeps `cooked_package` entries relative to `RuntimeRoot`; the helper does not rebase manifests placed elsewhere.

## Current Silver Texture Formats

The direct Rendering3DTest Silver asset set cooks these five textures from
`Assets/AssetSets/Rendering3DTestSilverTextures.json`:

| Logical path | Cook format |
| --- | --- |
| `Assets/Textures/Silver/silver_albedo.png` | `nvtex.v0.rgba8.srgb` |
| `Assets/Textures/Silver/silver_normal-ogl.png` | `nvtex.v0.rgba8.linear` |
| `Assets/Textures/Silver/silver_metallic.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_roughness.png` | `nvtex.v0.r8.linear` |
| `Assets/Textures/Silver/silver_ao.png` | `nvtex.v0.r8.linear` |

`stb_image` is still used at cook time by `AssetCook` to decode source images. Runtime smoke validation expects cooked `nvtex` loads and rejects loose `stb_image` fallback for these paths.

## NVTEX v0.1（ブロック圧縮と R16）

`CookedTextureFormatV0`（`Library/Core/Public/Asset/CookedTextureFormat.h`）の VersionMinor 1 は、v0.0 の上位互換で PixelFormat を足した版。
ヘッダ・ミップ表のレイアウトと、ミップを全段（フルチェーン）必須とする規則は v0.0 と同じ。ローダーは v0.0 と v0.1 のどちらも読む。
`--format` 指定のクック（非圧縮の 3 形式）は v0.0 のまま書く。`--usage` 指定のクック（BC・R16）は v0.2（下記）で書く。

| PixelFormat | 値 | 1 ブロック | ColorSpace |
| --- | --- | --- | --- |
| R8UNorm / RG8UNorm / RGBA8UNorm | 1 / 2 / 3 | 1x1 画素（1 / 2 / 4 バイト） | RGBA8 のみ sRGB 可（v0.0 から） |
| BC1 | 4 | 4x4 画素・8 バイト | Linear / sRGB |
| BC4 | 5 | 4x4 画素・8 バイト | Linear のみ |
| BC5 | 6 | 4x4 画素・16 バイト | Linear のみ |
| BC7 | 7 | 4x4 画素・16 バイト | Linear / sRGB |
| R16UNorm | 8 | 1x1 画素・2 バイト | Linear のみ |

- BC1/BC4/BC5/BC7/R16UNorm は VersionMinor 1 でだけ有効。v0.0 のヘッダにこれらの値があれば `UnknownPixelFormat` で拒否する。
- ミップのデータサイズは、ブロック単位で `ceil(width / 4) * ceil(height / 4) * ブロックのバイト数 * レイヤー数`。
  1x1 や 2x2 のミップも最小 1 ブロック分を持つ。ミップの幅・高さのレコードは画素単位のまま（`max(1, base >> mip)`）。
- 実行時は `MapCookedTextureFormat`（`CookedTextureUpload.cpp`）が RHI の形式（`BC1_UNORM`/`BC1_SRGB`/`BC4_UNORM`/`BC5_UNORM`/`BC7_UNORM`/`BC7_SRGB`/`R16_UNORM`）へ写し、
  ミップごとにブロック単位の行ピッチ・スライスピッチでアップロードする。`textureCompressionBC` に対応しないデバイスでは `TextureCreationFailed` になる。
- glTF の ARM の分割（`TrySplitPreparedCookedTextureMip0RGBA8UNormLinear`）は RGBA8 のときだけ通す。BC は理由（`unsupported pixel format`）を返して失敗する。

## NVTEX v0.2（タイル配置）

VersionMinor 2 は v0.1 の上位互換で、sparse テクスチャの 1 タイル（64 KiB）ずつをファイルの範囲読みで取り出せるように、
段のデータを標準ブロック形状のタイル単位に並べ、タイルの表を持つ。v0.0・v0.1 も引き続き読む。
`AssetCook --usage ...`（`CookAssets` の起動画面の材質を含む）は v0.2 で書く。

- **ヘッダは 160 バイト**（v0.0・v0.1 は 112 バイト）。112 バイト目から、TileWidth・TileHeight（texel）、FirstTailMip、TileDataBytes（65536）、
  TileTableOffset・TileTableSize、TailOffset・TailSize を持つ（`CookedTextureFormatV0::TiledHeaderOffset`）。
- **ファイルの並び**: ヘッダ（160）→ ミップ表 → タイルの表 → ペイロード。ミップ表はヘッダの直後、タイルの表はミップ表の直後、
  ペイロードはタイルの表の直後から始まる。つまり先頭の PayloadOffset バイトがメタデータの全部で、ここまで読めば本体を読まずに表を引ける。
- **標準ブロック形状**（Vulkan の標準 sparse イメージブロック。形式ごとに 1 つに決まり、デバイスに依らないので、クックの時点で固定できる）:

  | 1 ブロックのバイト数 | タイル（ブロック） | 該当する形式 | タイル（texel） |
  | --- | --- | --- | --- |
  | 1 | 256x256 | R8 | 256x256 |
  | 2 | 256x128 | RG8 / R16 | 256x128 |
  | 4 | 128x128 | RGBA8 | 128x128 |
  | 8 | 128x64 | BC1 / BC4 | 512x256 |
  | 16 | 64x64 | BC5 / BC7 | 256x256 |

  どれも 64 KiB。デバイスが標準ブロック形状でないタイルを返す形式は、結び付けの側が VT を使わず全常駐で描く
  （`DeviceCapabilities` の `bStandardBlockShape`）。クックの形状を実行時に変える必要はない。
- **ミップテイル**: 段の幅か高さがタイルより小さい最初の段（FirstTailMip）以降。最後の段（1x1）は必ずタイルより小さいので、テイルは空にならない。
  FirstTailMip はこの規則どおりの値でなければならない（ローダーが形式と大きさから求めて照合する）。デバイスの `imageMipTailFirstLod` との照合は
  結び付けの側の仕事で、食い違う形式・大きさは VT に載せない。
- **ペイロードの並び**: 段の昇順に、FirstTailMip より前の段はタイルを表の順に、それ以降の段は v0.0 と同じ行優先（段の昇順、レイヤーの昇順）で続ける。
  タイルは段を隙間なく分割するので、段のデータサイズ（ミップ表の DataSize）は v0.1 と同じで、ペイロードの全体の大きさも変わらない。
  ミップ表の DataOffset は、タイルの段ではその段のタイルの区間の先頭を指す。
- **タイル 1 枚の中身**: 段の中のタイルの範囲（右端・下端は段の大きさで切り詰める）を、1 行ずつ行優先で詰めたバイト列。行の余白は無く、
  非圧縮は 1 行が（タイルの幅 x 1 画素のバイト数）、ブロック圧縮は 1 行がタイルの幅のブロック分。最大 65536 バイト。
- **タイルの表**（32 バイトの件）: DataOffset・DataSize・MipIndex・LayerIndex・TileX・TileY。並びは「段 → レイヤー → タイルの行 → タイルの列」で、
  件数も各件の値も形式と大きさから決まる。ローダーは全件を照合し、食い違う表（位置・大きさ・番号・件数・タイル形状・ミップテイルの範囲）を
  `TileRecordMismatch`・`TileTableSizeMismatch`・`TileTableOutOfRange`・`InvalidTileShape`・`InvalidFirstTailMip`・`TailRangeMismatch` で拒否する。
  タイルの表はペイロードのハッシュに入らない（ハッシュはペイロードのバイト列だけ）ので、表の食い違いは表の検査が止める。
- **読み込み**: `ParseCookedTexture` は全体を読み、ペイロードのハッシュを検証する。v0.2 は既定で各段を行優先へ展開するので、`GetMipBytes` を
  v0.0 と同じに使える（従来の全常駐のアップロードの経路は変わらない）。ストリーマは `bMaterializeRowMajor = false` で展開を省ける。
  範囲読みは `ReadCookedTextureLayout`（先頭 112 バイトでメタデータの大きさを知り、メタデータだけを読んで表を検証する）→
  `ReadCookedTextureTile`・`ReadCookedTextureMipTail`（表が示す範囲だけを `AssetFileReader::ReadRange` で読む）。パッケージの中の .nvtex は
  baseOffset（エントリのペイロードの位置）を渡す。範囲読みはペイロードのハッシュを検証しない（本体を読まないため）。

## Direct Runtime Contract

The direct Silver workflow loads texture assets through the runtime manifest and package files before creating RHI textures. For cooked-ready entries the expected runtime profile stages are:

- `texture_asset_resolve`
- `texture_cooked_parse`
- `texture_cooked_upload`

The direct smoke rejects `source=loose_stbi` for the cooked Silver logical paths. Loose `stb_image` loads remain valid only when no manifest is loaded, a requested variant is missing, or explicit debug fallback mode permits a cooked failure fallback.

## glTF Prepared Texture Workflow

`Assets/AssetSets/Rendering3DTestSilverGltfTextures.json` describes the model-local Silver fixture used to validate glTF prepared texture loading. The fixture keeps texture URIs below `Assets/Models/Rendering3DTestSilverGltf/` so the cache keys are distinct from the direct Silver texture set:

| Logical path | Cook format | Usage |
| --- | --- | --- |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_albedo.png` | `nvtex.v0.rgba8.srgb` | `standard` |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_normal-ogl.png` | `nvtex.v0.rgba8.linear` | `standard` |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_arm.png` | `nvtex.v0.rgba8.linear` | `arm` |

For glTF model loads, `GLTFAnalyzer` preserves both the logical request path and the resolved loose fallback file path. Worker staging calls `PrepareTextureAssetForWorker()` for the logical path. If the prepared status is `CookedReady`, standard textures skip loose file read and `stb_image` decode on the worker; the main/render side later calls `FinalizePreparedTextureAsset()` and uploads the cooked payload. Packed ARM textures in this fixture use `nvtex.v0.rgba8.linear`; worker staging splits mip 0 into AO, roughness, and metallic R8 data through `texture_prepared_split`.

The expected prepared profile stages are:

- `texture_prepare_asset`
- `texture_prepared_cooked_upload`
- `texture_prepared_finalize`
- `texture_prepared_split`

Prepared cooked loads must not produce `gltf_image_read`, `gltf_image_decode`, or `source=loose_stbi` logs for the model-local cooked source paths. `stb_image` remains part of the cook-time source decode path and loose/debug fallback path, but cooked-ready runtime loads do not use it.

## Cooked Model Opt-In And Four-Entry Fixture

`Scripts/RunCookedModelGameProfile.ps1` turns the model-local Silver fixture into one
runtime root. It first uses `CookTextureAssetSet.ps1` for the three texture packages,
then invokes `AssetCook` for the glTF model and aggregates the model fragment with the
texture manifest. Before Game starts, the runner checks schema version `1` as a
mathematical integer, exactly four entries, unique `logical_path|kind|variant` keys,
normalized non-traversing package paths contained under the runtime root, package
existence, exact model fields, and the exact three glTF image URI/logical-path matches.

Cooked model loading is deliberately opt-in:

```text
--rendering3dtest-use-cooked-model
--texture-asset-root <absolute runtime root>
--texture-asset-manifest <absolute four-entry manifest>
--rendering3dtest-model Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf
```

The bare opt-in flag requires all three accompanying values and rejects duplicates.
Without it, the same root/manifest/model arguments preserve the existing
`GLTFAnalyzer` path and its `GltfPrepared` texture behavior. With it, only the Boulder
model request is issued through `MegaGeometryResources::LoadModelAsync`; the shared
callback, placeholder replacement, scope tracking, release, and cancellation behavior
remain unchanged, with cancellation returning to the same subsystem that issued the
request.

`SummarizeAssetLoadProfile.ps1 -RequireCompleteCookedModel` validates the cooked model
profile as a correlated contract. It requires worker resolve/parse rows from
`cooked_nvmesh`, the same nonzero request id and normalized path, four successful
main-render finalize stages with matching request/debug/path fields, a matching-debug
GPU upload, and a successful async flush that processed work. It rejects loose glTF
read/parse/buffer/extract/clusterize/staging/flush stages. The legacy
`-RequireCompleteModelFlush` switch remains available for loose GLTF profiles; the two
completeness switches are intentionally incompatible.
The cooked contract also requires external anchors so a self-consistent different
model cannot pass: supply `-ExpectedCookedModelLogicalPath` with the normalized logical
path and `-ExpectedCookedModelDebugName` with the original request/debug name. For the
Silver runner these are respectively
`Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf` and
`Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf`.

## Validation Commands

Build the cook tool and game:

```powershell
cmake --build build --config Debug --target AssetCook
cmake --build build --config Debug --target Game
```

Cook the Silver set into a standalone runtime root:

```powershell
.\Scripts\CookTextureAssetSet.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -SpecPath .\Assets\AssetSets\Rendering3DTestSilverTextures.json `
  -RuntimeRoot .\build\CookedAssetSets\Debug\Silver\RuntimeRoot
```

Run the game smoke:

```powershell
.\Scripts\RunCookedTextureGameSmoke.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe
```

Run the glTF prepared smoke:

```powershell
.\Scripts\RunCookedTextureGameSmoke.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe `
  -SpecPath .\Assets\AssetSets\Rendering3DTestSilverGltfTextures.json `
  -ModelPath Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf `
  -ExpectedLoadMode GltfPrepared
```

Run the alternating three-pair real-Game model profile:

```powershell
.\Scripts\RunCookedModelGameProfile.ps1 `
  -AssetCookExe .\build\Tools\AssetCook\Debug\AssetCook.exe `
  -GameExe .\build\Game\Debug\Game.exe
```

The default order is `loose,cooked`, `cooked,loose`, `loose,cooked`. Every run gets its
own working directory, `Game.log`, stdout, stderr, and generated summary beneath the
strict child `build/CookedModelGameProfile/Debug/`. `comparison.json` records all
samples and medians together with HEAD/tree, dirty status, tracked-diff and sorted
untracked hashes, input/output paths, configuration, order, timestamps, OS, and GPU.
The runner treats missing logs/markers, skips, timeouts, nonzero exits, contract
failures, or a non-shorter cooked median as failures and retains the evidence.

Generated packages, manifests, and smoke logs are build outputs. Keep them under `build/` or other ignored output locations and do not commit them.
