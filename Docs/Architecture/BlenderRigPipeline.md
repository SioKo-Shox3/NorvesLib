# Blender 骨格資産パイプライン

## 目的と置き場所

Blender の生成物をエンジンへ渡す前の生成・検査・cook の入口を `Scripts/Blender/` に置く。共通骨格の検査規則を取り込み側の AssetCook と同じリポジトリで管理し、規則のずれを防ぐ。生成物は git 管理外の `Assets/Models/Generated/<id>/` に置く。

Blender の既定パスは `Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe`。環境変数 `NORVESLIB_BLENDER` または `pipeline.py` の `--blender` で上書きできる。AssetCook の既定は `NORVESLIB_ASSETCOOK`、未設定なら `build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe` であり、`--assetcook` でも指定できる。

## 段と起動

`python -B Scripts/Blender/pipeline.py --config <config.json> --out <dir>` を使う。出力は `--out` からの相対パスで、`<stem>` は `logical` の値。

| 段 | 入力 | 出力 | 起動 |
|---|---|---|---|
| `rebuild` | 初期入力または直前の資産 | `rebuild/<stem>.blend` | Blender、`rebuild_on_base.py` |
| `weights` | 初期入力または直前の資産 | `weights/<stem>.blend` | Blender、`transfer_weights.py` |
| `export` | 初期入力または直前の資産 | `export/<stem>.glb` または `export/<stem>.gltf` | Blender、`export_gltf.py` |
| `validate` | 初期入力または直前の資産 | `validate/report.json` | `python -B validate_rig.py <asset> --spec <spec> --report <report>` |
| `cook` | 初期入力または直前の資産 | `cooked/` | `AssetCook --rig-split --input <asset> --out <dir> --logical <stem>` |

中間資産に `.blend` を使うのは、GLB を挟むと骨格のない頂点グループや焼いた画像の材質が失われるため。`export.format` の既定は `glb`、`gltf` は分離形式である。

Blender の起動形は `blender --background --factory-startup --python-exit-code 1 --python <script> -- <JSON>`。JSON の鍵は `stage`、`input`、`output`、`record`、`spec`、`settings` であり、パイプラインは `record` に `<out>/<stage>/record.json` を渡す。段のスクリプトを単独で起動する場合も同じ JSON を使い、`--` の後に JSON オブジェクト文字列または JSON ファイルのパスを 1 個渡す。`stage`・`input`・`output`・`record` は必須、`spec` の省略時は共通仕様、`settings` の省略時は空のオブジェクトを使う。単独起動の相対パスはカレントディレクトリを基準に解決する。未知の鍵、重複鍵、不正な JSON を拒否する。

`--config <path>` のルートに指定できる鍵は `input`・`logical`・`spec`・`stages`・`settings`。`input` と `spec` および `settings` 内のパスは設定ファイルのあるディレクトリから解決する。`logical` は `[A-Za-z0-9_][A-Za-z0-9_.-]*` に一致させる。`stages` は段名の配列、`settings` は段名を鍵とするオブジェクトで、`input`・`logical`・`spec`・`stages` は対応するコマンドライン引数で上書きできる。コマンドラインに指定したパスと `--out` はカレントディレクトリから解決する。`--out` は必須で、設定ファイルには書けない。

`--settings <path>` は `settings` オブジェクトだけを別ファイルから読む方法で、内部のパスはそのファイルのあるディレクトリから解決する。`--config` と `--settings` は同時に指定できない。設定の段名が未知の場合や、段の値がオブジェクトでない場合は拒否する。`validate`・`cook` の設定オブジェクトは受け付けるが CLI 引数に反映しない。

`--stages validate,cook` のように段を選べる。省略時は全段を選び、指定順によらず表の順に実行する。未知の段や重複指定は引数エラーになる。資産を次に渡すのは `rebuild`・`weights`・`export` だけで、`validate` の次の `cook` には同じ資産を渡す。各段が失敗すると後続を起動しない。

選択した Blender の段は起動前に設定を検査し、未知の鍵を拒否し、既定値を補ってパスを絶対化する。選択していない Blender の段は設定がある場合だけ検査する。したがって `rebuild` を選ぶなら `rebuild.base` が必須であり、設定なしで全段を選ぶと引数エラーになる。段の記録とパイプラインの `config_sha256` は、この正規化後に渡す設定を含む JSON から求める。

