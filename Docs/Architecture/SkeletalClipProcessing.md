# GR84: クリップ変換とルート移動の保存

## 入口

BVHとglTF/GLBのアニメーションを、指定した作者骨格へ変換して独立v1 ClipBankに保存する。

```text
AssetCook --retarget-clip --input run.bvh --skeleton dog.gltf --role-profile roles.json --out RuntimeRun --logical Animations/Run --clip-name Run
```

出力は新規directoryのみ。既存directoryを上書きしない。入力はUnicode対応、出力は管理cookと同じASCII path制限を持つ。既存runtimeへの追加・更新はasset-set v2のanimation項目に次を指定する。

- `skeleton_path`: source root相対のターゲットglTF/GLB
- `role_profile`: source root相対のRole Profile JSON
- `source_clip`: glTF内のクリップ名。省略時は全クリップ。BVHでは指定しない
- `clip_name`: 出力クリップ名。変換結果が1本の場合だけ指定できる

出力formatは`nvskel.v1.clips`。対象骨格とprofile、source/targetの外部bufferとsidecarのbytes・有無を既存の中央増分判定に含める。

## Role Profile v1の任意設定

既存の明示C方式を保持し、次を追加する。`match`/`align_bones`ではtarget roleの`C`を省略する。

```json
{
  "rest_pose": {"mode":"align_bones", "up_hint":[0,1,0], "maximum_error_degrees":5},
  "root_scale":1,
  "processing": {
    "output_fps":30, "loop_mode":"auto",
    "minimum_period":0.15, "maximum_period":3,
    "extract_root_motion":true, "exclude_roles":["tail"]
  }
}
```

