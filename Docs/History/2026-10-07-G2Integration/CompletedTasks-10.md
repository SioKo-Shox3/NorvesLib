- paths: Library/Core/Public/Animation, Library/Core/Private/Animation, Library/Core/Public/Component/SkinnedMeshComponent.h, Library/Core/Private/Component/SkinnedMeshComponent.cpp, Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: Owner子階層は既存Worldの変換確定境界が鮮度の前提。

## G1-GR01-P5: SpringArmとGameカメラを物理後へ移す
- status: done
- done-when: Camera群へ割当、GameHandlerのone-shot Delegateでlate proxy確定、weak寿命とID再解決で途中破棄を拒否、既存初期camera同期を保持する。
- verify: 既存SpringArmComponentTestへ群/固定0・1・2step/child変換/途中削除/one-shot寿命ケース追加（Windows未実行）。
- stop-when: callback寿命未解消、mode停止時の誤更新、初期同期破壊、blockingが残る場合。
- paths: Library/Core/Private/Component/SpringArmComponent.cpp, Game/CameraLateUpdate.h, Game/GameApplicationHandler.h, Game/GameApplicationHandler.cpp, Game/GameModes/Rendering3DTest/Rendering3DTestData.h, Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, Test/Core/Object/SpringArmComponentTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: Game専用single camera slotを使い、Coreのstate-machineや依存graphは広げない。

## G1-GR03-P1: 入力状態の一括解除と復帰時の座標基準を追加する
- status: done
- done-when: 全キー/ボタン解除、同frame押下後もReleasedを一frame保持、累積クリア、次の絶対位置の再基準化、通常遷移の維持。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputStateReleaseTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-release && /tmp/input-release
- stop-when: 通常入力回帰、release永続、OS依存の持込み、blocking未解消。
- paths: Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/InputStateReleaseTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Window/InputSystemへのfocus配線は後続。S4=a/S5=a/S8=aは作者承認済み。

## G1-GR03-P2: 入力軸の応答曲線と視点の時間単位を分離する
- status: done
- done-when: 1D/2D radial deadzoneとLinear/Power/Expo、方向/単調/長さ、mouse変位とstick速度のdt分離、無効値の安全な拒否。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputAxisMathTest.cpp -o /tmp/input-axis && /tmp/input-axis
- stop-when: 方向を曲げる成分別曲線、mouseへのdt適用、NaN漏れ、blocking未解消。
- paths: Library/Core/Public/Input/InputAxisMath.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputAxisMathTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 後続Mapperが使用する純ロジック。EngineやOS入力との接続は未完。

## G1-GR03-P3: ボタンの時間遷移と固定更新用の押下保持を追加する
- status: done
- done-when: 単調実時間でHold/Tap/DoubleTapを判定、同frameedgeを保持、fixedPressをconsumeまで維持、Cancelで遅延発火を消す。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputButtonStateTest.cpp -o /tmp/input-button && /tmp/input-button
- stop-when: 時刻/edge不整合、不正入力で状態変更、Cancel後の発火、押下消失、blocking未解消。
- paths: Library/Core/Public/Input/InputButtonState.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputButtonStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: binding集約後のdownを受ける純ロジック。Mapper/OS通知への接続は後続。

## G1-GR03-P4A: 入力解除の世代で古い押下許可を失効させる
- status: done
- done-when: 到達Pressedだけarmed、release serialで消費された解除と同frame再押下を検出、Repeat/修飾/Resetを安全に扱う。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputArmedStateTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-armed && /tmp/input-armed
- stop-when: UI消費入力で再armed、既存state回帰、OS/Delegate購読持込み、blocking未解消。
- paths: Library/Core/Public/Input/InputState.h, Library/Core/Public/Input/InputArmedState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/CMakeLists.txt, Test/Core/Input, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Router/Mapper接続前のkernel。実InputStateとcallback欠落を使った状態検証であり、ImGui実機の合格ではない。

## G1-GR03-P4B: 入力元とアクションbindingの値型契約を定義する
- status: done
- done-when: backend非依存pad stateとphysical source/bindingを宣言し、code/slot/target/modifier/nonfiniteを検証、変位と正規化の暗黙混在を拒否。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputBindingTypesTest.cpp -o /tmp/input-bindings && /tmp/input-bindings
- stop-when: WindowsSDK依存、不正値accept、既存名衝突、backend動作済み扱い、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputBindingTypes.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingTypesTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: XInput adapter/Mapper/JSONは未接続。4slot選定はS5=a承認に基づく。

## G1-GR03-P4C: パッド入力の正本と解除履歴を入力状態へ接続する
- status: done
- done-when: Pad snapshotを検証後一括適用、frame edge/解除serial/neutralと接続維持を実装し、armedのUI消費/Repeat失効をPadへ拡張する。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/GamepadInputStateTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/pad-state && /tmp/pad-state
- stop-when: 半端な更新、切断後down/armed、KBM回帰、SDK依存、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Public/Input/InputArmedState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: XInput polling/振動/InputSystem配送は未接続。Mapper用の正本契約を先に満たす。

## G1-GR03-P4D: アクションとコンテキストの設定をIdentityで所有する
- status: done
- done-when: 純settings検証とIdentity設定所有を実装し、不正/重複/unknownの非変更、deep copy、空unbind、借用viewの寿命を契約化する。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputActionSettingsTest.cpp -o /tmp/input-settings && /tmp/input-settings
- verify: InputBindingSetTestはWindowsのLoggerSinkTest bundleで実行（現環境では未実行）。
- stop-when: 無効変更の部分適用、長期pointer、String key、OS設定変更混入、blocking未解消。
- paths: Library/Core/Public/Application/CursorMode.h, Library/Core/Public/Input/InputActionSettings.h, Library/Core/Public/Input/InputBindingSet.h, Library/Core/Private/Input/InputBindingSet.cpp, Library/Core/CMakeLists.txt, Test/Core/Input, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Mapper/JSON/OSカーソルは未接続。

## G1-GR03-P4E: バインドからボタンと軸のアクション値を評価する
- status: done
- done-when: 到達入力/armedとbindingからOR button、curve軸、相対変位とrate時間単位を評価し、span非保持、cancel/fixed latch、不正入力非変更を守る。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputActionRuntimeTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-runtime && /tmp/input-runtime
- stop-when: UI迂回、span長期保持、dt混同、部分適用、blocking未解消。
- paths: Library/Core/Public/Input/InputActionRuntime.h, Library/Core/Public/Input/InputAxisMath.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputActionRuntimeTest.cpp, Test/Core/Input/InputAxisMathTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Identity/Router/Engineの接着は後続Mapper。

