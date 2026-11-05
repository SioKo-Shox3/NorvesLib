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

最上位はversion（整数1）とmodifiers配列。次は呼出側のPoseModifierTypeTableにCustomを登録し、そのsettingsでjointを受ける場合の例である。CustomはGetBuiltinPoseModifierTypesの組み込みの型の表（Lean・Spine・Look・FootIK・SpringBone・FollowThroughを載せる）に無いので、型の登録なしでこの例を読み込むとUnknownTypeになる。空の群は `{"version":1,"modifiers":[]}` で指定できる。

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
- order: 0〜10000の整数。小さい順に適用し、同値は配列の記載順を保つ。省略時は「順序」の表にある名前の既定値、それ以外は登録表のDefaultOrderを使う。既定値の名前があっても型が自動登録されるわけではない。
- enabled: bool、既定true。falseなら中のApplyを呼ばない。
- weight: 省略時1。0〜1の数値、または `{"param":"Speed","curve":[[0,0],[1,1]]}` の2つの形を取る。paramは既存のFloat parameter名。curveは有限で非負の入力値が厳密増加する2点以上の列で、各重みは有限の0〜1。区分線形で補間し、範囲外は端の値を使う。重み0なら中のApplyを呼ばない。それ以外は文脈のWeightへ渡し、呼出後に元のWeightを復元する。姿勢への重みの反映は各modifierが行う。

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

### 順序

グラフを評価した姿勢に、次の順で後処理を重ねる。`*.posemod.json` の最上位と各項目の形は前節を参照する。

| 段 | 既定の order | 読むもの | 書くもの |
|---|---:|---|---|
| グラフの評価 | — | クリップ・状態・パラメータ・同期位相 | 後処理の入力となるローカル姿勢 |
| Lean | 100 | 入力姿勢と駆動信号、またはバンク・ピッチのパラメータ | 骨盤・胴のチェーンへ配分した傾きの回転 |
| Spine | 110 | 入力姿勢・同期位相（なければ内部位相）・速度 | 背骨の各関節へ加える波の回転 |
| Look | 200 | 入力姿勢・頭の向き・優先された視線目標 | 視線方向へ配分したチェーンの回転 |
| FootIK | 500 | 傾き適用後の脚の姿勢・地面の高さと法線・接地状態 | 足先を地面へ合わせる脚の回転と、設定時の骨盤・胸の補正 |
| SpringBone | 700 | ここまでの姿勢のワールド位置・粒子の履歴・重力や風 | 耳・尾などの先端を遅らせるチェーンの回転 |
| FollowThrough | 710 | ここまでの姿勢の関節回転・ばねの履歴・保持状態 | 首・胸の遅れの回転と、指定時の頭の向きの補償 |
| ソケット（保持物の追従） | — | modifier の後に公開された最終姿勢・ソケットと保持のオフセット | 保持物のワールド変換 |

`order` を省くと表の既定値を使い、同じ値なら `modifiers` 配列の記載順を保つ。明示した `order` で入れ替えられるが、通常はこの既定の順を推奨する。グラフの評価とソケットの解決は modifier の `order` では入れ替わらない。

傾き・波・視線は、設定した胴や背骨の関節を回し、その子にある脚の付け根と足先も動かす。足 IK をそれより前に解くと、合わせた足先が後の回転で地面から浮くか沈むため、Lean・Spine・Look の後で接地を合わせる。

SpringBone・FollowThrough は、接地を崩さない範囲で末端や首・胸へ遅れを加えるために用いる。IK の後に置くことで、接地補正まで含む姿勢の動きから遅れを求める。ただし、脚の祖先を回す設定では足先にも影響するため、接地を保つことが自動的に保証されるわけではない。

`ModifierOrderTest`（`SkeletalAnimationSamplingTest` の中）は合成の四足を使い、同じ設定と時間刻みで 120 フレームの骨パレットのハッシュが一致することを確かめる。FootIK だけを `order: 50` にして傾きより前へ移すとハッシュが変わり、両方の順序に共通する接地標本で足先と地面の平均誤差が増えることも検査する。

2026-11-05 の Debug の試験出力では、同一設定のハッシュ不一致は 0/120、順序を変えた場合の差は 120/120。12 度の合成斜面、1/30 秒と 1/60 秒を混ぜた刻み、共通の接地標本 144 件で、地面の法線方向の平均誤差は既定順 0.0406215574 m、FootIK 先行 0.0549377772 m だった。この試験は順序による差を確かめるもので、接地誤差の絶対上限は保証しない。

