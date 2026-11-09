# 異なる比率の骨格へのリターゲット（GR85）

## 位置づけ

GR84 の [回転の写し](SkeletalRotationRetarget.md) はロールで対応づけた関節に `W = C D C^T B` を適用する。GR85 では、rest の骨方向に基づく補正 C のねじれをロールごとの上向き方向で決め、脚長の比によるルート並進の拡縮と、誤差・移動距離の報告および上限判定を加える。設定は [SkeletalRoleProfile.md](SkeletalRoleProfile.md) の version 2 に置く。

対象は四足の `quadruped_v1` と人型の `humanoid_v1` である。profile の `foot_planting.enabled` を有効にすると、リターゲット後の足を平らな地面へ再接地し、足滑り・めり込み・IK の失敗コマ数を測って上限を判定する。実在の骨格（Mixamo・Rigify など）のプリセットは置いていない。人型の試験には合成の骨格と歩行を使う。

## ロール表

欄の番号は `SkeletalRoleProfile.h` / `SkeletalRoleProfile.cpp` の定義に従い、`Source`・`Target`・`RoleUpHints` の添字とビット集合のビット番号を表す。同じ番号でも語彙によって意味が異なる。「鎖を受ける」は、1 ロールに複数関節を近位から遠位へ列挙できるかを表し、脚の IK が使う鎖とは別である。脚の定義は `GetSkeletalRoleVocabularyInfo` の `Legs` を使う。

### quadruped_v1（23 ロール）

| ロール名 | 欄の番号 | 意味（どの関節か） | 鎖を受けるか | 脚の鎖での役割・群 |
|---|---:|---|---|---|
| `root` | 0 | 実階層のルート | いいえ | — |
| `pelvis` | 1 | 骨盤 | いいえ | — |
| `spine` | 2 | 背骨 | はい | — |
| `neck` | 3 | 首 | はい | — |
| `head` | 4 | 頭 | いいえ | — |
| `jaw` | 5 | 顎 | いいえ | — |
| `tail` | 6 | 尾 | はい | — |
| `front_L_clavicle` | 7 | 左前脚の鎖骨 | いいえ | 鎖の外 |
| `front_L_upper` | 8 | 左前脚の上部 | いいえ | Upper、群 0 |
| `front_L_lower` | 9 | 左前脚の下部 | いいえ | Lower、群 0 |
| `front_L_paw` | 10 | 左前脚の足先 | いいえ | 足先、群 0 |
| `front_R_clavicle` | 11 | 右前脚の鎖骨 | いいえ | 鎖の外 |
| `front_R_upper` | 12 | 右前脚の上部 | いいえ | Upper、群 0 |
| `front_R_lower` | 13 | 右前脚の下部 | いいえ | Lower、群 0 |
| `front_R_paw` | 14 | 右前脚の足先 | いいえ | 足先、群 0 |
| `hind_L_thigh` | 15 | 左後脚の大腿 | いいえ | Upper、群 1 |
| `hind_L_shin` | 16 | 左後脚の下腿 | いいえ | Lower、群 1 |
| `hind_L_hock` | 17 | 左後脚の飛節 | いいえ | Hock、群 1 |
| `hind_L_paw` | 18 | 左後脚の足先 | いいえ | 足先、群 1 |
| `hind_R_thigh` | 19 | 右後脚の大腿 | いいえ | Upper、群 1 |
| `hind_R_shin` | 20 | 右後脚の下腿 | いいえ | Lower、群 1 |
| `hind_R_hock` | 21 | 右後脚の飛節 | いいえ | Hock、群 1 |
| `hind_R_paw` | 22 | 右後脚の足先 | いいえ | 足先、群 1 |

前脚は 2 ボーン、後脚は 3 ボーン。`root` と `pelvis` は別の役割である。

### humanoid_v1（22 ロール）

