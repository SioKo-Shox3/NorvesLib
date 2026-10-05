# NVMESH v1 メッシュ形式（LODの階層を持つクラスタの記録）

## 位置づけ

`.nvmesh` のクック済みメッシュに、**LOD の階層（クラスタの DAG）**を持たせる形式。v0 は1段のメッシュで、
`LODLevel`・`LODError`・`ParentStart`・`ParentCount` が 0 必須だった（`NVMESHv0.md`）。v1 はそれらを置き換え、
GPU のカリングが「自分の誤差で描けて、親の誤差では描けない」クラスタだけを選べるようにする。

- v0 と v1 は **magic で見分ける**（`NVMESHv0` / `NVMESHv1`）。`ParseCookedMesh` が両方を読む。
- v0 は従来の1段のメッシュとして返る（`FormatMajor=0`、`LODLevelCount=1`、グループ無し、全クラスタが根、
  フォールバックの範囲 = 全体のインデックス（`0, IndexCount`、誤差 0））。v0 の読み込みと検査は変えていない。
- v1 を書くのは クッカー（`Tools/AssetCook`。手順は「焼き込み（クッカー）」）。マニフェストの `format` に
  `nvmesh.v1.mesh3d.pnt.u32.lodgraph` を指定したクックだけが v1 を出す。v0 の形式名（`nvmesh.v0.…`）のクックと
  既存のマニフェストは従来どおり v0 のまま。
- ランタイムが v1 の階層を描くのは `VTG4-DAG-SELECT-GPU` 以降。それまでは、v1 を現行の読み込み経路
  （`BuildModelStagingFromCookedMesh`）へ通さない（全段のクラスタを1組として描いてしまうため）。
  v1 を扱う側は `CookedMeshData::FormatMajor` で分岐する。
- v1.1（`VersionMinor=1`）は、クラスタのグループを 128 KiB のページに詰めた形式。ページの表と、常駐の根のページ群（粗い段と
  フォールバックの段。0 番から連続した 1 ページ以上で、どれも 128 KiB 以下）を持つ。クッカーはこれを書く。詳細は「v1.1」の節。
- 実装: `Library/Core/Public/Asset/CookedMeshFormat.h`（定数・型・API）、`Private/Asset/CookedMeshLoader.cpp`
  （読み込みと書き出し）。MegaGeometry への受け渡しは `Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h`。

## 識別

| 項目 | 値 |
| --- | --- |
| 拡張子 | `.nvmesh` |
| magic | `NVMESHv1`（8 バイト、NUL 無し） |
| `VersionMajor` / `VersionMinor` | 1 / 0 |
| パッケージ FourCC | `Msh0`（v0 と同じ） |
| マニフェストの `format` | `nvmesh.v1.mesh3d.pnt.u32.lodgraph` |
| `cooked_version` | 1 |
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
| 84 | `uint32` | `PageId` | v1.0 は 0 固定。v1.1 は入っているページの番号（「v1.1」の節） |
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
  根が最も粗い段（`LODLevelCount - 1`）以外にある・根でないクラスタが最も粗い段にある（親の段が無い）、
  いずれかの段にクラスタが 1 つも無い、根の親の境界球が 0 でない、`GroupId` が範囲外、親の誤差 < 自分の誤差、段 0 の誤差が 0 でない、根が 1 つも無い、
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

## 焼き込み（クッカー）

実装は `Tools/AssetCook/CookMeshDag.{h,cpp}` の `BakeMeshLodDag`（`AssetCookMeshOptimizer` の静的ライブラリに入る）。
`MeshCooker` の v1 の経路（`CookLodGraphMesh`）が、入力の変換・材質の参照の付与・`SerializeCookedMeshV1` での書き出し・
`ParseCookedMesh` での自己検証を受け持つ。簡略化とクラスタ化は meshoptimizer（v1.3）を使う。

1. **溶接**: 位置・法線・UV がビット列で一致する頂点を 1 つにし（`-0.0` は `0.0` にそろえる。NaN・Inf は拒否）、
   縮退した三角形を除く。三角形が 1 つも残らない入力と、外形の大きさが 0 の入力は失敗にする。
2. **段 0 のクラスタ化**: `meshopt_buildMeshlets` で 128 三角形・128 頂点以下のクラスタに分ける。誤差は 0、
   境界球は頂点を包む球。