- rest mode: `explicit` は指定 C で W = C·D·Cᵀ·B、`match` は C = I で骨方向の許容差超過を拒否、`align_bones` は最小回転と up_hint による捻りから C を求め、W = D·Cᵀ·B で骨方向を元に合わせる。
  `match`・`align_bones` で方向が作れない場合は `rest_bone_direction_missing` で拒否する。詳細は [SkeletalBvhClipImport.md の「自動のrest補正（match・align_bones）」](SkeletalBvhClipImport.md#自動のrest補正matchalign_bones) を参照する。
- root scale: 正の倍率、または`auto_height`。後者はmapped pelvis（未指定時root）とpawの高さ比を使い、pawがなければ拒否する
- loop mode: `none`は全期間、`range`は`start_seconds`/`end_seconds`、`auto`は十分な反復がある場合に周期を検出する。検出できない場合は全期間を保持する
- glTFの時刻は作者の秒を使う。BVHのfps上書きをglTFに指定すると拒否する
- 旧NVSKEL 0.2入口は新設定を黙って無視せず拒否する

## 処理と境界

sourceのlocal quaternionをSLERP/STEP評価してからFKし、作者restとの差分をターゲット作者restへ写す。IBMやt=0をrestと見なさない。一様scaleと静的RootFrameを扱う。非rootの移動channelは保持せず件数を報告し、restと異なるアニメーションscaleは拒否する。

周期はin-place姿勢の重み付き角度差で探し、格子間の周期も精密化する。重みは下流の骨長、除外roleは0。開始点には姿勢と角速度の差を使い、選んだ周期の残差を全区間へ分散する。最終keyは正確な期間末尾へ置き、bodyの先頭/末尾を一致させる。rootの上下動はbodyに残す。

根の平面XZとworld +Yのtwistを抽出し、開始headingをbind方向へ正規化する。twistが定義できない姿勢は拒否する。移動軌跡は閉じない。区間中に180度以上回るほど疎な入力の旋回回数は一意に復元できないため、必要な時間密度で作者側から出力する。

v1 bankはターゲットの作者rest/frameを保持するため、既存の束縛時rest差検査を通る。自動retargetはこの入口で明示的に行う操作であり、通常の束縛が古いクリップを黙って補正することはない。

## RMTN任意節

profile 2/3だけで使用する。旧クリップは節を出さず既存wireを保持する。各40byte recordはclip index u32、root joint u32、time f32、reserved u32=0、XZ/yawの各f64。先頭time/変位/yawは0、末尾timeはclip duration、時刻は厳密増加。root indexはcanonical化・束縛で対応付ける。sample数は共通予算へ計上する。ANLYは軌跡の要約であり、軌跡そのものの代用ではない。

この段階は抽出・保存まで。再生時の経路差分適用・ループ跨ぎ・controllerへの受渡しはG3/G4で行う。

## 検証

既存SkeletalBvhClipImportTestに16→30fps・11.6frame周期・非周期・端点・巨大frame原点・yawのケース、RigStaticRootFrameWireTestにRMTN往復/拒否、RigSplitCookTestにBVH/glTF共通変換とasset-set→CPUロード、AssetCookRigSplitSmoke.pyに実CLIの新規公開/拒否を追加した。native実行結果はPROGRESS.mdに記録する。GPU/DCC/実犬での見た目はこの合成CPU検証に含めない。

## GR87: クリップのキーの削減（cook 時）

`--clip-optimize` を指定したときだけ、cook 中にクリップを削減する。旧形式は glTF の decode の後、`--bvh` によるクリップの追加・置換を行う場合はその適用後、NVSKEL の書き出し前に実行する。`--bvh` は Role Profile を使い、入力 BVH を旧形式のスキンに適用する。v1 の `--rig-split` は glTF/GLB から Skeleton・SkinMesh・ClipBank を分割して出力し、`--retarget-clip` は BVH または glTF の動きを対象骨格へ変換して ClipBank を出力する。どちらも `BuildClipBankV1` の前に削減する。

回転キーの四元数は隣接するキーを同じ半球へ揃える。許容誤差内で定数のチャンネルは 1 キーにし、既定ローカル（v1 は作者 rest、旧形式は逆 bind から復元）に近いチャンネルは削除する。ただし root・root motion の関節と、同じ関節・種類に先行する重複チャンネルがある場合は削除しない。残りは RDP 型の分割で、補間との差が許容を超えるキーを残す。平滑化はしない。

関節ごとの基準長 `L_j` は bind 姿勢での下流関節と、その関節または子孫が影響する頂点までの最大距離とする。削減時の位置許容を `ε_pos = min(関節位置上限, 頂点位置上限) × 0.5 × 係数` とし、回転の許容は `δθ_j = ε_pos / L_j` と回転上限×係数の小さい方（ラジアン）を使う。`L_j = 0` のときは回転上限を使う。大域姿勢とスキニング後の頂点を元のキー時刻と隣接時刻の 1/3・1/2・2/3 で検証し、上限を超えた場合は係数を 0.7 倍して最大 4 回やり直す（初回を含め最大 5 回）。RDP で削減するチャンネルは先頭と終端のキーを残し、定数化・削除後もクリップ全体の終端と少なくとも 1 チャンネルを保持する。欠けたチャンネルは実行時に作者 rest（無ければ bind から求めた既定ローカル）に戻る。

上限は `--clip-max-vertex-error-m` が 0.002 m、`--clip-max-joint-error-m` が 0.001 m、`--clip-max-rotation-error-deg` が 0.1° で、いずれも仮の値である。実際の BVH を確認し、作者が決める。各値は有限の正数で指定し、上限や `--clip-report <path>` は `--clip-optimize` と併用する。

`--clip-report` の JSON は `version`・`succeeded`・`failure_reason`、`settings` の 3 上限、`totals` の前後のキー数・バイト数と削除・定数チャンネル数、`clips` ごとの名前・長さ・前後のキー数とバイト数・前後のチャンネル数・削除数・定数数・試行数・許容係数・最大頂点誤差・各関節の位置と回転の最大誤差を記録する。バイト数はキー数×20 の見積もりであり、出力ファイル全体のバイト数ではない。削減や検証、レポート書き出しに失敗した場合、cook は失敗し、CLI は理由を表示して 0 以外で終了する。削減の失敗時もレポートの書き出しを試みる。

`--clip-optimize` を省略すると削減段を通さず、無効状態の設定値は出力バイト列と `SourceHash` に影響しない。キャッシュの指紋と `SourceHash` に削減の設定が含まれるのは有効なときだけである。単体の `--format nvskel.v1.clips` rig cook（`RigClipBankCook.cpp`）と、管理付きキャッシュの旧形式の通常 model cook は最適化を受け付けない。`--retarget-clip` は新しい出力先への公開だけに対応し、同じ出力先での再 cook は既存どおり拒否される。
