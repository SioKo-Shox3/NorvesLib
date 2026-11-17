# 毛のマップの規約

## 目的と全体の流れ

毛の長さ・流れ・密度と束・領域を UV テクスチャに焼き、AssetCook でエンジンへ渡す。入口は `Scripts/Blender/fur_flow.py`、Blender に依存しない焼き込みは `Scripts/Blender/fur_maps_core.py`、UV 空間の線画は `Scripts/Blender/fur_preview.py` に置く。コアと線画には numpy が必要。

土台メッシュ → `fur_flow.py`（Blender で頂点・骨・UV・塗りを抽出）→ `fur_mesh.npz` の保存とコアへの受け渡し → 4 枚の PNG と `fur_maps.json` → `fur_preview.py` の線画 → AssetCook による cook、の順で扱う。NPZ を保存すれば Blender を使わずに設定や補正マスクを変えて焼き直せる。生成物はリポジトリ管理外の出力ディレクトリに置く。

頂点カラーを使う場合も Blender 内で同じ 4 枚へ焼く。エンジンの頂点形式（`Mesh3DVertex` 32B、`SkinnedMeshVertex` 64B）は変えない。個体を共通の土台へ張り直して UV0 を共有すれば、手描きの補正マスクを AI の再生成後にも使える。

## マップと UV の規約

表中の `r`・`rg` はバイトを 255 で割った値。連続量は `floor(clamp(x, 0, 1) * 255 + 0.5)` で量子化する。すべて 8bit・正方形・パレットなし・インターレースなしの PNG であり、色変換を伴わない linear の cook 形式を使う。

| ファイル | PNG の形 | cook の形式 | 意味と復号 | 膨張後も埋まらない texel（補正前） |
|---|---|---|---|---|
| `fur_length.png` | グレー 1 チャネル | `nvtex.v0.r8.linear` | 長さ係数 `r`。実長は `r * MaxLengthMeters`。0 は毛なし | 0 |
| `fur_flow.png` | RGB、B = 0 | `nvtex.v0.rg8.linear` | UV 空間の流れ `v = 2 * rg - 1`。+X は U 増加、+Y は V 増加 | `(128,128,0)`。復号値は量子化により `(1/255,1/255)` |
| `fur_density_clump.png` | RGB、B = 0 | `nvtex.v0.rg8.linear` | R = 密度、G = 束。いずれも 0〜1 | `(0,0,0)` |
| `fur_region.png` | グレー 1 チャネル | `nvtex.v0.r8.linear` | 正規化前のバイトが領域 ID（0〜7）。`texelFetch` で読み、UNORM として得た値なら `round(r * 255)` で ID を戻す | `default_region` の ID（既定 0） |

コアは非零の流れを膨張の最後に単位化してから保存する。エンジンは復号した流れを正規化せず、長さを整い度として扱う。ミップの平均で異なる方向が打ち消し合うと長さが縮む。ゼロ方向は 8bit UNORM で厳密には表せない。補正マスクは画像全体へ適用するため、表の未充填値も補正で変わり得る。

UV は glTF の V 下向き（上端が 0）で扱う。Blender の UV は抽出時に `v = 1 - v` とする。解像度を N とすると texel 中心は `((x + 0.5) / N, (y + 0.5) / N)`。PNG の行は v の増加方向そのままに保存し、追加の上下反転をしない。UV の 0〜1 の範囲だけを焼き、範囲外の繰り返しは行わない。`uv_set` は記録上の名前であり、Blender の実際の UV 層は `uv_map` で選ぶ。

AssetCook は PNG を RGBA に展開してから先頭のチャネルを取る。R8 用はグレー、RG8 用は RGB（B = 0）で渡し、2 チャネル PNG は使わない。

## 焼き方

