# ロール経由の骨格 Profile（GR84 / GR85）

## 設定と語彙

GR84 のファイル入口は [SkeletalRoleFileCli.md](SkeletalRoleFileCli.md)、ClipBank 向けのリターゲットは [Retarget.md](Retarget.md) を参照する。

自己完結の UTF-8 JSON を使う。先頭の UTF-8 BOM は許容する。解析器が受ける version は整数 1 または 2、vocabulary は `quadruped_v1` または `humanoid_v1`。未知 field / role、重複 key、型違い、未対応の自動指定は拒否する。以下の例は GR84 の version 1 で、version 2 の追加設定は後述する。

`quadruped_v1` の語彙と正準展開順は次の23role。rootは実階層rootを指す技術的なroleであり、pelvisと同じという仮定ではない。

1. root、pelvis
2. spine、neck、head、jaw、tail
3. front_L_clavicle、front_L_upper、front_L_lower、front_L_paw
4. front_R_clavicle、front_R_upper、front_R_lower、front_R_paw
5. hind_L_thigh、hind_L_shin、hind_L_hock、hind_L_paw
6. hind_R_thigh、hind_R_shin、hind_R_hock、hind_R_paw

`quadruped_v1` では spine/neck/tail だけが chain。それ以外は単一 role。root 以外をゲームの必須集合にしない。

`humanoid_v1` の正準展開順は次の 22 ロールで、鎖（複数関節）を受けるロールはない。`hips` は欄 0 の必須ロールで、実階層の root と pelvis の役を兼ねる。

1. `hips`、`spine1`、`spine2`、`spine3`、`neck`、`head`
2. `L_shoulder`、`L_arm`、`L_forearm`、`L_hand`
3. `R_shoulder`、`R_arm`、`R_forearm`、`R_hand`
4. `L_upleg`、`L_leg`、`L_foot`、`L_toe`
5. `R_upleg`、`R_leg`、`R_foot`、`R_toe`

