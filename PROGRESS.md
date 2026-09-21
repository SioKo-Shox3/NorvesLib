# PROGRESS — NorvesLib

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
- R5-P6: `67780ea` / `29c483d` / `7d8d164`。Vulkan同期TLAS BuildはGPU完了後だけ実instance数を更新し、command-list Build 1件から同期Build 2件への変更、旧件数Update拒否、新件数UpdateとGPU queryを検証した。Debugビルドと専用CTestはexit 0、CTest 1/1 passed、独立評価PASS。証拠は`.harness/runs/20260920-174410/verify-R5-P6-5.txt`、`verify-R5-P6-6.txt`、`evaluator-R5-P6-2.txt`、`evaluator-R5-P6-3.txt`。
- R5-P7: RayTracing pipeline descriptorにraygen/miss/closest-hit shader groupを加え、Vulkan pipelineとSBT生成を実装した。6 RT stageとAllRayTracingのdescriptor visibility、無効group拒否、group handleとSBT region alignmentをGPUテストで確認した。Debug build exit 0、専用CTest 1/1 passed、独立評価PASS。ログは`.harness/runs/20260920-174410/verify-R5-P7-5.txt`と`verify-R5-P7-6.txt`。
- R5-P8: `2add53a` / `f9fa15a` / `1267197`。Vulkanの各dispatch軸上限と総呼出し数上限を検査し、同一deviceのTLAS binding、null/BLAS/foreign-device/別descriptor種の拒否、hit=1・miss=0のGPU readbackを固定した。両readback要素を番兵値から初期化して欠落dispatchも検出する。Debug build exit 0、専用CTest 1/1 passed、軸別拒否2件、上限・番兵値の修正差分は独立評価PASS。ログは`.harness/runs/20260921-r5-p8-resume/verify-R5-P8-resume-build-3.txt`、`verify-R5-P8-resume-ctest-3.txt`。

- R5-P9: `9e213c9` / `27d7505` / `c8db734`。GEngine所有のRayTracingSceneSubsystemがopaque DrawCommandからgeometryとinstance transformをFramePacketへコピーし、RenderThreadではFramePacketからBLAS/TLASを構築する。同一slotのTLAS update再利用、GPU待機後・device参照解放前の資源破棄、FramePacket寿命を契約テストで確認した。Debug build exit 0、専用GPU CTest 2/2 passed（Validation Layer無効）。証拠は`.harness/runs/20260921-r5-p9-resume/verify-R5-P9-final-build.txt`と`verify-R5-P9-final-ctest.txt`。
- R5-P10: `VulkanCommandList`のstage解決をRT pipeline capabilityで制御し、対応デバイスの`ShaderResource` barrierに`eRayTracingShaderKHR`を追加した。RT保存状態も全buffer/image barrier経路で専用RT stageを使う。対象build exit 0、同期validation有効のRT CTest 1/1と直接capture（Raster/RT A/B・RT無効fallback、max_lsb=0）を確認。再リンク後のRenderingValidationは20 passed/7 skipped/1 known baseline failure（Outdoor golden、`mean_flip=0.002326954`で過去基準と一致）。ログは`.harness/runs/20260921-073235/verify-R5-P10-17.txt`〜`-23.txt`。

- R5-P11: `LightingParamsLayoutTest`でプリエクスポージャを適用する7 mode（Normal、RAW252、Validation Lambert/PBR、R5 RasterHardShadow/RayTracingHardShadow/RasterFallback）を個別に固定し、RT visibility・RAW250/251は除外した。Debug build exit 0、対象CTest 2/2 passed、同期validation付き動的capture全段PASS（TLAS更新後の旧領域255・移動先0、Raster/RT/fallbackの`max_lsb=0`）。全CTestは235件中6失敗・7 skipで、開始baseline234件/8失敗の失敗集合に対して新規失敗0。ログは`.harness/runs/20260921-073235/verify-R5-P11-contract-fix-target-ctest.txt`、`verify-R5-P11-contract-fix-dynamic-sync.txt`、`verify-R5-P11-contract-fix-full-ctest.txt`。

- R5-P12: `d8cde71`。R5Acceptance.mdへ方式選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能ゲート保留を集約し、R4/DDGIを後続として記録した。

## In progress

## Next

- R5-P13: VulkanTexture::Updateの同期失敗時staging資源を解放する。

## Notes

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
- R5-P5検証: build exit 0（VulkanDevice.hの既存マクロ再定義warning 2件）、専用CTest 1/1 passed、実行ログでray_query_hit=1 / ray_query_miss=0。証拠は`.harness/runs/20260920-174410/verify-R5-P5-1.txt`〜`verify-R5-P5-3.txt`。独立評価と助言窓口はClaudeのセッション上限で起動できず、blocked/R5-P5.mdに再開条件を記録した。
- R5-P6の独立評価でVulkanTexture::Updateの同期失敗時にstaging buffer/memoryの解放漏れが見つかった。独立フォローアップR5-P13へ登録した。
- `VK_LAYER_VALIDATE_SYNC=1`をRenderingValidation全体へ設定した診断では、`RHIImageLayoutVulkanNoCasterSceneTest`と`RHIImageLayoutVulkanDrawThenNoCasterSceneTest`がswapchain画像のWRITE_AFTER_READを報告した。通常validationでは両テストが成功し、RT影専用テストも同期validation下で成功する。このswapchain経路は独立した追跡事項としてNEXT_FINDINGS.mdへ記録する。
- R5-P10の影captureは`shadow_luma=0`/`lit_luma=255`の高コントラスト画像であるため、現在のA/Bは影領域の位置と出力一致を検証し、半影や階調誤差は検出しない。
- R5-P12開始時の全CTest(2026-09-21): 235件中222 passed、7 skipped、6 failed/Not Run、exit 1。保存ログは`.harness/runs/20260921-125655/startup-ctest-LastTest.log`と`startup-ctest-LastTestsFailed.log`。R5のBDA、機能検出、RHI契約、RT影、動的TLAS/fallbackテストは成功。失敗はOutdoor golden差分、RenderGraphCompileTestのbinding 6 assertion、SkinnedRenderPathContractTestのpending件数assert、ScriptRuntimeSafetyTestのBAD_COMMAND、Bridgeの2 test executable不在。
