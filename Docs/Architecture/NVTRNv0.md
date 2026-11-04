# NVTRN v0 地形形式

## 目的と範囲

NVTRN v0 は、地形の高さとタイルごとのレイヤを格納する cook 済み形式。
高さは `HGT0` の生の `uint16` 標本として、ほかのレイヤは NVTEX v0 のファイル全体として埋め込む。

cook の使い方と処理は下の「cook（AssetCook --kind world）」に記す。実行時の地形・範囲読みは、この文書の範囲外。

定数・型・入口は [CookedTerrainFormat.h](../../Library/Core/Public/Asset/CookedTerrainFormat.h)、
書き出しは [CookedTerrainWriter.cpp](../../Library/Core/Private/Asset/CookedTerrainWriter.cpp) にある。
名前空間は `NorvesLib::Core::Asset`、形式の定数は `CookedTerrainFormatV0` に置く。

## 識別と数値の表現

| 項目 | 値 |
| --- | --- |
| `Magic` | `{'N','V','T','R','N','v','0','\0'}`（8 バイト、末尾 NUL を含む） |
| `VersionMajor` / `VersionMinor` | 0 / 0 |
| `EndianMarker` | `0x01020304` |
| `HeaderSize` | 192 バイト |
| `LayerRecordSize` | 32 バイト |
| `TileRecordHeadSize` / `TileLayerRangeSize` | 80 / 24 バイト |
| `PayloadAlignment` | 64 バイト |
| `MaxLodCount` / `MaxLayerCount` / `MaxSemanticBytes` | 8 / 16 / 255 |
| `LayerEncodingRawHeight16` / `LayerEncodingEmbeddedNvtex` | 1 / 2 |

数値はすべてリトルエンディアン。整数は符号なし、`float` は IEEE-754 binary32、
`double` は IEEE-754 binary64。C++ 構造体のメモリ配置をそのまま保存する形式ではない。
FourCC のバイト順は `MakeAssetPackageFourCC` と同じ。
位置は、相対と明記した欄を除き、ファイル先頭からの絶対バイトオフセット。

## ファイルの並び

```text
header -> layer table -> tile table -> string table -> padding -> payload
```

表どうしは隙間なく並ぶ。`TileCount = TileCountX * TileCountZ` とすると、配置は次のとおり。

| 領域 | 先頭 | 大きさ |
| --- | --- | --- |
| ヘッダ | 0 | 192 |
| レイヤ表 | `LayerTableOffset = HeaderSize` | `LayerTableSize = LayerCount * 32` |
| タイル表 | `TileTableOffset = LayerTableOffset + LayerTableSize` | `TileTableSize = TileCount * TileRecordSize` |
| 文字列表 | `StringTableOffset = TileTableOffset + TileTableSize` | `StringTableSize` |
| ペイロード | `PayloadOffset = AlignUp(StringTableOffset + StringTableSize, 64)` | `PayloadSize = FileSize - PayloadOffset` |

`AlignUp(value, 64)` は 64 の倍数への切り上げ。
文字列表の終端からペイロード先頭までと、ペイロード内のレイヤ間の隙間は 0 で埋める。
`PayloadOffset + PayloadSize == FileSize` であり、writer は最後のレイヤの終端をファイル末尾とする。

### ヘッダ（192 バイト）

