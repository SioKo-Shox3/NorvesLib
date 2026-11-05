# NVSCT v0 散布形式

## 目的と範囲

NVSCT v0 は、地形上に個別に配置する物の種・チャンク・インスタンスを格納する cook 済み形式。
定数・型・入口は [CookedScatterFormat.h](../../Library/Core/Public/Asset/CookedScatterFormat.h)、
読み込みと量子化は [CookedScatterLoader.cpp](../../Library/Core/Private/Asset/CookedScatterLoader.cpp)、
書き出しは [CookedScatterWriter.cpp](../../Library/Core/Private/Asset/CookedScatterWriter.cpp) にある。
名前空間は `NorvesLib::Core::Asset`、形式の定数は `CookedScatterFormatV0`。
アセットの種別は `AssetKind::World`、パッケージの entry type は FourCC `Sct0`、manifest の format は `nvsct.v0.inst16`。

## 識別とファイルの並び

| 項目 | 値 |
| --- | --- |
| `Magic` | `{'N','V','S','C','T','v','0',0}`（8 バイト） |
| `HeaderSize` / `Version` | 192 / 0 |
| `EndianMarker` | `0x01020304` |
| `SpeciesRecordSize` / `ChunkRecordSize` | 96 / 48 バイト |
| `RangeRecordSize` / `InstanceRecordSize` | 16 / 16 バイト |
| `MaxSpeciesCount` / `MaxLodCount` | 64 / 4 |
| `InstanceAlignment` | 16 バイト |

整数はリトルエンディアン、浮動小数は IEEE-754 binary32 / binary64。レコードの各欄を明示的に書き出し、
C++ 構造体のメモリ配置をファイルへの変換に使わない。特記しない位置はファイル先頭からのバイト位置。

```text
ヘッダ -> 種表 -> チャンク表 -> 範囲表 -> 文字列表 -> 0 埋め -> インスタンス配列
```

表はこの順に隙間なく並び、文字列表の終端を 16 バイト境界に切り上げてインスタンス配列を置く。
0 埋めの各バイトは予約欄で、配列の終端はファイルの終端と一致する。
`PayloadHash` は名前にかかわらず、ファイル全体のうちオフセット 32..39 の 8 バイトを**除いた**バイト列の FNV-1a64
（offset basis `14695981039346656037`、prime `1099511628211`）。ハッシュ欄をゼロにして含める方式ではない。

