# R6-a velocity 実装契約

策定日: 2026-09-22

## 目的

RenderingRoadmap R6-a の最初の実装単位として、カメラと不透明オブジェクトの前フレーム状態を
`FramePacket` の値スナップショットでRenderThreadへ渡し、遅延GBufferから画面velocityを出力する。
RTGI、テンポラル蓄積、デノイズはこの単位に含めない。

## 採用する契約

- velocityは正規化viewport UVでの符号付き差分 `currentUV - previousUV` とする。
- 解析値のGPU readback受入れ誤差は各成分 `0.002` 以下とし、ゼロ期待の背景画素も同じ閾値で検査する。
- UVは、現在フレームと前フレームのdevice用clip行列で頂点を投影し、clip空間のXYをWで割った後、
  `ndc * 0.5 + 0.5` へ変換して求める。Vulkanのdevice用Y補正は両フレームへ同じ規約で適用する。
- 前フレームが無い、投影が無効、またはclip Wが有限でない画素はvelocity `(0, 0)` とする。
  初回フレームを履歴へ取り込むまで、全ピクセルがこの値になる。
- 出力ターゲットは`R16G16_FLOAT`とする。GBufferのnamed resource名は`GBuffer_Velocity`とし、後続の
  R6テンポラル処理がRenderGraphから読む。既存のAlbedo/Normal/Material/Emissive/Depthの契約は変えない。
- 通常の不透明MeshProxyは`WorldTransform`と`PreviousWorldTransform`を同じFramePacketの
  `GPUSceneInstanceData`へ格納する。RenderThreadからWorldやSceneViewを参照して履歴を補うことは禁止する。
- メインSceneViewのカメラは現在値と前回値をFramePacketへ格納する。履歴更新はGameThread側の
  フレームパケット生成境界で行い、パケット公開後にRenderThreadから書き換えない。
- 初回の実装対象は通常の遅延不透明メッシュとメインSceneViewのvelocityとする。Skinned/MegaGeometry/
  Forward透明の履歴対応は、共通契約を壊さず個別の後続タスクで追加し、R6-a受入れ時に対象外を明記する。

## 実装境界

1. `GPUSceneInstanceData`へ前フレーム行列を追加し、SceneViewのスナップショット生成から埋める。
2. `FramePacket`へ前回メインカメラと有効フラグを追加し、Coordinatorから値で渡す。
3. GBufferの第5カラーモードとしてvelocityを作成・登録・保存する。
4. GBuffer頂点/フラグメントシェーダーで現在/前フレームclip位置からvelocityを出力する。
5. 静止、カメラ移動、オブジェクト移動、初回フレーム、非有限入力のCPU/GPU検証を追加する。

## 完了条件

- Debugビルドが成功し、既存GBuffer/Lighting/SceneViewの契約テストが回帰しない。
- 既知の平行移動を行うfixtureで、カメラのみ・物体のみ・カメラと物体の併用それぞれのGPU readback velocityが解析値の `0.002` 以下の誤差になる。物体のみは対象領域外の背景をゼロとして確認する。
- 静止フレームはゼロ、カメラのみの移動は全対象が同じ解析差分、オブジェクトのみの移動は対象領域だけが解析差分になる。
- 初回フレームと無効な履歴はゼロで、NaN/Infを出力しない。
- `GBuffer_Velocity`がRenderGraph named resourceとして公開され、後続パスが読む前提を検証できる。

## 停止条件

既存のRHI形式・RenderGraph attachment・FramePacketのいずれかが通常の遅延GBufferへ安全に接続できない場合は、
既存経路を推測で変更せず、該当するAPI不足と代替案を記録してこの単位を止める。
