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
    "wind": { "stiffness": 1.0, "phase_seed": 7 }
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
