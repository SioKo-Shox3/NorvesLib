# PROGRESS — NorvesLib

## G2全体の進捗（2026-10-07）

G2は進行中。取り込み基盤と0.2系の実装を終えつつ、材質・分離資産・安全な一括増分の実用経路を接続する中盤。小taskの完了数を全体の達成率には換算しない。

- GR77/GR78: GLB/sidecar/変換をcook・loose・骨格へ接続済み。実物での最終受入れは残る。surface_centroidは承認済み保留。
- GR86: 明示の影響数縮約/CUBICSPLINE焼込/morph dropと診断を接続済み。256関節はGR82 Stage Bと同時。
- GR32: 0.2のsubmesh/slot/描画/palette共有を接続済み。GPU実受入れは未完。
- GR79: 材質codec/reader/設定/source/ARM/selector、明示v1 writer/cook/hash/cache、全値staging・1材質Opaque runtimeのCPU/FakeDevice接続・複数primitive/material cookまで実Windowsで受入済み。N>1 runtime、対応外材質の描画、実GPU/実物受入れは未完。
- GR82: Stage Aの複数clipをcook/読込/Resource/一意名前APIへ接続済み。Stage Bは作者rest付きv1 ClipBankの保存・安全束縛・明示override/差report・実姿勢評価までCPU受入済み。Skeleton/SkinnedMeshの分離wire・同snapshot三パス読込・全MATS CPU所有・未登録owner/実姿勢まで受入済み。分離資産の既存Registry/runtimeへの接続もCPU受入済み。Armature/clip-only/256と要約/CLIは残る。GR83の実owner/M9経路の明示clip選択もCPU検証済み。
- GR83/GR84: GR84のBVH source/target FK・名前/座標/回転対応・全sample clip/report・NVSKEL cook/package/実Sampler・Role Profile bytesと実CLI/file依存を実Windows CPUで受入済み。GR83は所有CPUローダ・原子的な一括公開・実workerからownerへのdelegate配送まで受入済みで、実ApplicationProcessor/M9への配線・CPU helper・Game両構成ビルドまで検証済み。Stage Bの分離資産CPUローダは受入済み。分離資産も同じRegistry/runtime/ownerへ接続してCPU受入済み。自動rest補正・GPU/実物受入れは未完。
- GR96: 単体7CLI/79file互換とtexture v1/10file互換を実Windowsで受入済み。textureの管理付き増分公開・journal/recovery・通常CLIと明示復旧まで受入済み。種別横断spec v2・可変inventory・report/予算/jobs同値は未完。

S1〜S8は選定課題の番号であり、S6着手を全工程の6/8完了とは扱わない。新規出力へのtexture一括cookと、既存出力を安全に差分更新する完成経路を区別する。

## Done

- R0: `052a7dc`。描画検証基盤。
- R1 P1: `c67dfde`。Camera露出、物理ライト単位、Scene v2。
- R1 P2: `98bd5c9`。表示リニア中間とsRGB一回出力。
- R1 P3: `053e73be13ab49454691105fd91acbe5144140ff`。物理ライトとEV100プリエクスポージャ。
- R1 P4: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013`。放射輝度IBL、DFG、GGX補償と数値検証。focused build 13/13、CPU 7/7、GPU 6/6（exit 125なし）、独立レビュー、接続監査、fresh verifierを2026-09-08にPASS。

- R1 P5: `7d134b7d5fef9486210589d698b01d66bf0c7d31`。物理ライト・GGX・IBL・方向影の透明描画を受入完了。focused8/CPU6/skip、P4 GPU6、透明10行/背景10行/固定4輝度、両scene object-presence、lifecycle、独立recheck12件、累積source18の正式Claude評価（blocking 0）を確認した。
- R1 P6a: `93f01220c2bcd899c51e3a9c538265e5723395dc`（P6a forward-fixの受入状態を記録）。`2a50d4c` と `d4cbe3e` のforward-fixを含む最終報告、focused7、CPU6、fixture CPU検査、40 static/68 numerical/70 total/120 captures、両scene object-presence、script self-tests、GPU CTest exact21を確認した。旧P6b候補は再利用しない。
- R1 P6b: `eedec7a681489768645eabf77355886ae8e755c0`。承認済みbaseline（Indoor `3845F8671194A3C55EA929503E7ECB4C777EB7A5A4D900C03E75853E50D9D4EF` / Outdoor `05D202193F8C304E45099EBA6557A48BB7B5BF88E694552CBABC50E898D895B6`）、threshold、BackBuffer20=20/20、targetless full build=exit 0、full CTest=212 passed/7 intentional skipped/0 failedを受入完了。Roadmap trailerは`RenderingRoadmap: R1 complete`、GPU性能はDeferred。
- R2 M1: `Docs/Plans/RenderingR2SkyAtmosphereCsmPlan.md` を作成し、Hillaire 2020系空、太陽ディスクのプリエクスポージャ表現、空由来IBL、4カスケードCSM、R2-P1〜P8の検証単位を定義した。R1受入れ基点は再利用せず、R2用の数値/画像証拠を分離する。
- R2-P1: `76a0c15`。`SkyAtmosphereParameters` の有限化、太陽高度・方位角からの正規化方向、太陽ディスク立体角を積分したCPU参照サンプル、R1露出規約に沿う太陽ディスク安全域を実装した。対象ビルド成功、`SkyAtmosphereModelTest` は1/1 passed。P1の固定値は平行大気・単一散乱のCPU回帰アンカーとして後続LUTと単位を共有する。
- R2-P2: `53a58dc`（実装 `07a0cd9`）。空スナップショットをFramePacketからRenderThreadへ接続し、SkyAtmospherePassで透過率LUT・空放射輝度LUT・太陽ディスクを生成してRenderGraph named resourceへ公開した。LightingPassのbinding 14/15と空背景の有効/無効フォールバックを接続し、太陽ディスクは解析的な方向・角半径マスクへ修正した。P2対象ビルド・CTestは3/3 passed、エンジンと同じBOM除去経路のsky/lightingシェーダーコンパイルも合格した。空由来の拡散/鏡面IBL更新はP3へ分離した。
- R2-P3: `a235411`（実装 `5daf556`）。空のequirectangular放射輝度を同一のsanitized空パラメータから再構成し、既存のDiffuseIrradiance/PrefilteredSpecular生成へ接続した。空パラメータと放射輝度寸法の変更時だけ動的IBLを更新し、空要求時の生成失敗は黒へ固定する。朝/昼/夕EVの有限性・高度変更・SceneProxy寿命、R1静的HDR経路を確認し、4対象ビルド・CTestは4/4 passed。1周目のselector契約回帰を`a235411`で修正し、2周目の独立評価はPASS。
- R2-P4: `f51384f`。実カメラのnear/farから対数・線形混合の4分割距離を計算し、同一方向ライトの球近似行列、caster深度範囲、テクセル中心スナップをCPUだけで構築した。near/far逆転、無効方向、非有限bounds、固定4カスケード以外を有限な影なし結果へ戻し、サブテクセル移動の行列安定性と隣接境界の連続性を確認した。指定のDebugビルドとCTestは2/2 passed。RHI/Vulkanには触れていない。
- R2-P5: `9e015a3`。RHIに配列depthの指定layer viewを追加し、Vulkanの1層Framebufferへ安全にattachできるようにした。ShadowMapPassを4層D32 depth arrayと4つのlayer別Framebufferへ移行し、CSMの各行列・分割距離をRenderThreadの公開値へ記録した。部分初期化時は全リソースを解放し、未完成の配列深度をRenderGraphへ公開しない。P4回帰を含む関連CTestは6/6 passed、指定P5 focused CTestは3/3 passed。
- R2-P6: Lighting/ForwardのGPUパラメータ、descriptor、頂点/フラグメントシェーダーを4カスケード行列・5分割距離・`sampler2DArray`へ統一した。カスケード境界の10%ブレンド、有限性/分割順序検査、影なし・単一層・不完全公開値向けの1×1×4層型互換フォールバックを実装した。指定DebugビルドとCTestは4/4 passed、`forward_transparent.vert`/`.frag`/`lighting.frag`の`glslangValidator`は3/3 exit 0。
- R2-P7: `ca9204d`。朝・昼・夕のR2 sky golden 3枚と閾値TSV 2枚をR1 baselineから分離して固定し、`cameraForward`のview-depth規約、空パラメータのFramePacket接続、CPU/shaderのsmoothstep一致、実GPU BackBufferのケース間変化検証を追加した。最終評価PASS、source/CTest 5/5、R2 self-test、GPU BackBuffer 3/3、RHI/Vulkan 4/4、GLSL 3/3を確認した。
- R2-P8: R2受入れ記録、実装コミット一覧、検証ログ、golden/threshold、既知の非対象、Roadmapの完了行を確定した。R3は新規M1の入口として未着手のまま残した。
- R3 M1: 解析高さフォグ＋固定24ステップ方向光散乱を選択した。既存SceneColor/Depth、R2空radiance、4層CSMを使う全画面passとし、froxel/新規RHI資源を避ける。タスクR3-P1〜P4を登録した。
- R3-P1: `2973941`。指数高さ密度の有限なパラメータ正規化、水平・上昇・下降レイの解析透過率、SceneProxy/FramePacketスナップショットを実装した。対象Debugビルド成功、VolumetricFogModelTest 1/1 passed。
- R3-P2: 855de28 / b6865ea。解析フォグとR2空radianceをSceneColorへ合成し、Lighting後・Forward透明前へ接続した。指定ビルド、契約CTest 2/2、GLSLコンパイルを確認。
- R3-P3: RenderWorld→Coordinator→FramePacket.Sceneの高さフォグ値コピー、CSM配列からの固定24ステップ単一散乱、CSM不在時の解析フォグ維持を実装。focused Debug build exit 0、契約CTest 3/3、実GPU capture PASS（中心差1.59418/上限3、非遮蔽散乱差98.4629/下限1、解析フォグ差3.12447・15.3901/各下限0.25）。独立評価2周目PASS。ログは`.harness/runs/20260920-r3-p3-final/`。
- R3-P4: 9dc360d。3段階フォグ密度のGPU golden、CSM遮蔽A/B、遠景大気ブレンドの専用受入れを固定した。独立評価PASS。P3の散乱上限所見は`f446618`で解消済み。R1/R2基準は不変で、GPU性能計測は後続ゲートへ分離した。
- R5-P1: `5c82133`。RHIにBuffer Device Addressの明示要求と既定値0の取得APIを追加し、Vulkan 1.2能力に応じてバッファusage・メモリ割り当て・アドレス取得を同じ条件で制御した。RTX 4080で対応能力true、要求バッファのaddress非0、未要求バッファ0を確認。対象ビルドexit 0、BDA CTest 1/1、関連RHI/描画GPUテスト6/6、独立評価PASS。
- R5-P2: AS、ray query、RT pipelineを拡張とfeatureごとに照会し、Vulkan 1.2 BDAとdeferred host operationsを含む依存関係を満たす機能だけを論理デバイスで有効化した。非対応時の初期化とラスタ描画を維持する。
- R5-P3: RHI ShaderStageにRT 6 stageを追加し、Vulkan shadercをstageごとのshaderc kindへ一意に写像した。共通fixtureのDebugビルドはexit 0、6 stageのSPIR-V実行モデルを確認するCTestは1/1 passed。
- R5-P4: `6501234`。BLAS/TLASのBuild/Update記述子と加速構造resourceをバックエンド非依存APIへ追加した。BLAS内のgeometry type統一と混在拒否、source/destination双方の容量境界、無効入力、RT非対応時の戻り値を契約テストで固定した。
- R5-P5: `20260922-r5-p5-resume`。BDA vertex/index bufferから不透明triangle BLASを構築し、GPU queryの既知hit/miss、RT非対応時の非公開、従来経路維持、resource寿命を確認した。Debug build exit 0、専用CTest 1/1 passed、直接GPU実行 exit 0、独立評価PASS。容量再問い合わせ、build-input usageの追加検証、行末整理はnon-blockingとしてNEXT_FINDINGS.mdへ記録した。
- R5-P6: `67780ea` / `29c483d` / `7d8d164`。Vulkan同期TLAS BuildはGPU完了後だけ実instance数を更新し、command-list Build 1件から同期Build 2件への変更、旧件数Update拒否、新件数UpdateとGPU queryを検証した。Debugビルドと専用CTestはexit 0、CTest 1/1 passed、独立評価PASS。証拠は`.harness/runs/20260920-174410/verify-R5-P6-5.txt`、`verify-R5-P6-6.txt`、`evaluator-R5-P6-2.txt`、`evaluator-R5-P6-3.txt`。
- R5-P7: RayTracing pipeline descriptorにraygen/miss/closest-hit shader groupを加え、Vulkan pipelineとSBT生成を実装した。6 RT stageとAllRayTracingのdescriptor visibility、無効group拒否、group handleとSBT region alignmentをGPUテストで確認した。Debug build exit 0、専用CTest 1/1 passed、独立評価PASS。ログは`.harness/runs/20260920-174410/verify-R5-P7-5.txt`と`verify-R5-P7-6.txt`。
- R5-P8: `2add53a` / `f9fa15a` / `1267197`。Vulkanの各dispatch軸上限と総呼出し数上限を検査し、同一deviceのTLAS binding、null/BLAS/foreign-device/別descriptor種の拒否、hit=1・miss=0のGPU readbackを固定した。両readback要素を番兵値から初期化して欠落dispatchも検出する。Debug build exit 0、専用CTest 1/1 passed、軸別拒否2件、上限・番兵値の修正差分は独立評価PASS。ログは`.harness/runs/20260921-r5-p8-resume/verify-R5-P8-resume-build-3.txt`、`verify-R5-P8-resume-ctest-3.txt`。

- R5-P9: `9e213c9` / `27d7505` / `c8db734`。GEngine所有のRayTracingSceneSubsystemがopaque DrawCommandからgeometryとinstance transformをFramePacketへコピーし、RenderThreadではFramePacketからBLAS/TLASを構築する。同一slotのTLAS update再利用、GPU待機後・device参照解放前の資源破棄、FramePacket寿命を契約テストで確認した。Debug build exit 0、専用GPU CTest 2/2 passed（Validation Layer無効）。証拠は`.harness/runs/20260921-r5-p9-resume/verify-R5-P9-final-build.txt`と`verify-R5-P9-final-ctest.txt`。
- R5-P10: `VulkanCommandList`のstage解決をRT pipeline capabilityで制御し、対応デバイスの`ShaderResource` barrierに`eRayTracingShaderKHR`を追加した。RT保存状態も全buffer/image barrier経路で専用RT stageを使う。対象build exit 0、同期validation有効のRT CTest 1/1と直接capture（Raster/RT A/B・RT無効fallback、max_lsb=0）を確認。再リンク後のRenderingValidationは20 passed/7 skipped/1 known baseline failure（Outdoor golden、`mean_flip=0.002326954`で過去基準と一致）。ログは`.harness/runs/20260921-073235/verify-R5-P10-17.txt`〜`-23.txt`。

- R5-P11: `LightingParamsLayoutTest`でプリエクスポージャを適用する7 mode（Normal、RAW252、Validation Lambert/PBR、R5 RasterHardShadow/RayTracingHardShadow/RasterFallback）を個別に固定し、RT visibility・RAW250/251は除外した。Debug build exit 0、対象CTest 2/2 passed、同期validation付き動的capture全段PASS（TLAS更新後の旧領域255・移動先0、Raster/RT/fallbackの`max_lsb=0`）。全CTestは235件中6失敗・7 skipで、開始baseline234件/8失敗の失敗集合に対して新規失敗0。ログは`.harness/runs/20260921-073235/verify-R5-P11-contract-fix-target-ctest.txt`、`verify-R5-P11-contract-fix-dynamic-sync.txt`、`verify-R5-P11-contract-fix-full-ctest.txt`。

- R5-P12: `d8cde71`。R5Acceptance.mdへ方式選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能ゲート保留を集約し、R4/DDGIを後続として記録した。
- R5-P13: `1545a41`。VulkanTexture::Updateの終了・送信・待機失敗時にstaging資源と転送先texture資源をデバイス待機まで保持し、非device-lost待機失敗時のdevice teardown保護とValidation error検出を実装した。Debug build exit 0、直接GPU実行 exit 0、専用CTest 1/1 passed。ビルドには既存のVulkan macro再定義とthird-party PDB警告がある。同一deviceの共有command pool操作とWaitIdleは呼出側で直列化する。
- R5現行HEAD再検証: `20260922-r5-head-final`でR4/P13後のHEADを再ビルドし、R5 GPU群CTest 5/5 passed、静的RT影A/B・動的TLAS更新・RT無効fallbackの直接captureをすべてPASSで確認した。現行HEADのR5全体独立評価もPASS。Vulkan既存third-party PDB warningとSlang未導入warningはログに残るが、対象経路の終了コードは0。
- R4 M1: DDGIの有限なprobe volume、ray-query更新、scene-linear octahedral irradiance/distance atlas、LightingPass統合、Cornell RGBE region metric、動的8-frame収束のR4-P1〜P7を定義した。
- R4-P1: `9c34d35`。有限値検証、1..1024 probeのchecked grid indexing、octahedral方向変換、64 ray方向列と無効入力fallbackをCPU契約テストで固定した。Debug build exit 0、専用CTest 1/1 passed。
- R4-P2: `578236d`。DDGI volumeとlinear BaseColor/emissiveを値所有snapshot化し、RHI非対応時の無効化とpacket clearを固定した。Debug build exit 0、専用CTest 1/1 passed。
- R4-P3: FramePacketのTLASへ64方向のcompute ray queryをLightingPassからdispatchし、hit距離・instance/primitive属性とmissをGPU readbackした。同一command listのTLAS build→dispatch、frame slot再利用、RT無効時の番兵維持とSceneColor一致、pipeline/result-buffer生成例外時のDDGI停止・描画継続を固定した。`Game`とGPUテストのDebug buildはexit 0、専用CTestは1/1 passed。
- R4-P3A: `1ec32dd`。ray hit三角形normal・FramePacketのBaseColor/emissive・遮蔽付きdirectional/point/spot radianceとmiss環境をscene-linear ray結果へ保存した。frame slotごとにBDA geometry buffersを保持し、shaderInt64対応を有効化・ゲートした。Debug build exit 0、P3/P3A validation GPU CTest 2/2 passed、radiance readback期待値一致、独立評価PASS。
- R4-P5: `0116e8f`。GBufferのworld position/normalでvolume内probeをvisibility-weightedに補間し、diffuse IBLをDDGI irradianceへ置換して間接拡散を加えた。無効・volume外・RT非対応・不完全atlasでは既存IBLへ戻し、binding 4は全構造体を一括更新する。Debug build exit 0、focused CTest 3/3 passed。
- R4-P6: `20260922-r4-p7-final3`。Cornell RGBEを使うHDR scene-color captureでshadow/red/green ROIの相対誤差0.147881/0.152345/0.106696、red/green chroma差0.0194377/0.0229986を確認した。DDGI有効A/Bはmean/max delta=0.168949/6.5625、無効A/Bはmean/max delta=0/0、dynamic red/green ROIは4 warmup sample後のFrameNumber差8でprogress=0.971325/0.820146、VUID_COUNT=0。P4のDDGIProbeUpdateVulkanTestも回帰なし。
- R4-P7: `20260922-r4-p7-final3`。R4Acceptance.md、Cornell RGBE/threshold、build/GPU/CTest読戻しログ、atlas履歴、失敗時公開状態クリア、既知の制限、Deferred性能gateを確定した。指定R4 9件CTestは9/9 passed。独立評価2周目で指摘された帳簿・行末・成果物追跡のblockingを修正し、最終GPUログと差分衛生を再確認してR4を受入れ完了とする。
- SCENE-P1: `20260922`。起動経路は`GameApplicationHandler::CreateGameModeStateMachine`から従来どおり`Rendering3DTest`を開始するまま維持した。球・地面・ライト球・方向ライト・boulder非同期ロード・HDR環境の生成ログと120フレーム終了を確認し、既定フレームへ常時投入されていたテスト用黄色AABBだけを外した。選択表示AABBは維持した。Debug Game build exit 0、関連CTest 3/3 passed。
- AUDIT-RM-P1: `20260922`。RoadMapの依存関係とR0〜R5のコード・受入れ記録を照合した。R3〜R5の実装済みなのにRoadMap表が未着手のまま残る遅れ、R2〜R5の完了トレーラー不足、R6-aを次に開始できる依存状態、Rendering3DTestに読み込む保存済みレンダリングシーンが存在しないことを追跡監査へ固定した。RoadMap本体は無視対象のため変更・追跡化していない。
- R6-a: `2ba3854` の後続修正で、現フレームproxy更新、MeshComponentの履歴commit、カメラ履歴無効化、解析投影値・カメラのみ/物体のみ/併用・初回/安定静止・移動後停止のGPU検証を確定した。起動経路と既存Rendering3DTestのシーン構成は維持し、最終Game build、関連CTest 17/17、velocity GPU CTest 6/6、120フレーム起動ログを確認した。詳細は`Docs/RenderingValidation/R6aVelocityAcceptance.md`。
- R6-M1: `Docs/RenderingValidation/R6TechniquePlan.md`で、1 bounce diffuse RTGIを既定、2 bounce diffuseを限定拡張、R6 GIのray query/R5 shadowのRT pipeline分担、velocity再投影、自作テンポラル+3x3 cross-bilateral、8フレーム履歴寿命・8 rendered-frame warmup、動的ライトrevision、性能gate Deferredを固定した。R7 path tracer、ReSTIR、SSR/TAA、外部NRDは先取りしない。開始ゲートはGame/RHIRayTracingPipelineVulkanTestのDebug build exit 0、RHIRayTracingPipelineVulkanTestとRenderingVelocityCameraVulkanTestのCTest 2/2 passed。
- R6-P1: `RTGIContract`へ1 bounce diffuse・pre-exposed結果形式、ray-query capability、frame/scene/light revision一致、current/history radiance・age・confidence、R4 DDGI→既存IBL→raster fallbackを固定した。FramePacketへRTGI有効化とscene/light revisionを値コピーし、R5 TLAS snapshotの完全性を参照だけで判定してViewRenderContext・LightingPassへ接続した。Debug Game buildはexit 0、指定CTestは3/3 passed。証拠は`.harness/runs/20260922-200402/verify-R6-P1-1.txt`と`verify-R6-P1-2.txt`。
- R6-P1-FIX: `SceneRevision`から物体変換・`PreviousWorld`・TLAS transformを除き、シーン構成・材質・環境・TLAS構成だけをrevision化した。RTGI履歴はcurrent側のframe/revisionだけで選択し、history側のrevision差を`HasHistoryRevisionMismatch`でR6-P3へ保持する。1つ前のhistory revisionでRTGI選択を維持し、構成revision不一致でfallbackへ戻る契約テストを追加した。指定Game buildとCTest 3/3 passed。証拠は`.harness/runs/20260922-200402/verify-R6-P1-FIX-1.txt`と`verify-R6-P1-FIX-2.txt`。
- R6-P2: R5のFramePacket TLAS/BLAS snapshotを共有する1 bounce ray-query computeをLightingPassへ接続し、TLAS hit/miss、直接光または環境光の有限なdiffuse indirect radiance、pre-exposureを`R16G16B16A16_FLOAT`へ渡した。RT非対応・無効化・不完全TLAS・資源またはdispatch例外ではRTGI結果を公開せず、R4 DDGI→既存IBL→rasterへ戻す。指定Game buildはexit 0、`DDGIProbeRayQueryVulkanTest`と`RenderingRayTracingShadowVulkanTest`は2/2 passed。証拠は`.harness/runs/20260922-200402/verify-R6-P2-1.txt`と`verify-R6-P2-2.txt`。
- R6-P1-FIX-2: SceneRevisionを全MeshProxy/SkinnedMeshProxyの順序非依存な構成集合ハッシュへ移し、カリング済みDrawCommand・奥行きソート・物体/UI変換・RT配置を除外した。構成追加削除、メッシュ/材質/環境変更の変化と、カメラ/物体移動・カリング・proxy/UI/RT順序の不変性を性質テストで固定した。指定Game buildとCTest 3/3 passed。証拠は`.harness/runs/20260922-200402/verify-R6-P1-FIX-2-1.txt`と`verify-R6-P1-FIX-2-2.txt`。
- R6-P2-FIX: 専用`RTGIDiffuseIndirectVulkanTest`で完全TLASと2x2 GBufferをLightingPassへ渡し、ray-query computeのhit/miss finite radiance、RTGI公開、無効化/TLAS不完全時の既存raster fallbackをGPU readbackで固定した。RTGI descriptorのsampler/storage image bindingずれも修正し、R5 RT pipeline/SBTとRendering3DTest起動経路は変更していない。指定Game build、GPU CTest 3/3、GLSL compileはすべてexit 0。証拠は`.harness/runs/20260922-200402/verify-R6-P2-FIX-5.txt`〜`-7.txt`。
- R6-P3: current/history ping-pongの履歴slot状態を正しく遷移させ、連続rendered frameだけをvelocity再投影へ使うようにした。P2で追加されたvelocity/RTGI出力をRenderGraph契約へ反映した。指定Game buildはexit 0、`RenderingVelocityCameraVulkanTest`・`RenderingVelocityObjectVulkanTest`・`RenderGraphCompileTest`は3/3 passed。証拠は`.harness/runs/20260923-033012/verify-R6-P3-1.txt`と`verify-R6-P3-2.txt`。
- R6-P4: 3x3 cross-bilateralデノイザをLightingPassへ接続した。depth/normal/material境界を棄却し、履歴confidenceをweightへ反映する。Gameと対象テストのDebug build、Vulkan 1.2 shader compile、指定CTest 4/4が成功した。RTGI GPU readbackはfinite hit radiance、miss zero、RTGI公開、disabled/TLAS不完全時のfallbackを確認した。証拠は`.harness/runs/20260923-r6-p4-review2/`に保存した。
- R6-P5: `.harness/runs/20260923-050747/`。HDR captureでRT無効時のIBL、R4 fallback、固定8 rendered-frame warmup後のRTGI静止golden、カメラ/物体移動、履歴棄却、移動後停止、点光源移動の4 rendered-frame以内の追従を確認し、専用`R6RTGIAcceptanceVulkanTest`は1/1 passed、Game buildはEXIT_CODE=0だった。ただし指定の全体`RenderingValidation`は34 passed・8 skipped・10 failed、EXIT_CODE=8であり、TASKSの状態を`blocked`へ戻した。既定のRendering3DTest起動経路と既定シーンは変更していない。
- R7-M1: `Docs/RenderingValidation/R7SamplingAndExrSelection.md` と `R7TechniquePlan.md` に、NEE + power heuristic MIS、TinyEXR v3.2.0 C11 API、linear RGB float/ZIP scanline、seed決定論性、R7 core/outdoor段階を固定し、P1〜P7/O1〜O3へ分割した。
- R7-P1: `PbrMaterialEvaluation.glsl`へ共通のBRDF・texture評価をまとめ、opaque/transparent rasterとRTGI computeから参照する。shader-root相対include展開を接続した。対象build成功、R1 all-numericalは40 static rows/68 numerical rows/120 capturesで合格し、Indoor goldenも合格。Outdoor goldenは開始時と同じ`mean_flip=0.002326954`・459画素差で失敗し、R1 baselineは変更していない。

- R7-P2: 明示選択の独立PT passをR5 RT pipeline/SBTとFramePacketのRT snapshotへ接続し、1 sample/frameの累積平均をSceneColorへ公開した。静止時の収束、描画packet欠番時の履歴維持、camera/scene/light・材質・形状変更時のresetをGPU readbackで確認した。frame slot別のdescriptor/buffer資源を使い、既定rasterとRendering3DTest起動経路は変更していない。

- R7-P3停止条件調査: RT材質snapshotがBaseColor/emissiveのみで、共有BRDF・texture評価に必要な値と資源が欠けることを確認した。指定build・CTestも未登録targetのため失敗し、タスクはblocked。
- R7-P4の停止調査（タスク未完）: 既存PTのGPUスモークは1/1 passed。指定テストは未登録で、Cornell公開参照との比較を実証できないためblockedとした。
- R7-P5: FramePacketの前後カメラ・RT instance transformから決定論的なシャッター時刻と薄レンズ光線を構築し、並進のsample別TLASを接続した。CoC解析値49.7312 pxに対して1024レンズ試料の49.7069 px、静止・移動の32試料GPU基準画像、可変DeltaTimeでの静止累積を固定した。指定Debug buildとCTest 2/2、既存RT snapshot/PT回帰2/2に合格した。
- R7-P6: TinyEXR v3.2.0 C APIのRGB float/ZIP scanline出力を追加し、固定seed・32 SPP・frameの実GPU PT画像2回をbyte一致で保存した。NaN/Infは保存前に拒否し、同一ディレクトリの一時ファイルを完成後に公開する。独立したPython読取器とOpenCVのOpenEXR読取器でchannel・precision・window・既知RGB値を確認した。指定Debug buildとCTest 1/1に合格した。

- R7-O1: FramePacketのSkyAtmosphereParametersからR2の空LUTと太陽ディスクをPTへ接続した。missで散乱空を評価し、太陽ディスク方向を明示サンプルして可視面へ直接照明を加える。空設定・露出・LUT公開状態の変化で累積履歴を破棄する。屋外GPUテストで朝・昼・夕の有限性、R2入力一致、太陽照度、空無効とLUT欠落・復帰を確認した。

- R7-O2: R3の高さ霧スナップショットから解析透過率と固定24段の方向光単一散乱をPTの有限ヒット区間へ接続した。R3の異方性0.76を内部で共有し、霧・空の無効時、空放射霧色、RT遮蔽、霧と光の変更時の履歴破棄をGPUで確認した。
- R6-GATE-OUTDOOR: `c1474fc`。R2の4カスケードCSM導入で変わったOutdoor基準画像を、ユーザー承認のもと正式な候補生成・publish手順で現行出力（SHA256 `9933B558…F3B954`）へ置き換えた。Indoorと閾値は不変。置換後のOutdoor golden 1/1、RenderingValidation 49件は失敗0件。
- 物理テストABI同期: `a41190e`。R6/R7で増えたFramePacket（1152 byte）とSceneProxy（512 byte）へ物理側の境界契約を合わせ、全buildを復旧した。
- R7-P3A: `8ac04e8` / `4500394` / `48fa18e`。RHIに配列combined image samplerと要素単位bindを追加し、Vulkan 1.2の非一様添字を対応時だけ有効化した。配列layoutだけpool容量を広げ、stage別・set全体のdescriptor上限をVulkanの計数規則で検査する。64要素のGPUテストで最大誤差2.97e-08・validation 0件、pool拡張を無効化した負の対照で失敗を確認した。RenderingValidation 50件は失敗0件。
- R6-P5再開: `3e5c374` / `e49cf5c`。RTGIの試料を描画フレームごとに更新し、履歴棄却をNDC深度から線形距離へ変えた（遠景で露出領域の古い履歴を使っていた）。受入れは履歴ageとデノイズ後間接光の読み戻しで判定し、露出170画素の最大age 0、カメラ移動時の保持率0.994、停止後の再蓄積、ライト追従率0.84〜1.40（5回）を確認した。自己参照TSVは削除し、参照比較はR6-P5-REFへ分けた。RenderingValidation 50件は失敗0件。
- R6-P5 2周目対応: `ab86d83` / `c4a9d93`。履歴距離の比較を前フレームのカメラ基準へ揃え（前進カメラで静止面を捨てていた）、停止後の残留を物体なし参照・物体あり・停止後の画素差で判定する。ライト追従は面光源を隠して外れ値を除き、到達率0.87〜0.98。距離基準を戻す負の対照で保持率0を確認。
- TEST-RGC: `5e1bdfa`。R7-P1のinclude展開でShaderManagerがファイルを読むようになり、空のshader置き場で初期化するRenderGraphCompileTestが失敗していた。実在するAssets/Shadersを渡して合格。
- R7-P3B: `1cdf101` / `058aab9` / `20b1764` / `8c80695`。RT材質snapshotへinstance色とalbedo・normal・metallic・roughnessのtexture handleを加え、PTはRenderThreadで解決して重複を除いた256要素の配列descriptorへ束ねる（既定はGBufferと同じ白・平坦法線・黒・中間灰）。closest-hitはMesh3DVertexの法線・UVを補間し、ラスタと同じ余接フレームと共通の復号関数で材質値を得る。発光は色×nits×プリエクスポージャ。余接フレームの退化判定は尺度不変な共通関数にし、ラスタの常時fallbackも解消した。PathTracingMaterialVulkanTestでUV・instance色・metallic/roughness・法線マップ（伸縮UV・逆巻き・退化UV）・既定値・重複除去・handle解放時の履歴破棄・発光・stride 12・表の上限を固定し、validation 0件。RenderingValidation 51件で失敗0件（8件はskip契約）。
- R7-P3C: `f605567` / `9128e3e` / `16f4532` / `9e0b35e`（記録 `179c3e4` / `c770ec6`）。DFG LUTの生成をラスタと共有し、PTのBSDFをラスタIBL端点と同じ多重散乱補償と(1-Ed)拡散にした（VNDF標本化、粗さ0でも有限な安定式、視線が裏なら幾何法線）。点・spot・方向光はLightingPassと同じ光源表のNEE、発光三角形と太陽円盤はpower heuristicのMIS、環境光は黒・一様・正距円筒（固定0.05を廃止）。影レイは浮かせた原点から目標点まで調べ、面光源は最も近い命中が標本化した発光三角形かで判定する。PathTracingLightingVulkanTestで白炉17行（平均相対誤差の最大0.626%）、解析照明（0.244%以内、spot円錐外0）、面光源の3戦略と解析照度（0.623%以内）、光沢金属・急な法線・太陽の3戦略の一致、光源直前の遮蔽板を固定。RenderingValidation 52件で失敗0件。
- R7-P3D: `39a674d` / `c520c23` / `b70b412` / `c3448c8` / `8d6f7c0`、評価対応 `a1dc7e6` / `9125b04` / `1f79a37` / `19227a9` / `97890d2`。起動時の設定（`--renderer=path-tracing`、試料数/frame）でmain SceneViewをPTにでき、既定はラスタ。PTはLightingPassと同じ環境マップ読み込み、検証表示252/253/254、正射影、取得時の最小試料数とRGBA32F読み戻しに対応した。レイトレーシングシーンは影を落とさない不透明物体も含め、影・DDGI・RTGIはinstance maskで影を落とす物体だけを調べ、影を落とす物体がなければ従来どおりfallbackする。PTのシェーディング法線はラスタの法線行列と同じ退化規則に従う。カメラ標本はdispatchごとの連続した添字で引く。PathTracingRasterParityVulkanTestでR1の白炉15行と既知光度4段階をPTでも同じ評価関数に通し、ラスタとの差は白炉のマスク平均0.405%・8x8区画0.577%、既知光度のROI平均0.648%以内。RenderingValidation 53件で失敗0件（ラベル内のR6停止残留の揺らぎはTEST-R6-RESIDUAL-TIMINGへ）。
- R6-P5-REF: `98fd290` / `6a03e1b`。PTに光輸送の範囲（多重散乱・直接光のみ・拡散1バウンス）を加え、R6受入れの静止段階と同じCornellでR6 RTGIの静止収束SceneColorを、R6の申告範囲に合わせたPT参照とLDR-FLIPで比べるテストを作った。閾値は比較前に固定（参照の間接光±20%の知覚差）。R6は平均0.699（閾値0.0816）・原寸の画素単位最大0.965（閾値0.187）・8×8区画最大0.966（閾値0.166）で超過し、ルール5によりR6を再オープンした。原因はラスタが発光三角形を光源として扱わず、面光源の直接光がRTGIの偶然の命中だけで入ること。陽性対照（155試料のPT）は閾値内。
- R6-P5-REF評価対応: `c3443f8` / `3ee9016` / `5e577a0`（原寸の画素単位最大の判定、局所欠陥と面光源の対照）。
- R6-P7: `128294a` / `81bbf24` / `3f4f39c` / `a15c43a` / `02eaa7e`、判定の見直し`c162366` / `f7d0b59` / `14dfc02` / `775261a`、静止時の履歴延長と年齢に応じたデノイズ。ユーザーの判断で画素単位最大は幾何が一致する画素（不一致は2画素）で判定し、直接光を解析BRDFで揃え、静止時だけRTGIの履歴の年齢の上限を8から64へ上げた（動いている間は従来どおり）。R6参照比較は平均0.0466・一致画素の最大0.150・区画0.100で閾値内（閾値と物差しは不変）。ニューラルBRDFの筋はFIX-NEURAL-BRDF-STREAKで扱う。
- R7-P4: `2365a2c` / `944452e` / 判定範囲の見直し。PTの試料数収束は合格（入れ子の16/64/256 spp接頭列のMSEが全体と64区画すべてで単調、MSE比の中央値4.374、引き直しでbyte一致）。公開Cornellは、公開RGBEの色の符号化が不明で色の壁の彩度を再現できないため、ユーザーの判断で輝度を白い面・影・発光面の位置で、赤・緑の壁と赤・緑ROIを優勢色度で判定する範囲へ見直し（数値は事前固定のまま）、影ROI 0.0025・色度差0.0044/0.0217・580区画の中央値0.0101/90%点0.0340で合格。
- TEST-R6-RESIDUAL-TIMING: R6停止残留の物体影響の差はRTGIが面光源を光源標本しないことによるもので、R6-P7で解消した（テストの待ち時間は不変）。RenderingValidationラベル全件の3回連続実行で、R6停止残留は毎回最初の段階がframe 166〜177・物体影響0.0267〜0.0289（単体実行0.0251〜0.0280）で通過した。
- R4再照合（更新ルール5、2026-09-25）: `R4DDGIPathTracingReferenceVulkanTest`でR4受入れと同じCornell状態のDDGIを4096 sppの自前PTとR4の指標で比べ、赤ROIの相対輝度誤差0.257が閾値0.25を超えた（影0.162、緑0.057、色度差0.009/0.017）。R4を再オープンし、R4-REOPENで直す。
- R6-P6: R6を受入れた（完了条件: R6-a単独ゲート、動的ライト追従、R7自前PTとの静止収束比較、性能はDeferred）。全体gateはtargetless build成功、RenderingValidation 56件で失敗は再オープン中のR4の再照合1件だけ。記録は`Docs/RenderingValidation/R6Acceptance.md`。

- R7（2026-09-25）: コアと屋外拡張を受入れ（`Docs/RenderingValidation/R7CoreAcceptance.md`・`R7OutdoorAcceptance.md`、完了コミットのtrailerは`RenderingRoadmap: R7 complete`）。R6は自前PTの拡散2バウンス（median of means）と閾値内、R4は再照合で超過し再オープン。GPU性能はDeferred。
- R4-REOPEN（2026-09-25）: R4を再受入れ（`Docs/RenderingValidation/R4Acceptance.md`、完了コミットのtrailerは`RenderingRoadmap: R4 complete`）。probeの分類、probeでの発光面の直接照度、全体/間接光だけの2組のlayer、RTXGIの補間で、自前PTとの比較は影0.117・赤0.224・緑0.130（閾値0.25、不変）。公開Cornell参照・動的更新・golden・RenderingValidationラベル（57件中0件失敗）も通過。
- R8完了（2026-09-26）: R8-P4〜P11。被写界深度は比較の入力を画素内の一様標本にそろえて規則の判定PASS、動きぼけは動く球の床の影を既知の限界として規則の判定FAIL・既知の限界の範囲の判定WITHIN、フィルムグレインは既定オフ、EXR連番の検証exe、屋内・屋外の240フレーム（1280×720・1024 spp）はそれぞれ欠番0・非有限0・ポッピング0。記録は`Docs/RenderingValidation/R8Acceptance.md`。
- R8-P1（2026-09-26）: `Scripts/BakeAcesOutputLut.py`でOCIO 2.5.2の組み込み`studio-config-v4.0.0_aces-v2.0_ocio-v2.5`から、`sRGB - Display`／`ACES 2.0 - SDR 100 nits (Rec.709)`を65³ RGBA16F（display-linear）の`Assets/ColorManagement/Aces20SdrRec709.lut3d`へ焼き、HDR試験チャート（256×240）とOCIO厳密変換の基準画像を`R8Aces*`に置いた。shaperは`log2(x/2^-8+1)/log2(2^16+1)`（範囲[0,256]）。`--verify`は4ファイルbyte一致、チャート最大1.577/255（閾値2/255）でEXIT_CODE=0。記録は`Docs/RenderingValidation/R8ColorManagement.md`。
- R8-P2（2026-09-26）: `ToneMappingPass`に`Aces20Lut`演算子（operatorType 4）を加え、R8-P1の65³ LUTを3D texture（binding 2、RHIの`Texture3D`をGPUで初めて標本化）として固定のlog2 shaperと半テクセル補正で引く。LUT演算子では既定のグレーディングを掛けない。非LUT演算子は1×1×1の代替3D textureを結び、LUTの読込・作成・転送の失敗はACES Filmicへ退避する。起動引数`--tone-map=aces|aces20-lut`（既定はACES Filmicのまま）。`R8AcesLutToneMappingVulkanTest`でチャートの全61,440画素がOCIO基準画像とsRGB符号化後最大1.598/255（閾値2/255）、Filmic対照は223.96/255で不合格、作成失敗の注入時はFilmicと全画素一致、VUID_COUNT=0。Indoor/Outdoor goldenは不変。
- R8-P3（2026-09-26）: PTの連番の1フレームの経路を加えた。`CameraProxy::SequenceFrame`（0は連番でない）と、GameThread側の`PathTracingSequenceCarry`（`FramePacketManager`が所有し、`RenderingCoordinator::GenerateDrawCommands`でRTスナップショット構築の直後に毎パケット適用）で、同じSequenceFrameの間は全パケットの前カメラ・instance前変換を直前フレームの最後の状態に固定する。instanceはObjectId・メッシュ・index範囲・描画内番号で対応付け、インスタンシング描画は各instanceの元の物体IDをMeshProxyとの照合で求める。`PathTracingPass`はフレーム長（DeltaTimeを使わない）でシャッター区間を決め、dispatchごとにレンズとシャッター時刻を引き直す。起動引数`--path-tracing-frame-duration/--path-tracing-shutter/--path-tracing-aperture/--path-tracing-focus-distance`。`R8PathTracingSequenceFrameVulkanTest`で動きぼけ幅の誤差最大0.33 px（閾値1 px）、CoC相対誤差0.13%（閾値2%）、先頭パケット欠落でも64 sampleの履歴継続・再実行byte一致を確認した。既存PT／R4・R6・R7参照比較は16件すべてpassed。
- R8-P3-FIX（2026-09-26）: 連番の経路（SequenceFrame≠0）では`ApplyPathTracingSequenceCarry`がRTスナップショットのinstanceを物体ID・メッシュ・描画内番号・部分範囲・出現順の鍵の順へ並べ、customIndexを並びの番号に振り直す。材質texture表・発光instance表・TLASはこの並びから作られるため、同じフレームの中で描画の並びが変わってもPTの幾何署名が変わらず累積が続く。連番でないパケットの並びは変えない。`R8PathTracingSequenceFrameVulkanTest`に、物体P・A・B（IDは並びと一致しない50・40・60）をP,A,B→P,B,Aと並べ替えた2回のdispatchで試料数1→2、固定順の画像とbyte一致を確かめる検査を加えた。
- SS-CAPTURE（2026-09-27）: 起動画面の撮影経路を加えた。`Game.exe --capture-png=<path>`はアセットの読み込みが落ち着いてから60描画フレーム（`--exit-after-rendered-frames`で変更可）の後にBackBufferの取得を要求し、PNGに保存して終了コード0で終わる（取得・保存の失敗や120描画フレーム以内に結果が来ない場合は1）。撮影中はoverlay（ImGui）を描かず、キャンバスのボードを無効にする。`--startup-camera=<yaw>,<pitch>,<arm>`でRendering3DTestの起動時のSpringArmを指定する。`Scripts/CaptureStartupScene.ps1 -OutDir <dir>`が既定・近接（0,5,2.5）・低角度（20,-8,6）の3視点を撮り、各PNG・`<view>.Game.log`・`metrics.json`（平均輝度・白飛び率・黒つぶれ率）を出す。
- SS-POM（2026-09-27）: POMと余接フレームを`Assets/Shaders/Common/ParallaxOcclusionMapping.glsl`（`CalculateCotangentFrame`・`ApplyParallaxOcclusionMapping`）へまとめ、`gbuffer.frag`・`forward_transparent.frag`・`megageometry.frag`が同じ関数を使う。高さは白=高いとして`1.0 - height`で深さにし、輪郭のフェードと層数は幾何法線とビュー方向の内積で決める。接空間のビュー方向は正規化したT・Bへの射影から作り、マーチ中は分岐前に取ったUV勾配で`textureGrad`を使う。余接フレームは元のUVから一度だけ作り法線マップにも使う。石畳の法線マップは`nor_gl`がエンジンの基底（T=+∇u、B=+∇v、vは画像の下向き）と緑が逆なので`nor_dx`へ替えた。あわせて撮影スクリプトのシェーダー失敗の除外をSlang SDK未設定の`neural_material_decode.slang`だけに絞り、`--startup-camera`の接頭辞判定を独自の`String`で行う。

- SS-CSM-DISTANCE（2026-09-27）: CSMの分割の奥をカメラのfarではなく影の最大距離（`ShadowMapPassSettings::MaxShadowDistance`、既定80 m、`ShadowMapPass::SetMaxShadowDistance`で変更可）とfarの近い方にした。起動画面（画角60°・near 0.1 m・far 1000 m）の分割は0.1/10.3/21.4/37.5/80 m、1テクセルはカスケード0から順に1.29/2.53/4.39/9.44 cm（以前はカスケード0が0.1〜125.5 m・15.7 cm）。`lighting.frag`・`forward_transparent.frag`・`volumetrics.frag`は最後のカスケードの奥の10%で影を薄め、最大距離より遠い面は影を受けない。ShadowMapPassは分割が変わったときに`csm_splits`・`csm_texel_m`を記録する。Outdoor goldenは屋外シーンのfar 100 mが80 mに切られてカスケードが細かくなり、球の影の輪郭86画素だけが変わったため再承認した（`R1Acceptance.md`）。
- SS-SHOWCASE（2026-09-27）: 起動画面に材質見本の球（半径0.4 m、色はリニア(0.9,0.7,0.4)、手前の列が金属0・奥の列が金属1、左から粗さ0.1/0.3/0.5/0.7/0.9）と小屋を置き、地面を60 m四方の石畳（2 mのタイル、POMの高さのスケール0.03、テクスチャは球の石畳と共有して読み込みは1回）にした。`MaterialCreateData`に`Metallic`・`Roughness`（0〜1、負は未指定）を足し、GBufferPassはテクスチャが無くスカラー値があるときに、その値の1×1の灰色テクスチャ（8bitの値ごとに初回だけ作る）を既定のテクスチャの代わりに使う（未指定の材質の結果は変わらない。ForwardPass・MegaGeometry・RT/PTの材質はまだ値を読まない）。小屋は`Scripts/ConvertObjToGltf.py`（標準ライブラリだけ）でOBJを`Cottage_Clean.gltf`/`.bin`へ変換し、AO・粗さ・金属の3枚を1024²のARM（`Cottage_Clean_ARM.png`）にまとめた。基本色・法線は4KのPNGをそのまま参照する。GLTFAnalyzerでMegaGeometryとして読み、Z=-18 mに置く（cooked モデルの計測では読まない）。球（中心Y=0）と岩（Y=-0.93）を地面に接地させ、既定のカメラを腕の長さ10 m・仰角20°にした。
- SS-POINT-SHADOW-P1（2026-09-27）: 点光源のキューブシャドウを描く経路を加えた。GameThreadの`RenderingCoordinator::GenerateDrawCommands`が空の太陽を加えた後の光源表とメインカメラから`BuildPointShadowSnapshot`（`Rendering/PointShadowSnapshot.h`）で、影を落とす・表示中・範囲がnear面0.05 mより大きい点光源をカメラから近い順に最大4灯選び、6面のビュー行列（Vulkanの面の順+X,-X,+Y,-Y,+Z,-Z、ビュー+X=sc・+Y=tc）と90°の射影を`FramePacket::PointShadows`に入れる。`ShadowMapPass`は選ばれた灯があるフレームだけ、512²×6面×4灯のキューブ配列（D32、初めて要るときに作る）の各面へ`point_shadow.vert/.frag`（スキンは`skinned_point_shadow.vert`）で光源からの距離/範囲を`gl_FragDepth`に書き、RenderGraphの`PointShadowCubeMap`として公開する（使わないキューブの面は消去だけ）。キャスターはMeshProxy（物体ID）・スキンの境界球（ComponentId）・インスタンシングは同じメッシュの境界球を包む球で、光源の範囲の球と交わるものだけ描く。RHIはキューブ（配列）の層ごとの描画先（層=キューブ*6+面）と`imageCubeArray`を加えた。LightingPassはbinding 20に`samplerCubeArray`（無いフレームは距離1の1×1の既定値）を置き、光源バッファの`attenuation.w`にキューブの番号+1を入れる（照明の計算はまだ読まない）。デバッグ表示`DebugViewMode::PointShadowDistance`（F5で巡回）は、最初のキューブを持つ点光源への方向で格納距離を灰色、遮られた面を赤、範囲外を暗い青で出す。
- SS-AUTOEXPOSURE-P1（2026-09-27）: 自動露出の測定を加えた。`AutoExposurePass`（ポストプロセスのSSRとBloomの間）がSSR後のSceneColor（無ければScene.Color）の輝度（Rec.709の係数）からプリエクスポージャを外し、log2 輝度 -10〜22（cd/m²）を256区間に分けたヒストグラムを`auto_exposure_histogram.comp`（16×16のグループ、共有メモリで数えてから全体へ足す）で作る。ヒストグラムはフレームスロットごとの読み戻しバッファへ写し、同じスロットが次に回ってきたとき（スワップチェーンの待機で前の提出の完了が保証される）にCPUで読むので、RenderThreadの同期は変えない。EV100の計算と順応は`Rendering/AutoExposure.h`の関数（下位10%・上位2%を画素単位で除いた log2 輝度の平均 + 3 − 露出補正を最小・最大EV100で止め、明るくなる向き1/秒・暗くなる向き3/秒の指数順応）で、記録時のTotalTimeの差をタイムステップにする。数えた総数が画素数と違う読み戻しは捨てる。求めた値は最初の測定と30測定ごとにログへ出し、`GetLatestMeasurement()`で取れる。画面の露出はまだ変えない。`AutoExposureMathTest`（`CameraViewConstantsTest`の束）が区間の割り当て、外れを除いた平均と画素を並べた素朴な参照の一致、境目の区間の端数、露出補正・上下限、順応と閉じた式・微分方程式の積分の一致、向きごとの速さ、刻みの分け方によらないこと、無効な入力を確かめる。パスの実装はCoreのCMakeListsへ足さず、SceneView.cppから`AutoExposurePass.inl`として取り込む（被写界深度と同じ）。
- SS-AUTOEXPOSURE-P2（2026-09-27）: 自動露出を画面に掛けた。`CameraProxy`に`CameraExposureMode`（Manual/Auto、既定Manual）を足し、`CameraComponent::SetExposureMode`で設定する。Autoのカメラでは`SceneView::Render`が、その View の`AutoExposurePass`で前のフレームまでに順応させたEV100から露出（`AutoExposurePreExposureFromEV100`、手動と同じ 2^(-EV100)/1.2）を求め、描画の間だけ露出を写したカメラの複製へ`MainCamera`/`CurrentCamera`を差し替える（FramePacketのカメラは書き換えない。全パスが同じ露出を使い、測定はそのフレームに掛けた露出で割るので、掛けた露出が次の目標へ跳ね返らない）。最初の測定までと、Autoでもカメラの露出補正を自動露出の補正に使う。`AutoExposurePass`は同じフレームの2つ目以降のViewportでは測らず（P1の評価の指摘2）、1測定で進める順応の時間を0.1秒までにして、描画が止まった後も数フレームかけて順応させる。明るくなる向きの速さを1→2/秒にした（約5 EVの差が0.1 EVまで約2秒）。目標から0.5 EV以上離れたときに順応の開始・0.25秒ごとの途中・0.1 EV以内での終了（経過時間、止めた後の順応の時間、目標を越えた量、向きの反転の数）をログへ出す。起動画面（Rendering3DTest）はAutoにし、ImGuiの「空の太陽」ウィンドウに「自動露出」のチェックボックスを足した（`--exposure-ev100`は手動の値と自動の最初の測定までの値になる）。環境変数`NORVES_STARTUP_SUN_STEP=<仰角>,<秒>`で起動からその秒数の後に一度だけ太陽の仰角を変えられる。`AutoExposureMathTest`に、露出の式と中間の灰（一様な画面は露出後0.104）、読み戻しの遅れ（2〜3フレーム）のある閉ループで昼↔夕の急変が1〜3秒で目標を越えず単調に落ち着くことを足した。
- FIX-AUTOEXPOSURE-READBACK（2026-09-27）: 自動露出の読み戻しをホストへ見せ、EV100を起動画面に出した。RHIの`ResourceState`に`HostRead`（Vulkanでは`VK_ACCESS_HOST_READ_BIT`・`VK_PIPELINE_STAGE_HOST_BIT`）を末尾に足し、`AutoExposurePass`はヒストグラムを読み戻し先へ写した後に`CopyDest→HostRead`のバッファのバリアを記録する（既存の状態の割り当ては変えていない）。読み戻したヒストグラムを`GetLatestHistogram()`で取れるようにし、`AutoExposureMeasurement`を`AutoExposure.h`へ移した。`SceneView::TryGetAutoExposureMeasurement`で最初のSceneViewの測定をRenderThreadで集め、`RenderingCoordinatorStatsSnapshot::AutoExposure`としてGameThreadへ渡す（完了・未完了のどちらの公開でも最後の測定を載せる）。Rendering3DTestのTickがスナップショットから写し、ImGuiの「空の太陽」ウィンドウに目標と順応後のEV100を出す（測定が無い間はその旨を出す）。`AutoExposureHistogramVulkanTest`（`R8AcesLutToneMappingVulkanTest`の束へ追加、新しい実行ファイルは作らない）が40×24の灰色の画像（黒・下端より暗い値・8区間）をRenderGraph越しにパスへ通し、同じスロットの次のフレームで読み戻した256区間がCPUの`AutoExposureLuminanceToBin`で数えた期待値と全て一致し、測定の画素数が960になることを確かめる。
- SS-BLOOM-MIPCHAIN（2026-09-27）: ブルームを段階的な縮小・拡大に置き換えた。`BloomPass`は描画解像度の縦横半分から6段（`BloomSettings::MipCount`、上限8、1×1で打ち切り）の自前のRGBA16Fテクスチャを持ち、`bloom_downsample.frag`（13回の双線形サンプルで中央の箱0.5・四隅の箱0.125ずつ、最初の段だけ各箱を1/(1+輝度)で重み付けするKaris平均と非有限値の除去）で縮小し、`bloom_upsample.frag`（下の段の3×3テント、半径`Radius`=1テクセル）を下の段から順にこの段の縮小結果へ足す。`bloom.frag`は最上段を3×3テントで拡大して段数で割り、既定のしきい値なし（`Threshold`=0）では元の色と`Intensity`=0.04で線形補間してエネルギーを保つ（しきい値が正なら最初の段で明るい部分を取り出し、加算する）。段のテクスチャはパス内で閉じ、RenderGraphへは従来どおり`Bloom.SceneColor`だけを公開する。デバッグ表示では強度0で元の色をそのまま出す。SceneViewの既定をこの値にした。Indoor/Outdoor goldenは旧方式の縁取りが消え全体へ薄いにじみが混ざったため再承認した（`R1Acceptance.md`）。
- FIX-CAPTURE-SUN-AZIMUTH（2026-09-27）: 撮影スクリプトの`-SunAzimuth`で`[Nullable[double]]`の`.Value`がnullになり落ちていたのを`[double]`変換に直した。`-SunElevations 3 -SunAzimuth -120`で3視点を撮影し`result=pass`、Game.logに`--sun-azimuth=-120`と`sun_azimuth=-120.0`、low-sun3.pngで地平線近くの太陽が画角に入ることを確認した。証跡は`.harness/runs/20260927-041058/verify-FIX-CAPTURE-SUN-AZIMUTH-1.txt`。
- SS-TAA-P1（2026-09-27）: TAAを加えた（既定は無効）。`Rendering/TemporalAA.h`がHalton(2,3)の1〜8番目から0.5を引いた画素のずらし量とNDCのずらし量（2×画素/寸法）を求め、`CameraProxy`の`ProjectionJitterNdcX/Y`を`CameraViewConstants::BuildProjectionMatrix`が列ベクトル規約のNDCの平行移動として投影の前に掛ける（透視・正射影とも、どの深度でもNDCが同じ量ずれ、z・wは変わらない）。`CameraProxy::AntiAliasing`（`CameraAntiAliasingMode::FXAA`既定/`TemporalAA`、`CameraComponent::SetAntiAliasingMode`）でカメラが選ぶ。`SceneView::Render`は、TAAを選んだカメラのViewportで、自動露出の後のカメラの複製へジッタを掛けて`MainCamera`/`CurrentCamera`を差し替え、前のカメラ（`PreviousMainCamera`）の複製にも同じジッタを掛ける（velocityと空の再投影からジッタが消える。FramePacketのカメラは書き換えない）。同じフレームの2つ目以降のViewportには掛けない。`TemporalAAPass`（ポストプロセスのSSRの後・自動露出とブルームの前）は`SSR.SceneColor`（無ければ`Scene.Color`）の3×3近傍のYCoCgの平均±1σの箱へ、最も手前の画素のvelocity（空はカメラの動きから）で再投影した履歴（Catmull-Romの5回読み）をクリップし、現在の色0.1と1/(1+輝度)の重みで混ぜて、2枚の履歴を交互に書いてSceneColorへ書き戻す。露出が変わったら履歴へ現在/前のプリエクスポージャの比を掛ける。履歴は最初のフレーム・カメラIDの変化・前のカメラ無し・寸法の変化・働かなかったフレームの後で捨てる。TAAが働くViewportではFXAAを外し、止めたら戻す。環境変数`NORVES_TEMPORAL_AA=1`でカメラにかかわらず掛けられる（撮影の確認用）。
- SS-TAA-P1の指摘対応（2026-09-30）: 履歴とvelocityの基準フレームの食い違いを直した。RenderThreadは未描画のパケットを新しいパケットで置き換えるため、撮影では描いたフレームのほぼすべてがゲームのフレームを飛ばしていた。`TemporalAAHistoryTracker`（`Rendering/TemporalAA.h`）が履歴を書いたフレームの番号・時刻・Viewport・カメラ・露出を持ち、`TemporalAAPass`は履歴のカメラ（今のフレームと同じジッタ）から深度でカメラの動きを求め直し、物体自身の動き（velocityからパケットの前のカメラによる動きを引いた分）を経過時間の比で伸ばして足して再投影する（連続したフレームではvelocityでの再投影と同じ）。パケットの前のカメラは直前のゲームのフレームのものだけを渡す（`RenderingCoordinator`の`m_PreviousMainCameraFrameNumber`）。1つのSceneViewに2つのViewportがあるとき、TAAを掛けない2つ目のViewportは履歴に触れず、履歴を書いたViewportをTAA無しで描いたときだけ捨てる（`TemporalAAPass::NotifyViewportRendered`）。確認用に環境変数`NORVES_DEBUG_DROP_RENDER_FRAME_INTERVAL=<k>`で、フレーム番号がkの倍数のパケットを描かずに捨てられる（`RenderThread::TryDropPacketForDebug`）。
- SS-TAA-P1の指摘対応2（2026-09-30）: 物体の動きを経過時間の比で伸ばす方式をやめ、velocityの基準そのものを履歴のフレームへ揃えた。`Rendering/RenderedObjectHistory.h`をRenderThreadの`RenderingCoordinator::RenderFrame`（インスタンスの転送の前）で使い、最後に描いたフレームの物体の変換（MeshProxy・MegaGeometryはComponentId、スキニングは元のMeshComponentのIDごとの変換とパレット）を覚え、パケットが最後に描いたフレームの直後でないとき、TAAを選んだカメラ（または`NORVES_TEMPORAL_AA=1`）ならパケットのインスタンスの`PreviousWorld`・MeshProxy・MegaGeometry・スキニングの前の値をそのフレームのものへ付け替える（インスタンスはWorld・PreviousWorldが一致するMeshProxyで見分け、見分けられなければ不完全とする）。TAAを選ばないカメラではパケットを変えない。付け替えた基準のフレーム番号と完全さを`ViewRenderContext::PreviousObjectStateFrameNumber`・`bPreviousObjectStateComplete`で渡し、`TemporalAAHistoryTracker`は履歴のフレームと一致し完全なときだけ履歴を使う（`ObjectStateMismatch`）。カメラは、飛んだフレームで`SceneView`が前のカメラを履歴を書いたフレームのカメラ（`TemporalAAPass::FindReprojectionCamera`、今のフレームと同じジッタ）へ差し替える。`temporal_aa.frag`はvelocity（空は前のカメラからのカメラの動き）でそのまま再投影し、履歴のカメラ・時間の比の定数を除いた（256 byte）。

- SS-SKY-MODEL-P1（2026-09-30）: 空のモデル（`SkyAtmosphere.cpp`）を球殻の大気のレイマーチ（観測点は地表から100 m、視線32点）と Hillaire 2020 の等方の多重散乱の表、オゾンの吸収に置き換え、地平線より下を `GroundAlbedo` の地面として返す。公開の評価器 `SkyAtmosphereModel` が前計算（透過率の表・多重散乱の表・地表の空の照度）と sky-view の表（仰角80×方位差40）を持ち、空のradiance LUT・透過率LUT・空由来のIBLはこれを1回作って引く。radiance LUTは視線の透過率と地面を含むため、`lighting.frag` の背景と `PathTracingRayGen.glsl` の不交差の透過率の掛け算を外した。表示の太陽円盤は地表の透過率で減光・着色する。LUTは空のパラメータが変わったときだけ作り直し、生成時間を `Sky LUT generated in ... ms` でログに出す。Mieは晴天の典型のエアロゾル（光学的深さ約0.12）、`GroundAlbedo` の既定値は0.3。根拠と数値は `Docs/RenderingValidation/R2Acceptance.md` の「空のモデルの置き換え」。
- SS-SKY-MODEL-P1の差し戻しの対応（2026-09-30）: レイマーチの区間の積分 (1 - e^(-σt·Δt)) / σt が、許される最小の尺度高さ（Rayleigh・Mieとも100 m）で上空の密度が0へ落ちた区間で0/0になり、多重散乱の表・地表の空の照度を通して空全体が無効（放射輝度0）になっていた。光学的厚さが10^-6以下の区間は Δt·(1 - σt·Δt/2) の展開を使う（超越関数の回数は変えない）。テスト側の独立な積分も同じ0除算を `expm1` と極限で直し、尺度高さ100 mで天頂・真下・地表の照度が有限で有効な回帰テスト `TestZeroExtinctionSegmentsStayFinite` を足した。変更したテストの英語の失敗説明を日本語にした。
- SS-SKY-MODEL-P2（2026-10-01）: 空を有効にする検証を新しい空で回し直した。R2の昼のアンカー（天頂 (1798.9, 2416.1, 4064.7)・太陽の近く (48920.9, 44979.5, 40593.4) nits）と空のgolden 3枚は空のモデルの変更だけによる差として再基準化・再承認した（天頂の輝度 約2404 nits・B > G > R）。`PathTracingOutdoorVulkanTest` に足した空と地面からの間接光の比較が期待の58%で落ちた原因はテスト側で、面の画素を直接光の明るさで選んでいたため面より明るい新しい空の背景を拾っていた。画素中心の1次命中距離の検証出力で面を選ぶよう直し、0.146013 対 期待 0.146141。R7屋外の参照比較は3時刻とも閾値内（昼 FLIP平均 0.0195／閾値 0.0367）。閾値と規則は変えていない。記録は `R2Acceptance.md`・`R7OutdoorAcceptance.md` の「新しい空での再照合」。
## In progress

- 再開（2026-09-30）: 2026-09-27の引き継ぎ後の進捗は`a8588cc`（PROGRESS.md・TASKS.mdのBOMの除去とSS-TAA-P1をdoingへ戻す変更）だけで、コードの変更は無かった。BOMを戻した。SS-TAA-P1は`7461c3e`（未検証の途中保存）から続ける。下の止めていた4項目は各`blocked/<ID>.md`の推奨の選択肢で再開し、TASKS.mdにSS-SKY-MODEL-P1・P2とSS-EMISSIVE-PREEXPOSEを足した（ユーザーの方針: 細かな判断で止めず推奨で完走する）。
- SS-DAYLIGHT-P1（2026-09-27、blocked）: 起動画面の側を実装した。R2の空を有効にし（仰角40°・方位30°）、シーン独自の方向光を外して、矢印キーとImGui（「空の太陽」ウィンドウ）の角度を`SkySunControl.h`で空の太陽の仰角・方位へ写す（太陽は地平線より下へ行かない。Leaveで空を無効へ戻す）。`f90e7ea`の露出補正を外してf/16・1/100 s・ISO 100（EV100 約14.6）、点光源は1600 lm、発光球は1800 nits。影は地面にくっきり落ちるが、空のモデル（`EvaluateHillaireSkyReference`）が暗く（天頂732 nits、水平面の空の照度は全体の5.7%）、地平線の付近が橙、地平線より下が黒のため、「青い昼の空」を満たさない。空のモデルを直すとR2の空のgoldenとPT参照が変わるので、判断を`blocked/SS-DAYLIGHT-P1.md`に書いて止めた（推奨はSS-SKY-MODELを先に行う）。
- SS-POMの評価の指摘（点光源の近くで法線の緑の向きを確かめる）は未対応。昼の屋外では向きの分かる空の太陽で、太陽側の石の斜面が明るいことを`nor_gl`/`nor_dx`で比べて確かめられる。

- SS-DAYLIGHT-P2（2026-09-27、blocked）: 起動画面の側を実装した。`--sun-elevation`・`--sun-azimuth`で起動時の空の太陽、`--exposure-ev100`で手動露出（f/16・ISO 100を保ちシャッター速度で合わせる）を指定でき、ImGuiの「空の太陽」ウィンドウに「露出 EV100」のスライダーを足した。撮影スクリプトは`-SunElevations 10,45,3`で3視点×3時刻を`<視点>-sun<仰角>.png`に撮る（露出は`-ExposureEV100s`か、仰角からの目安）。高さフォグは`--height-fog-density`で掛けられるが、空のモデルが地平線より下の放射輝度を0にするため下向きの視線でフォグが黒くなり、地面を暗くするだけになる（既定視点で地面0.91倍、空の色へ霞まない）。既定の密度は0にし、判断を`blocked/SS-DAYLIGHT-P2.md`に書いて止めた（推奨はP1と同じくSS-SKY-MODELを先に行う）。

- SS-POINT-SHADOW-P2（2026-09-27、blocked）: 点光源の影をライティングに掛けた。`Assets/Shaders/Common/PointShadow.glsl`の`SamplePointShadow`（4×4のPCF、キューブの1テクセルに比例する法線方向のずらし、各タップで受け面を平面とみなしてそのタップの方向の距離と比べる）を`lighting.frag`・`forward_transparent.frag`が使い、光源バッファの`attenuation.w`（キューブの番号+1）が0でない点光源だけに掛ける。透明物はLightingPassが公開するキューブ配列（`PhysicalLightingResources::PointShadowCubeTexture`、無ければForwardPassの距離1の既定値）をbinding 14で読む。岩・小屋（MegaGeometry）は`MegaMeshGPUData::ShadowIndexCount`（LOD0の範囲）を`point_shadow.vert`のUBOの行列で1回で描く。Rendering3DTestの点光源を影ありにした。検証ゲートは合格したが、夕（仰角3°、EV100 11.5）の地面は約4000 lxで、1600 lmの点光源（真下で約32 lx）の影は画面で見えない。見える条件の判断を`blocked/SS-POINT-SHADOW-P2.md`に書いて止めた（推奨は夜の撮影条件を足す）。

## Next

- SS-BLOOM-MIPCHAINの評価指摘2（仰角45°の太陽を画角に入れた撮影）は`-SunAzimuth`で撮れるようになった。
- 起動画面（Rendering3DTest）の描画改善をTASKS.mdの`SS-`の項目で進める（ブランチ`feature/startup-scene-rendering`）。見た目の証拠は`Scripts/CaptureStartupScene.ps1`の撮影で確かめる。R8までの残りの`todo`10件は`backlog`にした。順序はSS-TAA-P1 → SS-SKY-MODEL-P1・P2 → SS-DAYLIGHT-P1・P2 → SS-POINT-SHADOW-P2（夜）→ SS-EMISSIVE-PREEXPOSE → SS-EMISSIVE-GLOW → SS-TAA-P2以降。
- R8は完了（2026-09-26）。R3・R5は受入れ記録（`R3Acceptance.md`・`R5Acceptance.md`）があるが、ロードマップの表は未着手のままで完了のtrailerも無い（整理が残る）。ほかはTASKS.mdの修正・改善の項目。FIX-NORMAL-MATRIX-SCALEは基準画像への影響を確かめてから扱う。R7-O3の既知差はRTGI-HIT-SPECULAR・RTGI-MULTI-BOUNCE・FIX-CSM-TERMINATOR・FIX-GRAZING-IBL-SPECULARとして残す。

- SS-DAYLIGHT-P1・P2 は新しい空（SS-SKY-MODEL-P1・P2 完了）で再開できる。
## Notes
- SS-SKY-MODEL-P2検証（2026-10-01）: `.harness/runs/20261001-193759/verify-SS-SKY-MODEL-P2-9.txt`で指定build EXIT_CODE=0（`-1`は空の間接光の比較を直す前の同じ手順）、`-2`で指定CTestは `PathTracingOutdoorVulkanTest` だけ不合格（sky_indirect_surface=0.0847 対 0.1461）、直した後の`-3`で3/3 passed、`-4`・`-5`で空を有効にする他のテスト（`RenderingGoldenImageComparatorTest`・`SkyAtmosphereModelTest`・`PathTracingMaterialVulkanTest`・`PathTracingLightingVulkanTest`・`RenderingDDGILightingContractTest`・`R8PathTracingSequenceFrameVulkanTest`）のbuild EXIT_CODE=0と6/6 passed、`-6`でR7屋外の参照比較 passed（721 s）、`-7`で同じ取得画像の比較の数値（`r7_outdoor_comparison=PASS sanity=PASS`）、`-8`でR2の自己検査と `--r2-scenario=sky-time-sweep` が PASS（GPUの読み戻しの昼の天頂は新しいアンカーと一致）。切り分けは一時的なシェーダーの変更（散乱光線の不交差を0・定数1にする）で行い、戻した。R2の実GPU取得は屋外の検証シーンの露出のままで平均RGB 233〜248と白に近いが、シナリオの判定は空でない・一様でない・変化するだけなので既知の限界としてR2Acceptanceに記録した。`R8SequenceSmokeTest`（`R8SequenceRenderer` の連番。屋外のシーンは空を使う。時間がかかる）は回していない。
- SS-SKY-MODEL-P1検証（2026-09-30）: `.harness/runs/20260930-200402/verify-SS-SKY-MODEL-P1-5.txt`でconfigure EXIT_CODE=0、`-6`でGame・SkyAtmosphereModelTest・RenderingGoldenImageTestのbuild EXIT_CODE=0（コンパイラ警告なし。第三者のLNK4099は既存）、`-7`で指定CTest 3/3 passed（Indoor/Outdoorのgoldenは空を無効にしているので一致）、`-8`で撮影9枚 result=pass（平均輝度 既定 10°84.5・45°87.4・3°85.7、白飛び率 最大0.00034）、`-9`で太陽の方位-150°（太陽を画角の側へ）の撮影 result=pass（`startup-capture/SS-SKY-MODEL-P1-toward-sun`。夕の太陽側の地平線が橙〜黄、昼は太陽の側に白い光冠）、`-10`で`SkyAtmosphereModelTest`の直接実行（独立な積分との差 最大2.2%・2.8%、天頂2103 nits、空の照度の割合23.8%、反太陽側の地平線 R/B 0.76）。LUTの生成は撮影の各起動で1回 117〜140 ms（Debug）。`-1`〜`-4`はオゾンの吸収と透過率LUTの表引きを入れる前の同じ手順の記録で、生成時間が最大166 msと150 msを超え、夕の太陽から離れた空が緑がかったため直した（撮影は`startup-capture/SS-SKY-MODEL-P1-before-ozone`）。技術判断: エアロゾルの量は Hillaire 2020 の既定値（光学的深さ約0.005）では天頂約1100 nits・空の照度の割合9%と晴天の実測より暗いため、晴天の典型（約0.12）にした。独立な積分との差は、多重散乱の表の高度を3乗の配置（地表側を細かく）、透過率の表を光学的深さで補間にして5%以内へ下げた（線形の透過率・2乗の高度では仰角3°の地平線で最大26%）。LightingPass.cppは行末が混在しており、変更していない行の行末を元に戻した（`git diff --numstat`と`--ignore-cr-at-eol`が一致）。空由来のIBLの元画像もLUTと同じ表の経路で作る（同じフレームで評価器をもう1回作る）。
- SS-SKY-MODEL-P1差し戻し対応の検証（2026-09-30）: `-17`でconfigure EXIT_CODE=0、`-18`でbuild EXIT_CODE=0（コンパイラ警告なし）、`-19`で指定CTest 3/3 passed、`-20`で直接実行 PASS（尺度高さ100 mで天頂 (34.3, 36.4, 46.0)・真下 (6613, 6427, 6701) nits、独立な積分との差は前と同じ）、`-21`で撮影9枚 result=pass（LUTの生成 106〜145 ms）。`-11`〜`-16`は `expm1` を実装側にも使った版の同じ手順の記録で、撮影の生成時間が最大167 msと150 msを超えたため、区間の透過率から積分を求める形に戻した（同じビルドでも負荷で約40 msばらつき、余裕は小さい）。`-12`はGit Bashが `/m:1` をパスに変換した失敗で、`-13`でPowerShellから実行し直した。
- SS-TAA-P1指摘対応2の検証（2026-09-30）: `.harness/runs/20260930-200402/verify-SS-TAA-P1-10.txt`でconfigure EXIT_CODE=0、`-11`で指定build EXIT_CODE=0（コンパイラ警告なし。第三者のMSB8065・LNK4099は既存）、`-12`で指定CTest 3/3 passed（goldenはTAA無効のまま一致）、`-13`で`TemporalAAJitterTest`の直接実行: 10を描き11を飛ばして12を描くとき、動いて止まる物体はパケットのvelocity 0 px・真値-4.1569 pxで付け替え後-4.1569 px、動いて戻る物体はパケット+4.1569 px・真値0で付け替え後0、等速はパケット-4.1569・真値-8.3138で付け替え後-8.3138、カメラも動く場合は真値(+12.4708, 0)に一致（パケットのままは+8.3138）。フレーム1〜30のうち5・9・10・20を捨てると付け替えありで履歴25回（うち飛んだ直後3回）・食い違い0、付け替えなしの対照では22回・食い違い3。照合できないインスタンスは不完全になる。`-14`で既定（FXAA）の撮影result=pass（平均輝度85.9・87.6・85.7。ログに付け替えの行なし）。`-15`・`-16`で`NORVES_TEMPORAL_AA=1`と`NORVES_DEBUG_DROP_RENDER_FRAME_INTERVAL=3`の撮影（カメラ固定と20°/秒で回す）がresult=passで、全ビューで付け替え63〜65フレーム・不完全0・履歴の再利用が同数・食い違い0・validationの行なし（`startup-capture/SS-TAA-P1-taa-drop3-rebase`・`-orbit-drop3-rebase`）。回る大きな球の模様に流れは見えない。付け替えはTAAを選んだカメラに限った（動きぼけなど他のvelocityの使い手を変えないため。RTGIは飛んだフレームで履歴を捨てる）。
- SS-TAA-P1検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-TAA-P1-11.txt`でconfigure EXIT_CODE=0、`-12`で指定build EXIT_CODE=0（コンパイラ警告なし。第三者依存のMSB8065は既存）、`-13`で指定CTest 3/3 passed（`TemporalAAJitterTest`とIndoor/Outdoor golden。goldenはTAA無効のまま一致）、`-8`で`TemporalAAJitterTest`の直接実行（8フレームのジッタの値、4フレーム目の中央の点のずれ(-0.375, -0.0556)画素、同じジッタを前後のカメラへ掛けたvelocityはジッタ無しと一致し、現在だけに掛けると-0.125画素ずれる対照）、`-14`でTAA無効の撮影result=pass（平均輝度 既定85.8・近接87.6・低角度85.6）、`-15`で`NORVES_TEMPORAL_AA=1`の撮影result=pass（84.6・87.0・85.7、ログにvalidationの行なし）。`startup-capture/SS-TAA-P1-taa/crop-near-roof-fxaa-left-taa-right-x4.png`で、止まった小屋の屋根の縁の階段がTAAで消え、`crop-near-gold-sphere-*`で球の輪郭もなめらかになった。地面の細かな模様のちらつきは平均されて少し柔らかくなる（シャープ化はSS-TAA-P2）。`-16`で仰角45°のTAA無効の撮影を変更前（SS-EMISSIVE-GLOW）と比べ、差は時間で回る大きな球とその影の縁と露出の1階調だけ（`SS-TAA-P1-fxaa-sun45/compare-with-SS-EMISSIVE-GLOW.txt`・`diff-near-x20-*.png`）。`-1`〜`-7`・`-9`・`-10`は履歴の読みを双線形からCatmull-Romへ変える前の同じ手順の記録（`-4`はCTestへの登録漏れで2件だけ走った）。
- SS-TAA-P1の既知の残り（SS-TAA-P2で扱う）: 時間で回る大きな球の模様が横へ流れる現象は、履歴とvelocityの基準フレームの食い違いが原因で、2026-09-30の指摘対応で消えた（`startup-capture/SS-TAA-P1-taa-orbit-drop3/crop-near-big-sphere-compare.png`）。DebugDrawPassはTAAの後にジッタを掛けたカメラで描くため線が揺れうる。TAA併用時、RTGIの静止判定がジッタ入りの逆ViewProjectionをハッシュするためstaticFramesが毎フレーム0に戻る（起動画面はRTGIを使わないため今は影響なし。SS-RTGI-DEFAULTで扱う）。MegaGeometry・スキニングのvelocity、`-OrbitDegreesPerSecond`、シャープ化は`7461c3e`で途中まで入っており、ビルドは通り既定の結果は変えない（検証はSS-TAA-P2）。
- SS-TAA-P1指摘対応の検証（2026-09-30）: `.harness/runs/20260930-200402/verify-SS-TAA-P1-1.txt`でconfigure EXIT_CODE=0、`-2`で指定build EXIT_CODE=0、`-3`で指定CTest 3/3 passed（goldenはTAA無効のまま一致）、`-4`で既定（FXAA）の撮影result=pass（平均輝度85.8・87.8・85.8）。`-9`の`TemporalAAJitterTest`直接実行で、フレーム1〜30のうち5・9・10・20を捨てたとき履歴を25回使い3回を求め直し、10の履歴から12を描くとき静止点・等速の点とも求め直した動きが真値と一致し、パケットのvelocityだけでは半分しか戻らない（対照）。`-5`〜`-7`・`-8`でTAAの撮影（フレーム番号が3の倍数のパケットを捨てる・カメラを20°/秒で回して捨てる・回して捨てない）がすべてresult=passで、ログは捨てたパケット13〜30、履歴の再利用62〜63フレーム（すべて求め直し）、validationの行なし。捨てない撮影でも求め直しが毎フレーム起きる（描画が普段からゲームのフレームを飛ばす）。`crop-near-big-sphere-compare.png`で、`06a26a7`で出ていた球の横流れが消えた。既定の撮影と`06a26a7`のFXAAの撮影の差は時間で回る大きな球とその影の縁だけ（`startup-capture/SS-TAA-P1/diff-*-vs-06a26a7-x20.png`）。
- FIX-AUTOEXPOSURE-READBACK検証（2026-09-27）: `.harness/runs/20260927-041058/verify-FIX-AUTOEXPOSURE-READBACK-1.txt`でGame・CameraViewConstantsTestのbuild EXIT_CODE=0、`-2`で`AutoExposureMathTest` 1/1 passed、`-3`で`R8AcesLutToneMappingVulkanTest`のbuild EXIT_CODE=0、`-4`で`AutoExposureHistogramVulkanTest`・`R8FilmGrainVulkanTest`・`R8AcesLutToneMappingVulkanTest`・`RenderGraphDumpTest` 4/4 passed、`-5`でヒストグラムの検査の直接実行（8区間の期待値と読み戻しが一致、mismatched_bins=0、total=960、VUID_COUNT=0、RESULT=PASS）。`-6`は`Game.exe --imgui`を約45秒動かしたウィンドウの撮影（`.harness/runs/startup-capture/FIX-AUTOEXPOSURE-READBACK/window-imgui.png`）で、「空の太陽」に目標 EV100 15.14・順応後 EV100 15.14が出て、同じ実行のログの`target_ev100=15.139 adapted_ev100=15.139`と一致し、validationの行は0。撮影スクリプト（`CaptureStartupScene.ps1`）の画像はImGuiを含まないので、表示の確認はウィンドウの撮影で行った。ホストの可視性の欠落は、写像がコヒーレントなメモリでは結果に出ないため、検査はバリアを通した読み戻しの一致を確かめるもので、バリアが無いことを検出するものではない。
- SS-AUTOEXPOSURE-P2検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-AUTOEXPOSURE-P2-1.txt`でconfigure EXIT_CODE=0、`-2`でGame・CameraViewConstantsTest・RenderingGoldenImageTestのbuild EXIT_CODE=0（警告なし）、`-3`で`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest`・`AutoExposureMathTest` 3/3 passed（検証シーンは手動のままで既存のgoldenと一致）、`-4`で撮影9枚 result=pass・EXIT_CODE=0。平均輝度（0〜255）は既定 朝87.5・昼85.4・夕78.5、近接84.8・87.5・78.6、低角度83.1・85.2・79.6で、どれも0.31〜0.34（範囲0.12〜0.40）、白飛び率は最大0.000008。`-5`で`AutoExposureMathTest`の直接実行（閉ループの落ち着くまでの時間 昼→夕2.00/1.93 s、夕→昼1.37/1.30 s、目標を越えた量0、反転0）。`-6`は太陽の急変の実機ログ（`startup-capture/SS-AUTOEXPOSURE-P2-sunstep/`、昼45°→夕3°と夕3°→昼45°を起動40秒後に変更）: 昼→夕はEV100 15.26→10.76を順応の時間1.93 s、夕→昼は10.66→15.16を1.30 sで、どちらも目標を越えた量0・反転0。太陽を変えた直後にDebugでは描画が約2.6 s止まり（経過時間は4.5 s・3.7 s）、止まった間の時間は順応に使わない。読み込みの後の順応（初回の測定から約1.2 EV）も同じ記録で1.2 s前後で落ち着く。夕の画面は空のモデル（SS-DAYLIGHT-P1の停止理由）のため強い橙のまま。露出が毎フレーム変わると、RTGIとPTの履歴の署名（プリエクスポージャを含む）が順応の間は変わり続ける（起動画面はRTGIを使わないため今は影響なし。SS-RTGI-DEFAULTで考慮する）。P1の評価の指摘1（ホスト読み取りのバリア）と3（EV100のデバッグ表示）はRHIとGameThreadへの受け渡しが要るため`FIX-AUTOEXPOSURE-READBACK`に分けた。
- SS-AUTOEXPOSURE-P1検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-AUTOEXPOSURE-P1-1.txt`でconfigure EXIT_CODE=0、`-2`でGame・CameraViewConstantsTestのbuild EXIT_CODE=0、`-3`で`AutoExposureMathTest` 1/1 passed、`-4`で撮影9枚 result=pass・EXIT_CODE=0。各Game.logの測定は総数が1280×720=921600画素と一致し、落ち着いた後の目標EV100は既定視点で朝(10°)13.12・昼(45°)15.26・夕(3°)10.66、近接で13.35・14.53・11.32、低角度で13.30・14.51・11.14。`-5`で夕の3視点を追加前（SS-POINT-SHADOW-P2の撮影）と比べ、空の行（y<80）と地面の行（y≥680）は差0、差のある矩形の外は最大2で、画面の露出は変わっていない（差は経過時間で回る大きな球とその周り）。初回の測定（frame=1）は読み込みが落ち着く前の画面で、後の測定より約1 EV高い。デバッグ表示（ImGui）への表示は`Game/`がこのタスクの範囲外なので未対応で、ログと`GetLatestMeasurement()`に出す。
- SS-POINT-SHADOW-P2検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-POINT-SHADOW-P2-1.txt`でconfigure EXIT_CODE=0、`-2`で指定build EXIT_CODE=0、`-3`で指定CTest 3/3 passed（Indoor/Outdoor goldenは変わらず合格。影を落とす点光源が無いため）、`-4`で撮影result=pass（平均輝度 既定53.5・近接72.5・低角度66.3）、3視点のGame.logに`point_shadow_lights=1`。影なしの同条件の撮影（`startup-capture/SS-POINT-SHADOW-P2-noshadow/`）との差は時間で回る大きな球だけ。点光源を一時的に20000 lmにした撮影（`-exp20k/`）でも地面の平均差は0.5/255で、差を20倍にした`lamp-pool-x20-crop.png`で岩の点光源の影が地面に出て縁に縞が無いことを確かめた（変更は戻して再build）。`glslc`で`lighting.frag`・`forward_transparent.frag`・`point_shadow.vert/.frag`・`skinned_point_shadow.vert`のコンパイルを確認した。
- SS-BLOOM-MIPCHAIN検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-BLOOM-MIPCHAIN-1.txt`でconfigure EXIT_CODE=0、`-2`で指定build EXIT_CODE=0、`-3`は旧goldenとの差（Indoor mean_flip 0.0060・7444画素、Outdoor 0.0342・39135画素）で0/2、再承認後の`-6`で2/2 passed、`-4`で撮影result=pass（平均輝度 45°: 既定85.5・近接87.6・低角度85.4、3°: 79.6・78.9・80.1、白飛び率0.000004以下）、`-5`でRenderGraphCompileTest 1/1 passed。広がりは`-7`（GPUと同じ連鎖をnumpyで再現した`bloom_chain_sim.py`）で、半径25 pxの円盤（720 pxの画面）のにじみが縁の値の1%になるのは中心から129 px（高さの17.9%）、エネルギー比1.000。1画素の孤立した強い点はKaris平均でエネルギーの25%に抑えられる。金属の球のハイライトは旧方式の星形のまだら模様が消え、模様のない柔らかいにじみになった（`startup-capture/SS-BLOOM-MIPCHAIN/before-after-metal-highlight-sun45-x3.png`）。太陽は撮影の3視点の画角に入らないため、低角度の視点で方位 -120°にしてGame.exeを直接起動して撮り（`startup-capture/SS-BLOOM-MIPCHAIN-sunview/low-sun3-az-120.png`。撮影スクリプトの`-SunAzimuth`は落ちるためFIX-CAPTURE-SUN-AZIMUTHへ登録）、太陽の周りに丸く柔らかいにじみが出て、格子や輪の模様が無いことを確かめた。発光球は夕の自動露出で画面の平均の約4倍しかなく、0.04の補間ではにじみが見えない（完了条件のうち発光球の項は満たしていない。SS-EMISSIVE-GLOWへ登録）。BloomPass.cpp/.hは行末が混在しており、編集後に変更していない行の行末を元に戻した（numstatの一致を確認）。
- SS-EMISSIVE-GLOW途中（2026-09-27、未完・blocked）: 起動画面の発光球を1800 → 45000 nits（1600 lmを実際の100 W 形電球の半径約3 cmの球から出した輝度）にした。`verify-SS-EMISSIVE-GLOW-3.txt`でGame build EXIT_CODE=0、`-4`で撮影result=pass（平均輝度 45°: 85.5・87.7・85.5、3°: 79.7・78.8・80.5、白飛び率 最大0.00034）。夕は発光球が背景の約70倍になり周りに柔らかいにじみが出る（`startup-capture/SS-EMISSIVE-GLOW/before-after-default-sun3-x3.png`）。昼はプリエクスポージャ後 約1.5でにじみが見えない（`before-after-default-sun45-x3.png`）。`gbuffer.frag`は発光を物理のnitsのままRGBA16Fの`GBuffer_Emissive`へ書くため、赤のチャンネルが溢れない上限は 約57000 nits。120000 nitsでは無限大になって発光が消えた（`-2`の撮影）。昼に要る 約150000 nitsはエンジン側の変更（GBufferへプリエクスポージャ後の値を書く等）が要り、本タスクの範囲外のため`blocked/SS-EMISSIVE-GLOW.md`に選択肢を記録した。
- SS-POINT-SHADOW-P1検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-POINT-SHADOW-P1-1.txt`でconfigure EXIT_CODE=0、`-2`で指定build EXIT_CODE=0、`-3`で指定CTest 4/4 passed（`PointShadowFaceMatricesTest`はVulkanの仕様の面の選び方の表を手で書いた期待値で6面×5点のNDC・near/farの深度・全方向の被覆・選択の順・範囲の球の選別を確かめる。Indoor/Outdoor goldenは変わらず合格）、`-4`で撮影result=pass（平均輝度 既定109.0・近接81.3・低角度77.2）。SS-SHOWCASEの撮影との差は時間で回る大きな球の模様だけで、ほかの画素は一致（`startup-capture/SS-POINT-SHADOW-P1/showcase-vs-p1-*.png`）。起動画面の点光源は影を落とさないので`point_shadow_lights`の記録は出ない。デバッグ表示は一時的に点光源を影ありにして撮り（`-5`・`startup-capture/SS-POINT-SHADOW-P1-debugview/`、変更は戻して再build）、球の光源側の格納距離0.33〜0.39（範囲10 m、幾何の約3.3 mと合う）、光源と反対側の球の影が赤、検証レイヤーのエラー0件。明暗境界付近の縞と、岩・小屋（MegaGeometry）がキューブに描かれないこと（CSMと同じ）はP2の法線方向のずらし・キャスターの扱いで見る。
- SS-SHOWCASE検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-SHOWCASE-1.txt`でGame build EXIT_CODE=0、`-2`で撮影result=pass・EXIT_CODE=0（平均輝度 既定109.0・近接80.8・低角度77.2、白飛び率0.000014以下）。3視点のGame.logに`Cottage model loaded and added to World`と`showcase spheres created count=10`。既定のPNGで見本の球10個・小屋・球・岩が重ならずに見え、粗さの順にハイライトと映り込みがぼける。金属の球の上半分に空、低い粗さの球にSSRで隣の球と地面が映る。ただし空のモデルが暗く地平線より下が黒いため（SS-DAYLIGHT-P1の停止理由）、金属の球の下半分は暗い。起動から撮影の要求までは小屋なし28.8/32.5/33.8 s、小屋あり36.0/35.8/36.7 s（視点ごと）で、増分は約3〜7 sと停止条件の30 sより小さいため、基本色・法線の4Kは縮小していない。岩（MegaGeometry）は接地させたがCSMへ描かれないため投影影は無い（FIX-CSM-MEGA-CASTER-BOUNDSの範囲）。Rendering3DTestRoutine.cppは行末が混在しており、変更していない行の行末を元に戻した（`git diff --numstat`と`--ignore-cr-at-eol`が一致）。RenderMaterialStore.cppは日本語の説明文を足したためBOMを付けた（コードページ932での誤読を防ぐ）。
- SS-CSM-DISTANCE検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-CSM-DISTANCE-1.txt`でconfigure EXIT_CODE=0、`-2`で指定build EXIT_CODE=0、`-3`は旧Outdoor goldenとの差（mean_flip 0.000544、86画素、球の影の輪郭だけ。`outdoor-golden-diff-SS-CSM-DISTANCE.png`）で3/4、再承認後の`-4`で4/4 passed、`-5`でCSMテストの直接実行（`csm_startup cascade0_texel_m=0.0128666`）、`-6`で撮影result=pass（平均輝度 既定56.3・近接62.0・低角度49.9）。3視点のGame.logに`csm_texel_m=0.0129,...`。前後比較（`startup-capture/SS-CSM-DISTANCE/before-after-*.png`、前はSS-DAYLIGHT-P1の撮影）で球の影の縁の階段と滲みが消えた（訂正: 岩はShadowMapPassが描かないMegaGeometry経路のため投影影が無く、「岩の接地部の影もくっきり」は誤り。評価者の指摘を参照）。最大距離の端のフェードは起動画面に80 mより奥の受け側が無いため撮影では見えず、シェーダーのコンパイルと分割のテストだけで確かめた。行末が混在するShadowMapPass.cpp/.h・forward_transparent.fragは編集後に元の行末へ戻した（numstatの一致を確認）。
- SS-DAYLIGHT-P2検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-DAYLIGHT-P2-2.txt`でGame build EXIT_CODE=0、`-3`で撮影9枚 result=pass・EXIT_CODE=0（平均輝度 既定 10°42.8・45°62.2・3°17.9、白飛び率0）。`powershell -File`は`-SunElevations 10,45,3`を1つの文字列で渡し、`[double[]]`だと`10453`に化けるため、文字列で受けて`,`で分ける。フォグの見比べは`startup-capture/SS-DAYLIGHT-P2-fog/`（密度0と0.06・0.15）。
- SS-DAYLIGHT-P1検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-DAYLIGHT-P1-1.txt`でGame build EXIT_CODE=0、`-2`で撮影EXIT_CODE=0（平均輝度 既定56.8・近接63.8・低角度49.9、白飛び率0、黒つぶれ率 既定0.443・近接0.228・低角度0.168）。既定のPNGで球の影の中の地面は輝度0、日向は44〜157（8bit）。Rendering3DTestRoutine.cppは行末が混在しており、編集後に元の行末へ戻した（行末だけの変更0行を確認。`git diff --numstat`と`--ignore-cr-at-eol`の1行の差は差分の対応付けの違い）。
- SS-CAPTURE検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-CAPTURE-1.txt`でGame build EXIT_CODE=0、`verify-SS-CAPTURE-2.txt`で撮影スクリプトEXIT_CODE=0。平均輝度は既定143.7・近接167.4・低角度182.4、白飛び率は0〜0.000003、黒つぶれ率は0.00016〜0.0069（1280×720）。3枚のPNGを開き、起動画面が写りImGui・ボードが無いことを確かめた。Game.logはリポジトリのルートに書かれるため、スクリプトは視点ごとに消してから起動し、終了後に出力先へ写す。撮影1回は読み込み待ちを含めて約25秒。
- 起動画面の描画改善のM1（2026-09-27）: 調査で、R2〜R8の機能（物理空・霧・RT影・DDGI・RTGI・被写界深度・動きぼけ）が起動画面ではすべて無効で、起動画面は実質R1の見た目のままと分かった。影が見えないのは静的HDRの環境光が地面の照度の約87%を占め（方向光1 lux）、影で約13%しか暗くならないため（`f3d4abb`でIBLをE/πにした後、比が約9倍悪化）。CSMはfar=1000のためカスケード0の1テクセルが約0.157 m。POMの凹凸の反転と輪郭の歪みは`8c80695`（余接フレームの退化判定）が引き金。ブルームは1回の16タップで広がり約10 px。点光源の影は未実装。ユーザー決定: 物理的な昼の屋外（物理空＋空の太陽）、点光源のキューブシャドウ、ポストは基本セット＋演出系、展示物の追加。開始儀式: `git log --oneline -10`はHEAD `163ffe5`。ビルドとテストはPCの使用の許可を待つため未実行で、最初の反復の開始儀式で行う。
- SS-POM検証（2026-09-27）: `.harness/runs/20260927-041058/verify-SS-POM-1.txt`でconfigure EXIT_CODE=0、`-2`でGame・RenderingGoldenImageTestのbuild EXIT_CODE=0、`-3`でIndoor/Outdoor golden 2/2 passed、`-4`で撮影EXIT_CODE=0（平均輝度 既定143.8・近接167.4・低角度182.4）。近接視点で石が盛り上がり目地がへこみ、輪郭の最外周で模様が流れないことを前後の拡大（`startup-capture/SS-POM/near-edge-before-after.png`）で確かめた。石の視点側の側面に引き伸ばされたテクセルが見えるのはPOMの側面表示で、凹凸の向きと合う。起動画面の点光源は球から遠く照明はIBLが支配的なため、法線マップの緑の向きは高さマップの勾配との相関で判定した（`-5`: nor_gl の緑は-0.933、nor_dx は+0.933）。
- R8のM1（2026-09-26）: S8はベイクLUT（OCIOでACES 2.0 SDR 100 nit Rec.709をsRGB表示向けに65³＋log2 shaperへ焼き、display-linearで持つ。新しいトーンマップの選択肢で既定は不変）。連番は1280×720・1024 spp、PTの1フレームは前後のカメラ・変換とシャッター区間（フレーム長1/24 s、シャッター1/48 s）を固定して1 spp×1024 dispatch。DoF・動きぼけの閾値はf値／シャッターを±20%変えたPTを物差しにする規則。フィルムグレインは既定オフで入れる。連番の検査はC++の検証exe（隣接フレームのFLIPが中央値の3倍を超えたらポッピング）とScripts、CTestは8フレームの連番。
- R8-P4再開（2026-09-26、run-id 20260926-130732）: 取得済みダンプの`--compare-dumps`（1回4秒）でgatherの変種を比べた。深度の層（4/8/16層の前から順の合成）は傾いた面を層に割って平均0.031〜0.046へ悪化、最も手前のCoCを基準にした前景/背景の分割（幅0.25画素・奥の広がりの余裕0.35画素）が最良。最終の比較ログは`.harness/runs/20260926-130732/diag-R8-P4-compare-final.txt`、指定buildは`verify-R8-P4-1.txt`でEXIT_CODE=0、指定CTestは`verify-R8-P4-2.txt`（結果は`blocked/R8-P4.md`）。
- R8-P3-FIX検証（2026-09-26）: `.harness/runs/20260926-130732/verify-R8-P3-FIX-3.txt`で指定build EXIT_CODE=0（`-1`はGit Bashが`/m:1`をパスに変えたため無効、`-2`は`RenderingCoordinator.obj`の破損によるLNK1163で、objを消して再build）。`-4`で新規検査の出力（`reordered_instances second_dispatch_samples=2 fixed_order_byte_identical=true all_bodies_visible=true`）、`-6`でPT系3 targetの再build EXIT_CODE=0、`-7`で指定CTest 4/4 passedを読戻し確認した。SequenceFrameを使うテストはR8だけで、R6/R7のPT参照比較は並べ替えの経路を通らない。 独立評価はPASS。非blockingの残課題として、同じ鍵（ObjectIdが0の描画が同じメッシュ・部分範囲で複数あるなど）の間の出現順の番号は描画順に依存するため、そうした物体同士が入れ替わると累積が捨てられうる。
- ループの反復（2026-09-26、R8）: 反復はヘッドレスで動き、40分の壁時計で止められる。バックグラウンドのコマンドの完了を待つためにターンを終えると、プロセスが終わって待っているテストも止まり、DONEが記録されない。長いctestはフォアグラウンドで1コマンド600秒以内に収まるよう対象を分けて順に実行し、GPUのテストは同時に2本走らせない。止められた反復のテストのプロセスが残ることがあるので、反復の最初に残ったctest・テストexeがないか確かめる。
- R8-P4検証（2026-09-26）: `verify-R8-P4-1.txt`で指定build EXIT_CODE=0、`verify-R8-P4-2.txt`で指定CTest 2/3 passed（EXIT_CODE=8）。比較テストの失敗は3回の取得で同値（平均FLIP 0.0274903、区画最大0.126995）、±40%の健全性確認はPASS、承認済みgoldenのIndoor・OutdoorはPassed。差は、画像下端の未命中の行へ落ちる床の前ボケ（区画最大の原因）、奥の面の深度の境界での隠れた面、天井の光源の縁の漏れの3種類。背景CoCの打ち切りに許容幅を入れる試行は平均を悪化させたため採用していない。前の反復のclaudeプロセスが取り残されて検証のctestを並行して走らせていたため停止し、その結果は採用していない。
- R8-P3検証（2026-09-26）: `.harness/runs/20260926-011446/verify-R8-P3-10.txt`で指定build EXIT_CODE=0、`verify-R8-P3-11.txt`で指定CTest 5/5 passed、`-12`でPT系13 targetの再build EXIT_CODE=0、`-13`〜`-17`でPT系・R4/R6/R7参照比較の16件すべてpassed（R7屋外参照は単独702 s）、`-18`で新規テストの直接実行値を読戻し確認した。独立評価は1周目（RenderThread側ラッチのパケット欠落・並び替え）、2周目（インスタンシング描画のバッチ先頭ObjectId）でNEEDS_WORK。2周の上限に達したため、2周目の指摘への対応（MeshProxy照合による物体IDの解決、実際のMeshBatcher経路の並び替え検査、負の対照`negative-R8-P3-instanced.txt`でEXIT_CODE=1）はR8-P4の評価対象へ含めて確認する。前の反復の取り残されたctest（修正前のbinary）は停止し、結果は採用していない。
- R8-P1検証（2026-09-26）: `.harness/runs/20260926-011446/verify-R8-P1-1.txt`でRESULT=PASS・EXIT_CODE=0を読戻し確認した。純粋なlog2 shaper（2^-14〜2^8など）はどの範囲でもチャート最大3.1/255以上で不合格だったため、0を格子端に置くオフセット付きlog2へ変えて候補を測り、c=2^-8・上限2^8を採用した（表は記録文書）。無作為な色20万点では高彩度の色域境界で最大5.8/255（67点が2/255超、合否外の参考値）で、R8-P2のGPU一致はチャートで確かめる。この反復はPythonと生成物だけの変更のため、エンジンのbuildは開始儀式（同日、HEAD `a42c8b1`）の結果を使い再実行していない。
- R8ループ開始儀式（2026-09-26、HEAD `a42c8b1`）: Game・ToneMappingParamsLayoutTestのDebug build BUILD_EXIT=0、CPU契約3件（ToneMappingParamsLayout・RenderingDDGILightingContract・PathTracingCamera）3/3 passed。直前の全target build後のRenderingValidationラベルは57件中0件失敗（`.harness/runs/20260925-r4-reopen/`）。
- R7-P3D評価（2026-09-24）: 1周目で1frameに束ねた試料のカメラ標本の偏り、影を落とさない物体だけのシーンでRTGIが既存の間接光を置き換える経路、PT機能判定のshaderInt64漏れが指摘され、2周目でPASS。R7-P3Bの`8c80695`とR7-P3Cの`9e0b35e`も追加指摘なし。負の対照は`.harness/runs/20260924-r7-p3d/negative-*.txt`。
- R7-P3C評価（2026-09-24）: 1周目でGGXの分母（粗さ0）、裏側の視線の標本化、65504の切り詰め、影レイの終端、2周目で面光源の直前（2mm以内）の遮蔽物の見逃しが指摘された。2周の上限に達したため、最後の修正`9e0b35e`はR7-P3Dの評価対象へ含めて確認する。負の対照は`.harness/runs/20260924-r7-p3c/negative-p3c-*.txt`。
- R7-P3B評価（2026-09-24）: 1周目で接空間基底の正規化・面裏散乱・texture解決変化の履歴破棄、2周目でラスタの退化判定（画面微分の絶対値）との分岐差が指摘された。2周の上限に達したため、最後の修正`8c80695`はR7-P3Cの評価対象へ含めて確認する。太陽標本の幾何面判定のテスト不足（non-blocking）はP3Cの太陽MISで扱う。ログは`.harness/runs/20260924-r7-p3b/`。
- R6評価（2026-09-24、2周目）: 距離基準のずれと停止後の残留検証不足でNEEDS_WORK。2周の上限に達したため、対応（`ab86d83`・`c4a9d93`・`c5765a0`）はR6-P5-REFの評価対象へ含めて確認する。SkinnedRenderPathContractTestの停止は既存問題としてTEST-SKINNEDへ登録した。
- R7-P3A評価（2026-09-24）: 1周目で配列を含まないlayoutのpool容量変化とデバイス上限未検査、2周目でmaxPerStageResourcesの計数規則（単独sampler・加速構造を除き、fragmentのcolor attachmentを加える）が指摘された。2周の上限に達したため、最後の修正`48fa18e`はR7-P3Bの評価対象へ含めて確認する。検証ログは`.harness/runs/20260923-resume/verify-R7-P3A-*-3.txt`。
- R6完了判定の評価（2026-09-23）: 静止参照が5値比較、ライト追従率の分母、RTGIのサンプル固定、履歴棄却の検証不足、再オープン規則の欠落でNEEDS_WORK。R6-P5を再開し、参照比較はR6-P5-REF（R7-P3D後）へ分けた。
- R7-O3停止監査（2026-09-23）: .harness/runs/20260923-095848/verify-R7-O3-1.txt〜-5.txtを読戻し、Game Debug build exit 0、指定屋外CTest 1/1、関連R7 CTest 6/6、P3/P4専用CTest登録0件を確認した。3時刻のPT空・太陽数値はあるが、霧込みPT/raster同一シーンのFLIP値はない。Docs/RenderingValidation/R7OutdoorAcceptance.mdとblocked/R7-O3.mdに停止根拠を記録し、R7完了trailerは付けない。
- R7-O2検証（2026-09-23）: .harness/runs/20260923-095848/verify-R7-O2-6.txtは指定Debug build EXIT_CODE=0、verify-R7-O2-7.txtはGPU CTest 2/2 passed。verify-R7-O2-8.txtでは密度0.15/0.3と高さ変更の透過率0.740818/0.548812/0.680309、方向光散乱の実測0.159891/0.0799457/0.0399729と解析期待値0.159801/0.0799007/0.0399503、霧付き表面差0.966716・空miss差0を読戻し確認した。ラスタとの遮蔽方式・二次レイ差はDocs/RenderingValidation/R7FogTransport.mdに記録した。
- R7-O1検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-O1-3.txt`は指定Debug build EXIT_CODE=0、`verify-R7-O1-4.txt`は屋外CTest 1/1 passed。`verify-R7-O1-5.txt`で朝/昼/夕の空miss合計0.0455075/0.0734877/0.0773978、太陽面0.718427/0.351861/0.672279と解析値0.720148/0.354884/0.674271を確認した。全画素有限、空無効・LUT欠落と同一設定の復帰で履歴1試料を確認した。`verify-R7-O1-6.txt`と`-7.txt`の既存PT・R2空モデルbuild/CTestはEXIT_CODE=0、2/2 passed。
- R7-P7受入れ監査（2026-09-23）: .harness/runs/20260923-095848/verify-R7-P7-1.txt〜-8.txtを読戻し、関連Debug build EXIT_CODE=0、CTest 7/7 passed、R1ラスタ数値40静的行/68数値行/120 capture、EXR 3枚2,099画素のfinite-scanを確認した。P3/P4専用CTestは登録0件で、同一条件self PT対R4/R6の比較値はない。Docs/RenderingValidation/R7CoreAcceptance.mdとblocked/R7-P7.mdに再検証単位を記録し、R7 trailerは付けない。
- R7-P6検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-P6-10.txt`で指定build EXIT_CODE=0、`verify-R7-P6-11.txt`で指定CTest 1/1 passed・EXIT_CODE=0、`verify-R7-P6-12.txt`で固定条件2出力のbyte一致（SHA256 `9333237a36a6a8ee732fe55557a430706c5a831b71e7d0f8bef3edb80291d768`）、BGR float32/ZIP、32×32・3×17 window、有限値とNaN/Inf拒否を読戻し確認した。`verify-R7-P6-9.txt`のOpenCV独立読取と、`verify-R7-P6-13.txt`・`-14.txt`の既存カメラGPUテストもEXIT_CODE=0。MSVCのC11 atomicsは専用compile optionで有効化し、TinyEXRのJPH SIMD builtinのみ移植した。
- R7-P5検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-P5-10.txt`で指定build EXIT_CODE=0、`verify-R7-P5-11.txt`で指定CTest 2/2 passed、`verify-R7-P5-12.txt`で可変DeltaTimeの静止32試料累積と静止/移動golden・画像差0.0576274を読戻し確認した。既存RT snapshot/PT回帰は`verify-R7-P5-3.txt`・`-4.txt`で2/2 passed。線形3×3が同じ並進だけを補間し、それ以外はcurrentを使う。実際のアニメーションでpacketのcamera/geometryが変わると既存の履歴規則で累積はリセットされる。独立評価は2周目PASS。
- R7-P4検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-P4-1.txt`は未登録build targetでEXIT_CODE=1、`verify-R7-P4-2.txt`はCTest未登録でEXIT_CODE=8。既存PathTracingVulkanTestのDebug buildとGPU CTestは成功（1/1）。停止理由は`blocked/R7-P4.md`。
- R7-P3検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-P3-1.txt`はGame成功後に未登録のPathTracingLightingVulkanTest targetでEXIT_CODE=1、`verify-R7-P3-2.txt`は指定3テストが未登録でEXIT_CODE=8。開始時のGame/PathTracingVulkanTest buildと既存PT GPU CTestは成功。停止理由と選択肢は`blocked/R7-P3.md`。
- R7-P2検証（2026-09-23）: `.harness/runs/20260923-095848/verify-R7-P2-8.txt`はGame/PathTracingVulkanTestのDebug buildでEXIT_CODE=0、`verify-R7-P2-9.txt`は指定CTest 1/1 passed・EXIT_CODE=0。`verify-R7-P2-10.txt`の実GPU出力は16 sampleの変化量が0.0130208から0.00131565へ低下し、交互frame slot、packet欠番時の履歴維持、camera/scene/material/geometry/light reset、明示選択を確認した。
- R6-P5再開時（2026-09-23）: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1` 成功、`ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R '^RTGIDiffuseIndirectVulkanTest$'` は1/1 passed。ログは`.harness/runs/20260923-r6-p5-resume/`。
- R6-P5最終検証（2026-09-23）: `verify-R6-P5-1.txt`のGame buildはEXIT_CODE=0、`verify-R6-P5-3.txt`の受入れテストbuildはEXIT_CODE=0、`verify-R6-P5-4.txt`は1/1 passed。HDR出力は8-frame warmup、static golden、RT無効/R4 fallback、カメラ/物体移動、履歴棄却、移動後停止2サンプル、ライト4 rendered frames以内の追従をPASSとして記録した。指定の全体`RenderingValidation`は`verify-R6-P5-2.txt`で44件中34 passed・8 skipped・10 failed、EXIT_CODE=8。10失敗（RHIGPUTimestamp、DDGIProbeRadiance、RenderingDDGI 2件、RHIImageLayout 4件、RenderingGolden 2件）は着手時baselineと同じで、R6-P5追加テストは全体実行でもpassed。
- R6-GATE-DDGI-ORACLE（2026-09-23）: `DDGIProbeRadianceVulkanTest`が1 pass内で非遮蔽→遮蔽を連続実行し、R4-P4のprevious-irradiance bounceを後者の直接照明期待値へ混入させていた。遮蔽ケースを独立passに分け、Debug build exit 0、CTest 1/1 passed。`.harness/runs/20260923-r6-gate-ddgi/verify-ctest-LastTest.log`でpoint/spot遮蔽期待値と実測値が両方`4.92249,2.50945,1.32604`と一致することを確認した。全体gateはこの修正後まだ未実行。
- R6-GATE-OUTDOOR（2026-09-23）: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`はexit 0、`ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R '^RenderingGoldenOutdoorVulkanTest$'`は平均FLIP `0.002326954`・最大FLIP `0.529430032`・459画素差で再失敗。承認済みbaseline SHA `05D202…D895B6`は不変。R2-P5で単一shadow matrixから4-cascade CSMへ移行し、R2受入れ検証はR1 Outdoor goldenを実行していない。現在のCSM出力は2回のstaging captureでSHA `9933B5…F3B954`に一致し、cropでは差分が主に影輪郭へ局在する。R1画像を置換せず、このゲートをblockedとしてR7コアへ進む。

- R4-P3Aの非blocking残課題: GPUテストはVUIDを自動でテスト失敗へ反映せず、validation layer未導入時はskipする。今回の受け入れでは実GPU readbackと実行ログを確認した。
- R4-P7旧blocked run（2026-09-22、run-id 20260922-045823）は履歴として保持する。後続run `20260922-r4-p7-final3`でHDR scene-color、red/green indirect ROI、disabled A/B、9/9 CTest、atlas履歴更新、失敗時の公開状態クリアを再検証した。独立評価2周目の帳簿・行末・成果物追跡に関する所見は受入れ前に修正し、現行成果物へ反映した。
- R6-a初回検証: 関連CPU/RenderGraph/FramePacket CTest 17/17、velocity GPU CTest 3/3、Game build exit 0とGame.logの既定シーン構成/`rendered=120`を確認したが、最終ソースとの時系列対応と履歴/解析値の検証不足が指摘されたため、受入れを確定扱いにしない。
- R6-a最終再検証: 最終ソースで関連CTest 17/17、velocity GPU CTest 6/6、カメラのみ/物体のみ/併用の解析値 readback、物体のみの背景ゼロ、移動後停止ゼロ、Game.logの既定シーン構成/`rendered=120`を確認した。Slang SDK未導入warning/errorは既存decoder無効化フォールバックであり、Vulkan validation errorは0件だった。
- R6-M1再開ゲート(2026-09-22): `cmake --build build --config Debug --target Game RHIRayTracingPipelineVulkanTest -- /m:1`はexit 0。`ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIRayTracingPipelineVulkanTest|RenderingVelocityCameraVulkanTest)$"`は2/2 passed。MSBuildの既存libwebsockets生成物warningは継続するが、対象buildとCTestの終了コードは0。
- R6-P1-FIX検証(2026-09-22): 指定Game buildはEXIT_CODE=0、`RenderingDDGILightingContractTest`・`RayTracingSceneSnapshotTest`・`RenderingVelocityCameraVulkanTest`は3/3 passed。履歴revision差の保持と構成revision不一致のfallbackを契約テストで読戻し確認した。MSBuildのthird-party PDB/libwebsockets生成物warningは継続するが、対象ゲートの終了コードは0。
- R6-P2-FIX検証(2026-09-23): `verify-R6-P2-FIX-5.txt`はGameと`RTGIDiffuseIndirectVulkanTest`のDebug build EXIT_CODE=0、`verify-R6-P2-FIX-6.txt`は指定GPU 3件が100% passed、`verify-R6-P2-FIX-7.txt`は`DiffuseIndirect.comp`のVulkan 1.2 compile EXIT_CODE=0。GPUテストのreadback出力はfinite、hit positive、miss zero、RTGI公開、disabled/incomplete fallback一致を示す。MSBuildの既存libwebsockets生成物warningは継続するが、対象ゲートの終了コードは0。
- R6-P3検証(2026-09-23): 指定Game build EXIT_CODE=0、camera/object velocityとRenderGraphCompileTestは3/3 passed。RTGI履歴slotのread/write barrierとrendered FrameNumber連続性を修正した。MSBuildのlibwebsockets生成物warningは継続するが、対象ゲートの終了コードは0。
- R6-P6開始検証(2026-09-23、run-id `20260923-050747`): 現HEADのGame buildはEXIT_CODE=0、既定シーン120フレームのスモークもEXIT_CODE=0だった。全体`RenderingValidation`の再実行は34 passed・8 skipped・2 failedで終了し、`DDGIProbeRadianceVulkanTest`の点/スポット遮蔽radiance不一致と`RenderingGoldenOutdoorVulkanTest`のgolden mismatchが残った。P5機能gate未完のためR6-P6を完了扱いにしない。

- R5-P8開始ゲート: Debug buildはEXIT_CODE=0、対象CTestは1/1 passed。ログは.harness/runs/20260920-174410/verify-R5-P8-1.txtとverify-R5-P8-2.txt。これは既存P7テストの開始時状態で、P8の完了証拠ではない。
- R5-P8再開儀式(2026-09-21): `git log --oneline -10`を確認し、再開時Debug build exit 0・専用CTest 1/1 passed。ログは`.harness/runs/20260921-r5-p8-resume/verify-R5-P8-resume-baseline-build.txt`と`verify-R5-P8-resume-baseline-ctest.txt`。
- R5-P8再開ゲートと完了検証: 2026-09-21にDebug build exit 0、専用CTest 1/1 passed。`LastTest.log`でhit=1、miss=0、軸別拒否2件を確認した。RTX 4080ではX軸の最大幅が総呼出し上限を超え、uint32 dispatch寸法ではX単独の上限超過を構成できないため、Y/Zを個別検査した。Validation Layerは無効で、buildにはNOMINMAX C4005とthird-party PDB LNK4099警告がある。これらをValidation Layer検証済み・警告なしとは扱わない。
- R5-P8のdescriptor型回帰は共通ResourceBindType変換とRT descriptor layoutを検査する。graphics pipeline全体の生成経路はこのGPUテストでは直接実行していない。
- R2完了: 初回評価で検出されたCPU/shader補間差、行末差分、実GPU経路の明示不足を `ca9204d` で修正し、受入れ記録とRoadmapへ反映した。R1 baseline、candidate/approvalは変更・再利用していない。
- P6a completion report: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-report.md`。forward-fixの最終証拠は `.harness/runs/20260908-084154/p6a-forward-fix-gpu/` と `p6a-forward-fix-review/claude-final.json` に保存する。旧P6a reportは同ディレクトリのアーカイブへ保持する。
- 独立再検証: `recheck-R1-P6A-4-1.txt`〜`-11.txt`。集計は `p6a-independent-recheck-audit-1730.json`。統合接続評価は `p6a-r1-integrated-review/receipt.json` / `stdout.json`。
- 統合評価はcapture状態遷移の全寿命と照明単位の全項を追跡していない。blocking 0として受理し、確認範囲を拡大解釈しない。bundle評価本文のS79→319はrawと不一致で、実値S80→320を採用する。
- 初回bundle評価の同時起動、7d1cff3→573a9e4のamend、573a9e4の運転文書を含むEOL差は履歴を保持する。現在のsource20はEOL一致。実施していないSol評価や古いfixed subject/direct parent/cached20をPASSと記録しない。
- 2026-08-15の承認: P6a v4 SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F、P6b v3 SHA=37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3。`Docs/Plans/RenderingR1P6aPhaseDeclaration.md`に承認原文と元sessionの参照を保持する。P6b baseline/thresholdの実物承認原文は`Docs/RenderingValidation/R1Acceptance.md`とcontrol eventへ記録した。
- P4/P5受入の詳細、途中診断、非blocking事項は `.harness/runs/20260908-084154/progress-before-p6a-acceptance.md` および `Docs/Plans/RenderingR1P6aParentNotes20260908.md` に保存した。途中の「未完」は当時の状態である。
- R2の設計判断: S2はHillaire 2020系の事前計算LUTを採用し、太陽高度・方位角をFramePacketの空スナップショットへ持たせる。空由来IBLは既存の放射輝度/拡散照明/プリフィルタ生成へ接続し、R1の静的HDRは空無効時だけフォールバックにする。性能回帰はR2本体の完了条件から分離する。\r
- R3の設計判断: 指数高さ密度の視線透過率は解析積分、方向光単一散乱は固定24ステップとし、既存CSMとR2 SkyAtmosphere radianceへ接続する。passはLighting後・Forward透明前で、透明物自体へのfog適用とGPU性能計測はR3完了条件外。
- R3-P1の検証ログは `.harness/runs/20260919-204219/verify-R3-P1-1.txt` と `verify-R3-P1-2.txt` に保存し、両ログでビルド終了コード0・CTest 1/1 passedを確認した。
- R2-P2評価: 1周目のblocking指摘（sky_atmosphere.fragの`#version`欠落、低解像度αマスク）を`53a58dc`で修正し、2周目の独立評価はPASS。空LUTの毎フレーム再生成、未使用sky shaderのUV/α契約、太陽ディスクの透過率適用、GameThread側の空有効化はP3以降の非blocking課題として扱う。
- R2-P3評価: 1周目のblocking指摘（LightingParamsLayoutTestのbinding 12/13 selector期待値が古い）を`a235411`で修正し、4対象CTestと2周目の独立評価はPASS。動的IBLの同期CPU生成と空源の二重評価は、連続する太陽高度アニメーションの性能課題としてR2本体の完了条件外に置く。
- tracked v1 scene、R0基準2文書、P6b開始時のsource-start hashesは証跡へ固定した。P6bの公開tracked範囲はPNG2枚、VisualThresholds.tsv、R1Acceptance.mdで、forward-fixのテスト契約修正と受入れ記録を別コミットに分離した。プッシュは行わない。
- R2-P7のgolden生成・比較、閾値自己検査、float readback、CSM境界、サブテクセル変化率はR2専用の受入れ経路で固定した。R1 `Indoor.png` / `Outdoor.png` はスクリプト自己検査でもハッシュ不変を確認した。
- R3-P2の最終検証ログ: `.harness/runs/20260919-204219/verify-R3-P2-4.txt` (Debug build, EXIT_CODE=0)、`verify-R3-P2-5.txt` (CTest 2/2 passed)、`verify-R3-P2-6.txt` (glslc, EXIT_CODE=0)。
- R3-P2では失敗経路にも空Load/StoreパスまたはRenderTarget→ShaderResourceバリアを積み、SceneColorの最終状態を維持する。独立評価はPASS。
- `855de28`の作業途中保存には、着手時点で未コミットだった`NEXT_FINDINGS.md`の52行削除も含まれる。R3-P2実装とTASKS更新は`b6865ea`。
- R3-P3のcapture状態遷移では段階更新直後と次のPreRenderで状態を適用する。重複適用は冪等で、フォローアップcaptureの段階とfixture状態の同期を優先した。P3評価の散乱量上限なし所見は、R3-P4で密度sweep/CSM A/Bの上限・非飽和閾値を追加して解消した。
- R3-P4の指定検証ログ: `.harness/runs/20260920-124224/verify-R3-P4-1.txt`〜`verify-R3-P4-7.txt`。全7ログの終了コード0、GPU受入れsentinel、CTest 7/7を読戻し確認した。R1/R2基準8ファイルのSHA256はHEADと一致する。
- R4 S4はDDGIを選定し、RT pipelineでのR5影実証とray queryでのR4 probe更新を計画した。R5完了後にR4へ進む。
- R5開始儀式(2026-09-20): `git log --oneline -10`確認、Game Debug build exit 0、RHI/GPU smoke CTest 4/4 passed。`vulkaninfo`でRTX 4080のBDA/AS/ray query/RT pipeline featureを確認した。Core更新後は静的リンク済みGPU test target自体も再buildしてから実行する。
- R5-P1検証ログ: `verify-R5-P1-1.txt`はALL_BUILD exit 1（PhysicsArchitectureContractTestとPhysicsFixedStepPipelineTestの8件のFramePacket/SceneProxy静的assert）、`verify-R5-P1-2.txt`はBDA CTest 1/1、`verify-R5-P1-3.txt`は全CTest 217 passed/7 skipped/3 failed（exit 8）、`verify-R5-P1-4.txt`はGameとBDAテスト対象build exit 0。全CTestの失敗はRenderingGoldenOutdoorVulkanTest（Slang SDK未設定ログの後にgolden差分）、RenderGraphCompileTest（binding 6 shadow map assertion）、SkinnedRenderPathContractTest（pending件数assert）。BDA・readback・RHI image layout・HDR indoor/outdoorの対象テストは成功した。
- R5-P2検証ログ: `.harness/runs/20260920-151245/verify-R5-P2-6.txt` (Gameと契約テストのDebug build exit 0)、`verify-R5-P2-7.txt` (能力契約CTest 1/1)、`verify-R5-P2-8.txt` (RHI image layoutとIndoor HDR描画CTest 2/2)。Vulkan実デバイス初期化と能力依存関係を確認した。
- R5-P3検証ログ: .harness/runs/20260920-151245/verify-R5-P3-1.txt（Debug build exit 0）とverify-R5-P3-2.txt（CTest 1/1 passed）。
- R5-P7ではVulkanDescriptorSet.cppに6つのRT stage flagsとAllRayTracingの写像を追加し、RT-only binding付きpipelineを専用GPUテストで確認した。
- R5-P4検証ログ: `.harness/runs/20260920-151245/verify-R5-P4-6.txt` (Debug build, EXIT_CODE=0)、`verify-R5-P4-7.txt` (CTest 1/1 passed)。
- R5-P5再開検証: `20260922-r5-p5-resume`でbuild exit 0、専用CTest 1/1 passed、直接実行 exit 0を確認した。直接ログはBLAS/TLASのframe別hit切替、未送信Build件数破棄、instance数不一致Update拒否、fence後解放を記録する。独立評価は`evaluator-R5-P5-round1.txt`でPASS。旧保留記録は履歴として保持する。
- R5-P6の独立評価でVulkanTexture::Updateの同期失敗時にstaging buffer/memoryの解放漏れが見つかった。独立フォローアップR5-P13へ登録した。
- `VK_LAYER_VALIDATE_SYNC=1`をRenderingValidation全体へ設定した診断では、`RHIImageLayoutVulkanNoCasterSceneTest`と`RHIImageLayoutVulkanDrawThenNoCasterSceneTest`がswapchain画像のWRITE_AFTER_READを報告した。通常validationでは両テストが成功し、RT影専用テストも同期validation下で成功する。このswapchain経路は独立した追跡事項としてNEXT_FINDINGS.mdへ記録する。
- R5-P10の影captureは`shadow_luma=0`/`lit_luma=255`の高コントラスト画像であるため、現在のA/Bは影領域の位置と出力一致を検証し、半影や階調誤差は検出しない。
- R5-P12開始時の全CTest(2026-09-21): 235件中222 passed、7 skipped、6 failed/Not Run、exit 1。保存ログは`.harness/runs/20260921-125655/startup-ctest-LastTest.log`と`startup-ctest-LastTestsFailed.log`。R5のBDA、機能検出、RHI契約、RT影、動的TLAS/fallbackテストは成功。失敗はOutdoor golden差分、RenderGraphCompileTestのbinding 6 assertion、SkinnedRenderPathContractTestのpending件数assert、ScriptRuntimeSafetyTestのBAD_COMMAND、Bridgeの2 test executable不在。
- R4-P1検証ログ: `.harness/runs/20260921-164825/verify-R4-P1-7.txt`（Debug build、EXIT_CODE=0）と`verify-R4-P1-8.txt`（CTest 1/1 passed）。両ログを開いて終了コードと結果を確認した。
- R4-P2検証ログ: `.harness/runs/20260921-164825/verify-R4-P2-4.txt`（Debug build、EXIT_CODE=0。third-party PDB LNK4099警告あり）と`verify-R4-P2-5.txt`（CTest 1/1 passed）。保存ログを開いて終了コードと結果を確認した。
- R4-P3検証ログ: `build/Testing/Temporary/LastTest.log`。GPU testでhit distance=2.03175、instance custom index=17、primitive=0、miss=-1を確認し、RT無効・compute-pipeline例外・result-buffer例外の各経路でdraw count=1とSceneColor一致を確認した。専用CTestは1/1 passed。
- R4-P4: source commits `738ede8`, `3dcd38f`, `63fa779`でprobe atlas更新を確定した。前frame irradianceの1段bounce、visibility weighting、octahedral borderを実装し、現行のwrap重み床とhysteresis 0.6へ期待値を再基準化した。single/2-probe GPU readbackでirradiance・距離モーメント・border sampleを検証した。Debug build exit 0（third-party shaderc PDB LNK4099警告のみ）、専用CTest 1/1 passed、直接実行exit 0。出力は`single_probe_array_layers=2`、border sample `0.912965` / interior-only `0.89276`、visibility-weighted `1.96391` / unweighted `1.30247`、`VUID_COUNT=0`。証跡は`.harness/runs/20260922-r4-p4/verify-4-build.txt`、`verify-5-ctest.txt`、`verify-6-direct.txt`。
- R4-P5最終検証: Debug buildはEXIT_CODE=0、対象CTestは3/3 passed。証跡は`.harness/runs/20260922-003731/verify-R4-P5-12.txt`と`verify-R4-P5-13.txt`。
- R4-P5の後続確認: Lighting側のnormal biasとGLSLコンパイルをR4-P6のGPU受入れで確認する。
- R4-P7最終実GPU検証（20260922-r4-p7-final3）: `build.txt` EXIT_CODE=0、Cornell static/dynamic capture EXIT_CODE=0、P4 direct EXIT_CODE=0、R4指定CTest 9/9 passed。証跡は`.harness/runs/20260922-r4-p7-final3/`に保存した。
- R6-P1検証: `verify-R6-P1-1.txt`でGame build EXIT_CODE=0、`verify-R6-P1-2.txt`でRayTracingSceneSnapshotTest・RenderingDDGILightingContractTest・RenderGraphCompileTestの3/3 passedとEXIT_CODE=0を読戻し確認した。既存のthird-party PDBおよびlibwebsockets生成物warningは残るが、対象ゲートは成功した。
- R7-P1検証（2026-09-23）: `verify-R7-P1-3.txt`のGame/関連target buildはEXIT_CODE=0。`verify-R7-P1-4.txt`の対象CTestは4/5 passedで、RTGI、Indoor golden、2つの契約テストがpassed、Outdoor goldenのみ開始時と同じ数値でfailed。`verify-R7-P1-5.txt`でR1 capture testを再buildし、`verify-R7-P1-6.txt`でall-numerical契約（40 static rows、68 numerical rows、120 captures、EXIT_CODE=0）を確認した。


## G1 ゲーム基盤（2026-10-02）

- Done: G1-GR08-P1。カプセル対球・箱・カプセルの符号付き分離距離、法線、表面点を追加。短い線分・近平行・微小数の回帰ケースと球解析式の固定乱数1万件をg++で実行した。ASan/UBSan（LeakSanitizerを除く）も成功。既存ComputeContactの実装は不変。
- In progress: GR08の衝突クエリ基盤。G1全体は未完了。
- Next: 球・カプセル掃引のCPU数学部分、続いてレイヤー/フィルタと統合クエリ。
- Notes: Windows/Core全体/CTest/Game/GPUは未検証。既存GeometryContactTestは無条件Windows.h取り込みだけを除いた一時コピーで回帰確認した。MathGeometryTestは既存Quaternionスカラー演算子不足、PhysicsはWindows.h依存のためLinux直接ビルド不能。描画側のソースと既存SS/R/FIX項目は変更していない。

- G1-GR08-P3M: 球/カプセルの並進掃引のCPU数学を追加。面/辺/角、かすり、回転、初期重なり、無効入力、移動距離0、反復上限を確認した。1万配置の既存接触判定の2mm走査+二分法との比較、ASan/UBSan（LeakSanitizer除外）、分離/既存接触の回帰検証に成功。Hit/NoHit/InvalidArgument/IterationLimitを区別する。RelativeToleranceは絶対座標に依存するため遠方では許容幅が拡大する。Physicsのスナップショット・フィルタ・façadeへの統合はまだなく、GR08/G1全体は未完了。

- G1-GR08-P2A: 既存の物理結果enumと世代ハンドルをOS非依存のPhysicsQueryTypesへ同じ定義で移設し、クエリ記述子・結果・バッチ範囲、32ビットのレイヤー、trigger/ignoreフィルタと対称ペア規則を追加。全32ビット、ignoreの世代一致、無効ハンドル、4096通りのペア真理値表、既定値/レイアウトを直接g++で検証。bundle相当のmain置換コンパイルとASan/UBSan（LeakSanitizer除外）も成功。実クエリへの接続はP2B以降。

- G1-GR08-P2B: 単一PhysicsShapeProxyの統合クエリを追加。ray/overlap/sweepへフィルタを適用し、世代ハンドルとUserDataを返す。新Overlapは対象からの押し出し法線、失敗は出力初期化、未収束はIterationLimit。既存float幾何がoverflowする相対尺度はInvalidArgumentで拒否する。実PhysicsBroadphase.cppとMathを直接リンク（未使用関数除去、OS/メモリmockなし）し、3形状のray/sweep、overlap法線、フィルタ/無視/trigger、初期重なり、無効入力・巨大入力を検証。ASan/UBSan（LeakSanitizer除外）成功。Windows/Game/複数proxy集約/コライダー設定からの接続は未検証・未実装の範囲として残る。

- G1-GR08-P2C: 複数proxyの結果をspanへ集約し、最近接rayは同距離の大きいhandle、All/sweepは距離とhandle昇順、overlapはhandle昇順にした。MaxHits、self-ignore後の背後命中、容量不足、空入力、途中失敗/未収束での全出力初期化を実コードで検証。入力と出力の格納領域は非aliasを必須とする。通常テストとASan/UBSan（LeakSanitizer除外）に成功。VariableArray wrapperはコンパイル確認、エンジンの実メモリシステム結合は未検証。
- Next（2026-10-02更新）: GR08のコライダー設定/SceneQuery接続は未完のまま保持し、ロードマップ先頭のGR01更新段階へ戻る。G1-S1/S2/S3の選定待ち。描画との競合可能性だけでは順序を後ろへ回さない。

- G1-GR01-P1: 固定8更新群とComponent設定/OnTickGroupを追加。既定Default/priority0、主群bit必須、未知群/maskの非変更拒否を単体g++で確認。NDEBUG指定でも検証が省略されないテストとし、bundle相当コンパイル、ASan/UBSan（LeakSanitizer除外）も成功。既存Tick/FixedTickとWorldの実行処理は不変。実Component/Windows/Gameは未検証。後続Worldでは優先度/maskだけでなく主群も収集時に固定する。
- Next: GR01のComponent遅延破棄とTick対象の寿命保証、続いてWorld/Applicationの群別更新へ接続する。個別依存は既存Delegateで連携する。

- G1-GR01-P2: Worldの全群/Fixed対象を一回収集し、前半群とLateTickへ接続。主群/優先度/maskを収集値で固定し、既定DFSとowner先行を維持。更新中のComponent/Entity除去は予約し、既存固定step後cleanupを維持した。Entity::RemoveInnerでObjectHeap/GCを含むdetachを捕捉し、保持entryとcleanup待ち参照を無効化。callback自身がheapから消えたときの戻り処理も対象を参照しない。順序/無効化helperはg++/ASan/UBSan（LeakSanitizer除外）成功。実World/Heapの統合ケースはWorldTickGroupTestへ追加したが、Windows.h依存によりコンパイル・実行は未検証。ApplicationからLateTickへの配線、Module/Application後段、ボーン姿勢公開、カメラ移行は未完。

- G1-GR01-P3: ApplicationのシミュレーションをTickSimulationへ抽出し、固定step後にWorld LateTick→Module LateTick→Handler OnLateUpdateを描画同期前へ接続。固定0回でも後段を実行し、pauseでは止める。フレーム中のhandlerを共有所有し差し替え時の寿命を保つ。既存固定step関数の不変と外側処理の順序を静的チェック/独立レビューで確認した。既存11ケースを維持し、全8群/0・1・2step/早期とCameraの位置対照/pause/空・非Running registryの6ケースを追加。Windows統合17ケースはコンパイル・実行未検証。portableの群設定/dispatch helperは再実行成功。
- Next: GR01の評価済みボーン姿勢公開とAnimation/PoseFinalize配線、その後SpringArmとGameカメラを後段へ移す。G1-S1/S2/S3は承認済み、依存連携はDelegateのイベント駆動を使う。

- G1-GR01-P4: SamplerへinverseBindを含まないJointModelMatricesを追加し、SkinnedMeshをAnimation/PoseFinalizeへ配線。EvaluatePose/serial、名前引き、評価済みmodel/world行列と表現可能な正scale TRSの読み取りを公開した。旧BonePalette式は不変。資産無効/子差替えを検出し、meshNodeの既定追従と明示overrideを区別、再評価/無効化を描画dirtyへ伝播する。既存SamplingTestへ手計算・inverseBind除外・名前索引・serial・child変換確定・shear拒否・実SceneViewの停止中差替え/unloadケースを追加。独立静的レビュー第2周PASS、CRLF差分検査成功。Windows.h依存でSampling/FramePacket/M9をコンパイル・実行できず、既存失敗基準線との差も未確認。
- Next: SpringArmのCamera群割当とRendering3DTestのカメラproxy確定をOnLateUpdateへ移す。

- G1-GR01-P5: SpringArmをCamera群へ移し、Rendering3DTestの後段カメラをGameHandler OnLateUpdateのone-shot Delegateで確定。Enterで一回作るData所有bindingをweakで予約し、Leave/handler破棄/途中Entity・Component削除を安全に拒否する。初期camera同期は維持、child pivot/cameraは前後の変換確定で対応。既存SpringArmComponentTestへ群/0・1・2固定step/child/削除/disabled/weak/上書き/再入の試験を追加した。静的レビュー第2周PASS、行末差分と初期同期保持の静的検査成功。Windows.h依存で実試験/CameraWorld/M9/画像比較は未実行。TICK_STAGE_SMOKEは実装済みだが実ログ未観測。GR01受入れ全完了やG1 completeは宣言しない。
- Next: G1のGR03入力抽象化。S4 Raw Input、S5 XInput、S8暫定保存先は作者へ確認中。選定依存の実装は保留し、依存しない純ロジック等の計画・実装を進める。

- G1選定更新（2026-10-02）: 作者がS4=a（Raw Input＋cursor固定/非表示）、S5=a（XInput）、S8=a（暫定working directory＋store抽象）を承認した。方式選定の待機は解除する。
- G1-GR03-P1: InputState::ReleaseAllを追加し、同frame押下後もReleasedを次のBeginFrameまで保持、押下/累積を解除、復帰最初の絶対座標を再基準化する。既存Pressedはcurrent/previous比較のまま。同frame再押下ではDownとReleasedが同時にtrueになり得ることを明記した。実InputState.cppのg++通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）とbundle相当compile、既存遷移基準線は成功。独立レビューでも実行PASS。OSフォーカス/InputSystemイベントdelta/controller解除は後続で未接続。
- Next: GR03入力軸のdeadzone/曲線/変位と速度の時間単位を分離した純ロジック。

- G1-GR03-P2: 正規化1D/2D radialのdeadzoneとLinear/Power/Expoを純関数へ分離し、方向・単調性・長さ上限を保つ。視点換算ではmouse変位にdtを掛けず、stickの毎秒速度だけへ実時間を掛ける。無効値/設定/overflowはfalseかつ出力0。実headerのg++通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）、bundle相当compileに成功。独立評価の追加843,360ケースもlong double参照値と整合してPASS。Mapper/OS/Game接続の合格ではない。
- Next: ボタンの時間遷移と固定step押下ラッチを純ロジック化し、入力Mapperへ組み込む土台を作る。

- G1-GR03-P3: ボタンのPressed/Held/Released、Hold/Tap/非重複DoubleTapと固定step消費までのbool押下ラッチを純kernelへ分離。Cancelはrelease以外の操作と遅延fixedPressを消す。独立評価が0.2/0.3の差分比較によるinclusive境界反転を検出し、絶対deadline比較（overflow/ゼロ経過も考慮）へ修正した。通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）、bundle相当compile、独立再現/境界probeに成功し第2周PASS。Router/Mapper/OS接続は未完。
- Next: 入力元の型とarmedを解除する履歴、Identityのaction/binding/context、Mapperへ接続する。UIがReleasedと同frame再Pressedを両方consumeした場合にも古いarmedを残さない契約を含める。

- G1-GR03-P4A: キー/マウスのdown→up serialを正本に追加し、Routerへ届いたPressedだけを許可するInputArmedStateを追加。UIがreleaseと同frame再pressを両方消費して最終down=trueでも旧許可を失効させる。Repeatで復活させず、modifierにも同じ許可条件を使う。正本差替え時はResetする前提を明記。実InputState＋armedの通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）とRelease回帰、独立-Wall/-Wextra/-Werror実行に成功、レビューPASS。Router/Mapper/ImGui接続は後続。
- Next: backend非依存Gamepad/物理binding型と検証、その上のIdentity設定所有とMapperを組む。

- G1-GR03-P4B: SDK非依存GamepadStateとphysical source/binding型を定義。4slot、単独button codeとstate用mask、axis/trigger範囲、target XY、modifiers、出力Normalized/FrameDeltaを検証する。変位sourceをNormalized軸へ暗黙clampしない。全16bit code/slot境界/型組合せ/数値異常を実headerの通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）で検証し、独立評価PASS。threshold 0/1と隣接範囲外も恒久試験へ追加して再実行成功。JSON代入前のwide整数検証は後続の責務。XInput/Mapper接続は未実装。
- Next: パッド入力の正本と解除履歴をInputState/armedへ接続し、Mapperが全sourceを読める形にする。

- G1-GR03-P4C: InputStateへ4slot Pad snapshot/前state、frame edge、release serialを接続し、Pad armedにも到達Pressedと世代一致を適用。更新は検証後一括、切断/ReleaseAllでPressedを取消し、ReleaseAllは物理接続/packetを保って入力をneutral化する。実InputStateの通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）と既存KBM/type回帰に成功。独立評価は全button/index、不正更新の非変更、12,000履歴遷移等の30,646,619チェックを通常/O2-NDEBUG/ASan・UBSanで通しPASS。InputSystem配送/XInput/haptics/実機は未接続・未検証。
- Next: Identityが所有するcontext/action/binding設定とcompile境界を作り、Mapperへ接続する。

- G1-GR03-P4D: CursorMode値型、InputActionSettings純検証、Identityでcontext/action/bindingsを所有するInputBindingSetを追加。重複/unknown/invalidを拒否し、空unbind、deep copy、alias置換、借用viewの寿命を定義した。純settingsは通常/NDEBUG/ASan・UBSan（LeakSanitizer除外）とbundle相当compileがPASS、所有/APIは独立静的レビューPASS。Identity→StringのWindows.h依存でInputBindingSet.cpp/所有試験のcompile・実行は未検証。boolの拒否とallocation例外を区別し、後続JSONの全体更新は候補構築後のmoveとする。Mapper/JSON/OSカーソルは未接続。
- Next: binding spanと正本/armedから実action値を作るportable runtime、それをIdentity設定・Routerへ接続するMapper。

- G1-GR03-P4E: binding span＋正本/armedからButton OR/短tap/固定stepラッチ、1D/2D curve、event時modifier判定付き相対変位とrate×実dtの混合を評価するInputActionRuntimeを追加。借用spanを保持せずinvalid/overflowは非変更。独立評価でcurve前float化の数理欠陥（巨大Gammaで飽和2Dが0、微小値がcurve前に消失）を検出し、double集約のままcurveまで通すWide APIへ修正、恒久回帰を追加。通常/NDEBUG-O2/ASan・UBSan（LeakSanitizer除外）、bundle相当compile、既存7件回帰が成功、第2周レビューPASS。Router/Identity/OS/Gameの実統合とは区別する。
- Next: Raw/Pad/resetの配送口とInputMapperを既存Routerへ接続し、context maskと設定所有を統合する。

- G1-GR03-P4F: Raw相対mouse/縦横wheelの独立累積と検証後一括更新、InputSystem→Delegate→Router配送、Pad snapshot→consume不能接続通知→新Pressed→旧Releasedの順序、全解除の取消通知を追加。絶対mouseの初回/ReleaseAll後deltaを正本と一致させ、ImGuiのRaw捕捉/横wheel/X1/X2を接続。実InputStateの通常/NDEBUG-O2/ASan・UBSan（LeakSanitizer除外）、KBM/Pad/runtime回帰、実CameraInputCollector compileに成功。公開API/配送/統合試験は独立静的レビューPASS。RoutingExtensionTestはWindows.h依存で未実行、OS/Mapper/Engine配線は未完。任意指摘を受け、Pad/Mouse heldのReleaseAllで通常Released通知が増えない試験も追加。
- Next: Identity設定を所有するInputMapperをRouterへ登録し、context stack/UI armed/focus cancel/固定step消費を接続する。

- G1-GR03-P4G: InputMapperがIdentity context/action/bindingsとruntimeを所有copyし、正本とRouterを借用してAttach/Detach。top contextだけを評価し、切替/focus/resetでCancel/armed Reset、同frame短tap/fixed消費/相対入力を接続。Pad断線は別bindingのheldを保ち、それ以外を通常TapではなくCancel。GetActionは値snapshot、未接続/非focusではanalogも停止。独立所有/寿命/公開API/統合試験の静的レビューPASS、実portable state/runtime5件回帰PASS。InputActionMapTestとMapperのcompileはIdentity→String→Windows.h依存で停止し未実行。任意改善のmouse/modifier/別Router再Attach試験と配送中設定変更禁止のheader記載を追加。Engine/App/OS/JSON/rebindは未完。
- Next: EngineがMapperを所有し、Runのmessage前BeginFrame→TickのOnUpdate前Updateへ単調な実時間で配線する。

- G1-GR03-P4H: Engineが正本/Routerより後にMapperを所有して自動Attachし、Runのmessage前BeginFrameとTickのOnUpdate前Updateへsteady_clock絶対時刻＋clamp前raw dtを配線。Run開始/終了で取消、Shutdown/destructorは先行Detach。独立評価でRun例外時に末尾CancelAll/EndRunが飛ぶ欠陥を検出し、scope-exitに修正。実Runを通る偽platformのPump例外/OnUpdate例外/通常終了、legacy透過、所有/時間/fixed latchの試験を既存Engine bundleへ追加し第2周静的レビューPASS。実InputActionRuntime/InputButtonState回帰PASS。新Engine試験と既存FixedStepSchedulerTestはWindows.h依存でcompile停止、統合実行/起動画面は未確認。Game JSON/context設定、rebind、OS供給/cursor/focus通知は後続。
- Next: bindings.v1の名前付きcode・設定JSON/既定とユーザー差分の重ね合わせ、IInputBindingStoreによる保存、リバインド捕捉を実装する。

- G1-GR03-P5A: bindings.v1の物理code名/安全数値変換、既定JSON/full書出とuser差分の全体適用/差分書出を追加。Identityは名前保存、未知項目は警告/無視、既知不正はdefaults fallback、absent/empty bindings、alias、BOM、1MiB/深さ64を扱う。JsonValue借用列挙を追加し、既存数値parseをlocale非依存のfrom_chars＋全token/範囲検証へ変更。独立評価でfloat化による意味範囲外値の丸め通過と、空未知field名の独自String c_str問題を検出。double/float両方の範囲検証と空文字literal警告へ修正し、恒久試験を追加、第2周PASS。実portable名前/数値試験は通常/NDEBUG-O2/ASan・UBSan（LeakSanitizer除外）/bundle compileと既存3件回帰成功。JSON/JsonWriterの統合compile/実行はWindows.h依存で未確認。既存Delegate識別/JSON surrogate問題は独立TODOとして追跡。
- Next: IInputBindingStoreによる一時working-directory保存/読込とGame既定JSONのロード、それからrebind捕捉とOS入力供給へ接続する。

- G1-GR03-P5B: IInputBindingStore値返し結果とLoadInputBindingConfiguration/SaveInputBindingOverridesを接続し、Missing/Invalid/ReadErrorを区別して既定へ退避。Windows暫定Storeは生成時に絶対W pathを固定、上限1MiB＋全read/EOF、同directoryのCREATE_NEW tempへ全write/flush/close後に置換する。自分のtempだけ後始末し、元targetを先にtruncateしない。個人設定をgitignoreへ追加。独立IO/寿命/Win API静的レビューPASS、portable名前/settings回帰PASS。FakeStore/native temp-directory試験をbundleへ追加したがWindows.h依存でcompile/実行未確認。任意指摘の1MiBちょうど成功と無関係temp保持の試験も追加した。起動時の自動上書きは行わない。
- Next: P5CでGameのDefaultInputBindings.jsonと初期化ロードへ接続する。

- G1-GR03-P5C: GameInputActionsと4 context/11 actionの既定Assetを追加し、GameInputSettingsが起動時に既定＋user差分をロードしてMapperへ反映する。ConfigureWithContextは設定と初期stackを一括反映し、失敗時は旧設定を維持。Debug/Normalで既存camera経路を保ち、保存は明示APIのみ。独立静的レビューPASS。実JSON dataから生成したportable binding/runtime検証は成功。GameInputSettingsTestとMapper回帰を既存bundleへ登録したが、Windows依存の実JsonDocumentロード/Game起動/統合試験は未実行。
- Next: event基盤を拡張する前に既存Delegateの解除対象誤識別を独立修正し、GR03のrebind捕捉とOS入力供給を接続する。

- CORE-DELEGATE-IDENTITY（G1-GR03イベント基盤の前提）: free functionの保存領域pointer比較を関数値比較へ、memberの同closure型誤一致を型付きinstance/method比較へ修正。functorは登録tokenで識別しDelegate copyで維持、別Bindは別登録。候補swapで例外時の呼出先/識別の整合、move元空、null member空を保証。void統合時のnonvoid member結果破棄の互換性を恒久回帰付きで修復し第2周レビューPASS。実Delegate試験は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功。Multicast個別解除/同登録一括解除の実試験を追加しsyntax/bundle compile成功、実allocatorリンクはWindows.h依存で未実行。
- Next: GR03の入力取消を既存camera/drag/UIへ伝え、OSのfocus/cursor/raw供給とリバインドを接続する。

- G1-GR03-P6A: CameraInputCollector/MayaCameraController/LightController/PickingController/ImGuiをOnInputResetへ接続。未完了の操作とqueue/current入力だけを取り消し、camera姿勢/light値/確定selectionを維持する。進行中sphere previewだけを消し、遅延Releasedで新たな選択を実行しない。実CameraInputResetTestの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功。実InputSystem→Routerを使うLegacyInputResetTestを登録し独立静的レビューPASS、Windows依存の統合compile/実行は未検証。
- Next: GR03のWindows focus喪失/復帰通知を接続し、handler通知をOS message処理外で行う。Raw Input/カーソル制御/リバインドは継続。

- G1-GR03-P6B: IWindowの入力focus DelegateとWindows activation/keyboard focusを接続。Processorがshared window＋保存Delegate購読を所有し、loss時Mapper停止/ReleaseAll/全controller focus通知、Game handlerはPump後へ順序配送する。再入は次batch、旧購読batchはserialで停止、handlerによるplatform破棄後の借用pointerアクセスを防止。非focus legacy入力を抑止。第1周で旧repeatがTranslateMessage経由で文字だけ漏れる問題を検出し、native VK履歴により翻訳前も抑止するよう修正。即時observerのOS callback制約を公開APIへ明記し第2周レビューPASS。実WindowsKeyRepeatGateTestは通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功、Delegate/ReleaseAll回帰成功。Windows native/IME実機/新旧Engine統合は未実行。
- Next: GR03のRaw Input供給とカーソル要求/有効モードを接続する。リバインド捕捉、GR04のXInput/hapticsは継続。

- G1-GR03-P6C: Windows main windowの明示Raw mouse登録/解除とWM_INPUT motion laneを接続。自然alignmentの固定RAWINPUT bufferでサイズ/typeを検証し、foreground cleanupをDefWindowProcへ残す。button/wheelはlegacyだけ、X1/X2/横wheelとsigned座標を追加。device別absolute履歴は初回/focus/geometry/mode/remove/evictionで再seedする。第1周でPAGEONLY競合の見逃しとdisable失敗時の配送残留を検出し、page-wide/exact照合分離とlogical delivery/native ownership分離へ修正、第2周レビューPASS。実tracker通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileとRaw state/repeat回帰成功。Engine接続試験に登録寿命の検証を追加、Windows native/登録API/実mouse/RDPは未実行。
- Next: GR03の要求/有効カーソルmode、Clip/非表示とLocked時の絶対delta抑止を接続する。

- G1-GR03-P6D: Mapperのfocus非依存cursor要求をmessage前/Tick後にWindowへ同期し、requestedと最後に成功したeffectiveを分離。Windowsはclient screen RECTへClip、WM_SETCURSORで非表示、非focus/非表示/minimizedとRun終了/例外/Disconnect/DestroyでNormalを要求。move/size/DPI/display時の再適用、失敗のfalse返却/再試行を追加。Lockedの絶対mouseは位置だけを配送しdelta0、基準化はRaw/wheel/buttonsを保持。第1周でshared clip所有の実状態不一致とchild cursor上書きを検出し、GetClipCursor照合＋実focus/foreground判定＋対象HWND確認へ修正、第2周レビューPASS。実absolute追跡通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleとRaw/ReleaseAll/runtime回帰成功。Mapper/Router/Engine統合試験を拡充したがWindows依存で未実行、native clip/表示も未確認。
- Next: GR03のリバインド捕捉と設定への明示適用を実装する。GR04の実pad供給/hapticsは未完。

- G1-GR03-P7A: Mapperのcontext stackをIdentity順に維持する設定再構築と、GameInputSettingsのaction bindings変更/個別既定復帰/全設定既定復帰を追加。候補copy/検証/compile完了後に旧操作取消とnoexcept moveで反映し、unknown/invalid/欠落contextでは両方を維持する。alias配列と空unbindを扱い、変更だけでは保存しない。独立所有/例外保証/試験の静的レビューPASS。既存portable runtime/Delegate回帰は成功、追加Mapper/Game統合試験とstatic_assertの実compileはWindows.h依存で未確認。
- Next: GR03の物理入力捕捉と中止/解除待ちを実装し、この明示設定反映口へ接続する。

- G1-GR03-P7B: InputStateに最後のprovider pad sample/受理serialを追加し、ReleaseAllのneutral化と分離。成功注入は同値でも既存connection/button edge後にsample Delegate→Routerを配送し、invalid/Resetではsampleを作らない。callback前に注入引数をcopyして呼出元可変値の変更にもsnapshotを保つ。実GamepadInputState通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileとReleaseAll/runtime/raw/camera-reset回帰成功。独立公開API/配送/所有レビューPASS、routing統合はWindows依存で未実行。capture側が物理neutralを取り違えず判定できる供給口を整えた。
- Next: GR03の物理入力capture kernelと解除待ち、そのEngine/Router接続へ進む。

- G1-GR03-P7C: 値状態の物理入力captureを追加。source mask/修飾chord/単独修飾/初期held除外/接続基準化/analog hysteresis/方向/中止/neutral待ちを実装。候補生成をRouter到達eventだけに限定し、pad履歴は人工的resetと区別する。相対入力の静止判定は正本の非zero受理serialで補い、UI消費と±相殺でも早期終了しない。第1周の静止判定指摘を修正して第2周レビューPASS。実InputStateを使うcapture/Raw試験は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compile成功、pad/armed/reset回帰成功。Windows/Engine/Router接続はこのkernelの検証範囲外で未実行。
- Next: GR03のcapture managerをRouter/Mapper/Engineへ接続し、捕捉中の操作とcursor要求を停止、終了時の設定反映へ進む。GR03全体は継続中。

- G1-GR03-P7D: InputRebindCaptureManagerを同じSystem/Router/Mapperの単一ownerとして予約最高優先度へ接続。request ID付き開始/中止/結果取得、全通常eventの遮断、Mapperのevent/polling/fixedPress/Active/cursor抑止、外部reset/focus喪失での中止を実装。Advanceで残留入力をresetしてから抑止を解除し結果を公開する。EngineでMapperより先にDetach、Run終了/例外も通知なしに正本を中立化してDetachし、次Runで再Attach。独立静的レビューPASS、通常終了の再Attach/取消も試験sourceへ補強。新規manager公開headerの実syntax検査とkernel/Raw/pad/armedのportable回帰成功。実System/Router/Mapper/Engineの統合試験を既存bundleへ登録したが、String.h/Containers.hのWindows.h依存によりcompile/実行は未検証。代替stubは使っていない。
- Next: GR03のGame側action/slot/revisionとcapture結果を結び、古い設定への誤適用を防ぎながらP7Aの明示反映へ接続する。GR03/GR04全体は継続中。

- G1-GR03-P7E: GameInputSettingsへcontext/action/slot/明示出力/revisionの不透明な値requestとApplyRebindCaptureを追加。末尾追加/置換をP7Aの一括更新へ接続し、Pending/Applied/Cancelled/Stale/Invalidを区別する。manager request IDとsettings revisionを各々process内の非wrap採番にし、別instance・古い設定・二重適用を拒否。要求はowner pointerを保持せず、成功時だけCurrent/revision更新、Saveは明示のみ。純helperのsource/modifier/方向/型/invalid非変更と実InputActionRuntimeでの負軸Button発火を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle compileで確認。独立静的レビューPASS。実Game/managerの追加・置換・中止・別owner・stale・保存・失敗後再試行の試験を追加したが、Windows.h依存のcompile/実行は未検証。
- Next: GR03の--imgui利用時のロック解除hotkeyを受入れ要件に合わせて追加する。GR04のXInput/polling/haptics/device種別とGR08の残接続は継続。

- G1-GR03-P8: --imguiのInstall/UninstallとEngine所有InputDebugOverlayControllerを接続し、有効時だけF1でcursor NormalとGame入力maskを切り替える。UIへ通常eventを渡した後でGameを遮断し、Mapperの独立抑止理由をcaptureとORしてpolling/fixed/Activeも停止。context/JSON/描画内容は変更しない。Processorの配送後に要求反映/resetし、Engine/Runの所有順と再Attachを接続。第1周の自己resetによるF1押下世代消失と未適用enter終了時のlegacy残留を修正。内部guardとSystem所有deferred reset（次のAttach/BeginFrameで通知回収）を追加し第2周静的レビューPASS。純state通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleと公開controller header syntax、capture/runtime/Raw回帰成功。Core/Engine/ImGui統合試験にrepeat/二重Pressed・UI先行・capture優先・owner・pending終了/再開を追加したが、Windows.h依存によりcompile/実行と実機cursor確認は未検証。
- Next: GR03の計画上の実装項目は一通り接続済み。Windows実機と統合bundleの受入れは未検証として残す。GR04へ進み、XInputの実pad供給・接続/切断・振動・device種別通知を実装する。

- G1-GR04-P1: XInputの成功packetを独自GamepadStateへ変換するOS非依存境界を追加。signed16軸の負/正側を別の除数で±1へ、triggerを255分率へ正規化し、未定義button bitを除去、packetを保持する。deadzoneはMapperへ残す。左右motorは有限0..1を検証後にuint16へ一括量子化し、invalidで旧出力を保持。Microsoft一次資料で範囲/bit/左右motorの意味を照合。軸65536値、trigger256値、全channel/mask/packet/実InputState受理、motor量子化/invalidを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleで確認。独立レビューも実再実行PASS。native API呼出し、Windows統合bundle、実機はこの境界の範囲外。
- Next: GR04のBackground/Baseline配送を操作正本と実sample履歴に分け、focus復帰直後のheldを誤捕捉しないようにする。その上でXInput provider/polling/Engineへ接続する。

- G1-GR04-P2: Live/Baseline/Backgroundのpad配送modeを追加。既定Liveは旧順序/通常button通知を維持し、非LiveはPressedラッチと通常button通知を抑止、connection/物理sample通知は維持する。Backgroundは操作正本だけneutral、Baselineは実値へ同期し、物理sample/serialは両者とも実値を保持。captureは非Liveを基準化だけに使い初回heldを誤捕捉しない。全検証は更新前、invalidで状態/serial/通知非変更。実State/kernelの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundleとarmed/runtime/reset回帰成功。invalid modeの試験を新serial高analog→同serial Liveの捕捉へ補強。独立静的レビューPASS。System/Router/Mapperのmode伝搬・非focus取消・復帰held非armed試験を追加したが、Windows依存の実統合は未実行。modeだけでfocus Cancelは代替せず、lossのMapper停止/ReleaseAllを先行させる。
- Next: GR04の差替XInput APIとポーリング状態を実装し、接続済み毎frame/未接続低頻度、Background/Baseline選択、停止時のmotor zeroへ接続する。

- G1-GR04-P3: SDK非依存IXInputApiとXInputPollingStateを追加。4slotの接続済み毎frame/未接続1秒以上のround-robin探索、同packet配送、error中のneutral退避とhealth保持、Background/復帰Baseline、sink拒否/例外後の再配送を実装。有限非負単調clockを検証し、同回二重読取や長時間経過のcatch-upを防止する。実InputState sinkによる通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle試験成功。独立レビューPASS、追加の複数error/拒否時の公平性/1000回探索/例外再試行も実行成功。Windows API・Engine接続・実機入力・振動はこの段では未実行。
- Next: GR04のnative adapterとEngine所有デバイスの寿命/ポーリングを接続し、その後hapticsとdevice種別通知へ進む。

- G1-GR04-P4: Platform::CreateGamepadDeviceとWindowsXInputApiを追加し、native成功packetの全fieldを明示変換。失敗時のraw非変更、slot検証、Xinputリンクを接続。XInputDeviceはAPIを独占所有しpoll stateを内包、null生成/コピー/移動を拒否。System参照は同期sinkだけが借用する。IInputDeviceの旧poll互換を保ち、時刻付きbool poll/focus/provider識別を追加。実device.cppと公開headerのcompile、統合試験のMEMBER object compileは成功。試験main改名時のreturn欠落を実compileで修正し再確認、独立レビューPASS。P3の通常/ASan・UBSan回帰成功。実System/Routerを含むリンクはWindows.h依存で停止し、統合実行/native SDK compile/実機は未検証。
- Next: GR04のdeviceをEngineで所有して初期化/終了/例外/Run再開とfocus-message後のframe pollingへ接続する。振動は後続。

- G1-GR04-P5: Engine所有device列と単一pad provider制約を接続。開始試行前に終了義務を記録し、false/例外は逆順rollback、停止例外は残りを止めた上で義務/所有を保持して再停止可能にする。busy再入拒否、finite非負単調clock、false時も他device継続を実装。handler設定後に標準pad未登録時だけ生成し、Run開始/メッセージ後poll/終了・例外/再Run/破棄へ接続。focus取消後にdevice hookを呼び、停止時はcapture中止/Mapper取消/正本neutral、legacy通知はSystem所有deferred resetへ保留。debug teardownも共用。独立静的レビューPASS。非null再入登録拒否・invalid clock後の同時刻pollも試験sourceに補強。P3 portable通常/ASan・UBSan回帰とP4 header/device.cpp/統合MEMBER object compile成功。Engineと追加bundle試験のcompileはString.hのWindows.h依存で停止し、統合実行/native実機は未検証。停止失敗時の物理停止成功は保証せず、再試行後にも失敗する場合は限界が残る。
- Next: GR04の振動包絡線/混合/成功ACKに基づく再送・最終zero、haptics.v1設定とEngine接続へ進む。その後に入力方式切替を接続する。

- G1-GR04-P6: 独自Spanによる振動包絡線viewと純評価/混合を追加。有限正duration・厳密昇順key・0..1を全検証し、端点保持/線形補間/非loop終了0/loop fmod、最高priority activeのmaxまたはadd-clamp合成/設定倍率を実装。出力は成功ACKのみ更新し、左右差分1/255、各motorの最終zero、失敗再試行/潜在作動保持を扱う。レビューで標準span規約不一致と小寄与の逐次float丸めを検出し、独自Spanとdouble蓄積・最後1回のfloat化へ修正。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle object成功、独立追加520万checkと100万微小寄与反例の解消を確認してPASS。実service/voice所有/native送信/JSON/Engine接続は後続。既存GR03のview規約不一致はG1-INPUT-SPANとして別件記録。
- Next: 入力層の既存viewを規約に揃えた後、GR04のvoice所有と振動出力・停止制御を接続する。

- G1-INPUT-SPAN（GR03入力境界）: runtime/settings/names/mapper/binding setの呼出中viewをContainer::Spanへ統一。レビューで(pointer, 0)のcount/range曖昧性を検出し、既存Spanのrange constructorを末尾型制約付きにして解消、同pointer/nullptrの空rangeでは減算を避ける。配列/const/VariableArray/既存viewの変換と所有寿命を維持。実Settings/Runtime/Names/RebindTypes/Hapticsの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER object成功、独立レビューPASS。実Mapper/BindingSetのcompileはString.hのWindows.h依存で停止し統合実行は未確認。
- Next: GR04の振動voice所有と実出力・停止制御へ進む。

- GAME-VFX-REGISTER（GR130〜GR137）: 作者提供の初期3文書とVFX追加原文をDocs/Plansへ全量配置し、8要件をロードマップ/要件書へ追記。G7/G8/G10配属案と未着手を登録、G1進行中と完了ゲート未通過を区別。原文の作者判断・提案・完了条件案を保持し、GR57資産形式、GR137→GR126→G7/G8の循環、後段renderer、Bridge/Editor/SDK境界を選定待ちへ記録。初版スナップショットと追加原文は不変保持、計画文書は非追跡のまま。TASKSへGAME-GR130-VFX〜GAME-GR137-VFXを追加。エフェクトの実装は未着手。
- Next: 現在のG1/GR04を継続。VFX追加は前提GRと選定事項が整った段階で着手する。

- G1-GR04-P7: IInputDeviceのbool SetVibration/TryShutdownとXInputSetState adapterを接続。float範囲検証/量子化後の左右WORDだけ送信しERROR_SUCCESSをACKとする。SDK非依存stateで非zero試行の潜在作動・失敗停止義務を保持し、focus喪失/Shutdownは全対象を停止、失敗後のnonzeroはzero先行、停止後再Initializeも残留zero回収を必須にした。EngineはTryShutdown=falseを保持し再試行する。deviceの有効clock pollで保留停止を再送、失敗でも入力継続。実state通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER object、既存poll回帰、device.cpp/統合試験object compile成功。統合試験に有効clockのstop失敗下で入力継続/回復を補強。独立静的レビューPASS。API callback再入はP3同様に明記禁止で、native callback経路は追加していない。Engine/実Systemリンク/WindowsSDK/実機振動は未検証。destructorの最後の停止試行も失敗する場合、物理的停止成功は保証できない。
- Next: GR04のHapticsServiceへ効果定義・voice所有・実時間更新・pause/focus停止・設定/JSONを接続する。

- G1-GR04-P8: HapticsServiceへIdentity付き効果/keyのコピー所有と最大64voice、非wrap handleのPlay/Stopを追加。全候補検証/確保成功後のConfigure交換、無効値/確保例外で旧状態保持、alias再設定を扱う。新voice初回t0/以後実dt/loop/終了、最高priority合成/gain/設定倍率、focus/pause/disabled/切断で取消、成功ACK差分とzero/失敗retryを同期sinkへ送る。同じ実backend/slot対応を寿命中維持する契約を明記。レビューの端点丸め反例を修正し、非loopの値がdurationへ丸まった場合もfinished=trueとしてvoice枠を解放。純時間kernel通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle、既存混合/出力回帰成功、独立再実行と静的レビューPASS。実serviceと所有/制御/再入の追加試験はIdentityPoolのWindows.h依存でcompile/実行未確認。制御setter/Stopの送信は次Update/Flushで、Engine即時接続とJSONは後続。
- Next: GR04のEngine所有HapticsServiceをsimulation後の実時間更新、focus/pause/終了の即時Flushへ接続する。

- G1-GR04-P9: EngineへHapticsService値所有と固定providerの同期sinkを接続。simulation前のpause取消/即時Flush、後のclamp前実dt更新、focus喪失時のservice取消/Flushとdevice独立zero、終了時StopAll/Flush→逆順TryShutdownを実装。owner/service busy双方を検査し、最後のbackend停止結果で義務を保持する。第1周レビューでpoll中focusの拒否取りこぼしと検証不足を検出。Processorに最新focus＋喪失印の保留を追加し、raw即時中立化/安全batchのreset・Router通知、callback前snapshot/clear、Engine一致/購読serial、Run開始/message後/poll後/振動更新前後の回収を接続。false→trueでも旧voice取消を失わない。4slot、送信順、service-busy単独、実共通pause経路、sample中focus変化、適用中の後発focus、防御的observer復帰、window交換、停止再試行、実Run例外cleanup/再Runの試験sourceを追加し第2周静的レビューPASS。純時間/mix/output回帰は成功、Engine/test compileはWindows.h依存で停止し統合/native実行未確認。FlushとTryShutdownの結果を個別に逆転させる組合せ試験は未追加のnon-blocking残課題。
- Next: GR04のhaptics.v1効果JSONと既定asset/起動読込みを接続し、その後にActiveDeviceKind通知へ進む。

- G1-GR04-P10: haptics.v1 codecを追加。effect/key/任意settingsの既知field型・重複・有限値・範囲・名前/curveと1MiB/深さ64/effect256/key256/name128byte上限、未知field警告を実装。Parse/Writeは候補完成後に反映、service Configure overloadで効果/設定を一括反映する。レビューの入力/report aliasをlocal診断の退出時公開で修正し、既存Identity pool別名衝突は元名とのbyte一致検査で拒否。Game起動は既定Footstep/Hit/BiteHold assetを読み、通常I/O/validation失敗なら旧設定で継続、自動再生/保存なし。実asset1539byte/3効果のPython検証、pure時間/mix/output回帰成功、独立静的レビューPASS。実codec/Game/追加testはWindows.h依存でcompile/実行未確認。既存AssetFileReaderは全file確保後にJSON上限を検査し、確保例外は伝播して起動失敗になり得る。永続化/UIはGR76/GR68へ接続する。
- Next: GR04のActiveDeviceKind検出・ノイズ除外・切替ヒステリシス・Delegate通知を実装する。

- G1-GR04-P11: KeyboardMouse/Gamepadの純活動判定stateを追加。新規押下/有効文字/wheel、Raw・absolute別累積の移動閾値、Live padの新buttonとslot別analog累積変位で選択し、既定0.3秒の最短切替間隔を守る。背景/基準sample、repeat、静止held、noise、人工resetで表示を奪い返さない。設定/clock/sampleの全検証後だけ更新し、focusはkindを維持して累積基準を破棄。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/bundle MEMBER object成功、独立レビューも追加の跨frame trigger/無効非変更/held再奪取試験を実行してPASS。
- Next: GR04のInputSystem GetActiveDeviceKind/DelegateとEngineのframe clock/focusへ接続する。純状態の合格は実System/Engine/Windows受入れを意味しない。

- G1-GR04-P12: InputSystemへ使用中入力方式の取得/設定/変更Delegateを接続。受理正本を先に更新して活動を評価し、padは人工resetに影響されない物理履歴と比較、UI consumeから独立させた。Processorから共通非scaled frame時刻と即時focus、Engineから適用focusを供給する。不正System clockはMapper/frame更新前に拒否。kind取得は即時、通知はEndFrameで最終値だけ集約し再入二重通知を防御。公開header syntaxとP11通常/O2/ASan・UBSan回帰成功、独立静的レビューPASS。実System/Router試験は登録済みだがWindows.h依存でcompile/実行未確認。
- Next: GR04の計画上の実装項目を一通り接続した。Windows統合bundle/実パッド/振動の受入れは未検証として残し、G1のGR08（コライダーmetadata、query façade/provider、mask、明示snapshot更新）へ進む。GR03/GR04/G1の全受入れ完了とは扱わない。

- G1-GR08-P2D: ColliderComponentへ32bit所属Layer/相互作用Maskとopaque uint64 UserDataを追加。既存のowner thread/登録世代検証を通して更新し、既定1/全bit/0、0/複数bitを許可してゲーム固有名は持たせない。BuildBroadphaseでTriggerも含め値snapshotへコピーし、旧ray/overlap hit末尾にもUserDataを伝搬。setterで公開snapshotを即時更新せず、法線/順序/旧既定query挙動は維持。実query値型/実proxy集約の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER compile成功、独立静的レビューPASS。実Collider/Moduleの全形状/公開時点/スレッド・未登録拒否試験を追加したが、Windows.h依存で統合compile/実行は未確認。
- Next: G1／GR08の新ExecuteQueryをSceneQuery façadeとPhysics providerへ接続し、フィルタ・複数hit・sweepを実際の公開snapshotから呼べるようにする。solver maskと明示refreshは後続。

- G1-GR08-P4A: SceneQuery::ExecuteQueryを非pureのprovider拡張へ接続。未対応providerはUnavailableでsource互換を維持し、GameThread/接続確認後に全descriptorを渡す。非Success/例外で出力を空にし例外は再送出。PhysicsModuleはreadiness後に公開Broadphaseを読み、filter付きray複数hit/overlap/sphere・capsule sweepを公開する。旧APIの数値契約やsnapshot時点を変更しない。独立静的レビューPASS、同じ3球・7種・filter/ignore/trigger/MaxHits/UserData期待を実proxyコードでRelease/ASan・UBSan実行成功。fake/実Module統合試験を既存bundleへ追加し、全shape fieldの転送比較も補強。統合compile/実行はWindows.h依存で未確認。
- Next: G1／GR08のExecuteBatchで要求順の連続hit/resultとreadiness確認の集約を追加する。候補訪問の最適化、solver mask、明示refreshは継続。

- G1-GR08-P4B: SceneQuery/provider/PhysicsModuleへExecuteBatchを追加。owner/接続/準備をbatch単位に確認し、同一公開Broadphaseへの要求を順次処理してResult/FirstHit/HitCountと連続hitを返す。個別NoHit/InvalidArgument等は0hitで継続、全体失敗/例外は両出力を空にする。候補配列への組立、size上限確認、noexcept swapで部分公開を防止。準備済み空batchはSuccess、暗黙refresh/sequence更新なし。独立静的レビューPASS、既存実proxy回帰Release/ASan・UBSan成功。新batchのfake/実Module試験に混在offset・全hit field単発一致・metadata/sequenceを追加したが、Windows.h依存で新経路のcompile/実行は未確認。
- Next: G1／GR08のsolverとbroadphaseへ対称のLayer/Mask判定を反映する。候補訪問の最適化とS9=aの明示snapshot更新も続ける。

- G1-GR08-P5A: broadphase端点にLayer/Maskをコピーし候補追加前に対称規則を適用、solverもworking proxyの同じ規則を接触/trigger/solid処理前に確認する。既定候補順/接触端点/重複除去を維持し、queryのLayerMaskとは分離。全32bitの許可/片側拒否/複数bitと4096真理値表、実proxy回帰を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERで実行成功。独立第1周でHit期待に接近速度がない点を指摘され、許可時に接近→Hit/押出し、拒否時に自由積分/速度保持/Hitなしへ修正し第2周静的レビューPASS。実候補/Worldのtrigger Begin/Endとsolid検証は既存bundleへ追加済みだが、Windows.h依存で統合compile/実行は未確認。
- Next: G1／GR08の承認済みS9=a、明示RefreshDynamicSnapshotを追加する。fixed-step sequenceとsimulation stateは保ったままquery snapshotだけを更新する。候補訪問/掃引AABB最適化も継続。

- G1-GR08-P5B: 承認S9=aのRefreshDynamicSnapshotをSceneQuery/providerへ追加。owner/readinessと固定処理・通知中の再入を検証し、freshなlifecycle/Transform/shape/metadata/Body対応で候補Broadphaseを作り、完成後のnoexcept moveで公開queryだけ交換する。GR09までは全proxy再構築、明示呼出しのみ。simulation active cache/working proxy/速度/impulse/PreFixed準備/イベント/固定公開sequenceを保持。同sequence内の内容変化とcaller cache無効化を文書化。独立静的レビューPASS、実Broadphaseのnoexcept move syntaxと既存proxy Release/ASan・UBSan回帰成功。実移動前後/新規/無効化・復帰/保留impulse/通知中拒否試験を追加したが、Windows.h依存で新経路の統合compile/実行は未確認。
- Next: G1／GR08の候補訪問関数とray/swept AABBによる早期除外を実装する。無効値/未収束・順序の公開契約を保ち、CPUの全探索参照と照合する。

- G1-GR08-P4C: span上のproxyをAABB/有限長rayで保守的に絞る同期visitorを追加。入力順/precheck先行/非Success中断、不正proxyを除外で隠さない契約を保持。rayはdouble slab・軸別余裕・厳密0のみ平行扱い。第1周で許容OBB軸の逆写像領域がboundsより広い例を発見し、最大半径長1e-3の余裕とray/点AABB再現試験で修正、第2周PASS。実訪問処理を実Broadphase/Mathへ直接リンクして通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER成功。独立再現/再ビルドもPASS。既存proxy集約の回帰成功、集約への最適化接続自体はP4D。
- Next: 全探索比較の前に、独立レビューで発見した既存のray対球のfloat桁落ち誤hitを別タスクで修正する。center0/radius1に対しorigin(-10000,2,0),dirUnitXで旧QueryProxyが距離10000の誤hitを返す。新visitorによる正しい除外を誤って回帰扱いしないため、数値基盤を先に整える。

- G1-GR08-RAY: 旧float二次式の遠方球/カプセル誤hitを実再現し、doubleの直線距離・断面評価、有限円筒＋端球の最小正根へ変更。内部0/距離上限/float同距離順を維持し、微小線分/ほぼ平行も扱う。根を点の再構成までdoubleで保ち、局所double hitオフセットから法線を作る。第1周で見つかった大きいworld座標＋小radiusの法線崩れも修正して独立レビューPASS。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとproxy/visitor回帰成功。独立long double oracleの40,000件はhit17,229/miss22,771、誤分類/最近根逸脱0。parentでもoracleと法線再現binaryを実行確認。既存未実行facade試験のVector3単項minusを修正し実型の式syntaxを確認、Windows全体統合は未検証。
- Next: GR08の候補訪問/掃引AABBを集約へ接続し、新旧既定queryの経路と固定seed比較を揃える。G1監査で追加判明した共通Math/Curves、更新/pause/camera契約表、Capsule辺・角/初期侵入/Depthの受入れ試験もtodoへ登録した。G1完了とはまだ扱わない。

- G1-GR08-P4D: ray/形状bounds/掃引始終点AABBの訪問を集約へ接続。除外前のgeometry/filter/尺度確認、順序/MaxHits/全出力clear、巨大bounds時の全探索fallbackを維持。確定混在7,000件（各種類300件以上のhit）を全QueryProxy＋独立sortと全field照合。候補内IterationLimitは維持し、AABB非交差が証明できる対象はNoHitと確定する。比較で見つかった旧OBB rayのepsilon平行誤hitもdouble slab/局所法線へ修正。第1周でSweep OBBのfloat/double検証差を発見し、既存Mathの条件をIsValidSweepBoxへ抽出して本体/precheckで共有、第2周PASS。Pruning/Ray/Proxyの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとassert有効の既存GeometrySweep 1万oracle回帰成功。実Engine統合/Windowsは未検証。
- Next: G1／GR08の旧Raycast/Overlap入口を新query kernelへ転送して互換結果/符号/順序を固定する。その後Capsule受入れ、共通曲線、更新/pause/camera契約の残りを閉じる。

- G1-GR08-P4E: 旧Raycast/Overlap3種のModule/Broadphase入口を共通query kernelへ統一し、旧hit識別値/Point/Depth/順序とOverlap法線反転、直接Broadphaseの成功時追記を保持。旧無filter契約ではLayer0も検索し、新APIのbitmaskとは区別する。4,000固定seed全field比較・Layer0の4種類・失敗clear/容量/同距離を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERで実行成功、Pruning7,000件も成功。独立第2周PASS、実Module/所有配列の統合試験は追加済みだがWindows.h依存でcompile/実行未確認。
- Next: G1／GR08のCapsule面・辺・角・回転・初期侵入とOverlap深さの明示受入れを固定し、共通曲線と更新契約へ進む。

- G1-GR08-CAPSULE-ACCEPTANCE: 縦Capsule対Boxの面/辺/角、横向きCapsuleとCapsule同士を解析距離/法線で固定。初期接触0・侵入0.25・対称内部1.5のDepth/外向きNormal/Distance0/start印とignoreをMath・実query集約で確認し、新旧符号と深い重なりの近似限界を文書化。通常/O2-NDEBUG（assert有効）/ASan・UBSan（LeakSanitizer除外）/MEMBERと既存10,000件oracle成功。独立レビューPASS、45度回転/端点反転/辺角Depth/反対側押出し/直交Capsuleの追加反証も成功。production変更なし、Windows/World/Module統合未検証。
- Next: G1／GR03・GR04の最小Math/Curves共通化とGR01更新/pause/camera契約を閉じる。

- G1-GR03-CURVES: Math/Curves.hへ有限・単調key検証、区分線形、Linear/Power/Expo/SmoothStepを追加。InputAxisMathとHapticsEnvelopeMathが同じ数理を再利用し、JSON/既定値/所有型/独自失敗出力/振動duration規則は維持。第1周の極大floatから小値へのkey端点桁落ちをstd::lerpと±FLT_MAX/内部key/nextafter回帰で修正し第2周PASS。Curves/InputAxis/HapticsMixer/PlaybackTimeの4試験を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実行、Curves MEMBER compileも成功。後続GR119/GR121の入口/閾値と曲線の再利用契約を文書化。Windows/全bundle統合は未検証。
- Next: G1／GR01のComponent割当・Bridge進行と将来pauseの区別・最終camera境界を文書で確定し、G1検証範囲を一覧化する。

- G1-GR01-CONTRACT: 全Componentの群割当、可変とFixedの順序差、現Bridge gateと将来pauseの3分類、新Component登録規約を文書化。最終cameraは承認S3のGame OnLateUpdateで、Module Lateより後/通常Module Tickより前と明記し、G14未決配置とG16読取契約を区別。独立source照合PASS、コード変更なし。無視対象Roadmapへ矛盾を上書きする補足を保存。
- Next: G1最終照合で群境界の子Transform鮮度不足が判明。GR01-TRANSFORM-BOUNDARYとして修正し、その後に検証一覧を確定する。

- G1-GR01-TRANSFORM-BOUNDARY: Worldの最初の群前とsnapshot内の群遷移でWorld変換を確定し、Movementで動かした親を次のDefault/Animation/PoseFinalizeから子が同frameに読めるようにする。Camera→PreRenderも確定し、既存のLate空対象時の更新、同群内非同期、順序/収集/破棄/Fixed契約は維持。実World試験に全8群の親子孫・逆登録・同群observer・疎な群・空Lateを追加し静的レビューPASS。純TickGroup/DispatchはO2-NDEBUG/ASan・UBSan（LeakSanitizer除外）成功。実階層試験はWindows.hでcompile停止、実行未確認。追加階層走査コストは未計測。
- Next: G1検証一覧とCurves header登録を確定し、作者のG2移行指示に従いAssetCookLib分離など選定不要のG2基盤へ進む。

- G1-VALIDATION: GameFoundationValidation.mdへGR01/03/04/08実装と検証範囲を保存。最終portable確認は34件実行成功/実行失敗0、FixedStepSchedulerはWindows.hでcompile blocked1件。実World/Engine/Module/Windows/native/GPU/完全bundleは未実行として区別。追加群境界実World試験も未実行を明記。代表コマンド4件を文書どおり再実行成功。G1追加Publicヘッダ29件を照合しCurves.hの明示登録漏れを補完、全件exactonce。文書と実ログの照合PASS。
- Next: 作者承認に従いG2へ進む。AssetCookLib分離を先行し、G2-S8既存JSON/stb活用とG2-S2ソース隣サイドカーの2選定は返答待ち。それ以外の未決選定も勝手に確定しない。

- G2-P0-COOK-LIB: GR77/GR78以降の共通基盤として3cookerのcpp/hをSTATIC AssetCookLibへ分離し、AssetCookはMain.cppから同libをリンク。Coreとcooker公開includeをPUBLIC伝搬、内部includeはPRIVATE。既存7実装/headerとsmoke登録blockのbyte不変、単一source所属/依存順を検証し静的レビューPASS。cmake不在とWindows.h依存のため構成/実build/smoke実行・出力バイト一致は未検証。
- Next: G2-S8/S2返答待ちの間、GR86の現行128関節上限を共有定数へ集約する。値や受理条件は変更しない。

- G2-GR86-LIMITS: 現glTF decoder/cooker/NVSKEL 0.x loaderの128上限をResource/SkeletalLimits.hのLegacyMaximumJointCountへ集約。値/比較演算/0件拒否/エラー/format/vertex ABIは維持し、新版の256化で旧形式を緩めない名前にした。公開headerのC++23/O2/Werror・128/129境界・0〜1024述語一致、3参照とPUBLIC_HEADERS登録/BOMCRLFを確認、独立レビューPASS。3consumerのcompileはWindows.hで停止、実資産ロードの合格ではない。
- Next: G2のGLB共有処理とsidecar正本は作者回答待ち。GLB質問への承認をBVH/FBXまで広げない。新規外部parserや形式/既定値変更は選定前に進めない。

- G2選定: 2026-10-03 UTCにS8のGLB共有解析（既存JsonDocument/stb）とS2のsource隣sidecar/cook-loose共用を作者承認。BVH/FBXと他の未決選定には拡張しない。
- G2-GR77-CONTAINER: 無割当/借用SpanのGLB構造parserを追加。magic/v2/全長/整列/範囲/JSON先頭・BIN第2/既知重複を検証し未知chunkは無視、非GLBはBOM除去JSON view、失敗時clear。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERと4,000固定seed破損入力成功。独立66,470ケース反証と寿命/非整列/巨大境界も成功しPASS。JSON意味/padding照合や3消費経路の配線は未実装で、GLB資産の取り込み完了とは扱わない。
- Next: GR77のdata URI向け厳密Base64 primitive、続いて共有buffer解決へ進む。

- G2-GR77-BASE64: 無割当Span版の厳密base64検証/必要長/復号を追加。標準alphabet、末尾padding、canonical pad bits、容量/全span非交差を検査し、失敗時output非変更・成功tail保持。第1周のサイズ参照alias問題をBase64DecodeOutcomeの値返却へ変更し解消。RFC例、全256単byte/65,536二byte/4,096三byteを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERで成功。独立multi-quartet8,256/overlap39,576/padding24,576反証も成功、第2周PASS。data URI構文/percent/JSON/3経路接続は未実装。
- Next: GR77のdata URI/BIN意味検証、共有buffer所有と3経路への接続へ進む。旧.gltfのsource hashはBOMと外部buffer余剰byteも保持し、新しい埋込みbytesを二重hashしない。

- G2-GR77-BUFFER-SEMANTICS: 借用data URI view、対応4MIME/parameter/base64 flag、percent size/decode、GLB buffer0の宣言長と0〜3byteゼロpaddingを共有化。第1周のraw URI文字制約不足をMIME tokenとは別のurlchar検査で修正し、percent表記は維持。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBER、256byte percent変換と4,000変異成功。独立1,536ケース反証もPASS、第2周PASS。サイズ/viewは値返却、失敗非変更/借用契約を保持。JSON/I/O/実BufferSet/3消費経路は未接続。
- Next: GR77のBufferSet所有・外部/data URI/BIN解決へ進む。旧.gltfの全source hashを保ち、loose accessorのunchecked範囲検証は接続時に別修正として扱う。

- G2-GR77-BUFFER-SET: 外部/data URIの所有bytesと借用BINを共有し、宣言viewとhash用全sourceを分離。標準readerはpercent後の相対ASCII pathとcanonical component境界/通常fileを検査。第1周のcopy assignment例外によるmetadata/bytes不整合をcandidate copy→Swapへ修復し第2周PASS。実source/testのsyntax-Werror、move特性、MEMBER object compile成功。実URI predicateをsection GCで直接リンクして通常/O2/ASan・UBSan成功。所有/実I/O/確保失敗試験は未実行で、実allocator接続は既存utility不足、その先のMemorySystemはWindows.h依存で停止。代替allocatorなし。JSON/3consumerは未接続。
- Next: GR77のJSON buffer adapter、次いでcooker/画像/骨格/looseへ接続する。検出したloose accessor overflowとMemoryOverrides include不足は別タスクへ保存。

- G2-GR77-BUFFER-JSON: JsonDocumentのbuffers記述を共有resolverへ接続。既知field重複、型、正の安全整数、uri省略/null/ASCIIを区別し、所有URI配列を固定して呼出中Span寿命を保持。数値helperは通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERで成功、独立レビューPASS。25個のfixture JSON文法も別途確認。JsonDocumentを含むsource/testはWindows.h依存でcompile/実行未確認、数値試験で代用しない。
- Next: GR77静的cookerへcontainer/bufferを接続する。外部imageは旧どおりpath参照だけで、embedded imageの所有/packagingは後続へ分ける。

- G2-GR77-COOK-BUFFERS: 静的cookerを共有container/JSON buffer resolverへ接続し、BINをコピーせず宣言viewでmesh抽出する。元source/BOMと外部buffer余剰を含むhashを保持し、GLB/data URIを二重hashしない。102byte三角形の外部/GLB/data URI payloadと混在/BOM/余剰hash・不正/失敗出力保持を既存束へ登録。旧AA== smokeは短いbuffer拒否へ更新。material/cluster/wire/skeletalのsource不変、fixture整合、CMake参照、BOMCRLFを確認、独立レビューPASS。実container/buffer-sourceの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER成功。cooker/新試験のcompileはWindows.hで停止し、native/CMake/smoke/payload一致は未検証。
- Next: GR77の画像source共有解決へ進み、続いてembedded imageの結果寿命とmodel+texture package/manifest接続を行う。loose/skeletalも後続で、GR77全体は未完。

- G2-GR77-IMAGE-SOURCE: file URI/data URI/bufferView画像を共有解決。path/data bytesは所有、viewはbuffer index/offset/lengthだけを保持して宣言範囲へ再bindし、自己SpanとBINコピーを避ける。MIME/既知重複/排他/型/安全整数/範囲と失敗clear、copy→swapを固定。第1周で画像viewのbyteStride黙認を指摘され、値/null/重複を全拒否する存在検査と負例を追加、第2周PASS。外部URIとdata URIのcopy/move所有、BufferSet寿命を試験へ登録。実range helper通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/Werror/MEMBERとmove特性成功。40JSON fixture文法/PNGJPEG prefix既知bytes確認。JsonDocument/native試験はWindows.hでcompile停止し、画像decode/消費経路は未検証。
- Next: GR77静的cookerのEmbeddedImagesと材質参照を接続し、GLB借用とlocal buffer由来画像の所有を区別する。その後Mainのmodel+texture packageとmanifest更新、loose/skeletalへ進む。

- G2-GR77-COOK-IMAGES: 静的cookerの材質をImageSourceへ接続し、imageIndex順のEmbeddedImagesへ仮想path・role/format・画像hashを保持。GLBだけ借用、外部/data URI buffer内imageと直接data URIは画像部分を結果所有し、local BufferSet破棄後のdanglingを防ぐ。copy→swap/自己所有領域の借用拒否、同format重複統合/srgb-linear衝突拒否。GLB入力はoutResult自身に所有させない契約を明記。実1pxPNGをstbでdecode成功、JSON23fragmentと174byte画像buffer fixture整合、実range3mode回帰成功。nativeにはpayload/3role/所有copy-move/画像hash/失敗全出力保持/JPEG pathを登録したがWindows.hでcompile停止、実行未確認。独立第2周PASS。Mainはpackage接続前のためEmbeddedImagesを出力書込前に拒否し、未生成textureへの参照を成功packageにしない。
- Next: GR77のMain model+N texture packageとmanifest一括統合を実装してguardを置換する。skeletal/looseは後続、GR77全体は未完。

- G2-GR77-PACKAGE-IMAGES: Mainのmodel-only guardをmodel+N textureの単一entry package群へ接続。全変換/メモリpackageとpayload検証→実file書込/flush→incoming全AssetSystem解決/bytes一致→manifest1回更新。audioのmergeをincoming列へ一般化し同keyだけ更新、serializerと従来model出力tailはbyte不変。第1周で保持entryのbacking packageを暗黙img出力が壊す衝突を発見し、canonical/equivalent/Windows case比較で書込前に一律拒否、実audio package衝突回帰を追加、第2周PASS。3GLB fixtureは実container/BIN/range/stbi memory decodeで正常2/異常1を確認。CMake3.14+PS smokeを登録したがCMake/PS不在、MainはWindows.hでcompile停止しCLI/package統合実行は未検証。複数file I/O rollbackなし、variant間画像共有を明記し、準備/確定・一時file置換はGR96へ残す。
- Next: GR77の骨格共有buffer/GLB入口を接続する。looseは既知のaccessor範囲計算を先に安全化してから接続する。GR77全体と実物61MB受入れは未完。

- G2-GR77-SKELETAL-BUFFERS: 骨格decoder/cooker/AnalyzeSkeletalを共有container/BufferSetへ接続し、GLB/data URIと外部bufferを宣言範囲で読む。旧metadata/抽出順、128関節/単一clip/変換/NVSKEL形式を保持。decodedは独立所有、hash用BINは借用、String互換出力は全source bytes所有copy。M9に3入力形のdecoded/NVSKEL/SourceHash、BIN pointer、BOM/余剰/旧出力/失敗clearを登録し、試験directoryを一意の排他作成へ変更。独立レビューPASS。実container/buffer意味の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER、M9 fixtureの実container/BIN、実配列/Spanの型syntaxは成功。骨格本体と新native試験はWindows.hでcompile停止し、decoder/cooker/analyzer実行とpayload一致は未検証。
- Next: GR77 loose accessorのunchecked範囲計算を先に安全化し、その後looseの共有buffer/画像bytes/stagingを接続する。実物GLB受入れと実Windows統合は未検証のまま区別する。

- G2-GR77-LOOSE-BOUNDS: looseの4accessorを純粋な宣言範囲helperへ通し、actual/declared/viewと正のcount/element/strideを減算・除算で証明してからpointerを作る。private CPU staging入口は候補完成時だけ出力置換。実helper通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で各5,438,750件（広い有効範囲1万を含む）、MEMBER compile成功。追跡glTF6fileの24layoutも通過（12実file長/12宣言長のみ）。小三角形の正常と短いview/宣言・巨大offset・0countをnativeへ登録したが、GLTFAnalyzer/実staging試験はWindows.hでcompile停止。独立レビューPASS。JSON getterや全形式の厳格化とは区別する。
- Next: GR77の画像bytes→CPU stagingと必須拡張判定を共有し、最後にlooseへGLB/buffer/imageを配線する。staging/GPU/実物受入れ未検証を完了扱いしない。

- G2-GR77-IMAGE-STAGING: file読込とbytes decodeを分離し、PNG/JPEG bytesから所有RGBA8/ARM3枚のCPU stagingを作る。Mainとsignature判定を共有し、旧外部fileの形式/既定RGBA8_UNORM/RGB→AO/Roughness/Metallicを維持。INT_MAX・dimension境界、非copy stb scope owner、候補成功時のみ移動、ARM出力alias拒否を追加。実signature/range通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBERとstbi memory 1px fixture成功。file/bytes/ARM/failure native試験は登録済みだがWindows.hでcompile停止、実staging未実行。独立レビューPASS。
- Next: GR77の必須glTF拡張拒否を共有し、looseへcontainer/buffer/image bytesを接続する。

- G2-GR77-DOCUMENT-PROFILE: 必須拡張の型/field重複/非対応を共通判定へ集約し、静的cooker・loose・骨格でbuffer/geometry前に拒否。空requiredとoptional usedは維持、cooker通常診断/骨格InvalidDocumentを保持。22共通fixture、実正常骨格の4拒否/2成功とString/bytes/source clear、loose正常geometryへの4必須拡張拒否を登録。ヘッダC++23/Werror・fixture JSON構文・CMake登録/BOMCRLF/diff成功、独立レビューPASS。実JsonDocument/consumer/testはWindows.hでcompile停止、native実行未確認。
- Next: loose接続前に発見したindex値の頂点範囲検査を補う。既存clusterizerはindexを信頼して頂点を参照するため、GLTFAnalyzer側で拒否してからGLB/buffer/image接続へ進む。

- G2-GR77-LOOSE-INDEX-RANGE: 全復号indexの頂点範囲をwinding/clusterizer前に検証し、宣言buffer内でも頂点配列外へ読む不正入力を拒否。正常geometry/材質は維持。純検査8,256組とnull/空/0頂点/UINT32_MAX、既存byte範囲5,438,750件を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で成功、MEMBER Werror compile成功。正常102byte fixtureのindex=3/65535の拒否と既存出力保持をnativeへ登録、Windows.hにより実行未確認。独立レビューPASS。
- Next: GR77 looseへ共有container/buffer/image bytesを接続する。G2他選定とGPU/実物受入れの未検証は維持する。

- G2-GR77-LOOSE-SOURCES: static looseを共有container/BufferSet/ImageSourceへ接続し、BIN借用・外部/data URI所有をscope内に保持して独立CPU pixelsへ復号。外部logical requestは従来どおりfinalize遅延、埋込みは空RequestPathでRGBA8_UNORM2枚/ARM3R8を保持。不正material imageは伝播失敗し候補を公開しない。集計metadata/read_total stageを維持。新native試験に4embedded経路と外部絶対/相対、5texture/geometry、壊れたcontainer/image/view/必須拡張/8byte PNGの失敗保持を登録。fixtureの実Container/range/stbは通常/O2-NDEBUG/ASan・UBSan成功。独立レビューで試験末尾文字列を修復し、const pointer listを明示配列化、最終PASS。nativeはWindows.hでcompile停止。実物大型GLB/GPUは未入手・未実行で別受入れへ保留。
- Next: GR77の主要3経路の配線が揃ったため、承認済みGR78の共有sidecar/設定・変換へ進む。宣言MIMEと実signature不一致拒否は非blocking残件として登録。GR77全体のnative/実物受入れを完了とは扱わない。

- G2-GR78-SETTINGS: cook/loose共用のv1取り込み設定をCore privateへ追加。units/fit/符号付きaxes/mirror/origin/UV/windingの値・型・有限性・矛盾とJSON未知/重複を検証し、metaはhashから除外、未実装予約blockは空objectのみ。成功時だけ出力置換。固定52byteのLE/binary64/-0統一で正規化し、sidecar無しは呼出側で別扱い。純値/軸36組/既知bytes/各field変更を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER-Werrorで成功。43JSON fixture文法確認、実JsonDocument source/testはWindows.hでcompile停止。独立レビューPASS。file探索/変換適用/hash/CLIはまだ未接続。
- Next: GR78の軸/scale/fit/原点/法線/UV/winding共有変換を実装し、その後sidecarを既存ロードとcookへ接続する。既存モデルはsidecar無しなら恒等・現行hash不変を維持する。

- G2-GR78-TRANSFORM: 軸/鏡像、scale/3種fit、bounds/足元/表面/custom原点、法線/UV/windingの無確保2pass変換を追加。全layout/index/結果検証後に書き込み、失敗非変更・非整列/未指定field保持を実装。48方向、全fit、各原点、極端値を純実装の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER-Werrorで成功。ただし独立第2周で累積表面重心の中間丸め相殺が残ったためblocked。最新修正は各頂点×面積とfma残差をmoment展開に直接加え、最後に3×面積で除算する。M=2^100/t=2^-100の2面と面順序/循環順の回帰も3mode成功だが最終差分の独立確認は未完。ロード/cooker未接続を維持する。
- Next: GR78-SIDECAR-IO-HASHを先行する。変換適用部分は保留を維持し、設定file選択/読込/hashの独立部分を進める。

- G2-GR78-SIDECAR-IO-HASH: source全名+.import.json/明示override/required/disabledを共有選択し、自動探索の真の不在だけ既定へ戻す独立APIを追加。通常file/1MiB/BOM/NUL/短読・増大/厳格JSON/出力保持を定義。第1周でMSVCの不正名/network障害がerrc不在へ畳まれる点を指摘され、Windows raw system error2/3限定へ修正し第2周PASS。hashは既存FNV stateへ長さ52LE64+正規化bytes+algorithm1LE32を連結し、無しなら完全不変。3既知値と変更条件を実hash通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で成功。HashTest/FileTestの実MEMBER Werror compile成功、Windows不正名回帰は登録のみ。実file loaderはWindows.hでcompile停止、I/O/JSON runtime未確認。変換/CLI/ロードは未接続。
- Next: 作者へ表面重心を保留し他の原点/scale/axes接続を先行する案を確認中。返答までは保留を維持し、独立したGR77のMIME/signature整合を補う。

- G2-GR77-MIME-SIGNATURE: 埋込みPNG/JPEGの宣言MIMEと実signatureを共有predicateで一致検査し、cooker結果の公開/packaging前とlooseで拒否する。外部画像と有効入力bytes/hashを変更せず、未知signatureの旧CLI reasonを維持。不一致1200byte GLB、native cooker/looseの失敗出力保持とCLI既存manifest/model保持を登録。pure判定は通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行、MEMBER compile成功、fixture構造/PNG CRC確認。独立レビューPASS。native cooker/loose/CLIはWindows.h/CMake制約で未実行。
- Next: GR78縮小スコープ案の返答待ちを維持。G2検証時に見つかったMemoryOverrides.hの不足utility includeを既存todoとして独立修正する。

- CORE-MEMORY-HEADER-UTILITY（G2検証基盤）: MemoryOverrides.hへstd::forwardを宣言するutilityを1行追加。実header単独の追加前compile失敗を再現し、追加後はC++23/Werror syntaxとNew<Probe>(int&&)の実object compile成功。独立確認でもPASS。BOM/CRLFと他bytesを保持し、allocator/runtime/API意味は変更しない。MemorySystemのWindows依存解消や実allocator実行とは区別する。
- Next: G2／GR78の表面重心を後回しにして他変換を接続する案は作者回答待ち。G2の未確定形式選定も維持し、承認無しに保留変換やNVSKEL形式を適用しない。

- G2-GR78-TRANSFORM（縮小scope）: 2026-10-03の作者承認でsurface_centroidを未対応として明示拒否し、keep/bounds中心/足元/custom・scale/fit・axes/mirror・UV/windingを先行。候補の表面面積/重心コードをactive実装から除き、値/JSONはUnsupportedFeature、変換はUnsupportedOrigin、canonicalはSize0、失敗出力保持を固定。48方向を含む変換/値の通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）6実行とMEMBER compile成功。44JSON fixture文法確認、nativeJSON未実行。新scope独立レビューPASS。SurfaceCentroidは別保留へ分離し、他変換の接続を再開できる状態。
- Next: GR78-STATIC-SIDECARとして静的cook/looseへ共有file設定と変換/hashを接続し、その後骨格の一様scaleを別反復で接続する。

- G2-GR78-STATIC-SIDECAR: 静的cook/looseへ共有設定file/変換をcluster前に接続し、設定有りだけsource_hashへ正規化を連結。無し/disabledは旧経路、結果に採用設定を所有してCLI/looseへ診断する。looseは設定適用後の非有限bounds/coneも公開前に拒否。native統合試験にglTF/GLB・各変換・無し/恒等/disabled・meta/hash・override/required・surface/巨大scale失敗保持を登録。第1周の空source locator互換低下を修正し、自己完結GLB/data URIの空/非空同値とrequired/overrideを追加、第2周PASS。共有kernel/hash実行成功、実cooker/loose/統合試験はWindows.hで未実行。
- Next: 新しい入力sidecarをcook出力先で上書きしないguardを追加してから、静的接続と併せて公開する。骨格一様scaleはその後に進める。

- G2-GR78-OUTPUT-SIDECAR-GUARD: 採用sidecarをmodel package/manifest/派生texture packageで上書きするpath・symlink・hardlink aliasを全write前に拒否。既存Windows case比較を再利用し、canonicalize失敗も拒否する。Mainの実helper textをLinuxで直接compileし、同一/正規化/hardlink/symlink/別fileを通常/O2/ASan・UBSan（LeakSanitizer除外）で成功。CLI smokeに2直接alias、通常cook、派生texture hardlinkと設定/モデル/manifest保持を追加し独立PASS。Main全体/Windows/CLI/CMakeは未実行で、helper検証と区別する。
- Next: 静的sidecar接続とguardの2コミットをまとめて公開し、GR78-SKELETAL-SIDECARの一様scaleへ進む。SurfaceCentroidは作者合意どおり拒否を維持。

- G2-GR78-SKELETAL-SIDECAR: 共有scale/fit倍率を骨格の頂点位置・IBM平行移動・Translation sample・mesh-node平行移動へ同時適用し、raw/legacy/loose/cookへ接続。fitはmesh-node線形変換後のasset空間寸法で決定し、回転/scale/法線/UV/weight/形式は維持。設定snapshotをcookとhashで共有し、骨格非対応のaxes/origin/mesh変更は拒否する。設定出力alias guardも骨格CLIに接続。共有primitiveを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実行成功、MEMBER compile成功。native試験に非identity Scale fixture、0/1/2秒の回転終端、raw/cookedのscale/fitと実sampler比較、設定有無/hash/失敗保持/CLI guardを登録。独立第2周PASS。実decoder/cooker/sampler/CLIはWindows.h等の環境制約で未実行、登録と実行を区別する。
- Next: GR78-CLIの設定指定/require/no-sidecar、単体skip、inspectを進める。surface_centroidと実物描画受入れは保留を維持する。

- G2-GR78-CLI-SETTINGS: --import-settings（別引数/equals）・--no-sidecar・--require-sidecarをstatic/skeletalの共有loaderへ接続。明示設定優先、排他/重複/空値/値付きbool/非model拒否、設定出力alias保護を維持。実parserの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功、独立確認PASS。GLB設定優先/無効化のbytes/hash回帰/失敗出力保持/明示正本保護と骨格override/disabledのCLI smokeを登録。Main/native CLIはWindows.h等により未実行。skip/inspectはまだ未知引数として拒否する。
- Next: GR78-CLI-SKIPを独立反復に分離。元source/buffers/設定hashをcook前に計算し、manifestと実package/派生画像を検証してから省略する。inspect診断はその後。

- G2-GR78-SOURCE-PREFLIGHT: FingerprintModelCookSourceを追加し、geometry変換/cluster/画像decode/package生成前に本cookと同じsource/buffers/正規化設定hashとowned embedded画像metadataを取得する。設定file helperをstatic/skeletal/cook前照合で共有。返却に借用bytesを含めず、成功末尾だけ公開する。既存native束へ各入力形態/設定/骨格/hash同値/画像metadata/失敗保持と巨大scaleでの照合成功・cook失敗の区別を登録。既存純設定hashの3mode再実行成功、今回のnativecompileはWindows.hで停止し実行未確認。独立レビューPASS。非blockingのrequired不在時preflight単独assert強化はnative受入れ時の追補候補として保持。
- Next: GR78-CLI-SKIPへ接続し、manifest key/source hash/要求format・出力先とmodelおよび派生画像package実体を検証してからcookを省略する。

- G2-GR78-CLI-SKIP: --skip-if-unchangedをcook前fingerprintへ接続。manifest key/source hash/format/entry/type/version/要求出力先と実NVPACK entry・payload hash・NVMESH/NVSKEL構造を照合し、全embedded画像のmanifest/package/NVTEX/colorspaceも検査したhitだけ早期return。入力未変更でも欠損・破損・要求変更なら通常cookへ戻す。実flag parserを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功。native smokeに全出力timestamp非更新、設定/meta、model/画像欠損・破損、有効表記のhash不一致/format/entry/variant/output変更、骨格buffer変更を登録。独立PASS。cache本体/Main/native smokeはWindows.h等のため未実行。競合変更に対するatomic性やFNVの認証用途は保証しない。
- Next: preflightとskipをまとめて2コミット公開後、GR78-CLI-INSPECTのcookしない診断に進む。実物4本は未入手のため手動照合は保留。

- G2-GR78-INSPECT-GEOMETRY: 無変換bounds/軸長/最長軸・頂点/三角形/完全一致位置溶接/頂点共有成分/厳密ゼロ法線の純kernelを追加。符号付きゼロは同一、近接値は別、孤立頂点込みと明記。sort/union-by-rank/path-halvingで計測し、非有限/index/容量/全Span重複と乗算overflowを先行拒否。第1周でworkspace使用prefixだけの検査と公開契約の差を指摘され、全Spanへ修正し回帰を追加、第2周PASS。最終ソースの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功。CLI接続はまだ行わない。
- Next: GR78 inspect用のPNG/JPEG寸法・チャンネル統計を実stbで診断する部品を追加し、その後geometry/画像/材質を無書込CLIへ統合する。

- G2-GR78-INSPECT-IMAGE: PNG/JPEGを実stbで診断し、幅/高さ/decoded channel/8・16bitと整数min/max/meanを取得。色空間の線形化や16bitの縮約はせず、入出力aliasと失敗公開を防ぎRAIIで解放。最終pixel payloadの512MiB上限をinfo後に判定する。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実stbの既知PNG8/PNG16/JPEGとinvalid/巨大/aliasを実行成功、MEMBER compile成功。独立PASS、追加反証のgray/gray-alpha/palette+tRNS/1bit/16bit RGB・tRNSも成功。512MiBはstb内部総メモリ/IDAT inflate上限ではない点をheaderと仕様に明記。CLI/native統合と実物照合は別受入れ。
- Next: 幾何・画像の診断部品を公開後、材質係数と合わせて--inspect <file>の無変換・無書込診断へ接続する。

- G2-GR78-CLI-INSPECT: --inspect <file>/equalsを独立modeとして接続。単一mesh/primitiveの現行static profileを既存accessor検証で読み、mesh-local変換前の幾何と全画像の整数統計、core材質係数/規定値と任意emissive strengthを診断する。sidecar/cook/cluster/書込を通らず、成功後だけstdoutへ表示。+Y仮定の水平軸候補と符号manual、恒等設定叩き台はstdoutのみ。実引数parserの3mode/MEMBER成功、native API/CLI試験に値・不正入力・出力保持/外部画像・無書込を登録。独立PASS。nativeはWindows.hで未実行、実物4本は未入手。必須拡張の具体的診断理由保持とCLI再入力差の回帰はnonblocking追補候補。
- Next: GR78の実装/検証/保留をまとめる。GR86(S4)とGR32/GR82(S1)の推奨案は作者へ確認し、回答までは変更を適用しない。

- G2-GR78-VALIDATION: Docs/Architecture/GR78ImportValidation.mdへ承認済み実装・18実行成功（6純実装×3mode）・MEMBER compile・native/実物の未実行・surface保留を分離して記録。CMake登録と実logを最終照合し、CI空statusを成功扱いしない。次のS4/S1確認待ちをtaskへ明記し、GR78承認を他選定へ拡張しない。
- Next: inspect/検証記録を公開し、S4/S1の回答に応じてG2を継続する。未承認の形式/Strict/描画既定は変更しない。

- G2-S1-S4-DECISIONS: 作者の2026-10-03承認を記録。S4 A（Strict追加セット拒否/4本縮約/LINEAR焼込/morph明示drop/256はv1）とS1 A（0.2統一→v1）を確定。オオカミ/シ者の作り分け/共通土台張り直しを採用済みとしていた前提を未定へ訂正。v1 clip作成時rest保持、現在骨格との束縛時比較、許容超過既定拒否、明示許可と差量報告、全関節Translation回帰を必須契約へ追加。SkeletonIdだけでcache共有しない点も反映。ローカルPlans3本と追跡仕様/選定taskを照合し、追加auditでblocking誤記なし。他選定は未確定のまま。
- Next: GR86 Strictの追加ウェイトセット黙認をまず拒否する。cook前fingerprintも同じ検査を共有し、過去cache経由で新Strict拒否を迂回させない。

- G2-GR86-STRICT-INFLUENCES: 承認済みStrict変更としてJOINTS_n/WEIGHTS_nの追加セットを値/片側/null/ゼロに依らずInfluenceLimitExceededで拒否。属性名の正準/overflow/customを分類し、実decodeとcook前fingerprintで同一Json gateを共有して旧cacheのskip迂回も防ぐ。status末尾15のみ追加し既存0〜14、128関節、64B頂点、NVSKEL形式は維持。分類実装を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBER compileで成功、独立PASS。追加重みゼロの正規fixtureとraw/legacy/cooker/preflight/CLI出力保持をnative登録。Json/decode/nativeはWindows.hで未実行。
- Next: 同一joint合算・決定的な上位4本・脱落量/閾値・失敗保持を備える縮約kernelを独立実装し、その後明示Reduce optionにだけ接続する。

- G2-GR86-REDUCE-KERNEL: 同jointを正準順で補償加算し、合算後微小値を除き、重み降順/同値joint番号順で4本へ縮約・float正規化する純kernelを追加。脱落量は除去値の直接和/元総和で求め、厳密な閾値超過と正値underflowを拒否、失敗時vertexを保持。型・容量・全Span/options aliasも検査。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBER成功、独立PASS。追加反証の合算後閾値/総和nextafter/巨大Span/部分alias/混合桁重複jointの10000順置換も成功。恒久回帰への後二例追補はnonblocking候補。glTF raw整数総和とセット構造検査は接続側で別途行うため、現時点ではdecode/CLI未接続。
- Next: Strict既定不変を保つ明示Reduce options/reportとcanonical policy hashを定義し、その後複数セット読込へ接続する。

- G2-GR86-OPTIONS-HASH: 独立Public型へStrict/ReduceToFour・warn/failと走査prefixを明示するreportを定義。Strictは既定閾値のみ受理しhash不変、ReduceはSRED/schema/policy/warn/failの25byte LE/-0正規化と長さ/algorithmを既存FNVへ連結。公開header登録とkernel初期値の定数共有を実施。Python独立固定bytes/3state goldenと実policy・kernelの計6mode実行、MEMBER/public単独include/layout/trivial-copy確認に成功、独立PASS。decode/CLI/形式/statusは未変更。
- Next: 0始まり連続のJOINTS_n/WEIGHTS_n pairを収集・検証する部品を作り、Reduce指定のdecode接続へ進む。

- G2-GR86-INFLUENCE-SETS: JSON内順序に依らずJOINTS_n/WEIGHTS_nを正準順へ収集する所有collectorとpair検査を追加。0始まり連続/各1個/同数pair/整数u32番号を検査し、失敗時結果保持。Strict専用gateは変更せず、accessor型/count/buffer/weightは後段責務と明記。pure pairと既存分類の6mode実行/MEMBER成功、native16JSON fixtureは文法確認と登録のみ（Windows.hで未実行）。独立PASS。
- Next: Reduceをraw/legacy/file decodeへ明示optionsとして接続し、全影響のjoint範囲とUNORM raw総和を落とす前に検査する。

- G2-GR86-REDUCE-DECODE: 明示Reduceだけraw/legacy/fileに複数set読込と縮約を接続。全slot joint範囲、型/count/layout、全整数影響の65535共通分母raw総和を縮約前検査。成功prefixの統計と失敗頂点脱落量を分離し、失敗Data/sourceを非公開。Strict/status0〜15/128関節/ABI/wire不変、16/17を末尾追加。3頂点中1頂点5影響fixtureと入口同値/閾値/不正joint/負値/UNORM/optionsをnative登録。純kernel/policy6実行成功、独立確認はpair込み9実行成功。review中にExtractAnimationの不要追加引数を復元し最終PASS。nativeはWindows.hで未実行。正常prefix後失敗と複数UNORM混在の恒久回帰強化はnonblocking追補。
- Next: collector/decodeを公開し、GR86-REDUCE-COOKのcook/preflight/hash/診断へ接続する。CLIはその後、CUBICSPLINE/morphは別反復。

- G2-GR86-REDUCE-COOK: 明示optionsを骨格cook/preflightへ伝播し、source/buffer→import settings→policy/algorithmの同順hashを共有。Strict hash/wire不変、成功DecodeReport所有、失敗out保持とerror内の頂点/脱落量を追加。preflight成功はcook可能性を意味せず、静的meshへの骨格options指定は拒否。raw/GLB再読込・hash/閾値差・sidecar・旧Strict同値・失敗保持、decoder正常prefix後失敗/混在UNORMの恒久回帰を登録。純policy3mode実行成功、独立PASS。nativeはWindows.hで未実行。
- Next: GR86-REDUCE-CLIの明示指定とskip接続へ。共有JSON ImportReportは別taskとして保持し、stderr診断だけでGR86全体完了としない。

- G2-GR86-REDUCE-CLI: --skin-influences strict|reduceとwarn/fail閾値を別引数/equalsで厳密parse。重複/不正数/閾値だけ/非骨格指定を拒否し、順序に依らず最終検査する。cookとskipへ同じpolicyを伝播し、実cook時のstderr統計/警告を追加。純parser通常/O2-NDEBUG/ASan・UBSan（LSan除外）と-Werror MEMBER成功、独立PASS。5影響CLI成功/既定拒否/閾値失敗出力保持/同値cachehit・閾値差cachemissのnative smokeを登録。Main/native smokeはWindows.hとPowerShell/CMake依存で未実行。警告文/全診断値のnative assert強化はnonblocking候補。
- Next: cook/CLIを公開し、共有JSON ImportReportへ成功/失敗測定を接続する。CUBICSPLINE/morph/joint-policyは未実装のまま拒否。

- G2-GR86-IMPORT-REPORT: version1共通envelope/skinの所有JSONを追加。payload_ready（package書込前）/failed/cache_hitを区別し、cook独立診断出力で失敗resultを保持しつつstatus/prefix/失敗頂点を返す。CLIはAPI結果とcacheにstderr1行JSONを出し新file書込なし。第1周の未測定0混同を修正し、Strict/cache/総数不明はscan=null、prefix0のmax/mean=null、失敗頂点測定は独立保持。第2周PASS。pure3mode/Werror MEMBER/Python独立JSON解析成功、native診断/CLI JSON・警告閾値回帰はWindows.hにより未実行。Cubic/morph/GR84測定は将来拡張として明記。
- Next: ImportReport公開後、GR86 CUBICSPLINEの保守的誤差付きLINEAR焼込へ。実samplerの短区間処理とfloat丸めを評価し、保証不能は拒否する。

- G2-GR86-CUBIC-KERNEL: Hermite→外向き区間Bezier、deCasteljau評価/分割、保存float端点に対する理想LINEARの全区間L2/SO(3)上界を追加。quaternion符号を変えずE<mで非ゼロを認証し、角度上界4E/m+d²を保守的に計算。失敗out保持、左右alias拒否、入力片側alias許可。IEEE最近接/subnormal/精密FPを仮定しunsafe算術検出時拒否。純3mode/Werror MEMBER成功、3781認証ランダム曲線×201点の独立回転反証も違反なし。内部zero回帰を追加し規約の波括弧整形後、第2周PASS。binary64参照式の丸め偽陽性をtest専用予算で区別し、double参照版も成功（ULP級厳密包絡/Windows成功ではない）。実samplerのfloat/短区間/sidecar単位・decode/cook受理は次taskであり現段階CUBICSPLINE拒否を維持。
- Next: GR86-CUBIC-RUNTIME。全回転区間を数学dot下限>0.9996へ分割して実NLERP分岐を認証し、256*float epsilon radの保守的数値予算を条件付きで加える。保存float時刻の区間長/分割比も外向き区間で扱い、短dt/深さ/sample不足は拒否する。

- G2-GR86-CUBIC-RUNTIME: 保存float時刻の差/分割比を外向き区間に入れ、ValueScale適用後の数学上界と条件付きfloat予算で適応LINEAR列を生成。回転はnorm[.99,1.01]/dot下限>.9996を認証し実NLERP枝へ限定、256epsilon rad予算、vectorは64epsilon*端点L1規模。短dt/時刻衝突/zeroq/深さ24/sample容量を拒否し全out保持。元samplerのNormalize/Slerp/alpha式をportable private helperへ式不変で移し本物Math型と共有。pure3mode/Werror MEMBER/旧bounds回帰成功、第2周PASS。独立反証は成功534曲線・6,503,462 queryで上界違反なし。oracle hをlong doubleのまま保ち2^-60→1時刻差を追加。Core/pose/Windows全体はWindows.hにより未実行、別targetのFP条件は未確認と明記。Cubic decode受理はまだ拒否。
- Next: Cubic policy/hashを定義し、最終メートル倍率のdecode接続とCLI/JSONへ順に進む。

- G2-GR86-CUBIC-POLICY: Reject/Bake、メートル/ラジアン/無次元の許容、depth/channel/asset予算、prefix/失敗診断を定義。Bake無しStrictSize0/ReduceSRED25byteと旧hashを維持し、Bake時だけSCBK66byteへ全設定/cubic algorithmを格納。未使用指定・不正数値/enum/予算を拒否し、bakerも公開sample上限を共有。decoder接続前のBakeはUnsupportedInterpolationで明示拒否。純policy3mode/MEMBER・header独立POD・CLI/report/bake回帰成功、独立PASS（65,536 enum組合せ/256設定golden等も成功）。native raw拒否回帰は登録のみでWindows.h未実行。境界受理の恒久assert強化はnonblocking追補。
- Next: GR86-CUBIC-DECODEへ接続し、fitを含む最終translation倍率をbakerへ渡して二重scaleを避ける。

- G2-GR86-CUBIC-DECODE: 明示Bakeだけtriplet/count/型/rangeを共有bakerへ接続。channel/asset予算を割当前に制限し、正常prefix/種類別counter/失敗詳細/単位別上界を返す。fit倍率を未変換meshから一度だけ解決し、Bakeでは通常translationも早期scale、最後animation再scaleを省いて二重変換を防ぐ。既定Reject順は維持、status18を末尾追加。3種Cubic fixtureとraw/legacy/file/GLB/cook再parse、scale/fit、通常LINEAR/STEP同値、累積予算/zeroq/非有限/count不正をnative登録。第1周のnative試験namespace不整合を既存Reduce試験分も含めて修正し第2周PASS。実bakerによるfixture数値検証と純policy/bake各3mode成功。native decoder/cookはWindows.hで未実行。
- Next: policy/decodeを公開し、CUBIC-CLIの明示指定・単位別許容・JSON診断へ接続する。

- G2-GR86-CUBIC-CLI: --cubicspline reject|bakeと最終メートル/度/scale許容・depth/channel/asset予算を厳密parseし、既存のcook/skip共通optionsへ接続。JSON version1はBake時だけ単位付き設定/成功prefix/種類別上界/失敗channelと理由を追加し、未開始/cache/該当種類未測定はnull。旧Reject JSON不変、変換警告も追加。純parser/report各3mode・MEMBER・独立JSON11件解析成功、独立PASS。実bakerでsmoke fixtureの101/85/45key・sample予算失敗も照合。Main/nativeCLI/CMake/PowerShellはWindows.h等により未実行。既定拒否/失敗出力保持/設定差cache/JSONをnative登録。
- Next: 明示morph Dropのpolicy/hash・decode・CLI診断へ進む。sparse拒否と128関節を維持し、256はv1まで保留。

- G2-GR86-MORPH-POLICY: Reject/Dropと除去数量/scan完了型を定義。Drop無しのSize0/SRED25/SCBK66を保持し、DropだけSMDP(schema/policy/inner size/旧canonical/morph algorithm)17/42/83byteで包む。decoderは接続までUnsupportedMorphTargets、JSONもDropを旧Rejectと偽らず未対応とする。純policy3mode/MEMBER/public単独POD/CLI・report回帰成功、独立Python固定列・12hash照合と再実行PASS。raw/legacy/GLB/file/cook拒否native回帰を登録、Windows.hにより未実行。
- Next: GR86-MORPH-DECODE。明示Dropだけtargets/初期weights/weight channelを検証して除去し、base/TRSを維持する。Strict/skipの無言無視も防ぐ。

- G2-GR86-MORPH-DECODE: 明示DropでPOSITION/NORMAL/TANGENTのデルタと初期mesh/node weight・weight channelを検証し、base/TRSへ適用せず除去。weight専用Cubicは焼込不要、共有samplerはTRS側条件を維持。数量は検証完了後だけ確定し、Strictとpreflightは同じmorph存在gateで拒否。nativeに入口/cook往復/不正12種/sparse/非有限/出力保持を登録。第1周の試験API名とcompact JSONのURI除去を修正し第2周PASS。fixture全buffer範囲/12mutation文法/URI除去をPython確認、既存純policy/CLI回帰成功。nativeはWindows.hで未実行。完全validatorではなく、POSITION bounds実値照合・weight入力min/maxとanimation不正の細分理由は残る。LINEAR/STEP weight等の追加回帰はCLI taskで補強する。
- Next: MORPH-CLIで明示指定、cook/skip同一設定、数量警告、JSON未測定/検証済みと失敗を接続する。

- G2-GR86-MORPH-CLI: --morph reject|dropを厳密parseしcook/skipへ同じ方針を伝播。数量と除去警告、JSONのmorph_policy/morph_scanを追加し、未走査/cacheはnull、実測0件と後段失敗の検証済み数量を区別。純parser/report各3mode・MEMBER・18JSON独立解析成功、独立PASS。nativeには警告/出力保持/cache/0件、LINEAR/STEP weight・複数target・weightだけのclip拒否・TRS後段失敗を追加。nativeはWindows.h/CMake/PowerShell依存で未実行。
- Next: GR86前半の最終portable回帰と検証範囲を記録する。GR79/GR32/GR82へ進むためS3(a)材質レコードとS7のmesh受理範囲は作者判断待ち。

- G2-GR86-VALIDATION: 公開実装b2aa6ffの8 portable試験をg++14.2.0で再ビルドし通常/O2-NDEBUG/ASan・UBSanの計24実行成功（LSan除外）。実InvokeBundledMainを含む8 MEMBER wrapper compileとJSON各18件・計54件の独立構文/意味照合も成功。GR86ImportValidation.mdに実装済み前半、native/実物/GPU未実行、数値条件・morph制限、v1待ち256を分離して記録。独立監査で過大主張なし。0.2は計画中と明示した。
- Next: S3(a)/S7回答待ちの間、GR32/GR82の形式移行とauthoring rest保持の変更箇所を読み取りで棚卸しする。機能実装/新wireの適用は先取りしない。

- G2-GR32-GR82-MIGRATION-INVENTORY: 現行minor0/1・256B、1primitive/1clip、cooker/loader/resource/component/起動側の単一clip依存を実ソースで棚卸しした。NVSKELMigrationInventory.mdへ統一0.2への変更境界と旧版/中間/Stage A試験、v1作成時restの現在の欠落箇所・取得→変換→cook→parse→bind→reloadの追跡と必須拒否/明示許可試験を整理。コード/wire/受理契約は未変更。S3(a)/S7とARM/emissive/S5/S6を未決のまま維持した。
- Next: 現G2の自律的に進められる実装/準備は一区切り。S3(a)/S7の回答後にGR32/GR79/GR82の次taskを具体化する。未実行native/実物、surface保留、v1 rest guardは引き続き未完として保持する。

- G2-S3(a)/S7: 2026-10-04作者がNVMESH v1/128B材質/旧v0併読と1mesh/Nprimitive案Aを承認。決定記録/元Plans/選定taskを反映し、ARM/emissive/S5/S6/BVH/FBXは別途保留を維持した。
- In progress: GR32のplain submesh型とpacked範囲検証。受理拡張・wire・描画の接続前に、旧空表互換と1..8範囲/slotのCPU契約を固定する。

- G2-GR32-SUBMESH-CONTRACT: Rendering非依存のplain SkeletalSubMeshと名前slotをSkeletalGltfData末尾へ追加し、1..8のpacked三角形範囲/slot/u32制限を純kernelで検査。両表空は旧互換1範囲/slot0、片側空拒否、失敗数量0と入力非変更を維持。通常/O2-NDEBUG/ASan・UBSan（LSan除外）と実MEMBER wrapper compile成功。第1周でCTest登録抜けを修正し第2周PASS。decoder/wire/resource/描画の受理は未変更。native全体は未実行。
- Next: GR32-V02-SCHEMA。0.2の64B名前slotとGR79/v1の128B材質を分離し、header320Bと旧版互換・複数clip所有契約を固定してから接続する。

- G2-GR32-V02-SCHEMA: 旧定数/hashの5498Bを内容不変でpure公開headerへ分離し、統一0.2の320B/64B submesh/64B名前slot/複数clip所有契約をNVSKELv0.mdへ固定。版別profile/count/packed節/拡張header/raw hash入力長の純検証を追加。旧0.0/0.1の単一clip/256B/hashと現reader/writerは未変更。通常/O2-NDEBUG/ASan・UBSan（LSan除外）/実MEMBER成功、独立Python4hashと原文一致確認、独立PASS。Core全体はWindows.hで未実行。record値/文字列/padding/clip所有の実読込検査はIO接続時に残る。旧名ASCIIを維持し0.2のUTF-8/NUL拒否はwriter公開前に接続する。
- Next: GR32-PRIMITIVE-DECODE。1mesh/1skin/1mesh-node/1clipを維持したままprimitiveを連結し、slot割当とReduce/Morphの資産集計を整合させる。未接続の下流は表を捨てず拒否する。

- G2-GR32-PRIMITIVE-DECODE: 2〜8primitiveを連結し、local index境界→絶対番号化、追加範囲のみ巻き順交換、所有表/初出slotとsource identity順の名前衝突解決を接続。material省略とindex0は別。Reduceの全体prefix、morphの総target数/共通幅（mesh_target_width）を分離し、全geometry後に一度だけfit/Bake。preflightも全primitiveを検査。単一primitiveは中間段階の旧空表互換、旧writerは新表を明示拒否。nativeに2/8/9・全入口・名前/順序/省略・越境・prefix4/6・fit・morph幅を登録、Windows.hで未実行。純report3mode/54JSON独立解析とlayout/wire/policy回帰成功、独立PASS。
- Next: 0.2 IO接続に向けた版別文字列/参照検証→reader→writer/cache移行。新表付き資産のcookはwriter接続まで意図的に失敗する。

- G2-GR32-V02-TEXT: minor0/1 printable ASCIIとminor2厳密UTF-8/NUL拒否、UTF-16/32とCore Charのcode unit/byte数を区別する名前codecを追加。参照差分境界・overflow/alignment・全out alias拒否・全入力先行検証で失敗時出力保持。全1,112,063 scalar/4,382,591Bを独立Python UTF-8黄金hashと照合し通常/O2-NDEBUG/ASan・UBSan（LSan除外）全3mode・実MEMBER compile成功。旧wire/layout回帰も成功、独立PASS。実reader/writerおよびWindows/Core全体は未変更・未検証。
- Next: GR32-V02-READERで版別表・予約/padding・clip別所有と名前を接続し、未対応下流への黙示的な情報欠落を防ぐ。

- G2-GR32-V02-READER: 旧0.0/0.1を維持して統一0.2の320B/表/UTF-8・予約byte・packed padding/hashを接続。submeshのVertexCount/NoShadow/boundsを保持し絶対index範囲を検査。clipごとにjoint/path重複とdurationを検査し、channel/sampleを表順一意所有、失敗Dataは非公開。未接続M9は新表/複数clipを明示拒否。pure wire/record・layout各3modeと実MEMBER成功、独立レビューPASS。手書き2clip/2submesh/UTF8と破損回帰をnative登録、独立Pythonは配置/名前/所有/時刻だけ照合。native/Core全体はWindows.hで未検証。UTF8は長さだけでなく再encodeした7byte一致も試験する。
- Next: GR32-V02-WRITER。統一0.2生成・旧空表具体化・再parse照合とcurrent writer minorによるcache移行を接続する。Resource/描画の実使用は引き続き別工程。

- G2-GR32-V02-WRITER: 1primitiveもslot名を保持し、統一0.2/320B/表/UTF-8/new hashを生成、再parseで表と名前を照合。旧空表は全index/Default slotへ具体化、静止頂点boundsは最終scale後に計算。cacheはminor2必須として旧0/1はload互換のみ。第1周で共通JSONのsurrogate未結合を発見し、pure escape/scalar部品で結合・不正拒否・4byte UTF-8を修正、第2周PASS。pure bounds/layout・JSON各3mode計6runと実MEMBER2件、wire/name回帰成功。全scalarのUTF8黄金hash、cache fixture JSON/配置も独立確認。raw/GLB→cook/parse・単一slot/Unicode名・旧cachemiss/新hit・出力保持はnative登録、Windows.hで本体/CLI未実行。M9新表guardはResource/描画接続まで維持。
- Next: GR32-RESOURCE-TABLES → COMPONENT-SLOTS → DRAW-RANGES。CPU契約と描画実装の接続を進め、Windows/GPU受入れはblockedの別gateに保持する。

- G2-GR32-RESOURCE-TABLES: SkinnedMeshResourceがsubmesh/slot名を所有し、geometry/有限bounds/頂点範囲/名前をLoad・Refreshで検査して不変leaseへコピーする。旧3引数/空表互換と、generation/非active/Unload後のsnapshot寿命を維持。新表FrameLeaseは範囲draw接続まで明示拒否し、情報を単一drawへ落とさない。純実index/metadata/layout3mode・MEMBER成功、独立PASS。native1/2/8表・不正slot/NUL・直前generation比較・明示表Unload後保持を登録、Windows.hでResource/Core/描画実行は未検証。
- Next: GR32-COMPONENT-SLOTS。材質slotの名前/index API、旧材質fallback、世代差し替え時の安全なbindingとframe値所有を接続する。

- G2-GR32-COMPONENT-SLOTS: index/名前APIと旧slot0別名を接続し、未割当slotはslot0へfallback、slot1以降のoverrideをmesh世代へ束縛。固定Materials[8]/MaterialCountをproxyへ値コピーし、SceneRevisionも材質列を反映。第1周でnative snapshotの既存null device・packet検証不足を確認し、実CPU生成をcapabilities引数へ分け、再同期/世代差し替え/破棄後の2slot保持を追加。原文のslot0/固定配列仕様へ整合させ第2周PASS。純binding3mode・MEMBER成功。native Component/packet/RevisionとGPUはWindows.hで未実行。
- Next: GR32のcook metadataへ表数量を追記し、範囲drawへ進む。palette共有は現/前フレーム2本1組の解釈を作者確認中で別taskに保留し、独立した部分を継続する。

- G2-GR32-COOK-METADATA: 再parse済みpayloadの表数量をcook結果/CLI/manifestへ保存。submesh_count/material_slot_countは組で任意、存在時は1〜8/index三角形数と照合し、旧省略はbool=false/0の未知として保持。mergeで既知数量を落とさず旧省略を捏造しない。純数量/layout3mode・MEMBER、既知/未知fixture JSON独立確認、独立PASS。native parser1/2/8・不正型/片側欠落、cook1/2/8、CLI1/1、audio merge既知保持/GLB merge省略保持を登録。Windows.hでnative/CLI/CMake/PowerShell未実行。
- Next: GR32-DRAW-RANGES。palette allocationの作者回答を待ちながら、独立した範囲draw/材質slot/NoShadowと検証へ進む。

- G2-GR32-DRAW-RANGES: immutable lease生成時に全表/名前を検証し、private appenderで1proxy/1frame leaseからsubmeshごとの範囲・材質・NoShadow commandを発行。記録前にtable/範囲/base0/component/prepared VB・IB・総数・前palette組を照合しDrawIndexed(count,start,0)、統計も範囲数へ。旧空表count0の全mesh互換、同handle別assetのupload混入拒否、tagged preparedのframe関連付けを維持。M9は表をResourceへ渡し、複数clipだけ保留。独立PASS、純range3mode/MEMBERとlayout/binding回帰成功。別exeのCPU RHI double契約に1/2/8・旧実記録・材質fallback・影・偽装拒否を登録、nativeはWindows.hで未実行。GPU readback/画素互換/M9実行は未検証。palette共有は未実装・作者回答待ちでGR32全体完了にはしない。
- Next: paletteの2本1組共有は作者判断待ち。Windows/GPU受入れも保留し、次のGR79の未決条件と共有材質wireを確認して、判断不要の準備を先に進める。

- G2-GR79-MATERIAL-WIRE: 共有128B材質recordをRendering非依存の明示little-endian codecへ固定。4StringRef/予約/flags/alpha/係数/発光の有限・範囲・Yとraw/再正規化後の65504上限を検査し、失敗時出力保持とalias拒否を保証。独立Python128B golden、通常/O2-NDEBUG/ASan・UBSan（LSan除外）3modeとMEMBER compile成功。独立レビュー第2周PASS。現container/parser/runtimeは未接続でARM/nits既定も未決。Windows/native/GPUは未実行。
- Next: 作者承認済みのGR32 palette共有。currentはcomponent・frameで1回、previousはGBufferのみ最大1回、影はprevious無し、パス順序非依存。

- G2-GR32-PALETTE-SHARING: 作者訂正のcurrent1/previousはGBufferのみ最大1に沿い、componentとepochでcurrentを共有しpreviousを遅延作成。影のpreparedは常にprevious無し、両順序で同currentを再利用。pose/handle/世代/asset実体一致、失敗時再作成禁止、全登録viewport leaseとsubmitted serialの寿命、tagged epoch/パス/使用flag照合、旧匿名跨frame互換を保持。独立ソースレビュー2周PASS。両順序・8範囲・viewport・別component/pose/世代・失敗回数・current/previous保持解放・偽装拒否を独立native契約へ追加。既存pure範囲3mode回帰PASS。新cache本体の実行はWindows.hで未検証、GPU受入れも未実施。計画の旧CreateBuffer総数2箇所を訂正。
- Next: GR32-POINT-SHADOW-BUDGET → POSE-HISTORY-GENERATION。GR32全体/GPU gateは未完了。

- G2-GR32-POINT-SHADOW-BUDGET: 点光源の各light/faceで16componentのUBO/descriptor表を共有し、submesh数で枠を増やさない。17番目は全範囲省略、失敗枠は再試行せず、epoch/handle/世代/palette/VB照合と旧匿名palette実体の区別を保持。全600枠の従来容量は不変、各face独立。実storage helperはpreviousを拒否し8current/9VBだけ設定。純16component×8範囲×6face・容量/失敗/identity/匿名テスト3mode成功、独立PASS。実Coreへ768draw/descriptor16face/binding10無しを登録、Windows.hでnative/GPU未実行。ShadowMapPass本体のUBO/faceData接続はソース確認のみ。
- Next: GR32-POSE-HISTORY-GENERATION。前姿勢の資産世代を照合し同骨数reloadの混同を防ぐ。

- G2-GR32-POSE-HISTORY-GENERATION: GT/RTの前姿勢を共通SkinnedPoseHistoryへ集約し、componentに加えてhandle/generation・immutable asset実体weak・骨数を照合。同骨数reload/別資産/不正frame/匿名をfallbackし、submesh/viewportは1回保存、一致しない混在は次frame不使用。GT直前frame/RTgapの時間条件とmesh/MegaGeometry経路は不変。2viewport8submesh・pose/資産混在・null/component不一致・gap/reset/weak寿命をnative登録、独立ソースレビュー2周PASS。Windows.h依存でnativecompile/実行/GPU未検証。
- Next: GR32の描画受入れはWindows/Vulkan gateへ残す。承認済みGR82 Stage Aの複数clip取り込み/選択を進め、旧strict入口の拒否互換を保持する。

- G2-GR82-A1-MULTI-CLIP-DECODE: 新DecodeRigGltfだけanimations>=1を受けcookへ接続。旧bytes/String/GLTFAnalyzerのstrict TwoClips/中間親拒否は維持し、現128joint/1mesh/1skin/1mesh-node/8primitive範囲を保つ。全clip順のCubic total/prefix/失敗添字・共通出力予算、全clip成功時complete。Morph rootは1回・weight channelsは各clip検査し全成功後のみ報告公開。旧設定hash/JSONキー不変。3clip(2primitive/duration2・3・4)raw/GLB/cook/parse・3本目失敗保持・Cubic全体予算/scale・Morph集計/後続失敗をnative登録、AssertEquivalentも全clip比較。pure report通常/O2/ASanUBSan(LSan除外)3mode・各19JSON計57parse・MEMBERcompile成功、fixture宣言範囲独立確認、独立PASS。実Core/decoder/cook/GLBはWindows.hで未実行。
- Next: GR82-A2-CLIP-RESOURCES。v1/restguard・M9複数clip接続は別工程。

- G2-GR82-A2-CLIP-RESOURCES: SetClipResourcesでclip列をコピー所有し、単数SetResourcesと安定した先頭GetAnimationClipを維持。index/一意完全一致name引き、空/欠落/重複拒否、全子Loaded/Valid、Unloadとcapacityメモリ計上を接続。第1周でStringViewのNUL終端比較を検出し、長さ+全codeunitへ修正。実保持NUL後B/Cと誤一致/誤重複、子内容増加時の束メモリ不変も反証追加し第2周PASS。新SkeletalAssetResourceTestをMEMBER/CTest登録、旧LifetimeTest無変更。Windows.hでnativeコンパイル/実行は未検証。M9/SkinnedMeshComponent/選択alias/v1は未変更。
- Next: ARM/nits既定は作者回答待ち。GR79 NVMESH v1の形式/検証を独立に進め、受理後の材質情報をruntimeで黙って捨てないよう接続gateを分離する。

- G2-GR79-IMPORT-POLICY-CORE: 作者追加指定のAI profile AO/metallic ignore・roughness auto、素材mode上書き、histogram Type7のp1〜p99有効幅(既定4/255)で定数判定を純処理化。診断min/max/meanは維持し、AOは1+strength*(sample-1)。emissiveFactor×strengthの非0なら換算明示必須、textureだけ/strength0は非発光。素材>asset>asset-setの選択、不正高優先値のfallback拒否、Y正規化/physical nits/codec上限を固定。第1周の中間underflowをmax成分正規化+frexp/scalbnで修正、第2周PASS。通常/O2/ASanUBSan(LSan除外)3mode・MEMBER成功、300sorted列の独立百分位照合・巨大count・極小factor回復・byte恒等を確認。JSON/asset-set/CLIと資産名/材質名診断、実画像・撮影・runtimeは未接続/未検証。
- Next: GR79 NVMESH v1の純wire検証 → reader/runtime明示gate → material設定/画像処理/診断・asset-set指定とwriterへの接続。生成元は明示profileで扱い、GLB拡張子だけで推測しない。

- G2-GR79-MESH-V1-WIRE: v0定数blockをbyte不変の純headerへ分離し、NVMESHv1/major1minor0/Header256/Material128/Cluster128/8B整列を定義。外枠のprofile/節範囲・packed/padding/FNV/予約/有限boundsとLOD0 clusterを純検証。第1周でVertexCountの絶対index上限をunique128と混同していた制限を修正し、Multi goldenもcap6へ訂正、第2周PASS。独立Python580/692/1120B、通常/O2/ASanUBSan(LSan除外)3mode・MEMBER成功、cap129/三角形128境界・Multi実index照合を追跡。旧full parser/cookerはv0のまま、v1 reader/runtime/GPU未接続。
- Next: GR79 v0/v1 readerとruntime gate。v1単材質も係数adapter未接続では受理しない。全submesh/index/clusterの所有と材質対応、unique頂点数128と任意絶対上限を分けて検査する。

- G2-GR79-MESH-V1-READER: v0の受理条件を保ちPbr既定/ARM3chへ昇格、v1を4ref/Pbr/全finite/表所有/材質対応/実indexとunique128付きで読込。全成功時だけBlob所有を公開。ModelAssetLoaderはv1(単材質含む)/N>1をログ拒否し、旧手組み空submeshの搬送は維持。pure partition3mode/MEMBER成功、wire回帰成功。独立第2周PASS、nativefixtureは4異なるpath/相対offset0,3,6,9・係数bit・寿命・不正群・runtimeguardを登録、Windows.hで未実行。v1 writer/manifest/runtime adapterは未接続。
- Next: 既存Core::Resource class/namespace衝突を別taskで修正。CookedSkeletalAssetTestの同TUincludeに既存の衝突があり、言語最小例でも拒否を確認。新V1Testはそのclassをincludeせず今回の新規衝突ではないが、bundle全体のbuildを妨げるため先に除く。S6 C++asset-set方式は作者回答待ち。

- G2-RESOURCE-IO-NAMESPACE: Core::Resource基底クラスと読込namespaceの既存衝突をResourceIOへ分離。29既存ソースは逆置換でHEADとbyte一致、基底/反射/継承実装は不変。全追跡sourceの旧namespace残存ゼロ、両include順の実native契約をMEMBER/CTest登録。独立レビューPASS、行末/diff検査正常。Windows.hでnative compileは未実行。独立言語例の両順成功は実Core成功とは扱わない。
- Next: GR79材質設定のtyped/JSON/hashと接続を具体化。G2-S6 Aは作者が追加条件付き承認、単体CLI分割前後および旧ps1 spec v1の実byte一致は未検証gateとして保持する。

- G2-S6-CLI-BYTE-SMOKE: 既存raw/texture/audio/mesh/GLB/import/骨格の7 smokeを変更せず実行するcaptureとpackage/manifestの非正規化snapshot比較を追加。既存出力拒否・case全成功/生成物必須・recipe固定/実行中変更拒否・改竄/一覧/byte差を検査。比較器10 unittest通常/O成功、独立第2周PASS。CMakeのsource列挙は分割で変わるためrecipeから除外し、rawコマンドはdriverSHAで固定。Windows実CLI/前後byte比較は未実行でMain分割gateは未達。
- G2-S6作者訂正: origin/main CookTextureAssetSet.ps1とSilverTextures/SilverGltfTexturesの2specを比較元に確定。手元確認用CookAssets.ps1/StartupMaterialsは対象外、比較元未発見の保留は解除。glTF外部ファイルとsidecarを増分印へ含める。実texture spec v1互換確認は未実行。
- Next: GR79材質設定の解決/JSON/hashへ進み、一括cook実byte受入れは別gateで保持する。

- G2-GR79-MATERIAL-SETTINGS-VALUE: 明示profileとchannel存在mask、素材>資産>既定、発光換算の素材>資産>asset-setを解決し、存在する不正下位値も先行拒否/出力保持。両面・alphaのInherit/FromSourceを区別して親強制から復帰可能。換算不在はImportEmissionの発光判定まで許容。解決済み67B canonical（未使用値/-0正規化）とdomain/長さ付きFNVを追加。独立Python golden/FNVff4637ca142e511e、通常/O2/ASanUBSan(LSan除外)3mode/MEMBER、独立5283反証PASS。旧幾何52B/hash/file/Mainはbyte不変。JSON/素材selector/file/asset-set/cook/cacheへの実接続は未実装。
- Next: GR79材質設定の厳密JSONと素材指定の照合を接続し、未知/重複/名前不一致を無言適用しない。実CLIのMain分割前gateとWindows実比較は未達のまま保持。

- G2-GR79-MATERIAL-SETTINGS-JSON: 設定block用の実JsonDocument parserを追加し資産profile/ARM各mode・constant/両面/alpha/nitsを厳密に型・範囲・未知・重複検査、全成功時だけprofile/layer置換。素材blockのprofile指定は拒否、空/省略は継承、換算不在は保持。pure ARM tokenのASCII全消費・finite0..1/非終端span/NUL/末尾余剰を通常/O2/ASanUBSan(LSan除外)/MEMBERと独立guard-pageで反証PASS。30JSON fixture構文をPython確認、native JSON MEMBER/CTest登録はWindows.hで未コンパイル/未実行。旧ParseSettings/LoadImportSettingsFile/Mainはbyte不変、sidecar非空materialの明示拒否を維持。独立レビューPASS。
- Next: 素材selectorは08:06作者回答待ち。一方glTF sourceの材質係数/発光strength/texture参照の読み取りは独立して進められる。sidecar/cook/cache/asset-setの実接続・Windows実比較・実物撮影は未完のまま保持する。

- G2-GR79-GLTF-MATERIAL-SOURCE: glTF PBR/normal/AO/emissiveFactor+KHR emissiveStrength/alpha/両面と5texture参照をdouble source値へ読み取るAPIを追加。既知field重複/型/範囲/キー・name NUL/参照範囲を検査し成功時だけ公開。texCoord0限定・textureInfo.extensions存在拒否は旧cooker同様。optional未知材質拡張はobject検査後fallback、document必須拡張gateは不変。公式schemaのnormalScale負/alphaCutoff>1/strength0以上を保持。純数値/参照/極小factorからの発光回復を通常/O2/ASanUBSan(LSan除外)/MEMBERと独立実行でPASS。nativeJson42fixture構文確認・MEMBER/CTest登録、Windows.hにより実JSONコンパイル/実行は未検証。独立レビューPASS。旧cook/profile/emission policy無変更。
- Next: GR79(6)再読でdoubleSided autoをFromSourceと名付けた値層の不整合を発見。別taskでAutoと明示し、geometry判定待ちのmodeを保持する。素材selectorは作者回答待ち、source→cookの実接続は続く工程。

- G2-GR79-DOUBLE-SIDED-AUTO: GR79(6)へ整合させprivate DoubleSidedSettingのFromSourceをAutoへ訂正。元値復帰ではなく後段の位置溶接・閉鎖性判定待ちmodeを保持する説明に修正。alphaのFromSource、enum数値1、67B canonical/FNVは不変。4source/testは逆rename+comment除去でHEADとbyte一致、通常/O2/ASanUBSan(LSan除外)3mode PASS、独立静的review PASS。geometry判定・JSON実行・cook/renderer接続は追加していない。
- Next: 素材selector回答待ちの間、ARMの画像pixelからhistogramと焼込出力を作る処理を接続する。glTF読込sourceと設定値の準備は済み、cook/cache/runtimeの受入れは未完。

- G2-GR79-ARM-PIXELS: 解凍linearRGBA8 AO(R)/MR(G,B)を実histogramへつなぎ、全定数ならARMなし、残るtextureのみ同寸法検査してfactorを焼込。省略textureは白sample/HasSourceImage=false、実測と区別。activeMR/なければAOのalphaを保持、factor1全textureで全RGBA byte不変。input/policy/factor/view/planのalias・範囲/overflow・不足出力を先行拒否し失敗保持。第1周で半端量子化の1byte下振れを発見、byte領域BakeとQuantizedScalarへ修正し、素材定数も正規化往復しない。通常/O2/ASanUBSan(LSan除外)/MEMBER成功、第2周PASS。全256値×全8mask×quarter factorsと独立混色312120例/constant768例、合成AI犬の外れ値/metallicignore/textureoverrideを確認。画像IO/NVTEX/manifest/実cook/実物/GPUは未接続・未検証。
- 作者09:00追加条件: 共通selectorをGR79/GR78 SurfaceName/GR32 slot名へ共用、未一致・同材質への名前/番号の二重指定拒否、同名GLBへ改名推奨警告。TRELLIS/Pixal素は無名1、Blender経由Material_0。元indexとslot初出index、元名と生成名は分けて扱う。
- Next: 共通resolver。raw/escape名のTCHAR幅問題も別taskへ追加。現ANSIではUTF8 bytes一致、wideには既存JSON数値tokenのcompile問題とbyte拡幅/char lexer縮約がある（実native実行でなく実ソースと純helper言語例の確認）。

- G2-MATERIAL-SELECTION-SHARED（GR79/GR78/GR32共通核）: strictUTF8/NUL拒否の一意元名・番号+期待元名をResolveMaterialSelectionへ集約。元catalogと生成slotを別domainとし、行順とidentity順を区別。未一致/曖昧/同target二重指定拒否、全catalog同名警告情報、失敗時出力保持/alias検査。第1周で後続未一致の診断に前行が残る問題を修正、回帰追加で第2周PASS。通常/O2/ASanUBSan(LSan除外)/実MEMBER wrapper compile成功、独立100000例とroot20000例の総当たり参照比較一致。nativeCore/GR79設定/GR78SurfaceName/GR32component接続は次工程で未完。
- Next: 作者10:02承認のWindows標準CIで実CLI基準採取を準備し、common resolverの設定/runtime接続も続ける。Main分割前baseline、--asset-setと旧main2specの実byte一致は未達。

- G2-S6-WINDOWS-CLI-CI: 作者10:02承認に基づきfeature限定Windows2022 workflowを準備。最小read権限/credential非永続、公式固定Vulkan SDK checksum検証+debug/copy_only導入、実Core/AssetCook/CookedMeshTestのReleaseビルド、CPU契約と既存7smoke capture、exe hash照合/明示artifact保存。YAML構造と比較器10単体は確認済み。初回run/実build/7smoke/保存物は未確認でdoingを維持。
- 独立workflowレビューPASS。alwaysログの未設定env参照を固定runner.temp/runId/attemptへ修正。実行成功の証明はpush後のrunで確認する。
- Windows CI初回run37194998794はSDK汎用名checksum APIの404で導入前停止。公式掲載の実ファイル名と固定SHAへ修正し、公式vulkanホストの実名receipt HTTP200/値一致を確認。ビルド/試験未到達、baseline未生成。

- G2-GR32-MATERIAL-SELECTION-ADAPTER: SkinnedMeshComponentのslot検索をstrict native→UTF8変換+GeneratedSlot共通resolverへ接続。元番号とは混ぜず、返るcatalog行をslot IdentityIndexへ変換。独立callback照合を廃止し、Default/一意空名/最大8/重複拒否、番号APIと世代fallbackを維持。純bindings通常/O2/ASanUBSan(LSan除外)/MEMBER wrapper compile成功、native snapshotへNUL query拒否を追加、独立review PASS。実component/native試験は未実行（現在のWindows runにはこの差分は含まれない）。公開旧helperをリポ外利用するコードは新UTF8catalog APIへの移行が必要。
- Next: GR79設定とGR78SurfaceNameのcatalog/selector接続。WindowsCIの基準採取結果も確認継続。

- G2-S6-NATIVE-TEST-COMPILE: run37195379624の実WindowsログでCore.libとAssetCook.exe生成成功を確認。CookedMeshTestはAnsiStringView==literalのtemplate推論不可(C2678)とWindows nearマクロによるlambda名消失(C2513)の2件で停止。前者を長さ3+memcmp、後者をisNearへ変更し、逆差分が親とbyte一致することを確認。値/許容幅/検証対象は維持。実再ビルド・CLI smokeは未確認のためdoing。

- G2-JSON-UNICODE-INPUT: strict UTF8 byte入口とUTF8/16/32 native検証をJsonDocumentへ追加し、lexerのchar縮約/広い空白判定/数値String型を修正。glTF/GLB/sidecar/cookのJSON入口6か所を接続、BOM/container/BIN借用/path変換は維持。第1周でescaped NUL後のTString容量拡張破損を指摘され、文字列単位のsource上限を事前scan/reserveして長いkey/value/node増加回帰を追加、第2周PASS。全Unicode scalar3幅の通常/O2/ASanUBSan(LSan除外)/MEMBER compileと独立JSON fixture確認成功。実JsonUnicodeInputTestとCI登録済み、native実行前なのでdoingを維持。Core全体のUNICODE構成成功とは区別する。
- G2-S6-NATIVE-TEST-COMPILE: run37196379322で2件修正後のCore/AssetCook/CookedMeshTestビルドと指定7CPU契約成功を確認し、この修正taskはdone。実CLIはRaw/Texture/Audio/Mesh成功、Glbのverify.ps1内Get-FileHash不在で停止。基準snapshotは未生成、Main分割gateは未達のまま。

- G2-S6-SMOKE-ENVIRONMENT: MicrosoftのPS7→Python/中間process→Windows PSのPSModulePath継承問題に沿って、driverがWindowsの子環境辞書だけから当該keyを大小文字非依存で除去。親/他変数/Skeletal memberを保持し12単体通常/-Oと独立review PASS。smoke command/fixture/生byte条件は不変、成功基準前のdriver hash更新を明記。Python UTF8 modeもCIに固定。実Glb/Import/Skeletal再実行と基準snapshotは未確認のためdoing。

- G2-JSON-UNICODE-INPUT実Windows検証: run37197864299で更新Core/AssetCookはビルド成功。JsonUnicodeInputTestのraw literalをCHECKの#式へ直接渡した箇所がMSVC C2017/C3688となったため、同一literalを局所変数へ移しmacro文字列化の対象から外す。fixture byte/期待値は不変。native試験/7smokeは未達、doingを継続。

- G2-MATERIAL-IMPORT-PLAN: 新document/元index+UTF8名catalog/解決済みplanを値所有化。幾何52Bと旧loaderの拒否を維持し、新file入口だけが材質設定を読む。全rowのARM/発光/surfaceを共通resolver1回へ渡し、名前/番号二重指定・不在を拒否。無名/Material_0・catalog順とsource順・重複診断source index・寿命/後段失敗保持・legacy/BOMをnative試験へ登録。45JSON literal構文確認、独立review PASS、非blockingの不正手組みcatalog/後続layerとsurface失敗保持も補強。実nativeは未実行のためdoing。CLI/v1 cook/cache/SurfaceName永続化は未接続。
- run37198756161でCore/AssetCook/CookedMeshTestビルドと9CPU試験成功を確認。G2-JSON-UNICODE-INPUTはnative ANSI/TCHAR経路も合格してdone（Core全体UNICODE構成は未検証）。PSModulePath修正でGlb/Importも通り環境修正taskはdone。全7smokeはSkeletalのfixture helperがextra_zero.binにfixture.bin固定needleを当てて停止し未完。出力/受入れ条件は緩めず別修正する。

- G2-S6-SKELETAL-FIXTURE-URI: 実run37198756161の最終Skeletal停止を特定。ChangeBufferUri呼出順の先行fixtureはすべてpretty fixture.bin、ExtraZeroInfluencesだけextra_zero.binで固定needleに不一致。期待URI引数（既定fixture.bin）と一意一致assertを設け、該当callだけextra_zero.binを指定。source fixture/binary/エンジン判定は無変更。実再試験まではdoing。

- run37200047966/d1307c32: 実Windows ReleaseでCore/AssetCook/CookedMeshTest、12+12比較器試験、10CPU契約、Raw/Texture/Audio/Mesh/Glb/Import/Skeletal全7CLI成功。before artifact11302304928（SHA256 7491b7178b1be87f79269624c337183629e770145b41ea6ca8477521f642d385）を外部検証storageへ保存し、79file=50package+29JSON全hash/size/一覧と2exe hashを独立検証＋root再確認。recipe31項目はgit blob直接20/WindowsCRLF11で説明可能。Main分割前gateを閉じ、材質import-plan・fixtureURI・WindowsCI基盤taskをdone。分割後byte比較、--asset-setとorigin/main2spec比較、依存hashは未完。
- Next: G2-S6-MAIN-SPLIT。argv/help/inspect外殻と単体cookサービスの境界を設け、同一79出力を実比較する。現Mainの2993行・振る舞いは基準採取時まで不変。

- G2-S6-MAIN-SPLIT: Mainを317行のargv/help/inspect外殻へ縮小し、単体cookサービスとprivate出力処理へ既存bodyを移動。公開requestは値所有、既存validation/dispatchを保持。機械的body照合・比較器12件・行末確認成功。固定before取得/recipe照合/79出力比較と日本語診断5literalのbyte照合をCIへ接続。実after検証前なのでdoing。

- G2-S6-MAIN-SPLIT受入: e8ac121ce8d2e9b92a5469ec321adb00b8a92ff3/run37203280358で実Core/AssetCook/CookedMeshTest build、11CPU、7CLI成功。保存したcandidate artifact11304570033とbeforeの全79file=50package+29JSONを独立raw byte比較し完全一致、recipe31項目・5診断literalも一致。rootで比較器を再実行して一致確認。単体分割taskをdone。--asset-set/texture v1/増分依存は別gateのまま。

- G2-S6-TEXTURE-BASELINE開始: upstream main b8c5df1のPS1/2spec/8source画像をblob固定。分割前exeとWindows PowerShell5.1を明示した専用job、2回全byte照合と10file snapshot採取器を追加。純拒否契約11件の通常/最適化とYAML parse成功。実Windows採取は未実行のためdoing。

- G2-S6-TEXTURE-BASELINE受入: 0893a213/run37205025949の実Windowsで固定旧PS1・2spec各2回cook成功、合計16texture cook。Windows PowerShell5.1.20348.5622、11入力blob/checkout、固定exe不変、8package+2manifestの2回byte一致を確認。保存artifact11303728946の全10file=296408686byteを独立検証＋root hash再確認。texture v1互換の比較元が確定、native --asset-setとの比較は次工程。

- G2-S6-TEXTURE-SPEC開始: v1の値所有parserと独立Json整数token情報を追加。旧variant/prefix/usage無視を保持、出力path安全制約とO(NlogN)重複検査、実2spec/寿命/失敗保持のnative契約を登録。単体CLI比較器12件は成功。実native parser試験前なのでdoing。

- G2-S6-TEXTURE-SPEC実検証: 441eeed/run37206606155は実buildと既存11CPU/診断byte成功、新spec試験のJson生成helperで停止（実2spec試験へは未到達）。TString::replaceがmemmove後にstrncpy_sの終端NULでsuffix先頭を潰すことをsourceで特定。fixtureだけを3区間appendへ変更し、失敗時hex記録を追加。汎用文字列修正は別taskに起票。parser/fixture taskとも実再試験までdoing。

- G2-S6-TEXTURE-SPEC/FIXTURE受入: f3100ed/run37207785735で実build、12CPU（実Silver2spec/8row全所有field・UTF8・失敗保持）、7CLI、79出力byte一致、診断5literal一致を確認。fixture修復後にparser本体無変更で全合格。両taskをdone。--asset-set実行/集約serializer/増分印は未接続、汎用String::replace修正は独立TODO。

- G2-S6-TEXTURE-BATCH開始: CLI/サービス/PS5.1集約serializer/同volume no-replace新規root公開を実装。全入力先行検査、private stage、単体manifest所有、最終cooked-only再解決を接続。固定10file×2回と全ASCII PS実probe、failure/既存root/junction/競合検査をCIへ追加。Python比較器4件成功、実native検証前のためdoing。既存root増分とproduction caller切替は後続。

- G2-S6-TEXTURE-BATCH受入: 79d019d/run37210701091の実Windowsで13CPU、単体7CLI/79byte/5診断、native2spec×2/10file、全ASCII PS5.1実probe成功。25native起動=9成功/16拒否を確認し、日本語source/cwd、junction、既存root、late画像不正、prefix衝突、公開先競合を検証。native artifact11306368465（SHA2568797453fad3cf42e8bc354300726984c24fa2a704885e97858bbe3a158ef368f）の10file296408686byteを独立全byte比較しroot再確認。新規root用v1 sliceをdone、既存root増分とglTF/sidecar stampは未完。

- G2-S6-DEPENDENCY-SNAPSHOT開始: source/glTF外部buffer・image/選択sidecarの生byte・presenceと要求/revisionを印へ集約。sidecar loaderに同じreadのRawSourceBytesを保持し、外部readerのcanonical path出力を共有。root/設定/URIの既存runtime SourceHashは不変。実file変更・不在復帰・policy・permission・失敗保持のnative契約を登録、実Windows前なのでdoing。Skip決定と既存root公開は未接続。

- G2-S6-DEPENDENCY-SNAPSHOT実検証: edc9a5c/run37213672528は実build/診断と15CPU成功、新依存試験がbuffer解決失敗で停止。コピーしたM9Skinnedに必須fixture.bin（416byte）が無いことを確認。既存skeletal試験の生成bodyをbyte書込callback付き共通headerへ機械移動し、新依存試験でも同じbufferを生成する。既存bodyの逆置換一致を確認。依存実装は変更せず、失敗時source表示を追加して再検証する。

- G2-S6-DEPENDENCY-SNAPSHOT受入: f96dc155/run37215038245の実Windowsで16CPU（snapshot/sidecar loader/外部buffer readerを含む）、単体7CLI/79byte/5診断、native2spec×2/10fileとPS5.1・Unicode・16拒否契約が成功。logs/native archive SHAと実行driverを確認。fixture補完後に依存collectorを変更せず合格しtaskをdone。Cook/Skip/Errorの共通決定、stamp永続化、既存root増分公開は別gate。

- G2-S6-OUTPUT-PACKAGE開始: 共通decisionに先立ち、読み込み済みpackageの型別照合を小さく分離。V1単一entry/数値hash/4texture形式/audio/mesh v0/骨格v0.2と6数量、全wrapper印を検証する。実単体cook、版別既存golden、padding差・失敗保持のnative契約を追加。純比較器12件成功、実Windows前なのでdoing。既存cache/Skip/path解決は変更しない。

- G2-S6-OUTPUT-PACKAGE受入: 7ba94fed/run37218137820の実Windowsで18CPU（新しい型別package検証とmesh v1拒否を含む）、7CLI/79byte/5診断、native texture2spec×2/10byte/16拒否が成功。単体cook全形式・派生画像3件・正常旧骨格版の拒否・padding印・失敗保持を実証。3 artifact ZIP SHAを確認しcandidate79file/2exeを独立検証。taskをdone。保存recordとの比較・共通Cook/Skip/Errorは未接続。

- G2-S6-CACHE-DECISION開始: 単体要求の正規化を既存private境界へ集約し、現入力からの完全な出力一覧・値所有record・共通Cook/Skip/Errorを追加。保存pathは採用せず、source/不在sidecar/他keyのaliasを先行拒否し、依存を操作前後で再採取する。全kind・外部依存・manifest無関係変更・Windows alias・途中入力変更のnative契約を登録。永続化と既存root公開は未接続、実Windows前なのでdoing。

- G2-S6-CACHE-DECISION実検証: 48bdd655/run37220986849は実buildと既存18CPU・5診断が成功、新cache試験の危険出力名拒否（line203）で停止。Windowsのabsolute化より前にraw物理名の検査を追加し、末尾dot/space等が正規化で消えてから検査される経路を塞ぐ。packageの各leaf診断とmanifest名の対称回帰を追加。判定条件は弱めず、79+10byte gateは未到達のためdoingを維持。

- G2-S6-CACHE-DECISION受入: cc9aa9e1/run37222258176で実Windows19CPU・7CLI/79byte・5診断・native2spec×2/10byte/16拒否が成功。保存record→Skip、全kind、外部依存・sidecar・他key、hardlink/case/junction、Cook/Skip中とrecord採取中の入力変更・保持を実証。実traceでa.とa空白がraw/aへ、CONがdevice pathへ、a:streamがdrive pathへ変わることを確認し、raw表記を先に拒否する修正も合格。3ZIP SHAと固定79+10出力の直接byte比較を独立検証。共通in-memory判断をdone、state永続化・既存root公開・旧CLI cache移行は未完。

- G2-S6-COOK-STYLE開始: 新規出力/cache検証10fileを規約の制御文brace/Allmanへ整理。bool識別子とheaderの明示修飾以外はbraceを除くtoken一致を確認。旧単体cookerは新設adapter範囲だけを対象とする。挙動の受入れは既存Windows gateで再確認する。

- G2-S6-COOK-STYLE受入: e3c919e9/run37224909032で実Windows19CPU、7CLI/79byte/5診断、native2spec×2/10byte/16拒否が成功。3 artifact ZIPと79+10出力の固定基準との直接byte一致を独立確認。書式と明示名修正がcook/増分判定の挙動を変えていないことを確認しtaskをdone。

- G2-S6-MODEL-NATIVE-PATH開始: source/sidecar locatorをnative pathのままfingerprint・実cook・検査・骨格decodeへ通し、文字列化を診断境界だけへ限定。日本語/非BMPの静的・骨格glTF/GLBと外部依存・sidecar・cache・失敗保持のWindows契約を追加。既存narrow APIはASCII互換として残し、argv/外部URI leaf/runtime論理pathの規約は拡張しない。実Windows前のためdoing。
- G2-S6-MODEL-NATIVE-PATH検証準備: 既存sidecar結合試験をnative path比較へ変更し、Windows対象を21CPUへ拡張。比較器12件×通常/最適化とportable native UTF8 locatorの空/日本語/非BMP/NUL/不正byte検査が成功。Linuxで全test TUをcompileする試行はWindows.h不在で未実施扱い。実Windowsと79+10byte gateは未確認。

- G2-S6-MODEL-NATIVE-PATH実検証: 4b9fd5b8/run37228182164でCore/AssetCook buildは成功、追加testのmainで局所alias TextとCore::Text、およびDetail名前空間が曖昧となりbundle compile失敗。test aliasをNativeTextへ変更し、診断encoderのnamespaceを明示する。production実装/試験値は変更せず、21CPU・79+10byteは未到達のためdoingを維持。

- G2-S6-MODEL-NATIVE-PATH受入: d2556d3c/run37229272949で実Windows build・21CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。test名衝突の修正後production実装は不変。日本語/非BMPの静的・骨格glTF/GLB、source/base/override、外部依存、共通record→Skip、lock/不在/不正locatorの拒否を実証。3ZIPのdigestと79+10固定出力の直接byte一致を独立確認しroot再実行。taskをdone。argv/非ASCII外部URI/runtime出力path拡張は含めない。

- G2-S6-STATE-CODEC開始: 永続化の前段として、owner/root/manifest bindingと共通CookOutputRecordを厳密JSONへ値所有で写す。uint64は固定hex、重複/未知field・所有衝突・過大入力を拒否する。file新規保存と既存root transactionは別taskに分け、codecの成功を上書き権限として扱わない。
- G2-S6-STATE-CODEC検証準備: JSON拒否fixtureの3文字列をC++ UCNではなくJSON escape byteへ訂正し、literalだけの独立compileでbackslash列を確認。全kind実cook recordへcodec往復を追加、22CPUへ登録。比較器12件と独立prefix順序10000例が成功。native Windows/79+10は未実行。DOM前node予算は未実装の制約として明記。

- G2-S6-STATE-CODEC受入: af63b209/run37231239763で実Windows build・22CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。全kindの実recordをJSON往復して共通Skipへ戻り、strict schema・uint64境界・scope・alias/prefix・失敗保持を実証。3ZIPと79+10固定出力の直接byte一致を独立確認しroot再実行。値codecをdone。実file保存/読込と既存root transactionは未接続。

- G2-S6-STATE-FILE開始: callerが保持するbindingとASCII絶対locatorを使い、RuntimeRoot外/同volumeをhandleの物理pathで照合する。新規tempをCREATE_NEWで排他取得し、write/flush/readback後に同じhandleでno-replace renameする案を採る。Missingは既存親の下の最終leaf不在だけ。所有handle以外をcleanupせず、production transactionへは未接続。
- G2-S6-STATE-FILE境界調整: 不在runtimeの将来8.3 aliasがstate新規fileと衝突する余地を除くため、このprimitiveはRuntimeRootも既存directory必須へ限定する。新規rootと外部stateの順序/rollbackは後続transactionで扱う。root不在はMissingではなくErrorとして保存前に止める。
- G2-S6-STATE-FILE検証準備: root不在試験は新しいstate leafを指定し、旧実装のMissing/新規保存を直接反証する形へ補強。既存file/directory/junction/sharing・実2writer・temp衝突・故障注入・orphan保持と全kind save/load→Skipを23CPUへ接続。比較器12件×通常/最適化とBOM/CRLF検査が成功。Win32 handle動作と79+10byte gateは未実行。

- G2-S6-STATE-FILE受入: 375a7dc6/run37234005425で実Windows build・23CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。native handleの保存/読戻し、全kind save/load→Skip、実2writerと競合/故障時の保持を確認。short_alias_distinct=1、different_volume_checked=1で両条件も実行済み。3ZIPと固定79+10出力の直接byte一致を独立確認しroot再実行。taskをdone。production一括更新/lock/journalは未接続。

- G2-S6-STAGED-OUTPUT-PLAN開始: まず単体のauthoritative inventory公開とfinal→stageの依存保持/captureを独立して検証する。集合を跨ぐ衝突はOUTPUT-SET-GUARDへ分離し、その計算量と物理aliasを別の完了条件で扱う。両者とtransactionが揃うまではproduction batchの書込/既存root受理を変更しない。
- G2-S6-STAGED-OUTPUT-PLAN検証準備: 既存cache実装のprefixは新header include以外のbyte一致を確認。全kindのstage再cookと元package印の一致を24CPUへ登録し、8.3別表記/reparse拒否も追加。drive root自体は支持範囲外と明記。比較器12件と行末検査は成功、実Windowsと既存byte gateは未確認。

- G2-S6-STAGED-OUTPUT-PLAN実検証: e4a3181d/run37237085524で実build/5診断と既存23CPUが成功。追加testは余分なfragmentを作るfixtureで停止。raw単体writerはmanifest追記でなく置換するため、2つの実cook rowを結合して有効な2件fragmentを作る試験へ訂正する。production実装は不変、残りのfreshness試験と79+10byte gateは未確認のためdoing。

- G2-S6-STAGED-OUTPUT-PLAN受入: 62e8c81e/run37238379793で実Windows build・24CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。元snapshotを保持した全kind stage再cook/capture、意味変更・raw sidecar/外部file変化・余分fragmentの拒否を実証。staged_short_alias_distinct=1を確認。fixtureだけの訂正後にproduction実装は不変。3ZIPと固定79+10出力の直接byte比較を独立確認しroot再実行。taskをdone、集合横断guardとproduction transactionは未接続。

- G2-S6-OUTPUT-SET-GUARD開始: 各planを既存authorityで再準備し、fresh依存と全targetを平坦化して、構造key・物理component・volume/file IDのsortで集合横断の衝突を調べる。集約passは同一locatorを1回だけ観測し、全件pairwiseのfilesystem queryを増やさない。現在の単体出力上限4件の既存guardは維持し、source byte再読込の費用と区別する。production採用/公開は行わない。

- G2-S6-OUTPUT-SET-GUARD検証準備: 共通Prepare由来の全依存とprimary/派生keyを再採取し、volume GUID/file ID・component順prefix・hardlinkを集合単位で照合する読み取り専用APIを追加。4096plan/65536 protected出現/32MiB metadata上限と集約identity観測数の試験を登録し25CPUへ拡張。既存CookCacheDecision実装prefixはinclude以外byte不変。比較器12件×通常/最適化、独立prefix順序モデル10000例が成功。実Windows/79+10byte gateは未実行。
- G2-S6-OUTPUT-SET-GUARD補強: 共通manifestはcase-fold path一致だけで統合せず、存在状態と既存file ID、不在時はcanonical native表記の完全一致を要求する。case-sensitive directoryの別endpointを誤って1件へ縮約しない。対応する値レベル反証と実不在pathのcase差分試験を追加。

- G2-S6-OUTPUT-SET-GUARD受入: 38a2c4b6/run37241681312で実Windows build・25CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。集合guard試験13.27秒、実8.3 aliasフラグ1。fresh全依存・primary/派生key・prefix・file ID・hardlink・manifest同一性・4096plan/65536保護出現/metadata予算を確認。3ZIPと79+10固定出力の直接byte一致を独立確認しrootで再実行。読み取り専用guardをdone。所有binding/lock/journal/production公開は未接続。

- G2-S6-OWNER-ID開始: producer・schema・spec identity・FINAL root identity・relative manifestを長さ付きUTF8としてSHA-256へ渡し、先頭16byteをowner識別子にする。既存Windows CNGを使い私製hashを増やさない。SourceRoot変更は既存依存fingerprintが扱う入力選択としownerから除外する。物理canonicalとdrive-form保存bindingのresolver、lock/journal/公開は別境界として残す。
- G2-S6-OWNER-ID検証準備: 実C++文字列literal4件を単独compileしてbyteを抽出し、独立Python hashlibの固定vectorと一致。比較器12件×通常/最適化も成功。独自UTF8 helperとcanonical manifest規約を共有し、26CPUへ登録。実Windows CNGと79+10byte gateは未実行。

- G2-S6-OWNER-ID受入: f1c97437/run37243514075で実Windows build/CNG・26CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。固定SHA-256 vector、field区切り、UTF8/各4096byte上限/失敗保持、独立expected bindingのcodec照合を実証。3ZIPと79+10固定出力を独立確認しrootで再実行。値導出をdone。物理path resolver、state配置、lock/journal、production採用は未接続。

- G2-S6-OWNER-RESOLVER開始: 既存schemaを維持してphysical volume-GUID UTF8 identityとASCII drive-form expected bindingを分離する。specは既存regular、rootは既存directoryまたは既存直親下の不在leafに限定。不在候補は作成後の再解決・完全一致が必須であり、tunneling/将来aliasを予測した公開許可にしない。
- G2-S6-OWNER-RESOLVER検証準備: file observerを同じprivate coreへ寄せ、file入口の型判定が従来と同値であることを確認。比較器12件×通常/最適化が成功。SUBST fixtureは専用例外でunwindし、自分のmappingだけをexact cleanupする。未使用driveの二段確認、強制例外後のmapping消失確認を追加。27CPU登録済み、実Windows/79+10byteは未実行。

- G2-S6-OWNER-RESOLVER受入: f9dc88ad/run37245964420で実Windows build・27CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。8.3/Unicode物理親へのASCII alias/SUBST/例外時mapping清掃/case-sensitiveの各flagは全て1。shareなしspecの属性観測も1であり、content-read/公開許可とは区別する。不在rootの通常作成前後照合、同名spec置換、lexical state不一致拒否、失敗保持を確認。3ZIPと79+10固定出力を独立確認しrootで再実行。read-only resolverをdone。lock/journal/production採用は未接続。

- G2-S6-DESTINATION-LOCK開始: 初期profileは同volume全writerを直列化し、root名のcase/8.3/tunneling/親子包含によるlock抜けを避ける。volumeの複数GUID表記をmount managerのcanonical名へ統一し、Global mutex名へ使う。同期callback・待ち時間0・同thread再入拒否を採り、file/ACL/privilege変更は行わない。abandonedは永続印ではないためordinary取得でもjournal検査を省略しない。
- G2-S6-DESTINATION-LOCK検証準備: 28CPUへ登録。Global名/volume GUIDの実C++literalを単独compileして区切りと長さを確認し、比較器12件×通常/最適化が成功。test bundleが--testを除去する実装に合わせてchild引数を調整。実thread/process、終了によるabandoned、全handle消滅後のordinary、例外/故障後の別thread再取得を試験化。実Windowsと79+10byteは未実行。

- G2-S6-DESTINATION-LOCK受入: 6a9eeba2/run37248641417で実Windows build・28CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。実thread/processの排他、Busy、例外/故障後の解除、実process終了のabandonedと全handle消滅後ordinaryを確認。8.3/SUBST/Unicode alias/別volume/cross-processはflag1、cross-sessionは未実測の0。Release/Close注入は実cleanup後の失敗報告試験。3ZIPと79+10固定出力を独立確認しrootで再実行。primitiveをdone、production/journalは未接続。

- G2-S6-MANAGED-UPDATE-INVENTORY開始: 管理済み更新の前段として、旧stateに宣言されたkey/packageと新FINAL planの対応を値所有する。独立bindingと明示scopeを照合し、package/manifest/stateの実before-image取得を後段の必須要件へ分ける。初期profileはflat manifestと既存inventory固定。純値層のためlock/recovery/state再読込/共通plan再検証を省略する根拠にはしない。
- G2-S6-MANAGED-UPDATE-INVENTORY検証準備: 旧state codecを共有し、sortしたindexでprimary/派生を対応付ける。新plan順と値寿命を保持、世代上限は変更用generation=0と不可flagで示す。全実kindのcache試験にも接続し29CPUへ登録。比較器12件×通常/最適化と差分衛生は成功、実Windows/79+10byte gateは未実行。
- G2-S6-MANAGED-UPDATE-INVENTORY試験修正: c5c1c28/run37252065385は実build・5診断・28/29CPUが成功したが、専用fixtureのRaw FourCC末尾NULをcodecが拒否した。fixtureを共有RawEntryTypeへ訂正し、production実装は不変。後続79+10byte gateは未到達のため再実行する。

- G2-S6-MANAGED-UPDATE-INVENTORY受入: e3244053/run37253796557で実Windows build・29CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。fixtureを共有RawEntryTypeへ合わせた後、順序非依存対応・固定inventory・世代上限・4096件/metadata予算・失敗保持・値所有と全実kindの接続を確認。3ZIPと79+10固定出力を独立確認しrootで再実行。純値対応表をdone、filesystem所有・復旧・production公開は未接続。

- G2-S6-MANAGED-STORE-OBSERVATION開始: runtime直親を物理workspaceとし、固定.norves-assetcookと祖先storeを読み取り専用で検査する。協調writer・stable namespace・既存unknown root採用禁止の範囲で入れ子登録を防ぐ。固定pendingはowner/spec変更でも見落とさず、全active rootの欠落/置換を停止理由にする。store生成・journal・書込接続は別task。
- G2-S6-MANAGED-STORE-OBSERVATION検証準備: 物理GUID祖先を列挙し、fixed long name/short alias・独立header ID・全root claimを照合する読み取り専用層を30CPUへ登録。共有lockが消す診断は独立値で保持し、列挙handleも例外時RAIIで閉じる。比較器12件×通常/最適化とGUID literalの単独compileが成功。実Windows/79+10byte gateは未実行。
- G2-S6-MANAGED-STORE-OBSERVATION診断追加: a870601c/run37256953519は実build・5診断・29/30CPUが成功したが、専用testの前後snapshot集約assertが失敗した。既存logには対象entry/変化field/呼出箇所がないため原因は未確定。比較条件は維持し、caller行・path・file種別・size/hash/write timeを出す診断だけを追加して実Windowsで再現する。後続79+10byteは未到達。
- G2-S6-MANAGED-STORE-OBSERVATION時刻観測修正: 1f0b6c42/run37258373173で差を特定。sub directory作成直後の試験で、親runtime directoryの列挙由来write timeだけが変わり、path/type/size/hashは同じだった。列挙cacheではなく全entryのnative handleからFileBasicInfoを読むようにし、directoryを含む時刻比較は維持する。加えてvolume/file IDの一致も検査し、productionは変更しない。

- G2-S6-MANAGED-STORE-OBSERVATION受入: 75926742/run37260128285で実Windows build・30CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。handle由来時刻とvolume/file IDを使うfixture修正後、入れ子/固定pending/全claim/未知root拒否/4096件/無変更を確認。storeの8.3/SUBST/Unicode alias/case-sensitive/reparseは全flag1。3ZIPと79+10固定出力を独立確認しrootで再実行。読み取り専用観測をdone、store作成・復旧・production公開は未接続。

- G2-S6-MANAGED-STORE-INITIALIZATION開始: 共通のlocked観測を再利用し、fresh stageに完全headerと空indexを作ってからdirectory handleでno-replace公開する。公開後は元stageのID保持を同handleで確認し、DELETE handleを閉じてから全観測を再実行する。公開後の失敗はPublishedButErrorとして保持し、root/state/package公開は含めない。
- G2-S6-MANAGED-STORE-INITIALIZATION検証準備: 初期化APIと31CPUを登録。共通観測をtyped private helperへ寄せ、root/ownerをstage作成後と公開後に再検査する。開いた既知handleだけをabort清掃し、閉じたchild/未知entryはorphanとして保持する。6境界の実process終了、公開前後の故障、ID維持と条件付きaliasを試験化。比較器12件×通常/最適化と差分衛生は成功、実Windows/79+10byteは未実行。
- G2-S6-MANAGED-STORE-INITIALIZATION公開形式修正: f0989617/run37263862034はbuild・5診断・既存30CPUが成功したが、専用test初回の相対renameが0x57で拒否された。RootDirectory=NULLとlive workspace由来の絶対native名に方式を統一し、sizeofを満たすbufferへ変更する。copy/replace/実行時fallbackは増やさず、同handle/同volume/no-replace/公開後ID検査を維持する。31CPU/79+10byteは再実行待ち。

- G2-S6-MANAGED-STORE-INITIALIZATION受入: 98ac19ab/run37265719494で実Windows build・31CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。8条件flagは全1で、6境界の実process終了、stage/store ID維持、新aliasの作成前後停止、no-replace競合、orphan保持と公開後失敗を確認。3ZIP・既存payload・CNG RNG import・証拠chainを独立確認しrootで再実行。fresh管理領域作成をdone、runtime/state/package/journalとCLI接続は未実装。

- G2-S6-MANAGED-TRANSACTION-INTENT開始: bootstrapは既知staged root、updateは宣言済みの変更fileだけを対象にし、root IDとunlistedを保持する。大きなstate/indexはinlineせず4固定roleのimage証拠とexact side bytesを検証する。index parserを共有化し、receiptは非循環の固定body＋既知object IDで照合する。NoChangeの判断と実publication/recoveryはcontroller側に残す。
- G2-S6-MANAGED-TRANSACTION-INTENT検証準備: 4固定controlのexact bytes/ID、共有index/state/manifest parser、固定key/package、Cook/Skip値、bootstrap directory閉包を32CPUへ接続。全kindは実stage/package/control IDでupdateと別不在rootのbootstrapを構成し、source不在parseも反証する。移動元wrapperを無効化し、深い共有prefixは親indexを一度だけ辿って検査時の文字列増幅を避ける。比較器12件×通常/最適化とliteral単独compileは成功、実Windows/79+10byteは未実行。

- G2-S6-MANAGED-TRANSACTION-INTENT受入: af3609a7/run37272790699で実Windows build・32CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。15種の実cook fixtureでupdate/bootstrapのnative imagesとsource不在parse、250段共有prefix/2000末端の閉包、移動元無効化と失敗保持を確認。共有codec抽出73項目・71 source receipts・3ZIP・CNGを独立検証しrootで再実行。値契約をdone、実publication/rollback/CLI接続は未実装。

- G2-S6-MANAGED-BOOTSTRAP-EXECUTOR開始: texture spec v1の共通prepare/captureと既知stageを、pending付きの実公開へ接続する。復旧はlive workspace/store/header/pendingで先に束縛し、固定after-index/stateのclaimを検証してから相対root/manifestを読む。元bindingをsource不在で独立再導出できるとは扱わない。receiptはpending内でcommitし、完全commit/rollback後だけpendingを退役。途中で必要なside原objectをdeleteせずrenameで保持する。
- G2-S6-MANAGED-BOOTSTRAP-EXECUTOR検証準備: 共通texture cookからroot/state/indexを公開するcontrollerとstorage-bound recoveryを33CPUへ登録。17境界の実process終了、source/spec不在、原index ID/byte保持、未知entry/親/別IDの拒否、orphan保持、Busy、aliasと解除失敗の試験を追加した。native観測helperを共有し、claim照合はfold-name索引で全件再列挙を避ける。比較器12件×通常/最適化と347 literalの単独compileは成功、実Windows/79+10byteは未実行。
- G2-S6-MANAGED-BOOTSTRAP-EXECUTOR診断追加: de680cd9/run37280393756は実build・5診断・既存32CPUが成功したが、新規bootstrap初回のCreated判定で停止した。返却result/errorがlogに無いため原因未確定。判定とproductionを変えず、fixtureのAPI結果/診断出力だけを追加する。17中断境界・後続79+10byteは未到達。
- G2-S6-MANAGED-BOOTSTRAP-EXECUTOR集合契約修正: 02aa146a/run37282610496でresult=Error、set_requires_one_manifest_pathを確認。asset別work directoryのfragment集合へ、FINAL用の単一manifest guardを呼んでいた。集合検査coreを共有した独立fragment入口を追加し、stageでは全manifestを別出力として数える。FINALの単一manifest条件は維持し、共有fragment拒否・cross-source/control衝突の反証を追加する。

- G2-S6-MANAGED-BOOTSTRAP-EXECUTOR受入: 48a425c5/run37285943680で実Windows build・33CPU・17境界の実child終了/復旧・bootstrap条件5flag全1・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否を確認。3ZIP/API digest・MSVC/CNG・source/evidence chainを独立照合しrootで再実行した。cross-session実測は従来どおり未実行。Busy診断のraw ff×8は空TStringのc_str()がnposを返す既存不具合と特定し、raw証拠を保持。次にこの基礎不具合を直し、既存rootの増分更新へ進む。production CLI接続とG2-S6全体は未完了。

- G2-S6-EMPTY-STRING開始: Busyの空診断を通して、未確保TStringのdata/c_strがnposのアドレスを返す既存不具合を検出した。文字型ごとの静的ゼロ終端だけに修正し、所有pointer・layout・allocator・iteratorは維持する。既存LoggerSinkTest束へ専用回帰を追加し、実Windowsと従来byte gateで再確認する。

- G2-S6-EMPTY-STRING検証束の修正: 8a25b8de/run37289884552ではAssetCook/CookedMeshTestと新規StringEmptyTestソースがcompile成功したが、新たにビルドしたLoggerSinkTest内の既存Input試験4fileで47件のcompile errorが発生し、実行gateは未到達。入力系の別修正へ範囲を広げず、StringEmptyTestを既存UnicodeTextTestと同じCookedMeshTest束へ移す。型修正と試験本文・34CPU条件・byte比較は不変。

- G2-S6-EMPTY-STRING受入: abc68946/run37292183322で実Windows34CPUが成功。5文字型の空/clear/shrink/move/reuse/printf回帰、raw LastTestのstrict UTF8とBusy空診断1行、bootstrap17実中断/5条件flag全1を確認。既存79+10出力の直接byte一致、2spec×2/16拒否、5診断、3ZIP/API digest/CRC/inventory、MSVC/CNGを独立検証しrootで再実行した。旧受入/失敗証拠は保持。cross-session実測は従来どおり未実行。既存Inputテストの別compile不具合は保留し、既存rootの増分更新へ進む。

- G2-S6-MANAGED-UPDATE-EXECUTOR開始: fixed inventoryの既存rootを、共通cache判断とfile単位の条件付き公開へ接続する。NoChangeはstage作成/世代加算より先に判定し、allSkipでもmanifest差があればmanifest-only更新する。native transaction/controllerをprivate実装へ寄せ、Bootstrap/Updateは準備を分離する。復旧は固定control検証後だけ相対対象を解決し、未関係fileとroot IDを保持する。

- G2-S6-MANAGED-UPDATE-EXECUTOR検証準備: native transactionをprivate controllerへ抽出し、NoChange/manifest-only/固定inventory更新とsource非依存復旧を接続。新試験は2Cook/1Skipで16公開＋12rollback＋abandonedの29実process終了、root/Skip/unlisted/sibling/orphan保持とbefore復元を反証する。静的レビュー2回はPASS、比較器12+4件×通常/最適化、391 literalの単独compile、BOM/CRLF差分検査が成功。35CPUと既存79+10byteの実Windows gateは未実行。

- G2-S6-MANAGED-UPDATE-EXECUTOR受入: a7f29603/run37297733138で実Windows35CPU、更新29実child終了/3flag全1、新規17実child終了/5flag全1が成功。共通controller抽出31helperを含む128 source検査、strict UTF8/両Busy空診断、既存79+10直接byte/2spec×2/16拒否/5診断、3ZIP/API digest/CRC/exact inventory、MSVC/CNGを独立照合しrootで全verifier再実行。旧証拠194fileを保持。cross-session実測と最大inventory性能は未確認。次はmanaged texture v1 CLI接続と明示復旧で、spec v2/旧ModelCookCache移行は残る。

- G2-S6-MANAGED-TEXTURE-CLI開始: texture v1通常経路を入力adapterと共通管理controller dispatchへ置き換える。normalはpendingで停止し、--recover --runtime-rootで親workspaceの固定pendingを明示復旧する。source/spec無しでも復旧できるが、新cookは別のnormal呼出しで独立検証する。frozen runtime payload比較は不変、外側の管理metadataは有限の別inventoryとして保存/検証する。

- G2-S6-MANAGED-TEXTURE-CLI検証準備: --asset-setを共通initializer/bootstrap/updateへ接続し、明示workspace復旧と36番目のCPU試験を追加。既存25実CLIを維持し、管理CLI15呼出しを別群、baseline metadata各4file＋原ID/byte receiptを可視managed/に保存する。レビュー2回の指摘（SourceRootのdot/末尾separator、headerのvolume UUID36形式）を修正し、directory表記回帰とmetadata純値fixtureを追加。比較器12+4、新証拠7件を通常/最適化で確認。実Windows36CPU/79+10byteとmetadata受入は未実行。

- G2-S6-MANAGED-TEXTURE-CLI受入: 19128d41/run37303534756で実Windows36CPU、既存Update29/Bootstrap17実中断と全条件flag、strict UTF8/Busy空診断、79+10直接byte/7smoke/5診断が成功。旧25実CLI（9成功/16拒否）に別15実CLI（8成功/7拒否）、管理metadata12fileのfinite inventory/schema/native ID/owner tuple/package hash、3ZIP/API digest/CRC、両MSVC/x64 binaryのCNGを独立照合しrootで全verifier再実行。旧証拠216file不変。texture v1のCLI接続をdoneとし、GR96全体完了とはしない。ロードマップGR79→GR82の依存に戻り、次は明示NVMESH v1のwriter/cook接続を優先する。

- G2-GR79-MESH-V1-WRITER-COOK開始: ロードマップのGR79→GR82へ戻る。shared reader/material codec/settings/ARM kernelを実NVMESH v1 cookへつなぎ、v0出力は不変にする。合成ARMは明示RawRgba8と共有texture cook入口、DoubleSided autoは位置溶接後のedge分類を必要とする。v1 fingerprintだけ画像解析が必要になるため、旧no-decode契約と分けて文書化する。runtime adapter/描画受入れは後続。

- G2/GR79 writer/cook検証中: 明示NVMESH v1の128B材質/cluster writer、共有材質plan、ARM/発光/外部画像、版付きmanifest/cache/output guardを接続。GltfMaterialCookV1Testを37番目のCPU gateへ追加。静的レビューround1のcluster幅指摘を修正しround2 PASS。host閉鎖判定の実コードsmokeとPython12+4+7 normal/-Oは合格。Windows nativeと旧79+10 byte互換はこのコミットのCIで未確認。runtime v1拒否とGPU未受入れを維持。

- G2/GR79 native初回run37313863062はビルド失敗。ModelCookCache.cppの2か所でAnsiStringViewとliteralの比較がMSVC C2678。右辺を明示AnsiStringViewに修正。テスト37件・byte比較は初回では未実行。次commitのCIで改めて検証する。

- G2/GR79 writer/cook受入: 5de8b9f539bb3a0ecfa5f7acb8f361c5135559f7 / tree7fb1789450844242dccc661f8ef63a031d6317a7 / run37315777388 attempt1 job111782027052。37CPUとv1実fixture、Bootstrap17/Update29、79standalone+10texture byte一致、7smoke/5診断、25native+15managed CLI/metadata12、Python12+4+7 normal/-O、実MSVCx64/CNG、3ZIPを独立照合。親のreadonly再実行もexit0。初回compile失敗は別証拠を保存。従来cross-session未試験を維持し、GPU/renderer/model asset-setは完了としない。

- G2/GR79 次の作業: packed ARMと全材質値の所有CPU adapter。単体cookの受入を起点とし、描画未接続の材質をFinalizeへ渡しても拒否する境界まで。runtime v1ロードはまだ開放しない。

- G2/GR79 CPU staging検証準備: 全材質値/packed ARM mask/所有UTF8 pathを保持するadapter、ModelStagingDataの保持枠、未接続Finalizeのtyped拒否を追加。round1で早期拒否testの偽陽性を指摘され、release有効のModelFinalizeStatus assertへ修正。異なるcanonical emissive RGB/NUL/negative zeroも追加しround2静的PASS。Python12+4+7 normal/-O合格。38CPUと旧gateは次の実Windows CIで未確認。Unicode blob reader対応は含めない。

- G2/GR79 CPU材質staging受入: c669322b1ab5955383baf4c33c54702ac9debdbd / treea7956787932a741ce966f2e51e2f4ae3d0bec646 / run37321891650 attempt1 job111802776013。38CPU、新旧marker、17/29中断、79+10byte、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG、3ZIPを独立検証し親も再実行exit0。全13float/48flagsは固定fixture有限検証。実cross-session未試験とGPU未受入れを維持。
- G2/GR79 次段方針補正: 本文8534–8551は使用channelのR8分割＋scalar1x1＋shader変更なしを指定するため、まずこの範囲へ接続する。後続GRのpacked GPU構想を直近へ前倒しする案は採らない。CPU PackedArmV1は元資産layoutとして維持。Nits=0/色0のemissiveTextureは寄与0としてupload省略で受理、OPAQUEのfactor alphaは保持して無視できる。albedo画像alphaの背景判定漏れは別途検査する。

- G2/GR79 opaque runtime接続を開始。ロードマップ本文のR8 selected-channel/scalar1x1・shader無変更へ限定し、CPU/FakeDeviceの実装受入れと実GPU画像確認を分ける。

- G2/GR79 opaque runtime実装を検証へ: 対応subsetをsync/worker/Finalizeで判定し、全mip R8選択uploadとscalar1x1 cacheを接続。round1の匿名texture registry所有残りをptr移管＋ReleaseTextureで修正し、解放/途中失敗/例外/geometry失敗のweak寿命試験を追加、round2静的PASS。Python12+4+7 normal/-Oは合格。旧38＋新runtime＋既存MegaGeometry/ModelResourceの計41CPUを次の実Windows CIで検証する。GPU画像受入れは未実行。

- G2/GR79 opaque runtime初回run37332093729はbuild失敗。ModelStaging.cppのR8直接UpdateでITextureの完全定義include不足（C2027/C2039）。RHI/ITexture.hを明示includeし修正する。41CPUとbyte/CLI検証は初回では未実行で、再CIへ送る。

- G2/GR79 opaque runtime修正run37334004766はCore/AssetCook/CookedMeshTestのbuildを通過後、既存MegaGeometryResourcesTestでprivate headerの相対include不足C1083。ModelStaging.hとImportedOpaqueRuntime.hから同じdirectoryのModelMaterialStaging.hを相対参照する形に修正し、consumerへprivate root追加を強制しない。41CPUとparityはまだ未実行。

- G2/GR79 opaque runtime受入: 17ce1e04b64e7ab943e489da270ac367c4193030 / tree d3680dce4ccae80060a3dbf5f7004b62390492b9 / run37336768659 attempt1 job111853407017。41CPU/3marker、Bootstrap17/Update29、79+10直接byte、7smoke/5診断、25native+15managed CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを独立照合し親再実行exit0。失敗run37332093729/37334004766の証拠も保持。GPU画像・cross-sessionは未実行のまま。
- G2/GR79 次優先: 現行v0/looseのCreateTextureFromPixelsにも匿名handleのregistry所有残りを確認したため、小さい所有移管/例外cleanupを先に閉じる。その後ロードマップ173の複数primitive/material cookを進め、N>1 runtime拒否を維持する。GR82 StageBへは飛ばず、StageA（済）後の既定GR84/GR83順を尊重する。

- G2/GR79 旧匿名texture所有修正を開始。今回の対象はmodelへ渡す匿名pixel textureとupload例外のcleanupだけ。named prepared/cooked cacheの所有・通常API戻り値・mip生成は維持する。

- G2/GR79 旧匿名texture所有修正の検証準備: ptr取得後registry解除、作成元storeのRAII例外cleanupを実装。5role単独/併用、失敗作成数4/1/5、weak失効、通常/空data/非例外mip失敗のcaller所有、named cache保持を既存testへ追加。静的2roundでblockerなし、Python12+4+7 normal/-O合格。41CPUと旧gateの実Windows再検証は未実行。

- G2/GR79 旧匿名texture所有修正受入: a0faaa015d1afb3680d1b06ca07fbb31a41c03af / tree9215c4ed8f6f8323f4d1bd0cf97cc93ef4cb7a0e / run37343440486 attempt1 job111875985370。41CPU/新旧4marker、Bootstrap17/Update29、79+10byte、7smoke/5診断、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを照合し親も全readonly verifier再実行exit0。GPU/cross-sessionは未実行。次はGR79複数primitive/material cookを優先し、N>1 runtime拒否を維持する。

- G2/GR79 複数primitive/material cook開始。材質表は参照元番号順（implicit defaultは独立の末尾）、primitive順は元のまま。doubleSided autoは材質境界を開口と誤認しないよう全meshの位置溶接で一度判定し、force指定は材質ごとに優先する。派生ARMのtool内IDを64bitへ拡げ、旧単primitiveのID/path/hashを保存しmultiだけ材質別namespaceへ分ける。

- G2/GR79 複数primitive cook検証準備: 1mesh内Nprimitiveの局所index/全体transform/primitive別cluster、元材質順の共有表とimplicit default、64bit派生画像ID/pathと厳密共有を接続。既存testに2材質・再利用・逆順・default・9primitive・GLB・後半不正・別形状fit/pivot・全体閉鎖/force・2ARM/共有参照・cache miss・N>1 runtime拒否を追加。静的2roundでblockerなし、Python12+4+7 normal/-OとBOM/EOL差分検査PASS。Linuxのnative構文検査はWindows.h不在で未到達。実Windows41CPUと新marker/旧79+10byte/managed gateは次のCIで未確認。

- G2/GR79 複数primitive/material cook受入: 02ec0e14cf3ceac95f0ea0fc26c4484ee9fb6499 / treee9377b2758027b2a4dc216e9a41c9c70d0e7ec34 / run37350305381 attempt1 job111899190790。41CPU/新multi＋旧4marker、Bootstrap17/Update29、79+10byte、7smoke/5診断、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを照合し親もreadonly再実行exit0。旧304証拠fileを保全、最新9pathsと累積27実装pathsを有限検証。GPU/cross-session/N>1描画は未受入れ。ロードマップ203の順序に従い、Stage A（済）後のGR84へ進み、まずraw BVH解析を独立した単位として接続する。

- G2/GR84 raw BVH解析開始。元データの順序/名前/秒/degree値を保持するpure parserから進める。NVSKEL128/256と独立したDecodeLimitsを設定し、変換の可否は後続へ分ける。失敗時は既存out保持、名前は厳密UTF8 byte所有で自動改名しない。root以外の位置channelも捨てず保持する。回転行列/retarget/Blender実測/CLIはこの単位の完了条件に含めない。

- G2/GR84 raw BVH解析の検証準備: 宣言順/親/UTF8名/OFFSET/End Site/生double frame列、有限値・行幅・末尾・明示limit・失敗時out保持を実装。6回転順、混在channel、257関節/深いstack、非BMP/C1、空行、limit境界/不正入力をliteralで追加。2round静的PASS、実2fileのg++ C++23 -Wall -Wextra構文検査、Python12+4+7 normal/-O、BOM/EOL検査PASS。確保故障注入は未実施でnothrow moveをcompile時固定。実Windows42CPU/新markerと既存89byte/managed gateは未確認。raw順序保持を回転行列/retarget受入れとは扱わない。

- G2/GR84 raw BVH解析受入: 9c62d3ba41093d7bfaa7c596bc95b2ac09247ab1 / tree4d6dc722031244dbb6c1f18290cb800629710cf2 / run37356979599 attempt1 job111921766216。42CPU/旧5＋新BVH marker、Bootstrap17/Update29、79+10byte、7smoke/5診断、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを照合し親readonly再実行exit0。旧538証拠file保全。raw順序/値の所有だけを受入れ、回転行列/retarget/Blender/CLI/StageB/確保故障注入は未受入れ。次はdoubleのlocal/world FKを独立単位にする。位置channelはOFFSET加算と絶対local置換の解釈が異なるため、必須の明示enumで指定しAutoを設けない。

- G2/GR84 double姿勢評価開始。OffsetPlusChannels/AbsoluteLocalChannelsを必須引数とし、どちらもroot/nonrootの完全3位置成分へ同じ数学規則で適用する。位置なしはOFFSET。完全3回転は宣言順右積（数学的列vector）とし、部分成分/交錯/回転後の位置は明示拒否する。BVH解説とBlender ARMATUREの位置処理には差があるため、一般規格/Blender互換の断定をせず、二つの明示解釈を独立した期待値で検証する。

- G2/GR84 double姿勢評価の検証準備: 必須の位置規約、6順の右積、親FK/End Site、構造/selected frameの有限検査、未対応channel/overflow拒否、nothrow置換を接続。0初期化enumはUnspecifiedとして拒否する。2round静的PASS、実cpp+testのg++構文検査とPython12+4+7 normal/-O/BOM/EOLが成功。実sourceのprivate数学helperだけをallocator stubなしで通常/O2-NDEBUG/ASan・UBSan（LSan除外）実行し、6順とFK literalを確認。full Evaluate/Coreのhost実行ではない。実Windows43CPU/新marker/既存89byte/managed gateは次CIで未確認。

- G2/GR84 source姿勢評価受入: 6fbbbe8b4bcfe7ccf592bc07d2a13be1652e6368 / tree7d5cab88c31be45955a7e34f2c9fbe9569d21bfc / run37362515658 attempt1 job111940266049。43CPU/旧6＋新FK marker、Bootstrap17/Update29、79+10byte、7smoke/5診断、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを照合し親readonly再実行exit0。旧572証拠file保全。synthetic6順1e-9と明示2位置規約source FK/末端まで受入れ、target/Sampler/Blender実測/変換clip/CLI/StageB等は残す。次は既存Identity索引の重複先頭勝ちを変えず、独立した厳密UTF8 joint-name indexを先に整える。

- G2/GR84 厳密joint-name index開始。Core/AssetCook向け下準備としてPrivate/Animationへ置き、既存SkeletonResource::FindJointIndexのIdentity/hash・重複先頭勝ちは変更しない。UTF8 byte poolと元indexを所有し、target native名の変換は既存NameCodec minor2へ集約する。名前解決成功は階層/rest/retarget互換を保証しない。

- G2/GR84 strict joint-name indexの検証準備: UTF8所有pool/完全一致/元番号、非空・重複・codec/上限/不正span検査、copy-and-swap/empty move、native minor2変換を接続。2round静的PASS、pure coreとgeneric test subsetのg++構文検査、既存NameCodecだけの通常/O2-NDEBUG/ASan・UBSan（LSan除外）、Python12+4+7 normal/-O、BOM/EOL検査PASS。native temporaryのmax_sizeをprepassへ追加し、misalignment/extent overflow/count/入力変更/失敗後設定保持も反証する。新index/NativeAdapter runtimeはhostで未実行で、次の実Windows44CPU/新marker/旧89byte/managed gateで検証する。

- G2/GR84 厳密joint-name index受入: 585d7004505af6cbaf917b2e466b9061aee4853c / tree47a062166c1232ed1ba0b0f1fd01061ebab56cf9 / run37368810439 attempt1 job111960454749。44CPU/旧7＋新index marker、Bootstrap17/Update29、79+10byte、7smoke/5診断、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを照合し親readonly再実行exit0。旧604証拠file保全、最終641file inventory一致。source名から元番号への厳密対応だけを受入れ、階層/rest/target変換・Sampler共有・実Blender等は残す。次は現行Samplerの実Core出力をDebug/Release別に先に凍結し、bindだけの共有化前後で比較する。

- G2/GR84 Sampler共有化前のbaseline固定開始。既存SkeletalAnimationSamplingTestを実CoreのDebug/Releaseへ接続し、現行の結果を先にbit列で記録する。Sampler/数学/Resourceを変えず、同じ構成内の反復一致と旧literalを併用する。shear/反射/極大bindは成功を先に約束せず旧boolと出力を保存し、falseならClear全項目を検証。次のbind抽出で同一fixture/toolchainの基準として使う。

- G2/GR84 Sampler baseline検証準備: 実Samplerを呼ぶ30caseのlittle-endian float-bit採取を既存testへ追加。known成功/拒否とshear等5caseの観察を分け、Clear全項目を固定した。実CoreのDebug/Releaseを各2回実行するworkflowと、旧Library tree・fixture・compiler/vcxproj/SDK/Core.lib/exe/hashの出自検査を追加。Python snapshot/receipt 19件と旧12+4+7をnormal/-OでPASS、Library tree不変・BOM/EOL・YAML構文を確認。実MSVC/45CPUと構成別snapshotは次CIで未確認。

- G2/GR84 Sampler baseline受入: 3cd8c479fa30eb0fafe52cb6aabc4c7ef226e690 / tree4853154229da00c8c49fdc51dd89ee2474b79ee1 / run37373318284 attempt1 job111975595995。45CPU/旧8＋Sampler marker、Debug/Release各2回30caseの実出力、同構成repeat一致、旧89byte/managed全gate/3ZIPを受入れ、親readonly再実行exit0/最終667file inventory一致。Library treeは585d7004から不変。各snapshot6095byte SHAa5b0a90496e9064c2808692706b69f8a11936fea9af3031f25eb909ab33e62d4。極大bindは拒否だがScale/Rotation上書きなら成功（24=false/25=true）。途中TRSのfinite検査を追加してこの旧挙動を失わせない。compiler19.44.35229.0/WindowsSDK10.0.26100.0/構成optionを保存し、cl/Core/exe実byteは非保存・CI hash receiptだけという限界を明記。次はbindのみをprivate helperへ抽出し、固定fixtureと環境条件の前後byte比較を行う。

- G2/GR84 bind行列共有開始。現Samplerのfinite/inverse・IBM→bindGlobal・child/parent→bindLocal・既存TRS分解だけをprivate mathへ移す。名前・Resource・clip上書き・Compose/FK/Palette/Clearには介入しない。既存Sampler fixture/main/Rendering CMakeは凍結したまま、独立helper試験は既存Asset bundleへ追加する。成功runの実snapshot/出自receiptを小さな固定fixtureとして保全し、構成別前後byteと同toolchain/optionsを別の比較器で検査する。

- G2/GR84 bind共有の検証準備: 旧float算術をprivate helperへ移し、Samplerのbind導出から使用。新helperはalias/失敗out保持、旧Decomposeの途中nonfiniteを維持する。独立literal試験とinverse後段overflowのbit保持を追加。実基準2構成6095byte/receiptを保存し、別比較器が固定harness/compiler/SDK/options/source-build-project-binary結合を拒否優先で検査する。静的2round PASS、新比較17/旧採取19/旧12+4+7 Python normal/-O、BOM/EOL/YAML/harness hash不変を確認。host GCCは既存MatrixUtils Normalize<Quaternion>のscalar operator欠落でcompile不能のためruntime未実行。実Windows46CPU/構成別基準一致は次CIで未確認。

- G2/GR84 bind共有受入: 430a6cbcf27ee1d174f5a9e0eea394d57559d8f9 / tree5239cf56c1deefd83435bb567776a84601400152 / run37379203280 attempt1 job111996430902。46CPU/旧全marker＋bind marker、Debug/Release各2回が旧6095byteと完全一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIPを受入れ、親readonly再実行exit0/最終782file inventory一致。旧668file保全。検証chain更新の件数・歴史参照・stdoutの3誤りは失敗証拠を保持して有限差分修正し、全条件の一括照合を完了。実CI自体は1回で成功。次は現Samplerのjoint-global row FKをprivate共有し、parent配列の追加確保をせず借用getterで既存joint配列を読む。source BVHのdouble列FKとは別にする。

- G2/GR84 joint-global row FK共有開始。既存joint配列のParentIndexを非throwの小さい借用getterで読み、local/global/visitStateは既存配列のSpanを渡す。旧再帰式と評価順を保ち、helper内で確保しない。内部global/visitStateはfalse時に部分更新され得るが、Sampleのfalse/Clear契約は変更しない。source BVHのdouble列FK・作者時rest snapshotとは別に扱う。

- G2/GR84 joint-global FK共有の検証準備: parent getter/Spanと同じ再帰評価を接続し、旧配列以外の所有/確保は追加しない。47件目としてroot signed-zero bit、非可換/親順/分岐/cache、cycle/不正入力、finite非剛体、部分出力とscratch再開の独立試験を追加。凍結harness/比較器/6095byte基準は不変。Python17+19+12+4+7 normal/-O、BOM/EOL/YAMLを確認。hostの既存MatrixUtils.h制約は継続し、実Windows47CPU/構成別旧byte各2回は次CIで未確認。

- G2/GR84 joint-global共有の初回native run37385295016（bcecc606、job112016967181）はMSVC Release bundle buildで失敗。新testがMatrix4x4::Zeroを初めて実体化し、既存Math/Matrix4x4.h:304の12要素initializerに対するC2661を検出。実runtime helperの変更ではなく、testの初期値をIdentityへ1行変更して再検証する。rootのtranslation/signed-zero期待値はIdentityと異なり、コピー検査は弱めない。既存Zero定数の修正は別件として記録し、この単位でpublic Mathを変更しない。初回capture/47CPUは未実行、失敗証拠を保存。

- G2/GR84 joint-global FK共有受入: 0225e17f9187a45b1443562b72ffc3e31b52d424 / tree62fe3239d8e4d85c6837a856cb73637fee6153e8 / run37387162803 attempt1 job112023133952。初回Zero実体化によるtest build失敗を1行のIdentity初期値で直し、47CPU/旧全marker＋FK marker、Debug/Release各2回の旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIPを受入。親readonly再実行exit0、最終774file inventory一致。旧受入783file/失敗650fileを保持。次はschema非依存の所有joint mapping解決を先行する。Roadmap:4228/4296のrole経由への修正を採用し、Requirementsの旧direct-pair JSON v1案を外部形式として固定しない。role要件そのものをv2へ先送りせず、具体的JSON/profile語彙は別の定義へ分ける。

- G2/GR84 joint mapping所有解決開始。role adapterが展開した具体的source/target名pairを受ける低層として作り、外部JSON version/role語彙/軸fps/restは含めない。root pairの番号はcaller必須指定であり、この層では階層rootかどうかを証明しない。source再利用は明示Reject/Allow、targetは一意、未写像targetはbind保持という後続契約のため一覧を返す。ゲーム固有必須role setは後続profileで定義する。

- G2/GR84 joint mapping検証準備: strict indexを再利用したschema非依存の名前pair解決、明示source再利用policy/root pair、target一意性、入力順pair/元順unmappedの所有、copy-and-swap/空move/最終nothrow置換を実装。catalog/mapping/byte上限と元lookup診断を保持。独立静的1round PASS、実cpp+testのg++ C++23 -Wall/-Wextra構文PASS、Python17+19+12+4+7 normal/-O、旧harness/6095byte不変・BOM/EOL/YAMLを確認。runtime/確保故障注入はhost未実施。実Windows48CPU/固定4capture/旧全gateは次CIで未確認。

- G2/GR84 joint mapping受入: 75ccc7225c3c995b510e033438fbe9ea51c2646c / tree05afdd4e4d50b692638618f6df6d67acb4a3257b / run37392452079 attempt1 job112040470502。48CPU/新mapping＋旧11marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIPを受入。親readonly再実行exit0/最終918payload inventory一致、旧受入775file/失敗650fileを保持。最終verifierは成功し、過去のpreaudit3失敗とpoll整形エラーは証拠として残す。次は既存SignedAxisとBvh double値型による明示基底/単位変換。canonical +Y上/+Z前/right-handedは変換規約であり、モデルの実際の正面を保証しない。

- G2/GR84 double座標変換開始。既存AssetImport::SignedAxisとBvhのdouble値型を再利用し、float rowのtarget評価とは分ける。軸/手系/単位は必須引数で明示し、privateな変換値はBuild成功後だけ有効とする。translationのscaleは1回だけ、行列はsigned permutation共役で全finite 3x3を扱い、SO3検査/正規化は後続consumerに残す。モデルの実際の正面やBVHのroot位置規約をこの層で決めない。

- G2/GR84 座標変換の検証準備: 明示signed basis/handednessとpositive scaleによるdouble index/sign変換、全finite行列、alias/atomic、非finite/overflow/zero-underflow拒否を実装。静的2roundで非nearest飽和overflowの指摘を解消し、IEEE binary64/nearest/gradual-underflow・fast-math拒否を追加。round2後の限定追加として、親がMXCSRだけの丸め変更も直接拒否するguardと3mode試験を追加し、guard除去の負例はexit134で検出した。callerのFP設定は変更しない。最終実cpp/testはhost通常/O2-NDEBUG/ASan-UBSan（LSan除外）で全48基底/FP環境反証PASS、fast-math実probeは拒否PASS。stubやCoreはリンクしていない。Python17+19+12+4+7 normal/-O、BOM/EOL/YAML/凍結harness不変を確認。実Windows49CPU/旧4capture/旧全gateは次CIで未確認。

- G2/GR84 明示座標変換受入: code67cb7620/tree75d8b4f9/run37398784660 attempt1 job112060978186。実Windows49CPU/新coordinate＋旧12marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終1320payload inventory一致、旧受入919file/失敗650file保全。実double helperのhost通常/O2-NDEBUG/ASan-UBSan成功とfast-math拒否も別途確認。有限の明示座標変換だけを受入れ、SO3/rest補正/root処理/retarget/JSON/CLI/StageB/GPU/Blenderは未受入れ。 summary SHA572b7a034132d4414577125a20e202b2388167c01f78651b4b0b94370b91a532、inventory SHA8858e1a4ebd1a3f7c136c89c892ac8c1e115656d63d62d820bd63ca649432f39。cl/Core/Sampler.exeの実byteは非保存でCI hash receiptのみ。次は明示C行列・heading保持・target平行移動保持に限定した1frame回転retargetを、現Samplerで実際に評価できる値へつなぐ。

- G2/GR84 1frame回転retarget開始。明示Cとcanonical source回転から目標worldを作り、未写像parentも実際の生成姿勢で辿る。target forestを禁止する根拠は無いため許容し、指定root pairだけ実rootであることを検査する。現在のbind参照は作者時rest snapshotではない。初期consumerは正のuniform scale（非unitを含む）のみを明示受理し、非uniform/shear/反射は保留診断にする。既存Samplerにはその制限を加えない。Composeのunit-scale factory＋9個のrow乗算と列quaternionの共役正規化を同じ式のまま共有し、生成floatを共有Compose/FKとpalette計算へ通して有限性・回転誤差を測る。固定baselineは変更しない。

- G2/GR84 1frame回転retargetの検証準備。schema非依存pair/明示Cからdouble列回転を作る有界・確保なしkernelと、現在bind/旧Compose/共役正規化/FKを再利用したnative実現値検査を接続した。正uniformの限定profile、forest、未写像親子、非可換/半回転、atomic拒否を固定。native fixtureは実BVH→明示左右basis→1key→実Sampler、非恒等mesh/scale2、独立末端literal、late位置overflowを含む。静的round1は全体PASS、round2は追加left-basis fixture差分PASS。最終実kernelはhost通常/O2-NDEBUG/ASan-UBSan（LSan除外）でPASS、同じ3構成の旧coordinate試験と両fast-math拒否probeもPASS。旧Python17+19+12+4+7 normal/-OとBOM/EOL/YAML/凍結harnessは不変。native hostは既存Containers.hのWindows.h依存でcompile不能、stubなし・native実行なし。実Windows51CPU/4capture旧6095byte/旧全gateは次CIで未確認。回転角度の専用閾値超過fixtureと全alias組合せ・確保故障注入は未網羅。

- G2/GR84 rotation初回native run37404174494（e467b378、job112077901826）はMSVC Release bundle buildで失敗。新規2testのmainが束ねる構成でNorvesTestMain_*へ改名され、暗黙のmain returnが適用されずC4716となった。2か所へ明示return 0だけを追加する。hostでも元ソースを同じ名前へ改名し-Werror=return-typeで失敗を再現し、修正後は改名済み関数を呼ぶ実exeの終了0とmarkerを確認。production実装/fixture期待値は変更しない。51CPU/4capture/旧gateは未実行、初回失敗証拠を保持し新commitで再検証する。

- G2/GR84 1frame回転retarget受入: codec3f7530d/treed4783ad5/run37405962755 attempt1 job112083481089。初回e467b378のC4716を2か所の明示return 0で直し、実Windows51CPU/新2＋旧13marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終1999payload inventory一致、旧coordinate1321file/失敗rotation1765file/失敗FK650file保全。latest3pathsと累積18paths/11Libraryを区別し、productionは初回候補から不変。明示C・heading/T/scale保持・正uniform targetの1frame回転と実Sampler接続だけを受入れ、自動C/role/clip時間列/root/CLI/StageB/GPU/Blenderは未完。 summary SHAe11433d7b58761cffefd834cfc8eefec72d083402543122979d7ace83c74b21b、inventory SHA54835e90c3310aee8c85a8aed1185493f3fc96bd5bf1f5d7a61b0f6ea0dd4482。stale jobs payload/REST commit shapeのchecker拒否は元資料ごと保存。cl/Core/Sampler.exeはCI hash receiptのみで実byte非保存。次は明示補正・sample保持のBVH→複数frame clipを実Samplerへ接続する。

- G2/GR84 sample保持のBVH→回転clip開始。外部role schemaや自動Cを固定せず、全frameを既存1frame検証へ接続して実Samplerで再生できる所有clipを作る。native前計算の大幅なAPI組替えは先行させず、現入口の二乗走査/反復確保も含めてchecked work/byte予算で制限し、無確保・線形時間とは称さない。source側だけは元documentの全値/構造を一度検証したprivate rotation-only planを使い、既存AxisRotation/Multiplyを共用してcaller scratchへ無確保で評価する。generic EvaluateBvhFrameの位置規約・0幅拒否・選択frame検証は変更しない。root deltaはadditiveならraw channels、absoluteならchecked channels−OFFSETとして別診断にし、位置の計算不能でheld-translationの回転clipを落とさない。自動rest補正の元方向一致条件とW=C D C^T Bの不整合は、この単位で黙って解消したことにしない。

- G2/GR84 clipのkey受入経路を具体化。既存Samplerは内部keyでもSlerp/再正規化を行い、区間がEPSILON以下ならalpha=0になるため、単frame生成値の検査だけでは最終clipのkey受入と同値でない。区間算術だけを共有し、厳密増加済みchannelの既知keyをO(1)で同じ式に通す。生成/半球連続化の後、全keyをこの経路でsampleし、追加したnative列値検査で同じWへ再照合する2passにする。work予算もnative2回分を計上し、全補間曲線の保証とは区別する。回転80/100/120度の−X軸fixtureで現在の最大成分正規化による符号反転を必ず踏む。小さい時刻区間の0/90/180度は、生成単frameが通っても実keyがずれる拒否fixtureにする。

- G2/GR84 sample保持clipの検証準備。全元データを検証する借用source回転plan、明示time/axes/C、所有clip/report、root位置診断、半球連続化を実装。完成後の各keyを共有区間式→native供給値検査で再照合し、旧EPSILONによる内部keyのずれを拒否する。静的round1全体PASS、round2追加source境界fixture PASS。実time/整数予算helperはhost通常/O2-NDEBUG/ASan-UBSan（LSan除外）とbundle改名入口でPASS、fast-math拒否も確認。BvhEvaluate.cppはg++構文PASS、native import/testは既存Windows.h依存でhost compile不可・stubなし。Python17+19+12+4+7 normal/-O、BOM/EOL/YAMLと固定harness/6095byteは不変。実Windows53CPU/4capture/旧全gateは次CIで未確認。key以外の全補間曲線・確保故障注入・外部role/CLI/cook/GPU/Blenderは未受入れ。

- G2/GR84 sample保持clip受入: code6e514704/tree7d9b9ac6/run37418209451 attempt1 job112121471665。実53CPU、旧15＋新2marker、4固定6095byte capture、旧89出力、managed17/29 child退出・25+15CLI・12metadata、Python17+19+12+4+7 normal/-O、3ZIP/archived PE-CNGを確認。親readonly再実行exit0・stderr空、240payload inventory一致。summary SHA256 9e876f50b79b7125cd97f516714e157588f3a7c04ae8ce98134bbb7f0f54b621、inventory cb42e8a5632a969b5bf8321589ad8d829c36b9866ea5e0053565d4319ee5e7c6。クラウド環境のローカル消失後、repoを同一HEAD/treeへ復元し現在runと固定参照から新規に検証した。旧受入/失敗の累積ローカル証拠は失われ、復元済みと称さない。cl/Core/Sampler.exe実byteはCI hash記録のみ、exact compile/link commandとnative2全payloadの再検証には保存範囲の制限が残る。生成clipの全stored keyと選択midpointまでのCPU接続を受入れ、全補間曲線・自動C・role・rootMotion・resampling・CLI/cook・StageB・GPU/Blenderは未完。次は明示Add/Replace-by-nameとanimation無しtarget専用decodeを備えたtyped NVSKEL cook接続。

- G2/GR84 typed cook接続開始: 新target専用入口だけ0clip rigを受け、Morph Dropのmesh/node検査をclip loopから共用helperへ切り出した。明示Add/Replace-by-name、毎回名前対応を再解決するBVH import、既存NVSKEL0.2 writer/自己parse、全clip値照合と所有reportの一括公開を接続。旧入口/hashは維持し、新BVH hashには全設定/limitsとraw bytesを含める。NVSKEL出力byte予算は最終配列確保前の境界で、全処理RSS予算ではない。外部role/CLIと増分fingerprintはまだ追加しない。

- G2/GR84 typed cook検証準備: Add/Replace/0clip専用decode、旧clip保持、raw/settings hash、所有結果、実package→AssetSystem→resolved内側parse→Resource/Sampler試験を追加。静的round1の指摘（payload/key保持、palette値、外側正常hashでの内側不正）を補いround2 PASS。既存Python17+19+12+4+7をnormal/-Oで確認、固定6095byteとreceipt不変、YAML/BOM/CRLFを確認。native host構文は既存Windows.hで停止、stubなし。実Windows54CPUと固定capture/旧出力/managed gateは次CIで未確認。forest/129joint/mesh無しのbridge専用拒否fixture、late failure時report全体、複数pair/rootのhash反証は未網羅。

- G2/GR84 cook bridge初回run37424129373（c81e178、54CPU）は新SkeletalBvhCookBridgeTestの共通CookIt CHECKで失敗し、旧53CPUとRelease build/4固定capture/診断byte検査は成功。後続CLI/旧出力比較/native asset-setは実行されていない。共通helperがerrorを出しておらず失敗caseは現ログだけでは特定できないが、positiveの1frame/位置のみBVH fixtureがFrame Timeとsampleを同じ行へ置き、既存parserのFinishLine契約に違反していることを確認。2fixtureの改行を修正し、失敗時にcook error/raw decode status/offset/呼出番号を出す。productionと期待値は変更しない。

- G2/GR84 typed BVH cook受入: codeafdd794f692da30bb930e7c77f1e522b74744cc0/tree6e8b044eb9bdb2542e5222d4dc61ad5e988d8ced/run37425986849 attempt1 job112145637479。実54CPU/18marker、4固定6095byte capture、旧89出力、25+15CLI・12metadata・17/29child退出、Python全group normal/-O、3ZIP/PE-CNGを確認。親readonly再実行exit0・stderr空、387payload inventory一致。summary SHA095a71e2d547e85590c4a4a610140629cb2f5848edda694caff2a8c0ce7f0cdd、inventory cdddf77c1b6fba02f7e15d8c1047986d254dbf3d1c36aa006a654fe186bd1e1f。初回失敗c81e178は別332payload inventoryで親再照合し、最新結果へ置換して隠さない。productionは初回から不変、修正は新test2入力の改行と診断のみ。現runと固定参照の独立受入で、環境消失以前の累積証拠の再現、未archiveバイナリ・native2全payloadの再hashは主張しない。既存128関節/単一root/mesh必須を維持し、型付きcookの生成から実package/AssetSystem解決・実Resource/SamplerまでのCPU経路を受入れた。次はrole-mediated Profile bytesを明示Cの入口へ接続し、外部direct-pair JSONを作らない。

- G2/GR84 role Profile入力の検証準備: quadruped_v1の23roleとroot必須、source/target表、単一/同長chain、必須role追加検査、明示axes/time/Cを所有解析しtyped BVH cookへ接続した。JSON DOM前にbyte/depth/tokenを制限し、source-only名も実BVHへ解決、正準role順と所有report、raw Profile/版/limitsのhashを追加。静的round1 PASS、round2でsource lookup statusと異なるC/回転の実Sampler literal、予算exact境界・limitsのみhash差の追加を確認しPASS。Profile literal JSON/positive BVH行構造、depth5/token45と非可換行列の独立数値確認、旧Python17+19+12+4+7 normal/-O、BOM/EOL/YAMLを確認。native hostは既存Windows.hで停止、stubなし。実Windows54CPU/新role marker/旧全gateは次CIで未確認。chain ordinal>0の実cookは未網羅。外部file/CLI/cache・自動C・ゲーム骨格採用・StageB/GPU品質は未完。

- G2/GR84 role Profile初回run37432555356（961d46dd）は新しい非可換C用SampleRoleCookのResource Load連結CHECKで失敗、旧53CPUは成功。Release build/4固定capture/診断byteは成功、後続CLI/旧出力/native asset-setはskip。stack生成したSkinnedMeshResourceはResourceId=0で、RefreshRenderAssetLeaseが拒否する既存契約をfixtureが満たしていなかった。既に受入済みのpackage fixtureと同じResourceRegistry::CreateTransient経由へ直し、ID非0と各Loadを個別CHECKにする。production/期待行列は変更しない。

- G2/GR84 role Profile受入: code22220d564607a64c925d372c28c3b884ba2bd693/treeff4a232be85d63a704988fb82c6496bc3ed3c5dc/run37435870572 attempt1 job112177403023。CPU54/19marker/4固定6095byte capture/旧89出力/25+15CLI/12metadata/17・29child退出/Python全group normal/-O/3ZIP/PE-CNGを確認。親readonly再実行exit0・stderr空、358payload inventory一致。summary SHA20650c6055bb98c48e53e07bf755e8691f8a4d3f8721199e9f3d7ec6d77a2011、inventory 877250035784feada6a2ddcfec182d5b85b119daeec52d07f720bb3dd785616d。初回961d46dd failedはarchived sourceとimmutable Git objectを正規APIへ照合し、339payload inventoryで親再検証。修正は追加Sampler fixtureのRegistry生成のみでproduction不変。quadruped_v1の所有role解析、正準対応、明示Cと型付きcook/実再生まで受入れた。旧18markerも同時に成功し、chain ordinal>0の実cook・自動rest/C・file/CLI/cache・StageB・GPU/実物は未受入。次は単体BVH/Profile file CLIを常時Cookで接続し、全入力依存/alias/公開前再採取を同時に扱う。CLIの旧narrow argvからUnicodeを推測せず、新modeだけWindows wide command line→strict UTF8→native locatorへ統一する方針。

- GR84 file/CLI候補: 新4引数とWindows wide argv専用入口、値所有要求、bounded BVH/Profile read、typed SourceHash、共通全依存snapshot、常時Cook、2回のwrite直前fresh/guard、実出力recordと有限JSON reportを実装中。新real-process Unicode smokeは旧frozen inventoryと分離。CPU fixtureへ依存変更/alias/Forced/実Samplerを追加。まだ対象Windows CI未実行のため受入れ前。2file transaction/同時writer・自動C/StageB/GPUは保証外。

- GR84 file/CLI受入: code3c0fbe71233ca1964bd5f8fb3d14194ed9197952/treeb7eb83a1cc00288f8c5b94dad41ff7d44da1221b/run37447482776 attempt1 job112215690482。CPU54/20marker、新58実process CLIと125fileartifact、旧89出力・25+15CLI・12metadata・17/29child退出・4固定6095byte captureを確認。GetACP1252下で4Unicode/nonBMP locatorと外部glTF baseを実行し、Profile331byte/FNV2209bd675314cf2b、最終NVPK1411byte/NVSKEL1235byteを独立decode。新旧10checkerのnormal/-O receipt/stdout一致・stderr空、親readonly replay exit0と387payload inventoryを確認。summary SHA0ad23aee0b9707d92e3abd78f7fc57a94a788397e08c3a43fdaab38422c4cfce、inventory 116174249d6d0919098318e9876356e003e5f3d10f5ded0e7bc65a3329b22b98。latest29paths/Library0。現在runと固定参照だけの有限証拠であり、消失した過去証拠の再構築は主張しない。新単体CLIの常時Cookと非transactional publication境界を受入れた。次はGR83の統合cooked資産→所有CPU解析→明示owner上の未登録Resource bundleを最小単位にする。async/cache/M9移行・Stage B・GPUは後続。

- GR83-P1候補: 統合cooked資産の任意論理path→所有CPU解析→事前owner thread上の未登録CreateResource bundleを実装中。UsedCooked/Skl0/format/metadata照合、全clip/表/transform保持、失敗時の旧CPU/out/Registry登録状態保持を独立試験へ追加。Releaseのlogging無効を前提にDebugで3段階profile配送を別検証する計画。現時点は静的確認済み・native未実行であり、async/cache/M9/製品GameThread/GPUは未接続。

- GR83-P1初回CI: code768e247a/run37454499346 job112238665215のRelease buildで、新testのAnsiStringViewと文字列literalの直接比較がMSVC C2678となった。期待側も明示AnsiStringViewにする1行だけを修正する。productionは不変。CPU55/Debug profile/後続CLIはこのrunでは未実行であり成功扱いしない。

- GR83-P1再試行CI: codef78b9ec7/run37456223654 job112244396945はRelease buildと固定4capture/診断比較を通過。CPUは旧54成功・新loader1失敗で、合成manifestの必須cooked_version欠落によりfixture構築で拒否された。fixtureへ明示0と失敗時JSON診断を追加する。productionは初回候補から不変。Debug/後続CLIは未実行。

- GR83-P1受入: codee29a2d3337cb9c3564c794090858debd7e39c567/tree7005c3d9dcc365af061a5481dcfdf8cd39f62610/run37458778661 attempt1 job112252798210。Release55CPU/21marker、direct Debugのloader/Profile、3stage成功/失敗のsink検査、旧89出力/25+15CLI/12metadata/17+29child退出/4固定6095byte captureとGR84別58process/125fileを確認。12checkerのnormal/-O receipt/stdout一致・stderr空。親readonly再実行exit0・470payload inventory一致。summary SHAcb02a58733b5b110e271582aed84e4135518e18bc30f02c1742ef5af493dfa46、inventory 3a01a0c4a9773ab1fabc3cb974c98601d82760e180bfc573d56dbe1bf46b6fd0。latest2paths/Library0、累積11paths/Library4。初回compile失敗399payloadと第二fixture失敗415payloadは独立archiveを親も再検証済み。中間sink entryや未archiveのcompiler/Core/sampler binaryはsource付きCI検査証拠であり、独立byte再実行と混同しない。cold loaderの全clip・未登録所有bundleまで受入れた。次はRegistry部分登録を防ぐ狭い一括公開を先に閉じ、その後event-driven asyncとcacheへ接続する。現public Registerの例外安全性は仮定しない。製品の二つのGEngineと実ApplicationProcessor経路を区別し、製品配送・M9・StageB・GPUは後続とする。

- GR83-P2a候補: P1を呼ぶopaque prepared値とRegistry session epoch、正確なkey/URI/child handlesを確認するcache Acquire、4型shadow poolの一括公開を実装中。既存public Registerは変更せず、欠損型placeholder準備後のnoexcept swapで全-or-zeroを閉じる。copied metadata件数と有限予算を持ち、cold公開O(既存pool)/累積O(K²)の費用を明示する。旧55＋新publication＋関連Registry4件でRelease60、Debugでも関連契約を実行する候補。現時点は静的確認済み・native未実行、async/製品loop/M9/StageB/GPUは未接続。

### GR83 一括公開のWindowsコンパイル修正（2026-10-06）

- code8e913414 / run37471551402 / job112296050946 は新しい SkeletalAssetPublicationTest の std::filesystem 宣言不足（C2079、temp_directory_path 等の未宣言）でRelease buildが失敗した。CPU60・Debug・後続CLIは未実行。
- 新試験へ必要な <filesystem> を直接includeする。production、期待値、固定基準、既存試験本文は変更しない。修正後のWindows CIで再検証し、成功前に完了扱いしない。

### GR83 骨格bundle一括公開のCPU受入（2026-10-06）

- Done: 177c1b69924566568376987c18e7ea6b34b80d59 / tree780f3603ec5a5086ec81cadee5094b20b3e691b9 / run37474221718 / job112305303905。Release60件とDebugのpublication/loader/Profile/Registry4member、旧cook/CLI/固定Samplerを通過。修正は新試験includeだけでproduction不変。
- 検証: 7比較器をnormal/-Oで計14回実行しreceipt/stdout一致・stderr空。保存済み証拠の親再実行もexit0、541payload一致。summary SHA256 8151ad7cc3a0c750cc13879a111aec33d15583d67fdc058bff30559664f7a129、inventory SHA256 4c68486b34223b98c8dfa749545c198631f029b15089ee6725ece3672bb89e5a。
- 失敗run37471551402はReleaseコンパイル失敗のまま別保全し、親readonly replayは421payload/exit0/stderr空。成功と混同しない。
- 範囲: Registry session付きprepared、子とaggregateの全部-or-zero登録、同key共有・typed handles・予算拒否・GC/lease保持をCPUで受入。shadow copyは追加ごとO(既存pool)、累積O(K²)。cold公開の正しさを保証する初期実装で高スループット/RSS上限を保証しない。
- Notes: 実行中の内部状態・profile sink配送はsource固定CI検査、未archive compiler/Core/Debug試験binaryはhash attestation。Windows nativeをLinuxで再実行したとは扱わない。製品GameThread/async/delegate/M9/StageB/GPUは未接続。

### GR83 有限ジョブ投入の事前契約（2026-10-06）

- In progress: G2-GR83-FINITE-SUBMIT-SAFETY。P2aの受入れ後、event-drivenロードに必要なJobSystemの狭いaccounting例外安全を修正する。
- 方針: finite countを増やす前に専用ticketとhandlerを準備し、queue公開成功後に同じgate内でarmする。完了観測とcounted印は同じfinite State mutexで管理する。登録済み失敗handlerは未armのまま残ってもcountに作用しない。
- 検証予定: JobSystemShutdownTestのDebug/Release、既存model非同期3束、境界注入と重複/同期完了/Drain fence/世代。一般Taskの例外隔離は変更せず、後続の骨格worker/ready通知には外側catchと事前確保slot/ackを別途必要とする。

- 検証範囲補足: 既存SnapshotReloadはassert内でInitialize/manifest読込を実行し、ModelAsyncLoadQueueもCloseにside effectがある。これらを新規Releaseゲートへ足さず、Debugの関連回帰3束で確認する。Releaseは旧60＋JobSystemの61件とし、有限ticketの常時有効検査はDebug/Releaseで実行する。

- 反証追加: AfterHandlerで止めた投入にStop/Shutdownを競合させ、成功/例外とclose先行拒否を3経路で確認する。Drain/closeがresize lock内へ到達した通知を使い、呼出前signalだけに依存しない。

### GR83 有限ジョブ投入のCPU受入（2026-10-07 JST）

- Done: 6a2d9815cc6840b9d03c91c87bd5bd3848c2b661 / tree4e4ec3c54d3c3bc98ccfad087002bec0b6f8e22c / run37483939116 / job112339002242。Release61とDebugのfinite試験・model非同期3束、既存loader/publication/Registry/Profileを確認した。旧60cpp・cook/CLI/固定Samplerは維持。
- 検証: 8比較器のnormal/-O計16回でreceipt/stdout一致・stderr空、親readonly replay exit0・629payload一致。summary SHA256 15910b58ed23720e63723c2fc6665921292461d653733f2c77dea8301f454881、inventory SHA256 76c41a0033eb875c77d30d9a0805f8e7921e605ed72d4e3e069aa056a92511e7。
- 範囲: submission別の未arm ticket、準備例外、遅い失敗handler、同期/重複、実resize fenceでのStop/Shutdown/Drain競合、旧世代参照を受入。一般Task worker/handler例外や全allocator OOMは保証しない。Drainのterminal計上と、consumerへの完了配送は異なる境界のまま。
- Notes: Runtime内部状態は固定sourceとnative CHECKの証拠。保存済みbyte比較と未archive binaryのhash attestationを区別する。製品loop/骨格event-ready/delegate/M9/StageB/GPUは未接続。

### GR83 統合event runtimeの事前契約（2026-10-07 JST）

- In progress: G2-GR83-SKELETAL-EVENT-RUNTIME。P1/P2a/finite-submit受入を土台に、骨格専用instanceでworker→ready→owner公開→delegateを接続する。queueだけの別完了にはしない。
- 方針: Bind/Load/Flush/SetSnapshotは明示owner限定、Cancel/Close/Drainはmutex管理。private Taskのworkerを外側catchで包み、eventは事前slotのlinkとhandoff ackのみ。Flush開始時のbatchとcallback配列を固定し、再入Loadは次Flushへ送る。
- 寿命: Registry sessionの専用単調domainでcacheを隔離し、登録はState→Registryの最終gateで行う。Closeは未開始配送を抑止し、Drainはsubmission/handoff/active配送/取消pinの終了後に確保なしで所有を解放する。一般並行Registry破棄・製品loop・M9・StageB・GPUは対象外。

- 反証補強: in-flight索引をIdentityへ合わせ、保持view/Request完全keyを照合して衝突を拒否する。early-terminalの正常受理→ready→公開/通知各1、commit内State mutexの別thread取得不能/外側取得可能を明示検査する。

### GR83 統合fixtureのmanifest境界修正（2026-10-07 JST）

- code61f325fe / run37498380281 / job112388696429 はRelease build・固定Sampler・旧61CPUを通過したが、新runtime試験の最初のmanifest構築で失敗した。Debugと後続CLIは未実行。
- 原因: 既存AssetManifest::TryReadStringMemberはIsAsciiJsonStringで非ASCIIを拒否する。新しい正例fixtureが非ASCII logical_pathを混ぜていた。正常4資産はActors/A〜Dへ修正し、UTF-8構文受理→未登録pathのResolveRejected、manifest側の既存拒否を別に確認する。診断にはparse status/error/JSONを追加する。
- production・旧parser・固定基準は変更しない。非ASCII論理pathのcooked成功を受入範囲から区別し、GR84の物理Unicode locatorと混同しない。

### GR83 統合event runtimeのCPU受入（2026-10-07 JST）

- Done: 2d69f4652e0d695bfaaf74bb38f9124957ba907f / treeb7a6beea3f4a1c9bdf7ddac89b0b8d3590754739 / run37502233582 / job112401887301。Release62とDebug runtime、両構成28子process/36 subcase marker、既存loader/Profile/publication/Registry/finite/model3を確認。
- 検証: 9比較器のnormal/-O計18回でreceipt/stdout一致・stderr空、親readonly replay exit0・672payload一致。summary SHA256 3e8fa06936d202ce5d8b5c5070811237c862565741e5d685b3c58fa146b5fa48、inventory SHA256 6837f8640825b3634333fb7410c8f541fc293f0cd3ab0daf0ae1d76e0bec14ca。
- 旧証拠: 旧61cpp・固定Sampler4本・89出力・25+15CLI・12metadata・17/29 child退出・GR84別58process/125file・PE-CNG/SHELL32を維持。初回run37498380281のmanifest失敗は545payloadの別archive、親再実行exit0で失敗のまま保持する。
- 範囲: 明示ownerの独立CPU runtimeまで。受理前拒否、早期worker完了、有限batch再入、実State→Registry gate、取消/Close、handoff ack/複数Drain/capture破棄、snapshot/domain/GCの契約を受入。consumer target寿命・借用依存のlifecycle排他はcallerの責任。
- Notes: 既存manifest ASCII制約のため非ASCII論理pathはtyped拒否を確認し、読込成功とはしない。private境界注入とnative CHECK、保存済みbyte比較、未archive binaryのhash attestationを区別する。製品GameThread/実loop/M9/StageB/GPU/実キャラ品質は未接続。

### GR83 製品owner/M9接続の事前契約（2026-10-07 JST）

- In progress: G2-GR83-OWNER-LIFECYCLE-M9-DELEGATE。event runtimeのrun37502233582受入と文書6ad01237を前提に、実ApplicationProcessorから通常GEngine所有sessionを駆動する。
- 方針: NorvesEngineの別renderer lifecycleは丸ごと動かさない。Registry所有/借用、明示owner、初回immutable snapshot pin、Close/Drain/consumer解体後Endを小さいproduction helperにまとめる。M9だけがsnapshotをBindし、通常起動の既存reloadを保つ。
- M9: 現CreateTransientは未登録でなく逐次登録。runtimeの全clip一括公開と名前指定Waveへ置換し、弱いcompletion eventをowner更新で一度consumeしてattachする。pause/failed Enter/Leave/再初期化の寿命を反証する。
- 限界: M9中の別snapshot reloadはrenderer更新前に拒否し、一部世代更新を避ける。Registry typed handleはsession epochを含まないため、所有session終了時にconsumer参照/handleを破棄する。実描画/XAudio2/実犬/DCCは別証拠のまま。

- activation境界: 消費済み/attach済みの再Prepareはaudio/config変更前に拒否する。Leave後はfresh Prepareが必要で、無準備/消費済みの再EnterをFailedへ流す。実productionのCanPrepare/CanEnterをCPU常時検査とsource配線検査で反証する。native検証は未実行。
- 実装候補: owner sessionと実loop、M9の弱参照一回event・明示clip・activation境界、実World/Scope attach helperを接続。Python source8件をnormal/-Oで各合格、workflow YAML parse・新規C++ BOM/CRLF・既存行末比較・diff whitespaceを確認。固定Sampler2原本6095bytes/SHAを保持。Release63とDebugのCPU契約、Game両構成compile、既存cook/CLI比較はCI待ち。

- 初回Windows CI: e02bd284 / run37516423509 / job112450358893はRelease bundle buildで失敗。新SkeletalAssetSessionTestの3箇所がconst lvalueをSetClip(rvalue専用)へ渡しMSVC C2664になった。所有copyを明示生成するfixtureだけを修正し、productionは変更しない。CPU63/Debug/Gameと後段CLIはskippedで未検証。失敗run原本は別archiveとして保持する。

### GR83 製品owner/M9接続のCPU受入（2026-10-07 JST）

- Done: a082707cc00b2d5d0c56c3e2642b455bb8fa8706 / tree 825dcb674755c992b4e707fb2baaf83f37fbfbe1 / run 37519489315 / job 112460910508。実ApplicationProcessorのBegin・pause外配送・Close/Drain・consumer解体後End、owned/borrowed Registry、M9の一回eventと明示clip・activation境界を接続した。
- 検証: Release 63、owner両構成20 child processと20 case marker、Python source 8件×2、M9 ContractOnly Allのprocess自己試験、Game Debug/Release compile/linkを確認。実Game起動・GPU描画・XAudio2音声・既定画面撮影ではない。未archiveのGame binary hashはCI attestationとして区別する。
- 互換: 旧62 cpp、runtime 28子process/36 marker、既存Debug/loader/Profile/publication/Registry/finite/model、固定4×6095 byte capture、旧89出力・25+15 CLI・12 metadata・17/29 child退出・GR84別58 process/125 file・PE-CNG/SHELL32を保持。
- 証拠: 10比較器normal/-Oの計20回でreceipt/stdout一致・stderr空。親readonly replay exit0、711 payload・23原本API・6 ZIP・427 source一致。summary SHA256 0919122fd6ab9efaae432796d532a4ac97cacd76ebce5660714dad19936e408e、inventory SHA256 88444cbd7eb58b15f07ba499d13f2373379cfb5e66372de18482d439781d3bac。
- 初回失敗: e02bd284 / run 37516423509のC2664をfixtureの所有copy3箇所だけで修正。productionは初回から不変。初回548 payloadは失敗のまま別保存、親の失敗保存replayもexit0。latest 2 paths/Library 0、累積26 paths/Library 9。
- Notes: 元golden生成時のSampler sourceと、受入済み抽出後sourceのhashは別のimmutable参照で照合した。原本ログのstep BOM・upload path継続行・CRLFの書式対応を比較器へ加え、変更前比較器と失敗stderr/patchを保持する。golden・native gateは変更していない。
- 範囲: 実Appの配線位置とGameビルド、実production CPU helperの寿命・取消・attachまで。実App lifecycleのOS/GPU実行、M9 t0/t1・negative control・音声drain・実犬/Blender品質は別受入。次はGR82 Stage Bのauthor-rest付きClipBankと安全束縛へ進む。

### GR82 Stage B1の事前契約（2026-10-07 JST）

- In progress: G2-GR82-B1-CLIPBANK-REST-BIND-POSE。基点はowner接続の受入code a082707cと結果文書dd24b999。原本/現在のユーザー条件/現decoderを再照合し、v1 ClipBankの保存→parse→安全束縛→実Resource/poseを次の一件とする。
- 作者rest: IBMからのbindやclipのt=0を代用品にせず、提供された作成元rigのnode local TRSを保存する。現在rigと別入力にし、古いactionを新restで再exportしたファイルから失われた履歴を復元できるとは扱わない。
- 互換: 新しい明示v1入口だけを追加。既存0.xの値/拒否/bytesとlegacy samplerを保持する。正のnonuniform TRSとq/-qを扱い、初回のmatrix/反射/特異scale/非joint親は明示未対応とする。
- 後続: 残るSkeleton/SkinnedMesh wire、三論理path・原子的な公開/既存delegate runtime、Armature/clip-only、GR86の256、metadataはStage B内の別境界。今回だけでGR82全体/G2をDoneにしない。
- 開始時検査: owner source8件はPASS。新v1型のLinux syntax-onlyは既存Containers.hのWindows.h依存で実行不能を確認した。ヘッダstubを用意せずWindows native CIで検査し、Linux成功として扱わない。
- 実装候補: source rigの作者TRS、必須snapshot付きv1 ClipBank、名前/topology/rest束縛、実ownerの未登録ResourceとSampler既定pose分岐を接続。tiny/huge quaternionを旧SamplerのIdentity/zeroへ落とさないv1拒否と、animation Scale正値を追加した。v1 importのcount/累積sample/外部実bytes予算を確保前へ接続し、LINEAR/STEP/Bake・共有accessor・末尾増幅・累積2fileを反証する新CPU試験を追加。nativeは未実行。
- 検証準備: 独立v1 oracle8件と既存owner source8件をnormal/-Oで各合格。新wireの独立literalは992 bytes/SHA256 43541886df2bce92e81697ed0b0d0ac6b1285201cf20470089ad3190761af106。legacy Sampler新分岐を除去するとbase dd24全文がwhitespace正規化で一致（11171428e0e187c5ecf15f3279d2f49c4ab6226842012008b9140b7ca06c63c6）。YAML・BOM/CRLF・行末比較も確認。Release65/新2束のDebug・実出力oracle・既存cook/CLIはCI待ち。LINEAR/STEPとBakeが同一assetで混在する専用の累積境界試験は残す（残量の実装は共通）。

- 初回B1 Windows CI: code 53f17969 / run 37534505686 / job 112511925333はRelease buildで失敗。新CookedClipBankV1Testの変数smallがWindows macroと衝突しC2628等になった。fixture変数をlowLimitsへ変更し、production/旧test/goldenは不変。Sampler/CPU65/Debug/oracle/Game/旧CLI後段はskippedで未実行。失敗599 payloadは別保持し、親の失敗readonly replayもexit0/stderr0で確認した。

### GR82 Stage B1のCPU受入（2026-10-07 JST）

- Done: code 7b423fb169ac57424348085bb42d2a676d19d4d3 / tree c10b19dec24d591aa9a4a0bb29a90a354530a9d5 / run 37537055880 / job 112520629199。作者TRSの所有import、author-rest必須v1 ClipBankの保存/parse、正準topologyと全jointの差検査、既定拒否/明示override、未登録owner Resourceと実Samplerまで接続した。
- 検証: Release65、新2membersのdirect Debug、codec3/binding3のcase markerを各構成で確認。新v1の992byte bankをDebug/Releaseとも独立literalと一致確認（SHA256 43541886df2bce92e81697ed0b0d0ac6b1285201cf20470089ad3190761af106）。native rest-binding JSON2本とoracle8件×normal/-Oも確認。
- 安全境界: 同名同階層/順序違い、全joint Translation、未アニメjoint、T/R/nonuniform scaleと閾値、q/-q、tiny/huge q・非正Scale拒否、複数author snapshot、owner/未登録失敗原子性を反証。importのcount・共有accessor・LINEAR/STEP/Bake sample・外部末尾/累積file/data URI増幅は明示予算内に制限する。全process RSSや任意入力の実行時間の保証ではない。
- 互換: 旧63 cpp、owner20/runtime28 child、既存Debug/loader/Profile/publication/Registry/finite/model、Game両構成build-only、旧4×6095byte capture、旧89出力/25+15 CLI/12 metadata/17+29 child/GR84別58 process・125 file/PE-CNG/SHELL32を維持。legacy Sampler分岐復元後の全文正規化一致と、変更された新分岐の別oracleを分離した。
- 証拠: 11比較器normal/-Oの計22回でreceipt/stdout一致・stderr空。親自身readonly replay exit0、788 payload・448 source・21原API・6 ZIPを確認。summary SHA256 d7e0a27e33ae696cbc73f338bca719ffd571f247e7f215ab65e1c287809dcf4f、inventory SHA256 465c95f53411b1c0e5cb10de689159efbccb76a9922e22f9a051b4ce09278bc1。
- 初回失敗: 53f17969 / run 37534505686はtest変数smallとWindows macroの衝突。lowLimitsへの3識別子置換と文書だけを修正し、全Library/AssetCook productionは初回と不変。失敗599 payloadは別保存。latest2 paths/Library0、累積31 paths/Library19。比較器のstdlib import補正は元script/stderr/patchを保持し、原本やnative gateを変更していない。
- Notes: B1はdirect TRS・正scale・single root・外部親なし・128以下のprofile。失われた過去restの復元、三資産cold-load/publication/runtime、Armature/clip-only/256、GPU/DCC/実犬品質とGame実行はまだ受入れていない。混在Line/Step/Bake専用累積境界試験は残る。次は残二roleと同snapshot cold-loadの接続を行う。

### GR82 Stage B2の事前契約（2026-10-07 JST）

- In progress: G2-GR82-B2-SPLIT-WIRE-COLD-LOAD。B1 code 7b423fb1 / run37537055880と受入文書c8a3bd6fを基点に、残二roleの保存から同snapshot cold-load・未登録owner組立・実姿勢までを次の一件にする。
- 推奨判断: B2は明示library入口に限定し、新CLIとファイル公開transactionは分ける。Skeletonはcurrent rest/正準topology/ROOT、MeshはIBM/M/geometry/materialsを所有する。B1のClipBank wireを維持し、MeshはSkeletonの完全topology・全wire内容・rest/ROOT hashをpinする。
- 入力: geometryとmaterialは同じ読込済みsettings/buffersを使い、source material indexと生成slotの対応を明示保持する。全MATSをCPU所有し、未対応の描画を単一Opaque fallbackで隠さない。per-file/全packageとimageの有限予算は確保前に検査する。
- 開始gate: B1親readonly replay exit0と独立oracle8件normal/-O、旧互換を受入済み。B2 native/sourceレビューは未実行。Armature/256、三資産runtime公開/cacheとGPU/DCCは後続境界として残す。

- 実装候補: 残二roleのwire、Mesh側IBM/Mと全MATS、immutable Skeleton共有、ordered Bank束縛、同snapshotの実package cold-load、未登録owner/別Sampler枝、新library cookを接続した。material/source-slotは同readのdocument/bufferを使い、取得時canonical locatorを保存する。画像はsplit専用stb workspace・累積output/copy、ARM2copy、role参照文字列alias、source名/suffixを確保前に制限する。
- 検証準備: 独立wire/pose oracle7件normal/-O、既存B1 oracle8件とowner source8件normal/-O、二段Sampler全文復元、B1 rest比較blockの抽出一致、YAML/BOM/行末を確認。新literalはSkeleton704/Mesh1360byte。Release69・新4members direct Debug・旧cook/CLI互換はnative CI待ちで、C++成功とは扱わない。
- Notes: 初回profileのMSLT/MATSは同件数、false cookでSuccess statusを残さない。新probeは確保直前の限定観測であり全allocator/RSSの計測ではない。一部probeの成功positive control、極小limit時の固定生成base、2snapshot/既登録pool/Load内部失敗の追加観測は未網羅として残す。GPU/DCC・CLI公開・Registry/runtime接続はこの一件の受入に含めない。

- 初回B2 Windows CI: code0043cdb3 / tree02d5f024 / run37550413059 / job112564262923。Release69と新4direct Debug・新12case markerまで成功したが、新oracleのMesh全byte比較で停止した。独立期待値が既存decoderのtriangle巻き順反転を落としていた。実Debug/ReleaseはともにINDX=[0,2,1]で、旧c8a3/dd24のswapと旧CookedSkeletalAssetTestの固定検査に一致する。差はbyte932/936と従属payload hash48–55のみ、他9sectionは一致。
- 修正: 新Python oracleのindex列だけを0,2,1へ正し、巻き順自己試験を追加（8件）。production/旧golden/旧65cppは変更しない。初回の原oracle・ログ・480source・ZIPは失敗のまま別保持する。新oracleによる初回出力の再照合は診断であり、skippedになったB1 oracle・finite/Game/CLI等の実行を補ったとは扱わない。全後段を新codeのCIで確認する。

### GR82 Stage B2のCPU受入（2026-10-07 JST）

- Done: code ef4cac08e77f05e4df29a53345b81e3d4ab9cb89 / tree e5a2740d11842d6c4d9eb7ac39d517f38c4835cc / run37553853287 / job112575390064。SkeletonとMeshの分離保存、同snapshot三資産読込、全Bankの安全束縛、未登録owner組立と実Samplerまで接続した。IBM/MはMesh専有、全MATSはCPU所有し、render leaseを発行しない。
- 検証: Release69・新4 direct Debug・12case各構成、独立wire/pose oracle8件×normal/-O。実Skeleton704byte（SHA256 15bab8e1f817e8e06ac8d54dbb5a2d8dce9ad9492e33e0327cc0df08b245ce7d）、Mesh1360byte（0ad749a7ad36d228346b9edb64dabe228cd9d9ad0aef75b97a519494ec22832b）を両構成で全byte一致確認。poseはclips2/child palette Y1/model Y2/vertex(-5,2)、materials_render_staged=false。
- 互換: B1の992byteとrest report、旧65 cpp・owner20/runtime28、有限投入/Registry/関連Debug、Game両構成build-only、固定4×6095byte、旧89出力・25+15 CLI・12metadata・17/29 child退出・GR84別58process/125file・PE-CNG/SHELL32を維持した。旧Samplerは二段の分岐除去後の全文正規化一致と固定出力を別々に照合した。
- 証拠: 12比較器×normal/-O計24回のreceipt/stdout一致・stderr空。親自身のreadonly replayもexit0/stderr0、827payload・480source・18原API・6ZIP一致。summary SHA256 2b043a9eb825022b8a9ddab864ab50499dca19a9b2758634bf738a378d1d811f、inventory SHA256 ba70c6380d6051c7d827c27a44f68252b73faecac04d8d1cad1e6286533001bb。
- 初回失敗: code0043cdb3 / run37550413059の新mesh比較器だけが既存triangle巻き順変換を落としていた。Python期待値と新自己試験・文書だけを修正し、478/480 sourceと全production/旧test/golden treeは不変。失敗710payloadは別保持し、失敗保存の親replayもexit0。latest2paths/Library0と累積57paths/Library37を区別する。
- Notes: 同asset内のLINEAR/STEP/Bake累積境界はB2で新検査した。一部確保probeのpositive control、2snapshot/既登録pool/Load内部失敗等は追加観測として残る。全allocator OOM/RSS/任意入力時間は保証しない。実GPU/DCC/Game起動、Registry/cache/runtime公開、新CLI/ファイル群公開、Armature/clip-only/256は未受入。次は既存GR83の一括公開とdelegate runtimeへ分離資産を接続する。

### GR82 Stage B3の事前契約（2026-10-07 JST）

- In progress: G2-GR82-B3-SPLIT-BUNDLE-PUBLICATION-RUNTIME。B2 codeef4cac08/run37553853287の親readonly受入と文書ce98f49fを基点に、分離資産を既存GR83へ接続する。
- 推奨判断: 既存runtimeに入力modeと有限な所有identityを追加し、workerはB2 loader、ownerは未登録組立と既存4型shadow commitを使う。要求policy/許容/予算/順序/manifest全参照を区別し、strict要求がoverride cacheへ合流しないことを優先する。
- 寿命: receiptは完成aggregateに持たせ、再取得時は実child handleと全clip値を照合する。別bundle間のSkeletonResource ID共有は行わず、同一のimmutable骨格内容だけを共有可能にする。既存session/ready/handoff/State→Registry gateを再実装しない。
- 開始gate: B2の12比較器×2、親replay exit0/stderr0、827payloadを確認済み。B3 nativeは未実行。GPU/CLI/Armature/256・一般OOM/RSS/並行Registry破棄は対象外。

- 実装候補: 分離要求と全field identity、同read証拠を持つopaque receipt、未登録組立から既存4型公開、同runtimeのworker/ready/delegateを接続した。成功callback内からの再購読も全内容を再照合し、clip改変/receipt消去時は新要求だけを拒否する。legacy入口もsplit childを識別し、receipt消去で別modeへ落とさない。
- 検証準備: 旧69 cpp、B2/B1 wireと両Sampler/oracleは不変。独立split8・bank8・owner source8をnormal/-Oで合格、YAML・BOM/CRLF・行末とwhitespaceを確認。新publication5caseとruntime8scenario×2 schedulerのDebug/Release常時検査を追加し、実Session pause helperとhandoff待機開始も反証する。Release71、新2 direct Debug、既存cook/CLI/固定captureのnativeはCI待ち。

### GR82 Stage B3のCPU受入（2026-10-07 JST）

- Done: code55831fe8f5563c936f6a9634ed06d7abeffb8b85 / treeb83204ae3963d1d15ec703c8be8df7b14875c17c / run37564096257 attempt1 / job112607753170。分離三資産を既存GR83のworker/ready/handoff/owner四型公開/delegateへ接続した。同じinstanceにlegacyとsplitを混在させ、別queue/sessionを増やしていない。
- 検証: Release71は旧69のsource/順序を保持し、新publication/runtimeのdirect Debugも成功。publication5case各構成、runtime8scenario×2schedulerの16child各構成/計32。全field要求identity、strictとoverride、Bank順/内容・許容・予算・snapshot世代、全clip変更、完了callback内の再要求、準備故障/取消/Close/Drain/再入と実Session pause外配送を常時CHECKで反証した。
- 公開契約: 3clipでResource+6/path+1、実typed handles、失敗時の既存pool/records/会計保持、receipt/clip寿命とGC、同骨格内容/異なるMeshのCPU姿勢を確認。成功後の再購読にも共有cache検査を適用し、改変を検出した新subscriberだけを拒否する。有限budgetは一般OOM/RSSや時間上限の保証ではない。
- 互換: B1/B2 wireと両Sampler/旧69 cppは不変。B1の992byte、B2の704/1360byteとpose、固定4×6095byte、owner20/runtime旧28、関連Debug/finite/Registry、Game両構成build-only、旧89出力/25+15CLI/12metadata/17+29child/GR84別58process・125file/PEを維持した。
- 証拠: 13比較器×normal/-O計26回でreceipt/stdout一致・stderr空。親自身のreadonly再実行exit0/stderr0、1095payload・488source・82原API・6ZIPを確認。summary SHA256 09be5c1c1e3a38b18f6bcc0739f5a5915927fb918acbefc8da5e80d6293442c1、inventory SHA256 a312fbfa6b5092e2c5d55574f2d0435a10b74201c484d5e20b74e262ce4bf1c4。新しいin-memory状態は固定sourceとWindows CHECKの実行証拠であり、独立した全状態dump再実行とは区別する。
- Notes: native初回成功、source修正・CI再試行なし。外部比較器の旧定数alias漏れと、封印stdoutをinventory内へ置いたことによる初回hash差は、元script/失敗stderrを保持して補正した。原native/API/ZIP/sourceは変更しない。変更26paths/Library18。CPU公開成功をGPU描画やファイル群transactionの成功として扱わない。
- 残る境界: Armature/非関節親・作者root frame、clip-only、GR86の256、要約/metadataとCLI/file公開、GPU/DCC/実物品質。別bundleのSkeletonResource wrapper/IDは独立で、内容を共有可能にする契約まで。次は128を保つ明示profileで静的Armature親と作者frameの安全検査を接続する。

### GR82 Stage B4の事前契約（2026-10-07 JST）

- In progress: G2-GR82-B4-STATIC-ROOT-FRAME128。B3 code55831fe8/run37564096257の親readonly受入と文書5a64431bを基点とする。旧profile1を残して、静的な非関節親の明示profile2を追加する。
- 推奨判断: skin.skeleton省略はgraphから一意のroot jointを選び、名前Armatureの特例を作らない。root上の祖先は静的・正一様TRSだけ。current ROOTとclip作者snapshot別のAFRMを所有し、完全frame差はrest overrideでも通さない。frame変更の自動補正/retargetはしない。
- 束縛: 新profile2ではowner組立で検証済みclipのproofを発行し、SetClip/Unloadで失効させる。直接Sampleも別target/無証明を拒否する。profile1/legacyの手作りclipや旧wireを変更しない。
- 開始gate: B3の13比較器×2・親replay exit0/stderr0・1095payloadを確認済み。B4は未実装/未検証。clip-onlyと256、要約/CLI、GPU/DCCは別境界として残す。

- 実装候補: 明示profile2の静的非関節祖先とskin.skeleton省略、作者frameの同read所有、Skeleton ROOT/Bank必須AFRM、全snapshot frame照合、既存三資産runtime、失効可能なclip束縛proofを接続した。AFRMは既存7節の意味・添字を保つ末尾必須節とし、既定profile1のbytes/hash/keyを保つ。
- 検証準備: 独立profile2 oracle11件をnormal/-Oで合格。合成の704/1360/1088byte literalと、非対角G＋実root T/R/Sを含む全joint行列・頂点の独立double期待を固定した。新3membersをRelease74/直接Debugへ追加し、runtimeは2schedulerの子processで検証する。旧profile1 oracle/owner source、YAML/BOM/CRLF/行末/whitespaceを確認。新C++ nativeは未実行でCI待ち。
- 追加観測: 未アニメjointを含むprofile2専用oracle、非直角の任意回転、node配列自体の交換、AFRM専用のguard/allocation probe、微小frame差、Unload単独、profile2で既存成功poolを保持する失敗と追加の累積予算ケースは残す。一般RSS/全allocator OOM・GPU/DCC/Game実行をこの候補の成功としない。

### GR82 メッシュ非依存のクリップ入力（2026-10-07）

- メッシュ/IBMなしのskin付きglTFと、作者側joint nodeを明示したskinなしglTFからClipBankを作る入口を接続。restはsource nodeからのみ取得し、既存のframe/rest束縛検査へ渡す。一様scaleを許可し、形状のないfitは拒否。既定のmesh付き入口は不変。
- 既存CookedClipBankV1Testに作者rest、skin有無・joint順でのwire一致、不正選択、非indexed mesh、scale/fitのケースを追加。fixture JSON変換・差分の行末/whitespace確認済み。C++実行は未検証。変更単位のCIは実施しない。

### GR86 分離v1の256関節（2026-10-07）

- 明示profile 3のdecode/cook、3資産のwire・manifest・束縛・CPU評価へ256関節を接続。旧形式とprofile 1/2の128制限は保持。
- 既存テストへ129/256関節のcook→3資産parse→束縛→CPU姿勢、clip抽出、旧profile拒否、257拒否を追加。合成入力の構造・buffer範囲と差分衛生を確認。C++実行は未検証、CI未実行。

### GR82 クリップ解析と保存（2026-10-07）

- 端点のloop候補、根の平面移動・平均速度・累積yaw、明示fps/timeScale補正を実装。作者rest/階層/ROOTを使い、任意ANLY節へ保存。既定の解析なしwire/hashは保持。cook要求から明示選択できる。
- 既存テストに周期・直進2m/s・90度旋回・時刻補正・ANLY往復と不正値拒否を追加。source確認とyaw式の数値確認、差分衛生まで。C++実行は未検証、CI未実行。

### GR82 分離rigのファイルCLI（2026-10-07）

- `--rig-split` からprofile 3/256・解析・名前指定root・材質textureを含むcookを接続。native Unicode入力/出力、実package/manifestのbytes照合と3資産のparse/束縛、新規directoryのno-replace公開を実装。既存出力を上書きしない。
- 既存RigSplitCookTestにファイル公開・既存出力保持・root名拒否・texture同梱を追加。親segment/末尾separator/非ASCII親directoryのケースを含む。実CLI用の小さいsmokeも用意したが未実行。追加C++の実行確認は未検証で、毎変更CIは行わない。

### GR96 種別横断の一括・増分cook（2026-10-07 JST）

- 実装: spec v2のraw/texture/audio/model/skeletal/animation、分離rig三資産とclip専用入力、共通の依存snapshot・manifest・transaction、資産/派生出力の増減、予算超過exit 2とJSON/Markdown統計を接続した。asset-setのemissiveNitsPerUnitは静的v1材質と分離rigへ渡す。
- 並列: --jobs 1を既定、明示1〜64。stage内cookだけを並列化し、全workerをjoinしてから入力順に集約・公開する。出力名とmanifestはjobs数に依存しない。
- 境界: 増分で追加するpackageの親directoryは既存であること。新しい階層は新規runtimeへのcookで作る。--prune指定時だけspecから外した資産を所有state/manifestから除き、package自体は消さない。同じ既存fileの自動再採用は拒否する。
- 検証準備: 既存試験に5種混在+独立clip、jobs 1/4の全package/manifest一致、全skip、外部buffer/sidecar更新、派生画像の増減、予算とreport障害、未所有出力の拒否を追加した。クリップで実際に読んだsource/sidecar/bufferと依存snapshotを照合する。C++実行はまだ未検証。
- GR82/GR86の前回CI: ab3126feのrun 37600512793はRigSplitFileCliの文字列ビュー比較でbuild失敗。比較の型を明示する修正を入れた。後続runtime/CLI検証は未実行だったため、合格には扱わない。
- 範囲: 要件GR96の「実装順」節に従い、G2はspec v2・増分・統合manifest・三角形/関節/texture予算と最小レポートまで。詳細異常検出とgeneratorsは後段。--forceで全件を再cookし、--verify相当のpayload照合は通常時も行う。
- 残り: GR96の関連実行検証と、GR84のrest補正・向き/ルート分離・リサンプル・周期切り出しとv1/glTF接続。G2は未完了。ロードマップの区切りで必要な検証をまとめ、完了後にマージしてG3へ進む。

### GR84 共通クリップ処理とv1出力（2026-10-07）

- BVH/glTFのlocal補間→FK→作者rest差分対応、明示/match/align_bones補正、headingと平面ルート軌跡、出力fps・auto/none/range周期処理を接続。骨長重み/role除外、角速度seam、巨大frame原点に依存しない軌跡を扱う。
- 任意RMTN節で累積軌跡を保存し、既存ANLYへ要約する。ターゲットの作者rest/frameを保持して既存の厳密束縛へ渡す。再生時適用はG3/G4。
- --retarget-clipの新規出力CLIとasset-set v2のskeleton_path/role_profile/source_clip/clip_nameを接続。増分判定は既存C++の1か所を使い、source/target外部buffer・sidecar・profile bytesを含める。
- 合成反証を既存CPU試験とCLI smokeへ追加。Python構文と差分衛生のみ確認、native実行は未検証。詳細はDocs/Architecture/SkeletalClipProcessing.md。
- GR96のrun37607084130はRigSingleCookのAnsiString範囲構築でbuild失敗。aa293f28でStringViewを明示して修正。後続試験は未実行で合格扱いにしない。
- G2は未完了・未マージ。関連検証をまとめて閉じてからマージし、G3へ移る。

- GR84/96の区切り検証run37627622848はCore buildで停止。SkeletalClipRetargetの座標変換呼出し2か所を、既存APIのConvertSkeletalMatrixBasisへ訂正した。関連CPU/CLIは未実行。

- 修復後run37628934551ではCore.libとAssetCook.exeのbuildが通過。追加CPU fixtureのmanifest bytes→独自Stringの構築2か所でtest buildが停止したため、明示StringView経由に訂正した。CPUケース本体は未実行。

- run37631295354はRelease全bundle buildとSampler両構成の旧出力比較が通過し、CPU 68/74成功。6失敗は4原因：固定/可変inventoryで同じkeyの二重対応を拒否し損ねる退行、静的祖先の行列積の未初期化、混在WAV fixtureの非対応8kHz、追加spec fixtureのString::replaceによる埋込NUL。重複key拒否・明示ゼロ初期化・48kHz fixture・substr連結へ修正した。再実行は未確認。
- 検証コマンドを増やさず、既存CPU契約と分離rig実CLIをRelease build直後へ移した。Debug buildを待たず実行失敗が分かる順にする。

- run37636419605はRelease build成功、CPU72/74成功。重複inventory・可変追加・静的frame Import/Runtimeは通過。残るWire試験は非対応matrixを使ったpitch fixtureを同じ回転のTRSへ訂正。混在rigの初回cookはstaged_captureで停止し、下位理由が失われていたためlogical pathとpackage検証の理由を保持する。検証条件は緩めず、原因の確定を続ける。

- run37640229342はRelease buildとCPU73/74成功。Wireも通過。残る混在rigの失敗理由はModels/Dogのunsafe_output_targetと確定。generic相対名をnative親に連結した物理pathの区切りが混在し、cook後の既存file終端照合だけが失敗するため、AddExpectedで物理targetをmake_preferredに揃えた。componentの畳み込みやguardの緩和は行わない。再実行は未確認。

- run37642958058はRelease buildとCPU73/74成功。GR96のManagedMixedRigは全経路を通過し、RetargetBatchへ到達。残る失敗は追加spec fixtureの必須default_variant欠落で、実装の拒否は正しいためfixtureへ補った。実CLIはCPU失敗と独立に結果を得られるよう、native build成功時に実行する条件へ変更した（同じ検査、jobの失敗は保持）。

- run37646734701はRelease CPU74/74・rig/retarget実CLI・Sampler両構成・Debug骨格/retarget/frameケースが成功。RootFrame独立wire照合だけ、旧直接builderの材質期待をglTF cookerへ流用したため不一致だった。実Release/Debug meshは1360byteで完全一致し、差はmetallic/roughnessの-1→1、開いた三角形のauto両面flag0→1とchecksumのみ。glTF既定/既存material policyに基づいてprofile2 fixtureの期待を訂正し、旧profile1 literalは保持した。
- 保存済みの実Release/Debug出力で、修正RootFrame oracleの12試験・3role wireと通常/一般poseの照合が成功。後続SplitRig/ClipBankの保存済みwire/pose/bindingも当該比較のみ実行して成功。本体C++は変更していない。
- 同じ既存CLI/asset-set byte互換確認をDebug buildより前へ移した。検証コマンドは増減させず、G2の受入条件を先に確認する順序にする。後段Game/CLI全体のworkflowはまだ成功していない。