`--dry-run` は設定を検査した後、選択した各段を `PIPELINE_STAGE index=<番号> name=<段> argv=<JSON配列>` として表示する。段の実行、ファイルの存在などの事前確認、出力や記録の作成はしない。通常実行では入力と仕様、選択した段のスクリプトと実行ファイル、選択した `rebuild.base` と転送時の `weights.donor` のファイルの存在、cook 出力先の不存在を先に確認する。開始時と終了時に `PIPELINE_STAGE`、記録後に `PIPELINE_RESULT` を出す。`status` は開始時 `start`、終了時 `ok` または `failed`。

`pipeline.py` の終了コードは成功 0、事前確認・処理・記録の失敗 1、引数や設定の誤り 2。`--help` は 0。各 Blender 段の終了コードは成功 0、処理の失敗またはしきい値の超過 1、設定の誤り 2。

## 段の設定

表の数値範囲は両端を含む。数値は有限値に限り、整数に真偽値は使えない。`null` を許す文字列も、指定する場合は空にしない。パスは正規化時に絶対化する。

### rebuild

| 鍵 | 型・範囲 | 既定値 |
|---|---|---|
| `base` | パス | 必須 |
| `base_object` | 文字列または `null` | `null` |
| `source_object` | 文字列または `null` | `null` |
| `source_rotation_deg` | 有限数 3 個の配列 | `[0, 0, 0]` |
| `landmarks` | ランドマークの配列または `null` | `null` |
| `bbox_scale_axis` | `longest` / `x` / `y` / `z` | `longest` |
| `bbox_anchor` | `center` / `bottom` | `center` |
| `wrap_method` | `NEAREST_SURFACEPOINT` / `PROJECT` | `NEAREST_SURFACEPOINT` |
| `project_limit` | 数値、0 以上 | `0.0` |
| `iterations` | 整数、1〜100 | `3` |
| `smooth_factor` | 数値、0〜1 | `0.5` |
| `smooth_repeat` | 整数、0〜100 | `1` |
| `bake_size` | 整数、16〜8192 | `1024` |
| `bake_samples` | 整数、1〜4096 | `16` |
| `bake_seed` | 整数、0〜2147483647 | `0` |
| `bake_margin` | 整数、0〜64 | `16` |
| `cage_extrusion` | 数値、0 以上 | `0.0` |
| `max_ray_distance` | 数値、0 以上 | `0.0` |
| `normal_presmooth_iterations` | 整数、0〜1000 | `0` |
| `normal_presmooth_factor` | 数値、0〜1 | `0.5` |
| `body_length` | 正の数または `null` | `null` |
| `max_distance_ratio` | 正の数 | `0.01` |
| `distance_metric` | `residual` / `displacement` | `residual` |
| `distance_statistic` | `max` / `p95` | `max` |

`landmarks` は 3 点以上で、各要素は重複しない空でない `name` と、有限数 3 個の配列 `source`・`target` を持つ。他の鍵は使えない。ランドマークと非ゼロの `source_rotation_deg` は併用できない。

### weights

| 鍵 | 型・範囲 | 既定値 |
|---|---|---|
| `mode` | `keep` / `transfer` | `keep` |
| `target_object` | 文字列または `null` | `null` |
| `max_influences` | 整数、1〜8 | `4` |
| `clean_threshold` | 数値、0〜1 | `0.0` |
| `donor` | パスまたは `null` | `null` |
| `donor_object` | 文字列または `null` | `null` |
| `donor_space` | `source` / `base` | `source` |
| `armature` | `base` / `donor` | `base` |
| `joint_map` | 空でない文字列同士の対応オブジェクトまたは `null` | `null` |
| `unmapped` | `error` / `drop` / `keep` | `error` |

`transfer` では `donor` が必須。`keep` では `donor`・`donor_object`・`donor_space`・`armature`・`joint_map`・`unmapped` に既定値以外を指定できない。`joint_map` の個々の鍵と値は空文字列にできないが、空のオブジェクト自体は指定できる。

### export