| ロール名 | 欄の番号 | 意味（どの関節か） | 鎖を受けるか | 脚の鎖での役割・群 |
|---|---:|---|---|---|
| `hips` | 0 | 腰（実階層のルート・骨盤） | いいえ | — |
| `spine1` | 1 | 背骨の下部 | いいえ | — |
| `spine2` | 2 | 背骨の中部 | いいえ | — |
| `spine3` | 3 | 背骨の上部 | いいえ | — |
| `neck` | 4 | 首 | いいえ | — |
| `head` | 5 | 頭 | いいえ | — |
| `L_shoulder` | 6 | 左肩 | いいえ | — |
| `L_arm` | 7 | 左上腕 | いいえ | — |
| `L_forearm` | 8 | 左前腕 | いいえ | — |
| `L_hand` | 9 | 左手 | いいえ | — |
| `R_shoulder` | 10 | 右肩 | いいえ | — |
| `R_arm` | 11 | 右上腕 | いいえ | — |
| `R_forearm` | 12 | 右前腕 | いいえ | — |
| `R_hand` | 13 | 右手 | いいえ | — |
| `L_upleg` | 14 | 左大腿 | いいえ | Upper、群 1 |
| `L_leg` | 15 | 左下腿 | いいえ | Lower、群 1 |
| `L_foot` | 16 | 左足首・足先 | いいえ | 足先、群 1 |
| `L_toe` | 17 | 左つま先 | いいえ | 鎖の外 |
| `R_upleg` | 18 | 右大腿 | いいえ | Upper、群 1 |
| `R_leg` | 19 | 右下腿 | いいえ | Lower、群 1 |
| `R_foot` | 20 | 右足首・足先 | いいえ | 足先、群 1 |
| `R_toe` | 21 | 右つま先 | いいえ | 鎖の外 |

両脚とも 2 ボーンで群 1。足先は `L_foot` / `R_foot` であり、`L_toe` / `R_toe` は脚長・再接地の鎖に含まない。`hips` は欄 0 で root の役を兼ね、語彙の pelvis も同じ欄を指す。

## rest の向きとねじれ

`align_bones` は、元と対象それぞれの rest 姿勢から対応関節の骨方向を求め、元の方向を対象の方向へ合わせる最小回転 S を求め、ねじれの固定を加えて補正 C とする。方向には、途中の未写像関節を越えた直近の写像済み子孫への単位方向を使い、複数あれば合成する。子孫がなければ親から関節への方向を使う。有効な方向を求められなければ失敗する。

共通座標系の `up_hint` をそれぞれの骨方向に垂直な面へ投影し、最小回転後の投影が対象側の投影に合うよう骨軸まわりのねじれを加える。方向が正反対のときは hint から回転軸を求め、退化すれば固定の補助軸を使う。どちらかの投影が退化する場合はねじれを推測せず、最小回転だけを使う。`rest_pose.roles.<role>.up_hint` はそのロールだけ共通 hint を上書きする。`explicit` の C 指定や `match` ではロール別 hint を指定できない。

### 式：rest の補正

すべて正準列ベクトル空間で表す。元・対象の単位骨方向を $a,b$、正規化した共通またはロール別 `up_hint` を $u$ とする。$R(q)$ は XYZW 四元数を正規化して回転行列へ変換する関数、$S$ は最小回転である。$\operatorname{normalize}$ は四元数の単位化を表す。正反対でない場合は次式となる。

$$
S=R\!\left(\operatorname{normalize}(a\times b,\;1+a\cdot b)\right).
$$

$a\cdot b<-1+10^{-10}$ の半回転では、$u-a(u\cdot a)$ を単位化した軸を使う。退化時は、$|a_x|<0.7$ なら $e=(1,0,0)$、それ以外なら $e=(0,0,1)$ を選び、$\operatorname{normalize}(a\times e)$ を軸にする。

投影した上向き $u_s,u_t$ と、最小回転後の上向き $m$ を次で定義する（$\widehat v=v/\|v\|$）。$R_b(\theta)$ は軸 $b$ 周りの回転、$\theta$ はねじれ角である。

