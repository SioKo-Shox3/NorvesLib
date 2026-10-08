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

## main 側の描画改善履歴

- VTG1-VRAM-BUDGET: `RHI::VideoMemoryBudget`（DeviceLocalヒープのbudget・usage合計とbValid）を`IDevice::GetVideoMemoryBudget()`（既定は無効値）で返し、Vulkanは`VK_EXT_memory_budget`を任意拡張として有効化して`vkGetPhysicalDeviceMemoryProperties2`の値を返す。`--vram-budget-mb=<MB>`をGameが読み`RenderResources::SetVideoMemoryCapMb`へ渡し、`RenderWorld::BeginFrame`から`PollVideoMemoryBudget`が初回と約1秒ごと（1%以上の変化時）に`VRAM_BUDGET`をログへ出す。Debug build exit 0、`VideoMemoryBudgetVulkanTest`（RHITextureUpdateVulkanTestの束のMEMBER）はctest 1/1 passed、直接実行でbudget=16022241280・usage=48041984を確認。証拠は`.harness/runs/20261004-105824/verify-VTG1-VRAM-BUDGET-1.txt`〜`-3.txt`。Game起動での`VRAM_BUDGET`ログの目視は重い処理のため未実施（段の終わりの起動撮影で確認する）。
- VTG1-VRAM-BUDGET 差し戻し対応: (1) `GetVideoMemoryBudget` は拡張が有効なら `bValid=true` を返し、予算0は呼び出し側（`VideoMemoryBudgetVulkanTest`）が失敗にする（拡張なしだけがスキップ）。(2) ログの間引き（初回・1秒間隔・1%変化）を `Rendering/VideoMemoryBudgetLogGate.h` に切り出し、`VideoMemoryBudgetLogGateTest`（`RHITextureUpdateVulkanTest` の束の MEMBER、GPU不要）で境界を確認。(3) Game を `--vram-budget-mb=4096` あり・なしで起動し、`VRAM_BUDGET ... cap_mb=4096|none source=ext` が初回に出て、以後1秒以上の間隔で出ることをログで確認した。証拠は `.harness/runs/20261004-105824/verify-VTG1-VRAM-BUDGET-4.txt`（build EXIT_CODE=0）・`-5.txt`（ゲートのctest 1/1）・`-6.txt`（GPUのctest 1/1、skipではなくpassed）・`-7.txt`（直接実行 budget=16022241280 usage=48041984）・`-game-cap.Game.log`・`-game-nocap.Game.log`。
- VTG1-VRAM-LEDGER: テクスチャの確保量の見積り（`RHI::GetFormatBytesPerPixel`・`RHI::EstimateTextureSize`、全ミップ・配列数込み）を `IGPUResourceAllocator.h` の inline 関数に一本化し、Vulkan アロケータと `TransientResourcePool` の重複実装を除いた。`GpuResourceStore` は作成・`RegisterUploadedTexture` でRHIテクスチャの実寸（形式・ミップ・配列）から `TextureResourceData::Bytes` を持ち、`ResourceStats::TextureBytes`（`TotalTextureMemory` にも同値）へ合計する。外部登録（スワップチェーン等）は所有しないので数えない。Game は `OnPostRender` から、読み込み中を一度見た後の最初の落ち着いた描画（見ないままなら600描画目）で `VRAM_LEDGER textures=<n> texture_mb=<n.n> buffers_mb=<n.n>` を1回出す。`TextureMemoryLedgerTest`（`RenderResourcesDomainContractTest` の束の MEMBER、GPU不要）が RGBA8 4096² 全ミップ=89,478,484 B・1×1=4 B・作成→解放で合計が戻る・外部登録は0を確かめる。証拠は `.harness/runs/20261004-105824/verify-VTG1-VRAM-LEDGER-1.txt`（Debug build EXIT_CODE=0）・`-2.txt`（ctest 1/1 passed）・`-2b.txt`（直接実行passed、同じ束の2テストもpassed）・`-3.txt`（RelWithDebInfo Game build EXIT_CODE=0）・`-4.txt`（起動画面の撮影 pass、mean_luminance=116.531、天球・地面・球・岩・家が見える）。起動画面の基準値（地面の見本を含む今の状態）: `VRAM_LEDGER textures=50 texture_mb=3609.6 buffers_mb=0.0`（同じ起動の `VRAM_BUDGET` はこの時点で heap_usage_mb=4265）。
- VTG1-VRAM-LEDGER の Notes: 起動画面では `GpuResourceStore` 経由のバッファ合計が0.0 MB だった（メッシュ等の確保経路との対応は未調査。次のタスクで BC の形式を足すときに、台帳の対象範囲を見直す材料にする）。`GpuResourceTypes.h`・`GpuResourceStore.cpp` は BOM が無く、日本語コメントを足すと cp932 で崩れる（C4819）ため BOM を付けた。
- VTG1-VRAM-LEDGER 差し戻し対応: 公開した `RHI::GetFormatBytesPerPixel`・`RHI::EstimateTextureSize` が `TransientResourcePoolTest.cpp` の同名補助関数とADLで衝突し（C2668）、指定verifyに含まれない既存テストがビルドできなくなっていた。テスト側を `GetExpectedFormatBytesPerPixel`・`EstimateExpectedTextureSize` に改名し独立した期待値計算は残した。`TextureMemoryLedgerTest.cpp` の失敗・成功の表示文言も日本語にした。証拠は `.harness/runs/20261004-105824/verify-VTG1-VRAM-LEDGER-5.txt`（Game・RenderResourcesDomainContractTest・TransientResourcePoolTest の Debug build EXIT_CODE=0）・`-6.txt`（TransientResourcePoolTest と TextureMemoryLedgerTest の ctest 2/2 passed）。テストだけの変更なので RelWithDebInfo の Game と起動撮影は再実行していない（前回の `-3.txt`・`-4.txt` が有効）。
- VTG1-RETIRE-QUEUE: `GpuRetireQueue`（`Rendering/GpuRetireQueue.h`、ヘッダのみ。`RenderResources::Impl` が持つ）を足し、`GpuResourceStore::ReleaseTexture`・`ReleaseBuffer` が外した RHI 資源をここへ渡すようにした。期限は「解放を頼んだ時点で最後に提出した serial」で、`SkinnedMeshGpuStore` と同じく 0 は完了済み扱い。レンダースレッドがフレームを記録中に頼まれた分は、記録済みのコマンドが参照している恐れがあるので、そのフレームの serial（`CommitFrame` で確定）まで延ばす（中止なら直前の提出まで）。状態は全部ミューテックスで守るので、GameThread から解放を頼んでも安全で、`FramePacket` の契約は変えていない。`RenderingCoordinator` がフレームの開始（`BeginRetireFrame(完了済み serial)`）・提出（`CommitRetireFrame`）・中止（`AbortRetireFrame`）を `SkinnedMeshes()` の呼び出しの隣で通知する。`RenderResources::Shutdown` は WaitIdle の後に `Clear()` で期限を問わず全部破棄する。`MegaMeshMaterial` のテクスチャは `TextureHandle` にし、`MegaGeometryPassCommand::Textures` 経由で `MegaGeometryPass` が描画のたびに引き直す（解放済みはデフォルトのテクスチャに落ちる）。`GpuRetireQueueTest`（`RenderResourcesDomainContractTest` の束の MEMBER、GPU不要）が、未提出なら即破棄・完了の serial が届くまで保持・届いたら破棄・記録中の解放は確定した serial まで・中止は直前の提出まで・Clear/デストラクタ/`RenderResources::Shutdown` で全部破棄・無効/二重の解放は積まない、を確かめる。証拠は `.harness/runs/20261004-105824/verify-VTG1-RETIRE-QUEUE-3.txt`（Debug build EXIT_CODE=0）・`-4.txt`（指定ctest 3/3 passed）・`-5.txt`/`-6.txt`（関連CPUテストのbuildとctest。後述の CanvasViewRenderTest 以外 passed）・`-9.txt`（RelWithDebInfo Game build EXIT_CODE=0）・`-10.txt`（起動画面の撮影 pass、天球・地面・球・岩・家が見える）。`-1.txt`・`-2.txt` は型変更の途中の失敗と、テストの期待の誤り（完了 serial を提出済みより先へ進めていた）の記録で、修正後が `-3.txt`・`-4.txt`。
- VTG1-RETIRE-QUEUE の Notes: (1) `paths:` の外のファイルを、型変更に伴う呼び出し側の修正として最小限だけ触った: `Library/Core/Private/Resource/ModelStaging.cpp`（モデル材質のテクスチャを `TexturePtr` でなくハンドルで受ける。ストア側のエントリの寿命は従来どおり）と `Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp`（石畳の球の材質へハンドルをそのまま渡す）。`MegaMeshMaterial` を変える以上、この2つのコンパイルが通らないと完了条件を満たせないため。(2) 起動画面の見た目: 直前の撮影（VTG1-VRAM-LEDGER、default の mean_luminance=116.531）に対し今回は 115.551、同じビルドの再撮影は 115.296。8×6 の格子で符号付き差を見ると、前回比は全セルで約 -3.7〜-5（空にも同じだけ出る）、再撮影比は全セルで約 -0.25 の一様な差で、材質・形に局所的な差は無い。天球・地面・球・岩・家は撮影で目視確認した（自動露出の収束タイミングによる全体の明るさの揺れと判断。`-10.txt`、画像は `.harness/runs/startup-capture/VTG1-RETIRE-QUEUE/default.png`）。(3) `CanvasViewRenderTest` は ctest の既定の作業ディレクトリ（`build/Test/Core/Rendering`）だと `CreateGraphicsPipelineCount == 3` の assert（解放より前の1061行）で止まりタイムアウトする（`-7.txt`）。リポジトリのルートから直接実行すると 7/7 passed（`-8.txt`）で、作業ディレクトリ依存のテスト。解放の経路（`ReleaseTexture` を呼ぶ後半のテスト）はルート実行で通っている。基準の HEAD では確かめていない。(4) `ReleaseTexture` は外部登録（スワップチェーン等）のテクスチャも同じキューへ渡す（強参照を遅れて落とすだけ）。`GpuResourceStore::Clear()`・`MegaGeometryResourceStore` の解放は今回の対象外（従来どおり即破棄）で、後の段の sparse のページ・ジオメトリのプールが `GpuRetireQueue::Retire` を使う。
- VTG1-RETIRE-QUEUE 差し戻し対応(再初期化): `GpuRetireQueue::Clear()` がエントリだけを消し、提出・完了の serial と記録中の印を残していたため、`Shutdown()` → `Initialize()` の後の新しい提出系列（serial が 1 から）で古い serial（例 5）が完了済みと判定され、GPU の使用中に資源を破棄しうる状態だった。`Clear()` で `m_LastSubmittedSerial`・`m_CompletedSerial`・`m_bFrameOpen` も初期状態へ戻すようにした（呼び出しは `Shutdown` の WaitIdle の後とデストラクタだけ）。`GpuRetireQueueTest` に再初期化のケース（`TestClearResetsSerialsForReinitialization`: 提出5・記録中のまま Clear → 新しい系列の serial 1 が届くまで破棄されない／記録中の印が残らない）を足した。`GpuResourceStore.h` は BOM が無いまま日本語コメントを足していて RelWithDebInfo で C4819 が出ていたので BOM を付けた（1行の差分、行末は元のまま）。`GpuRetireQueueTest.cpp` 末尾の英語コメントを日本語にした。証拠は `.harness/runs/20261004-105824/verify-VTG1-RETIRE-QUEUE-6-1.txt`（Debug build EXIT_CODE=0）・`-6-2.txt`（指定ctest 3/3 passed）・`-6-3.txt`（RelWithDebInfo Game build EXIT_CODE=0、C4819 なし）・`-6-4.txt`（起動画面の撮影 pass、default mean_luminance=115.046、天球・地面・球・岩・家が見える。ログの ERROR 2件は Slang SDK 無しの neural_material_decode.slang で今回の変更と無関係）。
- VTG1-BC-UPLOAD: `TextureCreateInfo` に BC1/BC1_SRGB/BC4/BC5/BC7/BC7_SRGB/R16_UNORM と `bInitialDataHasAllMips` を足し、`GpuResourceStore` が全ミップを詰めた初期データ（ミップ0から順、各ミップはブロック単位で切り上げ）をミップごとの `ITexture::Update(mip)` で上げるようにした（`GenerateMipmaps` は呼ばない。BC は常にこの経路、R16 などはフラグで選ぶ）。`VulkanTexture::Update` は既にミップ指定・`slicePitch` ぶんのコピーに対応しており、更新の契約の作り直しは不要だった。BC に対応しないデバイスでは `GpuResourceStore::CreateTexture` が `bTextureCompressionBC` を見て理由をログに出し無効ハンドルを返し、`VulkanDevice::CreateTexture` も同じ条件で nullptr を返す（CPU 展開の逃げ道なし）。BC はレンダーターゲット・深度にできない。全ミップに足りない初期データはミップを1つも上げずに失敗し、`CreateTexture(info, data, size)` はテクスチャを解放して無効ハンドルを返す（`TextureUploadResult::bUploadSucceeded` を追加）。`ImpostorBake.cpp` の形式 switch に新形式を足した。GPU テスト `RHIBlockCompressedTextureVulkanTest`（`RHITextureUpdateVulkanTest` の束）は、手で組んだ単色ブロックの BC1・BC4・BC5・BC7 と、全ミップを渡した R16 の 8×8・2段のテクスチャを `GpuResourceStore` 経由で作り、新設の `Assets/Shaders/texture_mip_probe.comp`（texelFetch でミップ0の4ブロックとミップ1の1ブロックを読む）で読み戻して期待（許容 2/255）と比べる。足りない初期データでの作成失敗も確かめる。読み戻しの最大差は BC1 の 565 量子化の 0.0019 だけで、他は 0、validation error 0件。証拠は `.harness/runs/20261004-105824/verify-VTG1-BC-UPLOAD-10.txt`（Debug build EXIT_CODE=0）・`-11.txt`（指定ctest 3/3 passed）・`-9.txt`（テストの直接実行。各プローブの読み戻しと期待値、VUID_COUNT=0）。BC 非対応デバイスでの失敗は開発機が対応しているため実機では走らせておらず、コードの条件分岐のみ（テスト未実施）。`VulkanTexture::Update` が呼び出しごとに待つ件は段2で扱う。
- VTG1-NVTEX-V01: `CookedTextureFormatV0` に `VersionMinorBlockCompressed = 1` と PixelFormat の BC1(4)・BC4(5)・BC5(6)・BC7(7)・R16UNorm(8) を足した（`VersionMinor = 0` はクッカーが書く v0.0 のまま）。`ParseCookedTexture` は v0.0 と v0.1 のどちらも読み、BC・R16 は v0.1 のヘッダでだけ受け付ける（v0.0 で来たら `UnknownPixelFormat`）。sRGB は RGBA8・BC1・BC7 だけ可。ミップのバイト数は `ComputeCookedTextureMipLayout`（ヘッダ内の constexpr。ブロック幅 4 で切り上げ、最小 1 ブロック）で数え、全段必須・ペイロード詰め込みの検査は従来どおり。`MapCookedTextureFormat` が BC/R16 を RHI・`TextureCreateInfo` の形式へ写し、`CookedTextureUpload` のピッチ検証とミップごとのアップロードをブロック単位にした（BC のレイヤー配列もレイヤーごとに `Update`）。glTF の ARM 分割は元から RGBA8 Linear 以外を `unsupported pixel format` で拒否しており、BC も同じ経路で失敗する（テストで確認）。テスト: `CookedTextureTest` に v0.1 の BC1/BC4/BC5/BC7・R16 のヘッダとミップ数・サイズ、v0.0 での BC 拒否、sRGB を持てない形式の拒否、ブロック不足・過剰・最小 1 ブロック・画素単位サイズの拒否、フルチェーン省略・切れたペイロードの拒否を足した。`CookedTextureUploadTest`（FakeDevice）に BC7 sRGB 8x8 の全ミップ（行/スライスピッチ 32/64・16/16×3、計 112B）・BC1 の 4 の倍数でない大きさ・BC4/BC5/R16・BC7 配列・色空間違いの拒否・BC 非対応デバイスの作成失敗を足した。`TextureResourcesTextureAssetTest` にパッケージ経由で v0.1 の BC7 を `LoadTexture` する経路を、`TextureResourcesPreparedTextureAssetTest` に BC7 の ARM 分割拒否を足した。文書: NVTEX の形式だけの文書は無かったので `Docs/Architecture/AssetCookWorkflow.md` に v0.1 の節を足した。証拠は `.harness/runs/20261004-105824/verify-VTG1-NVTEX-V01-1.txt`（Debug build。指定の3ターゲット＋`TextureResourcesPreparedTextureAssetTest`で EXIT_CODE=0）・`-2.txt`（ctest 4/4 passed。`CookedTextureTest`・`CookedTextureUploadTest`・`TextureResourcesTextureAssetTest`・`TextureResourcesPreparedTextureAssetTest`）・`-3.txt`（`AssetCook` のビルド EXIT_CODE=0）。Notes: (1) 日本語コメントを入れたヘッダ・ソース・テスト 7 本に BOM を付けた（BOM が無いと MSVC が cp932 として読み、`CookedTextureFormat.h` で行を壊してコンパイルが通らなかった。他の日本語入りファイルは BOM 付き）。行末は LF のまま。(2) `GetCookedTextureBytesPerPixel` は BC で 0 を返す（非圧縮専用）。`TextureAssetLoader` の読み込みプロファイルログの `channels` が BC では 0 になるが、ログだけで動作に影響しない。(3) GPU での BC の実読み戻しは前タスクの `RHIBlockCompressedTextureVulkanTest` で済んでおり、本タスクは FakeDevice での経路確認まで（GPU は回していない）。(4) クッカーは v0.0 のまま。v0.1 を書く BC のクックは別タスク。
- VTG1-BC7ENC-VENDOR: bc7enc_rdo(commit b9438627、MIT または パブリックドメイン)の `bc7enc`・`bc7decomp`・`rgbcx` と `LICENSE`・`UPSTREAM.json`(commit・取得元・sha256・SPDX)を `Library/ThirdParty/bc7enc_rdo/` に置き、独立の静的ライブラリ `NorvesThirdParty_Bc7Enc` にした。`Tools/AssetCook/BlockCompressor`(RGBA8→BC1・BC4・BC5・BC7、帯ごとのスレッド並列、検証用の復号つき)を足し、`AssetCook` だけがリンクする(Core・Game は未リンク)。
- VTG1-BC7ENC-VENDOR 検証: `.harness/runs/20261004-105824/verify-VTG1-BC7ENC-VENDOR-1.txt`(AssetCook の Debug build、EXIT_CODE=0)、`-2.txt`(AssetCookBlockCompressSmoke・AssetCookTextureSmoke が 2/2 passed)、`-3.txt`(スモーク直接実行。64×64 の PSNR は BC7 47.69・BC7 アルファ付き 46.99・BC5 50.50・BC4 48.36・BC1 42.86 dB。スレッド数 1 と 5 でバイト列が一致、30×18 の往復、不正入力の拒否も確認)。
- VTG1-BC7ENC-VENDOR の Notes: (1) スモークの exe は `AssetCook` の verify（`--target AssetCook`）だけで ctest が回るよう、`add_dependencies(AssetCook AssetCookBlockCompressSmoke)` で一緒にビルドする。(2) 取り込みは `Library/CMakeLists.txt`（`paths:` の外）を触らず、`Tools/AssetCook/CMakeLists.txt` から `add_subdirectory` した。(3) bc7enc_rdo は版タグが無いので `UPSTREAM.json` の version は commit 固定で記録した。`bc7e.ispc`(Apache-2.0)と lodepng は取り込んでいない。(4) BC7 の品質の写像(Fast: uber0/分割16、Normal: uber1/分割32、Best: uber4/分割64)は暫定。4096² のクック時間は VTG1-COOKER-USAGE で測って決める。(5) MSVC で第三者ソースに `/w` を付けたため D9025(`/W3` より `/w` が優先)の1行が出る。
- VTG1-BC7ENC-VENDOR 差し戻し対応: 新設した `BlockCompressor.h/.cpp` とスモークの `std::vector`・`std::string`・`std::thread` を独自型に置き換えた。入出力は `ByteArray`(= `VariableArray<uint8_t>`)、エラー文字列は `ErrorString`(= `AnsiString`)、ワーカーは `VariableArray<Thread::Thread>`（`Join`・`GetHardwareConcurrency`）。`AssetCookBlockCompressor` は `Core` をリンクする（AssetCook は元から Core を使う。Game は未リンクのまま）。取り込んだ第三者ソースは改変していない。証拠は `.harness/runs/20261004-105824/verify-VTG1-BC7ENC-VENDOR-4.txt`(AssetCook の Debug build、EXIT_CODE=0)・`-5.txt`(AssetCookBlockCompressSmoke・AssetCookTextureSmoke が 2/2 passed)・`smoke-direct-5.txt`(PSNR は BC7 47.69・BC7 アルファ付き 46.99・BC5 50.50・BC4 48.36・BC1 42.86 dB で変わらず)。`BlockCompressor.*` とスモークに `std::vector`・`std::string` が残っていないことを grep で確認した。Notes: Git Bash では `/m:1` が `m:1` に変換されて MSBuild が失敗するので、verify のビルドは PowerShell から走らせる。
- VTG1-COOKER-USAGE（2026-10-04、run `20261004-105824` 反復12、done）: `AssetCook` に `--usage albedo|normal|orm|single|height16`（`--kind texture`、`--format` とは併用不可でマニフェストの format は用途から決まる）と `--quality fast|normal|best` を足した。albedo→BC7 sRGB（ミップは線形空間の平均）、normal→BC5 linear（入力は DirectX の向きのまま。ミップは非正規化ベクトルの平均→再正規化→量子化で、0 に近い長さは (0,0,1) に倒す）、orm→BC7 linear（`--orm-ao`・`--orm-roughness`・`--orm-metallic` の R を R・G・B に詰め、無い枠は AO=1・粗さ=1・メタリック=0。枠の大きさが違えば断る。`--input` は取らない）、single→BC4、height16→R16（16bit の入力は精度を保ち、8bit は x257 で拡大。非圧縮なので BC を通さない）。NVTEX は v0.1 で書き、`--format` 指定の従来経路は v0.0 のまま。マニフェストの format は `nvtex.v0.1.bc7.srgb`・`bc5.linear`・`bc7.linear`・`bc4.linear`・`r16.linear`。`TextureCooker` は `AssetCookTextureCooker` 静的ライブラリへ分け、`AssetCookTextureUsageSmoke`（ORM の詰め方と既定値・法線の mip1・R16 の 16bit 精度と 8bit 拡大・BC4/BC7 の色空間とミップのバイト数・不正入力の拒否）を足して `AssetCookTextureSmoke` から呼ぶ。`AssetCookTextureSmoke` は用途ごとに CLI を回し、NVTEX のヘッダの minor・大きさ・ミップ数・PixelFormat・ColorSpace・ペイロードのバイト数とマニフェストの format、組み合わせの誤り（未知の用途・`--format` との併用・orm に `--input`・orm の元画像なし・orm 以外に `--orm-*`）の拒否を確かめる。クック時間は標準出力へ `TEXTURE_COOK usage=<u> format=<f> size=<w>x<h> ms=<n>` で出す。証拠は `.harness/runs/20261004-105824/verify-VTG1-COOKER-USAGE-1.txt`（Debug の AssetCook・CookedMeshTest build EXIT_CODE=0）・`-2.txt`（指定ctest 3/3 passed）・`-3.txt`（`AssetCookTextureSmoke` の -V。用途ごとの minor=1・mips=4・pixel_format・payload と `TEXTURE_USAGE_SMOKE_RESULT passed`）・`-4.txt`（RelWithDebInfo の AssetCook build EXIT_CODE=0）・`-5.txt`（4096² のクック時間）。
- VTG1-COOKER-USAGE の4096²の計測（RelWithDebInfo、全13ミップ、スレッド数は既定＝ハードウェア並列）: albedo BC7（Normal 品質）4072 ms、同 Best 品質 6732 ms、normal BC5 1333 ms、orm BC7 1399 ms（3枠が同じグレー画像なので albedo より軽い）、single BC4 199 ms、height16 R16 171 ms。BC7 は stop-when の60秒に対して余裕があり、`BlockQuality` の写像（Fast/Normal/Best）は暫定のまま Normal を既定にしてよい。書き出したサイズは BC7・BC5 が 22,370,176 B（NVTEX ヘッダ・ミップ表込み）、BC4 が 11,185,352 B、R16 が 44,739,770 B。
- VTG1-COOKER-USAGE の Notes: (1) `TextureCooker.h/.cpp`・`Main.cpp` は BOM が無いまま日本語コメントを足すと cp932 で崩れる（`#pragma once` の次の行以降が前のコメントに飲まれて C2447）ため BOM を付けた（各1行の差分、行末は元の LF のまま）。(2) stb_image の PNM は 16bit のバイト順を直さず（ビッグエンディアンのまま native として読む）、実測では先頭の画素にヘッダの区切りの改行も混ざったので、16bit の精度の確認は `AssetCookTextureUsageSmoke` の中で非圧縮 zlib の 16bit PNG を作って行った（CLI のスモークの height16 は 8bit の PGM でバイト数・形式だけ見る）。実際の入力は 16bit PNG（stb の PNG 読み込みは正しい）。(3) 視差の高さは R16 と BC4 の両方を焼ける（`--usage height16` / `--usage single`）。どちらにするかは VTG1-STARTUP-COOKED で縞の出方を見て決める。(4) `AssetCookTextureSmoke` の .nvtex の位置はエントリ名の長さでパッケージ内の位置が変わるため、マジックを探して決めている（既存の odd テクスチャの 176 バイトの決め打ちは変えていない）。
