# アニメーション実行系

## 姿勢と所有権

LocalPoseの回転はglTF規約。行ベクトル行列への共役はPoseTypesの変換に閉じる。グラフはAnimGraphResourceの不変データを共有し、時刻・状態・パラメータ・PosePoolはAnimGraphInstanceごとに持つ。骨格の親添字と各添字の名前が一致する場合だけ束縛する。重複名は骨格の既存契約に従い、名前検索では先頭を使う。

Instanceへ渡す骨格とmeshは使用中に呼出側が生存させる。AnimatorComponentはその強参照を持ち、aggregate内の差替えを検出する。資産setterでrevisionが変わったら再Initializeする。Mだけの変更はSetMeshTransformで再準備し、時刻とパラメータを保つ。割込snapshotが有効な間のM変更は拒否するため、その場合は明示再Initializeする。

Updateは時刻と状態を進め、Evaluateはローカル姿勢を公開する。Update-onlyは公開姿勢を変更しない。割込時は対象状態機械の現在のローカル姿勢を内部で評価して保存する。DAGで共有したnodeは時刻も共有し、別stateが有効な共有枝を遷移開始で巻き戻さない。独立した再生時刻が必要なら別のClip nodeを定義する。

AnimatorはAnimation群でUpdate、PoseFinalize群でEvaluate・modifier・Submitを行う。外部駆動中のSkinnedMeshComponentは自分で時刻を進めず、自動Sampleもしない。これによりコンポーネント作成順で二重評価しない。解除時は単一クリップ再生へ戻る。

## animgraph.json v1

最上位はversion=1、nodes配列、root（nodeのid）。paramsとmasksは任意。

- params: name、type(float/int/bool/trigger)、value（省略時0）、任意bind。bindはspeed、velocityX/Y/Z、accelerationX/Y/Z、turnRate、grounded、groundNormalX/Y/Z。型を厳密に合わせる。float/intだけ任意min/max（intは整数）を片方または両方指定できる。SetFloat/SetInt・bindの書き込み・スクリプトのSetAnimFloatByIndex/SetAnimIntByIndexは範囲へ丸める。既定値が範囲外、min>max、bool/triggerへのmin/max指定はInvalidSchemaで読込失敗。実行時は添字でアクセスする
- masks: name、root（関節名1つ）かroots（関節名の配列）のどちらか一方、fadeDepth。0は枝の全関節が1、正値は根から深くなるほど1へ立ち上げる。rootsは各根の枝の重みの関節ごとの最大を取り、fadeDepthは各根に同じく効く。root/rootsの両方の指定・空配列はInvalidSchema、重複はDuplicateName、未知の関節名はUnknownJoint。親が後ろに並ぶ骨格も扱う
- clip: clip（IClipResolverで解決する名前）、loop（既定true）、rate（既定1）、markerEvents（既定false）。負のnonloopは末尾から開始。markerEventsは真偽値だけを受け付け、型違いはInvalidSchema（marker_events）で読込失敗
- blend1d: x（float値またはfloatパラメータ名）、samples（position/node）。positionは数値か文字列"nominalSpeed"。"nominalSpeed"は子のClipノードのクリップの公称速度（metadataの明示値、無ければrootの移動から推定。AnimGraphInstance::GetNominalSpeedと同じ値）を位置にする。子がClipノードでないとInvalidSchema、明示の公称速度が正でないとInvalidNominalSpeed、解決後の位置が厳密に増加しないとInvalidSchemaで読込失敗。推定値はインスタンスのmeshの変換に依存するため、推定に頼る位置はインスタンスのInitializeで確かめ、不正ならInitializeが失敗する。実行時のApplyMetadataJsonで公称速度が変わったら次のUpdateで位置を解き直し、不正ならUpdateがfalseを返して前の位置を保つ。範囲外はクランプ
- blend2d: x/y、axisX/axisY（各2点以上の等間隔格子）、children（行優先）。双線形で混ぜる。軸は明示値で指定し、公称速度からの自動配置は行わない
- blend2: childrenが2件、weight（float値またはfloatパラメータ名、既定0.5）
- layered: base、layers（node、weight、任意mask、mode=override/additive）。加算の基準は各clipの先頭frameを現在の選択・ブレンド重みで合成した姿勢。平行移動・scaleは差分、回転はtarget×inverse(reference)の差を加える
- select: param（int/bool）、children。intは範囲にクランプ
- stateMachine: states（name/node）、initial（省略時先頭）、transitions。fromはstate名または*、to、duration（既定0.15秒）、priority（大きい順、同値は記述順）、exitTime、curve(linear/smoothstep)、interrupt(none/current/next/either)、conditions（param/op/value）。opはeq/ne/lt/le/gt/ge、boolとtriggerはeq/neのみ。自動の連鎖は1Updateで4回まで

