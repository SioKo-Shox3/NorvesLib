# 異なる比率の骨格へのリターゲット（GR85）

## 位置づけ

GR84 の [回転の写し](SkeletalRotationRetarget.md) はロールで対応づけた関節に `W = C D C^T B` を適用する。GR85 では、rest の骨方向に基づく補正 C のねじれをロールごとの上向き方向で決め、脚長の比によるルート並進の拡縮と、誤差・移動距離の報告および上限判定を加える。設定は [SkeletalRoleProfile.md](SkeletalRoleProfile.md) の version 2 に置く。

対象は四足の `quadruped_v1` である。profile の `foot_planting.enabled` を有効にすると、リターゲット後の足を平らな地面へ再接地し、足滑り・めり込み・IK の失敗コマ数を測って上限を判定する。人型には対応していない。

## rest の向きとねじれ

`align_bones` は、元と対象それぞれの rest 姿勢から対応関節の骨方向を求め、元の方向を対象の方向へ合わせる最小回転を C とする。方向には、途中の未写像関節を越えた直近の写像済み子孫への単位方向を使い、複数あれば合成する。子孫がなければ親から関節への方向を使う。有効な方向を求められなければ失敗する。

共通座標系の `up_hint` をそれぞれの骨方向に垂直な面へ投影し、最小回転後の投影が対象側の投影に合うよう骨軸まわりのねじれを加える。方向が正反対のときは hint から回転軸を求め、退化すれば固定の補助軸を使う。どちらかの投影が退化する場合はねじれを推測せず、最小回転だけを使う。`rest_pose.roles.<role>.up_hint` はそのロールだけ共通 hint を上書きする。`explicit` の C 指定や `match` ではロール別 hint を指定できない。

## 脚長とルートの倍率

元の rest 位置は座標・単位変換後、対象の rest 位置は対象の座標系で測る。脚の鎖は次のとおりで、左右にそれぞれ適用する。

- 前脚: `front_<L/R>_upper` → `front_<L/R>_lower` → `front_<L/R>_paw`。
- 後脚: `hind_<L/R>_thigh` → `hind_<L/R>_shin` → `hind_<L/R>_hock` → `hind_<L/R>_paw`。

前脚の `clavicle` は肩や胴の形の差を脚長に混ぜないため含めない。元と対象の両方にあるロールだけを鎖順に残し、残った隣同士の rest 位置間の距離を足す。途中のロールを省いた場合は、残った関節同士の直線距離を使う。2 関節以上ある脚だけを採り、利用できる左右の脚長を前脚と後脚それぞれで平均する。両群があれば前後の平均を等重みで求め、片群だけならその平均を使う。この平均を元と対象で別々に計算し、対象 ÷ 元を倍率にする。比率の平均ではない。使える脚がない、または元の平均脚長が小さすぎると失敗する。

`proportions.root_scale` の `"auto"` がこの比を使う。`leg_length_ratio` を指定すれば実測比の代わりにその正の値を使うが、脚長の測定そのものは省かない。倍率はルートの rest からの並進差分に掛かり、メタデータの地面オフセットと負でない公称速度にも単位倍率とともに掛かる。関節回転には掛からない。`"auto_height"` は別の方法で、元と対象それぞれの pelvis から最低の paw までの高さの比を使う。

胴や尾の長さの違いは、ルート以外では対象の bind / rest の局所平行移動を使い、対応関節の回転を写して扱う。各部の長さを元に合わせる処理や、足先の位置が一致する保証ではない。

## 再接地

`foot_planting.ik.leg_roles` に選んだ足先ロールを列挙する。前脚は upper・lower・paw、後脚は thigh・shin・hock・paw の対象関節と、元の paw 関節が必要である。元の時刻へ戻して FK した足先を座標・単位変換し、脚ごとの最低位置からの高さと鉛直速度を調べる。高さと速度がそれぞれのしきい値以下になった区間で、水平速度もしきい値未満なら接地とする。接地中は高さ・鉛直速度・水平速度の解除しきい値をそれぞれ 1.5 倍に広げ、短い区間は `min_stance_frames` で除く。しきい値は素材ごとに調整する仮の値である。