| 鍵 | 型・範囲 | 既定値 |
|---|---|---|
| `format` | `glb` / `gltf` | `glb` |
| `actions` | 重複しない空でない文字列の配列または `null` | `null` |
| `rest_clip_if_empty` | 真偽値 | `true` |
| `frame_step` | 整数、1〜100 | `1` |
| `image_format` | `AUTO` / `NONE` | `AUTO` |
| `armature_object_remove` | 真偽値 | `false` |

`actions` に空配列を指定できる。

## 張り直しとベイク

土台シーンから `base_object` でメッシュを選ぶ。指定がなければ候補がちょうど 1 個必要で、UV も必要。メッシュデータが共有されていれば複製してから編集する。入力側は `source_object` があればそのメッシュを選び、なければ取り込んだメッシュを結合する。

位置合わせには 2 方式ある。ランドマーク指定時は `source` を取り込み直後の入力のワールド座標、`target` を土台のワールド座標として対応させる。未指定時は `source_rotation_deg` の XYZ オイラー角を度で受け、列ベクトルに `Rz @ Ry @ Rx` の順で回転を適用し、回転後の入力と土台のワールド座標の軸平行境界箱を、一様な倍率と中心または Z 底面の位置で合わせる。`longest` はそれぞれの箱の最長辺、`x`・`y`・`z` は指定軸の辺を尺度に使う。求めた入力ワールド座標から土台ワールド座標への 4×4 変換を入力に適用し、行優先の 16 要素として土台の `norves_source_to_base` に保存する。

土台の各頂点から位置合わせ後の入力表面への最近傍距離を張り直し前に求めたものが `displacement`、Shrinkwrap と平滑化の反復後に求めたものが `residual`。`displacement` は同一頂点の変形前後の移動距離ではない。体長には `body_length` を使い、未指定なら位置合わせ後の入力のワールド境界箱の最長辺を使う。両方の最大値・p95 と体長比を記録し、選択した `distance_metric` と `distance_statistic` の体長比が `max_distance_ratio` を超えた場合は、ベイクと `.blend` の保存、段の記録を行って終了コード 1 を返す。

Shrinkwrap と平滑化のモディファイアは適用前にスタックの先頭へ移す。ベイクは Cycles の CPU、指定したサンプル数とシードを使い、デノイズは無効。選択物からアクティブへのベイクで cage を有効にし、`cage_extrusion`・`max_ray_distance`・`bake_margin` と最初の UV を使う。色は diffuse の color、法線は tangent 空間で焼く。法線のみ、指定された反復数が正なら入力のコピーを先に平滑化する。

画像は `.blend` と同じディレクトリの `<stem>_basecolor.png` と `<stem>_normal.png` に保存し、`.blend` に pack する。色は sRGB、法線は Non-Color とし、焼いた画像を使う材質を土台に設定する。段の `outputs` は `.blend` と 2 枚の PNG を含む。

## ウェイト

対象メッシュは `target_object`、未指定なら `norves_source_to_base` を持つメッシュ、それもなければ唯一のメッシュを選ぶ。候補が複数、または存在しなければ失敗する。`keep` は土台の骨格と既存ウェイトを使う。土台の骨格は対象の Armature モディファイア、次に親から探す。

`transfer` は `donor` を読み、`donor_object` 指定時はそのメッシュ、未指定時は頂点グループを持つ唯一のメッシュを使う。`donor_space=source` では保存した `norves_source_to_base` をドナーに適用し、骨格があればそれにも適用する。骨格またはそのデータにアクションや NLA ストリップのアクションがある場合、この変換は拒否する。`base` はドナーがすでに土台の座標にある扱い。

`joint_map` はドナーの頂点グループ名から行き先名への対応で、`null` なら同名対応。対応表にない名前は `unmapped=error` で失敗、`drop` で除外、`keep` で元の名前を維持する。複数の名前が同じ行き先に写る場合は重みを加算し、頂点ごとに 1 で上限を切る。`JOINT_MAP` に未対応・統合・不明な行き先・未使用の鍵を出す。`armature=base` で対応表を使った行き先が土台の変形用の骨にない場合は失敗する。