### 足の IK(FootIK)

組み込みの型 `FootIK` は既定order 500。設定は骨格単位の実行時サイドカー `*.posemod.json` のsettingsに置く。import.jsonのik節には置かない。長さはメートル、時間は秒。

| キー | 既定値・範囲 |
|---|---|
| legs | 必須。1〜8脚の配列 |
| legs[].name | 必須の文字列。脚の間で一意 |
| legs[].chain | 必須。根・中間・足首・任意の足先の関節名3〜4個。重複不可、後の関節は直前の関節の子孫 |
| legs[].pole | `"auto"`。または有限な数値3個の方向、長さ1e-6以上。正規化して使う |
| legs[].sole / legs[].maxStretch | 0.02 / 1.02。範囲はそれぞれ[0, 0.5] / [1, 1.5] |
| legs[].plant.downEvent / legs[].plant.upEvent | 空でないイベント名。既定は `Foot.<脚のname>.Down` / `Foot.<脚のname>.Up`。`legs[].plant` は省略可能なobject |
| plant | 省略時は足の固定が無効。objectを指定すると既定で有効になり、`{}` で仮の既定値を使う |
| plant.enabled | true。boolで足の固定を有効・無効にする |
| plant.heightThreshold / plant.speedThreshold | 仮の値0.03 / 0.5。範囲はそれぞれ[-0.5, 0.5] m / [0, 10] m/s |
| plant.liftHeight / plant.releaseBlendSeconds | 仮の値0.15 / 0.1。範囲はそれぞれ(0, 2] m / [0, 2]秒 |
| land | 省略可能なobject。足の固定の有効・無効にかかわらず着地の重みに使う |
| land.blendSeconds / land.snapDistance | 仮の値0.1秒 / 0.05 m。範囲はそれぞれ[0, 5]秒 / [0, 2] m |
| land.teleportDistance | 仮の値1.0 m。(0, 100] m |
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
    "ground": {"rayUp": 0.5, "rayDown": 1.0},
    "plant": {}
  }}]
}
```

bodyが有効でchestとpelvisが異なり、脚の根の祖先にchestがあれば前脚、それ以外は後脚。autoはbind姿勢の根〜足首の線から膝へ向かう方向を、根の親の関節ローカルで保持する。骨の軸の規約に依存せず、膝の曲がりを取り出せない姿勢は拒否する。明示のpoleはモデル空間の方向。

Applyは次の順に処理する。

1. 所有者のワールド位置が前フレームからteleportDistanceを超えて移動した場合、またはNotifyTeleportが呼ばれた場合、全脚の固定と着地の履歴を初期化する。初回は距離を比較しない。問い合わせの最後の当たり、IKの平滑化された重み、体の補正は保つ。Resetはそれらも含めて初期化する。
2. 足先のワールド位置からrayUpだけ上を始点にし、ワールドの真下へrayUp + rayDownの距離を一括で問い合わせる。当たりの点・法線を検証し、法線の傾斜がmaxSlopeDegを超えればTooSteepとする。
3. 足の固定が有効なとき、同じフレームの未配送PendingEventsを脚ごとに読むため1フレーム遅れない。downEventのPoint / Beginは接地、upEventのPoint / BeginまたはdownEventのEndは離地を表す。同じフレームに複数あれば、(Batch, OffsetSeconds, Sequence) で最も遅い1件だけを採る。続けて書いた窓のEndと次の窓のBeginが同じフレームに来ても接地のまま保ち、記録位置も変えない。イベント駆動では接地時にSwingからPlantedへ、離地時にSwingへ遷移する。まだイベントを受けていない脚は、持ち上がりがheightThreshold以下かつ足先の水平速度がspeedThreshold以下で接地し、持ち上がりがheightThresholdを超えると離地する。接地時には足先の真下で最後に有効だった地面のワールド位置を記録し、Planted中の足先X/Zをそこに固定する。Yは記録位置の高さにsoleと正の持ち上がりを加える。
4. Swingでは持ち上がりの割合 `a = clamp((補正前の足先Y − MeshToWorldの原点Y − sole) / liftHeight, 0, 1)` で、現在の地面と離地したときの地面の高さの差をそれぞれ `1 − a` と `a` の割合で足先の高さへ配る。Swing目標は地面とsoleの高さを下限とする。固定を解除したフレームの差（Planted目標 − Swing目標）を、解除したフレームを0秒としてreleaseBlendSecondsの間に直線で0へ減らしてSwing目標に加え、補正後も地面とsoleの高さを下限とする。足の固定が無効なときの足先目標は従来どおり、Yが `hit.Point.y + max(補正前の足先Y − MeshToWorldの原点Y, sole)`、X/Zが補正前の値。
5. 所有者が非接地なら脚ごとの着地重みを0にし、接地に戻った後は補正前の足先と地面＋soleの高さの差がsnapDistance以内になったフレームから、blendSecondsの間に0から1へ直線で上げる。blendSecondsが0なら即座に1とする。初回は所有者の接地状態に応じて1か0から始まり、駆動信号がなければ接地中とみなす。この着地重みは足の固定が無効でも計算する。
6. 各脚の問い合わせの重みを、当たりが有効なら1、失敗なら0へsmoothTimeで平滑化する。体の補正の標本は着地重みが0なら無効とし、それ以外は高さの差に着地重みを掛ける。有効な後脚の高さの差の最小を骨盤、前脚の最小を胸の目標にし、[-maxDrop, maxRaise]へクランプして臨界減衰で平滑化する。有効な脚がない群は0、前脚自体がなければ胸は骨盤と同じ目標。骨盤を上下し、平滑化後の2点の差と水平距離からピッチを求め、骨盤を除く胸までの関節へ均等に配る。水平距離1e-4未満ではピッチ0。
7. 脚ごとに、足首の目標 = 足先の目標 − 法線に合わせた足首→足先のベクトルを求める。Plantedの足首目標が根から `(根〜中間の長さ + 中間〜足首の長さ) × maxStretch` を超えたら固定を解除し、Swingの目標を求め直してから二関節IKと足首の法線合わせを行う。chainが3関節なら足首を足先として扱う。脚の問い合わせの重み・着地重み・文脈のWeightを補正に掛ける。

NoHit / NotReady / WrongThread / TooSteep / Unavailableの脚は最後の当たりで解きながら、重みをsmoothTimeで0へ下げる。まだ当たりがなければ解かない。非一様スケールの脚は警告してIKを掛けない。不正な補正値は書き込まず、姿勢にNaN・Infを出さない。

足の固定は歩幅合わせ（GR10・GR11の公称速度）と接地の検出に依存するため、16 fps由来の素材や浮遊期の長い襲歩では外れる。接地のPhaseMarkerから `Foot.<脚>.Down` を出す処理はGR11の範囲で、ここではイベント名を設定で受ける。一度でもその脚のイベントを受けると、テレポートまたはResetまでしきい値駆動へ戻らない。

地面はSetGroundQueryで借用して渡し、問い合わせの使用中は呼出側が生存させる。ParsePoseModifierConfigで作った包みからはFindFootIKModifierで取り出す。物理を使う場合は `MakePhysicsGroundQuerySettings(ground)` を渡してPhysicsGroundQueryを作る。layerMaskとmaxSlopeDegはそこで物理の問い合わせに反映し、FootIKもmaxSlopeDegで法線を検証する。

## 胴の傾き・背骨の波・視線

`GetBuiltinPoseModifierTypes` の組み込みの型の表には Lean・Spine・Look・FootIK・SpringBone・FollowThrough の 6 型が登録されている。次の 3 型は、既定の `order` では足 IK（`FootIK`: 500）より前に掛かる。明示した `order` があればそちらを使う。

| type | 実装 | 既定の order |
|---|---|---:|
| Lean | BodyLeanModifier | 100 |
| Spine | SpineWaveModifier | 110 |
| Look | LookAtModifier | 200 |

各型の `settings` の数値はすべて有限値でなければならない。下表の「有限 float」は float で表せる有限値を指す。区間の丸括弧は端を含まず、角括弧は端を含む。軸の配列は 3 要素の有限 float で、長さが `Math::Constants::EPSILON` より大きいものを正規化して使う。調整用の仮の既定値は表に示す。

### Lean の設定と傾きの配分

| キー | 既定値 | 範囲・条件 |
|---|---|---|
| `source` | `"params"` | `"params"` または `"signals"` |
| `bankParam` | `"BankAngle"` | 空でないパラメータ名。値の単位は rad |
| `pitchParam` | `"PitchAngle"` | 空でないパラメータ名。値の単位は rad |
| `chain` | 必須 | 1 関節以上の配列。根側から先端側へ後の関節が前の関節の子孫になる順で、重複しない。直接の親子でなくてもよい |
| `chain[].joint` | 必須 | 骨格にある関節名 |
| `chain[].weight` | 必須 | 非負の有限 float。全関節の和は 1 ± 1e-4 |
| `forwardAxis` | [0, 0, 1] | メッシュのモデル空間の前方軸 |
| `rightAxis` | [1, 0, 0] | メッシュのモデル空間の右軸。正規化後の前方軸との内積の絶対値が 0.9999 未満 |
| `maxBankDeg` | 25（仮） | 非負の有限 float、度。`signals` のバンクの上限 |
| `maxPitchDeg` | 10（仮） | 非負の有限 float、度。`signals` のピッチの上限 |
| `pitchPerAccel` | 0.02（仮） | 有限 float、rad / (m/s^2) |
| `gravity` | 9.81 | 正の有限 float、m/s^2 |
| `smoothingHalfLife` | 0.1（仮） | 正の有限 float、秒。`signals` の角度の臨界減衰 |
| `airFadeHalfLife` | 0.1（仮） | 正の有限 float、秒。接地による重みの臨界減衰 |
| `strength` | 1 | [0, 1]。文脈の重みへ掛ける倍率 |

`source="params"` は Float parameter の角度をそのまま配り、上限のクランプや角度の平滑化を加えない。パラメータが無いか Float でなければ 0 として読む。四足の移動では傾きの計算と平滑化を `QuadrupedLocomotionModel` に一本化し、その出力の `BankAngle`・`PitchAngle` を使う。

`source="signals"` は駆動信号から次の目標角を求め、`smoothingHalfLife` の臨界減衰で追う。`v` は `Speed`（m/s）、`ω` は `TurnRate`（rad/s）、`a_forward` はワールド空間の `Acceleration` をメッシュの前方へ射影した加速度（m/s^2）。駆動信号が無ければ両目標角は 0 になる。

```text
θ_bank = clamp(atan(v * ω / gravity), -radians(maxBankDeg), radians(maxBankDeg))
θ_pitch = clamp(a_forward * pitchPerAccel, -radians(maxPitchDeg), radians(maxPitchDeg))
w = groundWeight * strength * context.Weight
rotation = Quaternion(forwardAxis, θ_bank * w) * Quaternion(rightAxis, -θ_pitch * w)
```

回転の合成順と符号は移動の `PublishVisual` と同じで、ピッチの負回転を掛け、その左からバンクの正回転を掛ける。軸は関節のローカル軸ではなくメッシュのモデル空間で指定し、メッシュノードの回転で配分用の空間へ変換する。`TryDistributeModelRotation` でチェーンへ配り、並進とスケールは書き換えない。

両 source とも、駆動信号の `bGrounded=false` で `groundWeight` を 0 へ、接地中は 1 へ `airFadeHalfLife` の臨界減衰で移す。初回はその時点の接地の値から始め、駆動信号が無ければ接地中として扱う。

Lean を使う場面では `QuadrupedLocomotionComponent::SetVisualTiltScale(0)` にして、移動の `VisualPose` の剛体の傾きとの二重適用を避ける。この倍率の既定値は 1 で、既定の見た目を保つ。有限値を [0, 1] に制限し、非有限値は無視する。倍率が掛かるのは `VisualPose` の剛体の傾きだけで、`BankAngle`・`PitchAngle` のパラメータには掛からない。

### Spine の設定と位相

| キー | 既定値 | 範囲・条件 |
|---|---|---|
| `joints` | 必須 | 1 関節以上の配列。関節名は重複しない。祖先・子孫の順序の制約はない |
| `joints[].joint` | 必須 | 骨格にある関節名 |
| `joints[].phaseOffset` | 0 | 有限 float、周期単位。1 で 1 周 |
| `joints[].amplitude` | 0 | 有限 float の振幅（rad）、または 1 点以上の `{"speed":数値,"value":数値}` の配列 |
| `joints[].amplitude[].speed` | 必須 | 有限の非負値、m/s。配列内で厳密増加 |
| `joints[].amplitude[].value` | 必須 | 有限 float、rad。負の振幅も指定できる |
| `axis` | `"yaw"` | `"yaw"` または `"pitch"` |
| `upAxis` | [0, 1, 0] | メッシュのモデル空間のヨー軸 |
| `rightAxis` | [1, 0, 0] | メッシュのモデル空間のピッチ軸 |
| `frequency` | 1 | (0, 16]。位相 1 周あたりの波の周期数 |
| `syncGroup` | 省略時は内部位相 | 指定時は空でない同期グループ名 |
| `fallbackHz` | 1（仮） | [0, 20] Hz。内部位相の進む速さ |
| `strength` | 1 | [0, 1]。文脈の重みへ掛ける倍率 |

各関節へ次の角度を加える。`A_i(speed)` は数値の振幅、または速度のカーブの区分線形補間で、カーブの範囲外は端の値を使う。速度は駆動信号の `Speed`、信号が無ければ 0。

```text
θ_i = A_i(speed) * sin(2π * (frequency * p + phaseOffset_i)) * strength * context.Weight
```

`p` は名前で指す同期グループの位相。所有者の Animator の `FindSyncGroup(Identity name)` で添字を引き、`PoseModifierContext::SyncPhases` の同じ添字を使う。`FindSyncGroup` はグラフ未設定・名前未検出なら `Animation::InvalidAnimNode`（UINT32_MAX）を返す。所有者・Animator・有効な同期位相が無ければ、0 から始まる内部位相を `fallbackHz * DeltaSeconds` だけ進めて [0, 1) に折り返す。同期位相を使う間は内部位相を進めない。

`yaw` は `upAxis`、`pitch` は `rightAxis` の正回転を関節のグローバル回転へ左から掛ける。軸はメッシュノードの回転で配分用の空間へ変換する。`strength * context.Weight` が 0 なら姿勢に触らずビット一致で保つ。modifier の `Apply` が呼ばれる場合は、その間も内部位相を進めるが、設定ラッパーの `weight=0` は `Apply` 自体を省略する。

### Look の設定と目標

| キー | 既定値 | 範囲・条件 |
|---|---|---|
| `chain` | 必須 | 1 関節以上の配列。根側から先端側へ後の関節が前の関節の子孫になる順で、重複しない。直接の親子でなくてもよい |
| `chain[].joint` | 必須 | 骨格にある関節名 |
| `chain[].weight` | 必須 | 非負の有限 float。全関節の和は 1 ± 1e-4 |
| `head` | `chain` の最後の関節 | 最後の関節自身、またはその子孫の関節名 |
| `forwardAxis` | [0, 0, 1] | メッシュのモデル空間の前方軸 |
| `upAxis` | [0, 1, 0] | メッシュのモデル空間の上方軸。正規化後の前方軸との内積の絶対値が 0.9999 未満。前方を保って直交化する |
| `lookAxis` | バインド姿勢から計算 | 頭のローカル空間の注視軸。省略時は、メッシュのモデル空間での bind の頭の回転の逆で `forwardAxis` を変換して求める（メッシュノードの回転を含む） |
| `maxYawDeg` | 70（仮） | (0, 180] 度 |
| `maxPitchDeg` | 40（仮） | (0, 90] 度 |
| `halfLife` | 0.12（仮） | 正の有限 float、秒。角度の臨界減衰 |
| `maxAngularAccelDeg` | 2000（仮） | 正の有限 float、度/s^2。各軸の角加速度の上限 |
| `holdSlot` | 省略時は保持を参照しない | 指定時は空でない保持スロット名 |
| `holdWeightScale` | 0.5（仮） | [0, 1]。指定スロットの占有数が 0 より大きい間の重みの倍率 |
| `strength` | 1 | [0, 1]。文脈の重みへ掛ける倍率 |

目標は `AnimatorComponent::SetLookTarget(priority, target)` で登録する。`AnimatorLookTarget` は `None()`（目標なし）・`AtWorldPosition(position)`（ワールド位置）・`AtEntity(objectId)`（所有者と同じ World の Entity）の 3 種。大きい優先度が優先され、同じ優先度への登録は置き換える。非有限のワールド位置の登録は無視する。`ClearLookTarget(priority)` は指定した優先度だけを消し、未登録なら何もしない。

Look は毎フレーム、所有者の Animator の `TryResolveLookTarget(outWorldPosition)` で最高優先度の 1 件だけを引く。Entity はそのつど ObjectId から検索し、現在のワールド位置を使う。未登録・`None`・Entity の World 不在・Entity 消失なら false を返して出力を保ち、低い優先度には落ちず目標なしとして扱う。`None` は低い優先度の目標を明示的に抑えるためにも使える。

毎フレームの未加工の姿勢を入力し、頭の現在の注視方向と、頭から目標への方向をメッシュのモデル空間で測る。右は `upAxis × forwardAxis`。正のヨーは右、正のピッチは上を向く。現在の注視方向との差を目標角とし、ヨー差は最短角に折り返す。目標角は次の楕円の円錐に制限し、範囲外なら両角を同じ倍率で縮める。

```text
(yaw / radians(maxYawDeg))^2 + (pitch / radians(maxPitchDeg))^2 <= 1
```

角度を `halfLife` の臨界減衰で追い、ヨー・ピッチの各角速度の 1 フレームの差を `radians(maxAngularAccelDeg) * DeltaSeconds` 以下に制限する。角度は制限後の角速度 × `DeltaSeconds` だけ進める。内部の角度は rad、角速度は rad/s。角度へ `strength * context.Weight` と保持中の `holdWeightScale` を掛けた向きに、チェーンの配分で頭を向ける。

前フレームの回転後と回転前の頭の位置のずれをメッシュのモデル空間で保持し、今フレームの未加工姿勢の頭の位置に足した推定位置から目標方向を測る。視差を含む目標角を楕円の円錐で制限し、臨界減衰と角加速度の上限を通す。回転後に頭の位置のずれを更新し、回さなかったフレームではゼロにする。目標なしではヨー・ピッチの目標を 0 にして、アニメーションの姿勢へ平滑に戻る。

3 型とも、必須キーの欠けは `MissingKey`、型違いは `TypeMismatch`、未知の関節名は `UnknownJoint`、不正な数値・軸・配列や関節の条件違いは `OutOfRange` で拒否する。未知キー・重複キーは `UnknownKey`・`DuplicateKey`。設定を受け入れられない場合は前の modifier 群を保ち、`PoseModifierConfigReport.Path` が該当する設定を指す。

### 設定例

次は 3 型を並べた `*.posemod.json` の例。関節名・同期グループ名・保持スロット名は使用する骨格・グラフ・保持スロットの定義に合わせる。Lean の `BankAngle`・`PitchAngle` はグラフの Float parameter として用意し、移動から与える。移動の `VisualPose` の傾きは `SetVisualTiltScale(0)` で無効にする。

```json
{
  "version": 1,
  "modifiers": [
    {
      "type": "Lean",
      "settings": {
        "source": "params",
        "chain": [
          {"joint": "SpineRoot", "weight": 0.4},
          {"joint": "Chest", "weight": 0.6}
        ]
      }
    },
    {
      "type": "Spine",
      "settings": {
        "joints": [
          {"joint": "SpineRoot", "amplitude": [{"speed": 0, "value": 0}, {"speed": 4, "value": 0.03}]},
          {"joint": "Chest", "amplitude": 0.02, "phaseOffset": 0.25}
        ],
        "axis": "yaw",
        "syncGroup": "gait",
        "fallbackHz": 1
      }
    },
    {
      "type": "Look",
      "settings": {
        "chain": [
          {"joint": "Neck", "weight": 0.4},
          {"joint": "Head", "weight": 0.6}
        ],
        "holdSlot": "Mouth",
        "holdWeightScale": 0.5
      }
    }
  ]
}
```

## ばねの二次運動と首・胸の遅れ

組み込みの 6 型のうち、次の 2 型が二次運動と首・胸の遅れを担う。既定の `order` では足 IK（`FootIK`: 500）の後に掛かり、二次運動を加えてから首・胸の遅れを重ねる。明示した `order` があればそちらを使う。

| type | 実装 | 既定の order |
|---|---|---:|
| SpringBone | SpringBoneModifier | 700 |
| FollowThrough | FollowThroughModifier | 710 |

### SpringBone の設定

`settings` のキーは `chains` と `teleportDistance`。各チェーンのキーと合わせ、数値はすべて有限値でなければならない。区間の丸括弧は端を含まず、角括弧は端を含む。

| キー | 既定値 | 範囲・条件 |
|---|---|---|
| `chains` | 必須 | 1 本以上のチェーンの配列 |
| `teleportDistance` | 1.0 | (0, 1000] m。所有者の 1 フレームの移動の閾値 |
| `chains[].name` | 必須 | 文字列。同じ設定内のチェーン名は一意 |
| `chains[].joints` | 必須 | 2 関節以上の名前の配列。根側から先端側へ直接の親子で連続し、別チェーンと関節を共有しない。最初の関節はアニメ姿勢に従う固定の根 |
| `chains[].stiffness` | 80 | (0, 10000] s^-2 |
| `chains[].damping` | 8 | [0, 100] s^-1 |
| `chains[].gravity` | [0, 0, 0] | ワールド空間の加速度 [m/s^2]。3 要素の配列で各成分は [-100, 100] |
| `chains[].drag` | 0 | [0, 100] s^-1 |
| `chains[].radius` | 0 | [0, 10] m |
| `chains[].rotationLimitDeg` | 60 | (0, 180] 度 |

必須キーの欠けは `MissingKey`、型違いは `TypeMismatch`、数値・配列の範囲外、親子の不連続、チェーン間の関節共有は `OutOfRange`、未知の関節名は `UnknownJoint`。未知キーは `UnknownKey`、キーの重複とチェーン名の重複は `DuplicateKey` で拒否する。`PoseModifierConfigReport.Path` は、たとえば `modifiers[0].settings.chains[0].stiffness` のように問題の設定を指す。設定の検証に失敗した場合は前の modifier 群を保つ。

### SpringBone の粒子と姿勢への反映

粒子の位置と速度はワールド空間で保持する。各フレームのアニメ姿勢から求めたワールド位置を目標とするため、所有者の加速に対して先端が遅れ、その後目標へ戻る。`SpringStepAccumulator` による固定サブステップは `h = 1/120` 秒、1 回の `Apply` で最大 4 回。1 ステップ未満の余りは持ち越し、上限後にも 1 ステップ分に近い時間が残れば余り全体を捨てる。直前の評価で捨てた秒数は `GetLastDroppedSeconds` で取得できる。

固定の根以外を、ばね・速度に比例する減衰と抗力・重力・風の加速度で半陰的オイラー積分する。`pAnim` はアニメ姿勢のワールド位置、`p` と `v` は粒子の位置と速度である。

```text
a = stiffness * (pAnim - p) - (damping + drag) * v + gravity + windAcceleration
v = v + a * h
p = p + v * h
```

各サブステップで、根から先端へ順に親粒子からの距離をアニメ姿勢の骨の長さに拘束し、拘束後の位置で `v = (p - pPrevious) / h` と速度を置き直す。粒子の方向が退化した場合はアニメ姿勢の骨の方向を使う。長さが 0 の骨では親粒子の位置へ戻す。

粒子を `MeshToWorld` の逆変換と `MeshNodeTransform` で角度計算の空間へ戻し、現在の骨の方向から粒子の方向への差を `FromToRotation` で求める。差の角度を `rotationLimitDeg` で切り、単位回転から文脈の重み（0〜1 に制限）で補間して親関節のグローバル回転へ左から重ね、ローカル回転へ戻す。先端の位置を向けるために親の回転を変え、ローカルの並進・スケールは変えない。

初回、`RequestReset` 後、所有者の 1 フレームの移動が `teleportDistance` を超えたとき、または粒子の位置・速度が非有限になったときは、粒子をアニメ姿勢へ、速度と刻みの余りを 0 へ戻す。所有者の World の Tick の通し番号が前回の評価から 2 以上進んだ場合（評価停止や重み 0 からの復帰など）、または戻った場合も同様に戻す。同じ番号での再評価では戻さず、所有者か World がない場合は通し番号による判定をしない。目標位置や入力、積分途中などで非有限を検出して安全に処理できない場合は、リセットを要求し、有効な入力での次の評価で戻す。回転の反映途中に失敗した場合は書き換えた姿勢も復元する。不正な `DeltaSeconds`（非有限・負値）は `Apply` が拒否する。

重力・風が 0 なら静止した所有者のアニメ姿勢へ収束する。重力が 0 でない場合は、静止してもばねと重力が釣り合う分だけアニメ姿勢から垂れる。v1 では `radius` を受け取るが計算には使わず、球コライダーとの衝突はない。風は `SetWindAcceleration` の入力口だけで、既定は 0。風の場は GR46 の範囲である。

### FollowThrough の設定と角度の追従

`settings` の数値はすべて有限値でなければならない。

| キー | 既定値 | 範囲・条件 |
|---|---|---|
| `joints` | 必須 | 1 関節以上の名前の配列。重複せず、根側から先端側へ後の関節が前の関節の子孫になる順。直接の親子でなくてもよい |
| `head` | 省略時は補償なし | `joints` の最後の関節の子孫の名前 |
| `frequencyHz` | 2.0（仮） | (0, 30] Hz |
| `dampingRatio` | 0.7（仮） | (0, 1] |
| `massScale` | 1.0 | (0, 100] |
| `heldSlot` | 省略時は保持を参照しない | 指定時は空でない文字列 |
| `heldMassScale` | 1.0 | (0, 100] |
| `strength` | 0.25（仮） | [0, 1] |

必須キーの欠けは `MissingKey`、型違いは `TypeMismatch`、範囲外・関節の重複や順序違い・不正な `head` は `OutOfRange`、未知の関節名は `UnknownJoint`。未知キー・重複キーは `UnknownKey`・`DuplicateKey` で拒否する。

関節ごとに `AngularSpring` を持ち、モデル空間のグローバル回転を追う。ここでの回転は「角度の配分」と同じく、メッシュノードの逆変換を掛ける前の空間の回転である。実効の周波数は `frequencyHz / sqrt(massScaleEffective)`。`massScaleEffective` は通常 `massScale` で、所有者の `HoldSlotComponent` にある `heldSlot` の占有数が 0 より大きい間だけ `heldMassScale` を掛ける。保持物の質量の倍率を `HoldSlotComponent` から読むことはなく、設定値を使う。`heldMassScale > 1` なら周波数が下がり、遅れが大きくなる。所有者の World の Tick の通し番号が前回の評価から 2 以上進んだ場合（評価停止や重み 0 からの復帰など）、または戻った場合は、ばねの状態をアニメ回転へ戻す。

ばねの回転とアニメ回転の残差を、`strength` × 文脈の重み（積を 0〜1 に制限）で単位回転から補間し、アニメのグローバル回転へ左から重ねる。実効の重みが 0 のときは姿勢を書き換えず、ビット一致で保つ。`head` を指定すると、そのグローバル回転を補正前のアニメの値へ戻す。胸・首を `joints`、頭を `head` に指定すれば、頭の向きはアニメのまま胸と首だけが遅れる。

### 設定例

次の関節名・チェーン名・保持スロット名は例であり、使用する骨格と保持スロットの定義に合わせる。尾の `TailRoot` → `TailMid` → `TailTip` は直接の親子、`Chest` → `Neck` → `Head` は祖先から子孫の順を表す。

```json
{
  "version": 1,
  "modifiers": [
    {
      "type": "SpringBone",
      "settings": {
        "teleportDistance": 1.0,
        "chains": [
          {
            "name": "Tail",
            "joints": ["TailRoot", "TailMid", "TailTip"],
            "stiffness": 80,
            "damping": 8,
            "gravity": [0, 0, 0],
            "drag": 0,
            "radius": 0,
            "rotationLimitDeg": 60
          }
        ]
      }
    },
    {
      "type": "FollowThrough",
      "settings": {
        "joints": ["Chest", "Neck"],
        "head": "Head",
        "frequencyHz": 2.0,
        "dampingRatio": 0.7,
        "massScale": 1.0,
        "heldSlot": "Hand",
        "heldMassScale": 2.0,
        "strength": 0.25
      }
    }
  ]
}
```

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

同じ場面には、`AnimationDebugScene.cpp` が骨格・箱のメッシュ・歩行クリップをコードで作る合成の四足キャラクターも置く。実リグではない。四足は平地・18 度の斜面・階段を通る経路を周回する。床・斜面・階段は Box コライダーを持ち、表示する箱と同じ位置・回転で物理の地面問い合わせを受ける。

四足の後処理は `Assets/Gameplay/AnimationDebugQuadruped.posemod.json` から明示して読み込む。Lean・Spine・Look・FootIK・SpringBone・FollowThrough の 6 型を既定順で適用し、各重みはグラフの Float parameter へ結び付ける。耳と尾に SpringBone、胸と首に FollowThrough を掛ける。

`PoseModifierDebugView` の「姿勢の後処理」画面には、次の 5 種の調整・表示がある。

- ON/OFF: 各 modifier の「有効」を切り替える。
- 重み: 各 modifier の重みを 0〜1 で調整する。
- 数値: `settings` の数値をパスごとに編集する。不正な値は拒否し、最後に適用できた設定を保つ。
- A/B: 四足全体を A（補正あり）と B（補正なし）へ切り替える。
- デバッグ描画: 接地のレイと当たり・法線、固定点、ポールの設定方向と膝、視線、ばねの先端を表示する。ポールは設定方向の参考表示で、実際の解とは異なる場合がある。

`--animation-debug --pose-modifiers-off` を付けると、四足の後処理を無効にした B の状態で起動する。`--pose-modifiers-off` 単独はエラーになる。起動後は同じ調整画面で A/B を切り替えられる。

調整した値は手で資産へ写す。「現在の JSON をコピー」は設定と重みの結び付けをコピーするが、実行中の重みパラメータ値や各行の ON/OFF をそのまま資産へ保存する機能ではない。

この scene はコード接続と調整のための合成素材である。実リグでの見た目、GPU での実行、録画、既定起動画面との比較は、それぞれ別途の確認が必要。