Triggerは全状態機械が評価した後に一度消費する。スクリプトはEntityRefのSetAnimFloatByIndex/SetAnimIntByIndex/SetAnimBoolByIndex/SetAnimTriggerByIndexを使う。保持したEntityRefが失効した場合や型・添字が異なる場合はfalse。

グラフは未知のclip・parameter・node・mask・関節、循環、遷移先不在、重複id、数値不正、公称速度の不正（InvalidNominalSpeed）、未対応versionを区別して拒否する。cookは既存raw.v0でJSONを保持する。同期・metadataの契約は別のクリップメタデータ層が担当する。

## 診断と検証範囲

AnimatorDebugSnapshotはCoreの値データ。Game側AnimatorDebugViewは明示Attach/Detachで登録する。World終了前にDetachし、既定起動画面へは自動接続しない。パラメータ上書き・凍結・コマ送りを提供する。GPU表示と実素材の品質はCPUテストでは検証されない。

## クリップメタデータと配送

SkeletalAnimationClip.Metadataはインポート・cookの基底データ。glTFの各animation.extras.norvesを読み、無指定なら空のままにする。実行時のAnimationClipResource.ApplyMetadataJson／ApplyMetadataFileは、この基底へ明示したキーだけを上書きする。再読み込みは毎回基底から作り、前回のoverlayを積み重ねない。events/markersの空配列とloop:nullは明示削除。ファイルはUTF-8、最大1MiB、呼出側がパスを選ぶ。ディスクの自動探索は行わない。

overlayはMetadataRevisionだけを更新し、姿勢revision、cooked receipt、rigの束縛証明を変更しない。無効な再読み込みは現データを保つ。SetClip/Unloadはoverlayを破棄する。グラフはUpdate/Evaluate/RequestStateでmetadataの更新を検出する。

- events: name、tまたはphase、任意end、minWeight（既定0.3）、value、intValue。end省略はpoint、指定時は開始より後の終了時刻を持つwindow。時刻は非減少順、1clip最大4096件
- markers: name、tまたはphase。名前は一意、時刻は厳密増加、末尾時刻は含めない。最大256件
- loop: start/end、またはnull。有効範囲は0以上、start<end<=duration
- rootMotion: mode（none/inPlace/extract）、joint（省略時はcurveの根または骨格の根）、x/z/yaw（既定true）、nominalSpeed（負値は自動推定）
- groundOffset: モデル空間Yの基準高さ

イベントとmarkerの命名規約はドット区切り（例 `Foot.LH.Down`）。接地は `Foot.<脚>.Down`・`Foot.<脚>.Up`、脚の記号は `LF`（左前）・`RF`（右前）・`LH`（左後）・`RH`（右後）とする。身震いのように始まりと終わりのある動作は、`*_begin` / `*_end` の2つの点イベントに分けず、endを持つ窓イベント1つで表す。

名前は最大1024文字。未知キーはextrasでは無視し、実行時sidecarでは警告する。glTF由来の長さ値はインポート単位変換を受け、実行時sidecarは変換後のモデル単位で与える。時間の倍率はevents/markers/loopと明示nominalSpeedへ適用する。