3. **グループ化**: 約 4 クラスタずつ `meshopt_partitionClusters` でグループにする（隣り合うかの判定は、位置が同じ頂点を
   同じ頂点として数えるので、UV の継ぎ目で頂点が分かれていても隣とみなす）。段のクラスタが 5 つ以下なら全部を 1 グループにする。
4. **簡略化**: グループのメンバを結合し、**異なるグループの三角形が共有する辺の両端（位置が同じ頂点は全部）を固定**して、
   三角形を半分にする（`meshopt_simplifyWithAttributes`。絶対の誤差。属性は法線の重み 0.2・UV の重み 0.1、
   `meshopt_SimplifyErrorClamped`）。その結果を再びクラスタ化したものが次の段になる。
   - グループの誤差 = メンバの誤差の最大 + 簡略化の誤差（親の誤差が子より小さくならない）。
   - グループの境界球 = メンバの球を確実に包む球（半径は求め直す）。
   - 次の段のクラスタは、自分の境界球・誤差としてグループの値を持つ。メンバには、そのグループの値を親の値として複製する。
   - クラスタ表の並びは、段ごとに「グループの順にメンバを連続して置く」。グループの表の `ClusterOffset` はその位置。
5. **辺を非多様体にしない**: 簡略化の出力のうち、位置で頂点を同一視した辺の共有数が次を満たすものだけを候補にする。
   (a) グループの入力にある辺は入力の共有数以下、(b) 入力にない新しい辺は 2 以下で、段の入力全体の辺（隣のグループの辺）や、
   先に簡略化したグループが作った辺と重ならない。
   - 継ぎ目を保つ簡略化で三角形が目標の 1.25 倍以内に届かない（または候補が無い）グループは、**継ぎ目をまたぐ統合を許す
     許容モード**（`meshopt_SimplifyPermissive`）でやり直し、候補のうち三角形が少ない方を採る。
   - それでも候補が無ければ、目標を 3/4 にした継ぎ目を保つ簡略化を試す。
   - どれも安全でないグループは、**簡略化せずそのまま次の段へ残す**（次の段で隣と合わせて再挑戦する）。
6. **打ち切り**: 次のいずれかの段を根の段にし、その段のクラスタをすべて根にする。
   (a) クラスタが 1 つまで縮んだ、(b) 段が 64 に達した、(c) 次の段が空、または 1 段で三角形が 15% も減らない。
   したがって、根が 1 つとは限らない（(c) で止まったとき）。
7. **頂点とインデックス**: クラスタごとに自分の頂点の範囲（最大 128）を持ち、インデックスはその範囲の先頭からの相対で書く。
8. **フォールバックの段**: 切り口（自分の誤差 ≦ T < 親の誤差のクラスタ）の三角形数が `min(全体 / 16, 32768)` 以下になる
   最小の T を選び、その切り口の三角形を `FallbackIndexOffset`/`FallbackIndexCount` に入れる。`FallbackError` は
   切り口のクラスタの自分の誤差の最大。切り口が空ならクックは失敗にする。
   目標以下になる T の候補が 1 つも無いときは、候補のうち最大の T（最大の自己誤差）の切り口を採る。
   このため、フォールバックの三角形数が目標以下になることは保証されない。
   - 目標の下限は、クックの `fallback_min_triangles`（`AssetCook --fallback-min-triangles <N>`、`--kind model` と一緒に指定。
     0〜1000000 の整数、0 は指定なし。一覧では `Rendering3DTestStartupModels.json` の項目の `fallback_min_triangles`）で上げられる。
     目標は `max(min(全体 / 16, 32768), min(N, 131072))` になる。N の上限は 131072
     （`Tools/AssetCook/CookMeshDag.cpp` の `FallbackMinOverrideMaxTriangles`）で、これを超える N は 131072 として扱う。
   - 指定しなければ目標は従来どおり `min(全体 / 16, 32768)` で、`FallbackMaxTriangles`（32768）は変えていない。
     下限を上げるのは、石の盛り上がりや目地が数 cm の変位した大きな球のように、既定の粗さだと影・RT の形が実面から外れて
     自己遮蔽（近接の黒い斑点・目地の陰のずれ）を起こす資産だけ。起動画面では球が 131072 を指定し（フォールバックは
     130,946 三角形、誤差 3.3 mm）、岩は 32768、小屋は 4096 の指定を維持する（`Rendering3DTestStartupModels.json`）。
   - `AssetCookMeshSimplifySmoke --check-package <パッケージ> --fallback-min-triangles <N>` は、同じ下限までを目標内として検査する。