$$
u_s=\widehat{u-a(u\cdot a)},\quad u_t=\widehat{u-b(u\cdot b)},\quad m=\widehat{S u_s},
\qquad
\theta=\operatorname{atan2}\bigl(b\cdot(m\times u_t),\operatorname{clamp}(m\cdot u_t,-1,1)\bigr),
\qquad C=R_b(\theta)S.
$$

どちらかの投影を単位化できなければ $C=S$ とする。元の rest の world 回転を $A_0$、時刻 $t$ の world 回転を $A(t)$、対象の rest の world 回転を $B$ とすると、写す差分と理想回転は次のとおりである。

$$
D(t)=A(t)A_0^T,\qquad W(t)=C D(t)C^T B.
$$

局所回転は生成済みの親 world 回転の転置を左から掛けて求める。`SkeletalClipRetarget.cpp` の `AlignDirections` と `SkeletalRotationRetargetMath.h` の回転演算はこの列規約に従う。

## 脚長とルートの倍率

元の rest 位置は座標・単位変換後、対象の rest 位置は対象の座標系で測る。脚の鎖は次のとおりで、左右にそれぞれ適用する。

- 四足の前脚: `front_<L/R>_upper` → `front_<L/R>_lower` → `front_<L/R>_paw`。
- 四足の後脚: `hind_<L/R>_thigh` → `hind_<L/R>_shin` → `hind_<L/R>_hock` → `hind_<L/R>_paw`。
- 人型の脚: `L_upleg` → `L_leg` → `L_foot`、`R_upleg` → `R_leg` → `R_foot`。

前脚の `clavicle` は肩や胴の形の差を脚長に混ぜないため含めない。元と対象の両方にあるロールだけを鎖順に残し、残った隣同士の rest 位置間の距離を足す。途中のロールを省いた場合は、残った関節同士の直線距離を使う。2 関節以上ある脚だけを採り、利用できる脚長を群ごと（四足の前脚は群 0、四足の後脚と人型の脚は群 1）に平均する。両群があれば群の平均を等重みで求め、片群だけならその平均を使う。この平均を元と対象で別々に計算し、対象 ÷ 元を倍率にする。比率の平均ではない。使える脚がない、または元の平均脚長が小さすぎると失敗する。

`proportions.root_scale` の `"auto"` がこの比を使う。`leg_length_ratio` を指定すれば実測比の代わりにその正の値を使うが、脚長の測定そのものは省かない。倍率はルートの rest からの並進差分に掛かり、メタデータの地面オフセットと負でない公称速度にも単位倍率とともに掛かる。関節回転には掛からない。`"auto_height"` は別の方法で、元と対象それぞれの pelvis（人型は `hips`）から語彙の最低の足先までの高さの比を使う。

胴や尾の長さの違いは、ルート以外では対象の bind / rest の局所平行移動を使い、対応関節の回転を写して扱う。各部の長さを元に合わせる処理や、足先の位置が一致する保証ではない。

### 式：脚長と倍率

骨格を $X\in\{s,t\}$（元・対象）、rest 位置を $p^X_j$ とする。脚 $\ell$ の鎖から元・対象の両方にあるロールだけを残した関節列を $j_0,\ldots,j_k$ とし、$k\ge1$ の脚だけを使う。群 $g$ の有効な脚の集合を $E_g$、空でない群の集合を $G$ と定義する。

$$
L^X_\ell=\sum_{i=1}^{k}\|p^X_{j_i}-p^X_{j_{i-1}}\|,\qquad
\bar L^X_g=\frac{1}{|E_g|}\sum_{\ell\in E_g}L^X_\ell,\qquad
\bar L^X=\frac{1}{|G|}\sum_{g\in G}\bar L^X_g.
$$