イベントはUpdateで蓄積し、AnimatorのPoseFinalizeで配送する。windowはBegin用領域とEnd用領域を予約し、重み閾値割れ・metadata差替え・Reset・グラフ破棄でもinterrupted Endを出す。同期グループ内で同名・同じ周回の重なるwindowは1組へまとめる。複数Updateをまとめて配送してもbatch順を保つ。64件の配送領域、256件の候補、8周の走査上限を超えた分は警告し、開いたwindowの終了を優先する。イベントcallback中のグラフ交換・再初期化・移動は行わない。購読先は配送・グラフ破棄より長く生存させる。

clipノードで `"markerEvents": true` を指定すると、そのノードがmarkerを跨いだときに同じ名前の点イベントを出す。minWeightは `AnimEvent{}` の既定値0.3、Value・IntValueは0。EventIndexは `events` の件数 + markerの添字となる。順再生の `(prev, cur]`（逆再生は `[cur, prev)`）、周回、ループ範囲外の除外は点イベントと同じ。同期グループ内でも、同じ配送batch・周回で別ノードの同名イベントが同時刻に重なる場合の重複除去は点イベントと同じ。既定falseのグラフの配送は変わらない。

markerと同名の点イベントも持つクリップで有効にすると二重に配送されるため、markerEventsはopt-inとする。

## ルートモーションと解析

ConsumeRootMotionはEntityへ適用するモデル平面のX/Z並進とY軸yawを返し、その場で累積値をクリアする。複数Updateの差分は平面剛体変換として合成する。ループのyawは周回分を保ち、逆再生にも対応する。軸ロックは軌跡へ適用してから差分を求める。raw poseでは先頭の基準位置・headingを残し、抽出したEntity変換の逆をposeへ戻す。G2で既にrootを分離したcurveには二重の除去をしない。

noneは姿勢を変えず、inPlaceは移動を姿勢から除去、extractは除去した移動を消費口へ渡す。stateのrootMotionはinherit/animation/velocity。animationはinPlace/extractの移動を採用し、velocityは消費口への出力を抑える。noneのクリップから状態設定だけで移動を新規抽出しない。レイヤーのroot関節maskとブレンド重みも移動へ反映する。Entityや物理への適用は呼出側が行う。

AnalyzeFootContactsはモデル空間Yと鉛直速度から接地候補を作り、groundOffset・窓・confidence・下書きJSONを返す。接地の判定は `DetectFootContactWindows` 1つにまとめ、cookも同じ関数を呼ぶ。足ごとの最低の高さを基準に、既定では高さ0.03以下かつ鉛直速度の絶対値0.15以下を接地とし、0.05秒未満の窓を除く。窓の中では高さ・鉛直速度のしきい値をともに1.5倍にして接地を維持する。groundOffsetは全足の最低の高さ。下書きはmetadataのloopが有効なら `Foot.<脚>.Down/Up` のmarker、非ループなら窓ごとの同名の点イベントとし、終端まで接地する窓のUpは省く。窓イベントは生成しない。

水平足滑り、地形、実際の接触は判定しない。DetectCycleはG2の共通処理を再利用し、関節の局所回転から周期と下書きloopを返す。並進だけの周期は対象外。どちらもResource・ファイルを変更せず、人が候補を確認して採用する。

## NVSKEL v1の任意節

既存CLIPレコードは48バイトのまま。metadataが既定値だけなら新しい節・文字列を追加せず、旧wire値を維持する。metadataがある場合は次の3節をoptional flags=0でまとめて追加する。

- EVNT: 32バイト。名前offset u64、名前size u32、開始/終了/minWeight/value f32、intValue i32
- MARK: 16バイト。名前offset u64、名前size u32、時刻f32
- META: clipごと48バイト。event先頭/件数・marker先頭/件数u32、mode/loop/axis flags u32、root joint u32、loop開始/終了・nominalSpeed・groundOffset f32、末尾8バイト予約0

