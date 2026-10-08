# 更新段階の契約

更新順は固定の粗い段階で保証する。個別の処理の依存関係は既存のDelegateを通したイベント駆動で連携し、依存グラフのスケジューラは設けない。

順序は Input → Movement → Default → Animation → PoseFinalize → 固定ステップ → PostPhysics → Camera → PreRender → 描画同期。
主群の既定はDefault、優先度は0。小さい優先度を先に実行し、同優先度では収集時のEntity深さ優先・Component登録順を保つ。

## Componentの設定

- SetTickGroupは主群を変更し、maskをその1群へ戻す。無効な群はfalseを返し状態を変えない。
- SetTickGroupMaskは主群を含む有効な群の集合だけを受け付ける。主群を外す/未知bit/0はfalse。
- SetTickPriorityはint16の値で段階内の順序を指定する。
- 既定のOnTickGroupは主群の呼び出しだけを従来のTickへ渡す。複数群を使う型はOnTickGroupを上書きする。
- 設定はComponentの型の性質で、PROPERTY/シーン保存の対象にはしない。
- フレーム途中の設定変更とComponent追加は次のフレーム収集から反映する。破棄予約は実行前に確認する。

## 段階への割当とポーズ（接続時の契約）

- 明示割当のないComponentはDefaultのまま。例外を含む型別の正本は後掲のComponent割当表。EntityのTickもDefaultで実行する。
- SkinnedMeshComponentはAnimationで時刻更新、PoseFinalizeで姿勢確定する。
- SpringArmComponentはCameraで固定ステップ後の位置を読む。
- 後半の群、Module LateTick、Application OnLateUpdateを含め、シミュレーションが停止したフレームは進めない。
- Moduleの従来TickAllは描画同期後の意味を維持する。

## 実装状況

公開群、Component設定、Worldの一回収集と群別実行・LateTick、Application/Moduleの後段配線を実装した。アニメ姿勢公開、SpringArmのCamera群、GameHandlerによる後段カメラ確定を接続済み。Windows/Gameの受入れ試験は未実行で、GR01全受入れの完了を意味しない。

## Worldの収集と破棄

- World::Tickで全群とFixed対象を一回収集する。主群・mask・優先度・走査順はフレーム内で固定する。
- EntityのDefault優先度は自分のDefault群Componentの最小値（なければ0）。Entityを先に採番し、同じownerのComponentより先に実行する。全既定0では従来の深さ優先・登録順を保つ。
- 呼出直前にownerの所属/親のactive/pending、自身の有効状態を確認する。
- 群のdispatchは最初の群と次の群へ進む境界でWorld変換を更新する。前の群で変更した親の姿勢を次の群から子が読める。同群内のsetter直後には暗黙同期しない。World::LateTickは再収集せず、PostPhysics/Camera/PreRenderも同じ境界規則を使う。Fixed対象も同じフレームの収集結果を使う。Tick前の単独Fixed呼出はその呼出だけの収集結果を使う。
- 更新・cleanup通知中のRemoveComponent/RemoveEntityは破棄予約へ変換する。ComponentのMarkForDestroyはpendingを設定する。
- 既存のWorld::Tick後と各固定ステップ後のcleanup位置は維持する。実破棄前に保持entryを無効化する。
- ObjectHeap/GCの即時削除もEntity::RemoveInnerを通して無効化する。callback自身が即時破棄された場合、Worldは戻り際に対象を触らない。即時削除した対象をcallback側でも再利用してはならない。
- Context所有の対象を手動deleteせず、所有者の除去APIを使う。WorldのFinalizeは更新/cleanupへの再入中に実行しない。

## 検証範囲

群・設定・順序/無効化helperのportable試験は実行した。実Worldの追加/削除、ObjectHeap連携、固定step後cleanupを対象とするWorldTickGroupTestを既存bundleへ登録しているが、Windows依存により現環境では未実行。helperの合格をWorld/Game統合の合格とは扱わない。

## Application/Moduleの後段

- TickSimulationはGameMode→World Tick→Particle Tick→固定step→World LateTick→Module LateTick→Handler OnLateUpdateの順に実行し、その後に描画同期を行う。
- 固定stepが0回でも後段は1回実行する。シミュレーション停止中は前半/後段を呼ばず、schedulerの累積値を保持する。
- Module LateTickはRunningのregistryで登録順。既存TickAllの位置（描画同期後）は変えない。
- OnUpdate/Script維持とOnPreRenderの位置は変更しない。Handlerはフレーム内で共有所有し、callbackによる差し替えでもそのフレームの対象を生存させる。
- ApplicationFixedStepPipelineTestの既存11ケースを維持し、全8群の逆登録順、0/1/2 fixedstep、DefaultとCameraの位置読出し対照、pause/resume、空/非Running registryの6ケースを追加した。Windows依存により17ケースは未実行。実フレームの外側順序とhandler寿命は静的レビューで確認した範囲である。

## 評価済みボーン姿勢

