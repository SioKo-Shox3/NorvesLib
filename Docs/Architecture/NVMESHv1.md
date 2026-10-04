# NVMESH v1 メッシュ形式（LODの階層を持つクラスタの記録）

## 位置づけ

`.nvmesh` のクック済みメッシュに、**LOD の階層（クラスタの DAG）**を持たせる形式。v0 は1段のメッシュで、
`LODLevel`・`LODError`・`ParentStart`・`ParentCount` が 0 必須だった（`NVMESHv0.md`）。v1 はそれらを置き換え、
GPU のカリングが「自分の誤差で描けて、親の誤差では描けない」クラスタだけを選べるようにする。

- v0 と v1 は **magic で見分ける**（`NVMESHv0` / `NVMESHv1`）。`ParseCookedMesh` が両方を読む。
- v0 は従来の1段のメッシュとして返る（`FormatMajor=0`、`LODLevelCount=1`、グループ無し、全クラスタが根、
  フォールバックの範囲 = 全体のインデックス（`0, IndexCount`、誤差 0））。v0 の読み込みと検査は変えていない。
- v1 を書くのは クッカー（段4 の `VTG4-DAG-BAKE`）。この文書の時点では、`AssetCook` はまだ v0 だけを出す。
- ランタイムが v1 の階層を描くのは `VTG4-DAG-SELECT-GPU` 以降。それまでは、v1 を現行の読み込み経路
  （`BuildModelStagingFromCookedMesh`）へ通さない（全段のクラスタを1組として描いてしまうため）。
  v1 を扱う側は `CookedMeshData::FormatMajor` で分岐する。
- 実装: `Library/Core/Public/Asset/CookedMeshFormat.h`（定数・型・API）、`Private/Asset/CookedMeshLoader.cpp`
  （読み込みと書き出し）。MegaGeometry への受け渡しは `Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h`。

## 識別

| 項目 | 値 |
| --- | --- |
| 拡張子 | `.nvmesh` |
| magic | `NVMESHv1`（8 バイト、NUL 無し） |
| `VersionMajor` / `VersionMinor` | 1 / 0 |
| パッケージ FourCC | `Msh0`（v0 と同じ） |
| マニフェストの `format`（予定） | `nvmesh.v1.mesh3d.pnt.u32.lodgraph` |
| `cooked_version`（予定） | 1 |
| バイト順 | little-endian 固定。`EndianMarker = 0x01020304` |
| 浮動小数 | IEEE-754 binary32。NaN・Inf は拒否する |

v0 の旧エンジンは magic が違うので v1 を `BadMagic` で安全に拒否する。クック済みパッケージの無効化は
`NVMESHv0.md` の方針と同じ（形式の変更は `format`・`cooked_version` を上げて再クックする）。

## 構成

節の並び（先頭は 8 バイト境界。詰め物は 0。`PayloadHash` は最初の節から末尾までの FNV-1a64）:

```text
header -> submesh -> material -> cluster -> group -> string -> vertex -> index
```

v0 との違いは、(1) クラスタのレコードが 128B、(2) グループの表が増える、(3) ヘッダ末尾（v0 の予約領域）に
グループ・LOD の段数・フォールバックの範囲を持つ、の 3 点。頂点（32B）・サブメッシュ（64B）・材質（64B）・
文字列参照（16B）のレコードと、パスの規則（`NVMESHv0.md` の「String / Path Rules」）は v0 と同じ。

### ヘッダ（256B）

オフセット 0〜215 は v0 と同じ並び。差分だけ書く。

| オフセット | 型 | 項目 | 規則 |
| ---: | --- | --- | --- |
| 0 | `uint8[8]` | `Magic` | `NVMESHv1` |
| 12 / 14 | `uint16` | `VersionMajor` / `VersionMinor` | 1 / 0 |
| 32 | `uint32` | `ClusterRecordSize` | 128 |
| 160 | `uint32` | `SubmeshCount` | 1 |
| 164 | `uint32` | `MaterialCount` | 1 |
| 168 | `uint32` | `ClusterCount` | 1 以上 |
| 192 | `uint32` | `ClusterAlgorithmId` | 2（meshoptimizer のクラスタ + DAG） |
| 196 | `uint32` | `ClusterAlgorithmVersion` | 0 |
| 200 / 204 | `uint32` | `ClusterMaxTriangles` / `ClusterMaxVertices` | 128 / 128 |
| 208 | `uint32` | `ClusterSettingsFlags` | 0 |
| 212 | `uint32` | `Flags` | 0 |
| 216 | `uint32` | `GroupRecordSize` | 48 |
| 220 | `uint32` | `GroupCount` | `ClusterCount` 以下（0 可） |
| 224 / 232 | `uint64` | `GroupTableOffset` / `GroupTableSize` | `GroupCount * 48` |
| 240 | `uint32` | `LODLevelCount` | 1〜64 |
| 244 | `uint32` | `FallbackIndexOffset` | クラスタのインデックスの総数に等しい（3 の倍数） |
| 248 | `uint32` | `FallbackIndexCount` | 1 以上、3 の倍数。`Offset + Count == IndexCount` |
| 252 | `float32` | `FallbackError` | 有限・非負 |

