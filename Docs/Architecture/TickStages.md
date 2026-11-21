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
| `GroundContactComponent` | PostPhysics（主群・優先度 -1） | 固定ステップの公開番号が変わったときに、体と足ごとの接地のレイと表面の照会で同じ群の消費者（`FootSplashComponent` など優先度 0）より先に接地の状態を更新 | World | 非ポーズ時のsimulation | `Library/Core/Private/Component/GroundContactComponent.cpp:171` |
| `AnimatorComponent` | Animation（主群）・PoseFinalize | Animation でアニメを進め、PoseFinalize で評価とイベント配信 | Animation | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1978` |
| `SkinnedMeshComponent` | Animation（主群）・PoseFinalize | 自前の Animation 時刻更新と PoseFinalize での姿勢評価 | Animation | 非ポーズ時のsimulation | `Library/Core/Private/Object/World.cpp:1992` |
| `ScriptComponent` | Default（主群） | Script の更新 | World | 非ポーズ時のsimulation | `Library/Core/Private/Component/ScriptComponent.cpp:56` |
| `CharacterBodyComponent` | Movement（主群）・PoseFinalize | 設定に応じ一方の群で可変更新の移動を要求 | World | 非ポーズ時のsimulation | `Library/Modules/Physics/Private/Physics/PhysicsModule.cpp:103` |
| `SteeringAgentComponent` | 群なし（AI モジュールに登録） | `AIModule::PreFixedTick` で操舵し、固定駆動では `Physics.CharacterFixed` の `BeforeSimulation` で運動を要求。`PostLateTick.ModuleLateTick` で Animator へ公開し、有効時は操舵を描画 | 固定刻み（Physics）。可変駆動の運動は Body の World。公開・描画は delta を使わない | 非ポーズ時のsimulation | `Library/Modules/AI/Private/AI/Steering/SteeringAgentComponent.cpp` の `Initialize` |
| `EnemyComponent` | Default（主群） | 状態の遷移、攻撃の窓の開閉、アニメの要求。同じフレームの Animation 群に効く | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/Enemy/EnemyComponent.cpp` |
| `ShishaComponent` | Default（主群） | EnemyComponent と同じ本体を、型の完全一致のため自身の型で登録 | World | 非ポーズ時のsimulation | `Game/Gameplay/Shisha/ShishaComponent.cpp` の `Initialize` |
| `HitboxComponent` | 群なし（Module Late で評価） | 攻撃判定の窓と命中を評価し、OnHit を通知 | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp` の `LateTick` |
| `HurtboxSetComponent` | 群なし（Module Late で評価） | 部位ごとの被弾を評価し、OnHurt を通知 | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp` の `LateTick` |
| `ZoneSensorComponent` | なし（群に登録しない） | 毎フレームの処理なし。位置は EncounterService と InteractionService が Module Late で読む | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp:86` |
| `EncounterZoneComponent` | なし（群に登録しない） | 毎フレームの処理なし。EncounterService が Module Late で状態機械を進める | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp:87` |
| `InteractableComponent` | なし（群に登録しない） | 毎フレームの処理なし。InteractionService が Module Late で候補を読む | World | 非ポーズ時のsimulation | `Library/Modules/Gameplay/Private/Gameplay/GameplayModule.cpp:88` |
| `AudioComponent` | Tick なし（群に登録しない） | World3D のものは音声モジュールの Tick が持ち主の位置を読み、再生中のインスタンスに渡す | —（論理サウンド層の Update が実時間で進める） | Bridge の停止中も進む（通常 Module Tick は gate 外） | `Library/Modules/Audio/Private/Audio/AudioServiceModule.cpp`（Tick・RegisterReflectedTypes） |
| `AudioListenerComponent` | Tick なし（群に登録しない） | Initialize / Finalize で音声モジュールに登録・解除し、音声モジュールの Tick がリスナーを解決するときに読む | —（音声モジュールの Tick の実時間） | Bridge の停止中も進む（通常 Module Tick は gate 外） | `Library/Modules/Audio/Private/Audio/AudioServiceModule.cpp`（Tick・RegisterReflectedTypes） |
| `FollowCameraComponent` | Input（主群）・Camera | Input で視線入力、Camera で追従・衝突処理 | Unscaled | 非ポーズ時のsimulation | `Game/Gameplay/Camera/FollowCameraComponent.cpp:109` |
| `QuadrupedLocomotionComponent` | Input（主群）・PoseFinalize・PostPhysics | 入力収集、ルート移動の取得、物理後の移動終了と見た目の更新 | Unscaled | 非ポーズ時のsimulation | `Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.cpp:212` |