## G1-GR03-P4F: Raw・パッド・解除の入力配送を接続する
- status: done
- done-when: raw相対移動と水平wheelを独立正本へ保持しRouterへ配送、pad snapshotを検証後更新して新Pressed→Released順、接続/全解除をconsume不能通知で届ける。絶対mouseの初回再基準化とevent deltaを一致させる。
- verify: g++で実InputState.cppのRawInputStateTestと既存KBM/Pad/Runtime回帰。InputRoutingExtensionTestはLoggerSinkTestへ登録、Windows依存で未実行なら静的レビューとして明示。
- stop-when: 既存camera event互換性破壊、invalid入力の部分更新、UIのconsume迂回、切断取消し前のrelease、blocking未解消。
- paths: Library/Core/Public/Input/{InputTypes.h,InputState.h,InputSystem.h,IInputController.h,InputRouter.h}, Library/Core/Private/Input/{InputState.cpp,InputSystem.cpp,InputRouter.cpp}, Test/Core/Input/{RawInputStateTest.cpp,InputRoutingExtensionTest.cpp,CMakeLists.txt}, Test/Core/Logging/CMakeLists.txt, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: OS Raw/XInput供給とMapper/Engine接着は次段。新raw/pad配送は既存KBMと同じ優先度契約。

## G1-GR03-P4G: アクションMapperとコンテキストをRouterへ接続する
- status: done
- done-when: 設定を所有してcompileするInputMapperをIInputControllerとして接続し、top contextだけ評価、切替/focus/resetでCancel、UI到達armed、fixed press消費、相対入力、Pad切断を処理する。
- verify: InputActionMapTestをLoggerSinkTestへ登録。実portable runtime/state群はg++回帰。Identity/Router依存のMapperはWindows.hによる未実行を明記し、所有/寿命/公開APIの独立レビューを通す。
- stop-when: borrowed設定pointer保持、UI consumed押下の復活、context越しheld継続、解放後Router pointer、blocking未解消。
- paths: Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Engine/ApplicationProcessorとOS、JSON/rebind captureは後続。既存camera controllerへイベントを透過する。

## G1-GR03-P4H: Engineとフレーム処理へMapperを配線する
- status: done
- done-when: Engineが正本/Routerより短い寿命でMapperを所有し、message前BeginFrameとOnUpdate前Update、Run終了取消とShutdown先行Detachを接続する。入力時間はsteady_clock、dtはgame clamp前を使う。
- verify: InputFramePipelineTestを既存ApplicationFixedStepPipelineTest束へ追加し、所有/配送/実時間/固定press/終了取消を検証。現在Windows依存で未実行なら静的確認と明示。portable入力群/FixedStepScheduler回帰をg++で実行。
- stop-when: 入力更新がOnUpdateより後、clamped dt使用、正本/Router破棄後のMapper参照、起動画面変更、blocking未解消。
- paths: Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/InputFramePipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Game既定JSON、rebindとOS供給/cursor/focus通知は次段。Mapper未設定の旧camera経路を維持。

## G1-GR03-P5A: bindings.v1設定JSONと差分を読み書きする
- status: done
- done-when: 物理source/codeの安定した名前と安全なwide数値変換、既定JSONの検証読込/書出、既定＋ユーザー差分の全体適用/差分書出を実装。未知field/actionは警告し無視、既知不正/破損/version違いは既定へ退避する。
- verify: portable InputBindingNamesTestで全code/source往復と数値境界をg++通常/NDEBUG/ASanUBSan。InputBindingJsonTestを既存bundleへ登録しroundtrip/unknown/invalid/empty-unbind/alias-fallbackを検証（Windows依存で未実行なら静的確認を明示）。
- stop-when: cast前の検証なし、hashをJSON number化、borrowed JSON view保存、partial override、破損設定で既定破壊、blocking未解消。
- paths: Library/Core/Public/Input/InputBindingNames.h, Library/Core/Public/Input/InputBindingJson.h, Library/Core/Private/Input/InputBindingJson.cpp, Library/Core/Public/Text/JsonDocument.h, Library/Core/Private/Text/JsonDocument.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingNamesTest.cpp, Test/Core/Input/InputBindingJsonTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: FileStoreとGame load/save/rebind UI捕捉は次段。schemaはbindings.v1、識別子は文字列。JSON textは1MiB/深さ64以内。

## CORE-DELEGATE-IDENTITY: Delegateの解除対象を正しく識別する
- status: done
- done-when: member/lambdaの同closure型誤一致とfree function保存領域pointer比較を修正し、登録/複製/個別解除の反証試験を通す。
- verify: Delegate/MulticastDelegateの実コードで別instance/別method/copy/free functionを検証する。公開APIと寿命の独立レビュー必須。
- notes: GR03の設定保存を先に進め、focus等のevent基盤を増やす前に独立Taskとして処理。現在のMapperはRouter pointer解除、Camera slotはClearで回避している。

- stop-when: callableと比較情報の例外時不整合、別instance/別method誤一致、copyで解除handle消失、blocking未解消。
- paths: Library/Core/Public/Delegate/Delegate.h, Library/Core/Public/Delegate/MulticastDelegate.h, Test/Core/Delegate/DelegateIdentityTest.cpp, Test/Core/Delegate/DelegateCopyTest.cpp, Test/Core/Delegate/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, TASKS.md, PROGRESS.md
## G1-GR03-P5B: 入力設定のStoreと読込保存APIを接続する
- status: done
- done-when: IInputBindingStoreを介してmissing/loaded/errorを分離し、defaults＋user差分の読込結果と保存APIを提供。Windowsの暫定working-directory adapterは絶対pathを一度固定し、同directoryの排他tempへ全書込/flush後に置換して元ファイルを失敗時に保つ。
- verify: InputBindingPersistenceTestを既存bundleへ追加しFakeStoreでmissing/invalid/read error/save failure/差分roundtripを試験。WorkingDirectoryInputBindingStoreTestはWindowsの一時directory内だけでread/write/失敗時保持/cwd変更/サイズ上限を試験（現環境では未実行）。独立IO/寿命レビュー。
- stop-when: 既存targetを先にtruncate、unknown temp削除、CWD変更で保存先移動、既定不正でuser単独採用、startup自動上書き、blocking未解消。
- paths: Library/Core/Public/Input/IInputBindingStore.h, Library/Core/Public/Input/InputBindingPersistence.h, Library/Core/Private/Input/InputBindingPersistence.cpp, Library/Core/Private/Input/WorkingDirectoryInputBindingStore.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingPersistenceTest.cpp, Test/Core/Input/WorkingDirectoryInputBindingStoreTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, .gitignore, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: S8承認済み。GameのAsset JSON/初期化配線は次のP5Cで行い、起動時は読込だけにする。GR76ではStoreだけ差し替える。