`IndexCount` は、クラスタのインデックスとフォールバックのインデックスの合計。

### クラスタのレコード（128B）

| オフセット | 型 | 項目 | 規則 |
| ---: | --- | --- | --- |
| 0 | `float32[3]` | `SelfCenter` | 有限。自分の境界球の中心 |
| 12 | `float32` | `SelfRadius` | 有限・非負 |
| 16 | `float32` | `SelfError` | 有限・非負。自分の簡略化の誤差（ローカル空間の長さ）。段 0 は 0 |
| 20 | `uint32` | `GroupId` | このクラスタが属するグループ（親を作ったグループ）。根は `0xFFFFFFFF` |
| 24 | `float32[3]` | `ParentCenter` | 親のグループの境界球の中心。根は 0 |
| 36 | `float32` | `ParentRadius` | 根は 0 |
| 40 | `float32` | `ParentError` | 親のグループの誤差。根は `FLT_MAX`（無限大の代わり） |
| 44 | `uint32` | `LODLevel` | `LODLevelCount` 未満 |
| 48 | `float32[3]` | `ConeAxis` | 有限 |
| 60 | `float32` | `ConeCutoff` | 有限（-1 でカリング無効） |
| 64 | `uint32` | `IndexOffset` | 要素の位置（バイトではない）。クラスタの並びで 0 から隙間なく連続 |
| 68 | `uint32` | `IndexCount` | 3 の倍数、1〜384（128 三角形） |
| 72 | `uint32` | `VertexOffset` | 頂点の基点（インデックスに足す） |
| 76 | `uint32` | `VertexCount` | 1〜128。`VertexOffset + VertexCount <= 頂点数` |
| 80 | `uint32` | `MaterialIndex` | 0 |
| 84 | `uint32` | `PageId` | 0（ページのストリーミング（段5）まで） |
| 88 | `uint32` | `Flags` | bit0 = 根。他は 0 |
| 92 | `uint32` | `Reserved0` | 0 |
| 96 | `uint64[4]` | `Reserved1..4` | 0 |

クラスタのインデックスは、**そのクラスタの頂点の範囲の先頭からの相対**（`< VertexCount`）。描くときの頂点は
`VertexOffset + 相対`。

### グループのレコード（48B）

同じグループのメンバは、**同じ親の境界球と誤差を持ち、同じ判断で描く・描かないが決まる**（段の境目に割れ目が出ない）。
メンバは クラスタ表の連続した範囲に並ぶ。

| オフセット | 型 | 項目 | 規則 |
| ---: | --- | --- | --- |
| 0 | `float32[3]` | `BoundsCenter` | 有限 |
| 12 | `float32` | `BoundsRadius` | 有限・非負 |
| 16 | `float32` | `Error` | 有限・非負・`FLT_MAX` 未満 |
| 20 | `uint32` | `ClusterOffset` | メンバの先頭（クラスタ表の位置） |
| 24 | `uint32` | `ClusterCount` | 1 以上。`ClusterOffset + ClusterCount <= ClusterCount(全体)` |
| 28 | `uint32` | `LODLevel` | メンバの段。`LODLevelCount` 未満 |
| 32 | `uint32` | `Flags` | 0 |
| 36 | `uint32` | `Reserved0` | 0 |
| 40 | `uint64` | `Reserved1` | 0 |

## 階層の意味と選び方

- 段 0 が元の形（誤差 0）。段が上がるほど粗い。根のクラスタ（最も粗い段）は親を持たない。
- グループ `G` のメンバ（段 L）を結合して簡略化した結果が、段 L+1 の親のクラスタ。`G` の境界球と誤差は、
  メンバの `ParentCenter`/`ParentRadius`/`ParentError` として各メンバにも複製して持つ（GPU が表を引かずに判定できる）。
- 描くクラスタの条件（GPU）: **自分の誤差を画面へ投影した値がしきい値以下で、親の誤差を投影した値がしきい値を超える**。
  根は `ParentError = FLT_MAX` なので、自分が描ける限り常に描かれる。
- 誤差は階層をたどって単調に増える（親の誤差 ≥ 子の誤差）。親の境界球は子の境界球を包む。