- 優先度は `FollowCameraComponent` が -100、`CharacterBodyComponent` が 10、そのほかは既定の 0。型の初期設定は、前者 2 型と `QuadrupedLocomotionComponent` が `Initialize`、Animation 系と SpringArm・HoldSlot・SocketAttachment がコンストラクタで行う。
- `ColliderComponent` と `RigidBodyComponent` は毎フレーム処理の登録がない。物理は `PhysicsModule` が登録した World の固定ステップの段で動き、`FixedBegin` の `Physics.FixedPre` が `StepPreFixed`、`FixedTick` の `Physics.FixedStep` が `StepFixed` を呼ぶ。Module の `PreFixedTick` と `FixedTick` は、それぞれ `FixedModulePre` と `FixedModulePost` で別に呼ばれる。
- PostAnimation という群はない。アニメの姿勢を読んで付ける処理は `SocketAttachmentComponent`・`HoldSlotComponent` と同じ PostPhysics に置く。
- 新しく `RegisterComponentTick` で登録する型は、同じ変更でこの表に足す。表の 1 列目は型名をバッククォートで囲んだもの 1 つだけにする。
- 敵の Tick は Default 群で Animation 群より前に動き、アニメの要求を同じフレームに渡す。時計は World channel を使い、ヒットストップに追従する。
- HealthComponentは可変Tickを持たず、ApplyDamageを呼んだ側のTickの中でOnDamaged・OnStaggerBroken・OnDefeatedを通知する。
- `HitboxComponent`・`HurtboxSetComponent` は `RegisterComponentTick` で群に登録せず、Module Late と同じ進行 gate に従う。判定窓は `BeginAttack` / `EndAttack` の直接呼び出し、または同じ Entity の `AnimatorComponent::OnEvent` の Begin（Name が窓のイベント名と一致）と対応する WindowToken の End で開閉する。
- GameplayModule::Tick（通常のModule Tick）はDefeatGate::Updateの安全点で、持ち主の破棄を検出して保留を外し、保留のない撃破をOnDefeatCompleted → ProgressionState::MarkDefeatedへ渡す。
- `EncounterService` は `GameplayModule::LateTick`（Module Late）で、World の Late の群の後、Handler の `OnLateUpdate` の前に、シミュレーションが進むフレームごとに 1 回評価する。時間は `TimeSystem::GetFrameTimes()` の World channel を使い、`LateTick` の引数の Unscaled は使わない。
- `InteractionService` は同じ `LateTick` で `EncounterService` の後に評価し、操作入力が押された瞬間を読む。
- Component からの登録依頼は評価境界まで保留し、最初の評価では World 内も走査する。事件は集めてから世代付きの Entity ハンドルで再検証し、通知する。
- `OnActiveProfileChanged` はこの評価中に通知される。Camera 群はその前に終わっているため、通知を Camera 群の更新に反映できるのは次フレーム以降。カメラ側の購読・切り替えは未接続で、画面への反映時点は未保証。

## Bridge進行とポーズの3分類

シミュレーションの進行には 2 つのゲートがある。

- `bBridgeAdvance` は `IApplicationHandler::ShouldAdvanceSimulation()` の戻り値で、ハンドラがなければ `true`。ゲームでは Bridge が無効なら進行し、有効なら `Edit` / `Playing` で進行、`Paused` / `Stopped` でシミュレーション全体を停止する。
- `bWorldAdvance` は `bBridgeAdvance && !GEngine->GetSimulationPauseState().IsWorldPaused()`。Bridge が進行していても、ゲーム内のポーズ理由が残っている間は通常のワールド更新を止める。

`PreUpdate` の後、`TickSimulationAndHaptics` は Bridge の判定を 1 回行い、World の条件を振動出力に適用する。`TickSimulation` はその Bridge の判定を受け取り、入口で World の条件をもう一度評価する。以後の各段はこの値を使う。`OnUpdate` でのポーズ変更は当フレームに、判定後のゲームモードなどでの変更は次のフレームの進行に反映される。

`Engine::Engine` が持つ `SimulationPauseState` は `GetSimulationPauseState()` で取得する。`Push` と `Pop` は `Menu`、`FocusLost`、`Minimized`、`Loading`、`Dialog` ごとに要求を数える。同じ理由を 2 回要求すると、2 回解除するまでその理由は有効。件数が 0 の理由を `Pop` すると警告を記録し、`false` を返す。`IsWorldPaused()` が `false` になるのは全理由が 0 件のときだけ。有効な理由のビット集合が変わるたびに `OnChanged` を 1 回通知し、変更後の集合を渡す。通知中の `Push`・`Pop`・`Reset` は要求数とビット集合に即座に反映され、ビット集合に変化があれば、その時点の集合を保留し、今の通知を全購読者に配り終えてから発生順に通知する。`ModuleRegistry::DispatchPauseStateChanged` はレジストリが `Running` のとき、登録順に `IModule::OnPauseStateChanged(reasons)` を呼ぶ。

