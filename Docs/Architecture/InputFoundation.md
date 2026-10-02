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
