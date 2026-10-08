- G1-GR03-P4H: Engineが正本/Routerより後にMapperを所有して自動Attachし、Runのmessage前BeginFrameとTickのOnUpdate前Updateへsteady_clock絶対時刻＋clamp前raw dtを配線。Run開始/終了で取消、Shutdown/destructorは先行Detach。独立評価でRun例外時に末尾CancelAll/EndRunが飛ぶ欠陥を検出し、scope-exitに修正。実Runを通る偽platformのPump例外/OnUpdate例外/通常終了、legacy透過、所有/時間/fixed latchの試験を既存Engine bundleへ追加し第2周静的レビューPASS。実InputActionRuntime/InputButtonState回帰PASS。新Engine試験と既存FixedStepSchedulerTestはWindows.h依存でcompile停止、統合実行/起動画面は未確認。Game JSON/context設定、rebind、OS供給/cursor/focus通知は後続。
- Next: bindings.v1の名前付きcode・設定JSON/既定とユーザー差分の重ね合わせ、IInputBindingStoreによる保存、リバインド捕捉を実装する。

- G1-GR03-P5A: bindings.v1の物理code名/安全数値変換、既定JSON/full書出とuser差分の全体適用/差分書出を追加。Identityは名前保存、未知項目は警告/無視、既知不正はdefaults fallback、absent/empty bindings、alias、BOM、1MiB/深さ64を扱う。JsonValue借用列挙を追加し、既存数値parseをlocale非依存のfrom_chars＋全token/範囲検証へ変更。独立評価でfloat化による意味範囲外値の丸め通過と、空未知field名の独自String c_str問題を検出。double/float両方の範囲検証と空文字literal警告へ修正し、恒久試験を追加、第2周PASS。実portable名前/数値試験は通常/NDEBUG-O2/ASan・UBSan（LeakSanitizer除外）/bundle compileと既存3件回帰成功。JSON/JsonWriterの統合compile/実行はWindows.h依存で未確認。既存Delegate識別/JSON surrogate問題は独立TODOとして追跡。
- Next: IInputBindingStoreによる一時working-directory保存/読込とGame既定JSONのロード、それからrebind捕捉とOS入力供給へ接続する。

- G1-GR03-P5B: IInputBindingStore値返し結果とLoadInputBindingConfiguration/SaveInputBindingOverridesを接続し、Missing/Invalid/ReadErrorを区別して既定へ退避。Windows暫定Storeは生成時に絶対W pathを固定、上限1MiB＋全read/EOF、同directoryのCREATE_NEW tempへ全write/flush/close後に置換する。自分のtempだけ後始末し、元targetを先にtruncateしない。個人設定をgitignoreへ追加。独立IO/寿命/Win API静的レビューPASS、portable名前/settings回帰PASS。FakeStore/native temp-directory試験をbundleへ追加したがWindows.h依存でcompile/実行未確認。任意指摘の1MiBちょうど成功と無関係temp保持の試験も追加した。起動時の自動上書きは行わない。
- Next: P5CでGameのDefaultInputBindings.jsonと初期化ロードへ接続する。

- G1-GR03-P5C: GameInputActionsと4 context/11 actionの既定Assetを追加し、GameInputSettingsが起動時に既定＋user差分をロードしてMapperへ反映する。ConfigureWithContextは設定と初期stackを一括反映し、失敗時は旧設定を維持。Debug/Normalで既存camera経路を保ち、保存は明示APIのみ。独立静的レビューPASS。実JSON dataから生成したportable binding/runtime検証は成功。GameInputSettingsTestとMapper回帰を既存bundleへ登録したが、Windows依存の実JsonDocumentロード/Game起動/統合試験は未実行。
- Next: event基盤を拡張する前に既存Delegateの解除対象誤識別を独立修正し、GR03のrebind捕捉とOS入力供給を接続する。

- CORE-DELEGATE-IDENTITY（G1-GR03イベント基盤の前提）: free functionの保存領域pointer比較を関数値比較へ、memberの同closure型誤一致を型付きinstance/method比較へ修正。functorは登録tokenで識別しDelegate copyで維持、別Bindは別登録。候補swapで例外時の呼出先/識別の整合、move元空、null member空を保証。void統合時のnonvoid member結果破棄の互換性を恒久回帰付きで修復し第2周レビューPASS。実Delegate試験は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功。Multicast個別解除/同登録一括解除の実試験を追加しsyntax/bundle compile成功、実allocatorリンクはWindows.h依存で未実行。
- Next: GR03の入力取消を既存camera/drag/UIへ伝え、OSのfocus/cursor/raw供給とリバインドを接続する。

- G1-GR03-P6A: CameraInputCollector/MayaCameraController/LightController/PickingController/ImGuiをOnInputResetへ接続。未完了の操作とqueue/current入力だけを取り消し、camera姿勢/light値/確定selectionを維持する。進行中sphere previewだけを消し、遅延Releasedで新たな選択を実行しない。実CameraInputResetTestの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功。実InputSystem→Routerを使うLegacyInputResetTestを登録し独立静的レビューPASS、Windows依存の統合compile/実行は未検証。
- Next: GR03のWindows focus喪失/復帰通知を接続し、handler通知をOS message処理外で行う。Raw Input/カーソル制御/リバインドは継続。

- G1-GR03-P6B: IWindowの入力focus DelegateとWindows activation/keyboard focusを接続。Processorがshared window＋保存Delegate購読を所有し、loss時Mapper停止/ReleaseAll/全controller focus通知、Game handlerはPump後へ順序配送する。再入は次batch、旧購読batchはserialで停止、handlerによるplatform破棄後の借用pointerアクセスを防止。非focus legacy入力を抑止。第1周で旧repeatがTranslateMessage経由で文字だけ漏れる問題を検出し、native VK履歴により翻訳前も抑止するよう修正。即時observerのOS callback制約を公開APIへ明記し第2周レビューPASS。実WindowsKeyRepeatGateTestは通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功、Delegate/ReleaseAll回帰成功。Windows native/IME実機/新旧Engine統合は未実行。
- Next: GR03のRaw Input供給とカーソル要求/有効モードを接続する。リバインド捕捉、GR04のXInput/hapticsは継続。

- G1-GR03-P6C: Windows main windowの明示Raw mouse登録/解除とWM_INPUT motion laneを接続。自然alignmentの固定RAWINPUT bufferでサイズ/typeを検証し、foreground cleanupをDefWindowProcへ残す。button/wheelはlegacyだけ、X1/X2/横wheelとsigned座標を追加。device別absolute履歴は初回/focus/geometry/mode/remove/evictionで再seedする。第1周でPAGEONLY競合の見逃しとdisable失敗時の配送残留を検出し、page-wide/exact照合分離とlogical delivery/native ownership分離へ修正、第2周レビューPASS。実tracker通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileとRaw state/repeat回帰成功。Engine接続試験に登録寿命の検証を追加、Windows native/登録API/実mouse/RDPは未実行。
- Next: GR03の要求/有効カーソルmode、Clip/非表示とLocked時の絶対delta抑止を接続する。

- G1-GR03-P6D: Mapperのfocus非依存cursor要求をmessage前/Tick後にWindowへ同期し、requestedと最後に成功したeffectiveを分離。Windowsはclient screen RECTへClip、WM_SETCURSORで非表示、非focus/非表示/minimizedとRun終了/例外/Disconnect/DestroyでNormalを要求。move/size/DPI/display時の再適用、失敗のfalse返却/再試行を追加。Lockedの絶対mouseは位置だけを配送しdelta0、基準化はRaw/wheel/buttonsを保持。第1周でshared clip所有の実状態不一致とchild cursor上書きを検出し、GetClipCursor照合＋実focus/foreground判定＋対象HWND確認へ修正、第2周レビューPASS。実absolute追跡通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleとRaw/ReleaseAll/runtime回帰成功。Mapper/Router/Engine統合試験を拡充したがWindows依存で未実行、native clip/表示も未確認。
- Next: GR03のリバインド捕捉と設定への明示適用を実装する。GR04の実pad供給/hapticsは未完。

