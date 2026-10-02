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
- この段階はKeyboard/Mouseのkernelで、Router接着とGamepad側の同等契約は後続。Linux試験は実InputStateと実armed処理へcallback欠落を与える試験で、ImGuiの実機操作を検証したものではない。

- 同じInputState正本を継続して使うことが前提。別正本への差替え/再初期化時はResetする。serialは正本内の履歴であり、別instanceを識別するIDではない。

## 物理入力元とbinding値型

- GamepadTypesは4slot、14buttonの独自mask、4つの[-1,1]axis、2つの[0,1]trigger、接続状態とpacket番号を持つ。Windows SDK型を公開しない。未接続はbutton/axis/triggerが0で、packetは履歴値を許す。
- GamepadButtonのbinding codeは有効な単独bitだけ。StateのButtonsはその組合せを許し、予約bitを拒否する。正規化と実device pollingは後続XInput adapterの責務。
- InputPhysicalSourceはKey/MouseButton/MouseDelta/MouseWheel/GamepadButton/GamepadAxis/GamepadTrigger、code、slotを持つ。MouseDeltaはX=0/Y=1、Wheelはvertical=0/horizontal=1。Gamepad以外のslotは0。
- codeは狭いenumへcastする前に範囲検証する。Key None/Count、Mouse Count、未知source、複合button code、不正slotを拒否する。
- InputBindingはtarget X/Y、finite scale、invert、Shift/Ctrl/Altの修飾mask、[0,1]のbutton thresholdを持つ。Axis1D/Buttonではtarget Xだけ、Axis2DではX/Yを許す。scale 0は無寄与、負scaleとinvertは符号指定。
- Button/Axis1D/Axis2Dは既存のInputAction（Pressed/Released/Repeat）とは別の型。軸の出力はNormalizedまたはFrameDeltaで、MouseDelta/WheelをNormalizedへ暗黙に丸めない。Buttonの変位sourceは後続で瞬間impulseとして扱う。
- 値型の宣言/検証は実deviceの実装を意味しない。XInput、Mapper、JSONへの接続はまだ行っていない。
