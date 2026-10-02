# 入力基盤の契約

## 選定

2026-10-02作者承認: S4=a（Raw Input＋cursor固定/非表示・非active解除）、S5=a（XInputを交換可能なdeviceの背後へ）、S8=a（暫定working directory下のuser bindings、IInputBindingStore抽象、GR76で保存先を統合）。実装が済んだことは意味しない。

## InputStateの一括解除

- ReleaseAllは現在の全キー/マウスボタンをupにし、mouse deltaとscroll累積を消去する。modifierも解除される。
- 解除時にdownだった入力は、同frameで押されたものも含めてReleasedを次のBeginFrameまで保持する。反復ReleaseAllはedgeを翌frameへ延長しない。
- Releasedは照会で消費しない。一frame中は何度読んでもtrueで、次のBeginFrameでfalseになる。同frame再押下時にはDownとReleasedが共にtrueになり得る。
- Pressedは既存のcurrent/previous比較を維持する。そのため前frame held→解除→同frame再押下のPressedはfalse。後続MapperはRouterイベントを併用し、同frame内の短いtapや再押下を取り逃さない。
- 絶対位置は保持し、次のSetMousePositionで基準を再設定してInputStateの移動ジャンプを防ぐ。以降は通常の差分累積に戻る。
- この段階は状態APIだけ。InputSystemのevent.Delta、Windowのfocus/cursor、controller内のdrag/armedの解除は未接続で、OSでフォーカス問題を解消したとは扱わない。

## 正規化軸と視点単位

- InputAxisResponseはdeadzone [0,1)、Linear/Power/Expo、Gamma > 0、Expo [0,1]を持つ。全パラメータのfinite/範囲とcurve enumを検証する。
- 1Dは符号を保ち、2Dは半径にdeadzoneとcurveを適用する。成分別のcurveで方向を変えない。有限の範囲外入力は長さ1へ制限する。
- deadzone後の長さは (min(length,1)-deadzone)/(1-deadzone)。PowerはそのGamma乗、Expoはx*(1-e)+x^3*e。2Dは元の方向へ戻す。
- この応答は正規化stick/移動軸用。マウスのframe変位を[-1,1]へ丸めない。
- TryComputeLookDeltaはmouse変位×度/単位 ＋ 正規化stick×度/秒×実時間秒を度/frameで返す。反転は入力符号へ適用する。移動入力値を一律この視点APIへ通さない。
- 無効値/無効設定/結果overflowはfalseかつ出力0。radial関数は入力と出力が同一Vector2でもよい。計算途中はdoubleを使い、有限float巨大入力の正規化を壊さない。
- この段階はMapperが使う数理で、入力注入やGame操作への接続は後続。30/60/144Hzで同じ総マウス変位/同じstick時間の結果が一致する試験を持つ。

## ボタンの時間状態

- InputButtonStateは、Mapperが複数bindingを集約したdownを受ける。最初のbindingの押下でPressed、最後のbindingの解除でReleasedになるよう、呼出側がORしたdownの遷移を渡す。
- BeginFrameはPressed/Released/Tap/DoubleTap/HoldStartedなどの瞬間状態だけを消す。HeldとHoldのlevel、時刻、待機fixedPressは維持する。
- AdvanceToはfiniteで非負・単調な絶対実時間秒を受ける。逆行や非finiteは状態を一切変えずfalse。入力イベントの時刻粒度は呼出側が決め、kernelが勝手に時刻を取得しない。
- HeldDurationはdown中の経過時間、ReleasedHeldDurationはrelease時の経過時間をそのframeだけ保持する。HoldSecondsに達するとHold levelとHoldStarted edgeが立つ。
- release時にduration<=TapMaxSecondsかつHold未発火ならTap。release間のgap<=DoubleTapMaxGapSecondsの2tapを非重複の組としてDoubleTapにする。3tap目は新しい組の1回目。
- 同frameの押下→解除もPressed/Released/Tapを残す。snapshotのlevelとedgeは同時に成立し得る。各edgeの照会は消費操作ではない。
- fixedPressはbool latchで、固定stepが0回のframeでも消えず、ConsumeFixedPressで一度だけ消える。未消費中の複数押下は1つへ合流する。全押下回数のキューではない。
- Cancelはfocus/context喪失用。downならReleasedを残すが、Pressed/Tap/DoubleTap/HoldStarted・doubletap履歴・待機fixedPressを取り消す。通常releaseと違い、操作完了としてTapを発火させない。
- 閾値は仮の既定値を持つ設定。Tap/gapは非負、Holdは正、全finite。SetTimingはheld中と不正設定を拒否し、受理時はdoubletap履歴を消す。ゲームの操作意味は後続bindingsデータで決める。
- 現段階ではMapper/Router/Windowへの配線は未実装。純kernelの成功を実機入力の受入れ完了とは扱わない。
- 時間閾値はstart＋intervalの絶対deadlineと比較する。0.2/0.3等でduration差分の丸めがinclusive境界を反転させないため。deadline加算が+Infになる場合、有限時刻ではHoldに未到達、Tap/gapの上限内として扱う。正のintervalが同じ時刻へ丸められても、0経過ではHoldにしない。