名前は共通STRSのUTF-8。root jointは保存時に正準添字、束縛時に対象骨格添字へ写像する。EVNT/MARKの領域はclip順で連続し、余剰・重複・範囲外を拒否する。bank全体ではevents最大262144件、markers最大65536件。基底metadataをpublication同一性へ含め、実行時overlayは含めない。

## 位相同期と歩幅合わせ

Clip nodeのsyncGroupを指定したときだけ同期する。対象は正の周期を持つloop clip。任意の最上位syncGroups配列でname、speed（float値またはFloat parameter名）、minRate/maxRate（既定0.5/2）を指定する。speedを省略すれば歩幅倍率は1。設定を書かずnodeの名前だけで同期することもできる。

標準位相はgroupが周回数付きdoubleで保持し、最大重みのclipをleaderにする。同重みでは前のleaderを維持する。切替後もgroupの位相を新leaderの時刻から取り直さず、その位相に対応するleader秒から再生を続ける。参加者がいない間は位相を保持する。状態滞在時間と同期位相は別の時計であり、exitTimeは従来の状態時間を使う。

最初のmarker付きclipの名前順を標準順に固定し、i/件数の等間隔位相へ区分線形で対応させる。先頭を跨ぐ循環順の違いは許すが、名前集合・循環順の不一致やloop範囲外markerはMarkerMismatch。marker無しは正規化時刻へ降格する。実行時metadata交換でも検証して写像を更新し、標準位相は保つ。構成時にmarkerが無かったgroupへ後から新しい名前集合を入れる場合はgraphを再構成する。

歩幅倍率はleaderの通常周期速度を基準とする。各参加clipの公称速度×周期長を重み付きで平均し、leaderの再生rateと周期長から基準移動速度を求め、要求speedとの比をminRate/maxRateへ制限する。公称速度が得られない場合も倍率1をその範囲へ制限する。停止を許すデータはminRate=0を明示する。これは周期平均の調整であり、接地IKや地形への補正は行わない。

通常遷移の非共有clipは現在の標準位相へseedする。transitionのsourceMarker/targetMarker対を指定した場合は、実際に条件を満たした遷移元stateの寄与clipから位相offsetを採り、指定名の位相差を遷移先へ加える。group本体の位相は変えず、0秒の連鎖でもoffsetを合成する。両側に同groupの実markerが必要で、markerlessへ変更された場合は遷移開始前に拒否する。共有されて現在寄与中のnodeはseekしない。

seedによるseekはイベントとroot deltaの移動区間に含めない。イベントのframe内時刻はclip秒→標準位相→leader秒へ戻し、区分境界を跨いでも同時markerの配送を揃える。周回番号も標準位相に合わせる。GetSyncPhasesはgroup定義順の正規化位相、FindSyncGroupは名前から添字を返す。PoseModifierContextにも同じ位相列を渡す。

## 姿勢の後処理の土台

### 文脈と適用

PoseModifierContextはSkeleton・DeltaSeconds・Parameters・SyncPhasesに加え、MeshToWorld・MeshNodeTransformとWeight（0〜1、既定1）を渡す。AnimatorComponent.EvaluateAnimationがMeshToWorldへ詰める値は所有者Entityのワールド変換。行ベクトル規約でGetJointModelMatricesの行列に右から掛けるとワールドになり、SkinnedMeshComponent.TryGetJointWorldMatrixと同じ結果になる。所有者がなければ単位行列。MeshNodeTransformはSkinnedMeshComponent.GetMeshNodeGlobalTransformの値で、メッシュノードのグローバル変換を渡す。GetJointModelMatricesはその逆を掛けた後のモデル空間であり、AngleDistributionの空間との行き来に使う。GetDriveSignals／HasDriveSignalsで最後に受け入れた駆動信号と有無を読める。SetDriveSignalsはパラメータへの適用に成功した信号だけを保存し、拒否した信号では保存値を変えない。