- G1-GR03-P7A: Mapperのcontext stackをIdentity順に維持する設定再構築と、GameInputSettingsのaction bindings変更/個別既定復帰/全設定既定復帰を追加。候補copy/検証/compile完了後に旧操作取消とnoexcept moveで反映し、unknown/invalid/欠落contextでは両方を維持する。alias配列と空unbindを扱い、変更だけでは保存しない。独立所有/例外保証/試験の静的レビューPASS。既存portable runtime/Delegate回帰は成功、追加Mapper/Game統合試験とstatic_assertの実compileはWindows.h依存で未確認。
- Next: GR03の物理入力捕捉と中止/解除待ちを実装し、この明示設定反映口へ接続する。

- G1-GR03-P7B: InputStateに最後のprovider pad sample/受理serialを追加し、ReleaseAllのneutral化と分離。成功注入は同値でも既存connection/button edge後にsample Delegate→Routerを配送し、invalid/Resetではsampleを作らない。callback前に注入引数をcopyして呼出元可変値の変更にもsnapshotを保つ。実GamepadInputState通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileとReleaseAll/runtime/raw/camera-reset回帰成功。独立公開API/配送/所有レビューPASS、routing統合はWindows依存で未実行。capture側が物理neutralを取り違えず判定できる供給口を整えた。
- Next: GR03の物理入力capture kernelと解除待ち、そのEngine/Router接続へ進む。

- G1-GR03-P7C: 値状態の物理入力captureを追加。source mask/修飾chord/単独修飾/初期held除外/接続基準化/analog hysteresis/方向/中止/neutral待ちを実装。候補生成をRouter到達eventだけに限定し、pad履歴は人工的resetと区別する。相対入力の静止判定は正本の非zero受理serialで補い、UI消費と±相殺でも早期終了しない。第1周の静止判定指摘を修正して第2周レビューPASS。実InputStateを使うcapture/Raw試験は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功、pad/armed/reset回帰成功。Windows/Engine/Router接続はこのkernelの検証範囲外で未実行。
- Next: GR03のcapture managerをRouter/Mapper/Engineへ接続し、捕捉中の操作とcursor要求を停止、終了時の設定反映へ進む。GR03全体は継続中。

- G1-GR03-P7D: InputRebindCaptureManagerを同じSystem/Router/Mapperの単一ownerとして予約最高優先度へ接続。request ID付き開始/中止/結果取得、全通常eventの遮断、Mapperのevent/polling/fixedPress/Active/cursor抑止、外部reset/focus喪失での中止を実装。Advanceで残留入力をresetしてから抑止を解除し結果を公開する。EngineでMapperより先にDetach、Run終了/例外も通知なしに正本を中立化してDetachし、次Runで再Attach。独立静的レビューPASS、通常終了の再Attach/取消も試験sourceへ補強。新規manager公開headerの実syntax検査とkernel/Raw/pad/armedのportable回帰成功。実System/Router/Mapper/Engineの統合試験を既存bundleへ登録したが、String.h/Containers.hのWindows.h依存によりcompile/実行は未検証。代替stubは使っていない。
- Next: GR03のGame側action/slot/revisionとcapture結果を結び、古い設定への誤適用を防ぎながらP7Aの明示反映へ接続する。GR03/GR04全体は継続中。

- G1-GR03-P7E: GameInputSettingsへcontext/action/slot/明示出力/revisionの不透明な値requestとApplyRebindCaptureを追加。末尾追加/置換をP7Aの一括更新へ接続し、Pending/Applied/Cancelled/Stale/Invalidを区別する。manager request IDとsettings revisionを各々process内の非wrap採番にし、別instance・古い設定・二重適用を拒否。要求はowner pointerを保持せず、成功時だけCurrent/revision更新、Saveは明示のみ。純helperのsource/modifier/方向/型/invalid非変更と実InputActionRuntimeでの負軸Button発火を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileで確認。独立静的レビューPASS。実Game/managerの追加・置換・中止・別owner・stale・保存・失敗後再試行の試験を追加したが、Windows.h依存のcompile/実行は未検証。
- Next: GR03の--imgui利用時のロック解除hotkeyを受入れ要件に合わせて追加する。GR04のXInput/polling/haptics/device種別とGR08の残接続は継続。

- G1-GR03-P8: --imguiのInstall/UninstallとEngine所有InputDebugOverlayControllerを接続し、有効時だけF1でcursor NormalとGame入力maskを切り替える。UIへ通常eventを渡した後でGameを遮断し、Mapperの独立抑止理由をcaptureとORしてpolling/fixed/Activeも停止。context/JSON/描画内容は変更しない。Processorの配送後に要求反映/resetし、Engine/Runの所有順と再Attachを接続。第1周の自己resetによるF1押下世代消失と未適用enter終了時のlegacy残留を修正。内部guardとSystem所有deferred reset（次のAttach/BeginFrameで通知回収）を追加し第2周静的レビューPASS。純state通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleと公開controller header syntax、capture/runtime/Raw回帰成功。Core/Engine/ImGui統合試験にrepeat/二重Pressed・UI先行・capture優先・owner・pending終了/再開を追加したが、Windows.h依存によりcompile/実行と実機cursor確認は未検証。
- Next: GR03の計画上の実装項目は一通り接続済み。Windows実機と統合bundleの受入れは未検証として残す。GR04へ進み、XInputの実pad供給・接続/切断・振動・device種別通知を実装する。

- G1-GR04-P1: XInputの成功packetを独自GamepadStateへ変換するOS非依存境界を追加。signed16軸の負/正側を別の除数で±1へ、triggerを255分率へ正規化し、未定義button bitを除去、packetを保持する。deadzoneはMapperへ残す。左右motorは有限0..1を検証後にuint16へ一括量子化し、invalidで旧出力を保持。Microsoft一次資料で範囲/bit/左右motorの意味を照合。軸65536値、trigger256値、全channel/mask/packet/実InputState受理、motor量子化/invalidを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleで確認。独立レビューも実再実行PASS。native API呼出し、Windows統合bundle、実機はこの境界の範囲外。
- Next: GR04のBackground/Baseline配送を操作正本と実sample履歴に分け、focus復帰直後のheldを誤捕捉しないようにする。その上でXInput provider/polling/Engineへ接続する。

- G1-GR04-P2: Live/Baseline/Backgroundのpad配送modeを追加。既定Liveは旧順序/通常button通知を維持し、非LiveはPressedラッチと通常button通知を抑止、connection/物理sample通知は維持する。Backgroundは操作正本だけneutral、Baselineは実値へ同期し、物理sample/serialは両者とも実値を保持。captureは非Liveを基準化だけに使い初回heldを誤捕捉しない。全検証は更新前、invalidで状態/serial/通知非変更。実State/kernelの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleとarmed/runtime/reset回帰成功。invalid modeの試験を新serial高analog→同serial Liveの捕捉へ補強。独立静的レビューPASS。System/Router/Mapperのmode伝搬・非focus取消・復帰held非armed試験を追加したが、Windows依存の実統合は未実行。modeだけでfocus Cancelは代替せず、lossのMapper停止/ReleaseAllを先行させる。
- Next: GR04の差替XInput APIとポーリング状態を実装し、接続済み毎frame/未接続低頻度、Background/Baseline選択、停止時のmotor zeroへ接続する。