## G1-GR03-P5C: Gameの既定入力設定を起動時にロードする
- status: done
- done-when: GameInputActions IDとAssets/Config/DefaultInputBindings.jsonを用意し、GameInputSettingsがAssetReader→Store差分→Mapperを接続。初期Debug contextはNormalで既存camera経路を維持。起動読込で自動保存しない。設定と初期contextはMapperへ一括反映する。
- verify: GameInputSettingsTestを既存bundleへ追加し、実default Asset/注入FakeStore/mapperで既定ロード/override/fallback/初期context/明示保存/失敗非変更を検証（Windows依存未実行なら静的レビューを明示）。JSON syntax/dataはPython確認、portable binding/runtime回帰。
- stop-when: Game-specific actionをCoreへ固定、起動時Locked化、既定/userファイルの勝手な書戻し、context設定の部分反映、blocking未解消。
- paths: Game/Input/GameInputActions.h, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Assets/Config/DefaultInputBindings.json, Game/GameApplicationHandler.h, Game/GameApplicationHandler.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: defaultsは暫定data。Gameplayは後の実game用、Rendering3DTest起動はDebug/Normal。rebind捕捉/OS供給は後続。

## G1-GR03-P6A: 入力取消を既存操作コントローラーへ接続する
- status: done
- done-when: OnInputResetをCameraInputCollector/MayaCameraController/LightController/PickingController/ImGuiへ接続し、pressed/drag/modifier/queued UI入力を取り消す。camera姿勢/light値/確定selectionは維持する。
- verify: 実CameraInputCollectorとInputStateのportable試験、既存controllerの操作再開/取消/確定selection維持のbundle試験。ImGuiはvendor APIの整合と静的レビュー。Windows未実行は区別。
- stop-when: resetでclick/select実行、確定selectionやcamera姿勢を消去、UIのqueued pressが残留、blocking未解消。
- paths: Game/Input/CameraInputCollector.h, Game/Input/PickingController.h, Game/Input/PickingController.cpp, Library/Core/Public/Input/MayaCameraController.h, Library/Core/Private/Input/MayaCameraController.cpp, Library/Core/Public/Input/LightController.h, Library/Core/Private/Input/LightController.cpp, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Input/CameraInputResetTest.cpp, Test/Core/Input/LegacyInputResetTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: OS focus通知自体は次段。既存のInputSystem::ReleaseAll→全Router controller取消に応答する。非focus解除で誤ったfocusイベントは作らない。

## G1-GR03-P6B: Windowsの入力フォーカスを取消経路へ接続する
- status: done
- done-when: IWindowの入力focus値/Delegate通知をWindows focus/activationへ接続。Processorが購読寿命を所有し、loss時にMapper停止＋正本/全controller取消、UI focus通知、handler通知はPumpMessages後に順序を保って配送する。非focus入力と復帰後の古いrepeatは注入しない。
- verify: 偽window＋実Engine/InputSystem/Router/Processorのloss/gain/重複/同batch復帰/handler再入/購読解除を既存Engine bundleへ追加。Windows native実行未確認を明示、公開API/寿命独立レビュー。
- stop-when: WM_KILLFOCUS内でGame handlerを呼ぶ、旧window購読残留、focus loss後held/analog継続、イベント順序喪失、callback例外が新focus処理からOSへ漏れる、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/WindowsApplication.cpp, Library/Core/Private/Platform/Windows/WindowsKeyRepeatGate.h, Test/Core/Engine/WindowsKeyRepeatGateTest.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Application/IWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/IInputController.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Private/Input/InputRouter.cpp, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Engine/InputFocusPipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Raw Input/カーソル制御/rebindは後続。既存IsActiveとkeyboard input focusを分ける。Game handler通知はboot完了後だけ購読し、shutdown時に先行解除する。

## G1-GR03-P6C: WindowsからRaw mouseと拡張ボタンを供給する
- status: done
- done-when: main windowが明示的にRaw mouseを登録/解除し、他のprocess内登録を上書きしない。WM_INPUTの相対/絶対motionを専用laneへ供給し、初回/device/focus/geometry変化を再seed。legacy button/wheelを二重注入せずX1/X2と水平wheel、signed座標を供給する。foreground WM_INPUTはDefWindowProc cleanupを保つ。
- verify: 実RawMouseMotionTrackerのrelative/absolute/multi-device/invalid/geometry/reset/bounded evictionを通常/O2/ASanUBSan、native Windows配線と登録所有の独立レビュー。Windows実行未確認を明示。
- stop-when: 他のRaw登録を奪う/解除、二重button/wheel、absolute初回jump、focus前のdelta残留、DefWindowProc cleanup欠落、blocking未解消。
- paths: Test/Core/Engine/InputFocusPipelineTest.cpp, Library/Core/Private/Platform/Windows/RawMouseMotionTracker.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Application/IWindow.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/RawMouseMotionTrackerTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Raw機能を利用できないときは初期化を警告しlegacy起動を維持。cursorの要求/有効modeとLocked時absolute delta抑止は次段で接続する。

## G1-GR03-P6D: カーソルmodeと絶対mouseの基準化を接続する
- status: done
- done-when: requested/effective cursor modeを分離してMapper→Processor→Windowへ同期。Windowsはfocus/visibility/minimizeに応じてClipと非表示を適用/解除し、move/size/DPIで矩形を更新。native失敗はfalseで返し成功扱いしない。Lockedでは絶対位置だけを供給しdeltaを抑止、mode/clip変更を再seed。Run終了/例外/Shutdown/DestroyでNormalを要求する。
- verify: 実InputStateの非累積/再seed/Raw・wheel・buttons維持をportable試験。Mapperのfocus非依存要求とProcessor同期/解除を既存統合試験へ追加。Windows native clip/表示は未検証として独立レビュー。
- stop-when: 非focus要求をNormalで上書き、ShowCursor counter変更、他window上のcursor形状上書き、locked絶対deltaの漏れ、mode切替で逆delta、native失敗を成功報告、blocking未解消。
- paths: Library/Core/Public/Application/IWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Input/AbsoluteMouseTrackingTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: IWindow GetCursorModeは最後に成功したmode。OS失敗時はfalseを扱い再試行する。入力取消やRaw/wheelの取消を絶対mouse基準化に混ぜない。既定Debug/Normalの表示は維持する。

