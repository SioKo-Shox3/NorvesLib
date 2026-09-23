# R1 物理ベース土台 受入れ記録
## R1 visual approval
R1 baseline candidate Indoor=3845F8671194A3C55EA929503E7ECB4C777EB7A5A4D900C03E75853E50D9D4EF Outdoor=05D202193F8C304E45099EBA6557A48BB7B5BF88E694552CBABC50E898D895B6 CodeHead=2d4243395d62ace7ec0faa063fc9a78507b3bc00 を承認する。
R1 visual threshold candidate SHA256=19BBE1737E75796B7709DEE2DCABB66CB60DED050940F9B4E54A13BD242C7309 CodeHead=63f70dae644de0dc32a333670aed5720dde488c0 を承認する。
R1 baseline candidate Indoor=3845F8671194A3C55EA929503E7ECB4C777EB7A5A4D900C03E75853E50D9D4EF Outdoor=9933B55851574870C9AC8586B6C0F38FDFFB5E26B925547C37E92ECFBAF3B954 CodeHead=a41190e716ff98b3d4f52344996ff50e258086ff を承認する。

## P6b plan and procedure approval
P6b plan SHA256=37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3
R1 P6b条件付き実装計画 task-6b-conditional-implementation-plan-v3.md（SHA256=37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3）と、RunRenderingValidation.ps1を変更せずRenderingGoldenImageTest.exeを--capture-source=back-buffer付きでIndoor/Outdoor各10回、indoor1→outdoor1からindoor10→outdoor10の順に直接実行するGate D procedure substitutionを承認します。