オフセットは `HeaderOffset` の各定数に対応する。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint8[8]` | `Magic` | `NVTRNv0\0` |
| 8 | `uint32` | `HeaderSize` | 192 |
| 12 | `uint16` | `VersionMajor` | 0 |
| 14 | `uint16` | `VersionMinor` | 0 |
| 16 | `uint32` | `EndianMarker` | `0x01020304` |
| 20 | `uint32` | `Flags` | 予約、0 |
| 24 | `uint64` | `FileSize` | ファイル全体のバイト数、入力 blob の大きさと一致 |
| 32 | `uint64` | `PayloadHash` | この欄の 8 バイトを除くファイル全体の FNV-1a64 |
| 40 | `uint32` | `CellCountX` | 地形全体の x 方向のセル数、正 |
| 44 | `uint32` | `CellCountZ` | 地形全体の z 方向のセル数、正 |
| 48 | `uint32` | `TileCells` | タイルの一辺のセル数、正 |
| 52 | `uint32` | `TileCountX` | `CellCountX / TileCells`、割り切れること |
| 56 | `uint32` | `TileCountZ` | `CellCountZ / TileCells`、割り切れること |
| 60 | `uint32` | `LodCount` | 1..8 |
| 64 | `float` | `CellSizeM` | セルの一辺 [m]、有限で正 |
| 68 | `uint32` | `Reserved0` | 予約、0 |
| 72 | `double` | `OriginX` | 全体の標本 (0,0) の世界座標 x [m]、有限 |
| 80 | `double` | `OriginY` | 高さの基準 [m]、有限 |
| 88 | `double` | `OriginZ` | 全体の標本 (0,0) の世界座標 z [m]、有限 |
| 96 | `float` | `HeightMinM` | 標本値 0 の高さ [m]、有限、OriginY を含まない |
| 100 | `float` | `HeightMaxM` | 標本値 65535 の高さ [m]、有限で HeightMinM 以上 |
| 104 | `uint32` | `LayerCount` | 1..16 |
| 108 | `uint32` | `LayerRecordSize` | 32 |
| 112 | `uint32` | `TileRecordSize` | `80 + LayerCount * 24` |
| 116 | `uint32` | `Reserved1` | 予約、0 |
| 120 | `uint64` | `LayerTableOffset` | レイヤ表の絶対位置、192 |
| 128 | `uint64` | `LayerTableSize` | `LayerCount * 32` |
| 136 | `uint64` | `TileTableOffset` | レイヤ表の直後の絶対位置 |
| 144 | `uint64` | `TileTableSize` | `TileCountX * TileCountZ * TileRecordSize` |
| 152 | `uint64` | `StringTableOffset` | タイル表の直後の絶対位置 |
| 160 | `uint64` | `StringTableSize` | 意味の文字列を連結したバイト数 |
| 168 | `uint64` | `PayloadOffset` | 文字列表の終端を 64 バイト境界へ切り上げた絶対位置 |
| 176 | `uint64` | `PayloadSize` | ペイロード先頭からファイル末尾までのバイト数 |
| 184 | `uint64` | `Reserved2` | 予約、0 |

## レイヤ表と文字列表

レイヤレコードは 32 バイト。`LayerRecordOffset` は各レコード先頭からの相対位置。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint32` | `FourCC` | レイヤ識別子、重複不可 |
| 4 | `uint32` | `Encoding` | 1 = RawHeight16、2 = EmbeddedNvtex |
| 8 | `uint32` | `PixelFormat` | `CookedTexturePixelFormat` の既知の値 |
| 12 | `uint32` | `ColorSpace` | `CookedTextureColorSpace::Linear` または `SRGB` |
| 16 | `uint32` | `SemanticOffset` | 文字列表先頭からの相対バイト位置 |
| 20 | `uint32` | `SemanticSize` | 意味の文字列のバイト数、1..255、NUL を含まない |
| 24 | `uint64` | `Reserved` | 予約、0 |

レイヤ 0 は `FourCC = HGT0`、`Encoding = 1`、`PixelFormat = R16UNorm`（8）、
`ColorSpace = Linear`（1）に固定する。
レイヤ 1 以降は `FourCC != HGT0`、`Encoding = 2`。
埋め込む NVTEX の形式・色空間はレイヤ表の宣言と一致させる。

文字列表はレイヤの意味を表す文字列を NUL 終端なしで連結する。
各文字は印字可能 ASCII（0x20..0x7E）に限り、参照区間は文字列表の内側に収める。
writer はレイヤ表の順に文字列を連結し、各 SemanticOffset を記録する。

## タイル表とレイヤ区間

タイルレコードは `80 + LayerCount * 24` バイト。
先頭 80 バイトは `TileRecordOffset` の固定部分、その後にレイヤ表と同じ順で区間レコードを置く。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint32` | `TileX` | x 座標、TileCountX 未満 |
| 4 | `uint32` | `TileZ` | z 座標、TileCountZ 未満 |
| 8 | `float` | `HeightMinM` | 写像後のタイルの最小高さ [m]、OriginY を含まない |
| 12 | `float` | `HeightMaxM` | 写像後のタイルの最大高さ [m]、OriginY を含まない |
| 16 | `float[3]` | `AabbMin` | 原点 (OriginX, OriginY, OriginZ) からの相対最小座標 [m] |
| 28 | `float[3]` | `AabbMax` | 原点からの相対最大座標 [m] |
| 40 | `uint32` | `Reserved0` | 予約、0 |
| 44 | `float[8]` | `LodMaxErrorM` | 各 LOD の最大誤差 [m] |
| 76 | `uint32` | `Reserved1` | 予約、0 |
| 80 | 区間レコードの配列 | `LayerRanges` | LayerCount 件、1 件 24 バイト |

高さの min/max と AABB の各成分は有限で min <= max。
`LodMaxErrorM[lod]` は `lod < LodCount` なら有限で非負、それ以外は 0 とする。
タイルは `TileCountX * TileCountZ` 件で、全座標がちょうど 1 回ずつ現れる。
writer は入力順によらず TileZ 昇順、各行で TileX 昇順に書く。
読み手はタイル表の順を問わず、座標からタイル番号への表を作る。

レイヤ番号を i とすると、区間レコードはタイル先頭の `80 + i * 24` にある。
`TileLayerRangeOffset` はその区間レコード先頭からの相対位置。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint64` | `DataOffset` | データの絶対バイト位置、64 の倍数 |
| 8 | `uint64` | `DataSize` | 正のバイト数、HGT0 は `(TileCells + 1)² * 2` |
| 16 | `uint64` | `DataHash` | このタイル・レイヤのデータ全体の FNV-1a64 |

