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

## 段階への割当とポーズ（接続時の契約）

- 既存ComponentはDefaultのまま。EntityのTickもDefaultで実行する。
- SkinnedMeshComponentはAnimationで時刻更新、PoseFinalizeで姿勢確定する。
- SpringArmComponentはCameraで固定ステップ後の位置を読む。
- 後半の群、Module LateTick、Application OnLateUpdateを含め、シミュレーションが停止したフレームは進めない。
- Moduleの従来TickAllは描画同期後の意味を維持する。

## 実装状況

公開群、Component設定、Worldの一回収集と群別実行・LateTickを実装した。ApplicationからLateTickへの配線、Module/Application後段、アニメ姿勢公開、カメラの群移行は後続である。

## Worldの収集と破棄

- World::Tickで全群とFixed対象を一回収集する。主群・mask・優先度・走査順はフレーム内で固定する。
- EntityのDefault優先度は自分のDefault群Componentの最小値（なければ0）。Entityを先に採番し、同じownerのComponentより先に実行する。全既定0では従来の深さ優先・登録順を保つ。
- 呼出直前にownerの所属/親のactive/pending、自身の有効状態を確認する。
- World::LateTickは再収集せず、PostPhysics前とCamera前に変換を更新する。Fixed対象も同じフレームの収集結果を使う。Tick前の単独Fixed呼出はその呼出だけの収集結果を使う。
- 更新・cleanup通知中のRemoveComponent/RemoveEntityは破棄予約へ変換する。ComponentのMarkForDestroyはpendingを設定する。
- 既存のWorld::Tick後と各固定ステップ後のcleanup位置は維持する。実破棄前に保持entryを無効化する。
- ObjectHeap/GCの即時削除もEntity::RemoveInnerを通して無効化する。callback自身が即時破棄された場合、Worldは戻り際に対象を触らない。即時削除した対象をcallback側でも再利用してはならない。
- Context所有の対象を手動deleteせず、所有者の除去APIを使う。WorldのFinalizeは更新/cleanupへの再入中に実行しない。

## 検証範囲

群・設定・順序/無効化helperのportable試験は実行した。実Worldの追加/削除、ObjectHeap連携、固定step後cleanupを対象とするWorldTickGroupTestを既存bundleへ登録しているが、Windows依存により現環境では未実行。helperの合格をWorld/Game統合の合格とは扱わない。