## フォールバックの段

RT と影（VSM までのつなぎの CSM）は、階層を選ばずに **1 つのインデックスの範囲**で粗い段を描く。
その範囲を `FallbackIndexOffset`/`FallbackIndexCount` で持つ。

- クラスタのインデックスの**後ろに続く別の範囲**で、基点の頂点は 0（頂点全体への絶対のインデックス。`< 頂点数`）。
- 内容は、誤差のしきい値で切ったクラスタの三角形の集まり（クッカーが決める。目安は全体のおよそ 1/16 か 32K 三角形以下）。
  `FallbackError` はその段の誤差。
- 常に 1 つ以上の三角形を持つ（1 段だけの小さなメッシュも、段 0 の複製を持つ）。
- v0 を読んだときは、全体のインデックスの範囲（`0, IndexCount`）が粗い段として返る。

## 読み込みの検査

`ParseCookedMesh` は、状態（`CookedMeshParseStatus`）を返し、次を拒否する。v0 の検査（ヘッダ・版・endian・
レコードの大きさ・ファイルの大きさ・予約の非 0・節の範囲/整列/詰め物・hash・浮動小数・文字列/パス・
インデックスの範囲）は同じ。

- `InvalidCounts`: 件数と節の大きさの不一致、`GroupCount > ClusterCount`、`LODLevelCount` が 0 か 64 超、クラスタ 0。
- `UnsupportedV1Feature`: アルゴリズム・設定の値が違う、`MaterialIndex != 0`、`PageId != 0`。
- `InvalidFallbackRange`: 件数 0・3 の倍数でない・末尾まで届かない。
- `InvalidIndexRange`: サブメッシュの範囲がクラスタのインデックスの範囲と食い違う、インデックスが頂点数以上。
- `InvalidClusterRange`: インデックス数が 0・3 の倍数でない・128 三角形超、頂点数が 0 か 128 超、頂点の範囲が頂点数を超える、
  インデックスの並びに隙間・重なりがある、クラスタのインデックスが自分の頂点数以上。
- `InvalidLODGraph`: `LODLevel` が範囲外、根の 3 条件（根フラグ・グループ無し・親の誤差が最大値）の不一致、
  根の親の境界球が 0 でない、`GroupId` が範囲外、親の誤差 < 自分の誤差、段 0 の誤差が 0 でない、根が 1 つも無い、
  グループのメンバの `GroupId`・段・親の境界球・親の誤差が グループと不一致、メンバの境界球がグループの球に収まらない。
- `InvalidGroupTable`: メンバ数 0、範囲がクラスタ表を超える、段が範囲外、メンバの総数が「根以外のクラスタ数」と一致しない
  （一致すれば、根以外のクラスタはちょうど 1 つのグループに属する）。

## MegaGeometry への受け渡し

`Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h` の `BuildMegaMeshCreateInfoFromCookedMesh` が、
`CookedMeshData` から `MegaMeshCreateInfo` を作る（頂点・インデックスは `CookedMeshData` の配列を指すので、
`CreateMegaMesh` が終わるまで生かしておく）。

- v0: 従来どおり（`bBakedLODHierarchy=false`、フォールバック無し、`bBuildLODHierarchy=false`）。
- v1: `Clusters`（全段。`ParentBounds`・`ParentError`・`GroupId`・`PageId` つき）、`ClusterGroups`、
  `BakedLODLevelCount`、`FallbackIndexOffset`/`FallbackIndexCount`/`FallbackError` を渡し、`bBakedLODHierarchy=true`。
  `IndexData` はクラスタのインデックスにフォールバックのインデックスが続く並びのまま。

GPU のカリングで階層を選ぶ処理と、影・RT でのフォールバックの使用は、それぞれ後続の項目
（`VTG4-DAG-SELECT-GPU`・`VTG4-FALLBACK-LEVEL`）で行う。

## 書き出し

`SerializeCookedMeshV1(const CookedMeshV1WriteInput&, VariableArray<uint8_t>&)` が、表の組み立て・詰め物・hash を行う。
検査は行わないので、書いたあとに `ParseCookedMesh` で自己検証する（`MeshCooker` の v0 と同じ運用）。

## 試験

`CookedMeshTest`: v1 の書き出し → 読み込みの往復（フィールドの一致と、読んだ内容から組み直したバイト列の一致）、
グループ無しの 1 段の v1、v0 の読み込み（1 段のメッシュとして）、`MegaMeshCreateInfo` への変換、壊れた入力の拒否
（ヘッダ・hash・版・フォールバック・クラスタ・階層・グループの各条件を 1 つずつ壊す）。

```powershell
cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSmoke|CookedTextureTest)$"
```