`root_scale: "auto"` は $r=\bar L^t/\bar L^s$ を使い、`leg_length_ratio` があれば $r$ をその値に置き換える。報告の `leg_length_m.source` / `target` は上書き前の $\bar L^s,\bar L^t$ である。$G$ が空、または $\bar L^s<10^{-8}$ m なら失敗する。

元の座標・単位変換後のルート rest からの差分を $\Delta p_s(t)$、対象のルート rest 位置を $p^t_{r,0}$ とすると、倍率 $s_r$ の適用は次式である（この空間では root frame の定数原点を除く）。

$$
p^t_r(t)=p^t_{r,0}+s_r\Delta p_s(t),\qquad s_r=r\quad\text{（auto）}.
$$

`auto_height` では、骨盤の rest 高さを $y^X_p$、語彙の足先の rest 高さの最小値を $y^X_f$ として、$H^X=y^X_p-y^X_f$、$s_r=H^t/H^s$ とする。四足で pelvis の指定がなければルートを使う。元・対象それぞれに足先があり、両高さが $10^{-8}$ m 以上でなければ失敗する。数値の `root_scale` は指定値をそのまま使う。内部の設定に基礎倍率がある場合は、auto / auto_height の比をそれに掛ける（profile の自動指定では基礎倍率は 1）。

## 再接地

`foot_planting.ik.leg_roles` に選んだ足先ロールを列挙する。2 ボーンの脚（四足の前脚・人型の脚）は Upper・Lower・足先、3 ボーンの脚（四足の後脚）は Upper・Lower・Hock・足先の対象関節と、元の足先関節が必要である。元の時刻へ戻して FK した足先を座標・単位変換し、脚ごとの最低位置からの高さと鉛直速度を調べる。高さと速度がそれぞれのしきい値以下になった区間で、水平速度もしきい値未満なら接地とする。接地中は高さ・鉛直速度・水平速度の解除しきい値をそれぞれ 1.5 倍に広げ、短い区間は `min_stance_frames` で除く。しきい値は素材ごとに調整する仮の値である。

接地区間の開始時に対象骨格の FK で足先の X/Z を取り、Y を `ground.height` にした位置へ固定する。区間の前後 `blend_frames` コマは smoothstep で固定位置との間を補間する。2 ボーンの脚は Upper・Lower・足先の二骨 IK で解く。3 ボーンの脚は、写した回転のままの中足（hock から paw）のワールド方向を保ち、足先の目標から IK 前の中足のベクトルを引いた hock の目標へ thigh・shin の二骨 IK で解く。hock の局所回転は、そのワールド方向を保つように直す。二骨 IK には既存の `SolveTwoBoneIK` を使う。足の rest 姿勢での上向きを求め、接地に応じてワールドの上方向へ合わせる。

届かないときは必要量に応じて骨盤（ルート）を鉛直に下げる。下降の必要量に半径 `max(blend_frames, 1)` の最大値フィルタを掛けてから同じ半径の移動平均を掛け、各コマで必要量以上になるようにする。骨盤は全脚に共通で下げるので、下げたコマの遊脚の足先は地面より下がることがある。その量は全コマで測る報告の `foot_penetration_m` に出るため、`report.max_foot_penetration_m` の上限で cook を止められる。

ルートモーションを抽出した場合も、開始時の向きと累積ヨー・X/Z 移動を戻したワールド空間で接地を解く。固定位置と骨盤の補正はその軌跡を考慮する。元の時刻で速度を測るので、出力の時間倍率は検出速度に混ぜない。再接地は出力キーをほぼ等間隔として扱い、間隔が平均から 0.1% を超えて異なるキー列は `foot_planting_input` で失敗する。元のクリップが 16 fps の場合は接地の検出が外れやすいため、報告の `foot_contacts` を確認する。

地面は `ground.height` で表す平面である。地形への適応は実行時の GR14 が担当する。足の測定は再接地直後、後続のモーション整理やクリップ最適化の前の値である。

### 式：接地の検出と固定