## ヘッダ（192 バイト）

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint8[8]` | `Magic` | `NVSCTv0\0` |
| 8 | `uint32` | `HeaderSize` | 192 |
| 12 | `uint32` | `Version` | 0 |
| 16 | `uint32` | `EndianMarker` | `0x01020304` |
| 20 | `uint32` | `Reserved0` | 0 |
| 24 | `uint64` | `FileSize` | ファイル全体のバイト数 |
| 32 | `uint64` | `PayloadHash` | 上記のファイル全体の FNV-1a64 |
| 40 | `uint64` | `Seed` | 散布の種 |
| 48 | `uint64` | `TerrainCookedHash` | 元となる地形の cooked_hash |
| 56 | `float32` | `ChunkSizeMeters` | チャンクの一辺 [m]。有限で正 |
| 60 | `uint32` | `GridCountX` | X 方向の格子数。1 以上 |
| 64 | `uint32` | `GridCountZ` | Z 方向の格子数。1 以上 |
| 68 | `uint32` | `SpeciesCount` | 種の数。1..64 |
| 72 | `uint32` | `ChunkCount` | チャンクの数。0 可 |
| 76 | `uint32` | `Reserved1` | 0 |
| 80 | `float64` | `OriginX` | チャンク (0,0) の最小の角の X [m] |
| 88 | `float64` | `OriginZ` | チャンク (0,0) の最小の角の Z [m] |
| 96 / 104 | `uint64` × 2 | `SpeciesTableOffset` / `SpeciesTableSize` | 192 / `SpeciesCount * 96` |
| 112 / 120 | `uint64` × 2 | `ChunkTableOffset` / `ChunkTableSize` | 種表の直後 / `ChunkCount * 48` |
| 128 / 136 | `uint64` × 2 | `RangeTableOffset` / `RangeTableSize` | チャンク表の直後 / `ChunkCount * SpeciesCount * 16` |
| 144 / 152 | `uint64` × 2 | `StringTableOffset` / `StringTableSize` | 範囲表の直後 / 文字列のバイト数。最大 `UINT32_MAX` |
| 160 / 168 | `uint64` × 2 | `InstanceOffset` / `InstanceSize` | 文字列表の終端を 16 の倍数に切り上げた位置 / 配列のバイト数。大きさは 16 の倍数 |
| 176 / 184 | `uint64` × 2 | `Reserved2` / `Reserved3` | 0 |

原点と格子の外端 `OriginX + GridCountX * ChunkSizeMeters`、`OriginZ + GridCountZ * ChunkSizeMeters` は有限。

## 種表（1 件 96 バイト）

参照は `uint32` の文字列表先頭からの相対バイト位置と `uint32` のバイト長の組。
名前と使用中の LOD パスは空にできない。文字列は終端を含まない UTF-8 バイト列として格納し、
読み手は符号列の妥当性や名前・パスの重複を検査しない。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 / 4 | `uint32` × 2 | `Name` | 名前の位置 / 長さ |
| 8 | `uint32` | `RenderPath` | 0 = `Mega`、1 = `Foliage` |
| 12 | `uint32` | `LodCount` | 1..4 |
| 16 / 20 | `uint32` × 2 | `LodPaths[0]` | LOD0 メッシュの論理パスの位置 / 長さ |
| 24 / 28 | `uint32` × 2 | `LodPaths[1]` | LOD1 の位置 / 長さ |
| 32 / 36 | `uint32` × 2 | `LodPaths[2]` | LOD2 の位置 / 長さ |
| 40 / 44 | `uint32` × 2 | `LodPaths[3]` | LOD3 の位置 / 長さ |
| 48 | `float32` | `MeshRadiusMeters` | メッシュの境界半径 [m]。有限で非負 |
| 52 | `float32` | `MinSpacingMeters` | 最小間隔 r [m]。有限で正 |
| 56 / 60 | `float32` × 2 | `ScaleMin` / `ScaleMax` | 拡大率の範囲。有限で `0 < min <= max` |
| 64 | `float32` | `SlopeFollow` | 傾き追従率。有限で 0..1 |
| 68 | `float32` | `BrightnessVariation` | 明度の揺らぎ。有限で非負 |
| 72 | `float32` | `WindStiffness` | 風の硬さ。有限で非負 |
| 76 | `uint32` | `WindPhaseSeed` | 風の位相の種 |
| 80 | `uint32` | `Flags` | bit 0 は衝突の印として予約。bit 1..31 は 0 |
| 84 | `uint32` | `Reserved0` | 0 |
| 88 | `uint64` | `Reserved1` | 0 |

`LodCount` 以上の未使用のパス参照は位置・長さとも 0。書き手は文字列を各種の名前、LOD0 から
有効な LOD の順に隙間なく追加し、重複文字列も個別に書く。読み手は文字列参照の範囲を検査し、順序や共有は制限しない。

## チャンク表（1 件 48 バイト）

座標は `0 <= ChunkX < GridCountX`、`0 <= ChunkZ < GridCountZ`。座標の組は重複不可。
順番は入力順で、格子をすべて埋める必要はない。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 / 4 | `uint32` × 2 | `ChunkX` / `ChunkZ` | 格子上の番号 |
| 8 / 12 | `float32` × 2 | `MinY` / `MaxY` | 全種で共通のワールド Y 範囲 [m]。有限で `min <= max` |
| 16 | `float32[3]` | `AabbMin` | インスタンス位置のワールド座標での最小値 [m] |
| 28 | `float32[3]` | `AabbMax` | インスタンス位置のワールド座標での最大値 [m] |
| 40 | `uint64` | `Reserved` | 0 |

AABB はメッシュ半径を足さない。各値は有限で、各軸の最小値は最大値以下。
読み書きでは数値の範囲を検査し、AABB と位置の実測値との一致は検査しない。

## 種ごとの範囲表（1 件 16 バイト）

平らな配列の添字は `chunkIndex * SpeciesCount + speciesIndex`。インスタンス配列のレコード番号を使う。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint64` | `Offset` | インスタンス配列先頭からのレコード番号 |
| 8 | `uint64` | `Count` | レコード数。0 可 |

`Offset <= InstanceSize / 16`、`Count <= InstanceSize / 16 - Offset`。読み手は範囲同士の重なりや
配列に参照されないレコードの存在を制限しない。書き手は与えられた範囲・配列の順序を保持する。

## インスタンス配列（1 件 16 バイト）

| オフセット | 型 | 項目 | 意味 |
| ---: | --- | --- | --- |
| 0 / 2 / 4 | `uint16` × 3 | `PosX` / `PosZ` / `PosY` | X・Z はチャンク内、Y はチャンク共通の Y 範囲の量子化値 |
| 6 | `uint16` | `Yaw` | ワールド Y 軸回りの角度 |
| 8 | `uint8` | `Scale` | 種の拡大率の対数量子化値 |
| 9 | `uint8` | `Rank` | 間引きの順序に使う値 |
| 10 / 11 | `uint8` × 2 | `NormalOct` | 地面法線の八面体符号化 |
| 12 / 13 / 14 | `uint8` × 3 | `Tint` | RGB の 0..1 を 0..255 に量子化 |
| 15 | `uint8` | `Dry` | 枯れ度の 0..1 を 0..255 に量子化 |