GetOwnerは所有Entityを返し、そのポインタはApplyの間だけ有効。GetSkeletonAssetは束縛したSkeletonResource、FindJointIndexはその骨格の名前から添字を解決し、骨格なし・未知名は-1を返す。これらの借用値をmodifierの外へ持ち出さない。GetJointModelMatricesは必要になった時点でFKを一度だけ組み立てる。LocalPoseを書き換えて同じApply内で再取得するときはInvalidateを呼ぶ。Animatorも各modifierの前にInvalidateする。

### 角度の配分

Animation/Procedural/AngleDistribution.hのTryComputeJointGlobalRotation・TrySetJointGlobalRotation・TryDistributeModelRotationは、親から回転だけを合成する。根の親はsplitの骨格ならRootFrameの回転、それ以外は単位回転。ここでのグローバル回転と配分する回転は、GetJointModelMatricesがメッシュの逆変換を掛ける前の空間である。メッシュに回転があるときは呼出側がMeshNodeTransformを使い、この空間とメッシュのモデル空間の間で変換する。

関節の新しいグローバル回転からローカルへ戻す式は `local = inverse(parentGlobal) * newGlobal`。配分は根側から先端側へ指定し、各関節へモデル空間の軸回転を重みに応じて掛ける。重みは有限の非負値、和は1 ± 1e-4。チェーンは空でなく、添字は有効かつ重複せず、後の関節は前の関節の子孫でなければならない。不正な重み・回転・親経路を拒否した場合はfalseを返し、姿勢を一切変更しない。

並進・スケールは書き換えず、チェーン外の子孫のローカル回転も直接書き換えない。スケールは回転の合成に入れず、RootFrameからも各軸のスケールを除いて回転を取り出す。親の非一様スケールによる影響を含む行列の結果とは厳密には一致しない。

### ばね

Animation/Procedural/Spring.hはScalarSpring・VectorSpring・AngularSpringを提供する。AngularSpringは最短の回転差を回転ベクトルにし、角速度をrad/sで扱う。SpringSettingsのFrequencyHzは有限の正数、DampingRatioは有限かつ(0, 1]。1との差が1e-4以下なら臨界減衰としてMath/CriticalDamping.hのTryCriticalDampを使い、それ以外は減衰振動の応答を使う。

SpringStepAccumulatorは1/120秒の固定サブステップを蓄積し、1回のAdvanceで最大4回まで積分する。1ステップ未満の余りは持ち越す。上限後にも1ステップ分に近い時間が残れば、その余り全体を捨てた秒数としてoutDroppedSecondsへ返す。たとえば0.1秒では4/120秒だけ進み、残りを返す。捨てた時間を後のフレームで追い掛けない。

Resetは位置・回転と速度を指定値へ戻し、余りを0にする。既定は位置・速度がゼロ、回転は単位四元数。不正な入力でAdvance／Resetが失敗したときは状態を保持し、Advanceの出力も変更しない。

### *.posemod.json

骨格単位の実行時サイドカー。AnimatorComponent.ApplyPoseModifiersJson／ApplyPoseModifiersFileで明示して読み、cookのハッシュには含めない。ファイルのパスは呼出側が渡し、ディスクを自動探索しない。ファイルはUTF-8で、BOMも受け入れる。

最上位はversion（整数1）とmodifiers配列。次は呼出側のPoseModifierTypeTableにCustomを登録し、そのsettingsでjointを受ける場合の例である。GetBuiltinPoseModifierTypesにはFootIKが登録されている。Customを登録せずにこの例を読み込むとUnknownTypeになる。空の群は `{"version":1,"modifiers":[]}` で指定できる。

```json
{
  "version": 1,
  "modifiers": [
    {
      "type": "Custom",
      "order": 300,
      "enabled": true,
      "weight": {"param": "Speed", "curve": [[0, 0], [0.5, 0.2], [1, 1]]},
      "settings": {"joint": "Root"}
    }
  ]
}
```