1. 骨名を規則へ順に `re.fullmatch` し、最初に一致したものを使う。`mode = bone` は head → tail の単位方向 × `sign`、`vector` は指定ベクトルの単位方向 × `sign`、`none` と未一致はゼロ方向。頂点ごとの 3D 方向は `d = Σ w_i * dir_i` とする。`sources.flow = auto` で `fur_flow` カラーがあれば、骨より優先して `d = 2 * RGB - 1` を使う。
2. 単位法線 n の接平面へ `v = d - dot(d,n) * n` と射影する。長さが `tangent_epsilon` 未満なら無効とし、有効な方向は単位化する。無効頂点は三角形の辺で隣接する有効頂点の平均を自身の接平面へ再射影して埋める。同じ反復の更新を次の反復まで使わず、最大 `fill_iterations` 回。埋められない方向はゼロのまま。
3. 各三角形で UV ヤコビアンの列 `T = ∂p/∂u`、`B = ∂p/∂v` を求める。計量 `G = [[dot(T,T), dot(T,B)], [dot(T,B), dot(B,B)]]` により、角の接方向 v の UV 成分を `q = G⁻¹ * [dot(v,T), dot(v,B)]` として求め、非零の q を単位化する。T・B が直交しているとの仮定を置かないため、UV の回転・鏡映・歪みがあっても、`T*q.x + B*q.y` は三角形の接平面上で元の 3D 方向に対応する。鏡映の符号もヤコビアンに含まれる。UV の行列式の絶対値が `1e-12` 以下、計量の行列式が `1e-12` 以下、または計算結果が非有限の三角形は焼かない。
4. texel 中心の重心座標で、角の UV 方向と長さ・密度・束を補間する。領域は頂点で領域別の骨ウェイト和が最大の ID を選ぶ（同点なら設定の領域配列順、無ウェイトなら `default_region`）。texel では重心係数が最大の角の ID を取り、ID の数値を補間しない（同点なら角の順）。重なる UV は後の三角形が勝つ。
5. 既定 16px の膨張を行う。各反復の開始時点で埋まっている 8 近傍から連続量を平均し、空の texel を埋める。ID は平均せず、上・下・左・右・左上・右上・左下・右下の順で最初の有効近傍を取る。画像端をまたぐ繰り返しはしない。残った連続量はゼロ、ID は既定領域を保つ。流れを単位化した後、補正、量子化、PNG 保存を行う。

## 設定 JSON

JSON の未知の鍵、重複鍵、不正な型や範囲は拒否する。省略した欄は既定を使う。`sources` と `paint_overlays` は鍵ごとに既定へ重ね、それ以外の欄（`bones`・`regions` を含む）は欄全体を置き換える。

| コアの欄 | 型・範囲 | 既定 |
|---|---|---|
| `version` | 整数、1 のみ | `1` |
| `resolution` | 整数、16〜8192。幅と高さ共通 | `1024` |
| `uv_set` | 空でない文字列。情報に記録する UV 名 | `UV0` |
| `dilation_px` | 整数、0〜256 | `16` |
| `tangent_epsilon` | 有限の正数 | `0.0001` |
| `fill_iterations` | 整数、0〜1000 | `32` |
| `default_region` | `regions` に存在する名前 | `body` |
| `sources.flow` | `auto`・`bones`・`color` | `auto` |
| `sources.length`・`sources.density`・`sources.clump` | `auto`・`group`・`color`・`regions` | 各 `auto` |
| `bones` | 骨規則の配列。空も可 | 下表 |
| `regions` | 1〜8 個の領域の配列 | 下表 |
| `paint_overlays` | 対象 `length`・`density`・`clump`・`flow` から PNG パス文字列への辞書 | `{}` |

| 骨規則の欄 | 型・範囲 | 省略時 |
|---|---|---|
| `pattern` | 正規表現の文字列。骨名全体へ一致させる | 必須 |
| `mode` | `bone`・`vector`・`none` | 必須 |
| `sign` | 整数、-1 または 1 | `1` |
| `vector` | 有限数 3 個、長さは非零。同じ座標空間の方向 | `mode = vector` なら必須。ほかの mode でも指定すれば検査する |
| `region` | `regions` に存在する名前 | `default_region` |

| 領域の欄 | 型・範囲 | 省略時 |
|---|---|---|
| `name` | 空でない一意な文字列 | 必須 |
| `id` | 一意な整数、0〜7 | 必須 |
| `length`・`density`・`clump` | 有限数、0〜1 | すべて必須 |

| Blender 側の欄 | 型・意味 | 省略時 |
|---|---|---|
| `input` | 空でないパス文字列。`.blend`・`.glb`・`.gltf` | 必須 |
| `output_dir` | 空でない出力パス文字列 | 必須（コア CLI では `--out` があれば不要） |
| `mesh_object` | 空でないメッシュオブジェクト名 | アーマチュア modifier またはアーマチュアの親を持つメッシュが唯一なら選ぶ。複数・不在はエラー |
| `armature_object` | 空でないアーマチュアオブジェクト名 | 選んだメッシュの最初の有効なアーマチュア modifier、なければアーマチュアの親 |
| `uv_map` | 空でない UV 層名 | `active_render` の層、なければ最初の層。UV 層なしはエラー |

