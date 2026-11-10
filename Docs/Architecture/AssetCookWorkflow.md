# AssetCook Workflow

This workflow separates an asset-set spec from the runtime manifest consumed by the engine.

## Asset-Set Spec

`Assets/AssetSets/Rendering3DTestSilverTextures.json` describes the cook input set. It is source-controlled and contains:

- `version`: spec schema version, currently `1`.
- `name`: human-readable asset-set name.
- `package_root`: relative runtime package directory, currently `Cooked/Silver`.
- `default_variant`: variant used when a texture entry omits `variant`, currently `default`.
- `textures`: source images plus cook metadata for each texture package.

`Scripts/CookTextureAssetSet.ps1` は `SpecPath`、`RuntimeRoot`、`ManifestPath` を絶対パスまたはリポジトリ相対パスとして解決し、リポジトリを `--source-root` に指定する。spec内の `source_path` は絶対パスまたはsource rootからの相対パス。`package_root`、`package_name`、`logical_path`、`entry_name` は空でなく、絶対パス・ドライブ相対パス・UNC・`..` を含まない相対パスにし、native側で `/` に正規化して検証する。

`logical_path` と `entry_name` にはGame向けの `Assets/...` 表記も使える。native側が先頭の `Assets/` を除くため、例えば `Assets/Textures/Silver/silver_albedo.png` はruntime manifestでは `Textures/Silver/silver_albedo.png` になる。

## Runtime Manifest

包みは `AssetCook --asset-set` を1回呼び出す。specの検証、増分判定、全entryを含むmanifestの書き出しはnative側が行う。

The final manifest defaults to:

```text
<RuntimeRoot>/manifest.json
```

`-ManifestPath` を指定するときは、その親を `RuntimeRoot` と一致させる。native CLIを直接呼ぶ場合も `--manifest` にはruntime root直下の絶対パスを渡す。

未所有の既存runtime rootは拒否する。新しいrootの初期化時には、その親に管理情報の `.norves-assetcook` が作られる。同じ入力での再実行は `unchanged` となり、runtimeのbytesと更新時刻を変えない。包みはnativeの標準出力を転送しないため、結果表示は直接呼び出して確認する。復旧が必要な場合は `AssetCook --recover --runtime-root <root>` を実行し、その親workspaceにある保留中の処理を復旧する。

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
`--format` 指定のクック（非圧縮の 3 形式）は v0.0 のまま書く。`--usage` 指定のクック（BC・R16・RGBA16F・RG16）は v0.2（下記）で書く。

| PixelFormat | 値 | 1 ブロック | ColorSpace |
| --- | --- | --- | --- |
| R8UNorm / RG8UNorm / RGBA8UNorm | 1 / 2 / 3 | 1x1 画素（1 / 2 / 4 バイト） | RGBA8 のみ sRGB 可（v0.0 から） |
| BC1 | 4 | 4x4 画素・8 バイト | Linear / sRGB |
| BC4 | 5 | 4x4 画素・8 バイト | Linear のみ |
| BC5 | 6 | 4x4 画素・16 バイト | Linear のみ |
| BC7 | 7 | 4x4 画素・16 バイト | Linear / sRGB |
| R16UNorm | 8 | 1x1 画素・2 バイト | Linear のみ |
| RGBA16F | 9 | 1x1 画素・8 バイト | Linear のみ（v0.1 以降） |
| RG16UNorm | 10 | 1x1 画素・4 バイト | Linear のみ（v0.1 以降） |

- BC1/BC4/BC5/BC7/R16UNorm/RGBA16F/RG16UNorm は VersionMinor 1 以降で有効。v0.0 のヘッダにこれらの値があれば `UnknownPixelFormat` で拒否する。
- BC 形式のミップのデータサイズは、ブロック単位で `ceil(width / 4) * ceil(height / 4) * ブロックのバイト数 * レイヤー数`。
  1x1 や 2x2 のミップも最小 1 ブロック分を持つ。ミップの幅・高さのレコードは画素単位のまま（`max(1, base >> mip)`）。