## G1-GR03-P7A: リバインド設定をcontext stack維持で反映する
- status: done
- done-when: Mapperのcontext stackをIdentity順に維持して設定を一括再構築するAPIと、GameInputSettingsのaction bindings変更/個別既定復帰/全設定既定復帰を追加。成功時は旧操作を取消し、失敗時はMapperと保存対象Currentを両方維持。変更だけでStoreへ書き込まない。
- verify: Mapperのstack順/unknown context失敗/held取消とGameの更新/empty-unbind/alias/invalid/明示Save/既定復帰を既存bundle試験へ追加。所有/例外保証を独立レビュー、Windows依存未実行を明示。
- stop-when: 設定変更でMenuの下のGameplayを失う、失敗時の部分反映、借用binding参照を長期保存、暗黙保存、blocking未解消。
- paths: Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: captureは次段でこの更新口へ接続する。全既定復帰はbindingsだけでなく感度等のCurrent設定全体をDefaultsへ戻す。個別復帰はbindingsだけ。Storeのstartup UserStatusは読込履歴のまま。

## G1-GR03-P7B: 捕捉用の実pad sampleを取消状態と分離する
- status: done
- done-when: InputStateが最後に受理したprovider sampleと受理serialを保持し、ReleaseAllのneutral化で上書きしない。成功したInjectGamepadStateごとにbutton edge後のsample値イベントをDelegate/Routerへ配送し、neutral解除とanalogをcapture側が判別できる。
- verify: 実InputStateのsample履歴/serial/invalid非変更/Reset区別/全slotをportable試験。配送の同値sample/UIconsume/順序/Reset非通知を既存bundle試験へ追加し独立レビュー。
- stop-when: Resetを物理releaseとして通知、invalidで履歴変更、同値provider sampleを省略、callbackへ借用stateを長期保持、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/IInputController.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Private/Input/InputRouter.cpp, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 最後のsampleは過去の受理値であって現在操作状態ではない。gameplayは従来の正本/Mapperを使う。GR04のproviderもこの注入口を利用する。capture本体は後続。

## G1-GR03-P7C: 物理入力captureと解除待ちを純kernel化する
- status: done
- done-when: source mask、key/mouse/pad/axis/trigger/relative入力、修飾chord、Escape中止、物理neutral/relative静止待ちを値状態で実装。開始時のheldや接続直後sampleを捕捉せず、UIが消費した入力や古いsampleから候補を作らない。結果はsource/modifiers/directionの値で返す。
- verify: 実InputStateとkernelのdigital/chord/initial-held/UIconsume/sample freshness/analog hysteresis/disconnect/quiet-frame/invalid/再利用を通常/O2/ASanUBSanとbundle compileで検証し独立レビュー。
- stop-when: 初期heldの誤捕捉、Escapeをbinding化、解除前にfinished、Resetを物理neutralと誤認、入力消費をpollingで迂回、invalidで状態破壊、blocking未解消。
- paths: Library/Core/Public/Input/InputRebindCaptureState.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/RawInputStateTest.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputRebindCaptureStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: kernelはIdentity/Router/Store/OSを所有せず、同じ正本を各feed/Advanceへ渡す。Managerと設定反映は次段。符号やscaleの最終binding policyはsource結果を利用する側が決める。

## G1-GR03-P7D: capture managerを入力実行経路へ接続する
- status: done
- done-when: 同じSystem/Router/Mapperに単一ownerで常設登録し、request ID付きBegin/Cancel/resultを提供。capture中は全通常eventとMapper polling/fixedPress/cursor要求を抑止し、外部reset/focus lossで中止。配送外Advanceのreset後に解除して結果を公開。Engineの所有/寿命とProcessorのframe/終了へ接続する。
- verify: 実System/Router/Mapperの停止/復帰/古いrequest/owner/不正配線/初期held/analog/focus/reset試験を既存bundleへ追加。portable kernel/state回帰と独立レビュー、Windows統合未実行を明記。
- stop-when: capture中のgameplay漏れ、登録寿命不整合、reset再入、解除前の結果公開、古いrequestによる中止、blocking未解消。
- paths: Library/Core/Public/Input/InputRebindCaptureManager.h, Library/Core/Private/Input/InputRebindCaptureManager.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputRebindCaptureManagerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Game側の対象action/slotと設定revision付き適用は別段。request結果だけで設定を暗黙変更/保存しない。Detachは所有終了時に配送外で呼び、借用先の寿命を超えない。

## G1-GR03-P7E: 捕捉結果をGameの入力設定へ安全に反映する
- status: done
- done-when: context/action/slot/出力設定/revisionを不透明な値requestへ保持し、同じmanager要求とMapper、変更前revisionを検証してP7Aへ反映。末尾追加/置換/中止/待機/古い結果/二重適用を区別。managerや設定owner再生成でもIDを再利用せず、暗黙保存しない。
- verify: 純helperの型/方向/modifier/invalid非変更をportable通常/O2/ASanUBSan/bundleで実行。実Game設定＋managerの追加/置換/古いrevision/別owner/中止/保存試験を既存bundleへ追加。独立レビュー、Windows統合未実行を明記。
- stop-when: 古い/別ownerの結果を適用、失敗時にCurrent/Mapper部分反映、借用pointer長期保持、ID wrap、暗黙保存、blocking未解消。
- paths: Game/Input/GameInputRebindTypes.h, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Library/Core/Public/Input/InputRebindCaptureManager.h, Library/Core/Private/Input/InputRebindCaptureManager.cpp, Test/Core/Input/GameInputRebindTypesTest.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Test/Core/Input/InputRebindCaptureManagerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 出力component/gain/反転は呼出側が明示し、既存slotの意図を勝手に変換しない。UIはGR68、保存は既存Saveのみ。

