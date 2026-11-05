# NVGFLD v0 草の場形式

## 目的と範囲

NVGFLD v0 は、草の場のチャンクごとの画素と種別名を格納する cook 済み形式。
定数・型・入口は [CookedGrassFieldFormat.h](../../Library/Core/Public/Asset/CookedGrassFieldFormat.h)、
読み込みは [CookedGrassFieldLoader.cpp](../../Library/Core/Private/Asset/CookedGrassFieldLoader.cpp)、
書き出しは [CookedGrassFieldWriter.cpp](../../Library/Core/Private/Asset/CookedGrassFieldWriter.cpp) にある。
名前空間は `NorvesLib::Core::Asset`、形式の定数は `CookedGrassFieldFormatV0` に置く。

アセットの種別は既存の `AssetKind::World`、パッケージの entry type は FourCC `Gfd0`、
manifest の format は `nvgfld.v0`。v0 は entry に収めた場の全体を読み込む。
大きな場は領域ごとの entry に分ける前提であり、entry の途中だけを読む形式ではない。

## 識別とファイルの並び

| 項目 | 値 |
| --- | --- |
| `Magic` | `{'N','V','G','F','L','D','v','0'}`（8 バイト） |
| `HeaderSize` / `Version` | 128 / 0 |
| `EndianMarker` | `0x01020304` |
| `ChunkRecordSize` / `SpeciesNameRecordSize` | 48 / 64 バイト |
| `MaxSpeciesNameLength` / `MaxSpeciesCount` | 63 バイト / 64 件 |
| `PayloadAlignment` | 16 バイト |

整数はリトルエンディアン、`float32` は IEEE-754 binary32。
C++ 構造体のメモリ配置をそのまま保存しない。位置は相対と記したもの以外はファイル先頭からのバイト位置。

```text
ヘッダ -> チャンク表 -> 種別名表 -> 0 埋めの隙間 -> ペイロード
```

表は隙間なく並び、種別名表の終端を 16 バイト境界に切り上げてペイロードを置く。
ペイロードの終端はファイルの終端と一致する。ハッシュはペイロードの全バイトに対する FNV-1a64
（offset basis `14695981039346656037`、prime `1099511628211`）。空の場合のハッシュは offset basis。

## ヘッダ（128 バイト）

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `char[8]` | `Magic` | `NVGFLDv0` |
| 8 | `uint32` | `HeaderSize` | 128 |
| 12 | `uint32` | `Version` | 0 |
| 16 | `uint32` | `EndianMarker` | `0x01020304` |
| 20 | `uint32` | `Reserved0` | 0 |
| 24 | `uint64` | `FileSize` | ファイル全体のバイト数 |
| 32 | `uint64` | `PayloadHash` | ペイロードの FNV-1a64 |
| 40 | `float32` | `ChunkSizeMeters` | チャンクの一辺 [m]。有限で正 |
| 44 | `uint32` | `LayerAResolution` | 一辺の画素数、1..1024 |
| 48 | `uint32` | `LayerBResolution` | 一辺の画素数、1..1024 |
| 52 | `uint32` | `ChunkCount` | チャンク件数。0 可 |
| 56 | `uint64` | `ChunkTableOffset` | 128 |
| 64 | `uint64` | `ChunkTableSize` | `ChunkCount * 48` |
| 72 | `uint32` | `SpeciesCount` | 種別数、1..64 |
| 76 | `uint32` | `Reserved1` | 0 |
| 80 | `uint64` | `SpeciesTableOffset` | チャンク表の直後 |
| 88 | `uint64` | `SpeciesTableSize` | `SpeciesCount * 64` |
| 96 | `uint64` | `PayloadOffset` | 種別名表の終端を 16 バイト境界へ切り上げた位置 |
| 104 | `uint64` | `PayloadSize` | ペイロードのバイト数 |
| 112 | `uint64` | `Reserved2` | 0 |
| 120 | `uint64` | `Reserved3` | 0 |

## チャンク表（1 件 48 バイト）

