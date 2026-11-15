# sample保持のBVH回転clip取り込み（GR84）

## 有限の内部入口

ImportSkeletalBvhRotationClip は、decoded BVH・現在のtarget joints/mesh・解決済みpairと明示Cから、所有SkeletalAnimationClipと所有reportを作る。
全frameを保持し、mapped targetごとに1本のLINEAR回転trackを出す。一定のtrackも落とさない。
heading・targetのbind local平行移動/scaleを保持し、未写像targetにはchannelを出さない。
外部のrole/BoneMap schema、C生成、rootMotion、output-fps resampling、loop、CLI、cooker接続、Stage Bはこの入口に含めない。
明示pairは将来のrole展開の出力であり、旧direct-pair JSONを外部契約として復活させるものではない。

## 元sourceと位置を計算しないFK

BvhRotationSourcePlan は元document全体を一度検査する。
- 単一ROOT・parent-before-child・深さ、channel範囲の連続性/enum/重複、frame幅/値数、正で有限のframe timeと元duration
- BVH tokenとして非空で厳密UTF8、空白/control/引用符/braces/BOM無し、一意な名前
- 全frameの全値、OFFSET、宣言されたEnd Siteが有限
- rotationは宣言順のXYZ一組または無し。位置のpartial/並びは回転評価から除外する

planは親/rotation channel位置を所有し、元Valuesを借用する。使用中は元Valuesの生存と不変をcallerが守る。
move後の元planは未設定になる。成功時だけplanを置換し、失敗/確保例外では以前のplanを保つ。
評価は確保済みworld配列へ、既存AxisRotation/Multiplyと同じ順序・算術で回転だけを書き、位置を一切演算しない。
位置のみのsourceもidentity回転として扱う。巨大だが有限のOFFSETや無視する位置が、回転をoverflowさせることはない。
評価scratchは失敗時に部分更新され得る。元Valuesとの重なりは拒否する。
generic EvaluateBvhFrame の幅0拒否・位置規約・選択frame検査・部分channel拒否は変更していない。

## 明示の時刻

TimeModeは必須。HeaderFrameTimeでは t_i=double(i)*FrameTime、OverrideFpsでは t_i=double(i)/SourceFps を毎回独立に計算する。
HeaderFrameTime時のSourceFps設定は0とする。OverrideFpsは正で有限、逆数の間隔も正で有限でなければならない。
floatへの変換前に上限を検査し、正のtimeが0へ落ちる場合、非finite、隣接格納timeが厳密増加しない場合は拒否する。
DurationSecondsは最後に格納したfloat key timeそのもの。1frameならtime/durationとも0。
headerの間隔、選んだ間隔、double最終時刻、格納duration、最大丸め誤差をreportへ残す。

## 生成値だけでなく最終keyを検証する

1. source回転→明示basis→既存native retargetで全keyを生成する
2. trackごとに前keyとのdouble dotが負の場合だけquaternion全成分を反転し、半球を連続化する
3. 完成済みchannelの各keyを、実Samplerと共有する区間算術へ通す
4. その列quaternionを、同じWに対してnative共役正規化→Compose→FK→jointModel/paletteへ通して検査する
5. 全key成功後だけclip/reportをまとめて公開する

Samplerの内部keyはraw値を返すとは限らない。LINEARではSlerpと再正規化が入り、区間がMath::Constants::EPSILON以下ではalpha=0になる。
SampleSkeletalChannelIntervalは旧式をそのまま共有し、Samplerのempty/front/back/検索は維持する。
SampleSkeletalChannelAtKnownKeyは検証済みの厳密増加channel専用で、同じ区間式をO(1)で呼ぶ。内部keyをraw値へ近道しない。
このimporterでは全trackが同じkey grid、durationが最後のkeyなので、Samplerのtime clampはkey時刻を変えない。
新しいValidateSkeletalRotationFrameValuesは、mapping順/target一致・有限な近似単位quaternion（長さ二乗と1の差1e-4以内）を要求し、生成入口と同じnative実装で照合する。
生成側のRetargetSkeletalRotationFrameの契約は変えない。

bStoredKeysValidatedと最大回転誤差は、この最終key評価についての値。上限は既存の暫定0.05度。
補間区間全域、skinning/bounds、GPU、実Blenderの品質を保証するものではなく、bContinuousCurveValidatedはfalseのまま。
0/1e-7/2e-7秒で0/90/180度の例は、生成単frameが通っても内部keyが0度になり、最終key検査で拒否される。

## 位置は警告つき診断に分離

非rootのpositionは元joint名/番号とchannel番号、有限のmin/maxを所有reportへ残す。max−minは計算しない。
rootのcomplete XYZがrotationより前にある場合だけ、明示規約でdeltaを解釈する。
- additive: raw XYZ。OFFSET+channelsを一度作って引き直すとoverflow/桁落ちするので、この等価な式を使う
- absolute: checked(raw XYZ−OFFSET)
- position無し: delta=0。OFFSETを移動量として扱わない
- partial位置またはposition後置/混在: deltaを推測せず、IncompleteChannels/UnsupportedLayoutを記録する