## G1-GR03-P8: ImGuiのカーソル解除とGame入力マスクを接続する
- status: done
- done-when: --imgui有効時だけF1でcursorをNormalへ一時解除し、UI受取後のGame Router入力とMapper polling/fixedを停止。配送外で要求反映/旧操作resetし、再F1で元context要求へ戻す。capture最優先と寿命/Run再開を保ち、既定起動画面を変えない。
- verify: 純toggle stateのrepeat/押下世代/enable/変更待ち/Resetを実テスト。Coreのmode-mask/cursor/legacy停止/復帰/capture優先/別owner/終了・再開を既存bundleへ追加して独立レビュー。Windows/ImGui実機は未検証。
- stop-when: cursor解除中のGame入力漏れ、UI自体へ入力が届かない、F1 repeatで反転、captureと抑止理由の競合、callback内の全reset/登録変更、寿命不整合、blocking未解消。
- paths: Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputDebugOverlayState.h, Library/Core/Public/Input/InputDebugOverlayController.h, Library/Core/Private/Input/InputDebugOverlayController.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/CMakeLists.txt, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Input/InputDebugOverlayStateTest.cpp, Test/Core/Input/InputDebugOverlayControllerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: input context stack/JSONは変更しない。UIの描画内容も変更しない。ImGuiはF1だけ予約透過し、controllerはOverlayより下/Gameより上の常設順位を使う。

## G1-GR04-P1: XInputの入出力値を安全に正規化する
- status: done
- done-when: native成功packetの整数値を独自GamepadStateへ非対称軸正規化/trigger分率/定義button maskで変換し、左右motorの0..1を検証後にuint16へ一括量子化。deadzoneは適用せず、invalid motor値は出力保持。
- verify: 全軸値65536/trigger256の範囲・単調性・端点、全channel/予約bit/packet/実InputState受理、motor誤差とinvalid非変更を通常/O2/ASanUBSan/bundleで実行して独立レビュー。
- stop-when: -32768のoverflow/符号反転、範囲外出力、予約bit混入、deadzone二重適用、invalidの部分反映、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/XInputStateConversion.h, Library/Core/CMakeLists.txt, Test/Core/Input/GamepadStateNormalizeTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: native API/polling/接続/非focus配送とhaptics serviceは後続。API定義はMicrosoft一次資料を確認済み。

## G1-GR04-P2: 背景と復帰時のpad sampleを通常入力から分離する
- status: done
- done-when: Live/Baseline/Backgroundを導入し、実sample履歴を保持したまま非Liveの通常button通知/Pressedを抑止。Backgroundの操作正本はneutral、Baselineは物理状態へ同期。captureは非Liveを基準化だけに使い、初回heldを誤捕捉しない。invalid mode/stateで部分更新しない。
- verify: 実InputStateとkernelのmode・held・解除世代・sample履歴・invalid非変更を通常/O2/ASanUBSan/bundle。実System/Router/Mapperの通知/復帰held試験を既存bundleへ追加し独立レビュー、Windows統合未実行を明記。
- stop-when: 背景で操作値が残る、物理sampleを偽neutral化、Baselineから新規Pressed/captureが出る、invalidの部分更新、既定Live回帰、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputRebindCaptureState.h, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/InputRebindCaptureStateTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 非Live配送はfocus Cancelの代用ではない。実providerとEngineのfocus/ポーリング接続は後続で行う。

## G1-GR04-P3: 差替APIでXInputのポーリング状態を実装する
- status: done
- done-when: SDK非依存のAPI/sink境界で4slotを読み、接続済み毎frame/未接続低頻度round-robin、同packet配送、接続/切断/error退避、background/復帰baseline、sink拒否再試行、単調clockを処理する。API参照以外を長期借用しない。
- verify: Fake APIと実InputState sinkによる初期probe/周期/連続入力/再接続/error/拒否/Mode/invalid clock/Shutdownを通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: 同frame二重poll、欠番slotの放置、未接続の高頻度poll、背景Live注入、復帰held再押下、拒否を配送済み扱い、clock不正で状態更新、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/IXInputApi.h, Library/Core/Private/Platform/Windows/XInputPollingState.h, Library/Core/CMakeLists.txt, Test/Core/Input/XInputDevicePollingTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は入力読取り状態。native adapter/Engine接続と振動は後続。sinkは呼出中だけ、APIはstateより長命で使う。

## G1-GR04-P4: XInputデバイスを実APIと入力正本へ接続する
- status: done
- done-when: Windows APIを明示field変換するadapterと、APIを所有するXInputDeviceを実装。null APIを拒否し、Initialize/Shutdown/焦点/時刻付きpollをIInputDeviceへ接続、InputSystem注入は同期間の借用だけ。Platform factoryは未初期化deviceを返す。
- verify: 公開/非SDK header syntax、P3 portable回帰、実Systemでfake APIの所有/拒否/再初期化/背景/復帰試験を既存bundleへ追加。Windows APIと統合実行の限界を記録し独立レビュー。
- stop-when: null参照、APIより長い借用、copy/moveで参照破壊、入力systemの長期保持、失敗packet使用、既存IInputDevice実装の破壊、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Public/Platform/PlatformInputDevices.h, Library/Core/Private/Platform/Windows/XInputDevice.h, Library/Core/Private/Platform/Windows/XInputDevice.cpp, Library/Core/Private/Platform/Windows/WindowsXInputApi.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/XInputDeviceIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Engine登録/frame呼出しは次段。読み取り専用で、振動出力はhaptics接続時に追加する。

## G1-GR04-P5: 入力デバイスの所有とフレーム寿命をEngineへ接続する
- status: done
- done-when: Engineがdeviceを所有し単一pad providerを保証。開始の部分失敗を逆順停止、終了/例外/再Runを扱い、失敗停止は再試行対象に残す。focus message後/Mapper更新前に単調時刻でpollし、標準padはhandler設定後に未登録時だけ生成。停止は即時操作取消と安全地点でのlegacy reset通知を行う。
- verify: fake deviceの所有/順序/再入拒否/開始失敗/停止例外再試行/clock非変更、実Runのmessage→poll→handlerと例外cleanupを既存Engine bundleへ追加。既存P3/P4の関連回帰、静的独立レビュー。Windows依存の未実行を明示。
- stop-when: device二重pad供給、partial開始放置、callback中破棄/登録、Shutdown例外で残り未停止、焦点喪失前のLive poll、終了後held残留、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputDebugOverlayController.cpp, Test/Core/Engine/InputDeviceLifecycleTest.cpp, Test/Core/Engine/InputFocusPipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: native実機とWindows統合実行は未検証。振動serviceは後続。Shutdown失敗のdeviceは所有/再停止義務を保持し、再初期化を拒否する。