- G1-GR04-P3: SDK非依存IXInputApiとXInputPollingStateを追加。4slotの接続済み毎frame/未接続1秒以上のround-robin探索、同packet配送、error中のneutral退避とhealth保持、Background/復帰Baseline、sink拒否/例外後の再配送を実装。有限非負単調clockを検証し、同回二重読取や長時間経過のcatch-upを防止する。実InputState sinkによる通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle試験成功。独立レビューPASS、追加の複数error/拒否時の公平性/1000回探索/例外再試行も実行成功。Windows API・Engine接続・実機入力・振動はこの段では未実行。
- Next: GR04のnative adapterとEngine所有デバイスの寿命/ポーリングを接続し、その後hapticsとdevice種別通知へ進む。

- G1-GR04-P4: Platform::CreateGamepadDeviceとWindowsXInputApiを追加し、native成功packetの全fieldを明示変換。失敗時のraw非変更、slot検証、Xinputリンクを接続。XInputDeviceはAPIを独占所有しpoll stateを内包、null生成/コピー/移動を拒否。System参照は同期sinkだけが借用する。IInputDeviceの旧poll互換を保ち、時刻付きbool poll/focus/provider識別を追加。実device.cppと公開headerのcompile、統合試験のMEMBER object compileは成功。試験main改名時のreturn欠落を実compileで修正し再確認、独立レビューPASS。P3の通常/ASan・UBSan回帰成功。実System/Routerを含むリンクはWindows.h依存で停止し、統合実行/native SDK compile/実機は未検証。
- Next: GR04のdeviceをEngineで所有して初期化/終了/例外/Run再開とfocus-message後のframe pollingへ接続する。振動は後続。

- G1-GR04-P5: Engine所有device列と単一pad provider制約を接続。開始試行前に終了義務を記録し、false/例外は逆順rollback、停止例外は残りを止めた上で義務/所有を保持して再停止可能にする。busy再入拒否、finite非負単調clock、false時も他device継続を実装。handler設定後に標準pad未登録時だけ生成し、Run開始/メッセージ後poll/終了・例外/再Run/破棄へ接続。focus取消後にdevice hookを呼び、停止時はcapture中止/Mapper取消/正本neutral、legacy通知はSystem所有deferred resetへ保留。debug teardownも共用。独立静的レビューPASS。非null再入登録拒否・invalid clock後の同時刻pollも試験sourceに補強。P3 portable通常/ASan・UBSan回帰とP4 header/device.cpp/統合MEMBER object compile成功。Engineと追加bundle試験のcompileはString.hのWindows.h依存で停止し、統合実行/native実機は未検証。停止失敗時の物理停止成功は保証せず、再試行後にも失敗する場合は限界が残る。
- Next: GR04の振動包絡線/混合/成功ACKに基づく再送・最終zero、haptics.v1設定とEngine接続へ進む。その後に入力方式切替を接続する。

- G1-GR04-P6: 独自Spanによる振動包絡線viewと純評価/混合を追加。有限正duration・厳密昇順key・0..1を全検証し、端点保持/線形補間/非loop終了0/loop fmod、最高priority activeのmaxまたはadd-clamp合成/設定倍率を実装。出力は成功ACKのみ更新し、左右差分1/255、各motorの最終zero、失敗再試行/潜在作動保持を扱う。レビューで標準span規約不一致と小寄与の逐次float丸めを検出し、独自Spanとdouble蓄積・最後1回のfloat化へ修正。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle object成功、独立追加520万checkと100万微小寄与反例の解消を確認してPASS。実service/voice所有/native送信/JSON/Engine接続は後続。既存GR03のview規約不一致はG1-INPUT-SPANとして別件記録。
- Next: 入力層の既存viewを規約に揃えた後、GR04のvoice所有と振動出力・停止制御を接続する。

- G1-INPUT-SPAN（GR03入力境界）: runtime/settings/names/mapper/binding setの呼出中viewをContainer::Spanへ統一。レビューで(pointer, 0)のcount/range曖昧性を検出し、既存Spanのrange constructorを末尾型制約付きにして解消、同pointer/nullptrの空rangeでは減算を避ける。配列/const/VariableArray/既存viewの変換と所有寿命を維持。実Settings/Runtime/Names/RebindTypes/Hapticsの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER object成功、独立レビューPASS。実Mapper/BindingSetのcompileはString.hのWindows.h依存で停止し統合実行は未確認。
- Next: GR04の振動voice所有と実出力・停止制御へ進む。

- GAME-VFX-REGISTER（GR130〜GR137）: 作者提供の初期3文書とVFX追加原文をDocs/Plansへ全量配置し、8要件をロードマップ/要件書へ追記。G7/G8/G10配属案と未着手を登録、G1進行中と完了ゲート未通過を区別。原文の作者判断・提案・完了条件案を保持し、GR57資産形式、GR137→GR126→G7/G8の循環、後段renderer、Bridge/Editor/SDK境界を選定待ちへ記録。初版スナップショットと追加原文は不変保持、計画文書は非追跡のまま。TASKSへGAME-GR130-VFX〜GAME-GR137-VFXを追加。エフェクトの実装は未着手。
- Next: 現在のG1/GR04を継続。VFX追加は前提GRと選定事項が整った段階で着手する。

- G1-GR04-P7: IInputDeviceのbool SetVibration/TryShutdownとXInputSetState adapterを接続。float範囲検証/量子化後の左右WORDだけ送信しERROR_SUCCESSをACKとする。SDK非依存stateで非zero試行の潜在作動・失敗停止義務を保持し、focus喪失/Shutdownは全対象を停止、失敗後のnonzeroはzero先行、停止後再Initializeも残留zero回収を必須にした。EngineはTryShutdown=falseを保持し再試行する。deviceの有効clock pollで保留停止を再送、失敗でも入力継続。実state通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER object、既存poll回帰、device.cpp/統合試験object compile成功。統合試験に有効clockのstop失敗下で入力継続/回復を補強。独立静的レビューPASS。API callback再入はP3同様に明記禁止で、native callback経路は追加していない。Engine/実Systemリンク/WindowsSDK/実機振動は未検証。destructorの最後の停止試行も失敗する場合、物理的停止成功は保証できない。
- Next: GR04のHapticsServiceへ効果定義・voice所有・実時間更新・pause/focus停止・設定/JSONを接続する。

- G1-GR04-P8: HapticsServiceへIdentity付き効果/keyのコピー所有と最大64voice、非wrap handleのPlay/Stopを追加。全候補検証/確保成功後のConfigure交換、無効値/確保例外で旧状態保持、alias再設定を扱う。新voice初回t0/以後実dt/loop/終了、最高priority合成/gain/設定倍率、focus/pause/disabled/切断で取消、成功ACK差分とzero/失敗retryを同期sinkへ送る。同じ実backend/slot対応を寿命中維持する契約を明記。レビューの端点丸め反例を修正し、非loopの値がdurationへ丸まった場合もfinished=trueとしてvoice枠を解放。純時間kernel通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle、既存混合/出力回帰成功、独立再実行と静的レビューPASS。実serviceと所有/制御/再入の追加試験はIdentityPoolのWindows.h依存でcompile/実行未確認。制御setter/Stopの送信は次Update/Flushで、Engine即時接続とJSONは後続。
- Next: GR04のEngine所有HapticsServiceをsimulation後の実時間更新、focus/pause/終了の即時Flushへ接続する。