| 分類 | 進行条件 | 対象 |
|---|---|---|
| Bridge の進行に従う | `bBridgeAdvance` | ゲームモードの状態機械とストリーミングはゲーム内ポーズ中も進む。 |
| 非ポーズ時の simulation | `bWorldAdvance` | 通常のワールド更新、固定ステップ、物理後の更新を実行する。 |
| ポーズ中も更新する印付き対象 | `bBridgeAdvance && !bWorldAdvance` | `World::TickWhenPaused` が、自身か所有者に印のある `Component` を通常の更新と同じ順序で更新する。各呼び先には 100 ms の上限で丸める前の実経過秒を渡す。 |

`Entity::SetTickWhenPaused` と `Component::SetTickWhenPaused` の既定値はどちらも `false`。`Entity` の印は直下の `Component` にだけ効き、子の `Entity` には伝わらない。`Component` の印は自身だけに効く。`World` は対象の `Component` を集合に保持する。両方の印が付いても集合には 1 件だけ入る。印の変更、`Component` の着脱、登録の解除時にだけ集合を更新するため、毎フレーム階層や各 `Entity` の子を走査して対象を探す必要はない。

`World::TickWhenPaused` は、印の付いた対象の集合が空なら順序の作り直しも呼び先の収集もせず、破棄予約の回収（`CleanupDestroyedObjects`）だけを行って抜ける。このとき順序の汚れの印は下ろさず、再開後の通常の更新で付け直す。集合が空でなく、かつ木の構造や Tick の設定が変わったフレームに限り、通常の `World::Tick` と共通の `SyncOrderedTicks` で先に呼び先を付け直す。この作り直しの費用は通常のフレームと同じで、変化の無いフレームでは走らない。集合内の各 `Component` の順序付きの登録記録から群・呼び出す本体・優先度・深さ優先の番号を取り、通常の更新と同じ群順で呼ぶ。各群内は優先度の昇順、同じ優先度では深さ優先の番号の昇順となる。呼び出し直前に所有者、印、対象の有効状態を確認し、最後に `CleanupDestroyedObjects` で破棄予約を回収する。

通常のワールド更新では、時間チャンネルが呼び先の受け取る経過時間を決める。`World` と `Animation` には所有者の時間倍率も掛かる。ヒットストップでこれらの時間が 0 になっても、`bWorldAdvance` が開いていれば `Unscaled` の呼び先は進む。ゲーム内ポーズ中に更新を受けるには、チャンネルにかかわらず自身か所有者に印が必要となる。このとき `TickSimulation` が渡すのは、`ClampVariableDeltaTime` で丸める前の `rawDeltaNanoseconds` を秒に換算した値（0 以下なら 0 秒）。

### 現在の更新と進行条件

「ゲート外」の処理はゲーム内ポーズ中も Bridge 停止中も毎フレーム呼ばれる。