欄番号・関節の意味・脚の鎖は [Retarget.md のロール表](Retarget.md#ロール表) を参照する。綴りと L/R の大小文字は固定し、alias や Unicode 正規化は行わない。選択した語彙にないロール名（別語彙の名前を含む）は `source_roles`・`target_roles`・`required_roles`・`rest_pose.roles`・`foot_planting.ik.leg_roles` で `UnknownRole` になる。`neck`・`head` のように両語彙にある名前は、選択した語彙の欄として解釈する。

`processing.exclude_roles` も選択した語彙で解釈する。未知の名前は、四足では従来どおり `InvalidSettings`、人型では `UnknownRole`（フィールド `processing.exclude_roles`）になる。重複や文字列以外の要素は両語彙とも `InvalidSettings` で拒否する。

```json
{
  "version": 1,
  "vocabulary": "quadruped_v1",
  "axes": {"up": "+Y", "forward": "+Z", "handedness": "right"},
  "units": {"position_scale": 1},
  "position_convention": "additive",
  "time": {"mode": "header_frame_time"},
  "source_roles": {"root": ["Source"]},
  "target_roles": {
    "root": [{"joint": "Root", "C": [1,0,0,0,1,0,0,0,1]}]
  }
}
```

source_rolesはroleごとの非空joint名配列。target_rolesはjointと、明示補正時のCを持つ非空要素配列であり、ここにあるroleだけを写像する。両表のルート役（四足は `root`、人型は `hips`）は必須・1要素。単一roleは1要素、chainは利用者が近位から遠位へ明示し、写像する両側の長さは同じでなければならない。chainの解剖学的正しさを名前から推測しない。

required_rolesは任意の、重複しないrole名配列。指定roleはsourceとtargetの双方に必要。sourceだけのroleも記載名が実BVHに存在することを検査し、SourceOnlyRolesへ所有報告する。source-only aliasは活動pairの再利用とは区別する。活動pairはsource再利用とtarget重複を拒否する。root/pelvisが同じ実jointなら両方を活動pairにしない。

rest_pose が省略された場合や mode が explicit の場合、Cはcanonical列ベクトル空間の補正3×3を行順の9有限doubleで明示する。恒等も省略しない。既存のproper rotation許容検査へ通し、式W=C D C^T Bで使う。mode が match または align_bones の場合は C の指定を拒否し、rest から補正を扱う。元BVH骨方向との無条件一致を意味しない。

axesはup/forwardに±X/±Y/±Z、handednessにright/leftを指定。平行なup/forwardは拒否。position_scaleは正有限、position_conventionはadditive/absolute。time.modeはheader_frame_time（source_fps禁止）かoverride_fps（正有限source_fps必須）で、推測fpsを置かない。heading保持・回転のみ・全sample保持・source再利用RejectはGR84の型付きadapterの固定規約。

clip名とAdd/Replaceは別の型付き呼出要求。Profileで暗黙のclip選択やrenameをしない。

## version 2

version 1 の profile は従来どおり読める。version 1 では `rest_pose.roles`・`proportions`・`report`・`ground`・`foot_planting` は未知の鍵として拒否される。version 2 では `proportions` と、その中の `root_scale` が必須である。version 1 の `version` だけを 2 にしても受理されない。最上位の `root_scale` は version 1 だけで受け付け、version 2 では拒否する。ほかの語彙・必須項目も引き続き指定する。

- `rest_pose.roles.<role>.up_hint`: `align_bones` 用のロール別上向き方向。3 個の有限数で表す長さ 1e-10 以上のベクトルを正規化して使い、未指定のロールには共通の `rest_pose.up_hint`（既定 `[0, 1, 0]`）を使う。ロール名は上記の語彙に限り、各要素は `up_hint` を持つオブジェクトとする。`rest_pose.roles` は `mode` が `align_bones` 以外なら、空でも拒否する。
- `proportions.root_scale`: 正の有限数、`"auto"` または `"auto_height"`。数値はルート並進の倍率、`"auto"` は対応する脚の長さから求める倍率、`"auto_height"` は pelvis（人型は `hips`）と語彙の足先の rest 高さから求める倍率を使う。`"auto_height"` は従来の version 1 の最上位 `root_scale` でも使える。
- `proportions.leg_length_ratio`: 正の有限数による脚長比の上書き。`root_scale` が `"auto"` の場合だけ指定でき、数値または `"auto_height"` と組み合わせると拒否する。上書き時にも両骨格の脚長は測定する。
- `ground`: 平らな地面の設定オブジェクト。許す鍵は必須の `height` だけで、有限数によるワールド高さ（Y、メートル）を指定する。`ground` を省略したときの高さは 0。
- `foot_planting`: 再接地の設定オブジェクト。`enabled` は必須の真偽値で、`foot_planting` を省略したときは false。任意の `height_threshold_m` は足ごとの最低位置からの接地高さのしきい値（既定 0.03 メートル）、`speed_threshold_m_s` は元の時間で測る足の鉛直・水平速度のしきい値（既定 0.15 メートル毎秒）で、両方とも正の有限数。任意の `min_stance_frames` は接地区間の最短フレーム数（整数 1〜65535、既定 3）、`blend_frames` は接地区間の前後で補間するフレーム数（整数 0〜65535、既定 3）。
- `foot_planting.ik`: `enabled` が true なら必須のオブジェクト。許す鍵は必須の `leg_roles` だけで、選択した語彙の足先から選ぶ空でない、重複しない配列とする。四足は `front_L_paw`・`front_R_paw`・`hind_L_paw`・`hind_R_paw`、人型は `L_foot`・`R_foot` だけを受ける。語彙にない名前は `UnknownRole`、語彙にはあるが足先ではない名前（人型の `L_toe`・`R_toe` など）は `InvalidSettings`、重複は `DuplicateField` で拒否する。true のときは、選択した前脚の `front_<L/R>_upper`・`_lower`・`_paw`、後脚の `hind_<L/R>_thigh`・`_shin`・`_hock`・`_paw` がすべて `target_roles` にあり、選択した `_paw` が `source_roles` にあることが必要。人型では選択した側の `L_upleg`・`L_leg`・`L_foot`、または `R_upleg`・`R_leg`・`R_foot` がすべて `target_roles` にあり、選択した `L_foot` / `R_foot` が `source_roles` に必要で、つま先は鎖に含めない。不足時は `InvalidSettings`、フィールド `foot_planting.ik.leg_roles` で拒否する。false のときに `ik` を指定しても、その構造とロール名の検証は行う。
- `report.max_joint_error_deg`: 関節回転誤差の上限。有限数で 0 度より大きく 180 度以下。
- `report.max_foot_slide_m`、`report.max_foot_penetration_m`: 足滑りとめり込みの上限。いずれも 0 以上の有限数。`foot_planting.enabled` が true なら再接地の結果を測定して上限と比較し、false なら未計測のため上限判定は失敗する。
- `report.max_ik_failed_frames`: IK 失敗フレーム数の上限。整数 0〜2147483647。省略時は上限を設定しない。

`ground`・`foot_planting`・`ik`・`report` の未知の鍵、重複鍵、型違い、および各値の範囲外は拒否する。整数の設定には小数表記や指数表記を受け付けない。

`report` とその各鍵は任意である。再接地の処理と比率・誤差・上限の意味は [Retarget.md](Retarget.md) を参照する。次の 1.3 と 0.1 度は説明用の仮の数値である。

```json
{
  "version": 2,
  "vocabulary": "quadruped_v1",
  "axes": {"up": "+Y", "forward": "+Z", "handedness": "right"},
  "units": {"position_scale": 1},
  "position_convention": "additive",
  "time": {"mode": "header_frame_time"},
  "source_roles": {
    "root": ["Root"], "front_L_upper": ["Upper"],
    "front_L_lower": ["Lower"], "front_L_paw": ["Paw"]
  },
  "target_roles": {
    "root": [{"joint": "Root"}], "front_L_upper": [{"joint": "Upper"}],
    "front_L_lower": [{"joint": "Lower"}], "front_L_paw": [{"joint": "Paw"}]
  },
  "rest_pose": {
    "mode": "align_bones", "up_hint": [0, 1, 0],
    "roles": {"front_L_upper": {"up_hint": [0, 0, 1]}}
  },
  "proportions": {"root_scale": "auto", "leg_length_ratio": 1.3},
  "report": {"max_joint_error_deg": 0.1}
}
```

解析器が設定を受理することと、各 cook 入口がその設定を適用することは別である。従来の GR84 の `--bvh` / `--role-profile` 入口はこの比率補正・上限判定を接続していない。ClipBank の `--retarget-clip` と asset-set の経路では version 2 の比率・ロール別上向き方向・再接地・報告と上限判定を適用する。CLI 契約は [Retarget.md](Retarget.md) に示す。

## 所有、制限、hash

parse結果、具体名、C、正準順のExpandedRoles、非対象source role、元Profile byte数/hashを所有する。呼出成功時だけ以前の結果を置換し、失敗/確保例外では保持する。
JSON DOM確保前に入力byte数、構造深さ、構造token数（引用符外のopen bracket/colon/comma）を制限する。既定は1MiB、深さ16（hard上限64）、32768token。source要素/展開数は既定各1024、単名4096bytes、名前合計1MiB。制限は全処理RSSの上限ではない。
source-only検査の所有BVH decodeをbridge前に破棄する。現在は検証と実cookのためBVHを2回decodeする。型付きbridgeの既存予算も別途適用し、線形時間/無確保とは称さない。

既存BVH typed hashに、Profile parser版、語彙版、長さ付きraw Profile bytes、Profile limitsを追加する。JSON object順の違いは正準展開後のNVSKEL bytesを変えないが、元入力identityのhashは変える。旧Profile無し・BVH無し経路は変更しない。
GR84 の型付き入口は毎回 cook する。file / CLI と依存ファイルの扱いは [SkeletalRoleFileCli.md](SkeletalRoleFileCli.md) を参照する。

## 受入れ境界

合成Profileから既存NVSKEL/package/AssetSystem/実Resource/Samplerへ接続するCPU契約を対象にする。異なる実joint名、正準role順、明示非可換C、未写像targetのbind保持、拒否と所有、入力identityを確認する。
語彙IDはゲームの骨格採用や骨格共有の決定ではない。上記 GR84 入口の検証範囲と、後続の ClipBank 向けリターゲットの機能・検証範囲は区別する。異長chainの分配、IK、実Blender/GPU品質はここで保証しない。