- G1-GR04-P9: EngineへHapticsService値所有と固定providerの同期sinkを接続。simulation前のpause取消/即時Flush、後のclamp前実dt更新、focus喪失時のservice取消/Flushとdevice独立zero、終了時StopAll/Flush→逆順TryShutdownを実装。owner/service busy双方を検査し、最後のbackend停止結果で義務を保持する。第1周レビューでpoll中focusの拒否取りこぼしと検証不足を検出。Processorに最新focus＋喪失印の保留を追加し、raw即時中立化/安全batchのreset・Router通知、callback前snapshot/clear、Engine一致/購読serial、Run開始/message後/poll後/振動更新前後の回収を接続。false→trueでも旧voice取消を失わない。4slot、送信順、service-busy単独、実共通pause経路、sample中focus変化、適用中の後発focus、防御的observer復帰、window交換、停止再試行、実Run例外cleanup/再Runの試験sourceを追加し第2周静的レビューPASS。純時間/mix/output回帰は成功、Engine/test compileはWindows.h依存で停止し統合/native実行未確認。FlushとTryShutdownの結果を個別に逆転させる組合せ試験は未追加のnon-blocking残課題。
- Next: GR04のhaptics.v1効果JSONと既定asset/起動読込みを接続し、その後にActiveDeviceKind通知へ進む。

- G1-GR04-P10: haptics.v1 codecを追加。effect/key/任意settingsの既知field型・重複・有限値・範囲・名前/curveと1MiB/深さ64/effect256/key256/name128byte上限、未知field警告を実装。Parse/Writeは候補完成後に反映、service Configure overloadで効果/設定を一括反映する。レビューの入力/report aliasをlocal診断の退出時公開で修正し、既存Identity pool別名衝突は元名とのbyte一致検査で拒否。Game起動は既定Footstep/Hit/BiteHold assetを読み、通常I/O/validation失敗なら旧設定で継続、自動再生/保存なし。実asset1539byte/3効果のPython検証、pure時間/mix/output回帰成功、独立静的レビューPASS。実codec/Game/追加testはWindows.h依存でcompile/実行未確認。既存AssetFileReaderは全file確保後にJSON上限を検査し、確保例外は伝播して起動失敗になり得る。永続化/UIはGR76/GR68へ接続する。
- Next: GR04のActiveDeviceKind検出・ノイズ除外・切替ヒステリシス・Delegate通知を実装する。

- G1-GR04-P11: KeyboardMouse/Gamepadの純活動判定stateを追加。新規押下/有効文字/wheel、Raw・absolute別累積の移動閾値、Live padの新buttonとslot別analog累積変位で選択し、既定0.3秒の最短切替間隔を守る。背景/基準sample、repeat、静止held、noise、人工resetで表示を奪い返さない。設定/clock/sampleの全検証後だけ更新し、focusはkindを維持して累積基準を破棄。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle MEMBER object成功、独立レビューも追加の跨frame trigger/無効非変更/held再奪取試験を実行してPASS。
- Next: GR04のInputSystem GetActiveDeviceKind/DelegateとEngineのframe clock/focusへ接続する。純状態の合格は実System/Engine/Windows受入れを意味しない。

- G1-GR04-P12: InputSystemへ使用中入力方式の取得/設定/変更Delegateを接続。受理正本を先に更新して活動を評価し、padは人工resetに影響されない物理履歴と比較、UI consumeから独立させた。Processorから共通非scaled frame時刻と即時focus、Engineから適用focusを供給する。不正System clockはMapper/frame更新前に拒否。kind取得は即時、通知はEndFrameで最終値だけ集約し再入二重通知を防御。公開header syntaxとP11通常/O2/ASan・UBSan回帰成功、独立静的レビューPASS。実System/Router試験は登録済みだがWindows.h依存でcompile/実行未確認。
- Next: GR04の計画上の実装項目を一通り接続した。Windows統合bundle/実パッド/振動の受入れは未検証として残し、G1のGR08（コライダーmetadata、query façade/provider、mask、明示snapshot更新）へ進む。GR03/GR04/G1の全受入れ完了とは扱わない。

- G1-GR08-P2D: ColliderComponentへ32bit所属Layer/相互作用Maskとopaque uint64 UserDataを追加。既存のowner thread/登録世代検証を通して更新し、既定1/全bit/0、0/複数bitを許可してゲーム固有名は持たせない。BuildBroadphaseでTriggerも含め値snapshotへコピーし、旧ray/overlap hit末尾にもUserDataを伝搬。setterで公開snapshotを即時更新せず、法線/順序/旧既定query挙動は維持。実query値型/実proxy集約の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER compile成功、独立静的レビューPASS。実Collider/Moduleの全形状/公開時点/スレッド・未登録拒否試験を追加したが、Windows.h依存で統合compile/実行は未確認。
- Next: G1／GR08の新ExecuteQueryをSceneQuery façadeとPhysics providerへ接続し、フィルタ・複数hit・sweepを実際の公開snapshotから呼べるようにする。solver maskと明示refreshは後続。

- G1-GR08-P4A: SceneQuery::ExecuteQueryを非pureのprovider拡張へ接続。未対応providerはUnavailableでsource互換を維持し、GameThread/接続確認後に全descriptorを渡す。非Success/例外で出力を空にし例外は再送出。PhysicsModuleはreadiness後に公開Broadphaseを読み、filter付きray複数hit/overlap/sphere・capsule sweepを公開する。旧APIの数値契約やsnapshot時点を変更しない。独立静的レビューPASS、同じ3球・7種・filter/ignore/trigger/MaxHits/UserData期待を実proxyコードでRelease/ASan・UBSan実行成功。fake/実Module統合試験を既存bundleへ追加し、全shape fieldの転送比較も補強。統合compile/実行はWindows.h依存で未確認。
- Next: G1／GR08のExecuteBatchで要求順の連続hit/resultとreadiness確認の集約を追加する。候補訪問の最適化、solver mask、明示refreshは継続。

- G1-GR08-P4B: SceneQuery/provider/PhysicsModuleへExecuteBatchを追加。owner/接続/準備をbatch単位に確認し、同一公開Broadphaseへの要求を順次処理してResult/FirstHit/HitCountと連続hitを返す。個別NoHit/InvalidArgument等は0hitで継続、全体失敗/例外は両出力を空にする。候補配列への組立、size上限確認、noexcept swapで部分公開を防止。準備済み空batchはSuccess、暗黙refresh/sequence更新なし。独立静的レビューPASS、既存実proxy回帰Release/ASan・UBSan成功。新batchのfake/実Module試験に混在offset・全hit field単発一致・metadata/sequenceを追加したが、Windows.h依存で新経路のcompile/実行は未確認。
- Next: G1／GR08のsolverとbroadphaseへ対称のLayer/Mask判定を反映する。候補訪問の最適化とS9=aの明示snapshot更新も続ける。

- G1-GR08-P5A: broadphase端点にLayer/Maskをコピーし候補追加前に対称規則を適用、solverもworking proxyの同じ規則を接触/trigger/solid処理前に確認する。既定候補順/接触端点/重複除去を維持し、queryのLayerMaskとは分離。全32bitの許可/片側拒否/複数bitと4096真理値表、実proxy回帰を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERで実行成功。独立第1周でHit期待に接近速度がない点を指摘され、許可時に接近→Hit/押出し、拒否時に自由積分/速度保持/Hitなしへ修正し第2周静的レビューPASS。実候補/Worldのtrigger Begin/Endとsolid検証は既存bundleへ追加済みだが、Windows.h依存で統合compile/実行は未確認。
- Next: G1／GR08の承認済みS9=a、明示RefreshDynamicSnapshotを追加する。fixed-step sequenceとsimulation stateは保ったままquery snapshotだけを更新する。候補訪問/掃引AABB最適化も継続。

- G1-GR08-P5B: 承認S9=aのRefreshDynamicSnapshotをSceneQuery/providerへ追加。owner/readinessと固定処理・通知中の再入を検証し、freshなlifecycle/Transform/shape/metadata/Body対応で候補Broadphaseを作り、完成後のnoexcept moveで公開queryだけ交換する。GR09までは全proxy再構築、明示呼出しのみ。simulation active cache/working proxy/速度/impulse/PreFixed準備/イベント/固定公開sequenceを保持。同sequence内の内容変化とcaller cache無効化を文書化。独立静的レビューPASS、実Broadphaseのnoexcept move syntaxと既存proxy Release/ASan・UBSan回帰成功。実移動前後/新規/無効化・復帰/保留impulse/通知中拒否試験を追加したが、Windows.h依存で新経路の統合compile/実行は未確認。
- Next: G1／GR08の候補訪問関数とray/swept AABBによる早期除外を実装する。無効値/未収束・順序の公開契約を保ち、CPUの全探索参照と照合する。

