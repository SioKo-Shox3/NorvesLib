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
| `CollisionRecordSize` / `VariationSeedRecordSize` | 48 / 4 バイト |
| `MaxCollisionShapesPerSpecies` | 16 |
| `MaxSpeciesCount` / `MaxLodCount` | 64 / 4 |
| `InstanceAlignment` | 16 バイト |

整数はリトルエンディアン、浮動小数は IEEE-754 binary32 / binary64。レコードの各欄を明示的に書き出し、
C++ 構造体のメモリ配置をファイルへの変換に使わない。特記しない位置はファイル先頭からのバイト位置。

```text
ヘッダ -> 種表 -> チャンク表 -> 範囲表 -> 衝突の形の表 -> 文字列表 -> 0 埋め -> インスタンス配列 -> 個体差の種の配列
```

表はこの順に隙間なく並び、文字列表の終端を 16 バイト境界に切り上げてインスタンス配列を置く。
0 埋めの各バイトは予約欄。個体差の種の配列はインスタンス配列の直後から始まり、その終端がファイルの終端となる。
衝突の形の表と個体差の種の配列は省略でき、省略時の長さは 0。
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
| 144 / 152 | `uint64` × 2 | `StringTableOffset` / `StringTableSize` | 範囲表の終端 + `CollisionTableSize` / 文字列のバイト数。最大 `UINT32_MAX` |
| 160 / 168 | `uint64` × 2 | `InstanceOffset` / `InstanceSize` | 文字列表の終端を 16 の倍数に切り上げた位置 / 配列のバイト数。大きさは 16 の倍数 |
| 176 | `uint64` | `CollisionTableSize` | 範囲表の直後にある衝突の形の表の節のバイト数。0、または `SpeciesCount * 8 + 48 * 形の数`（形は 1 つ以上） |
| 184 | `uint64` | `VariationSeedSize` | インスタンス配列の直後にある個体差の種の配列のバイト数。0 または `(InstanceSize / 16) * 4` |

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
| 80 | `uint32` | `Flags` | bit 0 = 衝突の印、bit 1 = TLAS の選択に入れない、bit 2 = 影を落とさない。bit 3..31 は 0 |
| 84 | `uint32` | `SkyOccluderBits` | bit 0..1 は形（0 = なし、1 = 楕円体、2 = 箱）、bit 2 は canopy、bit 8..15 は底の比、bit 16..23 は κ。その他のビットは 0 |
| 88 | `float32` | `SkyOccluderRadiusMeters` | 遮蔽物の半径 [m]。形がある場合は有限の `(0, 256]` |
| 92 | `float32` | `SkyOccluderHeightMeters` | 遮蔽物の高さ [m]。形がある場合は有限の `(0, 256]` |

`LodCount` 以上の未使用のパス参照は位置・長さとも 0。書き手は文字列を各種の名前、LOD0 から
有効な LOD の順に隙間なく追加し、重複文字列も個別に書く。読み手は文字列参照の範囲を検査し、順序や共有は制限しない。

底の比と κ はそれぞれ `round(value * 255)` で量子化し、読み手は `q / 255` に戻す。
底の比の量子化値は 0..242、κ は 0..255。形が 0 のとき 84..95 の 12 バイトはすべて 0 で、
遮蔽物なしとして読む。旧形式の予約欄が 0 のファイルも遮蔽物なしとして読める。
衝突の形の範囲は種の記録ではなく、衝突の形の表の節の頭に置く（後述）。
衝突の印と形の数は独立で、規則による cook は形がなくても bit 0 を立てられる。
bit 1・bit 2 の既定は 0 で、TLAS の選択に入り、影を落とす。

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

## 衝突の形の表（種ごとの範囲 1 件 8 バイト + 形 1 件 48 バイト）