| 更新 | ゲート | 持ち主・場所 |
|---|---|---|
| ワールドの通常の `Tick` 全群（`Input`〜`PoseFinalize`） | `bWorldAdvance` | `ApplicationProcessor::TickSimulation` → `World::Tick` |
| アニメーションの進行と姿勢の確定（GR10） | `bWorldAdvance`（印の既定値） | `World` に登録した `AnimatorComponent` の `Animation` 群と `PoseFinalize` 群 |
| 植生 | `bWorldAdvance` | `TickSimulation` が `World::Tick` の直後に `Vegetation::Tick` へ `World` の経過秒を渡す。 |
| CPU 粒子と GPU 粒子の時計（GR57） | `bWorldAdvance` | `PostTick.Particles` → `ParticleSystem::Tick`。その中で `VfxEffectSystem::Tick` が GPU 粒子の経過時間を加算するため、`Unscaled` の GPU 粒子も停止する。 |
| 気象の時間更新（GR49） | `bWorldAdvance` | `PostTick.Environment` → `EnvironmentSystem::Tick`。経過時間は `World` から取る。 |
| 固定ステップ | `bWorldAdvance` | `AdvanceFixedSimulation` がワールドの `FixedBegin` / `FixedTick` / `FixedEnd` とモジュールの `PreFixedTick` / `FixedTick` を駆動する。停止中は累積値を保持し、その間の経過時間を加算しない。 |
| AI の経路探索と操舵 | `bWorldAdvance` | 固定ステップ前半の `AIModule::PreFixedTick` が操舵の歩を始め、`m_Navigation.RunBudget()` の後に操舵速度を求める。運動結果の Animator への公開と操舵の描画は `PostLateTick.ModuleLateTick` の `AIModule::LateTick`。 |
| 物理後のワールド更新 | `bWorldAdvance` | `TickSimulation` → `World::LateTick`（`PostPhysics`、`Camera`、`PreRender`） |
| モジュールとハンドラの後段更新 | `bWorldAdvance` | `PostLateTick.ModuleLateTick` → `IModule::LateTick`、続いて `PostLateTick.HandlerLateUpdate` → `IApplicationHandler::OnLateUpdate` |
| チャンネルの経過時間（GR02） | `bWorldAdvance`（`Unscaled` を除く） | `TickSimulation` は毎フレーム `TimeSystem::BeginFrame` に進行条件を渡す。停止中は `Unscaled` 以外の経過時間が 0 になり、内部の実時間は進む。 |
| 振動の再生 | `bWorldAdvance` | `TickSimulationAndHaptics` が `SetHapticsPaused(!bWorldAdvance)` で停止を反映する。停止時に再生中の振動を取り消し、解除時も自動再開しない。 |
| ゲームモードの状態機械 | `bBridgeAdvance` | `PreSimulation.GameModeStateMachine` → `UpdateGameModeStateMachine` に `Unscaled` の経過秒を渡す。 |
| 印付きの対象のポーズ時更新 | `bBridgeAdvance && !bWorldAdvance` | `TickSimulation` → `World::TickWhenPaused(elapsedSeconds)`（100 ms の上限で丸める前の実経過秒） |
| ストリーミング | `bBridgeAdvance` | 通常は `PostTick.Streaming`、ゲーム内ポーズ中は `TickSimulation` が直接 `StreamingSystem::Update` を呼ぶ。`Loading` によるポーズ中も読み込みが進む。 |
| 入力の評価 | ゲート外 | `ApplicationProcessor::Tick` → `UpdateInputFrame`（`PreUpdate` の前） |
| ハンドラの更新と Bridge 受信 | ゲート外 | `PreUpdate.HandlerUpdate` → `OnUpdate`。Bridge の受信はゲーム側の `DrainInbound` が処理する。 |
| スクリプトのフレーム前処理・後処理 | ゲート外 | `PreUpdate.ScriptBegin` → `BeginFrameMaintenance`、`PostUpdate.ScriptEnd` → `EndFrameMaintenance` |
| 気象の解決値の書き出しと近距離の雨 | ゲート外 | `TickSimulation` が `EnvironmentSystem::PublishTo`、`UpdateRainNear` の順に呼ぶ。 |
| 振動出力の更新 | ゲート外 | `TickSimulationAndHaptics` → `UpdateHapticsFrame` → `UpdateHaptics` に実経過秒を渡す。 |
| 描画の同期 | ゲート外 | `ApplicationProcessor::Tick` → `World::SyncToSceneView` |
| 空間検索の境界更新 | ゲート外 | `ApplicationProcessor::Tick` → `SceneQuery::Update`（描画同期後） |
| モジュールの通常更新 | ゲート外 | `PostUpdate.ModuleTick` → `ModuleRegistry::TickAll` |
| UI（`UiSystem`） | ゲート外（実装予定） | `G15-GR68-UI-CORE` で追加する。ゲーム内ポーズ中もゲートの外で実時間で更新する。追加時にこの欄に実装の場所を記す。 |
| ハンドラの描画前処理 | ゲート外 | `ApplicationProcessor::Tick` → `IApplicationHandler::OnPreRender`（`PostUpdate` の後） |

今後追加する `AIModule` の `FixedTick`（GR21〜GR24）は固定ステップとともに停止する。音のダッキング（GR73）は `OnPauseStateChanged` で受ける。それぞれの実装と同じ変更でこの表に行を足す。

## フレームの順序

`ApplicationProcessor` のフレーム段は次の順に走る。各段内に記したシステムは登録順で実行される。World の段は Application の段の間に呼ばれる。

