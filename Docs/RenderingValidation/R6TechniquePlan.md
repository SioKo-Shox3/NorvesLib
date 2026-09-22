# R6 RTGI・テンポラルデノイザ方式選定記録

策定日: 2026-09-22

## 判定

RenderingRoadmap R6-M1 の方式選定を完了する。R6-a で確定した `GBuffer_Velocity` と、R5 で受入れた
TLAS・ray query・RT pipeline・`FramePacket` snapshot を前提にする。R6 の初期実装は既存の起動経路と
`Rendering3DTest` を変更せず、RT非対応・RT無効・履歴無効時には既存のGI経路へ戻れる構成とする。

## 選定結果

| 選定項目 | 候補 | 採用 | 境界 |
|---|---|---|---|
| GI輸送 | 1 bounce、2 bounce、汎用path tracing | **1 bounce diffuseを既定**。2 bounce diffuseは同じ経路の限定拡張として後続タスク化 | R7のプログレッシブpath tracing、ReSTIR、specular transportは含めない |
| ray dispatch | ray query、RT pipeline | **R6 GIはcompute shader内のray query**。RT pipelineはR5のhard shadowと将来の汎用ray programへ分担 | R6 GIのために新しいSBT groupやRT pipeline契約を増やさない |
| 蓄積 | フレーム跨ぎの履歴、毎フレーム独立評価 | **velocity reprojection付きテンポラル蓄積** | TAAやSSR置換には使わない |
| デノイザ | SVGF全体、自作テンポラル+空間、外部NRD | **自作テンポラル+3x3 cross-bilateral空間フィルタ** | 外部ライブラリ、multi-pass variance-guided filterはR6初期対象外 |
| 履歴寿命 | 無期限、固定短寿命、シーン変更時のみ破棄 | **最大8フレーム**。棄却時は年齢を0へ戻す | 履歴を無期限に保持してghostingを隠さない |
| ウォームアップ | capture直後、可変収束、固定フレーム | **8 rendered frames**。`FrameNumber`で数え、8フレーム完了後を比較対象にする | capture回数では数えない |
| 性能 | R6完了gateに含める、Deferred | **Deferred**。将来のCI GPU性能トラックでパス別に計測する | ローカルGPU時間をR6の機能合格へ混ぜない |

## 前提と責務境界

- `FramePacket` はGameThreadからRenderThreadへ渡す不変snapshotとし、RenderThreadからlive `World`、
  `SceneView`、`MeshComponent`を参照して履歴を補わない。
- R5の `RayTracingSceneSubsystem` が作るTLASと、R5で確定したBDA geometryをR6のray query入力に使う。
  R6は加速構造の所有モデルを再定義しない。
- Rendering層は `RHI/Vulkan/*` を直接includeしない。ray queryのdispatch、履歴resource、fallbackの選択は
  backend-neutralなRendering/RHI境界を通す。Vulkan固有のcapability確認はR5と同じRHI側へ閉じ込める。
- GIの放射輝度は既存のscene-linear・pre-exposed規約に合わせる。最終のpre-exposure適用をLightingPass
  の外で重ねない。
- 既定シーン、起動後のgame mode、球、地面、ライト球、方向ライト、boulder、HDR環境の読み込み経路は
  R6の方式選定および実装で変更しない。R6の受入れは既定シーン上の追加fixtureまたは検証モードで行う。

## RTGIの輸送方式

### 既定の1 bounce

R6の最初の受入れ対象は、各対象pixelからTLASへ1本のdiffuse indirect rayを送り、最初に当たった不透明三角形の
材質・法線・直接照明または環境radianceを評価して間接拡散へ加える経路とする。ray方向は決定論的な低discrepancy
列から選び、フレーム番号で単純にseedを変更するのではなく、pixelとsample indexを含む固定規約で再現可能にする。

1 pixel 1 rayを基準にし、R6-M1では解像度削減や追加のup-sampleを選定しない。これにより、既存のGBufferの
depth/normal/materialをそのまま使い、最初のGPU readbackとgoldenの原因をray数・half-resolution補間と混ぜない。
必要な解像度変更は、Deferred性能gateの結果を根拠にした別タスクとする。

### 限定2 bounce

1 bounceが受入れ済みになった後にだけ、同じray query経路で最初のhitから2本目のdiffuse rayを1本だけ送る拡張を
許可する。2 bounceは固定1 sampleのdiffuse transportに限定し、specular、透過、再帰的なpath tracing、MIS、ReSTIRを
導入しない。2 bounceをR6の初期受入れに必須とはせず、1 bounceのgolden・動的追従・fallbackが安定してから別の
実装単位として追加する。

