# R7 カメラ試料の検証

PT の各累積試料は、FramePacket に値コピーされた current/previous camera、RT instance transform、`DeltaTime` から決まる。試料番号を Halton 基数 2・3・5 に入力し、順にレンズ半径、円盤角度、シャッター時刻を選ぶ。乱数の共有状態と live World 参照は使わない。

`CameraProxy::FocusDistance` はワールド単位の m であり、0 のとき従来のピンホール光線を維持する。正値のときは撮像面高 24 mm と垂直 FOV から焦点距離 `f` を求め、`Aperture` を F 値としてレンズ半径 `f/(2 Aperture)` を使う。像距離 `f s/(s-f)` に応じて投影画角を狭め、レンズ位置から焦点面上の同じ点へ光線を向ける。物体距離 `d`、合焦距離 `s`、画像高 `H` に対する錯乱円径の解析値は `f² |d-s| H / (Aperture d (s-f) 0.024)` ピクセル。CPU テストは既知の 49.7312 px に対し、1024個の円盤試料から得た 49.7069 px（相対誤差 0.05% 未満）を確認する。

シャッター区間は現フレーム終端を 1 とし、開始を `1 - clamp(ShutterSpeed / DeltaTime, 0, 1)` とする。前カメラが同一 ID なら位置・向き・垂直 FOV を区間内で補間する。RT geometry は FramePacket の前後 transform を使い、**線形 3×3 部分が同一の並進変化だけ**を補間する。回転・scale・shear の変化は補間せず current transform を使う。これにより行列要素の線形補間が不正な shear/scale を生成しない。時間試料ごとに既存 BLAS と補間後 instance から専用 TLAS を構築する。

GPU 基準は `PathTracingCameraVulkanTest` の 32×32・32試料、赤成分 4×4 分割平均で固定した。静止画像とカメラ・geometry の並進画像について、それぞれ同一 seed の2回目の全画素一致、基準分割値との差 0.02 以下、両画像の平均絶対差 0.01 以上を検査する。移動画像は同じ前後 snapshot を32試料で繰り返して時間区間を評価する。実際のアニメーションで packet の形状やカメラが更新されると、既存の PT 履歴規則に従い累積はリセットされる。