`armature=base` では土台の骨格を使い、`donor` ではドナーの骨格を選び、対応表があれば骨名も変更して対象を結び付ける。転送は対象の骨に対応する既存グループを取り除き、最近傍ポリゴン上の補間でドナーのウェイトを置き換える。変形用グループを対象に、`clean_threshold > 0` のとき `vertex_group_clean`（`keep_single=True`）、続いて `vertex_group_normalize_all`、`vertex_group_limit_total`、再度 `vertex_group_normalize_all` の順に処理する。いずれも `BONE_DEFORM` を使い、影響数は `max_influences` で制限する。重みが 0 の頂点は `MESH_STATS` に数えるが、この段はそれだけでは失敗せず、後続の検査段が拒否する。

## 書き出し

`GLTF_EXPORT_TABLE` は Blender 5.2 系だけを対象とし、それ以外は書き出し前に停止する。対象の版でも glTF 書き出し演算子の RNA プロパティ名と表のオプション名を照合し、欠けていれば停止する。出力の拡張子は `format` に対応する `.glb` または `.gltf` が必要。

シーンの骨格はちょうど 1 個必要で、その骨格を指す Armature モディファイアのないメッシュが 1 個でもあれば拒否する。結び付いたメッシュがなければ失敗し、複数なら結合して骨格とともに選択する。書き出しは選択物のみ、Y-up、UV・法線・材質・スキン・変形用の骨・アニメーションを含み、カメラ・ライト・extras・モーフは含めない。アニメーションは `ACTIONS`、強制サンプリング、サイズ最適化なしで書き出す。

`actions=null` ならシーン内の全アクション、配列なら指定した名前だけを使う。存在しない名前は失敗する。アクションがなく `rest_clip_if_empty=true` なら各ポーズ骨の平行移動・回転・拡大にフレーム 0 と 1 の静止キーを持つ `rest` を作り、この場合の `frame_step` は 1 にする。`false` なら警告を出してクリップなしで続ける。各アクションの名前と範囲を `EXPORT_CLIP` に出す。

出力後に GLB の JSON チャンクまたは `.gltf` の JSON を読み、分離形式のローカル参照ファイルの有無、書き出されたクリップの名前と個数が選択アクションに一致すること、各 sampler の補間が骨格仕様の `interpolations` に含まれることを確認する。既定仕様は `LINEAR` と `STEP`。書き出し器は `export_force_sampling=True` でも、キーのないチャンネルや 2 キーで値が変わらないチャンネルを `STEP` にすることがある。一定値なので `LINEAR` と同じであり、エンジンも `STEP` を受ける。これらの確認で不一致や欠落があれば、生成済みの出力と存在するファイルの記録を残して終了コード 1 を返す。`.gltf` の出力記録には本体のほか、参照するローカルのバッファと画像も含める。JSON の後処理は行わない（G6-S2）。

## 出力の行

各行はタグと `key=value` を空白でつなぐ。値の引用と JSON 表現は後述の検査段と共通。

```text
ALIGN mode=landmarks points=<n> scale=<number> rms_error=<number>
ALIGN mode=bbox scale=<number>
MESH_STATS stage=rebuild base_vertices=<n> base_triangles=<n> source_vertices=<n> source_triangles=<n> body_length=<number> residual_max=<number> residual_p95=<number> residual_max_ratio=<number> residual_p95_ratio=<number> displacement_max=<number> displacement_p95=<number> displacement_max_ratio=<number> displacement_p95_ratio=<number> metric=<residual|displacement> statistic=<max|p95> threshold_ratio=<number>
MESH_STATS stage=weights mode=<keep|transfer> vertices=<n> vertex_groups=<n> deform_groups=<n> zero_weight_vertices=<n> max_influences=<n>
MESH_STATS stage=export file=<name> format=<glb|gltf> meshes_joined=<n> materials=<n> deform_bones=<n> clips=<n>
BAKE size=<n> samples=<n> seed=<n> device=CPU presmooth_iterations=<n>
JOINT_MAP renamed=<n> unmapped=<JSON配列> merged=<JSONオブジェクト> unknown_targets=<JSON配列> unused_keys=<JSON配列>
BLENDER_VERSION version=<版> table=<主版.副版>
EXPORT_CLIP name=<name> frame_start=<number> frame_end=<number>
STAGE_RESULT stage=<rebuild|weights|export> status=<ok|failed> exit_code=<0|1> record=<path>
```