コマ $i=0,\ldots,N-1$ の元の足先位置を $p_i$、元の時間での標本間隔を $\Delta t$ とする。$H$ は `height_threshold_m`（既定 0.03 m）、$V$ は `speed_threshold_m_s`（既定 0.15 m/s）である。$\eta_i$ は高さ・鉛直速度の窓の外で 1、窓の継続中は 1.5 とする。この窓は `DetectFootContactWindows` により次式で求める。

$$
h_i=p_{i,y}-\min_j p_{j,y},\quad
v^y_i=\frac{|h_{i+1}-h_i|}{\Delta t},\quad
h_i\le\eta_i H\ \land\ v^y_i\le\eta_i V\quad(0\le i<N-1).
$$

末尾標本は速度差分の終点となり、窓の終了番号は最大 $N-1$（含まない）である。この窓の中で、さらに水平速度を調べる。$a=\max(i-1,0)$、$b=\min(i+1,N-1)$、$\|v\|_{xz}=\sqrt{v_x^2+v_z^2}$ とし、$\xi_i$ を水平の接地区間の外で 1、継続中は 1.5 とすると、

$$
v^{xz}_i=\frac{\|p_b-p_a\|_{xz}}{(b-a)\Delta t}<\xi_i V.
$$

高さ・鉛直速度の窓ごとに水平の状態をリセットし、`min_stance_frames`（既定 3）未満の区間を除く。鉛直速度は前進差分、水平速度は上記の中心差分（端では片側差分）であり、等号を許すのは高さ・鉛直速度である。

接地区間を $[b,e)$、再接地前の対象足先を $q_i$、地面高さを $y_g$ とすると、固定位置は $f=(q_{b,x},y_g,q_{b,z})$。コマから区間内の最も近い標本までの距離を $d$ とし、最も近い区間を選ぶ（同距離なら前の区間）。$B_f$ を `blend_frames`（既定 3）として、目標 $g_i$ と重み $w_i$ を次で定める。

$$
x=1-\frac{d}{B_f+1},\quad
w_i=\begin{cases}
1 & d=0,\\
\min(x^2(3-2x),\operatorname{nextafter}(1,0)) & 0<d\le B_f,\\
0 & d>B_f\text{ または区間なし},
\end{cases}
\qquad g_i=q_i+w_i(f-q_i)\quad(w_i>0).
$$

重み 0 では IK を適用しない。区間外の重みを厳密に 1 未満にすることで、固定中だけを測る浮きと区間前後の補間を区別する。

### 式：二骨 IK と骨盤の下降

IK 前の Upper・Lower・終端位置を $u,m,e$、その時点の足先位置を $q^{IK}$、目標を $t$ とする。これらの位置は骨盤を下げた姿勢で取り直す。2 ボーンでは終端は足先で $t=g_i$。3 ボーンでは終端は Hock で、中足ベクトル $v=q^{IK}-e$ を用いて $t=g_i-v$ とする。Hock の world 回転を IK 前の値に保つよう局所回転を戻すため、中足の world 方向を保つ。

`SolveTwoBoneIK` には `MaxStretch=1`、`SoftLimitWidth=0` を渡す。$l_1=\|m-u\|$、$l_2=\|e-m\|$、$T=l_1+l_2$ とし、非退化時の目標方向を $a=\widehat{t-u}$、その垂直面内の単位 pole を $n$ とすると、解の位置は次式に相当する。

$$
r=\operatorname{clamp}\bigl(\|t-u\|,\min(|l_1-l_2|+10^{-6}T,T),T\bigr),\quad
c=\operatorname{clamp}\!\left(\frac{l_1^2+(r-l_2)(r+l_2)}{2l_1r},-1,1\right),
$$
$$
m'=u+l_1\bigl(ca+\sqrt{\max(0,1-c^2)}n\bigr),\qquad e'=u+ra.
$$