設定ファイル内の `input`・`output_dir`・補正パスは設定ファイルのディレクトリ基準。Blender へ JSON オブジェクト文字列を渡した場合は作業ディレクトリ基準。CLI の `--mesh`・`--out`・`--paint-overlay` は作業ディレクトリ基準。コア CLI も Blender 側の欄を受け付けるが、抽出処理はせず、`output_dir` 以外はメッシュの選択に使わない。

### 既定の骨と領域

骨は骨盤から外へ伸び、背骨・首・頭・顎は頭へ、四肢は肉球へ、尾は先へ向く前提。毛は胴・首が頭から尾へ、顔が鼻から後頭へ、四肢・尾が根元から先へ流れる。異なる骨の向きでは符号や規則を変える。

| 順 | `pattern` | `mode` | `sign` | `region` |
|---|---|---|---|---|
| 1 | `root` | `none` | `1` | `body`（既定領域） |
| 2 | `head` | `bone` | `-1` | `face` |
| 3 | `jaw` | `bone` | `-1` | `face` |
| 4 | `neck_\d{2}` | `bone` | `-1` | `body` |
| 5 | `pelvis` または `spine_\d{2}`（正規表現の選択） | `bone` | `-1` | `body` |
| 6 | `tail_\d{2}` | `bone` | `1` | `tail` |
| 7 | `(front\|hind)_[LR]_paw` | `bone` | `1` | `paws` |
| 8 | `(front\|hind)_[LR]_[a-z]+` | `bone` | `1` | `legs` |

表の括弧内の `|` は正規表現の選択を表す。JSON 文字列に書く `\d` のバックスラッシュは `\\d` とエスケープする。`root` の省略された region は `default_region` に従う。未一致の骨も方向はゼロ、領域は `default_region` となる。`none` の骨のウェイトも領域判定と領域由来の量には含む。

| `name` | `id` | `length` | `density` | `clump` |
|---|---|---|---|---|
| `body` | 0 | 1.0 | 1.0 | 0.5 |
| `face` | 1 | 0.35 | 1.0 | 0.3 |
| `legs` | 2 | 0.6 | 1.0 | 0.4 |
| `paws` | 3 | 0.25 | 0.8 | 0.3 |
| `tail` | 4 | 1.0 | 1.0 | 0.6 |

### 頂点の量とカラー

`auto` での長さ・密度・束は、次の順で存在する入力を選ぶ。頂点ごとの未所属は、そのグループの 0 として扱い、次候補へは移らない。出力は 0〜1 に収める。入力を `group`・`color` に固定して必要なものがなければエラー。

| 量 | 第 1 候補：頂点グループ | 第 2 候補：頂点カラー | 第 3 候補 |
|---|---|---|---|
| 長さ | `fur_length` | `fur_length.R` | 領域の `length` |
| 密度 | `fur_density` | `fur_density.R` | 領域の `density` |
| 束 | `fur_clump` | `fur_density.G` | 領域の `clump` |

領域の量は、各骨に割り当てられた領域値をウェイトの合計で割って平均する。無ウェイトなら既定領域の値。領域 ID の最大選択とは異なり、境界の連続量は混ざる。流れの `auto` は `fur_flow` カラー → 骨の順で選ぶ。

Blender のカラー名は `fur_length`・`fur_density`・`fur_flow`。`BYTE_COLOR` は `color_srgb` を読み、塗った数値を使う。`FLOAT_COLOR` は `color` の値そのものを読む。`POINT` は頂点値、`CORNER` は同じ頂点の全ループを算術平均するため、角ごとの塗り分けは平均される。`fur_flow.RGB = 0.5 + 0.5 * d` で同じワールド座標空間の 3D 方向を符号化し、A は使わない。これは最終 PNG の 2D 流れの符号化とは異なる。

Blender は `obj.data` の元のメッシュと、`use_deform` が真の骨の `head_local`・`tail_local` を取り出し、それぞれの `matrix_world` で共通座標へ変換する。法線は逆転置で変換する。ポーズや modifier 適用後の評価済みメッシュを焼く処理ではない。

## 手描きの補正マスク

`paint_overlays` または `--paint-overlay length=PATH` のように指定する。対象は `length`・`density`・`clump`・`flow` の 4 つで、Region の補正はない。マスクは出力と同じ N×N の R8 グレー PNG を使い、UV・行方向も出力とそろえる。拡縮はしない。読込器は RGB や alpha 付き PNG も受けるが、使うのは先頭のチャネルだけ。8bit・パレットなし・インターレースなしが必要。