## G1-GR04-P6: 振動包絡線の混合と成功ACKによる出力判定を実装する
- status: done
- done-when: 有限duration/昇順key/値0..1を検証し、線形補間/loop/期間終了を処理。slot内最高priorityのactive sampleだけをmaxまたはadd-clamp合成し、設定倍率を最後に適用。成功ACKだけを記録し、差分1/255未満は抑止するが最終zero/失敗再試行は省略しない。
- verify: 実純ロジックの境界/巨大経過時間/優先度/両合成/倍率/invalid非変更、ACK/失敗/最終zeroを通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: 非有限/範囲外出力、優先度無視、invalid部分反映、失敗ACK扱い、微小値の最終zero省略、blocking未解消。
- paths: Library/Core/Public/Input/HapticsEnvelopeMath.h, Library/Core/Public/Input/HapticsOutputState.h, Library/Core/CMakeLists.txt, Test/Core/Input/HapticsMixerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は純評価/混合/出力記録。voice所有/実device送信/JSON/Engineのfocus・pauseは後続。定義のspanは呼出中だけ借用する。

## G1-INPUT-SPAN: 入力層の借用viewを独自Spanへ統一する
- status: done
- done-when: GR03の入力runtime/settings/names/mapper/binding setで使用しているstd::spanをContainer::Spanへ揃え、所有/寿命/呼出互換を維持する。独自Spanのrange constructorを末尾型制約付きにし、(pointer, 0)がcount指定として一意に解決するようにする。
- verify: 変更header/runtimeの関連portable試験を通常/O2/ASanUBSan/bundle、Mapper/SetはWindows依存の実行限界を明記し公開API差分を独立レビュー。
- stop-when: view寿命の延長、空view/配列/const変換の回帰、規約を満たすための偽platform代替、blocking未解消。
- paths: Library/Core/Public/Container/Span.h, Test/Core/Input/InputActionRuntimeTest.cpp, Library/Core/Public/Input/InputActionRuntime.h, Library/Core/Public/Input/InputBindingNames.h, Library/Core/Public/Input/InputMapper.h, Library/Core/Public/Input/InputActionSettings.h, Library/Core/Private/Input/InputBindingSet.cpp, Library/Core/Private/Input/InputMapper.cpp, TASKS.md, PROGRESS.md
- notes: GR04-P6レビューで独自Span規約との不一致を確認。既存他領域の一括置換は行わず、今回の入力整備で導入した境界に限定する。

## G1-GR04-P7: XInputの振動出力と失敗時の停止再試行を接続する
- status: done
- done-when: float0..1を検証/量子化して実XInputSetStateへ送り、成功だけACKとして返す。非zero試行の所有/不確実性を保持し、focus喪失/Shutdownで0、失敗は再試行、未停止状態から再Initializeを拒否する。EngineはTryShutdownの結果で終了義務を保持する。
- verify: SDK非依存実出力stateとfake APIで全slot/不正非変更/失敗非zero/停止失敗/再試行/背景拒否/再初期化を通常/O2/ASanUBSan/bundle。device/既存入力回帰、公開API/寿命/停止の独立レビュー。実nativeとEngine統合未実行を明示。
- stop-when: failed writeを成功扱い、停止義務の消失、背景nonzero、invalid clock/valueでAPI送信、未停止再開始、実装によるcallback中owner再入、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Private/Platform/Windows/IXInputApi.h, Library/Core/Private/Platform/Windows/XInputVibrationState.h, Library/Core/Private/Platform/Windows/XInputPollingState.h, Library/Core/Private/Platform/Windows/XInputDevice.h, Library/Core/Private/Platform/Windows/XInputDevice.cpp, Library/Core/Private/Platform/Windows/WindowsXInputApi.cpp, Library/Core/Private/Engine/Engine.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/XInputVibrationStateTest.cpp, Test/Core/Input/XInputDeviceIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: serviceのvoice/包絡線/ポーズとの接続とJSONは次段。native停止失敗の物理的成功は保証しない。stop pendingはAPI所有者の寿命内で保持する。

## G1-GR04-P8: 振動効果と再生voiceをHapticsServiceで所有する
- status: done
- done-when: 効果/keyをコピー所有し、最大64voiceを非wrap handleでPlay/Stop。実dt/loop/終了とpriority合成、倍率/onoff、focus/paused取消、成功ACK差分送信/失敗retryを一時sinkへ接続。無効設定/clockは非変更、再入を拒否しowner/APIを長期借用しない。
- verify: 実時間進行kernelの巨大dt/端点/無効非変更を通常/O2/ASanUBSan/bundle。実serviceの所有/handle/voice上限/再設定/倍率/停止/再入/failed ACKを既存bundleへ追加し独立レビュー。Windows依存の未実行を明示。
- stop-when: borrowed effect寿命超過、handle wrap/別owner誤停止、invalid部分更新、停止zero省略、inactiveで振動、sink再入で破損、blocking未解消。
- paths: Library/Core/Public/Input/HapticsPlaybackTime.h, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/HapticsPlaybackTimeTest.cpp, Test/Core/Input/HapticsServiceTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: service自体はbackend参照を保持しないが、寿命中の実backend/slot対応は同一とする。動的交換は旧service停止後に新serviceを使う。制御変更は次のUpdate/FlushOutputsで送信、Engineの即時focus/pause/終了接続とJSONは次段。

## G1-GR04-P9: 振動再生をEngineの更新と即時停止へ接続する
- status: done
- done-when: Engineがserviceを値所有し同一pad providerへ一時sinkで送信。simulation前にpauseを反映、後に未clamp実dt更新。focus/pause/終了で即時取消/Flushとnative停止を行い、再Runで旧voiceを再開しない。owner/service再入を拒否し、配送中focusの保留を安全地点で回収する。喪失→復帰の取消履歴と新しい通知を失わない。
- verify: fake providerで4slot/倍率/送信順/非focus/pause/終了失敗再試行/再Run、Processorの実dtヘルパーでclamp前の時間を確認する試験を既存Engine bundleへ追加。純時間/mix/output回帰と独立レビュー、Windows統合未実行を明示。
- stop-when: pause中nonzero、cleanup前にbackend破棄、scaled/clamped dt使用、古いvoice再開、callback中owner再入、停止失敗の偽成功、blocking未解消。
- paths: Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Test/Core/Engine/HapticsFramePipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: serviceの効果はGame側が設定。既定asset/JSON読込みは次段。実機停止が失敗した場合は再試行義務を保持し、物理成功を保証しない。