### ray queryとRT pipelineの分担

- **ray query**: R6のcompute GI、R4 DDGIと同じTLAS snapshotを使うscreen-space indirect query。既存のray query
  capabilityが無い場合はR6 GI resourceを公開せず、R4 DDGIまたは既存IBLへ戻す。
- **RT pipeline**: R5のhard shadowを維持する。R6 GI用のraygen/miss/closest-hit groupを追加せず、複雑なray
  programが必要になるR7 path tracerの責務へ残す。
- **共有資源**: TLAS、BLAS、BDA buffer、FramePacketのscene snapshotは共有するが、R6の履歴・GI出力はR5の
  shadow visibilityやR4のprobe atlasを上書きしない。

この分担はR5の受入れ済みAPIを利用し、R6でRHIの公開境界を拡張する範囲を最小化する。RT pipelineが利用可能でも、
R6 GIのためだけにray query経路をRT pipelineへ置き換えない。

## テンポラル蓄積と履歴棄却

### 画面再投影

R6-aのvelocityは `currentUV - previousUV` であるため、現在pixelから参照する過去UVは
`previousUV = currentUV - velocity` とする。過去UVがviewport外、前フレームが無効、またはvelocityが非有限なら
履歴を使わず現在のGI sampleだけを出力する。

GI履歴はcurrent/historyの2組をframe slotでping-pongする。履歴にはpre-exposed indirect radiance、history age、
confidenceを保持し、scene colorやGBufferの所有権を引き受けない。履歴resourceのresize、初回frame、RT capability
変更、TLAS再構築失敗、material/environment変更時は該当履歴を無効化する。

### 履歴の有効性

reproject先では次の順で履歴を検査する。

1. 過去UV、過去depth、radiance、confidenceが有限であること。
2. 現在と過去のdepthが許容範囲内であること。depth不連続はdisocclusionとして棄却する。
3. 現在と過去のnormalのdotが閾値以上であること。面の切替や大きな法線差は棄却する。
4. material class/roughnessなど既存GBufferで比較可能な材質条件が一致すること。比較できない場合に履歴を
   推測で保持せず、当該pixelだけ現在sampleへ戻す。
5. FramePacketのscene/light revisionが許容範囲であること。環境、材質、TLAS構成の変更は全画面履歴を無効化し、
   動的ライトの小さな移動は2フレームだけ履歴weightを抑える。

履歴が棄却されたpixelはcurrent radianceを採用し、ageを0、confidenceを初期値へ戻す。履歴を採用したpixelは
ageを1増やし、8で飽和させる。blend weightは `1 - 1 / (age + 1)` を基礎にし、`0.0..0.9`へclampする。新規sampleが
完全に消えることを防ぐため、normal/depth/material差が小さくてもcurrent sampleを必ず混ぜる。

動的ライトのrevision変化中はhistory weightの上限を0.25へ落とし、2 rendered frames後に通常値へ戻す。これにより
ライト移動時の古い間接光を8フレーム寿命のまま引きずらない。カメラ・物体移動はR6-a velocityとdepth/normal棄却で
pixel単位に処理し、全画面リセットを常用しない。

## デノイザ

外部NRDを追加せず、自作のテンポラル+空間方式を採用する。テンポラル段で履歴を検査・蓄積した後、同一のGI
resolutionに対して1回の3x3 cross-bilateral filterを実行する。空間weightはdepth差、normal dot、material互換性、
サンプルconfidenceから作り、物体境界を越えるfilter tapは除外する。

SVGF全体のvariance/moment履歴、複数段a-trous、外部ライセンス・CMake依存はR6初期対象外とする。将来、Deferred性能
gateまたはノイズ測定が不足を示した場合は、varianceを追加する別選定として扱い、今回の単純な履歴契約を暗黙に変更しない。

## fallbackと起動経路

R6 GIは次の優先順位で公開する。

1. R6 ray-query GIがcapability、TLAS、GI resource、履歴のすべてを満たす場合。
2. R6を明示的に無効化した場合、またはR6資源作成・dispatch・readback契約が失敗した場合は、validなR4 DDGI。
3. R4 DDGIも無効・非対応・不完全なら、既存のIBL/直接照明経路。

いずれのfallbackでも、R6の不完全な履歴やGI出力をRenderGraphへ公開しない。R6の初期化失敗で通常のraster、R5の
RT shadow、既定のgame modeを停止させない。

## 固定ウォームアップと受入れ契約