1. gate 外で入力を評価し、`PreUpdate` の `PreUpdate.HandlerUpdate`（骨格資産の反映と Handler の `OnUpdate`）→ `PreUpdate.ScriptBegin` を実行する。
2. `bBridgeAdvance` が開いていれば `PreSimulation.GameModeStateMachine` を実行する。続いて `bWorldAdvance` が開いていれば `World::Tick`（`Input`〜`PoseFinalize`）→ `Vegetation::Tick` → `PostTick.Particles` → `PostTick.Environment` → `PostTick.Streaming` の順に実行する。ゲーム内ポーズ中はこの通常更新に代えて `World::TickWhenPaused` → `StreamingSystem::Update` の順に呼ぶ。
3. `bWorldAdvance` が開いていれば固定ステップを 0 回以上実行する。各回はワールドの `FixedBegin`（変換の確定・補間準備と `Physics.FixedPre`）→ `FixedModulePre.PreFixedTick` → ワールドの `FixedTick`（`Physics.CharacterFixed`、`Physics.FixedStep` 等）→ `FixedModulePost.FixedTick` → ワールドの `FixedEnd`（変換の確定・補間履歴の記録・破棄予約の回収）の順。AI は `FixedModulePre.PreFixedTick` で操舵速度を確定し、固定駆動の CharacterBody は同じ歩の `Physics.CharacterFixed` でその値を消費する。可変駆動では手順 2 の Movement または PoseFinalize で直近の固定歩の値を使う。
4. `bWorldAdvance` が開いていれば、固定ステップが 0 回でも `World::LateTick`（`PostPhysics` → `Camera` → `PreRender`）を 1 回実行する。ゲート外で `UpdateWorldFieldInteractors` → `PublishTo` → `UpdateRainNear` を呼び、同じ `bWorldAdvance` が開いていれば `PostLateTick.ModuleLateTick` → `PostLateTick.HandlerLateUpdate` を実行する。レジストリが `Running` のとき、モジュールは登録順に呼ばれる。`AIModule::LateTick` は直近の運動結果を Animator へ公開し、操舵の描画が有効ならデバッグ描画を積む。
5. gate 外で振動更新、World の描画同期、SceneQuery の通常 world-AABB 更新を行い、`PostUpdate.ScriptEnd` → `PostUpdate.ModuleTick`（通常の `TickAll`）→ Handler の `OnPreRender` → 描画へ進む。Handler はフレーム内で共有所有し、callback による差し替えでもそのフレームの対象を生存させる。

`PostTick.Streaming` は焦点を受けたセルの状態遷移、Prepare の仕事の投入と完了の回収、予算つきのコミットとアンロードを GameThread で進める。
マニフェストが無ければ何もしない。

`PostLateTick.ModuleLateTick` の `GameplayModule::LateTick` は攻撃判定を 1 フレームに 1 回評価する。`CombatRuntime::Evaluate` の先頭で `World::UpdateWorldTransforms` を行い、姿勢評価・固定ステップ・PostPhysics のソケット追従後のシミュレーションの Transform（`GetWorldTransform`）を読む。
時間は `TimeSystem::GetFrameTimes().World` を使い、ヒットストップに追従する。Module `LateTick` の引数の delta は Unscaled のため攻撃判定には使わない。
配送は hitbox の登録順 → HurtboxSet の登録順 → 部位の番号で行い、候補ごとに配送直前に世代付きハンドルを再検証して `OnHit`（Hitbox）→ `OnHurt`（HurtboxSet）→ `OnAnyHit`（モジュール）の順に通知する。
剣の描画・トレイルは描画用に補間された Transform を読む。

通常 world-AABB の更新と、GR08 の物理公開 snapshot / 明示 `RefreshDynamicSnapshot` は別の更新であり、描画同期後に物理 snapshot が暗黙 refresh されるとは扱わない。

## 最終カメラとG14/G16の受渡し

- 最終のカメラの確定点は `PostLateTick` の Module `LateTick` の後に呼ばれる Handler の `OnLateUpdate`。現在の Game カメラの最終 proxy は `GameApplicationHandler::OnLateUpdate` の slot から確定する。ロードマップの「`IModule::LateTick` を最終のカメラの確定点」とする記述は採らない。
- G14 CameraDirector の配置は未決の G14-S3 に残す。どこに所有しても候補・intent の評価と最終 commit を分け、当フレームの最終結果は Game の `OnLateUpdate` の確定境界で一度だけ反映する。Module の `LateTick` で準備しても、描画同期後の通常 Module Tick から別の camera を上書きする方式にはしない。
- G16 listener は最終 commit 後の同じフレームの camera snapshot を読む。現在の順序なら、その後の通常 Module Tick か将来追加する明示的な確定通知が候補となる。Module の `LateTick` を最終カメラの読み取り点にしない。停止中も通常のモジュール更新でリスナーを解決する。
- GR73 では、音のリスナーは Handler の `OnLateUpdate` 後に動く通常の Module の `TickAll` で、同じフレームの最終カメラを読む。
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