量子化は範囲を 0..1 に写し、最も近い整数へ四捨五入する。チャンクの最小の角は
`x0 = OriginX + ChunkX * ChunkSizeMeters`、`z0 = OriginZ + ChunkZ * ChunkSizeMeters`。
復号は `x = x0 + PosX * ChunkSizeMeters / 65535`、`z = z0 + PosZ * ChunkSizeMeters / 65535`、
`y = MinY + PosY * (MaxY - MinY) / 65535`。`MinY == MaxY` のとき書き手は `PosY = 0` とし、復号値は `MinY`。
位置の丸め幅は X/Z が `ChunkSizeMeters / 65535`、Y が `(MaxY - MinY) / 65535`。

ヨーは `round(wrap(yaw, 0, 2π) * 65536 / (2π)) mod 65536`、復号は `Yaw * 2π / 65536`。
拡大率は `round(255 * (ln(scale) - ln(min)) / (ln(max) - ln(min)))`、復号は
`exp(ln(min) + (ln(max) - ln(min)) * Scale / 255)`。`min == max` のとき書き手は 0、復号値はその一定値。

法線は非ゼロの `(x,y,z)` を `abs(x)+abs(y)+abs(z)` で割り、`z < 0` なら
`(x,y) = ((1-abs(y))*signNotZero(x), (1-abs(x))*signNotZero(y))` に折り返す。
各成分を `round((v * 0.5 + 0.5) * 255)` に写す。復号時は `x = qx*2/255-1`、`y = qy*2/255-1`、
`z = 1-abs(x)-abs(y)` を求め、`z < 0` なら同じ折り返しを X/Y に適用してから正規化する。
`signNotZero(0) = +1`。色・枯れ度の復号は `q / 255`。

`EncodeScatterInstance` は範囲外や非有限の位置・拡大率・色・枯れ度、ゼロまたは非有限の法線を拒否する。
各軸について符号化後に復号した位置と入力との差が `ChunkSizeMeters / 65535` を超える場合も失敗する。
特に Y 範囲がチャンク寸法に比べて非常に広い場合は、16 ビットで必要な精度を表せない位置がある。
極端に大きい原点でも浮動小数点の精度により同様に失敗する場合がある。ヨーは任意の有限値を受けて巡回させる。
エンコードとデコードにはヘッダのチャンク寸法・原点、対象チャンク・種を渡し、座標は `double` で返す。

## 読み込みの拒否と検査の順

先に当たった理由を `CookedScatterParseStatus` で返す。

1. 無効な blob は `InvalidBlob`、192 バイト未満は `TruncatedHeader`。
2. マジック・版・バイト順・ヘッダ長の不一致は `BadMagic`・`UnsupportedVersion`・`BadEndian`・`BadHeaderSize`。
3. 実際のファイル長と宣言が違えば `FileSizeMismatch`、ハッシュ不一致は `HashMismatch`。
4. ヘッダ予約欄が 0 以外なら `ReservedNonZero`。チャンク寸法・格子・原点が不正なら `InvalidDimensions`。
5. 種数が範囲外なら `SpeciesOutOfRange`、チャンク数が格子を超えれば `ChunkOutOfRange`。
6. 表・文字列表・配列の位置、長さ、順序、整列、ファイル終端が不正なら `TableOutOfRange`。隙間が 0 以外なら `ReservedNonZero`。
7. 種の描画経路・LOD 数・数値が不正なら `SpeciesOutOfRange`、文字列参照が空または範囲外なら `StringOutOfRange`。
   未使用 LOD 参照・予約欄・未定義フラグが 0 以外なら `ReservedNonZero`。
8. チャンク座標が範囲外または重複なら `ChunkOutOfRange`、Y 範囲または AABB が不正なら `InvalidChunkBounds`、予約欄が 0 以外なら `ReservedNonZero`。
9. 範囲表の offset・count がインスタンス配列の外なら `InstanceRangeOutOfRange`。

成功時は `CookedScatterData::Source` が元のバイト列の寿命を保持する。
`GetInstances(chunkIndex, speciesIndex)` はその範囲の非所有バイトビューを返し、無効な添字・範囲では空を返す。
`ReadScatterInstance` で 16 バイトを構造体へ読み取る。blob の開始位置自体の整列を前提にしない。
書き手は同じ制約を検査し、失敗時は出力を空にする。

## 配置の規則