## Phase provenance
- R1 base: `052a7dcbcedfd21764b7b9c0c5151d5108b51343`
- P1: `c67dfde1f95fe3c2a4d8924533a181b01ec03b7a` — Camera露出と物理ライト単位をScene v2へ接続する
- P2: `98bd5c99932bb72bcaaf3e3460be0960c4d268a3` — 表示リニア中間とswapchain別sRGB一回出力を確立する
- P3: `053e73be13ab49454691105fd91acbe5144140ff` — 物理ライトとEV100プリエクスポージャをSceneColorへ結線する
- P4 actual Git: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013` — 作業途中の保存。P4 plan SHA256=`D2C83555B4E01CA982CEAD4D3AADBE73D26DAFA467A7FDB9960596EE2659A9AA`。
- P5: `7d134b7d5fef9486210589d698b01d66bf0c7d31` — 透明描画を実ライト・方向影・IBLのPBRへ接続する。P5 plan SHA256=`CA5D727F18AB922B0648A13421741643283C0523A1E2AE6DB36746DFF1C5EE9C`。
- P6a report: `P6A_CODE_HEAD=93f01220c2bcd899c51e3a9c538265e5723395dc`、report SHA256=`751568D5B5052E0BB2D38E781CBDCBF010FCDEDAF00C407D33279A33337B034C`、plan SHA256=`B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F`。
R1 P6a条件付き実装計画 task-6a-conditional-implementation-plan-v4.md（SHA256=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F）を最終版として承認します。
- P6a plan reviewはround2判定がBLOCKING、blocking fixをv4へ適用済み、post-fix round3は未実施。`P6A_SOL_REVIEW=NOT_RUN_CURRENT_AGENTS`、`P6A_CLAUDE_REVIEW=PASS`、seam・R1 integrated・fresh verifierは各PASS、exact20は20/20 PASS。
- P6a numerical evidence: 40 static、68 numerical、70 total、120 captures、frame latency 2、final `S+240`、`S<=359`、`final<600`。
- CodeHeadと公開資産hashは別管理する。baseline approvalのCodeHeadは`2d4243395d62ace7ec0faa063fc9a78507b3bc00`、現行threshold producerのCodeHeadは`63f70dae644de0dc32a333670aed5720dde488c0`、公開PNGは下記の承認hashである。

## Baseline publication
- P6b source-start: Indoor=`545E745CE9958F310A551B0D71BEAB4DAD35743C6367E0DA0724F9930E6F49E7`、Outdoor=`3676A470814C68841BF1C8E4BF8612802042AB936AAA77E1A9937C5EC642BA7E`。
- candidate/publication: Indoor=`3845F8671194A3C55EA929503E7ECB4C777EB7A5A4D900C03E75853E50D9D4EF`、Outdoor=`05D202193F8C304E45099EBA6557A48BB7B5BF88E694552CBABC50E898D895B6`。
- baseline manifest SHA256=`043C50E78E27CE8385B99CB50F1D8B40E03EFF15AA2356F866E23EEE7B49ACA9`。現行forward-fix reportは`.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6b-baseline-candidate-report-forward-fix.md`、SHA256=`749E410C9F1F3983BD82119B71E95C95BCF0D18E9D310D6F3C060F2EDBD276C5`。
- candidate paths: `build/RenderingValidation/R1/BaselineCandidate/Indoor.png`、`build/RenderingValidation/R1/BaselineCandidate/Outdoor.png`。capture sourceはBackBuffer、両方とも256x256・8-bit RGBA color type 6。
- 意図したA/B変化は、P2のgamma 1回、P3のphysical unit/pre-exposure、P4のradiance IBL/two-lobe GGX、P5のtransparent PBR、P6aのIndoor camera exposure compensation +4.0 EV／Outdoor directional main light 10000 luxである。
- transparent objectの実測はIndoor target_mean_y8=`96.125594141`、background=`108.000000000`、`abs(delta_y8)=11.874405859>=8`、Outdoor target_mean_y8=`172.101869922`、background=`17.457761719`、`abs(delta_y8)=154.644108203>=8`。永続証跡は`.harness/runs/20260908-084154/p6a-acceptance-final.json`。
- Indoor/Outdoorは同一transactionでatomic dual publishされ、片側だけのpublishはない。

## Visual threshold publication
- threshold candidate SHA256=`19BBE1737E75796B7709DEE2DCABB66CB60DED050940F9B4E54A13BD242C7309`、measurement SHA256=`43D6421FAC7B4A20FC22CF3C459E31F9701947921DA46716104F13451CFA16CE`、manifest SHA256=`42B3325559D8A85AE68632F44F364C4564D896810D4C7288E577BD18FF3C5046`。
- 現行forward-fix reportは`.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6b-threshold-candidate-report-forward-fix.md`、SHA256=`E3CCB03EAC7589EC74549EBD3D0138148E74E663AC5D7429B7CB829BEC0386CA`。公開`VisualThresholds.tsv` SHA256=`19BBE1737E75796B7709DEE2DCABB66CB60DED050940F9B4E54A13BD242C7309`。
- rowsはtotal=60、noise=20、artificial=40、Indoor/Outdoor各10 iterations、GPU skip=0。artificial orderは`1/1, 1/2, 1/4, 2/1, 1/8, 2/2, 2/4, 4/1, 2/8, 4/2, 4/4, 8/1, 4/8, 8/2, 8/4, 16/1, 8/8, 16/2, 16/4, 16/8`で固定した。
- scene noise maximumはIndoor=`0.000000000`、Outdoor=`0.000000000`。最初のstrict-separated artificial rowは両sceneともpatch_size=1、channel_delta=1で、Indoor mean_flip=`0.000001200`、Outdoor mean_flip=`0.000001702`。
- F6 thresholdは`0.000001`、patch=`1`、channel_delta=`1`。strict separationは両sceneで`0 < 0.000001 < selected artificial mean`。PPD=`67.0`、FLIP commit pin=`b475eb4bf394ab877c42166c9eb0a84a02cc5b14`、raw maximum channel delta=`8`。

## Numerical and compatibility evidence
- P6a/P6b evidenceは40 static rows、68 render numerical rows、70 total numerical rows、120 captures、10,485,760 actual BackBuffer byte channels、20,971,520 SceneColor/PresentationColor float channel visits、56 forced branch channels。
- SceneColor first offender=`0`、OETF-before display-linear first offender=`0`、全値`abs(value)<65504`。v1→v2 migration evidenceはSceneSerializerのphysical camera/light v1 migration testsで確認し、persisted emissive carrier=`0`。
- tracked v1 scene SHA256=`43B5FDD7E79E559345579CFA0BDE3AB4590EF7D1066BAF4EDDA2844EC2372099`。R0Acceptance SHA256=`10CCD7FFCF4277895F42CA6D23E33D9B4A1A60E590A88CAC1F580B119F9DD75A`、PerceptualDiffSelection SHA256=`0484889C4A74671BEA2F919721484F668905E688DC49EF87A3E3A73D5FF501C0`は不変。
- GPU CTest registrationは21/21 PASS、registration_delta=0。dead rendering reference=`0`。P6a immutable source hashes、script self-tests、skip contractはPASS。

## BackBuffer final validation
- `build/RenderingValidation/R1/P6bFinalFix/BackBuffer20/BackBuffer20.summary.txt`で20/20 PASS、Indoor=10/10、Outdoor=10/10、skip=0、order=interleaved、capture=back-buffer。
- Indoor/Outdoorのmax_mean_flip、max_flip、max_rawはいずれも`0`。20 invocation logsは各exit=0、`NORVESLIB_VISUAL_METRICS` sentinel exact1、`NORVESLIB_VISUAL_MEASUREMENT` 0件。
- exe SHA256はbefore=`AD3EE278D36F346FD4685A76E92EC9A2017C85522B99F36D91087B3B830EBA71`、after=`AD3EE278D36F346FD4685A76E92EC9A2017C85522B99F36D91087B3B830EBA71`。

## Full build and CTest
- targetless full buildは`build/RenderingValidation/R1/P6bForwardFix/FullBuild2/FullBuild.exit`で1回実行、exit=0。
- full CTestは`build/RenderingValidation/R1/P6bForwardFix/FullCTest2/FullCTest.stdout`で、registered=219、passed=212、intentional skipped=7、failed=0、exit=0。skipはGPU-only contractの7件である。
- CTest registration names SHA256=`0B527571484BDE7E6512F3B82C7A04BE16284F93B95220D5193655DF3089BC45`、GPU=21/21、registration_delta=0。normal GPU scenariosは14/14 PASS、skip=0。RenderingValidation fixture contractは2/2 PASS。

## Independent review
独立評価は受入れ証跡・実行結果・形式を確認し、第2周で検出されたLESSONS.mdの全体行末差分を8ca5087で追加行だけに修正した。計画上の第3周は実施しない。

## Performance decision
GPU performance=Deferred; executions=0; destination=future CI GPU runner

## R2 CSM後のOutdoor再承認
- 2026-09-23、ユーザーはR2の4カスケードCSM導入後のOutdoor出力（SHA256=`9933B55851574870C9AC8586B6C0F38FDFFB5E26B925547C37E92ECFBAF3B954`）を新しいOutdoor baselineとして承認した。上の3行目の承認行は置換前の履歴として残す。
- 差分の範囲: 旧baselineとの比較は平均FLIP `0.002326954`、最大FLIP `0.529430032`、459画素で、差は球の自己影の境界と接地影の輪郭に限られる。地面・空・非影領域は一致する。R2-P5で単一directional shadow matrixを4層CSMへ置き換えた意図した変化であり、CSMの境界欠落・二重化・シミー抑制はR2受入れ（`R2Acceptance.md`）で別に検証済みである。
- 再生成: `UpdateRenderingGoldenBaselines.ps1 -GenerateCandidate -CodeHead a41190e716ff98b3d4f52344996ff50e258086ff`。Indoor候補は既存baseline `3845F867…D9D4EF`と一致し、Outdoor候補は独立した2回のstaging captureと同じ`9933B558…F3B954`である。
- 閾値（`VisualThresholds.tsv`のmean FLIP上限`0.000001`、raw channel差上限8）は変更しない。