`JOINT_MAP` は転送時に出る。`STAGE_RESULT` は段が結果を返して記録を書けた場合に出る。設定エラーや処理中の例外など、そこまで到達しない失敗では段の記録や `STAGE_RESULT` が作られるとは限らない。

## rig_core

`rig_core.py` は `bpy` に依らず、位置合わせ、距離統計、関節名の対応表、設定の検査と正規化、glTF 書き出しオプション表を扱う。ランドマークの Procrustes は Horn の四元数法で、最小二乗の回転・正の一様倍率・平行移動を求める。2 点以下、一致または一直線の退化、鏡映になる対応は拒否する。ただし同一平面の点では鏡映と回転を区別できないため、鏡映として拒否しない。境界箱の方式は指定回転後の辺と基準位置から相似変換を求める。

距離統計は非負の有限値から最大・平均・p95・体長比を求める。p95 は昇順の最近順位、すなわち 1 始まりの `ceil(0.95 * n)` 番目の値。関節対応では改名、未対応、多対一の統合、実際に使った不明な行き先、未使用の対応表の鍵を返す。`GLTF_EXPORT_TABLE` は 5.2 系だけを登録する。

## 共通骨格仕様

`Scripts/Blender/skeleton_spec.json` は次を定義する。

| 鍵 | 意味と現在値 |
|---|---|
| `spec_version` | 仕様の版。`1` |
| `name` | 仕様名。`quadruped_common_v1` |
| `vocabulary` | role の語彙。`quadruped_v1` |
| `roles` | role から関節名の配列への対応。下表の 23 role、計 29 関節 |
| `required_roles` | 検査対象の各 skin に必要な role。下表の必須 20 role |
| `limits.max_joints` | skin ごとの関節数の上限。`128` |
| `limits.max_influences` | 頂点ごとの正の重みの数の上限。`4` |
| `limits.weight_sum_tolerance` | 重みの和と 1 の許容差。`0.001` |
| `naming.joint_pattern` | 関節名に適用する正規表現。現在値は下記 |
| `uv.sets`、`uv.min`、`uv.max` | UV は `TEXCOORD_0` のみ、値は `0.0`〜`1.0` |
| `morph_targets` | モーフターゲットの許可。`false` |
| `interpolations` | 許可する補間。`LINEAR`、`STEP` |

```text
^[a-z]+(_([a-z]+|[LR]|[0-9]{2}))*$
```

| role | 関節名（配列順） | 必須 |
|---|---|---|
| `root` | `root` | はい |
| `pelvis` | `pelvis` | はい |
| `spine` | `spine_01`, `spine_02`, `spine_03` | はい |
| `neck` | `neck_01`, `neck_02` | はい |
| `head` | `head` | はい |
| `jaw` | `jaw` | いいえ |
| `tail` | `tail_01`, `tail_02`, `tail_03`, `tail_04` | はい |
| `front_L_clavicle` | `front_L_clavicle` | いいえ |
| `front_L_upper` | `front_L_upper` | はい |
| `front_L_lower` | `front_L_lower` | はい |
| `front_L_paw` | `front_L_paw` | はい |
| `front_R_clavicle` | `front_R_clavicle` | いいえ |
| `front_R_upper` | `front_R_upper` | はい |
| `front_R_lower` | `front_R_lower` | はい |
| `front_R_paw` | `front_R_paw` | はい |
| `hind_L_thigh` | `hind_L_thigh` | はい |
| `hind_L_shin` | `hind_L_shin` | はい |
| `hind_L_hock` | `hind_L_hock` | はい |
| `hind_L_paw` | `hind_L_paw` | はい |
| `hind_R_thigh` | `hind_R_thigh` | はい |
| `hind_R_shin` | `hind_R_shin` | はい |
| `hind_R_hock` | `hind_R_hock` | はい |
| `hind_R_paw` | `hind_R_paw` | はい |

`spine`・`neck`・`tail` は複数関節の chain、それ以外は各 1 関節。必須 role の配列内の関節はすべて必要で、任意 role の関節はなくてもよい。

`python -B Scripts/Blender/skeleton_spec.py --check` は仕様そのものを検査し、`SPEC_CHECK` を出す。`--emit-role-profile` は `vocabulary` と `target_roles` を JSON で標準出力し、`--out <path>` でファイルに書ける。どちらも `--spec <path>` で仕様を変更できる。正常は 0、不正な仕様は 1、引数の誤りは 2。