浮動小数のマスク値 m に対し `delta = 2 * (m - 0.5)`。0.5 が恒等となる。8bit マスク b は `delta = (b - 127.5) / 127.5` だが、127 と 128 は特別に delta = 0 とする。配列を直接渡すコア API は `uint8` または有限な 0〜1 の浮動小数配列にも対応する。

長さ・密度・束はそれぞれ `clamp(value + delta, 0, 1)` とする。流れは UV 平面で `angle = delta * π`、`(x', y') = (cos(angle)*x - sin(angle)*y, sin(angle)*x + cos(angle)*y)` と回す。+Y が下向きなので、正角は画像上で時計回り。流れの大きさは保ち、ゼロから向きは作らない。

補正は膨張後・量子化前に画像全体へ重ねる。CLI は設定の同じ対象を上書きし、CLI で同じ対象を複数指定したら最後のものだけを使う。1 対象に複数枚を累積する指定ではない。適用順は対象名の昇順（`clump`・`density`・`flow`・`length`）で、各対象は別成分を変える。

## 出力の情報と NPZ

`fur_maps.json` は次の欄を持つ。PNG のパスはこのファイルからの相対パス。

| 欄 | 意味 |
|---|---|
| `format` | `norveslib.fur_maps.v1` |
| `resolution`・`uv_set`・`v_axis`・`dilation_px` | 解像度、UV 名、`down`、設定の膨張幅 |
| `maps` | `length`・`flow`・`density_clump`・`region` を鍵とする辞書 |
| `maps.<name>.file` | PNG ファイル名 |
| `maps.<name>.png_channels` | PNG のチャネル数（1 または 3）。cook 後の数とは異なる |
| `maps.<name>.cook_format`・`encoding` | cook 形式と値の説明文字列 |
| `maps.<name>.sha256` | 保存した PNG ファイル内容の SHA-256 |
| `regions` | 値を含む領域の配列 |
| `overlays` | 適用した順の `{target, path, sha256}` 配列。ファイルの内容をハッシュ化。配列入力なら `path = <array>`、連続化した元の配列のバイトをハッシュ化 |
| `inputs.mesh_sha256` | 検証・型統一後のメッシュ内容のハッシュ。名前・配列形・固定エンディアンの値・グループ・カラーを含む。NPZ ファイル自体のハッシュではない |
| `inputs.config_sha256` | 既定を補ったコア設定の正準 JSON のハッシュ。`paint_overlays` と Blender 側の 5 欄を除く |
| `inputs.source` | Blender 入口では `{input: {path, sha256}, mesh_object, armature_object, uv_map, blender_version}`。コア CLI では入力 NPZ の絶対パス。直接 API で省略した場合は `null` |
| `stats.vertices`・`triangles` | 入力の頂点数と三角形数 |
| `stats.degenerate_uv_triangles` | UV ヤコビアン・計量などが無効で焼かなかった三角形数 |
| `stats.unresolved_vertices` | 接方向を埋められなかった頂点数 |
| `stats.covered_texels` | 膨張前の被覆数 |
| `stats.dilated_texels` | 膨張で新たに埋まった数 |
| `stats.empty_texels` | 膨張後も空の数。補正で値が付いてもこの数は変わらない |

`inputs.mesh_sha256` と `inputs.config_sha256` を比較するとメッシュや焼き設定の変更が分かる。補正の変更は別に `overlays` の対象と `sha256` を比較する。入力元の変更は Blender の `inputs.source.input.sha256` でも追えるが、参照する外部ファイルまでは含まない。プレビューは形式・チャネル数・大きさ・ID 範囲などを検査するが、記録ハッシュとの一致を検証する処理ではない。

`fur_mesh.npz` は `save_mesh_npz` で保存した圧縮配列。V は頂点数、T は三角形数、K は骨数で、位置・法線・骨は同じ座標空間。読込時は pickle を許可せず、有限性と形を検査して数値を float64、頂点番号を int64 にそろえる。

| 鍵 | 形・値 |
|---|---|
| `positions`・`normals` | `(V,3)`。V > 0 |
| `triangles` | `(T,3)` の整数頂点番号、0〜V-1。T > 0 |
| `corner_uvs` | `(T,3,2)`、V 下向きの角 UV |
| `bone_names` | `(K,)`、Unicode 文字列 |
| `bone_heads`・`bone_tails` | `(K,3)` |
| `weights` | `(V,K)`、非負のウェイト。合計 1 の制約はない |
| `group_<name>` | 任意、`(V,)`。例：`group_fur_length` |
| `color_<name>` | 任意、`(V,4)`。読込は `(V,3)` も受け、A = 1 を補う。例：`color_fur_flow` |

## コマンド

