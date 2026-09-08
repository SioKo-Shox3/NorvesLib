# PROGRESS — NorvesLib

## Done

- R0: `052a7dc`。描画検証基盤。
- R1 P1: `c67dfde`。Camera露出、物理ライト単位、Scene v2。
- R1 P2: `98bd5c9`。表示リニア中間とsRGB一回出力。
- R1 P3: `053e73be13ab49454691105fd91acbe5144140ff`。物理ライトとEV100プリエクスポージャ。
- R1 P4: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013`。放射輝度IBL、DFG、GGX補償と数値検証。focused build 13/13、CPU 7/7、GPU 6/6（exit 125なし）、独立レビュー、接続監査、fresh verifierを2026-09-08にPASS。
- R1 P5: 透明描画を実ライト・GGX・IBL・方向影へ接続。固定10行、direct/IBL/shadow/metallic mutation、P4回帰6条件、CPU6、skip契約、Indoor/Outdoor object-presenceを検証済み。

## In progress

- R1-P5の実装・検証・コミットを完了。次はR1-P6Aへ進む。
- 10:26時点: focused8 build（verify20）exit0、透明vert/frag事前compile（verify21）各exit0、CPU6（verify22）6/6 PASS。RenderGraphCompileTestは184.70秒から72.28秒、CPU6全体74.23秒になった。
- LUTは256×256・4096sample・式・集積順序・RNEを維持し、行ごとの半角ベクトル事前計算へ変更。親のDebug比較で131072値/262144bytesが全一致、11.018539秒→4.150286秒。証拠 `%TEMP%/norveslib-p5-dfg-hoist-20260908/evidence.json`。元アルゴリズムは受入済みP4関数本体にEOL正規化後一致。
- 10:29時点: P4 GPU回帰6/6 exit0（verify23〜28）。capture数はknown-cd6、prefilter4、DFG100、roughness24、furnace60、direct-conductor4。raw log/hash/countの集計 `%TEMP%/norveslib-p5-p4-gpu-regression-20260908.json`。これは後続fixture再修正前の回帰証拠で、P5全受入ではない。
- P5新規numericalはfixture再投影を修正後、背景単独の値を測る前提で停止している。数値用cameraとobject-presence用cameraを区別し、実GPU向け行列規約で固定geometryを確認する。
- P5計画v4の背景side1.840303983m/同じ画面範囲とbackground-only ROI要求は両立しない。数値背景だけを全画面へ広げる修正を、全フェーズ再開依頼の範囲内のfixture不整合修正として採用した。追加確認はメインの判断で取り下げたもので、個別承認eventの受領を意味しない。対象ROI・threshold・exposure・10行・通常画像は保持する。根拠と変更前の検証契約は `Docs/Plans/RenderingR1P5BackgroundProposal20260908.md`。
- 初期binding5とSSBO更新順序を両立し、初期化成功前のpublishを `m_bInitialized && context.PhysicalLighting.bActive` で抑止する修正を含む。正式な独立評価、P5 numerical/object-presence受入、最終hygiene、source commitは未完。
- 親セッションのgoalでP5→P6a→P6b完了を追跡中。起動やプロセス生存を完了に置き換えない。
- 11:24時点: object-presenceの黒画面は透明描画の欠落ではなかった。verify58の実HDRはtarget平均RGB(0.003355026,0.003229141,0.003185272)、background(0.006248474,0.006248474,0.006248474)、NaN/Infなし。ACES後に既定Contrast1.05で全て0へclampされる。証拠 `%TEMP%/norveslib-p5-black-roi-diagnosis-20260908.json`。診断専用branchは受入前に撤去する。
- 既承認の最終照明条件をobject-presenceへ先行適用し、既存main lightの誤った無効化を修正する。P5数値fixtureと既定BuildSceneLayoutは保持。適用範囲と条件はTASKS.mdおよび `Docs/Plans/RenderingR1P5FixtureIntegration20260908.md`。
- 親の実GPU向け行列計算で、OutdoorのToMatrix再投影がfalse-greenだったことを確認。固定物理geometry/ROI/normalを保ったfixture内の表現補正を実装する。数値oracleも中心一色から画素ごとの独立double参照へ、Shadowedには遮蔽率条件を正しく適用する。指摘は `%TEMP%/norveslib-p5-parent-oracle-findings-20260908.md`。いずれもP5の正式受入は未完。
- 11:41時点のobject-presence実測（verify70/71）: Indoor delta_y8=11.874405859、Outdoor=154.644108203で、各delta>=8とF9の演算一致を確認した。既存main lightは各1件。診断コードを含む中間測定であり、通常goldenやP5全体の受入ではない。証拠 `%TEMP%/norveslib-p5-objectpresence-final-lighting-20260908.json`。
- verify90/92の背景平均0.015621185は、ROIの32列中10列だけが対象/背景の共通領域に重なることで説明できる。half(.05)*10/32=0.015621185302734375。従来のgeometryのままROIを移動しても背景単独の領域は作れない。証拠 `%TEMP%/norveslib-p5-partial-roi-proof-20260908.json`。
- 数値ROIの端ではNdotV=0.9951476490952となり、DFG参照にはx254/255とy127/128の4texelが必要。独立double積分・half RNE・二軸bilinearの修正を進めた。修正後HDR target build（verify93）はexit0。全10行のGPU数値受入、独立評価、source commitは未完。
- P6aのRaw251単一行へ25 queryを配置する準備を行った。既存camera/PlaneHandle/materialを使う25領域の100頂点を実GPU向け行列から再投影し、最大誤差5.82563808393e-06pixel、compile/run exit0。具体案と証拠は `Docs/Plans/RenderingR1P6aReadiness20260908.md`。P6a source実装・GPU受入は未着手。
- P5反復1終了後の12:32:43に `af759e0c8c1cd40b06560c9e14976c5fb39ae890`（作業途中の保存）が作られ、未完のsource18本とPROGRESS/blocked記録が保存された。これはP5受入コミットではない。履歴を保持し、以後の修正を積み重ねる。正式なP5評価はP4描画commitから最終P5 treeまでの累積source18差分を対象とし、最後の修正だけを評価して全体を合格扱いにしない。
- 反復2のsessionは `01a07f13-555d-7bf1-80f3-91faa7269947`。focused8 build（verify96）exit0後、CPU6（verify97）は6/6、RenderGraphCompileTest72.24秒、全体74.15秒、exit0。skip契約（verify98）は意図的Skipped、CTest exit0。数値背景修正前の証拠として保持し、以後の変更に関係する検証を続ける。

## Next

- `R1-P6A` → `R1-P6B` の順に実装・検証・コミットする。
- P6aが閉じたら、承認済みP6b計画に従って正式なIndoor/Outdoor候補を生成する。実物とcandidate hashへの人間の承認前にbaselineを公開しない。thresholdも60行の候補とhashへの承認後に公開する。
- R1全体の完了はP6bの最終検証とacceptanceまで保留する。

## Notes

- 2026-09-08開始時: `git log --oneline -10`を確認し、対象CPU7件を実行。`100% tests passed, 0 tests failed out of 7`、exit 0。
- P4保存コミットの実subjectは「作業途中の保存」。最終レビューsnapshotと11ファイルすべてbyte一致し、P3→P4のdirect diffはexact11、8212追加/608削除。保存済み履歴をそのまま使用する。
- P4 report: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-4-report.md`、SHA256=`78AFC2D4FD987F39BD582A7DF6DFD298AFFCCD396CCA8D140B194CE3BC86BC94`。旧計画がsubjectを固定値にしている箇所は、実commit hash・source11の直接比較・本受入証跡に読み替える。reportのSUBJECTも実subjectを記録する。
- 作業一覧と進捗だけのコミットを挟んでも描画基点を取り直さない。記録されたP4/P5の描画コミット、祖先関係、対象source同一性を確認する。技術仕様の数式・固定threshold・sample・source範囲は変更しない。
- P4検証の詳細: `Docs/Plans/SessionRecovery20260905.md`。最新の独立再実行ログは `%TEMP%/norveslib-p4-fresh-verifier-20260907T231637Z-026345e9/`。各JSONのexitとSHA、対応log本文を開いて確認する。
- P4のsource snapshot: `%TEMP%/norveslib-p4-review-r2-20260905T152810Z/manifest.json`。combined SHA=`10fa95181d028d6ef41bcaf310aeb4e671dabe1c55b4439968430a81dd09f69e`。
- P5 scope18およびP6a fixture変更は2026-08-15に承認済み。P6a v4 hash=`B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F`、P6b v3 hash=`37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3`も承認済み。旧親ログのlines 6190、7574、7587、7593が根拠。
- 承認ログ: `C:/Users/KINGkawamura/.codex/sessions/2026/08/13/rollout-2026-08-13T09-18-12-019ff87b-d252-7260-94ae-580b5adfc698.jsonl`。8月16日の停止後、9月5日と9月8日に再開指示を受けている。
- 実装主体・モデル・評価頻度は現行AGENTS.mdを適用する。旧計画のメイン実装禁止や反復ごとの追加承認は再導入しない。数式や画像公開の承認を変更する意味ではない。
- 残課題: `LightingParamsLayoutTest`のEss lexical assertionは演算子まで反証しない。現shader式はレビュー済みで正しいためP4の非blocking事項として保持。品質返済はR1の次の自然な区切りでまとめる。