各区間 `[DataOffset, DataOffset + DataSize)` は `[PayloadOffset, FileSize)` の内側に収める。

### ペイロード

writer はタイル表の順 × レイヤ表の順に、各データの開始を 64 バイト境界へ切り上げて配置する。
読み手もペイロードをタイル表の順 × レイヤ表の順に 64 バイト境界へ詰めた並びとして求め、違えば TableLayoutMismatch とする。
HGT0 は生の高さ標本、それ以外は NVTEX v0 のヘッダとペイロードを含むファイル全体を格納する。
DataSize は前後のアラインメント用の隙間を含まない。
LodCount は誤差欄の有効数であり、HGT0 の標本列を LOD ごとに複数格納する指定ではない。

## 高さの標本と世界座標

1 タイルに `(TileCells + 1)²` 個の uint16 リトルエンディアン標本を格納する。
z 行ごとに x 昇順の行優先で、標本 (sampleX, sampleZ) のバイト位置は次になる。

```text
DataOffset + 2 * (sampleZ * (TileCells + 1) + sampleX)
```

両座標の範囲は 0..TileCells。隣接タイルは境界の標本を共有する。
ファイル上は各タイルが端の標本をそれぞれ持つため、右端と隣の左端、下端と隣の上端には同じ値を格納する。

ヘッダの HeightMinM/HeightMaxM を使い、標本値 v を次のように高さ [m] へ写像する。
タイルレコードの min/max を写像の範囲として使わない。

```text
heightM = HeightMinM + (HeightMaxM - HeightMinM) * v / 65535
worldX = OriginX + (TileX * TileCells + sampleX) * CellSizeM
worldY = OriginY + heightM
worldZ = OriginZ + (TileZ * TileCells + sampleZ) * CellSizeM
```

`CookedTerrainData::DecodeHeightM` は heightM だけを返し、OriginY は使う側が加算する。
HeightMinM == HeightMaxM も有効で、全標本は同じ高さに写る。

## ハッシュ

FNV-1a64 の初期値は `14695981039346656037`、素数は `1099511628211`。
各バイトについて XOR 後に素数を掛け、64 ビットの桁あふれは 2^64 を法として扱う。

- `PayloadHash`: ファイルの先頭から末尾までのうち、オフセット 32..39 の自分自身の 8 バイトだけを
  入力から除く。0 として入力する処理ではない。ヘッダのほかの値、表、文字列、隙間、全データを含めるため、
  ヘッダの値の改変も検出対象になる。
- `DataHash`: 各タイル・各レイヤの `[DataOffset, DataOffset + DataSize)` 全体を対象とする。
  HGT0 は生の標本列、埋め込み NVTEX はヘッダを含むファイル全体。前後の隙間は含めない。

全体ハッシュは `ComputeCookedTerrainFileHash`、各レイヤのハッシュは
`ComputeCookedTexturePayloadHash` で計算する。
`ComputeCookedTerrainFileHash` は 40 バイト未満の入力では全バイトを対象とするが、有効な NVTRN は 192 バイト以上。
FNV-1a64 は暗号学的な真正性を保証するものではない。

## 拒否の種別と検査順

`CookedTerrainParseStatus` の列挙順は次のとおり。`Success` は全検査が成功した結果を表す。