- G1-GR08-P4C: span上のproxyをAABB/有限長rayで保守的に絞る同期visitorを追加。入力順/precheck先行/非Success中断、不正proxyを除外で隠さない契約を保持。rayはdouble slab・軸別余裕・厳密0のみ平行扱い。第1周で許容OBB軸の逆写像領域がboundsより広い例を発見し、最大半径長1e-3の余裕とray/点AABB再現試験で修正、第2周PASS。実訪問処理を実Broadphase/Mathへ直接リンクして通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER成功。独立再現/再ビルドもPASS。既存proxy集約の回帰成功、集約への最適化接続自体はP4D。
- Next: 全探索比較の前に、独立レビューで発見した既存のray対球のfloat桁落ち誤hitを別タスクで修正する。center0/radius1に対しorigin(-10000,2,0),dirUnitXで旧QueryProxyが距離10000の誤hitを返す。新visitorによる正しい除外を誤って回帰扱いしないため、数値基盤を先に整える。

- G1-GR08-RAY: 旧float二次式の遠方球/カプセル誤hitを実再現し、doubleの直線距離・断面評価、有限円筒＋端球の最小正根へ変更。内部0/距離上限/float同距離順を維持し、微小線分/ほぼ平行も扱う。根を点の再構成までdoubleで保ち、局所double hitオフセットから法線を作る。第1周で見つかった大きいworld座標＋小radiusの法線崩れも修正して独立レビューPASS。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとproxy/visitor回帰成功。独立long double oracleの40,000件はhit17,229/miss22,771、誤分類/最近根逸脱0。parentでもoracleと法線再現binaryを実行確認。既存未実行facade試験のVector3単項minusを修正し実型の式syntaxを確認、Windows全体統合は未検証。
- Next: GR08の候補訪問/掃引AABBを集約へ接続し、新旧既定queryの経路と固定seed比較を揃える。G1監査で追加判明した共通Math/Curves、更新/pause/camera契約表、Capsule辺・角/初期侵入/Depthの受入れ試験もtodoへ登録した。G1完了とはまだ扱わない。

- G1-GR08-P4D: ray/形状bounds/掃引始終点AABBの訪問を集約へ接続。除外前のgeometry/filter/尺度確認、順序/MaxHits/全出力clear、巨大bounds時の全探索fallbackを維持。確定混在7,000件（各種類300件以上のhit）を全QueryProxy＋独立sortと全field照合。候補内IterationLimitは維持し、AABB非交差が証明できる対象はNoHitと確定する。比較で見つかった旧OBB rayのepsilon平行誤hitもdouble slab/局所法線へ修正。第1周でSweep OBBのfloat/double検証差を発見し、既存Mathの条件をIsValidSweepBoxへ抽出して本体/precheckで共有、第2周PASS。Pruning/Ray/Proxyの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとassert有効の既存GeometrySweep 1万oracle回帰成功。実Engine統合/Windowsは未検証。
- Next: G1／GR08の旧Raycast/Overlap入口を新query kernelへ転送して互換結果/符号/順序を固定する。その後Capsule受入れ、共通曲線、更新/pause/camera契約の残りを閉じる。

- G1-GR08-P4E: 旧Raycast/Overlap3種のModule/Broadphase入口を共通query kernelへ統一し、旧hit識別値/Point/Depth/順序とOverlap法線反転、直接Broadphaseの成功時追記を保持。旧無filter契約ではLayer0も検索し、新APIのbitmaskとは区別する。4,000固定seed全field比較・Layer0の4種類・失敗clear/容量/同距離を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERで実行成功、Pruning7,000件も成功。独立第2周PASS、実Module/所有配列の統合試験は追加済みだがWindows.h依存でcompile/実行未確認。
- Next: G1／GR08のCapsule面・辺・角・回転・初期侵入とOverlap深さの明示受入れを固定し、共通曲線と更新契約へ進む。

- G1-GR08-CAPSULE-ACCEPTANCE: 縦Capsule対Boxの面/辺/角、横向きCapsuleとCapsule同士を解析距離/法線で固定。初期接触0・侵入0.25・対称内部1.5のDepth/外向きNormal/Distance0/start印とignoreをMath・実query集約で確認し、新旧符号と深い重なりの近似限界を文書化。通常/O2-NDEBUG（assert有効）/ASan・UBSan（LeakSanitizer除外）/MEMBERと既存10,000件oracle成功。独立レビューPASS、45度回転/端点反転/辺角Depth/反対側押出し/直交Capsuleの追加反証も成功。production変更なし、Windows/World/Module統合未検証。
- Next: G1／GR03・GR04の最小Math/Curves共通化とGR01更新/pause/camera契約を閉じる。

- G1-GR03-CURVES: Math/Curves.hへ有限・単調key検証、区分線形、Linear/Power/Expo/SmoothStepを追加。InputAxisMathとHapticsEnvelopeMathが同じ数理を再利用し、JSON/既定値/所有型/独自失敗出力/振動duration規則は維持。第1周の極大floatから小値へのkey端点桁落ちをstd::lerpと±FLT_MAX/内部key/nextafter回帰で修正し第2周PASS。Curves/InputAxis/HapticsMixer/PlaybackTimeの4試験を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実行、Curves MEMBER compileも成功。後続GR119/GR121の入口/閾値と曲線の再利用契約を文書化。Windows/全bundle統合は未検証。
- Next: G1／GR01のComponent割当・Bridge進行と将来pauseの区別・最終camera境界を文書で確定し、G1検証範囲を一覧化する。

- G1-GR01-CONTRACT: 全Componentの群割当、可変とFixedの順序差、現Bridge gateと将来pauseの3分類、新Component登録規約を文書化。最終cameraは承認S3のGame OnLateUpdateで、Module Lateより後/通常Module Tickより前と明記し、G14未決配置とG16読取契約を区別。独立source照合PASS、コード変更なし。無視対象Roadmapへ矛盾を上書きする補足を保存。
- Next: G1最終照合で群境界の子Transform鮮度不足が判明。GR01-TRANSFORM-BOUNDARYとして修正し、その後に検証一覧を確定する。

- G1-GR01-TRANSFORM-BOUNDARY: Worldの最初の群前とsnapshot内の群遷移でWorld変換を確定し、Movementで動かした親を次のDefault/Animation/PoseFinalizeから子が同frameに読めるようにする。Camera→PreRenderも確定し、既存のLate空対象時の更新、同群内非同期、順序/収集/破棄/Fixed契約は維持。実World試験に全8群の親子孫・逆登録・同群observer・疎な群・空Lateを追加し静的レビューPASS。純TickGroup/DispatchはO2-NDEBUG/ASan・UBSan（LeakSanitizer除外）成功。実階層試験はWindows.hでcompile停止、実行未確認。追加階層走査コストは未計測。
- Next: G1検証一覧とCurves header登録を確定し、作者のG2移行指示に従いAssetCookLib分離など選定不要のG2基盤へ進む。

