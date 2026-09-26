# R7 パストレーサー実装計画

## 目的と範囲

R5 RT pipelineとR1のリニア作業色空間を使う、オフライン参照品質のpath tracing modeを追加する。コアを先に完了させ、R2 SkyAtmosphereとR3 VolumetricFogを共有する屋外拡張を後から加える。R7は既存deferred rendererの置換ではなく、明示的に選択する独立pipelineとする。

既定の`Rendering3DTest`起動、保存シーン、`SetupDeferredPipeline`、R1 indoor/outdoor goldenは変更しない。R8が最終sequence rendererをPTに選ぶまで、ゲームの既定rendererはrasterのままにする。

## 設計判断

- S7は`R7SamplingAndExrSelection.md`を正とし、NEE + power heuristic MIS（β=2）、TinyEXR v3.2.0 C API、決定論的seedを採用する。ReSTIR、双方向PT、spectral rendering、CPU fallback、必須denoiserは対象外。
- Path tracingは別の`SceneView` pipelineとして構築し、R5のRT pipeline/SBTとFramePacketの値所有snapshotを使う。最大surface bounceは8、Russian rouletteは3 bounce以降。raygen内の反復traceでbounceとshadow visibilityを処理し、RT recursion depthを増やさない。
- 各pixelのsample indexは累積frame数と連動し、camera/geometry/material/light/environment revisionの変化で累積値を無効化する。seed、pixel、sample、bounce、dimensionから乱数を導出し、GPU global stateやatomic sum orderへ依存させない。
- ラスタとPTのBRDF・texture samplingは共通shader codeへ切り出す。ラスタ側の既存画像・数値契約を不変に保った後にPT側で使用する。
- PT sequenceはlinear Rec.709/D65 RGB float EXR。変換・OETFはR8へ分離し、sample seedとframe indexで同一出力を再生成できるようにする。
- 時間サンプルはCamera shutter open/close区間から決定論的に選ぶ。動的geometry/cameraはFramePacketのprevious/current snapshotだけを使い、RenderThreadからWorldを読まない。薄レンズはR1のfocal length/apertureと整合させ、CoCを解析比較する。

## 実装単位と受入れ

1. **R7-P1: BRDF・texture評価の共有** — raster opaque/transparentで既存契約を保ったまま共通include化し、RT shaderから同一コードを参照する。shader compileとIndoor/Outdoor goldenを確認する。
2. **R7-P2: 独立PT pipelineとprogressive accumulation** — R5 RT pipeline、FramePacket geometry/material snapshot、sample count、revision reset、1-sample/frame accumulationを接続する。camera/scene変更時のresetと静止時の累積をGPU検証する。
3. **R7-P3: NEE/MISと光輸送** — diffuse/GGX BSDF、point/directional/area light、emissive area sample、visibility、PDF/MIS weightを実装する。R1 white-furnaceと解析照明テストをPT modeで通し、同じmaterial/lightのraster/PT結果を比較する。
4. **R7-P4: SPP収束とCornell参照** — nested sample prefix 16/64/256 sppを固定seedで出力し、256 spp self-convergence imageに対するMSEが単調減少することをassertする。Cornell boxを公開参照renderと規定誤差内で比較する。
5. **R7-P5: thin-lensとshutter sampling** — aperture diskとshutter intervalをサンプルし、既知距離・焦点・絞りのCoC数値、deterministic golden、translation motion referenceを検証する。
6. **R7-P6: 決定論的EXR sequence** — TinyEXR writer、linear RGB float、seed/frame naming、有限値、atomic completionを実装し、2回のsequenceをbyte一致または選定閾値内で比較する。
7. **R7-P7: コア受入れとR4/R6再照合** — R7 coreの完了条件・証跡を集約する。同じcamera/geometry/material/light/exposureを揃えてR4 DDGIとR6 RTGIの暫定referenceをself PTと各1回比較し、閾値超過があれば該当phaseを再オープンする。ここではR7 complete trailerを付けない。
8. **R7-O1: SkyAtmosphere接続** — R2と同じ`SkyAtmosphereParameters`をFramePacketから読み、miss radianceとsolar disk samplingへ接続する。朝/昼/夕の3条件で有限性とparameter parityを確認する。
9. **R7-O2: volumetric fog接続** — R3と同じfog density/height/anisotropyを共有し、primary ray transmittanceとsingle-scatteringをpath integralへ接続する。空無効・fog無効のfallbackも維持する。
10. **R7-O3: 屋外受入れと完了** — 3時刻のPT/raster captureをperceptual thresholdで比較し、既知の近似差を根拠付きで記録する。R7 core + outdoor extension全条件を満たしたコードcommitだけに`RenderingRoadmap: R7 complete`を付ける。

## 検証ゲート

- BRDF共通化後: `Game` build、shader compile、承認済みIndoor/Outdoor golden。
- PT pipeline/light transport: `Game`とR7 GPU tests build、PT contract tests、R1 white-furnace/analytical-light、Cornell capture、16/64/256 spp convergence。
- Camera/output: CoC解析test、fixed-seed double-run、EXR independent readback、NaN/Inf scan。
- Outdoor: 朝/昼/夕PT captures、Raster/PT FLIP + pixel max thresholds、fog/sky disabled fallback、R7 full relevant CTest。
- 危険地帯に触れる差分は別文脈の評価者を通す。既定シーン・R1 baselines・ignored `Docs/Plans/`は変更しない。