## G1-GR04-P10: 振動効果JSONと既定assetの起動読込みを接続する
- status: done
- done-when: haptics.v1のeffect/key/settingsを型・有限値・範囲・重複・サイズ/深さ上限付きでParse/Writeし、invalid時の旧値を保持。serviceへ効果と設定を一括反映、既定assetをGame起動で読込み、通常I/O/validation失敗時は旧設定を維持して起動継続（確保例外は伝播）。自動再生は行わない。
- verify: JSON roundtrip/未知field/既知重複/不正型・範囲・名前・曲線・上限/旧設定保持とGame適用を既存bundleへ追加。既定assetの実Python検証、pure haptics回帰、独立レビュー。Windows依存の実parser/統合未実行を明示。
- stop-when: invalidの部分反映、設定の暗黙保存/自動再生、JSON解析のbyte/depth上限無視、未知fieldの誤採用、起動画面の変更、blocking未解消。
- paths: Library/Core/Public/Input/HapticsJson.h, Library/Core/Private/Input/HapticsJson.cpp, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Library/Core/CMakeLists.txt, Game/Input/GameHapticsSettings.h, Game/Input/GameHapticsSettings.cpp, Game/GameApplicationHandler.cpp, Assets/Config/HapticsEffects.json, Test/Core/Input/HapticsJsonTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 強度/onoffはruntime APIとJSONで表現し、user設定の保存先/UIはGR76/GR68へ接続する。既定の見た目/手触り値は仮、実機調整は未確認。既存AssetFileReaderは全file読込み後にJSON上限を検査し、確保前の個別上限は別課題。

## G1-GR04-P11: 入力方式の活動判定と切替ヒステリシスを実装する
- status: done
- done-when: KeyboardMouse/Gamepadを値状態で識別し、有効clockと活動時だけ切替、最短間隔0.3秒を既定にする。key/button新規押下、mouse移動閾値/scroll/char、Live padの新規button・deadzone外の意味ある変化を判定。背景/基準sample/同値/無効値で切替えず、focus復帰で旧kindを維持する。
- verify: 純状態の全source/境界/noise/同値/clock拒否/最初の切替/cooldown/設定非変更/focus/相対と絶対の二重計上回避を通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: analog drift/repeat/背景入力で切替、単調clock違反の状態更新、cooldown無視、mouse2lane二重加算、無効設定の部分反映、blocking未解消。
- paths: Library/Core/Public/Input/ActiveDeviceKind.h, Library/Core/CMakeLists.txt, Test/Core/Input/ActiveDeviceKindTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は純検出state。InputSystem/GetActiveDeviceKind/DelegateとEngine時刻・focus接続は次段。閾値は表示用の活動判定で、Mapperの操作値には適用しない。

## G1-GR04-P12: 使用中入力方式の取得と変更通知をフレームへ接続する
- status: done
- done-when: InputSystemでGetActiveDeviceKind/設定/変更Delegateを公開し、受理された入力を物理履歴からP11へ渡す。明示の非scaled frame時刻とfocusをEngineから供給し、EndFrameで最終kind変更のみ通知する。reset/held/背景/無効入力で切替を合成しない。
- verify: 純検出回帰、実Systemの各source/通知順/集約/cooldown/物理held/reset/focus/無効clock非変更を既存bundleへ登録し実compileを試す。Engineの時刻検証/focus供給を静的確認し独立レビュー。Windows依存で実行不能なら未検証と明記しstubで代用しない。
- stop-when: 正本の受理前に活動を反映、人工resetを新押下扱い、frame検証失敗で正本を更新、callback中の二重通知/寿命逸脱、旧入力経路の変更、blocking未解消。
- paths: Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Input/ActiveDeviceKindIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 既存BeginFrame()はsteady clockで互換を保つ。明示clockとの異なる時刻原点混用は禁止。初回frame以前の注入は従来通り正本へ受理するが種別判定対象外。変更通知callbackは入力配送外、再入frame/本体破棄/例外は不可。

## G1-GR08-P2D: コライダーのレイヤーと識別値を公開snapshotへ接続する
- status: done
- done-when: ColliderComponentに32bit Layer/Maskとuint64 UserDataを所有させ、SetTrigger同様に登録・owner thread検証後に更新する。既定Layer1/Mask全bit/UserData0を維持し、BuildBroadphaseがtriggerも含めコピーする。旧ray/overlap hit末尾にUserDataを追加し全形状で伝搬する。
- verify: 実proxy/query型のCPU回帰を通常/O2/ASanUBSan/MEMBERで実行。実component/snapshotの既定/設定/公開時点/旧hit/拒否非変更を既存Physics bundleへ追加しcompileを試す。独立公開契約レビュー。Windows依存は未検証と区別し代用品を作らない。
- stop-when: ゲーム固有layer名のCoreへの流入、公開snapshotの暗黙更新、旧hit順序/normalの変更、wrong thread/未登録での部分更新、blocking未解消。
- paths: Library/Core/Public/Scene/PhysicsQueryTypes.h, Library/Modules/Physics/Public/Physics/ColliderComponent.h, Library/Modules/Physics/Private/Physics/ColliderComponent.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Modules/Physics/PhysicsProxyQueryTest.cpp, Test/Modules/Physics/PhysicsModuleTestAccess.h, Test/Core/Object/PhysicsQueryTypesTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: Layerは所属bit集合で0/複数bitも許可。ゲーム側が名前/割当を所有する。Mask0も有効。solver適用と新query facadeは後続で、metadataを保存しただけで衝突挙動変更完了とはしない。