root deltaとcanonical deltaは別の有限range。deltaが表せないframe数/最初のframe、変換不能数/最初のframe/理由を分ける。
canonicalへのscale変換がoverflow/zero-underflowしても、位置を出さない回転clipはその理由だけでは拒否しない。
positionScaleはこの診断に使い、targetの平行移動/scaleを変えない。rawのNaN/Infを無視して受け入れるわけではない。

## 所有と予算

clip名・全key・source/targetのUTF8名・pair・入力C・未写像一覧・位置観測を所有する。以前のoutは成功まで変更せず、確保例外でも保持する。
入力と以前のoutが生存している間の追加payloadを、確保前にchecked uint64/size_tで見積もる。
- source/target数学的上限1024。名前/frame/value制限は明示limitsを併用する
- output keys F*Mは既定1<<20、joint-frames F*(S+T)は1<<22
- work chargeは 2*F*(S*S+3*T*T+M*M+64*(S+T+M))+2*R+16*N。既定1<<26
- Rは元value数、Nはsource/target/clip名のUTF8 byte数。2回のnative評価と残る二乗走査を数える
- 新しい所有payloadの計画上限は既定128MiB。sizeofに基づくkey/channel/report/scratch/native一時配列と名前/index用の保守的余裕を含む

workはwall-clock保証ではない。所有byteもRSSの保証ではなく、allocator管理領域・stack・caller入力・保持中の以前のoutは含めない。
既存native入口を反復するため、全importが線形/無確保とは称さない。前計算の広いAPI組替えより先に、検証済み経路でclipを通す有限段階である。
legacy cookerの128関節制限は別の書出し境界。ここではin-memory clipの数学的上限と混同しない。

## 自動のrest補正（match・align_bones）

`rest_pose.mode` の自動補正では、関節から、途中の未写像関節を越えた直近の写像済み子孫それぞれへの単位ベクトルを足し、その和を正規化して rest の骨方向とする。
写像済みの子孫がない末端は親から関節への方向を使う。元は `axes`・`position_scale` による座標・単位変換後の正準空間、対象は RootFrame を含む作者 rest のワールドで求める。
元・対象の単位骨方向を a・b、元の rest からのワールド回転差分を D、対象の rest のワールド回転を B、出力のワールド回転を W とする。

- `explicit`: 指定の C を使い、式は従来どおり W = C·D·Cᵀ·B。骨方向が決まらなくても、その理由では拒否しない。
- `match`: C = I、W = D·B。各対応の rest の骨方向の角度が `maximum_error_degrees`（既定 5°）を超えたら `rest_direction_mismatch` で拒否し、出力の clip と報告を変えない。成功時は対応ごとの補正前後の角度 `BeforeRadians`・`AfterRadians`（match では同じ値）と C を報告する。
- `align_bones`: 最小回転 S(a→b) と `up_hint` による骨軸まわりの捻り θ から C = R_b(θ)·S を求める。式は W = D·Cᵀ·B。対象の bind を Cᵀ·B（rest の骨を元の向きへ合わせた bind）に置き換えてから元の差分 D を掛けるため、変換後の骨方向は全フレームで元の骨方向 D·a と浮動小数の誤差の範囲内で一致する。

`up_hint` は骨軸まわりの捻りだけを決め、骨方向を変えない。それぞれの骨方向に垂直な面へ投影した対象側の上向きが、Cᵀ によって元側の上向きへ写るように捻りを決める。
S と θ の式の詳細は [Retarget.md の「rest の向きとねじれ」](Retarget.md#rest-の向きとねじれ) を参照する。
向きが正反対（a·b < −1 + 1e-10）の場合は、`up_hint` を a に垂直な面へ投影した軸で半回転する。その軸も退化すれば、|a_x| < 0.7 なら (1,0,0)、それ以外なら (0,0,1) を補助軸 e とし、a×e を正規化した軸を使う。
`up_hint` がどちらかの骨方向と平行な場合は捻りを推測せず最小回転 S だけを使う。正反対や平行であることだけでは拒否しない。

拒否の条件と理由は次のとおり。

- `match`・`align_bones` で方向の和の長さが 1e-10 未満など、骨方向が決まらない場合は `rest_bone_direction_missing`。写像済みの子孫も親もない root を含み、明示 C への切り替えはしない。
- `match` で rest の方向角が許容値を超えた場合は `rest_direction_mismatch`。
- 共通またはロール別の `up_hint` が非有限、または長さ 1e-10 未満なら `retarget_input`。
- `rest_up_hint` は捻りの補助軸も作れない場合の守りであり、単位の骨方向では起きない。

未写像の対象関節にはチャンネルを出さず、bind のローカル姿勢を保つ。

`SkeletalBvhClipImportTest` は、分岐・End Site を持ち全フレームで回る手書きの多関節 BVH と、rest に ±20° の振りと捻りのずれを持つ対象（RootFrame・メッシュの node の変換・bind の scale を持つ対象を含む）で、全フレームの骨方向誤差 0.1° 以内などを確かめる。
0.1° は要件の暫定値である。