以下の論理パスは例。Blender 用の設定 `fur_config.json` は例えば次の形にする。コアは同じ設定から NPZ を焼き直せる。

```json
{
  "input": "base.glb",
  "output_dir": "generated/fur",
  "resolution": 1024,
  "uv_set": "UV0",
  "paint_overlays": {"flow": "paint/flow.png"}
}
```

```powershell
& $env:NORVESLIB_BLENDER --background --factory-startup --python Scripts/Blender/fur_flow.py -- C:/FurWork/fur_config.json
python -B Scripts/Blender/fur_maps_core.py --mesh C:/FurWork/generated/fur/fur_mesh.npz --config C:/FurWork/fur_config.json --out C:/FurWork/rebaked --paint-overlay flow=C:/FurWork/paint/flow.png
python -B Scripts/Blender/fur_preview.py C:/FurWork/rebaked/fur_maps.json --out C:/FurWork/rebaked/fur_flow_preview.png --spacing 16 --scale 2 --background region
```

`NORVESLIB_BLENDER` には Blender 実行ファイルを指定する。`fur_flow.py` の CONFIG は JSON ファイルまたは JSON オブジェクト文字列。Blender の `--` より後へ渡す。

プレビューの INPUT は情報ファイルまたはそのディレクトリ。`--out` の既定は同じ場所の `fur_flow_preview.png`。`--spacing` は 1 以上の整数（既定 16）、`--scale` は 1〜8（既定 2）、`--background` は `length`・`region`・`none`（既定 `length`）。赤い根元から黄色い線が伸びる向きが流れを表す。流れの長さ 0.5 未満で追跡を止める。UV の線画であり、実物の毛の長さや見た目の再現ではない。

```powershell
$assetCook = 'build/Tools/AssetCook/Debug/AssetCook.exe'
& $assetCook --input C:/FurWork/rebaked/fur_length.png --out C:/FurWork/cooked/length.nvpak --manifest C:/FurWork/cooked/length.manifest.json --logical textures/creature/fur_length --kind texture --entry fur_length.nvtex --entry-type Tex0 --format nvtex.v0.r8.linear --variant default
& $assetCook --input C:/FurWork/rebaked/fur_flow.png --out C:/FurWork/cooked/flow.nvpak --manifest C:/FurWork/cooked/flow.manifest.json --logical textures/creature/fur_flow --kind texture --entry fur_flow.nvtex --entry-type Tex0 --format nvtex.v0.rg8.linear --variant default
& $assetCook --input C:/FurWork/rebaked/fur_density_clump.png --out C:/FurWork/cooked/density_clump.nvpak --manifest C:/FurWork/cooked/density_clump.manifest.json --logical textures/creature/fur_density_clump --kind texture --entry fur_density_clump.nvtex --entry-type Tex0 --format nvtex.v0.rg8.linear --variant default
& $assetCook --input C:/FurWork/rebaked/fur_region.png --out C:/FurWork/cooked/region.nvpak --manifest C:/FurWork/cooked/region.manifest.json --logical textures/creature/fur_region --kind texture --entry fur_region.nvtex --entry-type Tex0 --format nvtex.v0.r8.linear --variant default
```

出力ディレクトリはあらかじめ用意する。各コマンド直後の `$LASTEXITCODE` を確認する。コア・プレビューと `fur_flow.py` のスクリプト側の終了コードは成功 0、入力・設定・保存の失敗 1、引数の誤り 2（`--help` は 0）。Blender のプロセス終了コードは Blender 自体の起動処理にも依存する。成功時のログは焼き込みが `FUR_MAPS`、線画が `FUR_PREVIEW`。AssetCook の成功は 0、非零は cook 失敗として扱う。

## 既知の限界

UniRig 出力の約 14 関節の骨格は粗く、骨方向だけでは四肢の先と顔の流れが足りない。骨規則と領域の表、頂点の塗り、共通 UV の補正マスクを併用する。頭の先・耳・尾の付け根では骨方向が法線と近くなり、接方向が退化しやすい。近傍補完の反復でも埋まらない場合がある。

Region には補正マスクがない。UV が重なる場所は後の三角形が勝ち、個別の面へ別の値を持たせられない。退化 UV は焼かれず、周囲からの膨張だけで埋まる場合がある。頂点カラーの CORNER の違いは頂点平均で失われる。

Blender での抽出と実物の見え方の確認は、コアの焼き出し・プレビュー生成・AssetCook の受け入れとは別の確認項目となる。UV 線画と cook の成功だけでは、実物の流れや根元の境界の見た目までは保証しない。