クックのログに `MESH_COOK dag_levels=<段数> clusters=<クラスタ数> ms=<所要時間> rejected_groups=<残したグループ数>` を出す。

実測（Debug）: 滑らかな UV の球は 9 段・356 クラスタ・根 1・簡略化を見送ったグループ 0。四角形ごとに UV の島が違う球
（約 2 万三角形）は 10 段・717 クラスタ・根 1・許容モード 172 グループ。岩（`boulder_01`、約 6.6 万三角形。溶接後の頂点は
位置の約 2 倍で、ほぼ全頂点が UV の継ぎ目）は 13 段・1451 クラスタ・345 グループ・根 1・見送り 13（約 1.4 秒）。

## 生成器（displaced-sphere）

glTF を読まずに、起動画面の大きな球をクッカーが作って v1 に焼く。`AssetCook --kind model --generate displaced-sphere
--input <高さマップ> --format nvmesh.v1.mesh3d.pnt.u32.lodgraph ...`。`--generate` は `--kind model` と一緒に指定し、
値は `displaced-sphere` だけ。形式は v1 だけで、v0 の形式名では失敗にする。一覧では `models` の項目に
`"generate": "displaced-sphere"` を書き、`source_path` を高さマップにする（`Scripts/CookAssets.ps1` が `--generate` を渡す）。

- 入力: `--input` は 2 の累乗の正方形の画像。起動画面で使うのは 16 ビットのグレーの PNG
  （石畳の `cobblestone_floor_09_disp_4k.png`）。クッカーは stb_image で 1 チャンネルの 16 ビットとして読むので、
  元画像のビット深度やチャンネル数は検査しない（8 ビットは 16 ビットへ広げられ、カラーはグレーへ変換される）。
  画像として読めない、正方形でない、2 の累乗でない、大きさが 8 画素（ミップ開始段 3）未満のときは失敗にする。
- 球の仕様は `Library/Core/Public/Rendering/MegaGeometry/StartupBigSphereSpec.h` に集め、実行時の生成（Game）とクッカーが
  同じ値を使う。半径 1 m、格子 1024×512（頂点の間隔は約 6.1 mm）、テクスチャの繰り返し 3×1.5、変位の深さ 0.03 m、
  高さマップのミップを作り始める段 3。クック済みのメッシュの論理パスは
  `Assets/Models/BigCobbleSphere/BigCobbleSphere.generated`。値を変えるときは両方の経路が揃うよう、このヘッダだけを変える。
- 作り方: `BuildProceduralMegaSphere` を `LODLevelCount=1`（実行時の生成が作る 5 段の LOD は使わず、最も細かい段だけ）で
  呼び、その頂点・インデックスを `BakeMeshLodDag` に渡して階層とフォールバックを焼く。
- 材質の参照は持たせない（Game が石畳の材質を当て、`DisplacementUVSpacing()` を仕様から求めて渡す）。
- 元のハッシュ（`SourceHash`）は、高さマップの大きさと中身、球の仕様の値（分割・ミップ開始段・半径・変位の深さ・
  テクスチャの繰り返し）から作る。仕様の値を変えると焼き直される。
- 起動画面の項目は `fallback_min_triangles: 131072` を付ける（理由は「フォールバックの段」）。
- 焼いた結果: 17 段・17984 クラスタ・グループ 4330・根 1・頂点 1,524,833・インデックス 6,694,752（フォールバック 392,838 を含む）、
  パッケージ 78,084,048 バイト、焼く時間は約 7.8 秒。`MESH_COOK dag_levels=17 clusters=17984 ms=7842 rejected_groups=8`。
  `AssetCookMeshSimplifySmoke --check-package` で、誤差の単調・親の球の包含・しきい値 7 通りの切り口が閉じていること
  （穴 0・つまみ 0。経度 0/360 の継ぎ目と極を含む）・フォールバックが閉じていることを確かめた。
