# アニメーション実行系

## 姿勢と所有権

LocalPoseの回転はglTF規約。行ベクトル行列への共役はPoseTypesの変換に閉じる。グラフはAnimGraphResourceの不変データを共有し、時刻・状態・パラメータ・PosePoolはAnimGraphInstanceごとに持つ。骨格の親添字と各添字の名前が一致する場合だけ束縛する。重複名は骨格の既存契約に従い、名前検索では先頭を使う。

Instanceへ渡す骨格とmeshは使用中に呼出側が生存させる。AnimatorComponentはその強参照を持ち、aggregate内の差替えを検出する。資産setterでrevisionが変わったら再Initializeする。Mだけの変更はSetMeshTransformで再準備し、時刻とパラメータを保つ。割込snapshotが有効な間のM変更は拒否するため、その場合は明示再Initializeする。

Updateは時刻と状態を進め、Evaluateはローカル姿勢を公開する。Update-onlyは公開姿勢を変更しない。割込時は対象状態機械の現在のローカル姿勢を内部で評価して保存する。DAGで共有したnodeは時刻も共有し、別stateが有効な共有枝を遷移開始で巻き戻さない。独立した再生時刻が必要なら別のClip nodeを定義する。

AnimatorはAnimation群でUpdate、PoseFinalize群でEvaluate・modifier・Submitを行う。外部駆動中のSkinnedMeshComponentは自分で時刻を進めず、自動Sampleもしない。これによりコンポーネント作成順で二重評価しない。解除時は単一クリップ再生へ戻る。

## animgraph.json v1

最上位はversion=1、nodes配列、root（nodeのid）。paramsとmasksは任意。

- params: name、type(float/int/bool/trigger)、value、任意bind。bindはspeed、velocityX/Y/Z、accelerationX/Y/Z、turnRate、grounded、groundNormalX/Y/Z。型を厳密に合わせる。実行時は添字でアクセスする
- masks: name、root（関節名）、fadeDepth。0は枝の全関節が1、正値は根から深くなるほど1へ立ち上げる。親が後ろに並ぶ骨格も扱う
- clip: clip（IClipResolverで解決する名前）、loop（既定true）、rate（既定1）。負のnonloopは末尾から開始
- blend1d: x（float値またはfloatパラメータ名）、samples（position/node）。positionは厳密増加、範囲外はクランプ
- blend2d: x/y、axisX/axisY（各2点以上の等間隔格子）、children（行優先）。双線形で混ぜる
- blend2: childrenが2件、weight（float値またはfloatパラメータ名、既定0.5）
- layered: base、layers（node、weight、任意mask、mode=override/additive）。加算の基準は各clipの先頭frameを現在の選択・ブレンド重みで合成した姿勢。平行移動・scaleは差分、回転はtarget×inverse(reference)の差を加える
- select: param（int/bool）、children。intは範囲にクランプ
- stateMachine: states（name/node）、initial（省略時先頭）、transitions。fromはstate名または*、to、duration（既定0.15秒）、priority（大きい順、同値は記述順）、exitTime、curve(linear/smoothstep)、interrupt(none/current/next/either)、conditions（param/op/value）。opはeq/ne/lt/le/gt/ge、boolとtriggerはeq/neのみ。自動の連鎖は1Updateで4回まで

Triggerは全状態機械が評価した後に一度消費する。スクリプトはEntityRefのSetAnimFloatByIndex/SetAnimIntByIndex/SetAnimBoolByIndex/SetAnimTriggerByIndexを使う。保持したEntityRefが失効した場合や型・添字が異なる場合はfalse。

グラフは未知のclip・parameter・node・mask・関節、循環、遷移先不在、重複id、数値不正、未対応versionを区別して拒否する。cookは既存raw.v0でJSONを保持する。同期・metadataの契約は別のクリップメタデータ層が担当する。

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

名前は最大1024文字。未知キーはextrasでは無視し、実行時sidecarでは警告する。glTF由来の長さ値はインポート単位変換を受け、実行時sidecarは変換後のモデル単位で与える。時間の倍率はevents/markers/loopと明示nominalSpeedへ適用する。

イベントはUpdateで蓄積し、AnimatorのPoseFinalizeで配送する。windowはBegin用領域とEnd用領域を予約し、重み閾値割れ・metadata差替え・Reset・グラフ破棄でもinterrupted Endを出す。同期グループ内で同名・同じ周回の重なるwindowは1組へまとめる。複数Updateをまとめて配送してもbatch順を保つ。64件の配送領域、256件の候補、8周の走査上限を超えた分は警告し、開いたwindowの終了を優先する。イベントcallback中のグラフ交換・再初期化・移動は行わない。購読先は配送・グラフ破棄より長く生存させる。

## ルートモーションと解析

ConsumeRootMotionはEntityへ適用するモデル平面のX/Z並進とY軸yawを返し、その場で累積値をクリアする。複数Updateの差分は平面剛体変換として合成する。ループのyawは周回分を保ち、逆再生にも対応する。軸ロックは軌跡へ適用してから差分を求める。raw poseでは先頭の基準位置・headingを残し、抽出したEntity変換の逆をposeへ戻す。G2で既にrootを分離したcurveには二重の除去をしない。

noneは姿勢を変えず、inPlaceは移動を姿勢から除去、extractは除去した移動を消費口へ渡す。stateのrootMotionはinherit/animation/velocity。animationはinPlace/extractの移動を採用し、velocityは消費口への出力を抑える。noneのクリップから状態設定だけで移動を新規抽出しない。レイヤーのroot関節maskとブレンド重みも移動へ反映する。Entityや物理への適用は呼出側が行う。

AnalyzeFootContactsはモデル空間Yと鉛直速度から接地候補を作り、groundOffset・窓・confidence・下書きJSONを返す。水平足滑り、地形、実際の接触は判定しない。DetectCycleはG2の共通処理を再利用し、関節の局所回転から周期と下書きloopを返す。並進だけの周期は対象外。どちらもResource・ファイルを変更せず、人が候補を確認して採用する。

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

NORVES_ENABLE_IMGUI=ONでビルドし、Gameへ--animation-debugを付けると合成パネルと保持物を追加する。明示指定時だけImGuiを有効にし、通常の起動画面は変更しない。Animator画面でparameter・状態・同期phase・停止/コマ送り、ソケット画面でprofileとoffsetを調整できる。色付き軸はWorld LateTick後の当frame姿勢から描く。viewはEnter成功直前に登録し、Leave冒頭で解除する。

このsceneはコード接続と調整のための合成素材。実リグでの見た目、GPUでの実行、録画と既定起動画面の比較は別途確認が必要。