伸び切り $r\ge T$ では $c=1$ とする。pole は現在の膝の曲がりから取り、退化時には前コマの pole、またはルート親の前方を fallback に渡す。ソルバーはさらに旧骨方向や垂直軸で退化を扱う。float に丸めた解の骨方向から求めた最小回転を、更新前の Upper / Lower の world 回転にそれぞれ掛けて局所回転へ戻す。足先では rest の上向きから現在の world 上向き $u_f$ を求め、最小回転 $Q(u_f\to(0,1,0))$ を単位四元数から重み $w_i$ で slerp して足先の world 回転に掛ける。

骨盤の下降には rest の二骨の可到達長 $R=0.999(l^{rest}_1+l^{rest}_2)$ を使う。下降量の計算では、$u$ と中足ベクトルを骨盤の下降前の姿勢から取り、同じ式で目標 $t$ を求める。$o=u-t$、$\rho=\|o\|_{xz}$ として、重みが正の各脚の必要量と、そのコマの必要量 $n_i$ は次のとおりである（目標は補間済みなので重みを再度掛けない）。

$$
d_{i,\ell}=\begin{cases}
o_y-R\sqrt{\max(0,1-(\rho/R)^2)} & \rho<R,\\
o_y & \rho\ge R,
\end{cases}
\qquad n_i=\max(0,\max_{\ell:w_{i,\ell}>0}d_{i,\ell}).
$$

有効な脚がなければ $n_i=0$。半径 $k=\min(\max(B_f,1),N-1)$、端で切り詰めた窓 $I_i=\{j:0\le j<N,|j-i|\le k\}$ を使う最大値フィルタ・移動平均と、最終下降量 $\delta_i$ は次式である。

$$
M_i=\max_{j\in I_i}n_j,\qquad
\delta_i=\max\!\left(n_i,\frac{1}{|I_i|}\sum_{j\in I_i}M_j\right).
$$

ワールドの $(0,-\delta_i,0)$ をルート親の逆回転と frame の倍率で局所空間へ戻してルート並進に加え、その後に各脚を解く。

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

報告の欄は次のとおりである。`clips[]` 以下はクリップごとの値で、足の測定値は選択した全脚の最大値（IK 失敗はコマ数）である。

| 欄 | 単位 | 意味 | 未計測のとき |
|---|---|---|---|
| `version` | — | 報告形式の版。現在 1 | 常に出力 |
| `target_skeleton_id` | — | 対象 SkeletonId の 16 桁小文字 16 進文字列 | 常に出力 |
| `limits.max_joint_error_deg` | 度 | 関節回転誤差の上限 | 未指定なら `null` |
| `limits.max_foot_slide_m` | m | 再接地後の足滑りの上限 | 未指定なら `null` |
| `limits.max_foot_penetration_m` | m | めり込みの上限 | 未指定なら `null` |
| `limits.max_ik_failed_frames` | コマ | IK 失敗コマ数の上限 | 未指定なら `null` |
| `result.status` | — | 上限判定の結果。全指定上限内なら `within` | 下表の未計測時のコード |
| `result.clip` | — | 最初に判定に失敗したクリップ名 | `within` なら `null` |
| `result.value` / `result.limit` | 該当上限の単位 | 失敗した測定値と上限。`within` なら両方 0 | 未計測時の `value` は 0（測定値ではない）、`limit` は指定上限 |
| `clips[].name` | — | クリップ名 | 常に出力 |
| `clips[].rotation_retarget_error_deg.max` / `mean` | 度 | 全出力キー時刻・対応関節の回転角誤差の最大値 / 算術平均 | 親項目 `rotation_retarget_error_deg` が `null` |
| `clips[].root_scale` | 倍 | 実際に適用したルート並進倍率 | 常に数値 |
| `clips[].root_motion_distance_m.source` / `target` | m | 元 / 対象の標本間の 3 次元ルート移動距離の和 | 常に数値。未計測を示す `null` はない |
| `clips[].leg_length_m.source` / `target` | m | 元 / 対象の群平均を平均した脚長 | 両値 0 なら親項目 `leg_length_m` が `null`（auto 以外では測らない） |
| `clips[].foot_slide_m.before` / `after` | m | 各接地区間の開始足先からの水平距離の最大値。再接地前 / 後 | 親項目 `foot_slide_m` が `null` |
| `clips[].foot_penetration_m` | m | 全コマの地面下への距離の最大値（下限 0） | `null` |
| `clips[].foot_float_m` | m | 重み 1 の固定区間の地面からの浮きの最大値（下限 0） | `null` |
| `clips[].ik_adjustment_deg` | 度 | 二骨 IK の Upper / Lower の回転差分角の最大値。足先の上向き補正や Hock の補正角は含まない | `null` |
| `clips[].ik_failed_frames` | コマ | 重みが正の目標と足先の距離が 1 mm を超えたコマ数。複数脚で失敗しても 1 コマ | `null` |
| `clips[].pelvis_drop_m` | m | 骨盤（ルート）の最大下降量 | `null` |
| `clips[].foot_contacts` | — | 選択した脚ごとの接地区間の配列 | `null` |
| `clips[].foot_contacts[].leg` | — | 語彙の足先ロール名。四足は `front_L_paw` など、人型は `L_foot` / `R_foot` | 親配列が `null` |
| `clips[].foot_contacts[].frames` | コマ番号 | `[begin, end]` の配列。開始を含み終了を含まない区間 | 計測済みで接地なしなら空配列 |
| `clips[].unmapped_joints.source` / `target` | — | 対応がない元 / 対象の関節名の配列 | 常に配列。該当なしなら空配列 |