- Game は既定でこのパッケージを読み（`BuildMegaMeshCreateInfoFromCookedMesh`）、無い・壊れているときは警告
  （`COOKED_BIG_SPHERE_MISSING` / `COOKED_BIG_SPHERE_INVALID`）を出して実行時の生成へ戻る。比較用に
  `--rendering3dtest-big-sphere-source=cooked|runtime`（撮影スクリプトは `-BigSphereSource`）で経路を選べる。

## v1.1（クラスタのグループをページに詰めた形式）

ページのストリーミング（段5）のための形式。`VersionMinor = 1`（magic は `NVMESHv1` のまま）。クッカーは
`nvmesh.v1.mesh3d.pnt.u32.lodgraph` を **v1.1 で書く**。`ParseCookedMesh` は v1.0・v1.1 の両方を読み、v1.1 も
v1.0 と同じ形の全体の配列（`Vertices`・`Indices`・`Clusters`・`Groups`）へ組み直して返すので、`CookedMeshData` の
利用側（`BuildMegaMeshCreateInfoFromCookedMesh` など）は変わらない。違いは `FormatMinor`・`Pages` と、クラスタの
`PageId`（入っているページの番号）が入ること、クラスタの並びがページの順になること。

### ファイルの並び

```text
header -> submesh -> material -> page table -> group -> string -> page region
```

クラスタ・頂点・インデックスの節は無く、ページの表とページの領域になる。ヘッダの項目の位置は v1.0 と同じで、次の
3 つだけ意味が変わる（`PagedHeaderOffset`）。

| オフセット | v1.0 | v1.1 |
| ---: | --- | --- |
| 80 / 88 | `ClusterTableOffset` / `Size` | ページの表。`Size` = ページ数 × 48（ページ数は 1 以上） |
| 112 / 120 | `VertexPayloadOffset` / `Size` | ページの領域（ページを隙間なく並べた範囲） |
| 128 / 136 | `IndexPayloadOffset` / `Size` | 大きさ 0（オフセットはファイルの末尾） |

`VertexCount`・`IndexCount`・`ClusterCount`・`FallbackIndexOffset`・`FallbackIndexCount` は全ページの合計で、v1.0 と同じ
意味（`IndexCount` = クラスタのインデックスの総数 + フォールバックのインデックス）。サブメッシュのレコードも v1.0 と同じ。
`PayloadHash` は最初の節から末尾までの FNV-1a64。

### ページの表のレコード（48B）

ページの順（0 番から）に並ぶ。

| オフセット | 型 | 項目 | 規則 |
| ---: | --- | --- | --- |
| 0 | `uint64` | `FileOffset` | ファイルの先頭からのバイト位置。ページは領域に隙間なく並ぶ（前のページの終わり） |
| 8 | `uint32` | `Size` | ページの大きさ。64 以上、**131072（128 KiB）以下**（根のページも同じ） |
| 12 | `uint32` | `ParentPageId` | 親のページ。根のページは `0xFFFFFFFF`、それ以外は下の規則で導いた番号（自分より小さい） |
| 16 | `uint32` | `Flags` | bit0 = 根のページ（常駐）。根のページは 0 番から連続した先頭の 1 ページ以上。他は 0 |
| 20 | `uint32` | `ClusterCount` | ページの中のクラスタ数と一致。1 以上（フォールバックだけを持つ根のページは 0 でもよい） |
| 24 | `uint32` | `FirstClusterIndex` | 全体のクラスタの並びの中の先頭。前のページの `FirstClusterIndex + ClusterCount` |
| 28 | `uint32` | `VertexCount` | ページの頂点数（根のページはフォールバックの専用の頂点を含む）。一致が必要 |
| 32 | `uint32` | `IndexCount` | ページのクラスタのインデックス数（フォールバックを含まない）。一致が必要 |
| 36 | `uint32` | `Reserved0` | 0 |
| 40 | `uint64` | `PageHash` | ページのバイト列（`FileOffset` から `Size`）の FNV-1a64 |

根でないページの `ParentPageId` は、そのページのクラスタの親（1 つ上の段で、自分の親の境界球・誤差を自分の値として持つ
クラスタ）を持つ、自分より前のページのうち最も小さい番号（見つからなければ 0 番）。ページを描くには先に親のページが
要る、というストリーマの依存になる。読み込みは同じ規則（`ComputePageParentPages`）で番号を導き、表の値と違えば
`InvalidPageTable` として拒否する。

### ページ（自己完結した範囲）

