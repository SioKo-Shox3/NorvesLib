# G1実装と検証範囲

2026-10-02 UTC。対象はロードマップG1のGR01・GR03・GR04・GR08。GR02はG4であり、この一覧に含めない。

## 到達点

G1の予定機能を実装し、クラウドで実行できる純処理と実Physics query kernelを検証した。Windows必須ビルドを外して非描画作業を進める方針に従い、未実行の統合・実機確認を分けてG2へ進む。Game全体・既存描画・全受入れの合格宣言ではない。

| GR | 実装済みの範囲 | 契約の正本 |
|---|---|---|
| GR01 | 8更新群/優先度/mask、一回収集、追加翌frameと削除無効化、Late配線、群境界の親子world姿勢確定、評価済みjoint姿勢、SpringArmとGame最終camera | [TickStages.md](TickStages.md) |
| GR03 | action/axis/複数binding/context、UI消費と解除、Tap/DoubleTap/Hold、軸曲線と視点単位、Raw mouse/cursor/focus、JSON/Store/rebind/debug経路 | [InputFoundation.md](InputFoundation.md) |
| GR04 | XInput交換境界、4slot、低頻度未接続probe、振動効果/合成/JSON/停止、使用中入力方式と通知 | [InputFoundation.md](InputFoundation.md) |
| GR08 | layer/mask/UserData、7種query/filter/複数hit/batch、明示snapshot更新、候補除外、旧API転送、Capsule受入れ | [PhysicsQueries.md](PhysicsQueries.md) |

Math/Curves.hは入力と振動で共有する最小区分線形・イージング基盤。G1以降に機能を重複実装せず拡張する。G1で追加したCore Publicヘッダ29件はPUBLIC_HEADERSへ登録済み。

## 実行済みの最終portable確認

C++23、g++ -O2、assertを有効にした実ソースへの直接リンクで34件成功、実行失敗0。CoreのWindows依存を隠す代替実装は使用していない。CMake/CTestやWindows実行ではなく、section GCで未使用の依存部分を除いた範囲を含む。

実行時の基準コミットは0c1f4e90d70459ea029682d88bf34910da8860c6。以後の実装変更はWorldの群境界同期であり、当時の実World試験は未実行だった。2026-11-03のWindows Debugでの結果は後節に記す。群/dispatchの純helperはこの変更後も最適化構成とASan/UBSanで再実行成功した。

- GR01（2件）：TickGroupConfigurationTest、TickDispatchTest
- GR03（17件）：InputStateReleaseTest、InputButtonStateTest、InputAxisMathTest、InputArmedStateTest、InputBindingTypesTest、GamepadInputStateTest、InputActionSettingsTest、InputActionRuntimeTest、RawInputStateTest、InputBindingNamesTest、InputRebindCaptureStateTest、AbsoluteMouseTrackingTest、InputDebugOverlayStateTest、GameInputRebindTypesTest、CameraInputResetTest、RawMouseMotionTrackerTest、WindowsKeyRepeatGateTest
- GR04（6件）：GamepadStateNormalizeTest、XInputDevicePollingTest、HapticsMixerTest、XInputVibrationStateTest、HapticsPlaybackTimeTest、ActiveDeviceKindTest
- 共通（1件）：CurvesTest
- GR08（8件）：GeometrySeparationTest、GeometrySweepTest、PhysicsQueryTypesTest、PhysicsProxyQueryTest、PhysicsProxyVisitorTest、PhysicsRayPrecisionTest、PhysicsQueryPruningTest、PhysicsLegacyQueryTest

Windowsという名称を含む純state試験も、OSから実入力を受けた検証ではない。MEMBER名変更によるobject compileは成功したが、完全な既存bundleリンクの合格を意味しない。

### 重点的に確認した数値と境界

- queryの新旧4,000件：既定Layerの混在形状で全field/順序、同距離ray、旧Overlapの法線反転。旧無filterのLayer0検索と新bitmaskのLayer0除外は意図した差として別途固定
- 候補除外7,000件：全探索＋独立sortと7種queryを比較。各種類300件以上の確定hitを含む。検証失敗、容量、未収束、巨大bounds fallbackも確認
- Capsule sweep 10,000件：既存接触判定と二分法による比較。反復上限は確定hitへ変えない
- Capsuleの面/辺/角、横向き、Capsule同士、初期接触/侵入：解析距離と外向きNormal、Depth、初期flagを確認。深いBox侵入は近似であり任意の最小並進ベクトルを保証しない
- 曲線：Curves/InputAxis/HapticsMixer/PlaybackTimeを通常・O2/NDEBUG（assert有効）・ASan/UBSanで実行。極大floatから小値への内部key/端点、非有限値、既存入力/振動の従来式との一致を確認
- これらのASan/UBSan実行ではLeakSanitizerを無効にした。メモリリーク検査の合格は主張しない