- type: 登録表の型名。必須。settingsは型ごとの設定を置くobjectで、内容の検証と生成は登録したfactoryが担う。省略時は無効なJsonValueを渡すため、受け入れるかは型が決める。factoryへ渡したSettingsは呼出中だけ有効で、保持する値はコピーする。
- order: 0〜10000の整数。小さい順に適用し、同値は配列の記載順を保つ。省略時は次の名前の既定値、それ以外は登録表のDefaultOrderを使う。既定値の名前があっても型が自動登録されるわけではない。
- enabled: bool、既定true。falseなら中のApplyを呼ばない。
- weight: 省略時1。0〜1の数値、または `{"param":"Speed","curve":[[0,0],[1,1]]}` の2つの形を取る。paramは既存のFloat parameter名。curveは有限で非負の入力値が厳密増加する2点以上の列で、各重みは有限の0〜1。区分線形で補間し、範囲外は端の値を使う。重み0なら中のApplyを呼ばない。それ以外は文脈のWeightへ渡し、呼出後に元のWeightを復元する。姿勢への重みの反映は各modifierが行う。

| 型名 | 既定のorder |
|---|---:|
| Lean | 100 |
| Spine | 110 |
| Look | 200 |
| FootIK | 500 |
| SpringBone | 700 |
| FollowThrough | 710 |

PoseModifierConfigReportはErrorとPathを返す。最上位・modifier・weightの未知キーと重複キーは拒否し、settingsのキーは型がCheckPoseModifierKeysなどで検証する。factoryの相対パスは `modifiers[i].settings` の下へ結合する。ルートの構文エラーなどではPathが空になることがある。

| Error | 意味 |
|---|---|
| None | 成功 |
| InvalidJson | JSON構文またはファイルのUTF-8が不正 |
| UnsupportedVersion | 整数のversionが1でない |
| MissingKey | 必須キーの欠け |
| UnknownKey / DuplicateKey | 未知キー／重複キー |
| TypeMismatch / OutOfRange | 型違い／数値の範囲外 |
| UnknownType | 登録表にない型 |
| UnknownJoint | 型の設定で解決できない関節名 |
| UnknownParam | weightのparameterが存在しないかFloatでない |
| InvalidCurve | 点数・点の形・入力値の順序が不正 |
| NoSkeleton | Animatorに束縛した骨格がない |
| Busy | modifierを適用中の再設定 |
| FileReadFailed | 指定したファイルを読めない |
| CreateFailed | factoryがない、または生成に失敗 |

全件の検証と生成に成功した場合だけ、AddModifierで追加した分も含め、Animatorのmodifier群を丸ごと置き換える。空配列は群を空にする。構文・設定・ファイルの読込などで失敗した場合は前の群を保持する。

### 足の IK(FootIK)

組み込みの型 `FootIK` は既定order 500。設定は骨格単位の実行時サイドカー `*.posemod.json` のsettingsに置く。import.jsonのik節には置かない。長さはメートル、時間は秒。足の固定・着地の判定は扱わない。

| キー | 既定値・範囲 |
|---|---|
| legs | 必須。1〜8脚の配列 |
| legs[].name | 必須の文字列。脚の間で一意 |
| legs[].chain | 必須。根・中間・足首・任意の足先の関節名3〜4個。重複不可、後の関節は直前の関節の子孫 |
| legs[].pole | `"auto"`。または有限な数値3個の方向、長さ1e-6以上。正規化して使う |
| legs[].sole / legs[].maxStretch | 0.02 / 1.02。範囲はそれぞれ[0, 0.5] / [1, 1.5] |
| body | 省略時は体の補正なし。指定するならobject |
| body.pelvis / body.chest | body指定時は必須の関節名。chestはpelvis自身かその子孫 |
| body.maxDrop / body.maxRaise | 0.25 / 0.1。それぞれ[0, 2] |
| body.smoothTime | 0.12。[0, 5]。体と脚の重みの臨界減衰の半減期。0なら即時反映。body省略時も脚の重みに既定値が効く |
| ground | 省略時は以下の既定値 |
| ground.rayUp / ground.rayDown | 0.5 / 1.0。範囲はそれぞれ[0, 10] / (0, 20] |
| ground.maxSlopeDeg | 50。[0, 89]度 |
| ground.layerMask | 4294967295（全レイヤー）。[0, 4294967295]の整数リテラル |