ページはファイルの範囲読みで 1 つずつ読め、ほかのページや表を見ずに検査・使用できる。先頭の 64B の後に、
クラスタの記録・頂点・クラスタのインデックス・フォールバックのインデックスが並び、末尾を 8 バイトに揃える（詰め物は 0）。

| オフセット | 型 | 項目 | 規則 |
| ---: | --- | --- | --- |
| 0 | `uint32` | `ClusterCount` | 1 以上（フォールバックだけを持つ根のページは 0 でもよい） |
| 4 | `uint32` | `VertexCount` | |
| 8 | `uint32` | `IndexCount` | クラスタのインデックスの数 |
| 12 | `uint32` | `FallbackIndexCount` | 根のページは 3 の倍数（0 でもよい。全部の根のページの合計は 1 以上）、それ以外は 0 |
| 16 | `uint32` | `ClusterOffset` | 64（ページの先頭からのバイト数） |
| 20 | `uint32` | `VertexOffset` | `ClusterOffset + ClusterCount * 128` |
| 24 | `uint32` | `IndexOffset` | `VertexOffset + VertexCount * 32` |
| 28 | `uint32` | `FallbackIndexOffset` | `IndexOffset + IndexCount * 4` |
| 32 | `uint32` | `PageId` | ページの番号 |
| 36 | `uint32` | `Flags` | 根のページは 1、それ以外は 0 |
| 40 | `uint64[3]` | `Reserved0..2` | 0 |

- クラスタの記録（128B）は v1.0 と同じ並びで、`IndexOffset`・`VertexOffset` は**ページの中**の要素の位置、
  `PageId` は自分のページの番号（v1.0 は 0 固定）。インデックスは、そのクラスタの頂点の範囲の先頭からの相対（v1.0 と同じ）。
- クラスタごとに自分の頂点の範囲とインデックスの範囲を、ページの中で並びの順に隙間なく持つ。
- 根のページのフォールバックのインデックスは、**そのページの頂点**への添字（0 〜 `VertexCount - 1`）。フォールバックが
  使う頂点はクラスタの頂点の後ろにページごとに専用に持つ（複数の根のページが使う頂点は、ページごとに重複する）。
  フォールバックの三角形は元の順のまま、根のページの順に連続して分かれる。**根のページ群だけで粗い形が閉じて描ける**
  （ほかのページを待たない）。ファイルを全体で読んだ結果では、フォールバックのインデックスは根のページの頂点の基点を足した、
  全体の頂点への添字になる。

### 詰め方（クッカー）

`SerializeCookedMeshV1Paged`（`CookedMeshLoader.cpp`）が、`BakeMeshLodDag` の出力（平らなクラスタの表）を詰める。

1. **根のページ（0 番から連続した常駐のページ）**: 最も粗い段（根のクラスタ）は必ず入る。その下の段は、足したクラスタの
   記録・頂点・インデックスの合計が目安（`RootClusterBudgetBytes`、既定 512 KiB）以内に収まる間だけ、段ごとまとめて入る
   （段の途中では切らない）。グループは、1 つのグループが 1 つのページに収まるように、順にページへ詰める。続けて、フォールバックの
   三角形を（その三角形が使う頂点つきで）最後の根のページの空きへ順に入れ、入りきらなければ、クラスタを持たない根のページを
   足して続ける。どの根のページも 128 KiB 以下で、全部が常駐する（常駐する大きさは根のページの合計）。
2. **ほかのページ**: 残りの段を、粗い段から順に、1 つの段のグループだけを 1 つのページへ詰める（次のページへ移るのは、
   段が変わるか、次のグループが入りきらないとき）。**1 つのグループは必ず 1 つのページに収まり、ページをまたがない。**
   1 ページの大きさは 128 KiB 以下（8 バイトに揃えた後）。
3. 1 つのグループ（根のクラスタを含む）が 1 ページに収まらないときは書き出しに失敗する（`GroupExceedsPage`。クックは、そのグループの
   番号とバイト数を載せたエラーで止まる）。
4. **クラスタの並び**はページの順（根のページ群 → 粗い段のページ → 細かい段のページ）に並べ替えて書く。グループの表は
   元の番号のまま、メンバの先頭（`ClusterOffset`）だけ新しい並びへ直す。グループのメンバは連続したまま。