- SkinnedMeshComponentはAnimationで再生時刻を進め、PoseFinalizeでEvaluatePoseを呼ぶ。非表示でもゲーム用の姿勢は評価する。
- EvaluatePoseはdirty時だけSampleし、成功した再評価だけGetPoseSerialを増加する。描画proxyも同じキャッシュを使う。
- JointModelMatricesはinverse bindを含まないEntity空間の行ベクトル行列。既存BonePaletteの計算式は変えない。
- FindJointIndexはIdentity名を解決し、未発見/空名は-1。SkeletonResourceのSetJointsで索引を再構築し、重複は先頭優先、Unloadで消去する。
- TryGetJointModelMatrix / TryGetJointWorldMatrix / TryGetJointWorldTransformは自動評価しない。未評価/dirty/資産無効/範囲外はfalseで出力を維持する。
- ワールド行列はJointModel * OwnerWorld。読み取り時に合成するためrootの即時変更に追従する。child階層は既存WorldのUpdateWorldTransforms境界の確定値を読む。
- TryGetJointWorldTransformはEngineのCreateWorldRowVector（列scale）で再構成できる正scaleのTRSのみ成功する。shear/反転/退化をTRSへ黙って丸めない。完全な表現にはWorldMatrixを使う。
- 資産wrapper/子Resourceのunloadは読取りを拒否する。子Resourceの差替えはweak参照の同一性で再評価する。同じResource内のSetJoints/SetClipなどの編集は自動revision検出を行わず、SetSkeletalAssetで同じassetを再設定して姿勢を無効化する。
- SamplingTestへ追加した姿勢・serial・名前引き・資産寿命試験と既存FramePacket/M9はWindows依存で未実行。既存失敗の再現や、新しい失敗が増えていないことはこの環境では確認できていない。

- Mesh差し替え時は既定のmeshNode transformも再取得する。SetMeshNodeGlobalTransformによる明示overrideは維持し、SetSkeletalAssetで既定へ戻す。

## Gameカメラの後段確定

- SpringArmはCamera群。Rendering3DTestの入力/intentは通常Tickで処理し、カメラproxy送信だけをGameHandlerのOnLateUpdateへ予約する。Enterの初期同期は維持する。
- Game専用single camera slotは既存Delegateを使う。Data所有のbindingをweakでArmし、Dispatchは先にpendingを空にして一回だけ呼ぶ。同フレームの再Armは最後を採用、callback内の新予約を同じDispatchでは実行しない。
- binding/callbackは成功Enter時に一回作り、フレームごとのDelegateコピー/確保を増やさない。OnUpdateは前frame残件を破棄する。通常Tickがないmode/ポーズでは予約されない。Leave/failedEnter/handler破棄はweak参照で失効する。
- callbackはData/Componentをcaptureせず、weak state内のObjectId/ComponentIdからlive参照を解決する。通常Tickのcamera処理もIDから解決し、途中削除やdisabledを拒否する。
- Module Late後のchild pivot/cameraを扱うため、World変換確定→SpringArm refresh→World変換確定→proxy生成とする。追加のO(Entity数)走査2回はGR41でdirty世代による最適化の対象とする。
- late送信成功時のTICK_STAGE_SMOKE markerを追加したが、ログを実機で観測したわけではない。CameraWorld/M9、既定・近接・低角度の見た目は未検証。
- 既存SpringArmComponentTestへ全constructor群、固定0/1/2後の群単独追従、child変換、無効化/実削除、one-shot/上書き/weak寿命/再入のケースを追加。Windows.hでコンパイルが止まるため、静的レビューのみで実行合格ではない。

## G1のComponent割当表（現在の実装）

| 群 | 現在のComponent | 担当する処理 |
|---|---|---|
| Input | 明示割当なし | 後続の型を登録する枠 |
| Movement | 明示割当なし | 後続の型を登録する枠 |
| Default | MeshComponent、MegaGeometryComponent、BoardComponent、BillboardComponent、ImpostorComponent、TextComponent、LightComponent、DirectionalLightComponent、PointLightComponent、SpotLightComponent、CameraComponent、ScriptComponent、ColliderComponent、RigidBodyComponent | 既定の可変Tick。Entity::Tickもこの群 |
| Animation | SkinnedMeshComponent | 再生時刻更新 |
| PoseFinalize | SkinnedMeshComponent | EvaluatePoseによる姿勢確定 |
| PostPhysics | 明示割当なし | 固定処理後の利用者を登録する枠 |
| Camera | SpringArmComponent | 固定更新後のTransformから追従姿勢を更新 |
| PreRender | 明示割当なし | World Lateの最後。Module/HandlerのLateはこの群外 |

- 全型の現在の優先度は0。CameraComponentの可変Tickはno-opで、名前だけを理由にCamera群へ変更しない。Collider/RigidBodyも可変Tickは継承no-op、物理更新本体はPhysicsModuleのPreFixedTick/FixedTickにある
- 根拠はComponent/TickGroup.hの既定値、SkinnedMeshComponent全constructorとOnTickGroup、SpringArmComponent全constructor。主群/優先度による順序は可変群だけに適用し、FixedTickは収集されたEntity深さ優先・Component登録順のまま
- 今後Componentを追加・群変更するときは、この表に型名、群、処理、通常/ポーズ時の分類を同じ変更で登録する。複数群では実行責務と二重更新防止を明記し、依存はDelegateで通知する。名前やModule登録順から依存グラフを暗黙生成しない