再接地が無効なときは足の測定項目と `foot_contacts` が `null` になる。有効なら、接地区間がなくても足の項目は数値になりうる。接地の有無は `foot_contacts[].frames` も確認する。

| profile の鍵 | 比べる値（`clips[]` 以下） | 判定の順 | 超えたときの `result.status` |
|---|---|---:|---|
| `report.max_foot_slide_m` | `foot_slide_m.after` | 1 | `foot_slide_exceeded` |
| `report.max_foot_penetration_m` | `foot_penetration_m` | 2 | `foot_penetration_exceeded` |
| `report.max_ik_failed_frames` | `ik_failed_frames` | 3 | `ik_failed_frames_exceeded` |
| `report.max_joint_error_deg` | `rotation_retarget_error_deg.max` | 4 | `joint_error_exceeded` |

`CheckRetargetReportLimits` は未指定の上限を飛ばし、表の上限ごとに全クリップを配列順で調べ、最初の失敗を返す。等しい値は上限内である。指定した足の上限の測定がなければ `foot_metrics_unmeasured`、関節回転誤差の測定がないか NaN なら `joint_error_unmeasured` となり、超過と同様に cook が失敗する。浮き・IK 調整角・骨盤下降量には profile の上限設定はない。

`AssetCook --retarget-report <path>` は指定先へ JSON を書く。`--retarget-clip` と asset-set の経路はいずれも version 2 のロール別 hint・脚長倍率・再接地・上限判定を適用する。上限を超えた場合は報告を書いた後、`retarget_report_limit: <status> clip=<名前> value=<値> limit=<上限>` のエラーを返し、CLI は終了コード 1 で終了する。報告パスが入力（元クリップ・対象骨格・profile・依存ファイルなど）または出力と同じ場合は `retarget_report_path` で拒否する。報告 JSON の生成・保存に失敗した場合は `retarget_report_write` を返す。

## 骨格の同一性

既存の `SkeletonId` は関節名順に正準化した名前と親関係のトポロジーのハッシュである。rest 姿勢は別の rest ハッシュと比較で扱う。トポロジーと rest の同一性・差の確認に既存の仕組みを使い、リターゲット専用の互換 ID は新設しない。役割の名前が一致するだけでは同一骨格とみなさない。