オフセットは各レコードの先頭からの相対位置。チャンク座標の組は重複不可。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `int32` | `ChunkX` | X 方向のチャンク座標 |
| 4 | `int32` | `ChunkZ` | Z 方向のチャンク座標 |
| 8 | `uint32` | `Flags` | v0 は 0 |
| 12 | `uint8` | `MaxDensity` | LayerA.R の最大値、1..255 |
| 13 | `uint8` | `MaxHeightFactor` | LayerA.G の最大値 |
| 14 | `uint16` | `Reserved` | 0 |
| 16 | `uint64` | `LayerAOffset` | ペイロード先頭からの位置、4 の倍数 |
| 24 | `uint64` | `LayerASize` | `LayerAResolution² * 4` |
| 32 | `uint64` | `LayerBOffset` | ペイロード先頭からの位置、4 の倍数 |
| 40 | `uint64` | `LayerBSize` | `LayerBResolution² * 4` |

## 種別名表（1 件 64 バイト）

| オフセット | 型 | 意味・規則 |
| ---: | --- | --- |
| 0 | `uint8` | UTF-8 名のバイト長、1..63 |
| 1 | `uint8[63]` | 名前のバイト列。長さより後のバイトは 0 |

名前の重複は不可で、バイト列と長さを比較する。表の添字が LayerA.A の種別 ID。
読み書きは名前のバイト長・詰め・重複を検査し、UTF-8 の符号列の妥当性は検査しない。

## ペイロード

各レイヤは RGBA8 画素の配列。行は +Z、行内は +X の順。

| レイヤ | チャンネル | 意味 |
| --- | --- | --- |
| LayerA | R / G | 密度 / 高さ係数 |
| LayerA | B / A | 湿りの偏り（128 が中立）/ 種別 ID（0..63、かつ `SpeciesCount` 未満） |
| LayerB | RGB / A | 色味（128 が 1.0）/ 予約（書き出し時は 0） |

書き出しでは `(ChunkZ, ChunkX)` の昇順でチャンク表を作り、同じ順で各チャンクの LayerA、
その直後に LayerB を隙間なく置く。LayerA の R と G の最大値をレコードに書き、R がすべて 0 の
チャンクは拒否する。読み込みはチャンク表の順を保持し、各レイヤの範囲を検査する。
読み込み時に表の座標順やレイヤ間の隙間・重なり、LayerB.A の値は追加で制限しない。

## 読み込みの拒否と検査の順

先に当たった理由を `CookedGrassFieldParseStatus` で返す。区分内の検査も下の順に行う。

1. 無効な blob は `InvalidBlob`、128 バイト未満は `TruncatedHeader`。
2. マジック、版、バイト順、ヘッダ長の不一致は順に `BadMagic`、`UnsupportedVersion`、`EndianMismatch`、`HeaderSizeMismatch`。
3. ファイル長の不一致は `FileSizeMismatch`。
4. ヘッダ予約欄が 0 以外なら `ReservedFieldNonZero`。
5. チャンク寸法・解像度・種別数が範囲外なら `InvalidFieldDesc`。
6. 表の位置・大きさ、ペイロード先頭の配置、表の範囲が不正なら `SectionOutOfRange`。表の後の隙間が 0 以外なら `ReservedFieldNonZero`。
7. ペイロードの範囲がファイル外なら `TruncatedPayload`、後ろに余りがあれば `FileSizeMismatch`。
8. ペイロードのハッシュ不一致は `PayloadHashMismatch`。
9. 種別名の長さまたは詰めが不正なら `InvalidSpeciesName`、名前が重複すれば `DuplicateSpeciesName`。
10. フラグ・予約欄・最大密度・レイヤの大きさ・位置の整列が不正なら `InvalidChunkRecord`。レイヤの範囲外または範囲終端の桁あふれは `ChunkRangeOutOfRange`、座標の重複は `DuplicateChunk`。
11. LayerA.R/G の最大値がレコードと異なれば `ChunkStatsMismatch`、LayerA.A の種別 ID が範囲外なら `InvalidSpeciesId`。

成功時の `CookedGrassFieldData::Source` が元のバイト列の寿命を保持する。
`GetLayerA` / `GetLayerB` は非所有ビューを返し、無効な添字や範囲なら空ビューを返す。
