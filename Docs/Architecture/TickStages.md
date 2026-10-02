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

公開群、Component設定、Worldの一回収集と群別実行・LateTick、Application/Moduleの後段配線を実装した。アニメ姿勢公開を接続済みで、カメラの群移行は後続である。

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

## Application/Moduleの後段

- TickSimulationはGameMode→World Tick→Particle Tick→固定step→World LateTick→Module LateTick→Handler OnLateUpdateの順に実行し、その後に描画同期を行う。
- 固定stepが0回でも後段は1回実行する。シミュレーション停止中は前半/後段を呼ばず、schedulerの累積値を保持する。
- Module LateTickはRunningのregistryで登録順。既存TickAllの位置（描画同期後）は変えない。
- OnUpdate/Script維持とOnPreRenderの位置は変更しない。Handlerはフレーム内で共有所有し、callbackによる差し替えでもそのフレームの対象を生存させる。
- ApplicationFixedStepPipelineTestの既存11ケースを維持し、全8群の逆登録順、0/1/2 fixedstep、DefaultとCameraの位置読出し対照、pause/resume、空/非Running registryの6ケースを追加した。Windows依存により17ケースは未実行。実フレームの外側順序とhandler寿命は静的レビューで確認した範囲である。

## 評価済みボーン姿勢

- SkinnedMeshComponentはAnimationで再生時刻を進め、PoseFinalizeでEvaluatePoseを呼ぶ。非表示でもゲーム用の姿勢は評価する。
- EvaluatePoseはdirty時だけSampleし、成功した再評価だけGetPoseSerialを増加する。描画proxyも同じキャッシュを使う。
- JointModelMatricesはinverse bindを含まないEntity空間の行ベクトル行列。既存BonePaletteの計算式は変えない。
- FindJointIndexはIdentity名を解決し、未発見/空名は-1。SkeletonResourceのSetJointsで索引を再構築し、重複は先頭優先、Unloadで消去する。
- TryGetJointModelMatrix / TryGetJointWorldMatrix / TryGetJointWorldTransformは自動評価しない。未評価/dirty/資産無効/範囲外はfalseで出力を維持する。
- ワールド行列はJointModel * OwnerWorld。読み取り時に合成するためrootの即時変更に追従する。child階層は既存WorldのUpdateWorldTransforms境界の確定値を読む。
- TryGetJointWorldTransformはEngineのCreateWorldRowVector（列scale）で再構成できる正scaleのTRSのみ成功する。shear/反転/退化をTRSへ黙って丸めない。完全な表現にはWorldMatrixを使う。
- 資産wrapper/子Resourceのunloadは読取りを拒否する。子Resourceの差替えはweak参照の同一性で再評価する。同じResource内のSetJoints/SetClipなどの編集は自動revision検出を行わず、SetSkeletalAssetで同じassetを再設定して姿勢を無効化する。
- SamplingTestへ追加した姿勢・serial・名前引き・資産寿命試験と既存FramePacket/M9はWindows依存で未実行。既存失敗の再現や、新しい失敗が増えていないことはこの環境では確認できていない。

- Mesh差し替え時は既定のmeshNode transformも再取得する。SetMeshNodeGlobalTransformによる明示overrideは維持し、SetSkeletalAssetで既定へ戻す。