接地区間の開始時に対象骨格の FK で足先の X/Z を取り、Y を `ground.height` にした位置へ固定する。区間の前後 `blend_frames` コマは smoothstep で固定位置との間を補間する。前脚は upper・lower・paw の二骨 IK で解く。後脚は、写した回転のままの中足（hock から paw）のワールド方向を保ち、足先の目標から IK 前の中足のベクトルを引いた hock の目標へ thigh・shin の二骨 IK で解く。hock の局所回転は、そのワールド方向を保つように直す。二骨 IK には既存の `SolveTwoBoneIK` を使う。足の rest 姿勢での上向きを求め、接地に応じてワールドの上方向へ合わせる。

届かないときは必要量に応じて骨盤（ルート）を鉛直に下げる。下降の必要量に半径 `max(blend_frames, 1)` の最大値フィルタを掛けてから同じ半径の移動平均を掛け、各コマで必要量以上になるようにする。骨盤は全脚に共通で下げるので、下げたコマの遊脚の足先は地面より下がることがある。その量は全コマで測る報告の `foot_penetration_m` に出るため、`report.max_foot_penetration_m` の上限で cook を止められる。

ルートモーションを抽出した場合も、開始時の向きと累積ヨー・X/Z 移動を戻したワールド空間で接地を解く。固定位置と骨盤の補正はその軌跡を考慮する。元の時刻で速度を測るので、出力の時間倍率は検出速度に混ぜない。再接地は出力キーをほぼ等間隔として扱い、間隔が平均から 0.1% を超えて異なるキー列は `foot_planting_input` で失敗する。元のクリップが 16 fps の場合は接地の検出が外れやすいため、報告の `foot_contacts` を確認する。

地面は `ground.height` で表す平面である。地形への適応は実行時の GR14 が担当する。足の測定は再接地直後、後続のモーション整理やクリップ最適化の前の値である。

## 誤差と距離

出力した全チャンネルのキー時刻を集め、重複を除いて測る。各時刻を元のクリップの選択区間・時間倍率に対応づけ、対応関節ごとに元の姿勢から求めた理想 world 回転 `W = C D C^T B` と、出力をサンプルして親から組み立てた world 回転 `W_out` の回転角を比較する。`rotation_retarget_error_deg.max` は全時刻・全対応関節の最大角、`mean` はそれらの角度の算術平均である。root motion を抽出した場合は、抽出した向きを戻して比較する。

この値は出力キー時刻での値であり、キー間の連続曲線全体の最大誤差ではない。平滑化・周期平均・ループ継ぎ目の調整などで出力姿勢を変えた場合、理想回転との差が出るのは正しい。計測はリターゲット内の処理後、再接地および後続のモーション整理・クリップ最適化の前に行うため、最終 ClipBank の誤差そのものを保証しない。

`root_motion_distance_m` は同じ時刻列におけるルート位置の隣接差分の 3 次元ユークリッド長を合計したもの。元は座標・単位変換後、対象は出力した位置で測る。root motion 抽出時は累積移動を復元して足す。始点と終点の直線距離や連続軌跡の厳密な長さではない。`unmapped_joints` は、ロールによる対応が作られなかった元・対象それぞれの関節名である。未写像の対象関節は bind / rest の局所姿勢を保ち、親の動きには従う。

## 報告と上限

報告 JSON の `version` は profile の version とは別の報告形式の版で、現在は 1。`target_skeleton_id` は対象の SkeletonId を 16 桁の小文字 16 進文字列で表す。`limits` は各上限、`result` は判定、`clips` はクリップ別の回転誤差・倍率・ルート距離・脚長・足の測定値・未写像関節を持つ。しきい値と測定値の数値は説明用の仮の値である。