- G1-VALIDATION: GameFoundationValidation.mdへGR01/03/04/08実装と検証範囲を保存。最終portable確認は34件実行成功/実行失敗0、FixedStepSchedulerはWindows.hでcompile blocked1件。実World/Engine/Module/Windows/native/GPU/完全bundleは未実行として区別。追加群境界実World試験も未実行を明記。代表コマンド4件を文書どおり再実行成功。G1追加Publicヘッダ29件を照合しCurves.hの明示登録漏れを補完、全件exactonce。文書と実ログの照合PASS。
- Next: 作者承認に従いG2へ進む。AssetCookLib分離を先行し、G2-S8既存JSON/stb活用とG2-S2ソース隣サイドカーの2選定は返答待ち。それ以外の未決選定も勝手に確定しない。

- G2-P0-COOK-LIB: GR77/GR78以降の共通基盤として3cookerのcpp/hをSTATIC AssetCookLibへ分離し、AssetCookはMain.cppから同libをリンク。Coreとcooker公開includeをPUBLIC伝搬、内部includeはPRIVATE。既存7実装/headerとsmoke登録blockのbyte不変、単一source所属/依存順を検証し静的レビューPASS。cmake不在とWindows.h依存のため構成/実build/smoke実行・出力バイト一致は未検証。
- Next: G2-S8/S2返答待ちの間、GR86の現行128関節上限を共有定数へ集約する。値や受理条件は変更しない。

- G2-GR86-LIMITS: 現glTF decoder/cooker/NVSKEL 0.x loaderの128上限をResource/SkeletalLimits.hのLegacyMaximumJointCountへ集約。値/比較演算/0件拒否/エラー/format/vertex ABIは維持し、新版の256化で旧形式を緩めない名前にした。公開headerのC++23/O2/Werror・128/129境界・0〜1024述語一致、3参照とPUBLIC_HEADERS登録/BOMCRLFを確認、独立レビューPASS。3consumerのcompileはWindows.hで停止、実資産ロードの合格ではない。
- Next: G2のGLB共有処理とsidecar正本は作者回答待ち。GLB質問への承認をBVH/FBXまで広げない。新規外部parserや形式/既定値変更は選定前に進めない。

- G2選定: 2026-10-03 UTCにS8のGLB共有解析（既存JsonDocument/stb）とS2のsource隣sidecar/cook-loose共用を作者承認。BVH/FBXと他の未決選定には拡張しない。
- G2-GR77-CONTAINER: 無割当/借用SpanのGLB構造parserを追加。magic/v2/全長/整列/範囲/JSON先頭・BIN第2/既知重複を検証し未知chunkは無視、非GLBはBOM除去JSON view、失敗時clear。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERと4,000固定seed破損入力成功。独立66,470ケース反証と寿命/非整列/巨大境界も成功しPASS。JSON意味/padding照合や3消費経路の配線は未実装で、GLB資産の取り込み完了とは扱わない。
- Next: GR77のdata URI向け厳密Base64 primitive、続いて共有buffer解決へ進む。

- G2-GR77-BASE64: 無割当Span版の厳密base64検証/必要長/復号を追加。標準alphabet、末尾padding、canonical pad bits、容量/全span非交差を検査し、失敗時output非変更・成功tail保持。第1周のサイズ参照alias問題をBase64DecodeOutcomeの値返却へ変更し解消。RFC例、全256単byte/65,536二byte/4,096三byteを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERで成功。独立multi-quartet8,256/overlap39,576/padding24,576反証も成功、第2周PASS。data URI構文/percent/JSON/3経路接続は未実装。
- Next: GR77のdata URI/BIN意味検証、共有buffer所有と3経路への接続へ進む。旧.gltfのsource hashはBOMと外部buffer余剰byteも保持し、新しい埋込みbytesを二重hashしない。

- G2-GR77-BUFFER-SEMANTICS: 借用data URI view、対応4MIME/parameter/base64 flag、percent size/decode、GLB buffer0の宣言長と0〜3byteゼロpaddingを共有化。第1周のraw URI文字制約不足をMIME tokenとは別のurlchar検査で修正し、percent表記は維持。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBER、256byte percent変換と4,000変異成功。独立1,536ケース反証もPASS、第2周PASS。サイズ/viewは値返却、失敗非変更/借用契約を保持。JSON/I/O/実BufferSet/3消費経路は未接続。
- Next: GR77のBufferSet所有・外部/data URI/BIN解決へ進む。旧.gltfの全source hashを保ち、loose accessorのunchecked範囲検証は接続時に別修正として扱う。

- G2-GR77-BUFFER-SET: 外部/data URIの所有bytesと借用BINを共有し、宣言viewとhash用全sourceを分離。標準readerはpercent後の相対ASCII pathとcanonical component境界/通常fileを検査。第1周のcopy assignment例外によるmetadata/bytes不整合をcandidate copy→Swapへ修復し第2周PASS。実source/testのsyntax-Werror、move特性、MEMBER object compile成功。実URI predicateをsection GCで直接リンクして通常/O2/ASan・UBSan成功。所有/実I/O/確保失敗試験は未実行で、実allocator接続は既存utility不足、その先のMemorySystemはWindows.h依存で停止。代替allocatorなし。JSON/3consumerは未接続。
- Next: GR77のJSON buffer adapter、次いでcooker/画像/骨格/looseへ接続する。検出したloose accessor overflowとMemoryOverrides include不足は別タスクへ保存。

- G2-GR77-BUFFER-JSON: JsonDocumentのbuffers記述を共有resolverへ接続。既知field重複、型、正の安全整数、uri省略/null/ASCIIを区別し、所有URI配列を固定して呼出中Span寿命を保持。数値helperは通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERで成功、独立レビューPASS。25個のfixture JSON文法も別途確認。JsonDocumentを含むsource/testはWindows.h依存でcompile/実行未確認、数値試験で代用しない。
- Next: GR77静的cookerへcontainer/bufferを接続する。外部imageは旧どおりpath参照だけで、embedded imageの所有/packagingは後続へ分ける。

- G2-GR77-COOK-BUFFERS: 静的cookerを共有container/JSON buffer resolverへ接続し、BINをコピーせず宣言viewでmesh抽出する。元source/BOMと外部buffer余剰を含むhashを保持し、GLB/data URIを二重hashしない。102byte三角形の外部/GLB/data URI payloadと混在/BOM/余剰hash・不正/失敗出力保持を既存束へ登録。旧AA== smokeは短いbuffer拒否へ更新。material/cluster/wire/skeletalのsource不変、fixture整合、CMake参照、BOMCRLFを確認、独立レビューPASS。実container/buffer-sourceの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER成功。cooker/新試験のcompileはWindows.hで停止し、native/CMake/smoke/payload一致は未検証。
- Next: GR77の画像source共有解決へ進み、続いてembedded imageの結果寿命とmodel+texture package/manifest接続を行う。loose/skeletalも後続で、GR77全体は未完。

- G2-GR77-IMAGE-SOURCE: file URI/data URI/bufferView画像を共有解決。path/data bytesは所有、viewはbuffer index/offset/lengthだけを保持して宣言範囲へ再bindし、自己SpanとBINコピーを避ける。MIME/既知重複/排他/型/安全整数/範囲と失敗clear、copy→swapを固定。第1周で画像viewのbyteStride黙認を指摘され、値/null/重複を全拒否する存在検査と負例を追加、第2周PASS。外部URIとdata URIのcopy/move所有、BufferSet寿命を試験へ登録。実range helper通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERとmove特性成功。40JSON fixture文法/PNGJPEG prefix既知bytes確認。JsonDocument/native試験はWindows.hでcompile停止し、画像decode/消費経路は未検証。
- Next: GR77静的cookerのEmbeddedImagesと材質参照を接続し、GLB借用とlocal buffer由来画像の所有を区別する。その後Mainのmodel+texture packageとmanifest更新、loose/skeletalへ進む。

