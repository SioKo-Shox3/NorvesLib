# 更新段階の契約

更新順は固定の粗い段階で保証する。個別の処理の依存関係は既存の `Delegate` を通したイベント駆動で連携し、依存グラフのスケジューラは設けない。

群の順序は Input → Movement → Default → Animation → PoseFinalize → 固定ステップ → PostPhysics → Camera → PreRender → 描画同期。主群の既定は Default、優先度は 0。小さい優先度を先に実行し、同優先度では Entity の深さ優先・Component の登録順を保つ。

## Component の設定と型ごとの登録

- `SetTickGroup` は有効な主群を設定し、mask をその 1 群へ戻す。無効な群は `false` を返し、状態を変えない。
- `SetTickGroupMask` は主群を含む有効な群の集合だけを受け付ける。主群を外す mask、未知の bit、0 は `false` を返し、状態を変えない。複数群を使うときは主群の設定後に mask を設定する。
- `SetTickPriority` は `int16_t` の値で同じ群内の順序を指定する。設定はシーン保存やリフレクションの対象ではない。
- 群・mask・優先度の変更、型の登録変更、Component の追加は次の収集から実行順に反映する。通常のフレーム中に変更されたものは次のフレームから反映する。破棄予約や無効化は呼び出し直前にも確認する。

毎フレームの処理は `World::RegisterComponentTick(const IClass*, const Component::ComponentTickRegistration&)` で型ごとに登録する。キーは `IClass` の完全一致であり、リフレクションを宣言した派生型には別途登録が必要となる。リフレクションを宣言しない派生型は基底の型として扱われる。登録のない型は毎フレームの処理を持たない。同じ型の再登録は登録を置き換え、次の収集で呼び先を付け直す。

`ComponentTickFn` は Component 参照と `float` の delta を受け取る関数ポインタ。`ComponentTickRegistration` の各項目は次の役割を持つ。

- `Name` はログ・検査用の名前で、文字列は登録した側が保持する。
- `Run` は共通の本体、`GroupRuns[TickGroupCount]` は群別の本体。`BodyFor` はその群の本体があればそれを、なければ `Run` を返す。`HasBody` はいずれかの群別本体または `Run` が存在するかを調べる。すべてが null の型は順序付きの一覧に載らず、ある群の `BodyFor` が null の場合もその群には載らない。`Run` が null でも群別本体は登録できる。
- `bPrimaryGroupOnly` が `true` のときは主群だけを対象にする。`false` のときは `Groups` に含まれる群を対象にする。どちらも Component 側の mask と `BodyFor` の有無を満たす必要がある。
- `TimeChannel` は型が受け取る delta と `World::TryGetComponentTickDelta` の取得元で、既定は World。`Reads`・`Writes` は本体が読み書きする ECS 列の型番号を登録する欄である。

`World::Tick` は収集の前に必要な呼び先を同期し、収集を開いて Input〜PoseFinalize を実行する。`World::LateTick` は同じ収集を使い、PostPhysics と Camera〜PreRender を実行して収集を閉じる。`m_OrderedTickSystems` は [0] Input〜PoseFinalize、[1] PostPhysics、[2] Camera〜PreRender の 3 系で、各呼び先は群・優先度・深さ優先の番号の順に並ぶ。Entity の各 Component は子 Entity より先に採番される。ECS 化で Entity 自身の毎フレームの処理（Entity 側の Tick 関数）が無くなったので、Entity 自身の処理を自分の Component より先に実行する対象はない。

## World の収集と破棄

- 収集中は呼び先の群・優先度・順序を固定する。`World::LateTick` では再収集しない。固定ステップのシステムも同じフレームの収集番号を共有する。
- 呼び出し直前に owner の World 所属・Tick 有効状態・親を含む active / pending 状態と、Component 自身の所有者・active / Tick 有効 / pending 状態を確認する。対象が外されたときは収集中でも呼び出さない。
- 更新・cleanup 通知中の `RemoveComponent` / `RemoveEntity` は破棄予約へ変換する。Component の `MarkForDestroy` は破棄を予約する。`World::Tick` 後、`World::LateTick` 後、各固定ステップ後に cleanup を行い、実破棄前に保持した呼び先を無効化する。
- `ObjectHeap` / GC の即時削除も `Entity::RemoveInner` を通して呼び先を無効化する。callback 自身が即時破棄された場合、World は戻り際に対象を触らない。callback 側でも即時削除した対象を再利用しない。
- Context 所有の対象を手動で `delete` せず、所有者の除去 API を使う。World の `Finalize` は更新・cleanup への再入中に実行しない。

