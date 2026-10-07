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