- G2-GR77-COOK-IMAGES: 静的cookerの材質をImageSourceへ接続し、imageIndex順のEmbeddedImagesへ仮想path・role/format・画像hashを保持。GLBだけ借用、外部/data URI buffer内imageと直接data URIは画像部分を結果所有し、local BufferSet破棄後のdanglingを防ぐ。copy→swap/自己所有領域の借用拒否、同format重複統合/srgb-linear衝突拒否。GLB入力はoutResult自身に所有させない契約を明記。実1pxPNGをstbでdecode成功、JSON23fragmentと174byte画像buffer fixture整合、実range3mode回帰成功。nativeにはpayload/3role/所有copy-move/画像hash/失敗全出力保持/JPEG pathを登録したがWindows.hでcompile停止、実行未確認。独立第2周PASS。Mainはpackage接続前のためEmbeddedImagesを出力書込前に拒否し、未生成textureへの参照を成功packageにしない。
- Next: GR77のMain model+N texture packageとmanifest一括統合を実装してguardを置換する。skeletal/looseは後続、GR77全体は未完。

- G2-GR77-PACKAGE-IMAGES: Mainのmodel-only guardをmodel+N textureの単一entry package群へ接続。全変換/メモリpackageとpayload検証→実file書込/flush→incoming全AssetSystem解決/bytes一致→manifest1回更新。audioのmergeをincoming列へ一般化し同keyだけ更新、serializerと従来model出力tailはbyte不変。第1周で保持entryのbacking packageを暗黙img出力が壊す衝突を発見し、canonical/equivalent/Windows case比較で書込前に一律拒否、実audio package衝突回帰を追加、第2周PASS。3GLB fixtureは実container/BIN/range/stbi memory decodeで正常2/異常1を確認。CMake3.14+PS smokeを登録したがCMake/PS不在、MainはWindows.hでcompile停止しCLI/package統合実行は未検証。複数file I/O rollbackなし、variant間画像共有を明記し、準備/確定・一時file置換はGR96へ残す。
- Next: GR77の骨格共有buffer/GLB入口を接続する。looseは既知のaccessor範囲計算を先に安全化してから接続する。GR77全体と実物61MB受入れは未完。

- G2-GR77-SKELETAL-BUFFERS: 骨格decoder/cooker/AnalyzeSkeletalを共有container/BufferSetへ接続し、GLB/data URIと外部bufferを宣言範囲で読む。旧metadata/抽出順、128関節/単一clip/変換/NVSKEL形式を保持。decodedは独立所有、hash用BINは借用、String互換出力は全source bytes所有copy。M9に3入力形のdecoded/NVSKEL/SourceHash、BIN pointer、BOM/余剰/旧出力/失敗clearを登録し、試験directoryを一意の排他作成へ変更。独立レビューPASS。実container/buffer意味の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER、M9 fixtureの実container/BIN、実配列/Spanの型syntaxは成功。骨格本体と新native試験はWindows.hでcompile停止し、decoder/cooker/analyzer実行とpayload一致は未検証。
- Next: GR77 loose accessorのunchecked範囲計算を先に安全化し、その後looseの共有buffer/画像bytes/stagingを接続する。実物GLB受入れと実Windows統合は未検証のまま区別する。

- G2-GR77-LOOSE-BOUNDS: looseの4accessorを純粋な宣言範囲helperへ通し、actual/declared/viewと正のcount/element/strideを減算・除算で証明してからpointerを作る。private CPU staging入口は候補完成時だけ出力置換。実helper通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で各5,438,750件（広い有効範囲1万を含む）、MEMBER compile成功。追跡glTF6fileの24layoutも通過（12実file長/12宣言長のみ）。小三角形の正常と短いview/宣言・巨大offset・0countをnativeへ登録したが、GLTFAnalyzer/実staging試験はWindows.hでcompile停止。独立レビューPASS。JSON getterや全形式の厳格化とは区別する。
- Next: GR77の画像bytes→CPU stagingと必須拡張判定を共有し、最後にlooseへGLB/buffer/imageを配線する。staging/GPU/実物受入れ未検証を完了扱いしない。

- G2-GR77-IMAGE-STAGING: file読込とbytes decodeを分離し、PNG/JPEG bytesから所有RGBA8/ARM3枚のCPU stagingを作る。Mainとsignature判定を共有し、旧外部fileの形式/既定RGBA8_UNORM/RGB→AO/Roughness/Metallicを維持。INT_MAX・dimension境界、非copy stb scope owner、候補成功時のみ移動、ARM出力alias拒否を追加。実signature/range通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとstbi memory 1px fixture成功。file/bytes/ARM/failure native試験は登録済みだがWindows.hでcompile停止、実staging未実行。独立レビューPASS。
- Next: GR77の必須glTF拡張拒否を共有し、looseへcontainer/buffer/image bytesを接続する。

- G2-GR77-DOCUMENT-PROFILE: 必須拡張の型/field重複/非対応を共通判定へ集約し、静的cooker・loose・骨格でbuffer/geometry前に拒否。空requiredとoptional usedは維持、cooker通常診断/骨格InvalidDocumentを保持。22共通fixture、実正常骨格の4拒否/2成功とString/bytes/source clear、loose正常geometryへの4必須拡張拒否を登録。ヘッダC++23/Werror・fixture JSON構文・CMake登録/BOMCRLF/diff成功、独立レビューPASS。実JsonDocument/consumer/testはWindows.hでcompile停止、native実行未確認。
- Next: loose接続前に発見したindex値の頂点範囲検査を補う。既存clusterizerはindexを信頼して頂点を参照するため、GLTFAnalyzer側で拒否してからGLB/buffer/image接続へ進む。

- G2-GR77-LOOSE-INDEX-RANGE: 全復号indexの頂点範囲をwinding/clusterizer前に検証し、宣言buffer内でも頂点配列外へ読む不正入力を拒否。正常geometry/材質は維持。純検査8,256組とnull/空/0頂点/UINT32_MAX、既存byte範囲5,438,750件を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で成功、MEMBER Werror compile成功。正常102byte fixtureのindex=3/65535の拒否と既存出力保持をnativeへ登録、Windows.hにより実行未確認。独立レビューPASS。
- Next: GR77 looseへ共有container/buffer/image bytesを接続する。G2他選定とGPU/実物受入れの未検証は維持する。

- G2-GR77-LOOSE-SOURCES: static looseを共有container/BufferSet/ImageSourceへ接続し、BIN借用・外部/data URI所有をscope内に保持して独立CPU pixelsへ復号。外部logical requestは従来どおりfinalize遅延、埋込みは空RequestPathでRGBA8_UNORM2枚/ARM3R8を保持。不正material imageは伝播失敗し候補を公開しない。集計metadata/read_total stageを維持。新native試験に4embedded経路と外部絶対/相対、5texture/geometry、壊れたcontainer/image/view/必須拡張/8byte PNGの失敗保持を登録。fixtureの実Container/range/stbは通常/O2-NDEBUG/ASan・UBSan成功。独立レビューで試験末尾文字列を修復し、const pointer listを明示配列化、最終PASS。nativeはWindows.hでcompile停止。実物大型GLB/GPUは未入手・未実行で別受入れへ保留。
- Next: GR77の主要3経路の配線が揃ったため、承認済みGR78の共有sidecar/設定・変換へ進む。宣言MIMEと実signature不一致拒否は非blocking残件として登録。GR77全体のnative/実物受入れを完了とは扱わない。

- G2-GR78-SETTINGS: cook/loose共用のv1取り込み設定をCore privateへ追加。units/fit/符号付きaxes/mirror/origin/UV/windingの値・型・有限性・矛盾とJSON未知/重複を検証し、metaはhashから除外、未実装予約blockは空objectのみ。成功時だけ出力置換。固定52byteのLE/binary64/-0統一で正規化し、sidecar無しは呼出側で別扱い。純値/軸36組/既知bytes/各field変更を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER-Werrorで成功。43JSON fixture文法確認、実JsonDocument source/testはWindows.hでcompile停止。独立レビューPASS。file探索/変換適用/hash/CLIはまだ未接続。
- Next: GR78の軸/scale/fit/原点/法線/UV/winding共有変換を実装し、その後sidecarを既存ロードとcookへ接続する。既存モデルはsidecar無しなら恒等・現行hash不変を維持する。