クックのログに、`MESH_COOK` の次の行で詰め方を出す。
`MESH_COOK_PAGES pages=<ページ数> root_pages=<根のページ数> root_page_bytes=<根のページの大きさの合計> root_page_clusters=<根のページのクラスタ数の合計> root_min_level=<根のページの最も細かい段> max_page_bytes=<全ページの最大> max_group_bytes=<最大のグループのバイト数>`。

実測（起動画面の資産 6 点）: 岩 `boulder_01`（1451 クラスタ）は 75 ページ（根のページ 16・合計 1.96 MB・83 クラスタ）、
小屋は 8 ページ（根のページ 5・合計 0.58 MB）、大きな球（17984 クラスタ）は 668 ページ（根のページ 42・合計 5.4 MB・86 クラスタ。
フォールバック 130,946 三角形の専用の頂点を含む）、スキャン資産 3 点は 467〜665 ページ（根のページ 14〜15・合計 1.7〜1.9 MB）。
全ページの最大は 131,072 B 以下（根のページも）、最大のグループは 25.9〜28.4 KB で、ページの上限（128 KiB）の約 1/4 に収まる。

### 読み込みの検査（v1.1）

v1.0 の検査（ヘッダ・版・節・hash・浮動小数・文字列・クラスタの階層・グループの表・フォールバック）に加えて、
`ParseCookedMesh` が次を拒否する。`ParseCookedMeshPage` は、ページ 1 つ分の検査（下の `InvalidPageData` 以下）を単独で行う。

- `InvalidCounts`: ページの表の大きさが 0 か 48 の倍数でない、インデックスの節に大きさがある、ページの合計がヘッダの件数と合わない、
  ヘッダの頂点数・クラスタ数・インデックス数・ページ数がページの領域の大きさに収まらない（壊れた件数のまま配列を確保しない。
  確保の前に検査する）。
- `InvalidPageTable`: 根の印が 0 番に無い・根のページが先頭から連続しない、根のページに親がある・それ以外の親が自分以降、
  親の番号が、書き出しと同じ規則で導いた番号と違う、位置が前のページの終わりと合わない、ページがヘッダの 64B 未満、
  ページ（根のページも）が 128 KiB 超、根でないページのクラスタ数が 0・表と中身の件数が合わない、`FirstClusterIndex` が
  連続しない、ページの領域に余り、根のクラスタが根のページに無い。
- `PageHashMismatch`: 表の `PageHash` とページのバイト列が合わない。
- `InvalidPageData`: ページの先頭の番号・印・オフセットの並び・ページの大きさ（8 バイトに揃えた長さ）が合わない、
  クラスタの `PageId` が自分のページの番号でない。
- `InvalidClusterRange`・`InvalidIndexRange`・`InvalidFallbackRange`: ページの中のクラスタの頂点・インデックスの範囲、
  フォールバックのインデックスがページの頂点数以上、フォールバックが根以外のページにある・根のページを合わせても無い。
- `InvalidGroupTable`: 1 つのグループのメンバが複数のページにまたがる。

### 試験（v1.1）

`CookedMeshTest`: 4 段・15 クラスタ・7 グループと八面体のフォールバックから、小さなページの設定（上限 1100 B、根のページの
目安 800 B）で 4 ページ（根のページは 1）に詰め、(1) ページの表の往復（読んだ内容から組み直すとバイト列が一致）・位置・親・件数、
(2) 全クラスタが 1 回ずつ入り、頂点・三角形が元と同じで、グループがページをまたがない、(3) 根のページのバイト列だけを
`ParseCookedMeshPage` で読み、フォールバックのインデックスが根のページの頂点の範囲に収まり、閉じた形（向きつきの辺が
ちょうど 1 回、逆向きも 1 回）で元の八面体と同じ位置になる、(4) 全部が根に収まる 1 ページのメッシュ、(5) 書き出しの失敗
（グループが収まらない・範囲外の入力）、(6) 表・ページの壊れ方（位置・大きさ・根の印・親（実際の親のクラスタを持つページと
違う番号を含む）・件数・hash・ページの先頭・クラスタの記録・インデックス・巨大な件数）の拒否、(7) v1.0 が従来どおり読める、
(8) 根のページが複数に分かれる設定（上限 600 B、フォールバックを 4 つの八面体）で、根のページが先頭から連続し、クラスタを持たない
根のページがあり、根のページだけを 1 ページずつ読んで集めたフォールバックが元の位置の閉じた形になり、往復でバイト列が一致、
(9) 既定の上限（128 KiB）で 20,000 三角形のフォールバックを持つメッシュを書くと、根のページが複数に分かれ、全ページが 128 KiB 以下。`AssetCookMeshSmoke` は岩を v1.1 でクックして
`MESH_COOK_PAGES` と `--check-package` を確かめる。