| 種別 | 条件 |
| --- | --- |
| `Success` | 成功 |
| `InvalidBlob` | 無効または空の blob |
| `HeaderTooSmall` | 192 バイト未満 |
| `BadMagic` | Magic が不一致 |
| `UnsupportedVersion` | 主版または副版が 0 以外 |
| `EndianMismatch` | EndianMarker が不一致 |
| `HeaderSizeMismatch` | HeaderSize が 192 以外 |
| `FileSizeMismatch` | 宣言したファイル長と実際の長さが不一致（切り詰めを含む） |
| `ReservedFieldNonZero` | ヘッダ・レイヤ・タイルの予約欄、または未使用 LOD 欄が 0 以外 |
| `PayloadHashMismatch` | ファイル全体のハッシュが不一致 |
| `InvalidDimensions` | セル数・TileCells が 0、割り切れない、TileCount が不一致、セル寸法・高さの写像・原点が不正 |
| `InvalidLodCount` | LodCount が 1..8 の外 |
| `InvalidLayerCount` | LayerCount が 1..16 の外 |
| `RecordSizeMismatch` | LayerRecordSize または TileRecordSize が不一致 |
| `TableLayoutMismatch` | 表の順序・大きさ、64 バイト境界、ファイル長との整合が不正 |
| `IntegerOverflow` | 大きさ・位置などの整数演算や格納可能な件数を超過 |
| `InvalidLayerRecord` | レイヤの規則または意味の文字列が不正 |
| `DuplicateLayerFourCC` | レイヤの FourCC が重複 |
| `TileIndexOutOfRange` | タイル座標が範囲外 |
| `DuplicateTile` | タイル座標が重複 |
| `InvalidTileBounds` | 高さ・AABB が非有限または min > max、有効 LOD 誤差が非有限または負 |
| `LayerRangeOutOfRange` | データ区間がペイロード外、開始が 64 バイト境界以外、または大きさが 0 |
| `HeightSizeMismatch` | HGT0 の大きさが `(TileCells + 1)² * 2` と不一致 |
| `LayerHashMismatch` | DataHash が不一致 |
| `EmbeddedTextureInvalid` | `ParseCookedTexture(sub, false)` の失敗、または形式・色空間がレイヤ表と不一致 |

検査は次の順に行う。整数演算の桁あふれは、その値を使う前に検査する。
表の予約欄と未使用 LOD 欄は、全体ハッシュより前にまとめて調べるのではなく、その表を読む段階で検査する。

1. blob が無効・空でないこと、ヘッダ長、Magic、版、EndianMarker、HeaderSize、FileSize、
   ヘッダの予約欄（Flags・Reserved0・Reserved1・Reserved2）の順に検査する。
2. PayloadHash を検査する。
3. 寸法・原点・高さの写像、LodCount、LayerCount、レコード長の順に検査する。
4. 大きさ・位置の整数演算の桁あふれを検査し、表の配置・大きさ・64 バイト境界・ファイル末尾との整合、
   文字列表の終端からペイロードまでの 0 埋めを検査する（TableLayoutMismatch）。
   続いて、タイル番号と各配列に格納できる件数の上限を検査する（IntegerOverflow）。
5. レイヤ表は全件の予約欄を先に検査する。次に各件の規則と意味の文字列を検査し、
   文字列が先頭から連続して詰まり、印字可能 ASCII であることを求める。
   その後、文字列の合計と文字列表の大きさの一致、FourCC の重複の順に検査する。
6. タイル表は全件の予約欄と未使用 LOD 欄を先に検査する。
   続いて、座標の範囲、座標の重複、高さ・AABB・使用する LOD の誤差、各区間の範囲、
   HGT0 の大きさ、DataHash、埋め込み NVTEX の順に、それぞれ全件を検査してから次へ進む。
7. 最後に、正常な区間がタイル表の順 × レイヤ表の順に、各開始を 64 バイト境界へ切り上げて
   隙間なく詰まり、整列の隙間が 0 で、最後の区間の終端がファイル末尾であることを検査する
   （TableLayoutMismatch。切り上げの桁あふれは IntegerOverflow）。
   大きさやハッシュの不正を先にそれぞれの種別で分類するため、この配置検査を最後に行う。

## manifest との結び付き

| manifest の項目 | 値 |
| --- | --- |
| `kind` | `"world"`（`AssetKind::World = 7`） |
| `entry_type` | `"Trn0"`（`CookedTerrainFormatV0::PackageEntryType`） |
| `format` | `"nvtrn.v0.height16"`（`CookedTerrainFormatV0::ManifestFormat`） |

World は地形・草の場・散布で共用する種別。
草の場・散布は将来それぞれ `Gfd0`・`Sct0` を同じ World に載せる予定。

## cook（AssetCook --kind world）

地形スペックの UTF-8 JSON を入力し、NVTRN を 1 件の `Trn0` エントリとしてパッケージへ格納し、
manifest を作成・更新する。コマンドの形は次のとおり（パスは用途に合わせて指定する）。

```powershell
build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --kind world --format nvtrn.v0.height16 --entry-type Trn0 --input <spec> --out <package.nvpkg> --manifest <manifest.json> --logical Terrain/Example/Example.nvtrn --entry Terrain/Example/Example.nvtrn
```

`--kind world` には `--format nvtrn.v0.height16` と `--entry-type Trn0` を使う。
`Raw` など別の entry type は拒否される。既存の manifest があるときは対象の参照を統合する。

### CookAssets の一覧

[CookAssets.ps1](../../Scripts/CookAssets.ps1) は `Assets/AssetSets/*.json` の省略可能な
`worlds` 配列から同じ cook を呼ぶ。各項目は次の欄を持つ。