`target_roles` は role ごとに `{"joint": <name>, "C": [1,0,0,0,1,0,0,0,1]}` の配列を持ち、正準 role 順と chain の配列順を保つ。恒等の `C` も明示する。これは [SkeletalRoleProfile.md](SkeletalRoleProfile.md) の GR84・GR85 の role profile v1 と共有できる断片であり、profile 全体として使うには `version`、`axes`、`units`、`position_convention`、`time`、`source_roles` などの必要な欄を別途指定する。

## 資産の検査

`python -B Scripts/Blender/validate_rig.py <input.gltf|input.glb> [--spec <path>] [--report <path>]` を実行する。指定がなければ同じディレクトリの仕様を使う。`--report` は入力や仕様と異なるパスにする。

以下の context は `RIG_ISSUE` と JSON の issue に追加される鍵。各 issue には共通して `severity`、`code`、`message` がある。

| code | 重さ | 検査内容 | context の鍵 |
|---|---|---|---|
| `spec_error` | error | 仕様ファイルの読み取り・内容の誤り | なし |
| `read_error` | error | 入力・参照バッファの読み取り、構造やレポート書き込みの誤り | なし |
| `no_skin` | error | skin がない | なし |
| `joint_count` | error | skin の関節数が上限を超える | `skin`, `joints`, `limit` |
| `duplicate_joint_name` | error | 同じ skin 内の関節名が重複 | `skin`, `joint` |
| `joint_name` | error | 関節名が命名規則に一致しない | `skin`, `joint` |
| `missing_role` | error | 必須 role に必要な関節が skin にない | `skin`, `role`, `joint` |
| `missing_weights` | error | skin 付き mesh の primitive に `WEIGHTS_0` がない | `mesh`, `primitive` |
| `influences` | error | 正の重みの数が上限を超える | `vertices`, `max`, `limit`, `mesh`, `primitive` |
| `zero_weight_vertex` | error | 重みの和が 0 の頂点がある | `vertices`, `mesh`, `primitive` |
| `weight_sum` | error | 重みの和が 1 から許容差を超えて外れる、または非有限 | `vertices`, `worst`, `tolerance`, `mesh`, `primitive` |
| `uv_extra_set` | error | 許可されていない `TEXCOORD_1` 以降がある | `attribute`, `mesh`, `primitive` |
| `uv_missing` | error | `TEXCOORD_0` がない | `mesh`, `primitive` |
| `uv_range` | error | `TEXCOORD_0` の値が非有限か範囲外 | `values`, `min`, `max`, `mesh`, `primitive` |
| `morph_targets` | error | 仕様で禁止されたモーフターゲットがある | `targets`, `mesh`, `primitive` |
| `interpolation` | warning | sampler の補間が仕様の許可一覧にない | `interpolation`, `animation`, `sampler` |
| `time_not_increasing` | error | sampler の時刻が非有限か狭義単調増加でない | `index`, `animation`, `sampler` |
| `animated_non_joint_ancestor` | error | 関節でない祖先に animation channel がある | `animation`, `channel`, `node`, `node_name`, `path` |

重みは対応する `JOINTS_n` と対になった `WEIGHTS_n` を集計する。UV の範囲判定には境界の外側に `1e-6` の許容幅を設ける。警告だけなら検査は成功する。

コンソールには次の形で 1 行ずつ出す。

```text
MESH_STATS file=<name> vertices=<n> triangles=<n> uv_layers=<n> materials=<n> skins=<n> joints=<n> animations=<n>
RIG_ISSUE severity=<error|warning> code=<code> <context...> message=<message>
RIG_RESULT status=<ok|fail> errors=<n> warnings=<n>
```

`MESH_STATS` は読み取れた場合に出し、全 mesh の集計を示す。`common.format_line` はタグと `key=value` を空白でつなぐ。文字列が `[A-Za-z0-9_.:/\\+\-]+` に一致すれば引用せず、それ以外は JSON 文字列として引用・エスケープする。真偽値は `true` / `false`、欠値は `null`、配列とオブジェクトはコンパクトな JSON とする。