- 2026-09-08 09:33 JSTの読取照会: CTest登録219/GPU21/意図的skip7のname hashとsource-start6ファイルhashは計画に一致。固定name hashはPowerShell `Sort-Object -CaseSensitive` による。証拠: `%TEMP%/norveslib-r1-final-prerequisite-inventory-20260908.json`。正式P6b実行やfull build/full CTestは未実行。
- 12:31 JST: DFGをx254/255・y127/128の独立double積分、half RNE、二軸bilinearへ修正し、HDR capture build（verify93）とknown-cd数値行（verify94）はexit 0。transparent-physical-lighting（verify95）はDFG preflight/fixture 10行後、固定ROIの背景mean 0.015621185（期待0.1）でexit 1。追加captureまたはgeometry/ROI変更の判断待ちとして `blocked/R1-P5.md` に記録し、P5は未完・未コミット。
- 2026-09-08 反復2: `verify-R1-P5-123.txt` focused8 build、`verify-R1-P5-124.txt` CPU6、`verify-R1-P5-125.txt` skip契約、`verify-R1-P5-126.txt`〜`verify-R1-P5-132.txt` のGPU7条件、`verify-R1-P5-133.txt`/`verify-R1-P5-134.txt` のIndoor/Outdoorを保存し再読した。全実行はexit 0、skip契約は1/1 Skipped。透明10行は direct/metallic、shadow、IBL on/off を含み、固定 preflight は direct M0=0.141046407037373、direct M0.5=0.265191352940684、IBL M0=31.951981492677799、IBL M0.5=29.654360326265852。object-presenceのdelta_y8はIndoor=11.874405859、Outdoor=154.644108203。`verify-R1-P5-135.txt`/`verify-R1-P5-136.txt` で `PhysicalLightingResources` の lifecycle 実値検査も通過した。