## UI消費後の押下許可（armed）

- InputStateのキー/マウスrelease serialはdown→upごとに進む。ReleaseAllも含み、重複upでは増えず、BeginFrameでも消えない。uint64の周回比較なので2^64回を観測間に跨ぐことは保証外。
- InputArmedStateはInputSystem正本更新後、Routerで到達したPressedを受けたときだけ許可を記録する。Repeatは新規許可しない。生のDelegateを購読してUI consumeを迂回しない。
- 読み取りはarmedかつ正本downかつ許可時のrelease serial一致が必要。UIがreleaseと再pressを同じframeで両方消費し、正本の最終状態がdownでも旧許可は失効する。
- Shift/Ctrl/Altも左右それぞれのrouted許可を用いる。物理的にdownなだけの修飾キーからchordを成立させない。
- Resetはfocus/context/binding変更用に全許可を忘れる。heldのまま戻ってもRepeatでは復活せず、新しい到達Pressedを待つ。Reconcileは失効済みの記録を掃除するが、呼ぶ前でも照会は正本/serialを照合する。
- 配送済みイベントの処理を後から最終InputStateだけで再現する口ではない。Mapperは即時callbackで許可と順序を保持し、短いpress/releaseもボタンkernelへ渡す。
- Keyboard/Mouseから始めたkernelをPadにも拡張したが、Router/Mapperへの接着は後続。Linux試験は実InputStateと実armed処理へcallback欠落を与える試験で、ImGuiの実機操作を検証したものではない。

- 同じInputState正本を継続して使うことが前提。別正本への差替え/再初期化時はResetする。serialは正本内の履歴であり、別instanceを識別するIDではない。

## 物理入力元とbinding値型

- GamepadTypesは4slot、14buttonの独自mask、4つの[-1,1]axis、2つの[0,1]trigger、接続状態とpacket番号を持つ。Windows SDK型を公開しない。未接続はbutton/axis/triggerが0で、packetは履歴値を許す。
- GamepadButtonのbinding codeは有効な単独bitだけ。StateのButtonsはその組合せを許し、予約bitを拒否する。正規化と実device pollingは後続XInput adapterの責務。
- InputPhysicalSourceはKey/MouseButton/MouseDelta/MouseWheel/GamepadButton/GamepadAxis/GamepadTrigger、code、slotを持つ。MouseDeltaはX=0/Y=1、Wheelはvertical=0/horizontal=1。Gamepad以外のslotは0。
- codeは狭いenumへcastする前に範囲検証する。Key None/Count、Mouse Count、未知source、複合button code、不正slotを拒否する。
- InputBindingはtarget X/Y、finite scale、invert、Shift/Ctrl/Altの修飾mask、[0,1]のbutton thresholdを持つ。Axis1D/Buttonではtarget Xだけ、Axis2DではX/Yを許す。scale 0は無寄与、負scaleとinvertは符号指定。
- Button/Axis1D/Axis2Dは既存のInputAction（Pressed/Released/Repeat）とは別の型。軸の出力はNormalizedまたはFrameDeltaで、MouseDelta/WheelをNormalizedへ暗黙に丸めない。Buttonの変位sourceは後続で瞬間impulseとして扱う。
- 値型の宣言/検証は実deviceの実装を意味しない。XInput、Mapper、JSONへの接続はまだ行っていない。

## パッド入力の正本

- InputStateはslotごとの現/前frameのGamepadStateを値として返す。SetGamepadStateはslotと全値を検証してから一括更新し、invalidではsnapshot/edge/serialを何も変えない。
- PadのPressed/Releasedはframe内の遷移をラッチする。同frame短押下/解除でも両方trueになる。BeginFrameは前stateを保存してedgeだけを消す。
- 切断はneutralなsnapshotだけを受け付け、Pressedを取り消してReleasedを残す。ReleaseAllもPressedを取り消し、物理接続フラグとpacketは保持してbutton/axis/triggerだけneutral化する。
- buttonごとのrelease serialはdown→upで進み、frame/切断/再接続を跨いで維持する。Padのarmedも到達Pressedと現在down/serial一致で判定し、Repeatでは再許可しない。ResetGamepadは指定slotだけを忘れる。
- invalid slot/button/axis/triggerの照会は空値/false/0。snapshotは値返しで、可変内部配列の参照を外へ保持させない。
- InputSystemでのpad event配送、XInput polling、振動、UIと実機の接続は未実装。この段階は実InputStateにsnapshotを供給する純ロジック試験まで。

## 設定の所有と変更