- 実行時は `MapCookedTextureFormat`（`CookedTextureUpload.cpp`）が RHI の形式（`BC1_UNORM`/`BC1_SRGB`/`BC4_UNORM`/`BC5_UNORM`/`BC7_UNORM`/`BC7_SRGB`/`R16_UNORM`/`R16G16B16A16_FLOAT`（RGBA16F）/`R16G16_UNORM`（RG16UNorm））へ写し、
  ミップごとにブロック単位の行ピッチ・スライスピッチでアップロードする。BC 形式は `textureCompressionBC` に対応しないデバイスで `TextureCreationFailed` になる。
- glTF の ARM の分割（`TrySplitPreparedCookedTextureMip0RGBA8UNormLinear`）は RGBA8 のときだけ通す。BC は理由（`unsupported pixel format`）を返して失敗する。

## NVTEX v0.2（タイル配置）

VersionMinor 2 は v0.1 の上位互換で、sparse テクスチャの 1 タイル（64 KiB）ずつをファイルの範囲読みで取り出せるように、
段のデータを標準ブロック形状のタイル単位に並べ、タイルの表を持つ。v0.0・v0.1 も引き続き読む。
`AssetCook --usage ...`（`CookAssets` の起動画面の材質を含む）は v0.2 で書く。
用途は `albedo`・`normal`・`orm`・`single`・`height16`・`hdr`・`rg16` を指定できる。

- `--usage hdr`: EXR・HDR の浮動小数の入力を `nvtex.v0.2.rgba16f.linear` へ焼く。無圧縮で全ミップを持ち、ミップは線形の平均で作る。
- `--usage rg16`: R・G を 16bit で保ち、`nvtex.v0.2.rg16.linear` へ焼く。
- `--usage height16`: `nvtex.v0.2.r16.linear` へ焼く。浮動小数の入力には `--height-range <min>,<max>` が必須。
  2 数は有限で `min < max` とし、この範囲を 0..65535 へ正規化する。範囲外の入力は失敗する。
- `--require-16bit` は `height16`・`rg16` で指定でき、8bit の入力を失敗にする。`--height-range` は `height16` だけで指定できる。

- **ヘッダは 160 バイト**（v0.0・v0.1 は 112 バイト）。112 バイト目から、TileWidth・TileHeight（texel）、FirstTailMip、TileDataBytes（65536）、
  TileTableOffset・TileTableSize、TailOffset・TailSize を持つ（`CookedTextureFormatV0::TiledHeaderOffset`）。
- **ファイルの並び**: ヘッダ（160）→ ミップ表 → タイルの表 → ペイロード。ミップ表はヘッダの直後、タイルの表はミップ表の直後、
  ペイロードはタイルの表の直後から始まる。つまり先頭の PayloadOffset バイトがメタデータの全部で、ここまで読めば本体を読まずに表を引ける。
- **標準ブロック形状**（Vulkan の標準 sparse イメージブロック。形式ごとに 1 つに決まり、デバイスに依らないので、クックの時点で固定できる）:

  | 1 ブロックのバイト数 | タイル（ブロック） | 該当する形式 | タイル（texel） |
  | --- | --- | --- | --- |
  | 1 | 256x256 | R8 | 256x256 |
  | 2 | 256x128 | RG8 / R16 | 256x128 |
  | 4 | 128x128 | RGBA8 / RG16 | 128x128 |
  | 8 | 128x64 | BC1 / BC4 | 512x256 |
  | 8 | 128x64 | RGBA16F | 128x64 |
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

## モデルの取込設定（GR78）

source隣の `<source>.import.json` をstatic/skeletalのcookとlooseで共有します。
設定の意味・骨格での制約・hashの互換性は [ImportSettings.md](ImportSettings.md) を参照してください。
CLIでは `--import-settings <file>` で正本を指定し、`--require-sidecar` で不在を検出、
`--no-sidecar` で従来の無設定経路を選べます。無効化と前二つは同時指定できません。

診断のみの場合は `AssetCook --inspect <model.gltf|model.glb>` を使います。
元のmesh-local形状、画像の8/16bit整数統計、材質係数を表示し、sourceや設定fileを変更しません。
単一mesh/primitiveの現行static profileが対象です。表示されるforward候補の符号と実寸は手動で決めてください。