`--report` の JSON は `report_version`（1）、`input`、`input_sha256`、`spec`、`spec_name`、`stats`、`issues`、`status` を持つ。`input` と `spec` は絶対パス、`stats` は `MESH_STATS` の `file` 以外の値、`issues` は上表の issue 配列、`status` は `ok` または `fail`。入力を読めなければ `stats` は空、入力のハッシュを得られなければ `input_sha256` は `null`。レポート自体を書けないときは `read_error` をコンソールに出す。

終了コードはエラーなし 0、入力の読み取り・仕様違反・レポート書き込みの失敗 1、引数または仕様ファイルの誤り 2。

## 記録

`common.make_record(stage, inputs, config, outputs)` は `record_version`（1）、`stage`、`blender_version`、`inputs`、`config`、`config_sha256`、`outputs` を持つオブジェクトを作る。Blender の段はこの形式を `common.write_record` で指定された `record` に書く契約である。`blender_version` は `bpy.app.version_string`、取得できない場合は `null`。

ファイルの記録は絶対 `path` と内容の `sha256`。ディレクトリは絶対 `path`、相対 `path` と `sha256` の一覧 `files`、その一覧の `sha256` を持つ。一覧は相対パス順。JSON のハッシュは鍵順を整え、空白を除いた UTF-8 表現から計算する。

`pipeline.py` は `<out>/record.json` に `record_version`（1）、`logical`、`spec`、`input`、`stages`、`status` を書く。`spec` と `input` は上記のファイルまたはディレクトリの記録。`stages` の各要素の鍵は `name`、`kind`、`argv`、`exit_code`、`inputs`、`config_sha256`、`outputs`、`blender_version`。`kind` は `blender`・`python`・`assetcook`。Blender の `config_sha256` は設定 JSON、他の段は `argv` の JSON のハッシュである。Blender の版と `outputs` はその段が書いた記録から読み、記録があり `outputs` がなければ、主出力が存在する場合に主出力の記録を使う。Blender 以外の段も主出力が存在する場合に主出力の記録を使い、版は `null`。段の共通入口は存在する入出力だけを記録する。パイプライン自体は主出力の不存在を失敗として扱わず、子の `exit_code` が 0 なら `outputs` が空でも成功し得る。各段の `status` 欄はなく、子プロセスの終了コードを `exit_code` に記録する。全体の `status` は `ok` または `failed`。

Blender の段の記録 `<out>/<stage>/record.json` は段の起動前に消す。段が記録を書く前に失敗するなど、記録がない場合は、その段の `outputs` は空、`blender_version` は `null` となる。

ファイル入力のハッシュは指定ファイル自体のものなので、外部 `.bin` などの参照ファイルは含まれない。事前確認が失敗した場合はパイプラインの記録を作らない。

## 検査とエンジンの境界

`validate_rig.py` は共通骨格仕様を検査し、エンジンの取り込み規則を複製しない。最終的な受け入れは `AssetCook --rig-split` の成功で判断する。入口の実装は `Tools/AssetCook/RigSplitFileCli.cpp`、詳細は [RigStaticRootFrame128.md](RigStaticRootFrame128.md) と [SkeletalImportLimits.md](SkeletalImportLimits.md) を参照する。

`--rig-split` は mesh 付きの 1 mesh・1 skin を対象にし、`StaticRootFrame256` で最大 256 関節を受ける。`skin.skeleton` が省略された場合は joint と node の階層から唯一の根の関節を求め、指定時は根の関節かその祖先を要求する。関節間の非関節の中間親は拒否する。根の上の非関節の祖先は、静的な直接 TRS で正の一様 scale などの条件を満たす場合に受け入れる。非関節の祖先へのアニメーションはエンジンも拒否するが、cook 前に理由を示して止めるため検査にも置く。

AssetCook には `--morph drop`、`--cubicspline bake`、`--skin-influences reduce` などの明示オプションがある。これらはエンジン側の受け入れ・変換の規則であり、既定の共通仕様の検査結果とは別である。`pipeline.py` はこれらのオプションを転送しないため、必要な場合は AssetCook を直接呼び出す。

## 既知の限界

この文書の時点で Blender 上では実行していない。Blender 上でしか分からない誤りは実機で実行して修正する。土台メッシュは作者が手で作る資産で、リポジトリにはない。glTF の JSON は書き出し後の確認に読み取るが、後処理はしない（G6-S2）。