| 欄 | 指定内容 |
| --- | --- |
| `logical_path` | 地形の論理パス。`Assets/` 接頭辞は正規化で除く |
| `source_path` | 地形スペックのパス。リポジトリのルートを基準に解決する |
| `package_name` | 一覧の `package_root` 以下に作るパッケージの相対パス |
| `entry_name` | パッケージ内のエントリ名。`Assets/` 接頭辞は正規化で除く |
| `format` | 省略時 `nvtrn.v0.height16` |
| `variant` | 省略時は一覧の `default_variant` |

実例は [GrasslandTerrain.json（一覧）](../../Assets/AssetSets/GrasslandTerrain.json)。
スペック自身と `height_source.path` の入力の両方を変更検出に含める。
対象を絞る場合は次のように実行する。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe -OnlyLogicalPath "Terrain/Grassland/*"
```

`-OnlyLogicalPath` は対象外の項目を既存の `build/CookedAssets/manifest.json` から引き継ぐため、
対象外の参照がそろった manifest が必要。入力と印が一致する項目は省略し、`-Force` を加えると対象を焼き直す。

### スペック

ルートはオブジェクトで、`version` は整数 `1`。
ルート・`height_source`・`height_mapping` の未知のキーは拒否する。
数値は有限であることを求め、整数欄は小数を受け付けない。

| 欄 | 意味・条件・省略時の値 |
| --- | --- |
| `height_source` | 必須の入力オブジェクト。以下の欄を持つ |
| `height_source.path` | 必須。完全な絶対パス、または**スペックの親ディレクトリ**を基準にした相対パス |
| `height_source.format` | 必須。`png16`・`png8`・`raw16`・`raw32f`・`exr` |
| `height_source.width` / `height_source.height` | RAW だけに必須の標本数。それぞれ 2..4294967295 の整数。他形式では指定を拒否 |
| `height_source.endian` | RAW だけに指定可能。`little`（既定）または `big` |
| `height_source.exr_channel` | EXR だけに指定可能。チャンネル名を完全一致で選ぶ。省略時は単一チャンネル、または `Y` を選択し、選べなければ拒否 |
| `height_source.flip_z` | 真偽値。既定 `false`。`true` なら z 行を反転する |
| `height_mapping`（整数入力） | `png16`・`raw16`・`png8` では必須。`min_m` と `max_m` の組、または `scale_m` と省略可能な `offset_m`（既定 0）を指定。両方式の混在と `scale` / `offset` は拒否 |
| `height_mapping`（浮動小数点入力） | `raw32f`・`exr` は `scale`（既定 1）・`offset`（既定 0）。オブジェクトごと省略可能。`min_m` / `max_m` / `scale_m` / `offset_m` は拒否 |
| `cell_size_m` | 必須。有限で正の標本間隔 [m] |
| `tile_cells` | タイルの一辺のセル数。既定 128。8..1024 の 2 の冪 |
| `fit` | `error`（既定）・`crop`・`resample`。後述の寸法調整 |
| `require_bit_depth` | 整数 8 または 16。16 は 8bit 入力を拒否する。8 または省略は制限なしで、16/32bit も受け付ける |
| `smooth` | ガウス平滑化の σ [m]。有限で 0 以上。既定 0（平滑化なし） |
| `seed` | 0..4294967295 の整数。既定 0。現状の高さ処理には使わず、スペックのバイトとして `source_hash` に影響する |
| `recipe` | 省略可能なオブジェクト。現状はオブジェクトという形だけを検査し、内部のキーの検査・生成処理は行わない |

整数入力の `min_m/max_m` は `max_m >= min_m` を求め、
`heightM = min_m + v * (max_m - min_m) / D`（PNG8 は `D=255`、ほかは `65535`）で写像する。
`scale_m/offset_m` は生の整数値 `v` に `heightM = offset_m + scale_m * v` を適用する。
RAW32F・EXR は `heightM = offset + scale * v`。入力の単位を推測しない。
入力値と写像後の高さの NaN / Inf は拒否する。

PNG は 2×2 標本以上で、指定した 8/16bit と実ファイルのビット深度が一致する必要がある。
RGB/RGBA は全標本で R=G=B を求め、アルファは使わない。
RAW はヘッダなしの z 行優先・x 昇順で、長さは `width * height * 2`（RAW16）または
`width * height * 4`（RAW32F、IEEE-754）と一致させる。
EXR は選んだチャンネルの標本間隔が 1×1 で、half・float・uint を受け付ける。

`fit` は幅・高さをそれぞれ `tile_cells*N+1`（`N>=1`）へ合わせる。
`error` は不一致を拒否する。`crop` は (x,z)=(0,0) 側を残し、余った大きい座標側を切り捨てる
（1 タイルに満たなければ拒否）。`resample` は `N=max(1, round((sourceSamples-1)/tile_cells))`
で近い寸法を選び、入力両端を対応させた双線形補間を行う。標本間隔は `cell_size_m` のままなので、
寸法調整で地形の物理的な広さも変わる。

PNG8 は `terrain_height_bit_depth=8` を含む警告を出し、1 段の高さ（写像の scale の絶対値）を知らせる。
16bit への再量子化で元の 256 段が増えるわけではない。`smooth` は段差を均す手段となる。
σ を標本単位へ変換して半径 `ceil(3*σ/cell_size_m)` の分離ガウスを x、z の順にかけ、
範囲外の標本位置は端へ丸める。

実際のスペックは [GrasslandTerrain.json](../../Assets/TerrainSpecs/Grassland/GrasslandTerrain.json)。
PNG16 を 0..64 m に写像し、標本間隔 1 m・128 セル/タイル・`fit=error`・16bit 必須で取り込む。

### 処理の順序と生成値

1. スペックと高さ入力を読み、指定した形式で標本を取り込む。
2. 高さをメートルへ写像する。
3. `flip_z` を適用する。
4. `fit` で格子の寸法を合わせる。
5. `smooth` を適用する。
6. 全体の min/max（ヘッダには float で格納）を使い、
   `round((heightM-min)/(max-min)*65535)` を 0..65535 へ制限して `uint16` 量子化する。
   全標本が同じ高さなら 0 とする。
7. 全体格子の中心差分（外周のみ片側差分）から法線を計算する。
   量子化前の高さの微分を `dx=dh/dx, dz=dh/dz` とし、`normalize(-dx,1,-dz)` を使う。
   量子化と法線計算は全体を走査する同じループ内で行う。
8. タイルへ切り出す。各タイルは `(tile_cells+1)²` 標本を持ち、境界 1 標本の高さと法線を共有する。
9. 量子化した高さを復元し、各タイルの LOD 誤差・高さの min/max・AABB を求める。
10. HGT0 と NRM0 を NVTRN へ書き、`Trn0` エントリのパッケージと manifest を書く。

`NRM0` は線形 RG8（`RG8UNorm`）の NVTEX 全体を埋め込んだレイヤ。
R に x、G に z を `round((n*0.5+0.5)*255)`（0..255 に制限）で符号化する。
読み手は R/G を `n=byte*2/255-1` で戻し、上向きの y を
`sqrt(max(0,1-x²-z²))` で復元する。ミップは領域平均で生成し、法線の再正規化はしない。

LOD 数は 4、stride は順に 1/2/4/8。LOD0 の誤差は 0。
各粗いセルの境界を含む全 LOD0 標本について、粗いセルの四隅から両方の対角線による
三角形補間を計算し、量子化後に復元した LOD0 の高さとの差の絶対値の最大を記録する。
両対角を含めるため、描画側の対角線の向きに依存しない。単位は m、集計は double、格納は float。

AABB は原点からの相対座標で、タイル (tx,tz)、`T=tile_cells`、`c=cell_size_m` に対し、
min は `(tx*T*c, tileHeightMin, tz*T*c)`、
max は `((tx+1)*T*c, tileHeightMax, (tz+1)*T*c)`。
y の範囲は量子化後に復元した全タイル標本の min/max で、x/z はタイルのセル範囲。
この cook が生成する原点は (0,0,0)。

### manifest と決定性

`kind=world`、`entry_type=Trn0`、`format=nvtrn.v0.height16`、`cooked_version=0`。
`cooked_hash` はパッケージに入る NVTRN の全バイトを対象にした payload の FNV-1a64 で、
NVTRN ヘッダの `PayloadHash`（自身の 8 バイトを除く）とは異なる。
`source_hash` はスペックと参照した全入力のバイトから求める FNV-1a64。
現状はスペック、高さファイルの順に、各ファイルのバイト長を uint64 リトルエンディアン 8 バイトで投入し、
続けてそのファイルの全バイトを投入する。パスの文字列ではなくファイル内容が対象で、
JSON の空白や seed だけの変更もハッシュを変える。

同じバイナリと同じ入力なら同じ NVTRN バイトを生成する。
浮動小数点処理の決定性の保証は同じバイナリ内に限り、コンパイラ・構成・実行環境の異なるバイナリ間の
バイト一致を前提にしない。

### 実装と試験の所在

入口は [SingleAssetCook.cpp](../../Tools/AssetCook/SingleAssetCook.cpp) の `CookWorldAsset`、
スペックと入力ハッシュは [TerrainSpec.cpp](../../Tools/AssetCook/TerrainSpec.cpp)、
取り込みは [HeightfieldImport.cpp](../../Tools/AssetCook/HeightfieldImport.cpp)、
量子化・法線・タイル生成は [TerrainCooker.cpp](../../Tools/AssetCook/TerrainCooker.cpp) にある。

[CookedTerrainTest](../../Test/Core/Asset/CookedTerrainTest.cpp) は形式の検査に加え、解析面で次を検査する。

| 項目 | 検査 |
| --- | --- |
| (a) | 高さの最大誤差が半量子化幅 + 2 ULP 以下 |
| (b) | x/z 両方向の共有辺で HGT0・NRM0 が一致 |
| (c) | 解析的な法線との最大角度が 0.5 度以下 |
| (d) | AABB と stride 1/2/4/8 の LOD 誤差が独立した計算と一致 |
| (e) | PNG16・両 endian の RAW16・RAW32F・EXR の一致と EXR の端数保持 |
| (f) | 8bit 警告、16bit 必須指定、不正寸法の拒否 |
| (g) | 再 cook のバイトと payload hash が一致し、入力や seed の変更で source hash が変化 |

[AssetCookTerrainSmoke](../../Tools/AssetCook/AssetCookTerrainSmoke.cmake.in) は CLI から 33×33 の PNG16 を
16 セル/タイルの 2×2 タイルへ cook し、manifest の kind・entry type・format と 16 桁のハッシュを確認する。
`--entry-type Raw` が適切な診断で拒否されることも検査する。

### 2049×2049 合成入力の計測

測定日: 2026-11-04。Windows、CPU: 13th Gen Intel(R) Core(TM) i9-13900KF。
Visual Studio 2022（MSVC 14.44）、x64、**RelWithDebInfo** の AssetCook を使用した。

```powershell
cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1
```

入力は 2049×2049 のリトルエンディアン RAW16（8,396,802 バイト）。
整数座標 `x,z=0..2048`、`seed=89021` とし、次の滑らかな周期関数を最近接整数へ丸めた。
乱数生成器は使わず、seed の剰余で x の位相を定める。

```text
v(x,z) = floor(32768
             + 12000*sin(2*pi*(x + seed%257)/2048)*cos(2*pi*z/2048)
             + 6000*cos(4*pi*z/2048) + 0.5)