## 未実行・未確認

以前はThread/Containers経由のWindows.h欠如でcompileが停止していたが、2026-11-03にはWindows・Debug（MSVC、Visual Studio 17 2022）で下表の対象試験をcompile・実行できた。基準はHEADのコミット 09b4fb9b464330c5a0cf64cb306168c1fe24fd96。指定した5つの試験bundleのビルドは終了コード0、CTestは更新群・Engine等の13件中13件、入力・振動の36件中36件がpassした。FixedStepSchedulerTestはbundle内でcompileされたが、今回のCTestでは実行していない。

| GR | 今回の統合試験結果と未確認の範囲 |
|---|---|
| GR01 | TickGroupConfigurationTest、WorldTickGroupTest（追加の全8群・親子孫/疎な群/空Lateを含む）、ApplicationFixedStepPipelineTest、SkeletalAnimationSamplingTest、SpringArmComponentTest、TickContractTableTestがWindows Debugで2026-11-03にpass（コミット 09b4fb9b464330c5a0cf64cb306168c1fe24fd96）。群の契約は`Docs/Architecture/TickStages.md`を正とし、表の漏れはTickContractTableTestが止める。詳細実行でも`TICK_CONTRACT_TABLE result=pass types=20`を確認（1件中1件pass） |
| GR03 | 実Mapper/Router（InputRouterTest、InputRoutingExtensionTest、InputActionMapTest）、binding所有/JSON/保存（InputBindingSetTest、InputBindingJsonTest、InputBindingPersistenceTest、WorkingDirectoryInputBindingStoreTest）、Game設定（GameInputSettingsTest）、focus/frame（InputFocusPipelineTest、InputFramePipelineTest、RawMouseMotionTrackerTest、WindowsKeyRepeatGateTest）、capture（InputRebindCaptureStateTest、InputRebindCaptureManagerTest、GameInputRebindTypesTest）、overlay統合（InputDebugOverlayStateTest、InputDebugOverlayControllerTest）がWindows Debugで2026-11-03にpass（コミット 09b4fb9b464330c5a0cf64cb306168c1fe24fd96）。Game起動時の設定ロード・OS入力との実接続、実ユーザーの保存先でのfile IO成功や性能の実測は未確認 |
| GR04 | XInputDevicePollingTest、XInputDeviceIntegrationTest、GamepadStateNormalizeTest、InputDeviceLifecycleTest、HapticsMixerTest、XInputVibrationStateTest、HapticsPlaybackTimeTest、HapticsServiceTest、HapticsJsonTest、HapticsFramePipelineTest、ActiveDeviceKindTest、ActiveDeviceKindIntegrationTestがWindows Debugで2026-11-03にpass（コミット 09b4fb9b464330c5a0cf64cb306168c1fe24fd96）。device/API/providerのfakeを使う統合試験であり、実pad接続/振動は未確認 |
| GR08 | `Docs/Architecture/PhysicsQueries.md`を正とする。GR08の試験は今回のverifyに含めていない |

以下も未確認として残す。

- Gameフルビルドと完全bundle/CTest
- 着手前の既存失敗baseline再現、M9スモーク、TICK_STAGE_SMOKEの実ログ、起動画面3視点の画像確認
- 実WindowsのAlt+Tab、Raw mouseの画面端越え、cursor固定/解除、実pad接続/振動
- 群境界の追加階層走査コスト、実規模の物理query性能
- GitHubの対象コミットでstatus一覧が空の場合、CI合格とは扱わない

これらの未実行事項を非描画G2の開始条件へ戻さない。一方、将来GPU/描画変更や実機の保証をする際は該当する確認を別途実行する。

## 代表的なportable再現コマンド

リポジトリルートで実行する。実行ファイルは一時領域へ出し、既存CMakeのbundle登録は増やさない。

```sh
g++ -std=c++23 -O2 -UNDEBUG -I Library/Core/Public Test/Core/Input/InputActionRuntimeTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/g1-runtime && /tmp/g1-runtime

g++ -std=c++23 -O2 -UNDEBUG -I Library/Core/Public Test/Core/Math/CurvesTest.cpp -o /tmp/g1-curves && /tmp/g1-curves

g++ -std=c++23 -O2 -UNDEBUG -I Library/Core/Public Test/Core/Math/GeometrySweepTest.cpp Library/Core/Private/Math/GeometryIntersection.cpp -o /tmp/g1-sweep && /tmp/g1-sweep

g++ -std=c++23 -O2 -UNDEBUG -ffunction-sections -fdata-sections -I Library/Core/Public -I Library/Modules/Physics/Private Test/Modules/Physics/PhysicsLegacyQueryTest.cpp Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp Library/Core/Private/Math/GeometryIntersection.cpp -Wl,--gc-sections -o /tmp/g1-legacy && /tmp/g1-legacy
```