```json
{
  "version": 1,
  "target_skeleton_id": "0123456789abcdef",
  "limits": {
    "max_joint_error_deg": 0.1,
    "max_foot_slide_m": 0.05,
    "max_foot_penetration_m": 0.02,
    "max_ik_failed_frames": 2
  },
  "result": {"status": "within", "clip": null, "value": 0, "limit": 0},
  "clips": [
    {
      "name": "Walk",
      "rotation_retarget_error_deg": {"max": 0.03, "mean": 0.01},
      "root_scale": 1.3,
      "root_motion_distance_m": {"source": 2.0, "target": 2.6},
      "leg_length_m": {"source": 1.0, "target": 1.3},
      "foot_slide_m": {"before": 0.08, "after": 0.02},
      "foot_penetration_m": 0.01,
      "foot_float_m": 0.01,
      "ik_adjustment_deg": 12.0,
      "ik_failed_frames": 1,
      "pelvis_drop_m": 0.03,
      "foot_contacts": [
        {"leg": "front_L_paw", "frames": [[0, 8], [20, 30]]},
        {"leg": "front_R_paw", "frames": [[10, 19]]}
      ],
      "unmapped_joints": {"source": ["SourceTail"], "target": ["TargetTail"]}
    }
  ]
}
```

未指定の上限は `null`、未計測の回転誤差と脚長はそれぞれの項目が `null` になる。`root_scale` と `root_motion_distance_m` は常に数値で、距離の未計測を示す `null` はない。再接地が無効なときは足の測定項目と `foot_contacts` が `null` になる。再接地が有効なら `foot_contacts` は脚別の配列で、`frames` の各 `[begin, end]` は開始を含み終了を含まないコマ番号の区間を表す。

足滑りは接地区間の最初の足先からの水平距離の最大値で、`before` と `after` は再接地の前後を表す。めり込みは全コマの地面下への最大距離、浮きは固定区間の地面からの最大距離である。`ik_adjustment_deg` は脚の関節調整角の最大値、`ik_failed_frames` は有効な目標位置とのずれが 1 mm を超えるコマ数、`pelvis_drop_m` は骨盤の最大下降量である。

`result.status` は `within`、`foot_slide_exceeded`、`foot_penetration_exceeded`、`ik_failed_frames_exceeded`、`foot_metrics_unmeasured`、`joint_error_exceeded`、`joint_error_unmeasured` のいずれか。失敗時の `result.clip` は該当クリップ名である。`result.value` と `result.limit` は数値で、`within` ではともに 0、未計測時の `value` の 0 は測定値を意味しない。

指定された上限は、再接地後の足滑り、めり込み、IK の失敗コマ数、関節誤差の順に判定し、最初の失敗を報告する。どの上限も等しい値は上限内で、超過または対象の測定値がない場合に cook は失敗する。

`AssetCook --retarget-report <path>` は指定先へ JSON を書く。`--retarget-clip` と asset-set の経路はいずれも version 2 のロール別 hint・脚長倍率・再接地・上限判定を適用する。上限を超えた場合は報告を書いた後、`retarget_report_limit: <status> clip=<名前> value=<値> limit=<上限>` のエラーを返し、CLI は終了コード 1 で終了する。報告パスが入力（元クリップ・対象骨格・profile・依存ファイルなど）または出力と同じ場合は `retarget_report_path` で拒否する。報告 JSON の生成・保存に失敗した場合は `retarget_report_write` を返す。

## 骨格の同一性

既存の `SkeletonId` は関節名順に正準化した名前と親関係のトポロジーのハッシュである。rest 姿勢は別の rest ハッシュと比較で扱う。トポロジーと rest の同一性・差の確認に既存の仕組みを使い、リターゲット専用の互換 ID は新設しない。役割の名前が一致するだけでは同一骨格とみなさない。