数値は有限値だけを受け入れる。未知・重複キーはUnknownKey / DuplicateKey、必須キーの欠けはMissingKey、型違いはTypeMismatch、範囲・チェーン・autoポールの不正はOutOfRange、未知の関節名はUnknownJoint。脚名の重複もDuplicateKey。エラーのPathは `modifiers[i].settings` 以下を指し、拒否時は前のmodifier群を保つ。

```json
{
  "version": 1,
  "modifiers": [{"type": "FootIK", "settings": {
    "legs": [{"name": "RearLeft", "chain": ["Hip", "Knee", "Ankle", "Toe"], "pole": "auto"}],
    "body": {"pelvis": "Pelvis", "chest": "Chest"},
    "ground": {"rayUp": 0.5, "rayDown": 1.0}
  }}]
}
```

bodyが有効でchestとpelvisが異なり、脚の根の祖先にchestがあれば前脚、それ以外は後脚。autoはbind姿勢の根〜足首の線から膝へ向かう方向を、根の親の関節ローカルで保持する。骨の軸の規約に依存せず、膝の曲がりを取り出せない姿勢は拒否する。明示のpoleはモデル空間の方向。

Applyは次の順に処理する。

1. 足先のワールド位置からrayUpだけ上を始点にし、ワールドの真下へrayUp + rayDownの距離を一括で問い合わせる。
2. 当たりの点・法線を検証して状態を分類する。法線の傾斜がmaxSlopeDegを超えればTooSteep。足先の目標Yは `hit.Point.y + max(補正前の足先Y − MeshToWorldの原点Y, sole)`、X/Zは補正前を保つ。
3. 各脚の重みを、当たりが有効なら1、失敗なら0へsmoothTimeで平滑化する。
4. 有効な後脚の高さの差の最小を骨盤、前脚の最小を胸の目標にし、[-maxDrop, maxRaise]へクランプして臨界減衰で平滑化する。有効な脚がない群は0、前脚自体がなければ胸は骨盤と同じ目標。骨盤を上下し、平滑化後の2点の差と水平距離からピッチを求め、骨盤を除く胸までの関節へ均等に配る。水平距離1e-4未満ではピッチ0。
5. 脚ごとに、足首の目標 = 足先の目標 − 法線に合わせた足首→足先のベクトルを求め、二関節IK、足首の法線合わせを行う。chainが3関節なら足首を足先として扱う。脚の重みと文脈のWeightを補正に掛ける。

NoHit / NotReady / WrongThread / TooSteep / Unavailableの脚は最後の当たりで解きながら、重みをsmoothTimeで0へ下げる。まだ当たりがなければ解かない。非一様スケールの脚は警告してIKを掛けない。不正な補正値は書き込まず、姿勢にNaN・Infを出さない。

地面はSetGroundQueryで借用して渡し、問い合わせの使用中は呼出側が生存させる。ParsePoseModifierConfigで作った包みからはFindFootIKModifierで取り出す。物理を使う場合は `MakePhysicsGroundQuerySettings(ground)` を渡してPhysicsGroundQueryを作る。layerMaskとmaxSlopeDegはそこで物理の問い合わせに反映し、FootIKもmaxSlopeDegで法線を検証する。

## ソケットと保持

SkeletonResourceはSetSockets／ApplySocketsJson／ApplySocketsFileで実行時のソケット定義を置換する。失敗時は旧定義を保持する。名前は一意、親は既存joint、offsetは有限な剛体変換（scale=1、単位Quaternion）。JSONの親は関節名、C++のParentJointは現在のresourceの添字。runtime置換はpose revisionとimmutable cooked hashを変更せず、SetJoints／SetSplitSkeleton／Unloadで定義を更新・破棄する。FindSocket/GetSocketsの借用は置換まで有効。