heightM = -128 + 256*v/65535
```

`cell_size_m=1`、`tile_cells=128`、`fit=error`、`flip_z=false`、
`require_bit_depth=16`、`smooth=0`、`recipe={}`。
2048×2048 セル、16×16 タイル、各タイル 129×129 標本、4 LOD の出力となる。
次のコマンドで一時ディレクトリを用意する。

```powershell
$benchDir = Join-Path ([System.IO.Path]::GetTempPath()) ('NvtrnBenchmark-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $benchDir | Out-Null
```

次の Python 3 台本を `$benchDir/generate.py` に保存する。
台本は出力先の一時ディレクトリを引数に受け取る。

```python
import json, math, pathlib, struct, sys
root = pathlib.Path(sys.argv[1])
side, seed = 2049, 89021
with (root / "height.raw").open("wb") as output:
    for z in range(side):
        values = [
            math.floor(32768 + 12000 * math.sin(2 * math.pi * (x + seed % 257) / 2048)
                       * math.cos(2 * math.pi * z / 2048)
                       + 6000 * math.cos(4 * math.pi * z / 2048) + 0.5)
            for x in range(side)
        ]
        output.write(struct.pack("<2049H", *values))
spec = {
    "version": 1,
    "height_source": {"path": "height.raw", "format": "raw16", "width": side,
                      "height": side, "endian": "little", "flip_z": False},
    "height_mapping": {"min_m": -128.0, "max_m": 128.0},
    "cell_size_m": 1.0, "tile_cells": 128, "fit": "error",
    "require_bit_depth": 16, "smooth": 0.0, "seed": seed, "recipe": {}
}
(root / "terrain.json").write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
print(f"RAW16 bytes={(root / 'height.raw').stat().st_size} samples={side}x{side} seed={seed}")
```

PowerShell での実行例。各回は新しい AssetCook プロセスで、
別のパッケージと manifest を書く。

```powershell
python (Join-Path $benchDir 'generate.py') $benchDir
1..3 | ForEach-Object {
    build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --kind world --format nvtrn.v0.height16 --entry-type Trn0 --input "$benchDir/terrain.json" --out "$benchDir/run$_.nvpkg" --manifest "$benchDir/run$_.manifest.json" --logical Terrain/Benchmark/Height.nvtrn --entry Terrain/Benchmark/Height.nvtrn
    (Get-Item -LiteralPath "$benchDir/run$_.nvpkg").Length
}
```

`AssetCook wrote world ...` の `elapsed_ms`・`peak_working_set_bytes`・`nvtrn_bytes` を採用し、
パッケージのバイト数は出力ファイル長から取得する。
`elapsed_ms` は `CookTerrainSpecFile` 呼び出し直前からパッケージ・manifest の書き込み完了までで、
最初の引数・パス処理とスペックの先読み、および書き込み後の読み戻し検証は含まない。
`peak_working_set_bytes` はその時点で取得した Windows の `PeakWorkingSetSize`
（プロセス開始からの作業セットのピーク）で、cook の確保量だけを示す値ではない。
入力生成・ビルド時間は計測に含めない。ファイルキャッシュを消去せず、3 回を順に実行した。

| 実行 | elapsed_ms [ms] | peak_working_set_bytes [バイト] | NVTRN [バイト] | パッケージ [バイト] |
| --- | ---: | ---: | ---: | ---: |
| 1 | 415 | 106,610,688 | 19,988,764 | 19,988,956 |
| 2 | 422 | 106,094,592 | 19,988,764 | 19,988,956 |
| 3 | 415 | 106,102,784 | 19,988,764 | 19,988,956 |
| 中央値 | 415 | 106,102,784 | 19,988,764 | 19,988,956 |
| 範囲（最小～最大） | 415～422 | 106,094,592～106,610,688 | 19,988,764～19,988,764 | 19,988,956～19,988,956 |

3 回とも終了コード 0、`cooked_hash=591a4f15478f4fa7` で一致した。
この値は上の入力・平滑化なし・同一機械での実測であり、平滑化や別の入力形式の費用を含まない。

## 読み書きの入口

### 書き出し

`WriteCookedTerrain(const CookedTerrainWriteDesc&, Container::VariableArray<uint8_t>& outBytes)`
を Tools と試験で共用する。`CookedTerrainWriteDesc` は全体の寸法・原点・高さの写像・LOD 数と、
レイヤの記述 `CookedTerrainLayerDesc`、タイルの記述 `CookedTerrainTileDesc` を持つ。
各タイルの LayerBytes はレイヤ表と同じ数・順の `Container::Span<const uint8_t>` で、データを所有しない。

writer は全入力を検査し、タイルを座標順に並べて表とデータを書き、最後に PayloadHash を埋める。
埋め込みレイヤは `ParseCookedTexture(AssetBlob::CopyBytes(span), false)` で解析し、
形式と色空間の一致も検査する。失敗時は outBytes を空にする。

戻り値 `CookedTerrainWriteStatus` は `Success`、`InvalidDimensions`、`InvalidLodCount`、
`InvalidLayerCount`、`InvalidLayer`、`DuplicateLayerFourCC`、`TileCountMismatch`、
`TileIndexOutOfRange`、`DuplicateTile`、`InvalidTileBounds`、`LayerBytesCountMismatch`、
`HeightSizeMismatch`、`EmbeddedTextureInvalid`、`IntegerOverflow`。
未使用 LOD 欄の非 0 も writer では InvalidTileBounds になる。

### 解析とレイヤの参照

`ParseCookedTerrain(AssetBlob sourceBlob)` は `CookedTerrainParseResult` を返す。
`Succeeded()` で成功を確認し、`Terrain` の `CookedTerrainData` を参照する。
SourceBlob が元のデータを保持し、Tiles はファイル内の順、
LayerRanges は `tileIndex * Layers.size() + layerIndex` の順で保持する。
TileIndexByCoord は `TileZ * TileCountX + TileX` から Tiles の番号を引く表。

| 補助関数 | 用途 |
| --- | --- |
| `GetSamplesPerSide()` | `TileCells + 1` |
| `FindTile(tileX, tileZ, outTileIndex)` | 座標からタイルを探し、任意でタイル番号も得る |
| `FindLayer(fourCC)` | レイヤ番号を探す。見つからなければ -1 |
| `GetLayerRange(tileIndex, layerIndex)` | 対応する DataOffset・DataSize・DataHash を参照する |
| `GetLayerBlob(tileIndex, layerIndex)` | SourceBlob.TryCreateSubBlob で部分 blob を得る。範囲外は AssetBlob::Invalid() |
| `GetHeightSample(tileIndex, sampleX, sampleZ)` | uint16 標本を得る。範囲外は 0 |
| `DecodeHeightM(value)` | ヘッダの min/max による写像だけを返す |

埋め込みレイヤを使う側は FindTile と FindLayer で番号を得て、
`GetLayerBlob(tileIndex, layerIndex)` の結果を `ParseCookedTexture(blob, false)` に渡す。
HGT0 は NVTEX ではないため GetHeightSample と DecodeHeightM で参照する。