- InputBindingSetはIdentityをキーとしてcontext/action/binding配列を所有する。同context内の重複actionと重複contextを拒否し、contextが違えば同じaction名を別定義できる。
- Add/Replace/SetBindings/SetContextCursorModeは既知対象と全値を検証してから変更する。不正設定・未知対象では既存設定を維持する。空bindingsは明示unbindであり有効。
- InputActionSettingsは型/出力、axis response、button timing、mouse/rate感度を持つ。全settingsを検証し、未使用型の設定でも非finiteや不正範囲を受け付けない。
- Find/Getは借用viewで、次の変更/破棄で失効するものとして扱う。Mapperは長期pointerを保持せず、設定をcompile/copyしてruntimeを所有する。SetBindings/ReplaceActionは内部viewを入力に渡した場合も先にcopyしてから置き換える。
- 設定全体のcopyは配列をdeep copyする。Identity文字列は既存pool/literalの寿命契約を使う。JSON文字列からはinternしたIdentityを作り、parserのborrowed viewを設定内に保存しない。
- CursorModeはNormal/Hidden/Confined/Lockedの値型だけを追加した段階で、IWindowやOS状態を変更していない。
- Settings/cursorの純検証はLinuxで実行。InputBindingSetの所有/copy試験は既存bundleへ追加したが、Identity→StringのWindows.h依存でコンパイル・実行は未検証。純検証の合格を所有/Mapper統合の合格とは扱わない。

- 所有APIのboolはvalidation拒否を表し、allocation失敗は例外として伝播する。copy assignmentの強い例外保証は約束しない。JSON等の全体更新は候補を構築・検証し、成功時だけmoveで入れ替える。

## アクション評価核

- InputActionRuntimeはsettings/ボタン時間状態/軸/相対変位だけを所有する。binding span・InputState・armedは呼出中だけ借用し、保持しない。Configure後は同じcompile済みbindingsを渡し続け、再設定時は外側でCancel/armedのResetを行う。
- Configureは成功時に全入力状態を初期化し、単調時刻だけ維持する。不正settingsは非変更。旧Releasedを通知したい場合はConfigure前にCancelの結果を読む。
- BeginFrameは単調な実時刻でedge/変位/軸を初期化する。SyncButtonsをRouter到達イベント直後に呼び、全persistent bindingのORを反映して同frame短tapを保持する。相対sourceのButtonは到達時にpress/release impulseを作る。
- modifierにもarmedを使う。相対変位は到達イベント時の修飾条件で加算し、frame末のmodifier状態で遡って削除しない。正本のglobal mouse累積を読んでUIを迂回しない。
- persistent laneはscale→invert後に合計し、1D clamp/2D長さ制限とdeadzone/curveを一度適用する。FrameDeltaだけが相対変位×mouse感度＋正規化lane×rate感度×unscaled dtを出す。相対laneへcurve/dtは掛けない。
- Buttonは変換後valueが正かつthreshold以上でdownになる。複数bindingのORと持続中の相対impulseは不要なreleaseを出さない。Pad axis/triggerは正本のpolling値で、context/focusの遮断は外側Mapperの責務。
- Updateは現在frame時刻でbuttonを同期した後、指定時刻まで進める。eventの精度は呼出側のframe時刻粒度。invalid/計算overflowはfalseで既存結果/時刻/蓄積を保持し、再評価やCancelが可能。外側は失敗を無視せず安全にCancelすること。
- Cancelは通常release完了と区別し、Tap/DoubleTap/Hold/固定step待ち押下/軸/変位を消す。focus/context切替時はarmedもResetして押しっぱなしを再許可しない。
- このkernelはOS/Router/Identityから独立して実行検証する。InputMapper・JSON・Engine・Raw Input・XInputの接続と実機受入は引き続き別段階。

- 軸の合計はcurveまでdoubleで保ち、最後だけfloatへ変換する。先に正規化したfloatの半径を再評価すると、巨大Gammaで長さ1の入力が0へ落ちたり、微小入力がcurve前に消えるため禁止する。

## Raw・Pad・全解除の配送口

- MouseStateは絶対位置差分とRawDeltaXYを別に持つ。rawと縦横wheelはfinite/float範囲を検証して成分を一括加算し、BeginFrame/ReleaseAllで消す。失敗時は正本も通知も変えない。
- InjectRawMouseDelta/InjectMouseScrollAxes/InjectGamepadStateは正本更新→Delegate→Router。Raw/Pad buttonは優先度順にconsume可能。ImGuiはRawをWantCaptureMouseで遮断し、絶対位置と二重供給しない。横wheelとX1/X2はImGuiの対応入力へ渡す。
- Padはsnapshot全体を検証後に更新し、接続変更を全controllerへ通知してから、新Pressed→旧Releasedの順で配送する。切断通知はconsume不能なので、Mapperが通常releaseのTap完了より先にCancelできる。
- InputSystem::ReleaseAllは正本をneutral化し、全controllerへOnInputResetを通知する。通常Releasedを合成しない。Delegateを含む通知callback内からの再入Inject/ReleaseAll、Router登録変更は禁止。
- InjectMouseMoveのevent deltaは正本の累積前後差とし、初期化/ReleaseAll後の初回絶対座標で大きく飛ばないよう一致させる。
- OSのRaw/XInput供給、focus喪失からReleaseAllへの呼出し、legacy各controllerのReset対応、Mapperの接続は次段。配送口の実装だけでフォーカス解除や実機入力が動いたとは扱わない。