同じ明示ファイルに次の配列を置き、それぞれの所有者へ適用できる。UTF-8/BOM、最大1MiB、JSON深さ32、各object最大64キー。自動探索や書き戻しは行わない。

- sockets: name、parent、任意position[3]、rotation[4]、scale[3]。最大256件
- holdSlots: name、capacity（既定1、最大32）、acceptTags。最大64slot、tagはslotごと最大64件
- attachProfiles: name、任意position/rotation/scale。最大64件
- grip: 保持物のlocal剛体変換。静的な保持物でも使える

SkinnedMeshComponent.GetSocketWorldTransformは公開済みjoint modelを読み、自動評価しない。骨basisをX軸から直交化してscale/shearを除去し、平行移動を保持する。反転・軸退化は失敗。最後にOwnerの正のEntity scaleを適用する。行ベクトルの合成順はinverse(grip)×profile×socketOffset×rigidJointModel×ownerWorld。

SocketAttachmentComponentは保持物に付け、targetをObjectIdで毎回解決する。初回のworld姿勢から位置Lerp・回転Slerpで補間し、profile切替もその時点のoffsetから補間する。AttachStateはDetached/Blending/Attached。Detachは現在姿勢を保持し、直近差分のLinear/Angular velocityを返す。最大sample dtと平滑秒数は設定可能で、長い空白では速度の基準を取り直す。物理bodyへ速度は自動適用しない。

PostPhysicsではOwner親とtarget親の依存を祖先から更新し、各依存のWorld変換を確定する。同じWorld TickSerialで重複評価せず、Entity親とattachmentを合わせた循環を拒否する。tick停止中の依存は姿勢を保持する。現実装は依存ごとにWorld変換走査を行うため、大量の追従物では走査の集約が必要。

HoldSlotComponentは取得直後から容量を予約する。acceptTagsが空なら全て、指定時はいずれかのtag一致が必要。保持物ID・質量倍率をOnAcquired/OnReleasedへ渡す。同じ物の二重取得、満杯、tag不一致、遷移中を区別する。通常Releaseも遷移中は拒否し、attachmentの中断・終了・component除去は内部清算で予約を解放する。Outer解除後のFinalizeでも、World生存中の保存済みowner IDを使って購読と予約を清算する。

profile切替は直接API、target AnimatorのInt parameter、Hold.ProfileイベントのIntValue、EntityRef.SetHoldProfileByIndexから行える。共通setterがbound Intも更新するため、script/event/UIからの選択が直後のparameter読出しで戻らない。EntityRef.DetachHeldItemも同じ寿命検証を通す。

### cooked SOCK

Skeleton v1にoptional SOCKを追加する。1件64Bで、STRS名offset u64/size u32、正準ParentJoint u32、position3f/rotation4f/scale3f、予約8B。空定義では節を出さず旧bytesを保持する。WithSkeletonSocketsは新しい所有Skeletonを返し、SOCKを含むContentHashへ更新する。この変更後はSkinMeshも新しいSkeletonContentHashに合わせて再構築する。runtime overlayはそのhashへ含めない。

### 合成デバッグscene

NORVES_ENABLE_IMGUI=ONでビルドし、Gameへ--animation-debugを付けると合成パネルと保持物を追加する。明示指定時だけImGuiを有効にし、通常の起動画面は変更しない。Animator画面でparameter・状態・同期phase・停止/コマ送り、ソケット画面でprofileとoffsetを調整できる。厳密境界の比較を有効にすると全頂点をCPUで評価し、関節境界との最小・最大値と包含を比較する。この追加コストは比較を無効にすれば発生しない。色付き軸はWorld LateTick後の当frame姿勢から描く。viewはEnter成功直前に登録し、Leave冒頭で解除する。

このsceneはコード接続と調整のための合成素材。実リグでの見た目、GPUでの実行、録画と既定起動画面の比較は別途確認が必要。
