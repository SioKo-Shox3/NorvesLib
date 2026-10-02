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
