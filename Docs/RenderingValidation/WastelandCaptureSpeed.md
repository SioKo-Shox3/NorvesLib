# 荒野の撮影の時間と構成

荒野（`wasteland`）の撮影の、起動から撮り終わるまでの時間を段ごとに測り、撮影の構成を決めた記録。判定日: 2026-10-16。
ブランチ `feature/scene-launcher`。タスク SCN-WL-CAPTURE-SPEED。

## 決めたこと

- 荒野の撮影（`Scripts/CaptureScene.ps1 -Scene wasteland`）と周回スモーク（`Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland`）は
  **RelWithDebInfo の Game** で回す（`-Configuration RelWithDebInfo -Build`）。`-Build` で撮る前にその構成の Game をビルドし、古い exe で撮らない。
- Debug の Game のビルド（`cmake --build build --config Debug --target Game -- /m:1`）は各項目の verify に残す。
- RelWithDebInfo は Vulkan の検証レイヤーを有効にしない（`CaptureScene.ps1` は Debug のときだけ検証レイヤーのログを数える）。
  描画の経路やリソースの扱いを変える項目は、検証レイヤーの確かめを Debug の起動画面の撮影（`CaptureStartupScene.ps1`）などで別に行う。
- Release には計測・デバッグの機能を足さない（ログが無いので撮影の検査にも使わない）。

## 段ごとの時間（手本の 6 視点、1920×1080、1 回の起動で順に撮る）

起動からの秒。Game.log の時刻から求めた（`SCENE_READY` はシーンの組み立てと公開の完了）。

| 段 | Debug | RelWithDebInfo |
|---|---:|---:|
| シェーダーのコンパイル（起動時に GLSL から SPIR-V へ、77 本） | 0〜86.5 秒 | 0〜12.8 秒 |
| 地形の区画の読み込み（近景の DEM 100・遠景 98 の区画と置き物、206 個のクック済みメッシュの解析と MegaMesh の作成。DEM の区画は 1 個約 100 万三角形） | 168.9 秒 | 60.2 秒 |
| 崖錐の岩の生成（275,000 片）から SceneReady まで | 74.3 秒 | 5.0 秒 |
| SceneReady | 330.2 秒 | 78.3 秒 |
| 最初の視点を撮るまで（落ち着くまでのフレーム） | 8.2 秒 | 12.8 秒 |
| 残りの 5 視点（切り替え・ページの読み込み・落ち着くまで） | 160.8 秒 | 70.0 秒 |
| 撮影の起動の全体 | 500.4 秒 | 161.7 秒 |
| verify のコマンドの全体（RelWithDebInfo はビルドの確認込み） | 503 秒 | 176 秒 |

- 草・低木・礫の生成は読み込みの初めに数十 ms で済み、重い段ではない。テクスチャの読み込みは数秒。
- 重いのはどの段も CPU の処理で、Debug の最適化の無いコード（シェーダーのコンパイラ・メッシュの解析・崖錐の配置）が主因。
  RelWithDebInfo で全体が 0.35 倍になる。
- RelWithDebInfo で残るいちばん重い段は地形の区画の読み込み（1 区画あたり約 0.22 秒、ゲームスレッドで順に解析して MegaMesh を作る。
  `WastelandRoutine.cpp` の `PlaceTerrainTile`）。

## 撮影がページの読み込みの途中で撮っていたことの直し

RelWithDebInfo で初めに撮ると、ref_cliff_01 だけが Debug と違った（PSNR 34.5 dB、差が 8 を超える画素 9.9 %。近くの地面の起伏の陰影が粗い）。
撮影の瞬間のジオメトリのページは、Debug が読み終わっていた（`GEOMETRY_PAGES ... wanted=0`）のに対し、RelWithDebInfo は読み込みの途中
（`wanted=599`）だった。

- 撮影は、ジオメトリのページの読み込みが落ち着くのを `GeometryPageStreamer::HasPendingWork` で待つ。この判定は、作業集合が目標を超えて
  追い出しと読み直しが 300 更新以上続くと「入れ替わり」とみなして待たない。
- 荒野では、置き場が埋まったメッシュで 1 ページの追い出しと読み直しがゆっくり続き（常駐 3493 ⇔ 3494）、視点を切り替えた直後に前の視点の
  ページを置き場から外しながら新しい視点のページを積む間も、追い出しが途切れない。その間を入れ替わりと数えて、読み込みの途中で撮っていた。
  Debug はフレームが遅く、300 フレームのうちに読み込みが終わっていたので表に出なかった。
- 直し（`Library/Core/Private/Rendering/GeometryPageStreamer.h`）: 追い出しが続いていても、常駐の量（書き込み中を含む）がその間の最大を
  超えて増えたら、読み込みが進んでいるとして入れ替わりの長さを数え直す。作業集合が目標に頭打ちになって入れ替わる場合は量が増えないので、
  これまでどおり待ちに数えない（`GeometryPageStreamerTest` の `TestChurnIsNotPendingWork` が通る）。
- 直した後は、6 視点とも Debug の撮影と平均輝度の差が 0.01 以内、PSNR 53.0〜62.5 dB、差が 8 を超える画素 0.02 % 以下。
  視点ごとの待ちは 5 秒から 14 秒に延びたが、全体は Debug の 0.32 倍。

## 周回スモーク

荒野の周回スモークは Debug で 1 周約 300 秒（3 周 928 秒、SCN-WL-OUTPOST）。RelWithDebInfo の 3 周（`-Build` 付き）は、
ページのストリーマを直した後でビルドの確認込み 256 秒、result=pass（3 周とも settled=1、texture_bytes=1703784520 で周回で増えない）。
重い段は撮影と同じ読み込み（シェーダー・地形の区画・崖錐）で、同じ構成の切り替えで速くなる。