## G1-GR08-P4A: 統合クエリをSceneQueryと物理providerへ接続する
- status: done
- done-when: SceneQuery::ExecuteQueryでPhysicsQueryDescをproviderへ委譲し、PhysicsModuleがreadiness/owner thread検証後に公開snapshotの実ExecuteQueryを呼ぶ。未対応providerの既定実装はUnavailable、非Successと例外で出力を残さない。旧ray/overlap入口は互換維持する。
- verify: fake providerの旧実装互換/完全descriptor伝搬/成功/全失敗/例外/未bind/wrong threadを既存SceneQueryPhysicsFacadeTestへ、実Moduleのray複数hit/filter/ignore/trigger/overlap/sweep/UserData/未準備を既存PhysicsBroadphaseQueryTestへ追加。実compileを試し、実proxy CPU回帰と独立レビューを実施。
- stop-when: CoreがPhysics moduleへ依存、wrong thread/未bindでprovider呼出し、非Success/例外で残留hit、公開snapshot以外への暗黙refresh、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: generic descriptorでfilter付き7種を公開する。batch/訪問関数最適化/solver mask/refreshは後続。既存APIへ新しい数値制限を強制する経路差替は行わない。Windows依存の統合実行は未検証と区別する。

## G1-GR08-P4B: 同じ公開snapshotへの問い合わせをバッチ化する
- status: done
- done-when: SceneQuery/provider/PhysicsModuleへExecuteBatchを追加し、準備/owner確認をbatch単位にして順次実行する。要求順のResult/FirstHit/HitCountと連続hitを返す。個別失敗は0hitで他要求を継続、全体の非Success/例外は両出力を空にする。準備済み空batchはSuccess。
- verify: fake providerの既定未対応/1回委譲/全体失敗/例外/不正span/wrong threadと、実ModuleのSuccess/NoHit/InvalidArgument混在/offset/MaxHits/空/未準備/単発結果一致/snapshotsequence不変を既存bundleへ追加。実compile試行、CPU回帰、独立レビュー。
- stop-when: itemごとにreadiness再確認、異なるsnapshotへの暗黙更新、失敗itemのhit混入、offset/size overflow、全体失敗/例外で部分出力、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 戻り値Successはbatch処理成立で、各要求成否はoutResultsを見る。failure itemのFirstHitはその時点の連続hit末尾、HitCount0。入力span/両出力は非aliasかつ同期呼出し中有効。未対応providerの既定はUnavailable。batch自体の結果と個別結果を混同しない。

## G1-GR08-P5A: 衝突候補とsolverへ対称レイヤーマスクを適用する
- status: done
- done-when: broadphase候補生成前とResolveContactsの接触/trigger判定前に(A.Layer & B.Mask)&&(B.Layer & A.Mask)を適用する。既定は従来通り、0/高bit/複数bitを扱い、許可切替でtrigger Begin/Endが次stepに整合する。queryのLayerMaskと相互作用Maskを混同しない。
- verify: 純bit判定の全32bit/両方向/4096真理値表と実proxy回帰を通常/O2/sanitizer/MEMBERで実行。実broadphaseの順序/接触端点/一方向拒否、実Worldのtrigger通知/solid押出し有無を既存Physics bundleへ追加してcompile試行、独立レビュー。
- stop-when: 片側許可だけで衝突、拒否pairの押出し/Hit/Begin、旧既定の候補順序変更、queryへMaskを誤適用、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Core/Object/PhysicsQueryTypesTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: Layer/Maskはworking snapshotの値で評価し、callback中の設定は次stepに反映。既存trigger終了通知は許可撤回時にも配信する。実Windows/Core統合は未検証と区別する。

## G1-GR08-P5B: simulationを進めずquery snapshotを明示更新する
- status: done
- done-when: SceneQuery/provider経由のRefreshDynamicSnapshotを追加し、owner/準備/physics処理中を検証してquery公開データだけ再構築する。承認S9=a通りfixed-step sequence/速度/impulse/準備位置/接触・通知状態は保持。候補完成後のnoexcept置換で旧snapshotを保護する。
- verify: fake provider未対応/委譲/wrong threadと実Moduleの移動前後/明示更新/sequence不変/新規・無効化/metadata/Body対応/未準備/通知中拒否を既存bundleへ追加。実compile試行、Broadphaseのnoexcept移動syntax、CPU回帰、独立レビュー。
- stop-when: refreshでsimulation進行/impulse消費/イベント発火/sequence更新、working snapshotの変更、候補失敗で公開値破壊、reentryで通知中のsnapshot変更、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: GR09の分離最適化までは全proxy再構築。明示呼出しのみで毎frame自動実行しない。現在のlifecycleをquery用に評価するがsimulationのactive cacheは更新しない。sequenceは最後の固定更新公開を示し、明示refreshで同sequence内のquery内容が変わる。

## G1-GR08-P4C: 物理proxy候補のAABBとray訪問境界を追加する
- status: done
- done-when: span上の実proxyをAABB/有限長rayで保守的に絞り、同期callbackへ入力順で渡す訪問APIを追加。任意precheckでfilter/validationを先行でき、失敗の順序を保つ。非有限/逆転bounds等は要求拒否、proxy側の不正boundsは黙って落とさずcallbackへ渡す。
- verify: 実Broadphase.cppを直接リンクし、遠方除外/接触/巨大・微小direction/長さ上限/0距離/順序/早期失敗/precheck拒否/不正値/不正spanを通常/O2-NDEBUG/ASanUBSan/MEMBERで検証し独立レビュー。既存proxy回帰も実行。
- stop-when: 接触候補の取りこぼし、近zero軸をepsilonで平行扱い、callback借用の保持、validator失敗の隠蔽、proxy不正の黙殺、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsProxyVisitorTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 同期callbackはSuccess/NoHitで継続、他のResultで中断。precheckのNoHitは候補除外。callback/context/proxyの借用は呼出し中だけ。float丸めとsweep許容を見込みboundsを保守的に拡張。既存集約への接続はP4D。

## G1-GR08-RAY: 遠方rayと球・カプセルの桁落ち誤判定を修正する
- status: done
- done-when: 物理query内のray/sphere/capsule計算をdoubleの線距離・断面判定へ変更し、遠方の明確なmissをfalse hitにしない。内部始点0/最寄り正根/端球・円筒・縮退/正規化・距離上限の契約を保つ。共有Math/描画側は変更しない。
- verify: radius1/offset2/distance10000の球とカプセルで旧Success距離10000を実再現済み。軸方向の解析解、遠方hit/miss/tangent/inside/逆向き/縮退/極小線分/向き・倍率を通常/O2-NDEBUG/ASanUBSan/MEMBERで検証し、既存proxy/visitor回帰と独立レビューを行う。
- stop-when: 明確な非交差をhit、真の交差をmiss、非有限結果、近い正根より遠い根を選ぶ、旧inside0の破壊、描画への変更、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsRayPrecisionTest.cpp, Test/Modules/Physics/CMakeLists.txt, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