## Component の割り当て表

表の群は各型の初期設定と登録で対象になる群を示す。主群や mask は後から変更できるが、実際の呼び先は前節の登録条件にも従う。ポーズ分類は後述の 3 分類を使う。処理なしの型も表に載せ、型ごとの登録があるものは処理がある場合の進行条件を示し、未登録のものは関連するサービスの条件を示す。登録が指定する channel は処理なしの型でも delta の問い合わせに使われる。

| 型 | 群（主群と全対象群） | 担当する処理 | 時間の channel | ポーズでの分類 | 登録の場所 |
|---|---|---|---|---|---|
| `MeshComponent` | Default（主群、処理なし） | 毎フレームの処理なし。描画時に変換キャッシュを更新 | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1909` |
| `MegaGeometryComponent` | Default（主群、処理なし） | 毎フレームの処理なし | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1910` |
| `CameraComponent` | Default（主群、処理なし） | 毎フレームの処理なし | Unscaled | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1913` |
| `LightComponent` | Default（主群、処理なし） | 毎フレームの処理なし | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1914` |
| `DirectionalLightComponent` | Default（主群、処理なし） | 毎フレームの処理なし | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1915` |
| `PointLightComponent` | Default（主群、処理なし） | 毎フレームの処理なし | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1916` |
| `SpotLightComponent` | Default（主群、処理なし） | 毎フレームの処理なし | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1917` |
| `BoardComponent` | Default（主群） | Flipbook を進める | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1925` |
| `BillboardComponent` | Default（主群） | Board の Flipbook を進める | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1926` |
| `ImpostorComponent` | Default（主群） | Board の Flipbook を進める | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1927` |
| `TextComponent` | Default（主群） | Board の Flipbook を進める | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1928` |
| `SpringArmComponent` | Camera（主群） | 固定更新後の位置を読み、追従姿勢を更新 | Unscaled | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1938` |
| `HoldSlotComponent` | PostPhysics（主群） | 保持状態の整理 | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1950` |
| `SocketAttachmentComponent` | PostPhysics（主群） | 評価済みの姿勢からソケットの追従を更新 | World | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1960` |
| `AnimatorComponent` | Animation（主群）・PoseFinalize | Animation でアニメを進め、PoseFinalize で評価とイベント配信 | Animation | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1978` |
| `SkinnedMeshComponent` | Animation（主群）・PoseFinalize | 自前の Animation 時刻更新と PoseFinalize での姿勢評価 | Animation | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1992` |
| `ScriptComponent` | Default（主群） | Script の更新 | World | 非ポーズ時のsimulation | `Library/Core/Private/Component/ScriptComponent.cpp:56` |
| `CharacterBodyComponent` | Movement（主群）・PoseFinalize | 設定に応じ一方の群で可変更新の移動を要求 | World | 非ポーズ時のsimulation | `Library/Modules/Physics/Private/Physics/PhysicsModule.cpp:103` |
| `EnemyComponent` | Default（主群） | 状態の遷移、攻撃の窓の開閉、アニメの要求。同じフレームの Animation 群に効く | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/Enemy/EnemyComponent.cpp` |
| `ShishaComponent` | Default（主群） | EnemyComponent と同じ本体を、型の完全一致のため自身の型で登録 | World | 非ポーズ時のsimulation | `Game/Gameplay/Shisha/ShishaComponent.cpp` の `Initialize` |
| `HitboxComponent` | 群なし（Module Late で評価） | 攻撃判定の窓と命中を評価し、OnHit を通知 | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp` の `LateTick` |
| `HurtboxSetComponent` | 群なし（Module Late で評価） | 部位ごとの被弾を評価し、OnHurt を通知 | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp` の `LateTick` |
| `ZoneSensorComponent` | なし（群に登録しない） | 毎フレームの処理なし。位置は EncounterService が Module Late で読む | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp:56` |
| `EncounterZoneComponent` | なし（群に登録しない） | 毎フレームの処理なし。EncounterService が Module Late で状態機械を進める | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp:57` |
| `FollowCameraComponent` | Input（主群）・Camera | Input で視線入力、Camera で追従・衝突処理 | Unscaled | 非ポーズ時のsimulation | `Game/Gameplay/Camera/FollowCameraComponent.cpp:109` |
| `QuadrupedLocomotionComponent` | Input（主群）・PoseFinalize・PostPhysics | 入力収集、ルート移動の取得、物理後の移動終了と見た目の更新 | Unscaled | 非ポーズ時のsimulation | `Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.cpp:212` |

- 優先度は `FollowCameraComponent` が -100、`CharacterBodyComponent` が 10、そのほかは既定の 0。型の初期設定は、前者 2 型と `QuadrupedLocomotionComponent` が `Initialize`、Animation 系と SpringArm・HoldSlot・SocketAttachment がコンストラクタで行う。
- `ColliderComponent` と `RigidBodyComponent` は毎フレーム処理の登録がない。物理は `PhysicsModule` が登録した World の固定ステップの段で動き、`FixedBegin` の `Physics.FixedPre` が `StepPreFixed`、`FixedTick` の `Physics.FixedStep` が `StepFixed` を呼ぶ。Module の `PreFixedTick` と `FixedTick` は、それぞれ `FixedModulePre` と `FixedModulePost` で別に呼ばれる。
- PostAnimation という群はない。アニメの姿勢を読んで付ける処理は `SocketAttachmentComponent`・`HoldSlotComponent` と同じ PostPhysics に置く。
- 新しく `RegisterComponentTick` で登録する型は、同じ変更でこの表に足す。漏れは ctest `TickContractTableTest`(`Scripts/CheckTickContractTable.ps1`)が止める。台本は `Library` と `Game` の `.cpp` から `RegisterComponentTick(<型>::StaticClass()` と `RegisterComponentTick(StaticClass()`(後者はファイル名の型)を集め、この表の 1 列目(バッククォートで囲んだ型名)に無い型、または文書に消えた API の名前があれば失敗する。表の 1 列目は型名をバッククォートで囲んだもの 1 つだけにする。
- 敵の Tick は Default 群で Animation 群より前に動き、アニメの要求を同じフレームに渡す。時計は World channel を使い、ヒットストップに追従する。
- HealthComponentは可変Tickを持たず、ApplyDamageを呼んだ側のTickの中でOnDamaged・OnStaggerBroken・OnDefeatedを通知する。
- `HitboxComponent`・`HurtboxSetComponent` は `RegisterComponentTick` で群に登録せず、Module Late と同じ進行 gate に従う。判定窓は `BeginAttack` / `EndAttack` の直接呼び出し、または同じ Entity の `AnimatorComponent::OnEvent` の Begin（Name が窓のイベント名と一致）と対応する WindowToken の End で開閉する。
- GameplayModule::Tick（通常のModule Tick）はDefeatGate::Updateの安全点で、持ち主の破棄を検出して保留を外し、保留のない撃破をOnDefeatCompleted → ProgressionState::MarkDefeatedへ渡す。
- `EncounterService` は `GameplayModule::LateTick`（Module Late）で、World の Late の群の後、Handler の `OnLateUpdate` の前に、シミュレーションが進むフレームごとに 1 回評価する。時間は `TimeSystem::GetFrameTimes()` の World channel を使い、`LateTick` の引数の Unscaled は使わない。
- Component からの登録依頼は評価境界まで保留し、最初の評価では World 内も走査する。事件は集めてから世代付きの Entity ハンドルで再検証し、通知する。
- `OnActiveProfileChanged` はこの評価中に通知される。Camera 群はその前に終わっているため、通知を Camera 群の更新に反映できるのは次フレーム以降。カメラ側の購読・切り替えは未接続で、画面への反映時点は未保証。

## Bridge進行とポーズの3分類

現時点では `GameApplicationHandler::ShouldAdvanceSimulation` による共通 gate が 1 つある。Bridge 無効なら進行し、有効時は Edit / Playing で進行、Paused / Stopped で停止する。`SimulationPauseState`、`bTickWhenPaused`、`World::TickWhenPaused` は未実装であり、将来欄を現在の機能として使わない。

| 分類 | 現行動作 | GR119 で拡張するときの契約 |
|---|---|---|
| Bridgeの進行に従う | GameMode は共通 gate で更新 | GameMode の進行判断は Bridge に残し、ゲーム内 pause 状態と混同しない |
| 非ポーズ時のsimulation | World 全群、Particle、固定ステップ、Module Late、Handler Late も同じ gate で更新 | Bridge 進行 AND ゲーム内非 pause の条件で実行 |
| ポーズ中も更新する印付き対象 | 未実装、該当する印付き Component なし | `bTickWhenPaused` 等の明示指定を持つ対象だけ実時間 delta で更新し、割り当て表へ登録 |

時間の channel はシミュレーションが進むフレームの中で delta の取り方を決めるものであり、進行 gate を越える指定ではない。World / Animation は owner の時間倍率も適用し、Unscaled には適用しない。時間倍率による hit stop で World / Animation / Physics が 0 でも進行 gate が開いていれば Unscaled の呼び先は実行される。一方、シミュレーションの進行が停止中は Unscaled の型も呼ばれない。`Test/Core/Engine/ApplicationFixedStepPipelineTest.cpp` の `TestHitStopTimeChannels` は両方の条件を区別している。

Handler の `OnUpdate` と Bridge の `DrainInbound`、入力評価・メンテナンス、描画同期後の通常 Module `TickAll`、`OnPreRender` は simulation gate の外にある。停止中は fixed scheduler の累積値を保持する。振動は既存の simulation / focus 通知で voice を取り消し、自動再開しない。

## フレームの順序

`ApplicationProcessor` のフレーム段は次の順に走る。各段内に記したシステムは登録順で実行される。World の段は Application の段の間に呼ばれる。

1. gate 外で入力を評価し、`PreUpdate` の `PreUpdate.HandlerUpdate`（骨格資産の反映と Handler の `OnUpdate`）→ `PreUpdate.ScriptBegin` を実行する。
2. 進行 gate 内で `PreSimulation` の `PreSimulation.GameModeStateMachine` → World の `Tick`（Input〜PoseFinalize）→ `PostTick` の `PostTick.Particles` を実行する。
3. 進行 gate 内で固定ステップを 0 回以上実行する。各回は World の `FixedBegin`（Transform 確定・補間準備と `Physics.FixedPre`）→ `FixedModulePre.PreFixedTick` → World の `FixedTick`（`Physics.CharacterFixed`、`Physics.FixedStep` 等）→ `FixedModulePost.FixedTick` → World の `FixedEnd`（Transform 確定・補間履歴の記録・cleanup）の順。固定ステップが 0 回でも次の後段は 1 回実行する。
4. 進行 gate 内で World の `LateTick`（PostPhysics → Camera → PreRender）→ `PostLateTick.ModuleLateTick` → `PostLateTick.HandlerLateUpdate` を実行する。Module の呼び出しは Running の registry で登録順。
5. gate 外で振動更新、World の描画同期、SceneQuery の通常 world-AABB 更新を行い、`PostUpdate.ScriptEnd` → `PostUpdate.ModuleTick`（通常の `TickAll`）→ Handler の `OnPreRender` → 描画へ進む。Handler はフレーム内で共有所有し、callback による差し替えでもそのフレームの対象を生存させる。

`PostLateTick.ModuleLateTick` の `GameplayModule::LateTick` は攻撃判定を 1 フレームに 1 回評価する。`CombatRuntime::Evaluate` の先頭で `World::UpdateWorldTransforms` を行い、姿勢評価・固定ステップ・PostPhysics のソケット追従後のシミュレーションの Transform（`GetWorldTransform`）を読む。
時間は `TimeSystem::GetFrameTimes().World` を使い、ヒットストップに追従する。Module `LateTick` の引数の delta は Unscaled のため攻撃判定には使わない。
配送は hitbox の登録順 → HurtboxSet の登録順 → 部位の番号で行い、候補ごとに配送直前に世代付きハンドルを再検証して `OnHit`（Hitbox）→ `OnHurt`（HurtboxSet）→ `OnAnyHit`（モジュール）の順に通知する。
剣の描画・トレイルは描画用に補間された Transform を読む。

通常 world-AABB の更新と、GR08 の物理公開 snapshot / 明示 `RefreshDynamicSnapshot` は別の更新であり、描画同期後に物理 snapshot が暗黙 refresh されるとは扱わない。

## 最終カメラとG14/G16の受渡し

- 最終のカメラの確定点は `PostLateTick` の Module `LateTick` の後に呼ばれる Handler の `OnLateUpdate`。現在の Game カメラの最終 proxy は `GameApplicationHandler::OnLateUpdate` の slot から確定する。ロードマップの「`IModule::LateTick` を最終のカメラの確定点」とする記述は採らない。
- G14 CameraDirector の配置は未決の G14-S3 に残す。どこに所有しても候補・intent の評価と最終 commit を分け、当フレームの最終結果は Game の `OnLateUpdate` の確定境界で一度だけ反映する。Module の `LateTick` で準備しても、描画同期後の通常 Module Tick から別の camera を上書きする方式にはしない。
- G16 listener は最終 commit 後の同じフレームの camera snapshot を読む。現在の順序なら、その後の通常 Module Tick か将来追加する明示的な確定通知が候補となる。Module の `LateTick` を最終カメラの読み取り点にしない。停止中の listener / Audio 方針は GR119 / GR73 で定め、現在の `AudioServiceModule` に listener 連携があるとは扱わない。
- Game 専用の camera slot は `Delegate` と weak binding を使う。`OnUpdate` で前フレームの残件を破棄し、同一フレームで複数回予約した場合は最後を採用する。`OnLateUpdate` の呼び出しは pending を先に空にし、callback 内の新予約は同じ呼び出しでは実行しない。
- 現在の camera snapshot は ID から live な Entity / Component を解決し、`UpdateRenderTransforms` → SpringArm の再更新 → `UpdateWorldTransforms` → proxy 生成の順で確定する。これにより Module Late 後の child pivot と SpringArm が更新した child camera を反映する。

## 評価済みボーン姿勢

- `SkinnedMeshComponent` は自前のアニメ駆動時、Animation で再生時刻を進め、PoseFinalize で `EvaluatePose` を呼ぶ。非表示でもゲーム用の姿勢を評価する。外部アニメ駆動時は自前の時刻更新・評価を行わず、`AnimatorComponent` が評価した姿勢を渡す。
- `EvaluatePose` は現在のキャッシュが有効なら再利用し、再評価が成功したときに `GetPoseSerial` が増える。外部からの `SubmitLocalPose` が成功した場合も serial が増える。描画 proxy も姿勢のキャッシュを使う。
- `JointModelMatrices` は inverse bind を含まない Entity 空間の行ベクトル行列。`FindJointIndex` は `Identity` 名を解決し、未発見・空名は -1。
- `TryGetJointModelMatrix` / `TryGetJointWorldMatrix` / `TryGetJointWorldTransform` は自動評価しない。未評価・dirty・資産無効・範囲外は `false` で出力を維持する。ワールド行列は `JointModel * OwnerWorld` で、child 階層は World の変換確定境界の値を読む。
- `TryGetJointWorldTransform` はエンジンの `CreateWorldRowVector` で再構成できる正 scale の TRS のみ成功する。shear・反転・退化を黙って丸めず、完全な表現には WorldMatrix を使う。
- 資産 wrapper / 子 Resource の unload 時は読み取りを拒否する。子 Resource の差し替えは weak 参照の同一性で再評価する。同じ Resource 内の `SetJoints` / `SetClip` 等の編集は自動 revision 検出を行わず、`SetSkeletalAsset` で同じ asset を再設定して姿勢を無効化する。Mesh 差し替え時は既定の meshNode transform を再取得し、明示 override は `SetSkeletalAsset` で既定へ戻す。

## 群境界のTransform鮮度

`World::Tick` と `World::LateTick` の入口で `UpdateWorldTransforms` を実行する。同じ順序付きの系の中では、呼び先が次の群へ移る境界で変換を更新する。同群内の setter 直後には暗黙同期しない。Late の PostPhysics 後は `UpdateRenderTransforms` が `UpdateWorldTransforms` を呼び、Camera〜PreRender の前に World と描画の変換を確定する。これらの入口と PostPhysics 後の確定は、対象が空の場合も維持する。

前の群で変更した親の姿勢は次の群から子が読める。群ごとの一括確定を保ち、同群内の遅い observer に setter 直後の伝播を保証しない。固定ステップの前後にも変換を確定する。

## 検証範囲

群・設定・順序・破棄、固定ステップ、時間 channel、姿勢および camera 後段の試験結果は `Docs/Architecture/GameFoundationValidation.md` に記録する。