## Bridge進行とポーズの3分類

現時点ではGameApplicationHandler::ShouldAdvanceSimulationによる共通gateが1つだけある。Bridge無効なら進行、有効時はEdit/Playingで進行しPaused/Stoppedで停止する。SimulationPauseState、bTickWhenPaused、World::TickWhenPausedは未実装であり、次表の将来欄を現在の機能として使ってはならない。

| 分類 | G1の現行動作 | GR119で拡張するときの契約 |
|---|---|---|
| Bridgeの進行に従う | GameModeは共通gateで更新 | GameModeの進行判断はBridgeに残し、ゲーム内pause状態と混同しない |
| 非ポーズ時のsimulation | World全群、Particle、固定step、Module Late、Handler Lateも同じgateで更新 | Bridge進行 AND ゲーム内非pauseの条件で実行 |
| ポーズ中も更新する印付き対象 | 未実装、該当する印付きComponentなし | bTickWhenPaused等の明示指定を持つ対象だけ、実時間dtで更新。上表と合わせて割当を登録 |

Handler OnUpdateとBridge DrainInbound、入力評価/メンテナンス、描画同期後の通常Module TickAll、OnPreRenderは現在もsimulation gateの外にある。これは将来の印付きComponent更新とは別物。停止中はfixed schedulerの累積値を保持し、前半/後半だけを勝手に動かさない。振動は既存のsimulation/focus通知でvoiceを取り消し、自動再開しない。

## 最終カメラとG14/G16の受渡し

現行フレームの主要な更新順は次のとおり（振動はHandler Late後・描画同期前にgate外で更新する）。

1. 入力評価 → Handler OnUpdate → Script BeginFrameMaintenance
2. 進行gate内：GameMode → World Tick（Input〜PoseFinalize）→ Particle
3. 進行gate内：固定stepを0〜8回。各回はTransform → Module PreFixedTick → Component FixedTick → Module FixedTick → Transform → cleanup
4. 進行gate内：World Late（PostPhysics → Camera → PreRender）→ Module Late → Handler OnLateUpdate
5. gate外：World描画同期 → SceneQueryの通常world-AABB再構築 → Script EndFrameMaintenance → Module TickAll → Handler OnPreRender → 描画

通常world-AABBの再構築と、GR08の物理公開snapshot/明示RefreshDynamicSnapshotは別の更新である。物理snapshotがここで暗黙refreshされると読んではならない。

- 承認済みS3に従い、現在のGameカメラ最終proxyはGameApplicationHandler::OnLateUpdateのslotから確定する。Module Lateは全World群より後だが、その最終確定より前である。「Module Late=Camera群」「Module Lateで最終カメラ確定」というロードマップ上の記述は現実装に適用しない
- G14 CameraDirectorの配置自体は未決のG14-S3に残す。どこに所有しても、候補/intentの評価と最終commitを分け、当フレームの最終結果はGame OnLateUpdateの確定境界で一度だけ反映する。Module Lateで準備する案は可能だが、描画同期後の通常Module Tickから別のcameraを上書きする方式にはしない
- G16 listenerはこの最終commit後の同じフレームのcamera snapshotを読む。現在の順序なら、その後の通常Module Tickか将来追加する明示的な確定通知が候補になる。Module Lateを最終cameraの読取点にしてはならない。停止中のlistener/Audio方針はGR119/GR73で定め、今のAudioServiceModuleにlistener連携があるとは扱わない
- 根拠：ApplicationProcessor::Tick/TickSimulation/AdvanceFixedSimulation、World::Tick/LateTick/FixedTick、GameApplicationHandler::OnLateUpdate/ShouldAdvanceSimulation、Game/CameraLateUpdate.h。これらの文書化は更新順や描画実装の変更ではない

## 群境界のTransform鮮度

- 最初のInputからPoseFinalizeまでとLateの各群に入る前にUpdateWorldTransformsを実行する。追加の群間同期はsnapshot内で次の群へ移ったときだけ行う。既存のLate入口/PostPhysics後の明示確定は、Late対象が空の場合も維持する。前半/後半の群順、同群優先度と登録順、追加翌frame/破棄無効化の規則は変えない
- WorldTickGroupTestは親・子・孫を作り、逆順登録した全8群で親を動かし、次群が同frameの子world姿勢を読むことを確認する。同群内の遅いobserverは同期前の値を読み、群ごとの一括確定であることも固定する
- この追加の実World試験はWindows.h依存で現環境ではcompile/実行未確認。既存の純群/dispatch helperの実行結果を、この階層試験の合格に置き換えない
- 追加の階層走査コストは実測前。GR41でdirty世代等による最適化を行うときも、この公開境界を保つ
