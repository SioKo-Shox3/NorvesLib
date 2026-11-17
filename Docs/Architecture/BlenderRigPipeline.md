# Blender 骨格資産パイプライン

## 目的と置き場所

Blender の生成物をエンジンへ渡す前の生成・検査・cook の入口を `Scripts/Blender/` に置く。共通骨格の検査規則を取り込み側の AssetCook と同じリポジトリで管理し、規則のずれを防ぐ。生成物は git 管理外の `Assets/Models/Generated/<id>/` に置く。

Blender の既定パスは `Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe`。環境変数 `NORVESLIB_BLENDER` または `pipeline.py` の `--blender` で上書きできる。AssetCook の既定は `NORVESLIB_ASSETCOOK`、未設定なら `build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe` であり、`--assetcook` でも指定できる。

## 段と起動

`python -B Scripts/Blender/pipeline.py --input <asset> --out <dir> --logical <stem>` を使う。出力は `--out` からの相対パスで、`<stem>` は `--logical` の値。

| 段 | 入力 | 出力 | 起動 |
|---|---|---|---|
| `rebuild` | 初期入力または直前の資産 | `rebuild/<stem>.glb` | Blender、`rebuild_on_base.py` |
| `weights` | 初期入力または直前の資産 | `weights/<stem>.glb` | Blender、`transfer_weights.py` |
| `export` | 初期入力または直前の資産 | `export/<stem>.gltf` | Blender、`export_gltf.py` |
| `validate` | 初期入力または直前の資産 | `validate/report.json` | `python -B validate_rig.py <asset> --spec <spec> --report <report>` |
| `cook` | 初期入力または直前の資産 | `cooked/` | `AssetCook --rig-split --input <asset> --out <dir> --logical <stem>` |

Blender の起動形は `blender -b --factory-startup -P <script> -- <JSON>`。JSON の鍵は `stage`、`input`、`output`、`spec`、`record`、`settings` で、`record` は `<stage>/record.json` を指す。各スクリプトは `common.load_config()` で `--` の後の JSON オブジェクト文字列または JSON ファイルを 1 個読む。指定がなければ空のオブジェクトになり、重複鍵や不正な JSON は拒否する。3 本の Blender 用スクリプトは未実装で、選択して実行すると事前確認で止まる。

`--stages validate,cook` のように段を選べる。省略時は全段を選び、指定順によらず表の順に実行する。未知の段や重複指定は引数エラーになる。資産を次に渡すのは `rebuild`・`weights`・`export` だけで、`validate` はレポートを出し、次の `cook` には同じ資産を渡す。各段で失敗すると後続を起動しない。`--logical` は `[A-Za-z0-9_][A-Za-z0-9_.-]*` に一致させる。

`--spec` は共通仕様、`--python` は検査に使う Python を指定する。`--settings <path>` は段名を鍵、設定オブジェクトを値とする JSON を読む。未知の段名、オブジェクト以外の値、重複鍵、記録できない値は拒否する。設定の内容を渡すのは Blender の段だけで、`validate`・`cook` の設定オブジェクトは CLI 引数に反映されない。

`--dry-run` は選択した各段を `PIPELINE_STAGE index=<番号> name=<段> argv=<JSON配列>` として表示し、段の実行、事前確認、出力や記録の作成をしない。ただし引数と設定 JSON は検査する。通常実行は選択した段のスクリプトと実行ファイル、入力と仕様、cook 出力先の不存在を先に確認し、開始時と終了時に `PIPELINE_STAGE`、記録後に `PIPELINE_RESULT` を出す。`status` は開始時 `start`、終了時 `ok` または `failed`。

`pipeline.py` の終了コードは成功 0、事前確認・処理・記録の失敗 1、引数や設定の誤り 2。`--help` は 0。

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

`pipeline.py` は `<out>/record.json` に `record_version`（1）、`logical`、`spec`、`input`、`stages`、`status` を書く。`spec` と `input` は上記のファイルまたはディレクトリの記録。`stages` の各要素の鍵は `name`、`kind`、`argv`、`exit_code`、`inputs`、`config_sha256`、`outputs`、`blender_version`。`kind` は `blender`・`python`・`assetcook`。Blender の `config_sha256` は設定 JSON、他の段は `argv` の JSON のハッシュである。Blender の版は各段の記録から読み、それ以外は `null`。出力が存在する場合に `outputs` に記録する。出力の不存在は失敗として扱わず、`exit_code` が 0 なら `outputs` が空でも成功し得る。各段の `status` 欄はなく、子プロセスの終了コードを `exit_code` に記録する。全体の `status` は `ok` または `failed`。

ファイル入力のハッシュは指定ファイル自体のものなので、外部 `.bin` などの参照ファイルは含まれない。事前確認が失敗した場合はパイプラインの記録を作らない。

## 検査とエンジンの境界

`validate_rig.py` は共通骨格仕様を検査し、エンジンの取り込み規則を複製しない。最終的な受け入れは `AssetCook --rig-split` の成功で判断する。入口の実装は `Tools/AssetCook/RigSplitFileCli.cpp`、詳細は [RigStaticRootFrame128.md](RigStaticRootFrame128.md) と [SkeletalImportLimits.md](SkeletalImportLimits.md) を参照する。

`--rig-split` は mesh 付きの 1 mesh・1 skin を対象にし、`StaticRootFrame256` で最大 256 関節を受ける。`skin.skeleton` が省略された場合は joint と node の階層から唯一の根の関節を求め、指定時は根の関節かその祖先を要求する。関節間の非関節の中間親は拒否する。根の上の非関節の祖先は、静的な直接 TRS で正の一様 scale などの条件を満たす場合に受け入れる。非関節の祖先へのアニメーションはエンジンも拒否するが、cook 前に理由を示して止めるため検査にも置く。

AssetCook には `--morph drop`、`--cubicspline bake`、`--skin-influences reduce` などの明示オプションがある。これらはエンジン側の受け入れ・変換の規則であり、既定の共通仕様の検査結果とは別である。`pipeline.py` はこれらのオプションを転送しないため、必要な場合は AssetCook を直接呼び出す。