- G2-GR78-TRANSFORM: 軸/鏡像、scale/3種fit、bounds/足元/表面/custom原点、法線/UV/windingの無確保2pass変換を追加。全layout/index/結果検証後に書き込み、失敗非変更・非整列/未指定field保持を実装。48方向、全fit、各原点、極端値を純実装の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER-Werrorで成功。ただし独立第2周で累積表面重心の中間丸め相殺が残ったためblocked。最新修正は各頂点×面積とfma残差をmoment展開に直接加え、最後に3×面積で除算する。M=2^100/t=2^-100の2面と面順序/循環順の回帰も3mode成功だが最終差分の独立確認は未完。ロード/cooker未接続を維持する。
- Next: GR78-SIDECAR-IO-HASHを先行する。変換適用部分は保留を維持し、設定file選択/読込/hashの独立部分を進める。

- G2-GR78-SIDECAR-IO-HASH: source全名+.import.json/明示override/required/disabledを共有選択し、自動探索の真の不在だけ既定へ戻す独立APIを追加。通常file/1MiB/BOM/NUL/短読・増大/厳格JSON/出力保持を定義。第1周でMSVCの不正名/network障害がerrc不在へ畳まれる点を指摘され、Windows raw system error2/3限定へ修正し第2周PASS。hashは既存FNV stateへ長さ52LE64+正規化bytes+algorithm1LE32を連結し、無しなら完全不変。3既知値と変更条件を実hash通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で成功。HashTest/FileTestの実MEMBER Werror compile成功、Windows不正名回帰は登録のみ。実file loaderはWindows.hでcompile停止、I/O/JSON runtime未確認。変換/CLI/ロードは未接続。
- Next: 作者へ表面重心を保留し他の原点/scale/axes接続を先行する案を確認中。返答までは保留を維持し、独立したGR77のMIME/signature整合を補う。

- G2-GR77-MIME-SIGNATURE: 埋込みPNG/JPEGの宣言MIMEと実signatureを共有predicateで一致検査し、cooker結果の公開/packaging前とlooseで拒否する。外部画像と有効入力bytes/hashを変更せず、未知signatureの旧CLI reasonを維持。不一致1200byte GLB、native cooker/looseの失敗出力保持とCLI既存manifest/model保持を登録。pure判定は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行、MEMBER compile成功、fixture構造/PNG CRC確認。独立レビューPASS。native cooker/loose/CLIはWindows.h/CMake制約で未実行。
- Next: GR78縮小スコープ案の返答待ちを維持。G2検証時に見つかったMemoryOverrides.hの不足utility includeを既存todoとして独立修正する。

- CORE-MEMORY-HEADER-UTILITY（G2検証基盤）: MemoryOverrides.hへstd::forwardを宣言するutilityを1行追加。実header単独の追加前compile失敗を再現し、追加後はC++23/Werror syntaxとNew<Probe>(int&&)の実object compile成功。独立確認でもPASS。BOM/CRLFと他bytesを保持し、allocator/runtime/API意味は変更しない。MemorySystemのWindows依存解消や実allocator実行とは区別する。
- Next: G2／GR78の表面重心を後回しにして他変換を接続する案は作者回答待ち。G2の未確定形式選定も維持し、承認無しに保留変換やNVSKEL形式を適用しない。

- G2-GR78-TRANSFORM（縮小scope）: 2026-10-03の作者承認でsurface_centroidを未対応として明示拒否し、keep/bounds中心/足元/custom・scale/fit・axes/mirror・UV/windingを先行。候補の表面面積/重心コードをactive実装から除き、値/JSONはUnsupportedFeature、変換はUnsupportedOrigin、canonicalはSize0、失敗出力保持を固定。48方向を含む変換/値の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）6実行とMEMBER compile成功。44JSON fixture文法確認、nativeJSON未実行。新scope独立レビューPASS。SurfaceCentroidは別保留へ分離し、他変換の接続を再開できる状態。
- Next: GR78-STATIC-SIDECARとして静的cook/looseへ共有file設定と変換/hashを接続し、その後骨格の一様scaleを別反復で接続する。

- G2-GR78-STATIC-SIDECAR: 静的cook/looseへ共有設定file/変換をcluster前に接続し、設定有りだけsource_hashへ正規化を連結。無し/disabledは旧経路、結果に採用設定を所有してCLI/looseへ診断する。looseは設定適用後の非有限bounds/coneも公開前に拒否。native統合試験にglTF/GLB・各変換・無し/恒等/disabled・meta/hash・override/required・surface/巨大scale失敗保持を登録。第1周の空source locator互換低下を修正し、自己完結GLB/data URIの空/非空同値とrequired/overrideを追加、第2周PASS。共有kernel/hash実行成功、実cooker/loose/統合試験はWindows.hで未実行。
- Next: 新しい入力sidecarをcook出力先で上書きしないguardを追加してから、静的接続と併せて公開する。骨格一様scaleはその後に進める。

- G2-GR78-OUTPUT-SIDECAR-GUARD: 採用sidecarをmodel package/manifest/派生texture packageで上書きするpath・symlink・hardlink aliasを全write前に拒否。既存Windows case比較を再利用し、canonicalize失敗も拒否する。Mainの実helper textをLinuxで直接compileし、同一/正規化/hardlink/symlink/別fileを通常/O2/ASan・UBSan（LeakSanitizer除外）で成功。CLI smokeに2直接alias、通常cook、派生texture hardlinkと設定/モデル/manifest保持を追加し独立PASS。Main全体/Windows/CLI/CMakeは未実行で、helper検証と区別する。
- Next: 静的sidecar接続とguardの2コミットをまとめて公開し、GR78-SKELETAL-SIDECARの一様scaleへ進む。SurfaceCentroidは作者合意どおり拒否を維持。

- G2-GR78-SKELETAL-SIDECAR: 共有scale/fit倍率を骨格の頂点位置・IBM平行移動・Translation sample・mesh-node平行移動へ同時適用し、raw/legacy/loose/cookへ接続。fitはmesh-node線形変換後のasset空間寸法で決定し、回転/scale/法線/UV/weight/形式は維持。設定snapshotをcookとhashで共有し、骨格非対応のaxes/origin/mesh変更は拒否する。設定出力alias guardも骨格CLIに接続。共有primitiveを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実行成功、MEMBER compile成功。native試験に非identity Scale fixture、0/1/2秒の回転終端、raw/cookedのscale/fitと実sampler比較、設定有無/hash/失敗保持/CLI guardを登録。独立第2周PASS。実decoder/cooker/sampler/CLIはWindows.h等の環境制約で未実行、登録と実行を区別する。
- Next: GR78-CLIの設定指定/require/no-sidecar、単体skip、inspectを進める。surface_centroidと実物描画受入れは保留を維持する。

- G2-GR78-CLI-SETTINGS: --import-settings（別引数/equals）・--no-sidecar・--require-sidecarをstatic/skeletalの共有loaderへ接続。明示設定優先、排他/重複/空値/値付きbool/非model拒否、設定出力alias保護を維持。実parserの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功、独立確認PASS。GLB設定優先/無効化のbytes/hash回帰/失敗出力保持/明示正本保護と骨格override/disabledのCLI smokeを登録。Main/native CLIはWindows.h等により未実行。skip/inspectはまだ未知引数として拒否する。
- Next: GR78-CLI-SKIPを独立反復に分離。元source/buffers/設定hashをcook前に計算し、manifestと実package/派生画像を検証してから省略する。inspect診断はその後。