- 起動または履歴無効化後、rendered `FrameNumber`を8フレーム進める。比較対象は8フレーム完了後の次の有効frameとし、
  capture回数をwarmupの代わりにしない。
- 静止カメラ・静止物体では、warmup後のGI出力が有限で、履歴ageが8へ到達し、同一条件の再実行が決定論的な範囲へ入る
  ことを確認する。
- 既知のライト移動では、移動後4 rendered frames以内に間接光ROIが最終変化量の80%以上へ追従することを目標とする。
  ライトrevisionによる2フレームのweight制限を含めて判定し、許容値とROIは実装タスクでfixtureと同時に固定する。
- 既知のカメラ・物体移動では、R6-aのvelocity符号・depth/normal棄却・移動後停止の残留が同時に検証できるfixtureを使う。
- 静止画の比較先はR7が未完の間は暫定参照を使い、R7コア完了時にRenderingRoadmapの再照合ルールに従って自前PTへ
  比較し直す。R6の受入れをR7実装の先取りで成立させない。
- R6本体の完了条件からGPU時間を分離する。ray query、テンポラル、空間filter、fallbackの機能検証は行うが、
  パス別GPU時間の合否は将来のCI GPU性能回帰トラックで判定する。

## 実装単位

| タスク | 内容 | 主な境界 |
|---|---|---|
| R6-P1 | R6 GIの結果形式、capability/fallback、FramePacketのscene/light revision、履歴resourceの公開契約を定義する | R5のTLAS所有・R6-a velocityを再利用し、RHI/Vulkan APIを先取りしない |
| R6-P2 | 1 bounce diffuse ray-query GIをcompute passへ接続し、TLAS hit/miss、有限性、R4/IBL fallbackをGPU検証する | RT pipeline/SBTと既定起動経路は変更しない |
| R6-P3 | velocity reprojection、depth/normal/material棄却、8-frame age/confidence、light revision抑制を実装する | TAA、SSR置換、live World参照を追加しない |
| R6-P4 | 3x3 cross-bilateral spatial filterとLightingPassへの合成を実装する | 外部NRD、複数段SVGF、pre-exposure二重適用を追加しない |
| R6-P5 | 静止・カメラ移動・物体移動・ライト移動・RT無効fallbackの複数frame GPU受入れを固定する | 既定シーンの読み込み経路を変更しない |
| R6-P6 | R6受入れ記録、golden/threshold、既知の制限、Roadmap trailer、性能gate保留を確定する | R7/R8を完了扱いにしない |

## 停止条件

- R5のTLAS/BLAS snapshotをR6のray queryから安全に読めない場合は、R5 APIや所有権モデルを推測で変更せず、必要な
  API不足をR6-P1の未決定事項へ戻す。
- `GBuffer_Velocity`の符号・履歴寿命・FramePacketの値所有を保てない場合は、テンポラル蓄積を進めずR6-a契約との差分を
  記録する。
- Rendering層からVulkan型を要求する、またはR6 GIのためにR7のpath tracer/SBT設計を先取りする必要が出た場合は、
  その機能をR6の初期範囲から外し、別の選定課題として停止する。
- R6無効時に既定raster、R5 RT shadow、既存R4/IBL fallbackのいずれかを維持できない場合は、R6の機能実装を止めて
  fallbackを先に修復する。

## RenderingRoadmapとの整合確認

| Roadmapの要求 | 本記録での扱い | 判定 |
|---|---|---|
| R6-aを独立工程として先行 | `R6aVelocityPlan.md` / `R6aVelocityAcceptance.md` と `c4d1ea4`を前提にする | 整合 |
| RTGI 1〜2 bounce | 1 bounceを初期既定、限定2 bounceを後続拡張として残す | 整合 |
| ray query / RT pipelineの使い分け | R6 GIはray query、R5 shadowはRT pipeline、R7 PTは別境界 | 整合 |
| テンポラル蓄積 + デノイザ | velocity reprojection + 自作temporal/3x3 spatialを採用 | 整合 |
| ReSTIR、SSR置換、TAAは非ゴール | 本記録と実装単位から除外 | 整合 |
| 固定フレーム数warmup | 8 rendered frames、FrameNumber基準に固定 | 整合 |
| GPU性能はDeferred | R6-P6後のCI性能gateへ分離 | 整合 |
| R7/R8との依存 | R7は暫定参照の再照合先、R8はR6-a velocityだけを利用 | 先取りなし |

`Docs/Plans/RenderingRoadmap.md` はリポジトリのignore対象であり、本記録では編集・追跡しない。選定結果の正本は
この追跡対象文書、実装コミット、完了時のRoadmap trailerとする。
