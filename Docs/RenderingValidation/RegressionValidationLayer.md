# 回帰の撮影と検証レイヤー

回帰の撮影（`Scripts/Terrain/CaptureRegression.ps1`）を RelWithDebInfo の Game に起動引数 `--validation-layer` を付けて撮り、
Vulkan の検証レイヤーの指摘を `ReferenceCheck.py --regression` の検査に入れた記録。判定日: 2026-10-19。
ブランチ `feature/scene-launcher`。タスク SCN-WLR-REGRESSION-VALIDATION。

## 決めたこと

- 起動引数 `--validation-layer`（値を取らない）を足した。開発ビルド（Debug・RelWithDebInfo。`NORVES_BUILD_DEVELOPMENT`）だけで効き、
  `Library/Core/Private/Boot/AppLauncher.cpp` が引数を見て `BootConfig::bEnableRHIValidation` を立てる。Release ではこの判定ごと
  コンパイルされず、引数は無視される。Debug は前から既定で検証レイヤーが入る。
- 入ったかどうかは Game.log に出す。`VulkanDevice` がインスタンスを作った後に `VALIDATION_LAYER enabled layer=VK_LAYER_KHRONOS_validation`
  （入らないときは `VALIDATION_LAYER disabled`）を、引数で頼んだときは `AppLauncher` が `VALIDATION_LAYER requested source=argument` を書く。
- `Scripts/CaptureScene.ps1` は Debug と、`-GameArgs` に `validation-layer` を含む撮影で検証レイヤーのログを数える。ログのファイルが無い、
  または Game.log に `VALIDATION_LAYER enabled` が無ければ失敗にする（metrics.json の `vulkan_validation.layer_enabled_logged`）。
  Release で `validation-layer` を与えると撮らずに失敗にする（`reason=validation_layer_in_release`）。
- 回帰の撮影は rendering3dtest・mega-bananas・sandbox・night-village の overview を RelWithDebInfo ＋ `--validation-layer` で撮る。
  `--regression` は各シーンの metrics.json の `vulkan_validation.error_count` が 1 件以上、または記録が無ければ落とす。

## 時間（4 シーン、RelWithDebInfo ＋ `--validation-layer`、最初のシーンの前に RelWithDebInfo の Game をビルド）

| シーン | before（秒） | after（秒） |
|---|---:|---:|
| rendering3dtest（ビルド込み） | 50 | 28 |
| mega-bananas | 18 | 18 |
| sandbox | 15 | 16 |
| night-village | 132 | 122 |
| 全体（`--regression` を含む） | 216 | 186 |

上限の 9 分（540 秒）に収まる。Debug で撮ると rendering3dtest 227 秒（ビルド込み）・mega-bananas 190 秒で、4 シーンでは収まらない見込みだった。
before の rendering3dtest はコードの変更を取り込むビルドを含む。

## 検証レイヤーの指摘が検査に入ること

- after の 4 シーンとも Game.log に `VALIDATION_LAYER enabled` があり、検証レイヤーのログ（`overview.Validation.log`）ができた。
  エラー・警告はどのシーンも 0 件で、`regression_check.json` の `validation_errors` は 4 シーンとも 0。
- RelWithDebInfo で指摘がログと metrics.json まで届くことを、best practices の検査を足した sandbox の撮影で確かめた
  （`VK_KHRONOS_VALIDATION_ENABLES=VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT`。警告 23 件、エラー 0 件）。
- 撮影の写しで night-village の `error_count` を 1 にすると `--regression` が「検証レイヤーのエラーが 1 件ある」で落ち、sandbox の
  `vulkan_validation` を消すと「検証レイヤーの記録が無い」で落ちる。

## Release で引数が効かないこと

Release と RelWithDebInfo の Game（同じ日のビルド）を、同じ引数（`--scene=sandbox ... --validation-layer`）と検証レイヤーのログの
環境変数（`VK_KHRONOS_VALIDATION_LOG_FILENAME` ほか）で起動した。

| 構成 | 終了コード | 撮影 | 検証レイヤーのログ |
|---|---:|---|---|
| Release | 0 | あり | 無い（検証レイヤーが入っていない） |
| RelWithDebInfo | 0 | あり | ある（6 行） |

Release はログを書かないので Game.log では確かめられない。検証レイヤーのログのファイルができないことで、引数が効かないことを確かめた。

## 既知の限界

- 検証レイヤーを入れた RelWithDebInfo は、入れない RelWithDebInfo より遅い。回帰の撮影だけで使い、性能の計測（`-GpuTimingFrames`）には付けない。
- `-Motion` の組（rendering3dtest のカメラ台本）は Rendering3DTest が台本を受け取らないので、まだ撮れない（SCN-WLR-REGRESSION-MOTION）。