節は、種ごとの範囲を種表と同じ順に `SpeciesCount` 件並べ、その直後に形のレコードを並べる。
形が 1 つも無いファイルは節ごと省き、`CollisionTableSize` を 0 にする（このとき全種の範囲は 0 として読む）。
新しい欄を使わない規則の散布は、この節も個体差の種の配列も持たない。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint32` | `CollisionFirst` | 形のレコードでの先頭番号 |
| 4 | `uint32` | `CollisionCount` | 形の数。0..16 |

`CollisionFirst + CollisionCount` は形の総数以下とし、`CollisionCount == 0` なら `CollisionFirst == 0`。

形のレコードは種のローカル座標で表し、個体のヨーと拡大に従って回転・拡大する。
`A`・`B`・`Radius` の値はすべて有限。種ごとの参照は重なってもよく、参照されない形も置ける。

| オフセット | 型 | 項目 | 意味・規則 |
| ---: | --- | --- | --- |
| 0 | `uint32` | `Type` | 0 = `Capsule`、1 = `Sphere`、2 = `Box` |
| 4 | `uint32` | `Reserved0` | 0 |
| 8 | `float32[3]` | `A` | カプセルの線分の一端、球または箱の中心 |
| 20 | `float32[3]` | `B` | カプセルの線分の他端、球では全成分 0、箱では各軸の半分の寸法 |
| 32 | `float32` | `Radius` | カプセル・球では正の半径、箱では 0 |
| 36 | `uint32` | `Reserved1` | 0 |
| 40 | `uint64` | `Reserved2` | 0 |

箱の `B` は全成分が正。箱の軸は種のローカル軸に沿う。

## インスタンス配列（1 件 16 バイト）

| オフセット | 型 | 項目 | 意味 |
| ---: | --- | --- | --- |
| 0 / 2 / 4 | `uint16` × 3 | `PosX` / `PosZ` / `PosY` | X・Z はチャンク内、Y はチャンク共通の Y 範囲の量子化値 |
| 6 | `uint16` | `Yaw` | ワールド Y 軸回りの角度 |
| 8 | `uint8` | `Scale` | 種の拡大率の対数量子化値 |
| 9 | `uint8` | `Rank` | 間引きの順序に使う値 |
| 10 / 11 | `uint8` × 2 | `NormalOct` | 地面法線の八面体符号化 |
| 12 / 13 / 14 | `uint8` × 3 | `Tint` | RGB の色の倍率の半分を 0..255 に量子化 |
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
`signNotZero(0) = +1`。`Tint` の各値 `q` は色の倍率の半分を表し、倍率は `2q / 255`。
規則による cook の `128` はほぼ 1 倍に相当する。描画先へ渡すときは倍率を 1 で切るため、1 より明るくはできない。
`ScatterSystem` は mega の描画入力の色味へ、各成分 `min(1, 2q/255)` を RGBA8 にして渡す（3 成分とも 1 以上なら白）。
`EncodeScatterInstance` と `DecodeScatterInstance` の `ScatterInstanceValue::Tint` は倍率の半分を表し、
復号値は `q / 255`。枯れ度の復号も `q / 255`。

`EncodeScatterInstance` は範囲外や非有限の位置・拡大率・色・枯れ度、ゼロまたは非有限の法線を拒否する。
各軸について符号化後に復号した位置と入力との差が `ChunkSizeMeters / 65535` を超える場合も失敗する。
特に Y 範囲がチャンク寸法に比べて非常に広い場合は、16 ビットで必要な精度を表せない位置がある。
極端に大きい原点でも浮動小数点の精度により同様に失敗する場合がある。ヨーは任意の有限値を受けて巡回させる。
エンコードとデコードにはヘッダのチャンク寸法・原点、対象チャンク・種を渡し、座標は `double` で返す。

## 個体差の種の配列（1 件 4 バイト）

配列があるときはインスタンスと同数の `uint32` リトルエンディアン値を、インスタンス配列と同じ番号順に格納する。
値 0 は既定の種を使う意味。配列がないとき `VariationSeedSize` は 0。

## 読み込みの拒否と検査の順

先に当たった理由を `CookedScatterParseStatus` で返す。

1. 無効な blob は `InvalidBlob`、192 バイト未満は `TruncatedHeader`。
2. マジック・版・バイト順・ヘッダ長の不一致は `BadMagic`・`UnsupportedVersion`・`BadEndian`・`BadHeaderSize`。
3. 実際のファイル長と宣言が違えば `FileSizeMismatch`、ハッシュ不一致は `HashMismatch`。
4. ヘッダ予約欄が 0 以外なら `ReservedNonZero`。チャンク寸法・格子・原点が不正なら `InvalidDimensions`。
5. 種数が範囲外なら `SpeciesOutOfRange`、チャンク数が格子を超えれば `ChunkOutOfRange`。
6. 衝突の形の表の長さ、表・文字列表・配列の位置、長さ、順序、整列、ファイル終端が不正なら `TableOutOfRange`。個体差の種の配列の長さもここで検査する。隙間が 0 以外なら `ReservedNonZero`。
7. 各種について、描画経路・LOD 数、名前と LOD の文字列参照、未定義フラグと予約欄、遮蔽物、数値、衝突の形の参照の順に検査する。
   描画経路・LOD 数・数値が不正なら `SpeciesOutOfRange`、文字列参照が空または範囲外なら `StringOutOfRange`。
   未使用 LOD 参照・予約欄・未定義フラグ・遮蔽物の未使用ビットが 0 以外、または形が 0 で 84..95 がすべて 0 でなければ `ReservedNonZero`。
   遮蔽物の形が範囲外、半径・高さが不正、底の比の量子化値が 242 を超える場合は `InvalidSkyOccluder`。
   衝突の形の参照が不正なら `CollisionOutOfRange`。
8. チャンク座標が範囲外または重複なら `ChunkOutOfRange`、Y 範囲または AABB が不正なら `InvalidChunkBounds`、予約欄が 0 以外なら `ReservedNonZero`。
9. 範囲表の offset・count がインスタンス配列の外なら `InstanceRangeOutOfRange`。
10. 各衝突の形について、予約欄が 0 以外なら `ReservedNonZero`、種類または数値が不正なら `CollisionOutOfRange`。

成功時は `CookedScatterData::Source` が元のバイト列の寿命を保持する。
`GetInstances(chunkIndex, speciesIndex)` はその範囲の非所有バイトビューを返し、無効な添字・範囲では空を返す。
`GetVariationSeeds(chunkIndex, speciesIndex)` は同じ範囲の個体差の種の非所有バイトビューを返し、
配列がない場合や無効な添字・範囲では空を返す。`GetCollisionShapes(speciesIndex)` は種が参照する形の非所有ビューを返し、
範囲外では空を返す。形のビューの寿命は `CookedScatterData` の配列に従う。
`ReadScatterInstance` と `ReadScatterVariationSeed` はそれぞれ 16 バイトと 4 バイトを読み取る。
blob の開始位置自体の整列を前提にしない。
書き手は同じ制約を検査し、衝突の形やその参照の不正は `InvalidCollision`、個体差の種の本数の不正は
`InvalidVariationSeeds` とする。失敗時は出力を空にする。

## 配置の規則


`ScatterPlacement.cpp` の配置は種ごとに独立しており、異なる種同士の間隔は制約しない。
最小間隔を `r` とする。`min_spacing_m` が 0 のときだけ
`r = 2 * mesh_radius_m * scale_range[1]` を使い、一辺 `a = r / √2` の全体セルを地形原点から切る。
`r` は有限で正である必要がある。地形タイルの一辺 `TileCells * CellSizeM` は
`chunk_size_m` の整数倍で、散布原点は地形の原点に一致する。

### セルのハッシュと乱数

演算は uint64 の剰余演算。種の鍵は名前の UTF-8 バイト列の FNV-1a64
（初期値 `0xcbf29ce484222325`、素数 `0x100000001b3`）で、種表の添字は使わない。
splitmix 系の混合 `Mix(v)` は順に
`v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9`、
`v = (v ^ (v >> 27)) * 0x94d049bb133111eb`、`v ^ (v >> 31)`。
セルの初期状態は次の順で畳む。

```text
state = Mix(seed + 0x9e3779b97f4a7c15)
state = Mix(state ^ SpeciesKey(name))
state = Mix(state ^ uint64(CellX))
state = Mix(state ^ uint64(CellZ))
```

`Next` は状態に `0x9e3779b97f4a7c15` を足して `Mix(state)` を返す。
呼ぶ順は X ジッタ、Z ジッタ、優先度、受理、ヨー、拡大、Rank、明度。
単位乱数は `(Next >> 11) * 2^-53` で `[0,1)`。
候補位置は `Origin + (Cell + jitter) * a`。

### 密度、除外と局所最大

地形外、標高の帯外、傾斜の帯外、道の値が閾値以上、湿りの帯外の候補を除外する。
帯の両端は含む。層の標本は地形のセル角を双一次補間した 0..1 の値。
密度は `ρ = base_density_per_m2 * densityFactor` とし、倍率は定数か指定した層のチャンネル。
`ρ <= 0` は受理しない。`A = πr²` として Matérn II の逆変換を用い、
`ρA < 1` なら `λ = -log1p(-ρA) / A`、受理確率 `p = min(1, λa²)`。
`ρA >= 1` なら逆変換が有限でないため `p = 1` とし、種の飽和フラグを立てる。
ジッタ格子での補正は近似であり、低占有率ほど指定密度に近づく。
受理乱数が `p` 未満の候補を近傍競合に進める。

各候補について同じ種のセル差 `[-2,2] × [-2,2]` の 5×5 を同じ関数で評価し、
XZ 距離が `r` 未満で自分より上位の受理候補があれば落とす。
優先度、CellX、CellZ の順で値が大きい方を上位にする全順序なので、優先度の衝突でも決まる。
競合相手が別の相手に落とされる場合も、自分を落とす判定は変わらない。
近傍はチャンクを越えて評価し、チャンク番号や焼く順は乱数に使わない。

チャンクが所有する位置は X/Z とも `[Origin + coord * size, Origin + (coord+1) * size)`。
地形の実寸と重なる範囲だけを走査する。`ScatterBaker.cpp` は全チャンクなら Z、X の順に焼き、
指定チャンクだけなら重複を除いて同じ順に並べる。
各チャンク・各種の中は Rank、SortKey、CellX、CellZ の昇順。
Rank は専用乱数の上位8ビット、SortKey は Rank を上位に置いた同じ64ビット乱数である。

### 地形の三角形と属性

高さと法線は LOD0 の三角形から取る。`UsesMainDiagonal` と同じく全体セルの
`(column + row)` が偶数なら主対角線を使い、セル内 `u >= v` で三角形を選ぶ。
奇数なら逆対角線を使い、`u + v <= 1` で選ぶ。
各頂点は `OriginY + DecodeHeightM(sample)` を float に丸めてから三角形の平面を補間し、
上向きの面法線を正規化する。高さの双一次補間は使わない。

ヨーは `[0,2π)`、拡大は `exp(lerp(ln(min), ln(max), u))`。
Tint は各成分共通で `round(clamp(128 * (1 + brightness_variation * (2u-1)), 0, 255))`。
`dry_from_mask` が真なら MSK0.g を 0..255 に丸め、偽なら Dry は 0。
地形法線は NormalOct に保存し、`slope_follow` は種表に保存する。
風の硬さ・位相の種、衝突の印も種表の属性として保存する。

## 散布 spec JSON

AssetCook の入口は `--kind world --format nvsct.v0.inst16 --entry-type Sct0`。
JSON は UTF-8（先頭 BOM 可）。全オブジェクトで未知の鍵・重複した鍵を拒否し、種名の重複も拒否する。
文字列は空でない有効な Unicode で、NUL を含められない。

```json
{
  "version": 0,
  "terrain": { "path": "terrain.json", "cooked_hash": "0123456789abcdef" },
  "chunk_size_m": 32,
  "seed": 12345,
  "species": [{
    "name": "rock_small", "render_path": "mega",
    "lods": ["Meshes/Rock_LOD0.nvmesh"],
    "mesh_radius_m": 0.6, "min_spacing_m": 0,
    "density": { "source": "constant", "value": 1.0 },
    "base_density_per_m2": 0.01,
    "height_band_m": [0, 200], "slope_band_deg": [0, 35],
    "exclude": { "road_threshold": 0.5, "wetness": [0.0, 0.8] },
    "scale_range": [0.7, 1.4], "slope_follow": 0.8,
    "brightness_variation": 0.1, "dry_from_mask": false, "collision": false,
    "wind": { "stiffness": 1.0, "phase_seed": 7 },
    "sky_occluder": { "shape": "ellipsoid", "class": "solid", "radius_m": 1.0, "height_m": 1.3 }
  }]
}
```

| 鍵 | 必須・既定値 | 値と意味 |
| --- | --- | --- |
| `version` | 必須 | 0 |
| `terrain` | 必須 | 地形への参照オブジェクト |
| `terrain.path` | 必須 | spec の親を基準とするパス。`.nvtrn` は直接読み、それ以外は地形 spec として同じプロセスで cook |
| `terrain.cooked_hash` | 必須 | 地形 manifest の cooked_hash。大文字・小文字を問わない16桁の16進文字列 |
| `chunk_size_m` | 必須 | 正の有限値。地形タイル寸法の約数 |
| `seed` | 必須 | 0..18446744073709551615 の10進整数。数値 token と10進文字列の双方を正確に読む |
| `species` | 必須 | 1..64件の種オブジェクト |
| `species[].name` | 必須 | 種名。乱数の種の鍵にも使う |
| `render_path` | 必須 | `mega` または `foliage` |
| `lods` | 必須 | 1..4件のメッシュ論理パス。LOD0 からの順。地形のファイルパスのようには解決しない |
| `mesh_radius_m` | 必須 | 有限で非負のメッシュ半径 |
| `min_spacing_m` | 0 | 有限で非負。0 のときだけ最大の拡大を含む直径から求める |
| `density` | 必須 | 下記の2種の形式 |
| `density.source` | 必須 | `constant` または `layer` |
| `density.value` | constant で必須 | 有限の 0..1。constant の鍵は source と value だけ |
| `density.layer` | layer で必須 | 4文字の層 FourCC（例: GRS0） |
| `density.channel` | layer で必須 | r/g/b/a。layer の鍵は source・layer・channel だけ。層とチャンネルの存在を焼く前に検査 |
| `base_density_per_m2` | 必須 | 有限で非負の基準密度 [本/m²] |
| `height_band_m` | 無効 | `[min,max]`、有限で min <= max。既定の内部値は [0,0] |
| `slope_band_deg` | 無効 | `[min,max]`、0..90。既定の内部値は [0,90] |
| `exclude` | 各除外は無効 | 道・湿りの設定オブジェクト。空のオブジェクトも可 |
| `exclude.road_threshold` | 道の除外は無効 | 0..1。指定時のみ MSK0.b >= 閾値を除外。既定の内部値は 0.5 |
| `exclude.wetness` | 湿りの帯は無効 | 0..1 の `[min,max]`。指定時のみ MSK0.r の帯外を除外。既定の内部値は [0,1] |
| `scale_range` | 必須 | `[min,max]`、有限で正、min <= max。float の正規化最小値以上 |
| `slope_follow` | 必須 | 有限の 0..1。0=鉛直、1=地面法線への追従 |
| `brightness_variation` | 必須 | 有限の 0..1 |
| `dry_from_mask` | false | 真なら MSK0.g から枯れ度を読む |
| `collision` | false | 衝突用に予約された印 |
| `wind` | 下位項目の既定 | 風の属性オブジェクト。空のオブジェクトも可 |
| `wind.stiffness` | 1.0 | 有限で非負 |
| `wind.phase_seed` | 0 | uint32 範囲の10進整数または10進文字列 |
| `sky_occluder` | 遮蔽物なし | 遮蔽物の属性オブジェクト |
| `sky_occluder.shape` | 必須 | `ellipsoid` または `box` |
| `sky_occluder.class` | 必須 | `solid` または `canopy` |
| `sky_occluder.radius_m` / `height_m` | 必須 | 有限の `(0, 256]` [m]。float32 で 0 に丸まる正の値は最小の正の値として保存 |
| `sky_occluder.base_ratio` | 0 | 有限の `0..0.95` |
| `sky_occluder.kappa` | solid では 1、canopy では必須 | 有限の `0..1` |

density の layer 形式は `{"source":"layer","layer":"GRS0","channel":"r"}`。
寸法・半径・間隔・拡大・基準密度・標高・風の硬さは有限の float の最大値以下とする。
必要な MSK0 や指定密度層が無ければ失敗する。

### 地形ハッシュの照合と source_hash

`ScatterSpec.cpp` は `ComputeAssetPackagePayloadHash(NVTRN全体のバイト列)` を呼ぶ。
これは `AssetCookOutput.cpp` の `BuildSingleSkeletalEntryPackage` が地形 manifest の
cooked_hash に返す関数・範囲と同じであり、NVTRN 内部の PayloadHash とは異なる。
記録と違えば散布を焼く前に失敗し、診断に記録と実際の16桁を両方出す。
成功時は照合した値を NVSCT ヘッダの TerrainCookedHash に格納する。

散布の source_hash は FNV-1a64 の初期値から、次の順で畳む。
各入力の前には長さを little endian の uint64 で混ぜ、続けて全バイトを混ぜる。

1. 散布 spec の元のバイト列（空白・BOM も含む）。
2. 地形の cooked_hash を little endian の8バイトにした列（長さは8）。
3. `.nvtrn` 参照ならその全バイト。地形 spec なら CookTerrainSpecFile の InputFiles 全件を参照順
   （先頭は地形 spec）に読み、その全バイト。

成功した CLI は標準出力に `SCATTER_COOKED chunks=... species=... instances=... bytes=... payload_hash=...`、
続けて種ごとに `SCATTER_SPECIES name=... instances=...` を出す。
payload_hash は NVSCT 内部ハッシュ。散布 manifest の cooked_hash は NVSCT 全体のパッケージ用ハッシュである。
飽和した種は標準エラーへ `警告:` で始まる診断を1行ずつ出す。

### grassland の散布 spec

`Assets/Scatter/grassland.scatter.json` は 128 m の地形タイルを一辺 32 m のチャンクに分け、
固定 seed `89021090` と地形の cooked_hash `1d620f39881e0053` を使う。密度・間隔・帯は企画未定の仮値。
岩は `MSK0.a`（岩）の密度と急斜面、倒木は緩斜面、低木と草以外の草本の株は湿りの帯を使い、
いずれも道を除外する。植物は `MSK0.g` を枯れ度に使う。草の密な配置は草の場が受け持つ。
現在の地形 spec は道の塗りを指定していないため `MSK0.b` は全域 0 で、道の除外は配置に影響しない。
岩の層がある範囲も小さく、岩はほかの種よりかなり少なく焼かれる。

岩の LOD0 は荒野の崖錐でも使う既存の論理パス
`Assets/Models/PolyHaven/boulder_01/boulder_01_4k.gltf`（元メッシュは 66,122 三角形）。
荒野の低木と株は実行時の手続きでメッシュを作るため、参照可能なメッシュ論理パスはない。
次はまだ cook されない仮のパスで、対応する資産を用意する必要がある。

- 倒木: `Assets/Models/Grassland/FallenLog/FallenLog.gltf`
- 低木: `Assets/Models/Grassland/Shrub/Shrub.gltf`
- 草本の株: `Assets/Models/Grassland/ForbClump/ForbClump.gltf`

## 配置の一覧 JSON

同じ AssetCook の入口で、最上位の `kind` が `"placement"` なら配置の一覧を読む。
`kind` が無ければ散布 spec を読む。UTF-8（先頭 BOM 可）で、すべてのオブジェクトの未知・重複した鍵を拒否する。
文字列は空でない有効な Unicode とし、NUL を含められない。

```json
{
  "version": 0,
  "kind": "placement",
  "terrain": { "path": "../TerrainSpecs/Grassland/GrasslandTerrain.json", "cooked_hash": "3455d60b54f23a5c" },
  "chunk_size_m": 32,
  "seed": 47001,
  "height_mode": "terrain",
  "species": [{
    "name": "conifer", "render_path": "mega",
    "lods": ["Assets/Models/PolyHaven/dead_quiver_trunk/dead_quiver_trunk_4k.gltf"],
    "mesh_radius_m": 3.0, "scale_range": [1.2, 2.0],
    "slope_follow": 0.0, "brightness_variation": 0.0,
    "cast_shadow": true, "ray_tracing": true,
    "collision": [{ "shape": "capsule", "a": [0, 0, 0], "b": [0, 6, 0], "radius": 0.35 }],
    "wind": { "stiffness": 1.0, "phase_seed": 0 }
  }],
  "instances": [
    { "species": "conifer", "position": [12.5, -0.2, -80.25], "yaw_deg": 35.0, "scale": 1.4, "tint": [0.95, 0.95, 0.95], "seed": 1234 }
  ]
}
```

最上位で使える鍵は `version`・`kind`・`terrain`・`chunk_size_m`・`seed`・`height_mode`・`species`・`instances`。
`version` は必須で 0、`kind` は必須で `"placement"`。
`terrain`（`path`・`cooked_hash` のみ）・`chunk_size_m`・`seed` は必須で、散布 spec と同じ意味・検査を使う。
地形の cooked_hash の照合、元の JSON バイト列・照合したハッシュ・地形入力からの source_hash の計算、
`SCATTER_COOKED`・`SCATTER_SPECIES` の書式も同じである。
`height_mode` は既定が `"absolute"`。`position.y` をワールド Y とし、法線は `(0,1,0)`。
`"terrain"` では Y は地形面からの高さで、規則配置と同じ LOD0 の三角形の高さを加え、その面法線を NormalOct に記録する。

### 種の表

`species` は必須で1〜64件。各種で使える鍵と既定値は次のとおり。

| 鍵 | 必須・既定値 | 値と意味 |
| --- | --- | --- |
| `name` | 必須 | 重複しない種名 |
| `render_path` | 必須 | `mega` または `foliage` |
| `lods` | 必須 | LOD0 から順に1〜4件のメッシュ論理パス |
| `mesh_radius_m` | 必須 | 有限で正のメッシュ半径 |
| `scale_range` | 必須 | 散布 spec と同じ有限で正の `[min,max]`。min <= max、float の正規化最小値以上 |
| `slope_follow` | 0 | 有限の0〜1。種表に保存 |
| `brightness_variation` | 0 | 有限の0〜1。種表に保存し、配置一覧の色味には乱数を加えない |
| `cast_shadow` | true | false なら `ScatterSpeciesFlagNoCastShadow` |
| `ray_tracing` | true | false なら `ScatterSpeciesFlagNoRayTracing` |
| `collision` | `[]` | 0〜16件の衝突の形。空でなければ `ScatterSpeciesFlagCollision` |
| `wind` | 下位項目の既定 | 散布 spec と同じ。`stiffness` は既定1、有限で非負。`phase_seed` は既定0、uint32 の10進整数または10進文字列 |
| `sky_occluder` | 任意。省略時は遮蔽物なし | 鍵と範囲は散布 spec JSON の `sky_occluder` と同じ |

種表の `MinSpacingMeters` は `2 * mesh_radius_m * scale_range[1]`。有限で正の float で表せる値が必要で、間隔による配置の除外には使わない。
寸法・属性は有限の float で表せる範囲とする。

衝突の形は次の3形式で、形ごとの鍵は示したものだけを許す。座標は種のローカル座標。

```json
{ "shape": "capsule", "a": [0, 0, 0], "b": [0, 6, 0], "radius": 0.35 }
{ "shape": "sphere", "center": [0, 1, 0], "radius": 1 }
{ "shape": "box", "center": [0, 1, 0], "half_extents": [1, 1, 1] }
```

値はすべて有限、半径と半分の寸法は正。カプセルの `a`・`b` は A・B、球と箱の `center` は A に写す。
球の B は0、箱の `half_extents` は B、箱の Radius は0。形の配列を種の順に連結し、種の `CollisionFirst`・`CollisionCount` で参照する。

### 個体とチャンク

`instances` は必須で1〜4,000,000件。各個体で使える鍵は次のとおり。

| 鍵 | 必須・既定値 | 値と意味 |
| --- | --- | --- |
| `species` | 必須 | 種表にある名前 |
| `position` | 必須 | 有限の `[x,y,z]`。Y の意味は height_mode に従う |
| `yaw_deg` | 0 | 有限の角度。度で巡回してからラジアンに変換 |
| `scale` | 1 | 有限で、参照する種の scale_range 内 |
| `tint` | `[1,1,1]` | 色の倍率。各成分が有限の0〜2。EncodeScatterInstance へ tint / 2 を渡し、`q = round(tint * 127.5)` |
| `seed` | 未指定 | 1〜4294967295の整数 token。0・小数・指数・文字列は拒否 |

地形原点を格子原点とし、格子数は規則配置と同じ
`ceil(CellCount * CellSizeM / chunk_size_m)`。個体のチャンクは X/Z とも
`floor((position - Origin) / chunk_size_m)` で求め、格子外は拒否する。
個体が入るチャンクだけを Z、X の順で書く。MinY・MaxY と AABB は全種の個体位置を囲む値とし、float の丸めでも範囲を縮めない。

各チャンク・各種の中は Rank 昇順、同じ Rank は入力順。入力の番号は0から始め、Rank は
`Mix(Mix(seed + 0x9e3779b97f4a7c15) ^ SpeciesKey(name) ^ 入力の番号)` の上位8ビット。
Mix と種名の鍵は規則配置と共通。Dry は0。量子化と個体の検査には `EncodeScatterInstance` を使う。
どれかの個体に seed があれば、出力する個体と同じ順に全個体ぶんの VariationSeeds を書き、未指定の個体は0とする。
どの個体にも seed が無ければ配列を書かない。

### エラーの種別

診断は `配置の一覧 <name>: <日本語の説明>（個体の番号や種の名前）` の形で、文脈がある場合は末尾に添える。
個体の番号は0から始まる。

| name | 意味 |
| --- | --- |
| `json` | 構文、未知・重複した鍵、型の違い |
| `invalid_species` | 種表の値・件数の不正、種名の重複 |
| `unknown_species` | 個体の種名が種表に無い |
| `non_finite` | 個体の位置・ヨー・拡大・色味を有限値で表せない。`1e400` も含む |
| `scale_out_of_range` / `tint_out_of_range` | 個体の拡大または色味が範囲外 |
| `invalid_seed` | seed が整数・範囲の条件を満たさない |
| `invalid_collision` | 衝突の形・寸法・件数の不正 |
| `invalid_chunk_size` | チャンク寸法が非有限・非正・地形タイル寸法の約数でない、または格子を表せない |
| `out_of_grid` | 個体が格子外、または地形位置を標本化できない |
| `terrain_mismatch` | 地形 cooked_hash が記録と実際で異なる |
| `quantization` | NVSCT の個体の量子化または表の書き出しに失敗 |
| `too_many_instances` | 個体数が1〜4,000,000件の範囲外 |
