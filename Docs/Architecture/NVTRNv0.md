# NVTRN v0 地形形式

## 目的と範囲

NVTRN v0 は、地形の高さとタイルごとのレイヤを格納する cook 済み形式。
高さは `HGT0` の生の `uint16` 標本として、ほかのレイヤは NVTEX v0 のファイル全体として埋め込む。

cook の本体・実行時の地形・範囲読みは、この文書の範囲外。

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
