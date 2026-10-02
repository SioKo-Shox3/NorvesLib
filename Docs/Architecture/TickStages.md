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

## 段階への割当とポーズ

- 既存ComponentはDefaultのまま。EntityのTickもDefaultで実行する。
- SkinnedMeshComponentはAnimationで時刻更新、PoseFinalizeで姿勢確定する。
- SpringArmComponentはCameraで固定ステップ後の位置を読む。
- 後半の群、Module LateTick、Application OnLateUpdateを含め、シミュレーションが停止したフレームは進めない。
- Moduleの従来TickAllは描画同期後の意味を維持する。

## 実装状況

公開群とComponent設定を追加した段階。Worldの群別実行、寿命管理、後段Module/Application、アニメ姿勢公開、カメラへの接続は後続である。従来のWorld::Tickはまだ従来の順序で実行する。