## 既知の限界

- 属性（法線 0.2・UV 0.1）の重みは、継ぎ目だらけの資産で誤差が膨らまない値として選んだ（重みを上げると岩で誤差が外形の
  数倍になった）。段が切り替わる見た目で調整する余地がある（`CookMeshDag.cpp` の定数）。重みは無次元で、座標の単位に左右されない。
- 簡略化を見送ったグループ（`rejected_groups`）は、その段で三角形が減らない。多いと打ち切り（15% 未満）の原因になり、
  根が複数になる。起動画面の資産を v1 に切り替えるとき、この数と段の誤差の見え方を確かめる。
- 入力に元から非多様体の辺がある資産では、その辺は (a) の「入力の共有数以下」の範囲で残る。穴・非多様体の辺 0 の保証は、
  入力が閉じた多様体のメッシュのときの話（岩と試験の球で確かめた範囲）。
- 溶接は完全一致のみ（近い頂点を寄せない）。クラスタ化と簡略化は meshoptimizer の挙動に従うので、meshoptimizer の版を
  上げると階層が変わりうる。上げるときは階層の性質の試験を回し直し、`cooked_version` を上げて再クックする。
- 材質は 1 つ・サブメッシュは 1 つ。ページ（v1.1）は書き出しと読み込みまで。ページを要求から読み込んで追い出すストリーミングは
  `VTG5-PAGE-STREAMER`、カリングがページを要求する経路は `VTG5-PAGE-REQUEST` で、描画側は今は全ページを常駐のまま使う。
- 根のページは 1 つ 128 KiB 以下だが、フォールバックの専用の頂点（クラスタの頂点と重複し、複数の根のページが使う頂点はページごとに
  重複する）を持つので、常駐する根のページの合計は大きくなる（実測は上の「実測」）。フォールバックの頂点をクラスタの頂点と
  共有するかどうかは、常駐の予算を見てストリーマの項目で決める。

## 試験

`CookedMeshTest`: v1 の書き出し → 読み込みの往復（フィールドの一致と、読んだ内容から組み直したバイト列の一致）、
グループ無しの 1 段の v1、v0 の読み込み（1 段のメッシュとして）、`MegaMeshCreateInfo` への変換、壊れた入力の拒否
（ヘッダ・hash・版・フォールバック・クラスタ・階層・グループの各条件を 1 つずつ壊す）。

`AssetCookMeshSimplifySmoke`: 閉じた球（滑らかな UV）と、UV の島が違う継ぎ目だらけの球から階層を焼き、v1 に書いて
`ParseCookedMesh` で読み直して次を確かめる。
- 誤差が子から親へ単調（根以外は親の誤差 ≧ 自分の誤差、グループの誤差 ≧ メンバ、段 0 は 0）。
- 親の境界球が子の境界球とクラスタの頂点を包む。
- 誤差のしきい値を 5 通り（と段 0 だけ・根だけ）に変えて切った各メッシュで、すべての辺がちょうど 2 つの三角形に共有され、
  向きも釣り合う（穴 0・非多様体の辺 0）。
- フォールバックの三角形数が全体の 1/64 以上 1/16 以下で、穴が無い。
- 出力の頂点が入力の頂点の属性のどれかと一致する。
- 継ぎ目だらけの球で許容モードが使われること、座標を 1024 倍して焼いても段ごとの三角形数・許容モードのグループ数が
  変わらないこと、不正な入力（索引数が 3 の倍数でない・空）の拒否。

`AssetCookMeshSmoke`: 岩を v1 でクックして `MESH_COOK` のログと `cooked_version` 1 を確かめ、焼いたパッケージを
`AssetCookMeshSimplifySmoke --check-package` で読み直し、実資産にも同じ検査（穴・非多様体の辺 0）を掛ける。

```powershell
cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1
ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSimplifySmoke|AssetCookMeshSmoke|CookedTextureTest)$"
```
