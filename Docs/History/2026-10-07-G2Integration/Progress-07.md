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

