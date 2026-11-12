# managed texture asset-set CLI

G2-S6／GR96のtexture spec v1用adapter。入力の読込と結果表示だけをCLI/service側へ置き、初期化・新規公開・既存更新・復旧は受入済みの共通controllerへ委譲する。旧独立stage/publication経路は通常実行から除去する。

## 通常実行

`AssetCook --asset-set <spec.json> --runtime-root <new-or-managed-directory> [--source-root <directory>] [--manifest <file>]`

- 未所有の既存rootは拒否する。旧CLI/PSで作った出力を黙って採用しない
- 新しいrootはstoreを初期化し、Bootstrapで公開する
- 所有済みrootはUpdateへ渡す。NoChangeは書込・世代加算を行わない
- store/ownerを観測してpendingがあれば停止する。通常実行で他のtransactionを自動復旧しない
- specのJSONとsource内容を読む前にpendingを調べる。ただし通常のowner解決には既存spec fileが必要なので、spec自体が消えている場合は下の明示復旧を使う
- source/specの実内容、旧state/controlの現在性はcontrollerがlock内でもう一度確認する

SourceRoot省略は開始時cwd。相対spec/runtime/source/manifestは従来のcwd規則を保つ。SourceRootの`.`、`source/.`、`source/`はdirectory表記として扱う。dotを畳む前に実componentを共通validatorで検査し、`CON/../safe`のように不正componentを隠す表記は拒否する。runtimeはASCII local-drive、manifestはruntime直下の安全な単一名に限定する。

specは16MiB以内。BOMはJSON解析時だけ除き、controllerへは解析に使ったBOM込みの原bytesを渡す。v1 parser、serializer、単体出力形式は変更しない。増分判断や衝突判断をadapterへ複製せず、共通Prepare/SetGuard/Decideを使う。

## 明示復旧

`AssetCook --recover --runtime-root <directory>`

このruntime locatorは親workspaceを選ぶためのもの。処理する対象はそのworkspaceの固定store/pendingであり、検証済みpendingが兄弟rootを指す場合もそのtransactionを復旧する。root単独に対象を限定するコマンドではない。

source/spec/manifestのオプションや通常cookのオプションは混在できない。sourceとspecが不在でも、原controlとbefore/afterのnative ID/bytesでrollbackまたはcommit確定する。復旧後に新しいcookを自動継続しない。新cookには改めて通常コマンドのowner/spec/source検査が必要。

固定storeが無ければ失敗し、作成しない。NoPending、RolledBack、Committedは成功。Busy、未知/壊れたpending、ancestor pending等は失敗して保持する。stage/retired prefixを探索・採用・清掃しない。

## 結果と失敗後の状態

終了codeは成功0、失敗1を維持する。通常結果はpublished、updated、unchangedを表示する。復旧はworkspace_recoveryのno_pending、rolled_back、committedを表示する。

NeedsRecoveryはpendingを残し、明示復旧コマンドを案内する。CommittedButErrorはcommit済み状態を保つ。初期化のPublishedButErrorは公開済みstoreを保ち、自動削除・自動再試行しない。成功後のknown cleanup失敗は成功＋UTF8 locator付き警告とする。

新規store初期化後に画像のcook等が失敗した場合、初期化済みstoreと未公開stageが残ることがある。runtimeは未公開のままで、原index/既存claimを保持する。これを投機的にremove_allで消さない。

## 互換と検証

- 固定単体CLIの79出力byteと5診断を変えない
- texture v1はorigin/mainのCookTextureAssetSet.ps1から凍結したRendering3DTestSilverTextures / Rendering3DTestSilverGltfTexturesの10出力byteと直接比較する
- 元の25呼出し（9成功・16拒否）を別群としてそのまま維持する。既存rootの拒否fixtureは全て未所有なので、新しい所有rootの反復成功とは矛盾しない
- managed CLIの実呼出しは別receipt群。作成、NoChangeの原ID/bytes/write-time不変、1Cook/1Skip、SourceRoot表記、inventory/owner拒否、source/spec不在NoPending、復旧引数拒否を追加する
- native CPUのadapter試験はtest-only probeで中断を作り、通常pending停止、source/spec不在のcommit/rollback、兄弟locatorのworkspaceスコープ、再cookしないこと、未知pending保持を反証する。production故障注入flagは追加しない

## 管理metadataの証拠

runtimeのgolden payload比較は従来の有限一覧のまま。artifactはruntime2ディレクトリを明示選択し、管理証拠を可視のmanaged/配下へ別保存する。hidden-fileの既定除外や一括ignoreに依存しない。

各baseline workspaceはheader、generation3のroots、generation1のstate2本という4fileだけを持つことを確認する。元workspace/store/root/fileのnative IDと時刻、physical GUID path、raw SHA/sizeをreceiptへ保存し、別コピーのbytesを照合する。元IDがコピー先にも保持されるとは扱わない。

ownerは実spec/root canonical identityとmanifestから独立にSHA-256 tupleで算出する。strict JSON/有限shape、claim/stateの対応、golden manifestとのrecord対応、package全bytesのFNVとSHA/sizeを照合する。baseline2回分の各4file＋receipt、CLI receipt、summaryを有限の別inventoryとして受け入れる。79/10 fileの直接byte比較器は変更しない。

textureのproduction callerは `CookTextureAssetSet.ps1` が `AssetCook --asset-set` へ委譲する形で接続済み。`--asset-set` はspec v2のmodel/skeletalを受け付ける（`Tools/AssetCook/TextureAssetSetCook.cpp:106`、`Tools/AssetCook/TextureAssetSetSpec.cpp:270`）。所属資産の追加・除去は `Test/Core/Asset/TextureManagedCliTest.cpp:312` で検証し、budget・`--jobs`・reportsはコミット `d9c2f019` で導入済み。依存の印（`CaptureCookDependencySnapshot`、`CookDependencySnapshot.md`）には、glTF/GLBの全外部buffer・外部imageと選択sidecarのraw byteが既に含まれ、共通の増分判断 `CookCacheDecision` がそれを使う。model資産もこの印による増分cookの対象である。G2-S6全体や全kindの増分統合完了とはしない。texture以外のproduction callers、旧standalone ModelCookCache移行、cross-session実測、最大規模性能、電源断保証は後続。

## 実行受入れ（2026-10-05）

19128d41503a72784ad4703c4ce048e1310d3a9f / run37303534756で36CPUとadapter receipt、Update29/Bootstrap17の実child終了、全条件flagを確認。元25実CLIの9成功/16拒否、別15実CLIの8成功/7拒否、7単体smoke/79＋10直接byte/5診断が成功した。管理metadata12fileの有限一覧・raw SHA/size・原native ID・独立owner tuple・package/dependency record、3 ZIP/API digest/CRC/exact inventory、両実MSVC x64 binaryのCNGを独立照合し、親側でも全verifierを再実行した。

旧証拠216fileを保持し、共通controller/cache/parser/serializerの不変を確認した。cross-session実測と広いGR96残件は未完了。次はロードマップのGR79モデル材質writer/cookを進め、spec v2/可変inventory拡張を先行させない。

## 実行受入れ（2026-11-02、ローカル Windows）

Windows 11 10.0.22631、Windows PowerShell 5.1.22621.6133で実行した。使用したDebug構成の `AssetCook.exe` のSHA-256は `430a20c25bf2ad58a8422385a2618ebe6414147bc469cba3af500305b2b080bb`、`CookedMeshTest.exe` は `c70158994cc512d0c8b13248018573a4e4964febf2d5cbf2fdf75aebf01ce8bb`。凍結基準を作ったRelease構成の `AssetCook.exe` は `7ac890ce40434bf9e1c062ca0fe0ee0f5f2b49d79fbf40ca7b81662a01b50079`、`CookedMeshTest.exe` は `93b8d1dfc012bcb4afeffdefd4eca356bf2d709dead2bd76cf6ab4bbbeaf0c48`。

- texture旧基準（run 37205025949、artifact 11303728946）の `snapshot.json` はSHA-256 `9e20e823c0c518c5b6dc34f5d561472ebcf10ac4dc5acd0042627d8e5bfe96e2` に一致。Debugの `AssetCook --asset-set` と `CookTextureAssetSet.ps1` の両方で、2 specの各10出力（6＋4）がそれぞれ基準と生byte一致した。各経路の同一root再実行でも10出力のbytesと更新時刻が不変で、管理情報のbytesと更新時刻も不変だった。直接呼び出しは `unchanged` を表示し、包みの再実行後に同じrootを直接呼んだときも `unchanged` を表示した。
- 標準の `Scripts/VerifyTextureAssetSet.py` は当初、凍結時との入力hash差3件で停止した。うち `Scripts/CookTextureAssetSet.ps1` の不一致は旧PSから包みへの変更によるもので、CIの `texture_parity` も必ず停止する原因だったため、checkout入力の初回と終了時の照合対象から外した。native検証はこのPS1を読まず、旧出力の正しさはreceiptのsnapshot SHAとpayload hashで保つ。残る2 specはこの機のcheckoutがLFのため凍結値と異なり、CIのWindows checkout（CRLF）に相当するHEADのCRLF化では凍結値と一致した。8 PNGはrawで一致した。照合変更後のローカル実行もspec 2件を名指しして入口で停止した。この機の `powershell.exe` のbinary SHA-256は `3247bcfd60f6dd25f34cb74b5889ab10ef1b3ec72b4d4b3d95b5b25b534560b8` で、receiptの `38f4384643b3fa0de714d2367b712c2e0fa1c89e2cfd131ae6b831ad962b1033` と異なるため、specの行末を直してもローカルの完走は `powershell.exe` のbinary照合で止まる。スクリプトの失敗時検査（既存のdirectory・empty・file・junctionの拒否、source-reparseの拒否、prefix・manifestの衝突の拒否、失敗したrootを公開しないこと・stageを残さないこと）と末尾の `VerifyManagedTextureCli.py` に対するローカルの根拠は、通過済みの `TextureManagedCliTest` と `TextureAssetSetOutputTest` である。スクリプト全体の完走には `windows-assetcook.yml` のCI手動起動による確認が必要で、まだ実行していない。上記のbyte比較には固定snapshotを検証したうえで比較器の `load_reference`・`files`・`compare` を使用した。
- 指定のDebugビルドは成功し、`TextureAssetSetSpecTest`、`TextureAssetSetOutputTest`、`CookDependencySnapshotTest`、`CookCacheDecisionTest`、`CookOutputPackageTest`、`SingleAssetCookServiceTest`、`AssetCookTextureSmoke`、`AssetCookGlbSmoke` は8/8成功。`CookDependencySnapshotTest` と `CookCacheDecisionTest` は、外部buffer・画像の変更・不在・復帰、sidecarの追加・変更・不正・必須不在、出力packageの破損に対する依存記録・再cook判定・拒否を受け持つ。依存記録の段では画像を完全decodeしない。
- 追加の管理試験は `CookManagedUpdateTest` と `TextureManagedCliTest` が成功し、`CookManagedBootstrapTest` は180秒でタイムアウトした。出力には復旧で `RolledBack` を期待した箇所の `rename_no_replace; win32_hex=5` と `Error` が残ったため、Bootstrapの失敗時の保持・復旧をこの実行で合格とは扱わない。比較と並行しない単独の再実行（`ctest -R "^CookManagedBootstrapTest$" --timeout 300`）でも300秒でタイムアウトし、最後の出力は `line 182: InitializeNewCookManagedStore(Owner, out, error) == CookManagedStoreInitializationResult::Created` だった。今回の変更ではC++と試験を変更していない。
- 旧 `CHECK` は失敗時に `std::abort()` を呼び、当時の版には試験共通の対話窓抑止がなかったため、Debug CRTのabort対話窓とWERが入力を待ち、ctestのtimeoutまで停止した。現行版には共通の対話窓抑止があり、`CHECK` もstderrをflushして `std::_Exit(EXIT_FAILURE)` で終了する。強制した失敗では親が0.070秒、`--bootstrap-child` の子が0.030秒で、ともに終了コード1だった。旧182行の初期化失敗は、約13.3万項目の共有TEMP配下にfixtureを置き、管理ストアがRuntimeRootの祖先からボリュームの根まで列挙する際に1ディレクトリ16384項目の上限を超えたことが原因で、修正前の排他競合がない実行では `STORE_INITIALIZATION result=5 error=cook_managed_store: entry_limit` で0.6秒で終了した。fixtureをctestの作業ディレクトリ配下へ移し、製品コードと期待値は変えていない。timeoutは180秒のまま。`rename_no_replace; win32_hex=5` は置き場の変更後に再現せず、原因は未特定。`cook_store_initialization: volume_busy` はボリューム単位・機械全体の名前付きmutex `Global\NorvesLib.AssetCook.DestinationVolumeLock.v1.<ボリューム GUID>` を取得できないと待たずに返る結果で、別枠の管理ストア試験との並行時に発生する。修正後、既定のTEMPでBootstrapは24.40秒で成功した。今回の検証でもBootstrapは24.35秒で成功し、既定のTEMPでは `CookManagedStoreInitializationTest`、`CookManagedUpdateTest`、`TextureManagedCliTest` が0.26秒・0.11秒・0.11秒で失敗した。StoreInitializationの出力は `cook_managed_store: entry_limit` だった。TEMP/TMPを空のディレクトリへ向けた再実行(TEMP の置き場のパスが長いと取引の段のディレクトリ作成がパス長の上限で失敗するため、短いパスの空のディレクトリを使った)では3本がそれぞれ1.68秒・84.37秒・12.63秒で成功した。今回ビルドしたDebugの `CookedMeshTest.exe` のSHA-256は `99cd12d9b09a3e7f861bb00cc12cd6971da5a5ee44cd403e7b5d058dacda159b`。
- 再実行では、別の作業ツリーの管理ストア試験と重なったBootstrapが `STORE_INITIALIZATION result=4 error=cook_store_initialization: volume_busy` で0.03秒で失敗した。製品はボリューム単位のmutex `Global\NorvesLib.AssetCook.DestinationVolumeLock.v1.<GUID>` を待たずに返す設計で、製品コードは変更していない。試験側で別プロセスによる `Busy` だけを1プロセス累計90秒まで待ち直すようにした。試験自身がロックを握って `Busy` を期待する箇所は待ち直さず、期待値は弱めていない。timeoutは180秒のまま。近くの `CookManagedUpdateTest`、`TextureManagedCliTest`、`CookManagedStoreInitializationTest` も、既定のTEMP配下では `entry_limit` で失敗していたため、fixtureの置き場をctestの作業ディレクトリ配下へ移し、同じ待ち直しを入れた。TEMP/TMPを既定のままにした検証では、`CookManagedBootstrapTest` は22.10秒、`CookManagedUpdateTest` は88.89秒、`TextureManagedCliTest` は14.84秒、`CookManagedStoreInitializationTest` は1.89秒で、4本ともPassedだった。

### 単体 CLI の凍結基準との比較

比較対象 HEAD は `b9d7468e3f0c739a1b51b8a42a0fdca329d1622d`。今回の実行環境は Windows 11 10.0.22631、PowerShell 7.6.9、Python 3.14.3、CMake 3.31.6。Debug の `AssetCook.exe` は SHA-256 `ba17bc55a585f9f209bdf9852e5d877008c0988396b53e1b7adf32f347fc67cc`、`CookedMeshTest.exe` は `98f3bbc8f3a1bf3f15406f033823624c5d96a56736661545d3891e8accb2aaff`。上の別ゲートで使った Debug バイナリとは区別する。

A は run `37200047966`、artifact `11302304928`、commit `d1307c32ec86ec187c7cfb3959d5452760059543`、Release の凍結基準。取得先は `%TEMP%/NorvesCliP2/frozen`。`snapshot/snapshot.json` の SHA-256 `09af3262f55ce75cf512cfb9086c0dbf3b81cd8344233b5f10ebda8e87f36c85` と79出力、および凍結 exe 2本の上記 SHA-256 が `Docs/AssetCookCliFrozenBaseline.json` と一致した。snapshot の各出力もサイズ・SHA-256・有限一覧を検証した。

| 記号 | exe・source | capture の結果 |
|---|---|---|
| B | 現行 Debug・checkout（LF） | `%TEMP%/NorvesCliP2-U1-cp932` を再利用。recipe・exe hash が現在値と一致。全7件成功、91出力 |
| C | 現行 Debug・HEAD の CRLF 写し | `%TEMP%/NorvesCliP2/C`。全7件成功、91出力 |
| D | 凍結 Release・checkout（LF） | `%TEMP%/NorvesCliP2/D`。Raw 成功、Texture の `--usage albedo` で終了コード1。6出力、snapshot なし |
| E | 凍結 Release・HEAD の CRLF 写し | `%TEMP%/NorvesCliP2/E`。D と同じ箇所で終了コード1。6出力、snapshot なし |

D/E とも `logs/Texture.log:15` に `AssetCook error: unknown argument: --usage`、同17–18行に `AssetCookTextureSmoke.cmake.in:148` と `AssetCook --usage albedo が exit code 1 で失敗した` がある。これは `bb53b109` で増えた呼び出しに凍結 exe が対応しないため。Audio 以降の73出力は capture では未生成であり、不一致とは数えない。D/E の生出力は driver の `artifacts()` で選び、A の `files/` と比較した。

A 対 B/C の正式な `compare` はいずれも終了コード1。両方とも recipe 差の警告と追加12件を含み、旧出力の不一致は B が22件、C が20件だった。原因別の排他的な件数は **(a) クラスタ化20件（11 package＋9 JSON）、(b) 行末2件（1 package＋1 JSON）、(c) その他0件**。B で一致する旧出力は57件、C では59件。欠落は B/C とも0件。

| ケース | 旧出力数 | B の一致 | C の一致 | 主原因 |
|---|---:|---:|---:|---|
| Raw | 2 | 0 | 2 | (b) 2件 |
| Texture | 4 | 4 | 4 | 一致。追加12件は別集計 |
| Audio | 6 | 6 | 6 | 一致 |
| Mesh | 26 | 6 | 6 | (a) 20件。うち5 manifest は (b) も重なる |
| Glb | 12 | 12 | 12 | 一致 |
| Import | 13 | 13 | 13 | 一致 |
| Skeletal | 16 | 16 | 16 | 最終出力はすべて一致 |

下表は旧79出力を省略せず列挙する。「差 offset; A/X」は0始まりの先頭差 byte と旧/比較先の size（byte）。「一致」は全 byte と size の一致、「未生成」は capture がそのケースまで進まなかったことを表す。`a` は CRLF を揃えても残るクラスタ化差、`b` は CRLF を揃えると消える差で、行末も重なる Mesh manifest を `b` に重複計上しない。

| パス（A の files/ からの相対名） | B | C | D | E | 分類 |
|---|---|---|---|---|---|
| `Audio/AB/Cooked/a.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Audio/AB/Cooked/b.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Audio/AB/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Audio/BA/Cooked/a.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Audio/BA/Cooked/b.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Audio/BA/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Cooked/model.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Cooked/model.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Cooked/model.nvpkg.img1.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Cooked/model.nvpkg.img2.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Retained/Cooked/model.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/Retained/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/SettingsGuard/Cooked/model.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/SettingsGuard/Cooked/model.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/SettingsGuard/Cooked/model.nvpkg.img1.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/SettingsGuard/Cooked/model.nvpkg.img2.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Glb/Embedded/SettingsGuard/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/model.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/model.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/model.nvpkg.img1.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/model.nvpkg.img2.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/moved.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/moved.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/moved.nvpkg.img1.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/moved.nvpkg.img2.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/variant.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/variant.nvpkg.img0.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/variant.nvpkg.img1.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/Cooked/variant.nvpkg.img2.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Import/ImportSettings/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Bom/Plain/Cooked/Rendering3DTestSilverGltf.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/Bom/Plain/manifest.json` | 差 167; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/Bom/Prefixed/Cooked/Rendering3DTestSilverGltf.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/Bom/Prefixed/manifest.json` | 差 167; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/Cooked/Rendering3DTestSilverGltf.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/CorruptMsh0FourCC.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/manifest.json` | 差 167; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/ShortMsh0StoredSize.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/SourceHash/Baseline/Cooked/Rendering3DTestSilverGltf.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/SourceHash/Baseline/manifest.json` | 差 167; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/SourceHash/MutatedBuffer/Cooked/Rendering3DTestSilverGltf.nvpkg` | 差 24; 4984568/3021848 | 差 24; 4984568/3021848 | 未生成 | 未生成 | a |
| `Mesh/SourceHash/MutatedBuffer/manifest.json` | 差 167; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/Synthetic/boundary_128/boundary_128.manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Synthetic/boundary_128/boundary_128.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Synthetic/one_triangle/one_triangle.manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Synthetic/one_triangle/one_triangle.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Synthetic/split_129/split_129.manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/Synthetic/split_129/split_129.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Mesh/ValidVariants/base_color_tex_coord_zero.manifest.json` | 差 467; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/Cooked/base_color_tex_coord_zero.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/Cooked/extensions_required_empty.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/Cooked/optional_primitive_extension.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/Cooked/transformed_mesh_node.nvpkg` | 差 24; 4984568/3021928 | 差 24; 4984568/3021928 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/extensions_required_empty.manifest.json` | 差 467; 523/523 | 差 467; 523/523 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/optional_primitive_extension.manifest.json` | 差 470; 526/526 | 差 470; 526/526 | 未生成 | 未生成 | a |
| `Mesh/ValidVariants/transformed_mesh_node.manifest.json` | 差 463; 519/519 | 差 463; 519/519 | 未生成 | 未生成 | a |
| `Raw/Cooked/raw_sample.nvpkg` | 差 24; 208/207 | 一致 | 差 24; 208/207 | 一致 | b |
| `Raw/manifest.json` | 差 120; 375/375 | 一致 | 差 120; 375/375 | 一致 | b |
| `Skeletal/Cubic/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/Cubic/rig.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/Morph/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/Morph/rig.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/Reduce/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/Reduce/rig.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/Cooked/disabled.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/Cooked/explicit.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/Cooked/imported.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/Cooked/ValidU8Float-mutated.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/Cooked/ValidU8Float.nvpkg` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/disabled.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/explicit.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/imported.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/manifest-mutated.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Skeletal/RuntimeRoot/manifest.json` | 一致 | 一致 | 未生成 | 未生成 | 一致 |
| `Texture/Cooked/odd_texture.nvpkg` | 一致 | 一致 | 一致 | 一致 | 一致 |
| `Texture/Cooked/sample_texture.nvpkg` | 一致 | 一致 | 一致 | 一致 | 一致 |
| `Texture/manifest.json` | 一致 | 一致 | 一致 | 一致 | 一致 |
| `Texture/odd_manifest.json` | 一致 | 一致 | 一致 | 一致 | 一致 |

#### クラスタ化と行末の切り分け

D/E の capture 後、凍結コミットの `Tools/AssetCook/AssetCookMeshSmoke.cmake.in` を `git show d1307c32:Tools/AssetCook/AssetCookMeshSmoke.cmake.in` で `%TEMP%/NorvesCliP2/tools/AssetCookMeshSmoke-frozen.cmake.in` に取り出し、凍結 `AssetCook.exe` とそれぞれの source で独立実行した。出力先は `D-mesh-proof/Mesh` と `E-mesh-proof/Mesh`。両方終了コード0、各26出力。E の補助出力は26/26が A と生 byte 一致し、D は21/26一致、残る5 manifest だけ `source_hash` が異なった。補助出力は上表の D/E capture の未生成欄とは混ぜない。

現行 Mesh smoke の凍結時との差はクラスタ数の期待値 `25562 → 1029` と、`DAG_SMOKE_EXE` 指定時だけ走る LOD 階層の追加ブロック。capture はその変数を渡さないため、今回の11 package はすべて **`NVMESHv0`、version 0.0、header 256 byte、cluster record 80 byte** のまま。LOD 階層・NVMESH v1/v1.1 の追加は今回の差を直接作っていない。

`Tools/AssetCook/MeshCooker.cpp` の `BuildNvmeshBytes` と `Library/Core/Public/Asset/CookedMeshWireFormat.h` を読み、節を header → submesh → material → cluster → string → vertex → index の順に分けて照合した。代表の `Mesh/Cooked/Rendering3DTestSilverGltf.nvpkg` は次のとおり（位置は NVMESH 先頭から、package 内の NVMESH 先頭は232）。

| 領域・値 | A | C | 差の意味 |
|---|---|---|---|
| package 全体 | 4984568 byte | 3021928 byte | 先頭差24は NVPKG の `PackageSize` |
| NVMESH 全体 | 4984336 byte | 3021696 byte | 内側の先頭差40は `FileSize` |
| vertex / index / cluster 数 | 67042 / 198366 / 25562 | 67042 / 198366 / 1029 | クラスタのまとめ方だけ変化 |
| submesh | 64 byte @256 | 64 byte @256 | レコード内 `ClusterCount`（+24）だけ変化 |
| material | 64 byte @320 | 64 byte @320 | byte 一致 |
| cluster | 2044960 byte @384 | 82320 byte @384 | 80 byte × cluster 数。内容も変化 |
| string | 178 byte @2045344 | 178 byte @82704 | 移動のみ、byte 一致 |
| vertex | 2145344 byte @2045528 | 2145344 byte @82888 | 移動のみ、byte 一致 |
| index | 793464 byte @4190872 | 793464 byte @2228232 | クラスタ順へ並べ替え。向きと重複数を保った三角形の多重集合は一致 |

11 package 全件で材質・文字列・頂点の一致、三角形の多重集合一致、submesh の差が cluster 数だけであることを確認した。NVMESH header の差は `FileSize`、cluster 表の size、後続節の offset、`PayloadHash`、`ClusterCount` のみ。package 側の差も全体 size と entry の size/hash に限られる。意図的に壊した `CorruptMsh0FourCC.nvpkg`・`ShortMsh0StoredSize.nvpkg` も同じ分類で、元の FourCC/短縮値の作り方は維持される。`SourceHash/MutatedBuffer` だけは新 cluster 数1028、cluster 表82240 byte、package3021848 byteで、同じ検査を通った。

原因は `13ba56a7` の `Library/Core/Private/Rendering/MegaGeometry/MeshClusterizer.cpp`。位置による隣接と、隣接が尽きた後の近い三角形からの成長が cluster の件数・範囲・境界・index 順序を変える。`MeshCooker.cpp` が cluster 表・並べ替えた index・header/hash を書き、`Library/Core/Private/Asset/CookedMeshLoader.cpp` はその版と節を検査する。LOD 書き出しの `CookLodGraphMesh` / `SerializeCookedMeshV1Paged` は別の format 指定であり、今回使われない。

9 manifest の A 対 C は `/assets/0/cooked_hash` だけが異なる。通常は `4eaa09c42173afa3 → b47d19c4baa20a4d`、`SourceHash/MutatedBuffer/manifest.json` は `97af1be12256d6ab → 68e11c13a79aa2cf`。他の JSON 値は同じで、package 内の NVMESH の変更から連鎖する (a) とした。

行末の (b) 2件は `raw_sample.bin` の LF/CRLF 差。B/D の Raw package は207 byte、A/C/E は208 byteで、manifest の `source_hash`・`cooked_hash` もその1 byte差から変わる。また `Mesh/manifest.json`、`Mesh/Bom/Plain/manifest.json`、`Mesh/Bom/Prefixed/manifest.json`、`Mesh/SourceHash/Baseline/manifest.json`、`Mesh/SourceHash/MutatedBuffer/manifest.json` の5件では B の `source_hash` も行末で変化する（通常 `87cbc9ceb25ac859 → 05b973076601d67e`、BOM付き `d86bea10639d5d7f → e104881275d30aae`、buffer変更 `12fe109cd4a74344 → 95fd5c52d337bf23`）。C では全5件の `source_hash` が A と一致し、残る `cooked_hash` だけを (a) に数えた。Skeletal は途中の manifest ではなく、capture 完走後の16出力で一致した。

#### 追加出力と recipe

追加12件（6 package＋6 JSON）は旧79出力の不一致に含めない。`bb53b109` が5用途の10件を増やし、`e4a71feb` がタイル付き single の2件を増やした。用途別5種類の形式が v0.1 から v0.2 へ変わったのも `e4a71feb`。今回の一覧は次のとおり。

| 追加パス | size（byte） | smoke を追加したコミット |
|---|---|---|
| `Texture/Cooked/tiled_single.nvpkg` | 350400 | `e4a71feb`（タイル付き single） |
| `Texture/Cooked/usage_albedo.nvpkg` | 584 | `bb53b109`（用途別5種類） |
| `Texture/Cooked/usage_height16.nvpkg` | 642 | `bb53b109`（用途別5種類） |
| `Texture/Cooked/usage_normal.nvpkg` | 584 | `bb53b109`（用途別5種類） |
| `Texture/Cooked/usage_orm.nvpkg` | 576 | `bb53b109`（用途別5種類） |
| `Texture/Cooked/usage_single.nvpkg` | 528 | `bb53b109`（用途別5種類） |
| `Texture/tiled_single_manifest.json` | 416 | `e4a71feb`（タイル付き single） |
| `Texture/usage_albedo_manifest.json` | 414 | `bb53b109`（用途別5種類） |
| `Texture/usage_height16_manifest.json` | 422 | `bb53b109`（用途別5種類） |
| `Texture/usage_normal_manifest.json` | 416 | `bb53b109`（用途別5種類） |
| `Texture/usage_orm_manifest.json` | 407 | `bb53b109`（用途別5種類） |
| `Texture/usage_single_manifest.json` | 416 | `bb53b109`（用途別5種類） |

recipe は両方31項目。B は18一致、行末だけ8件、smoke 変更4件（うち3件は行末も異なる）、driver 変更1件。C は26一致、smoke 変更4件、driver 変更1件。旧31項目のうち d1307c32 から git blob が変わっていない26項目は、CRLF 写しの hash がすべて凍結 recipe と一致した。属性は全30ファイルで `text/eol: unspecified`。NUL のあるバイナリを除き、recipe 対象だけ LF→CRLF とし、実際に byte が変わったのは11件。driver は呼び出し元の実ファイルを記録するため、写しの Scripts へ置き換えない。項目別の分類は次のとおり。

| recipe 項目 | A 対 B | A 対 C | 原因・コミット |
|---|---|---|---|
| `Assets/Models/M9Skinned/CubicChannels.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ExtraZeroInfluences.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/FiveInfluences.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/MorphDrop.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ThreeClips.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ThreeClipsInvalid.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ThreeCubicClips.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ThreeMorphClips.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ThreeMorphClipsInvalid.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/TwoMaterials.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ValidImportScale.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ValidU16Unorm8.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ValidU8Float.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/M9Skinned/ValidU8Unorm16.gltf` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.bin` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_albedo.png` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_arm.png` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Assets/Models/Rendering3DTestSilverGltf/textures/silver_normal-ogl.png` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Test/Core/Asset/CookedSkeletalAssetTest.cmake.in` | smoke の変更 | smoke の変更 | `2707ab89`：出力の `ENCODING UTF-8` |
| `Test/Core/Asset/Fixtures/AssetCook/embedded_tri.glb` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Test/Core/Asset/Fixtures/AssetCook/embedded_tri_changed.glb` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Test/Core/Asset/Fixtures/AssetCook/embedded_tri_invalid_image.glb` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Test/Core/Asset/Fixtures/AssetCook/embedded_tri_mismatched_mime.glb` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Test/Core/Asset/Fixtures/AssetCook/raw_sample.bin` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Tools/AssetCook/AssetCookAudioSmoke.cmake.in` | 一致 | 一致 | 変更なし（git blob 同一） |
| `Tools/AssetCook/AssetCookGlbSmoke.cmake.in` | smoke の変更＋行末 | smoke の変更 | `619e16eb`：manifest の hash 計算を .NET へ変更 |
| `Tools/AssetCook/AssetCookImportSmoke.cmake.in` | (b) 行末 | 一致 | 変更なし（git blob 同一） |
| `Tools/AssetCook/AssetCookMeshSmoke.cmake.in` | smoke の変更＋行末 | smoke の変更 | `13ba56a7`：クラスタ数の期待値、`4bc840da`・`585ebd09`：任意の LOD smoke |
| `Tools/AssetCook/AssetCookTextureSmoke.cmake.in` | smoke の変更＋行末 | smoke の変更 | `bb53b109`・`38110fef`・`e4a71feb`：用途別・タイルの smoke |
| `driver` | driver の変更 | driver の変更 | `b9d7468e`：source 配下出力の拒否文言と既存出力の判定 |

#### 基準の保存と適用範囲

(c) は0件で、未説明の旧出力差は残らなかった。C の `snapshot/` をそのまま `%LOCALAPPDATA%/NorvesLib/AssetCookCliBaseline/b9d7468e3f0c-crlf/snapshot` へ新規保存した。保存後に snapshot の有限一覧・全出力 byte と元 C の一致を確認した。91出力（56 package＋35 JSON）、`snapshot.json` の SHA-256 は `e58a03535cd8c52f0121d7acb1d616be019fba64e632592d971a5f71fdfe6a52`。採取コミットは `b9d7468e3f0c739a1b51b8a42a0fdca329d1622d`、exe hash はこの節の現行 Debug の2本、source は `%TEMP%/NorvesCliP2/src-crlf`。参考として B の snapshot SHA-256 は `a7b76d2dd924e3d776375b5437af661ad654c742298da646ea7610d1fb6aa6ff`。再現手順は [単体 CLI 比較手順](../TestingAssetCookCliParity.md#クラスタ化の後の基準) に記す。

CI の `windows-assetcook.yml` はまだ旧 artifact `11302304928` を参照し、この新しいローカル基準は artifact として公開していない。A 対 C の `compare` が成功したという意味ではなく、recipe・追加・クラスタ化を説明したうえで保存した別基準である。凍結 Release と現行 Debug の比較であり、現行 Release の実行結果はこの比較に含まない。

## CI の手動起動で検証するもの

`windows-assetcook.yml` を既定の引数で手動起動すると、`NORVES_MERGE_VALIDATION=true` となり、`[validate-merge]` と同じ範囲を検証する。

```powershell
gh workflow run windows-assetcook.yml --ref <branch>
```

段4・5の凍結基準の取得と固定hashの照合、段6の在庫79件の照合は実行する。段6のrecipe照合は行わず、段17・18の `capture` と `parity`、それに続く候補のartifact保存は skipped になる。`texture_parity`（`VerifyTextureAssetSet.py`）・CPU契約・Role Profile・Sampler・骨格ローダなど、ほかの段は実行する。段の番号は GitHub の画面で「Set up job」を1とした番号である。

凍結時点の出力を確かめ直す場合は、`-f frozen_parity=true` を指定する。

```powershell
gh workflow run windows-assetcook.yml --ref <branch> -f frozen_parity=true
```

この指定では従来どおりrecipe照合と全出力のbyte比較を実行する。mainのクラスタ化（`13ba56a7`）以後の出力は旧基準と一致しないため、凍結時点の出力を確かめ直すときだけ使う。

push時の `[validate-assets]` / `[validate-merge]` の起動条件と検証範囲は変わらない。

## cook の前の generator（--run-generators）

```powershell
AssetCook --asset-set <spec.json> --runtime-root <directory> --source-root <directory> --run-generators
```

`--run-generators` を付けた場合だけ、spec v2 の `generators` を cook の前に実行する。引数値を取らないフラグで、重複指定や `--run-generators=<value>`、`--recover` との併用は終了コード1で拒否する。spec の検査は generator を起動する前に完了する。固定 pending 等の管理状態の観測も先に行う。

各 generator は宣言順に処理し、次のいずれかなら実行して `ran`、すべて該当しなければ `skipped` となる。

- 同じ名前（完全一致）の前回の記録がない。
- inputs の hash が前回と異なる。
- command の正準形の hash が前回と異なる。
- 宣言された outputs のいずれかが存在しない、または存在の確認に失敗する。

inputs は各段を判断する直前に読み取るため、前の generator の output を後の generator の input にできる。各入力は通常ファイルである必要がある。inputs の宣言順に `path + "\n" + byte 数の10進表記 + "\n" + 内容の SHA-256（小文字16進64桁）+ "\n"` を連結し、その全体の SHA-256 を inputs の hash とする。空の inputs は空列の hash になる。ファイルの時刻には依存しない。

command の正準形は `command\n`、各 command 要素、`cwd\n`、正規化した cwd、`outputs\n`、正規化した各 output の順に並べる。値はそれぞれ `UTF-8 の byte 数の10進表記 + ":" + 値 + "\n"` で符号化し、その全体の SHA-256 を取る。command の引数の空白や順序、cwd、outputs の順序も判定に含まれる。省略した cwd と `"."` は同じ空文字列として記録される。プログラム本体の内容や出力の内容は、この判定では hash 化しない。実行ファイル等の内容変更も検知したい場合は inputs に宣言する。

### 起動と出力先

Windows の `CreateProcessW` で shell を通さず直接起動し、子の終了を待つ。command は1要素が1引数で、shell 用の引用符をあらかじめ付けない。空白・引用符を含む引数は Windows の引数引用規則で符号化し、引用符直前の逆斜線を `2n+1` 本、閉じ引用符直前の末尾の逆斜線を `2n` 本にする。`argv[0]` は全体を引用符で囲み、プログラム名そのものに `"` があると `generator_command` で拒否する。構築した command line は終端 NUL を除いて32766 UTF-16 code unit 以下で、超過は `generator_command_too_long` となる。`|`・`>`・環境変数等の shell 展開は行わない。

絶対 path のプログラムはその path を使い、`/` または `\` を含む相対 path のプログラムは generator の cwd から絶対化する。区切りのない名前は `CreateProcessW` の通常の実行ファイル探索に任せる。cwd は省略時 source root、指定時 source root 相対。inputs・outputs の基準は cwd にかかわらず source root のままであり、command 内の相対引数は子が cwd を基準に解釈する。親の環境を継承し、有効な標準入力・標準出力・標準エラーの handle だけを複製して子に渡す。子の標準出力・標準エラーを別途収集・解析することはない。

実行の前に全 generator の path と出力先を解決する。宣言した outputs が runtime root、同じ親の `.norves-assetcook`、`<runtime-leaf>.reports` の内部・同一・祖先になる場合や、spec 自体またはその祖先になる場合は `generator_output_protected name=<name> output=<path>` で拒否する。比較は path の区間ごとに大文字小文字を無視して行う。この保護は宣言した outputs の検査であり、子プロセスの書き込みを隔離する sandbox ではない。

### 失敗・記録・report

子が非ゼロで終了すると `generator_failed name=<name> exit_code=<code>`、0で終了しても output が欠けると `generator_failed name=<name> exit_code=0 missing_output=<path>` を報告する。起動・待機・終了コード取得の失敗は `generator_launch_failed name=<name> win32=<code>` となる。入力の不在・読込失敗はそれぞれ `generator_input_missing name=<name> input=<path>`・`generator_input_read name=<name> input=<path>`、hash の失敗は `generator_hash name=<name>` となる。これらは cook に進まず CLI の終了コード1となる。起動できない場合は子の終了コードはなく、Win32 エラーを報告する。

失敗した generator は `failed`、処理しなかった残りは `not_run` とし、既に完了した段の `ran`・`skipped` は保持する。全 path の事前検査で失敗した場合は、それ以前の段も未実行なので `not_run` のままとなる。子が source 側に既に作った出力は巻き戻さない。

前回の記録は runtime の claim に対応する cook state の任意欄 `generators` から読み取る。各要素は `{"name":"make_texture","inputs":"<sha256>","command":"<sha256>"}` で、空なら欄自体を省略する。欄のない旧 state は記録なしとして扱うが、所有済み root の state ファイル自体が欠ける場合は `generator_state_missing` で停止する。不正な state や binding の不一致も停止する。

全 generator の成功後、新しい記録は cook state と同じ管理取引で保存する。generator の失敗時は記録を保存しないため、途中で成功した段も、その記録が前回と異なるままなら次回再実行される。cook の公開前に失敗した場合も未公開の記録は採用しない。公開後のエラーや復旧では通常の管理取引の確定状態に従う。

資産がすべて Skip で manifest の内容も同じでも、generator の記録だけが変われば `unchanged` にはせず、資産を再 cook せず記録を更新する。state ファイルだけを書き換える処理ではなく、通常の取引で state・index の世代を進め、同一内容の manifest も再公開する。

`--run-generators` を付けない場合は generator を一切実行せず、既存 state の記録を引き継ぐ。既存の cook の出力形式と byte 列は変わらず、入力も出力も変わらない再実行は通常どおり `unchanged` となる。この呼び出しでは report に generator の行を追加しない。

実行結果がある場合、`cook_report.json` の任意配列 `generators` に宣言順で `{"name":"make_texture","status":"ran"}` の形を出し、`cook_report.md` にも `Generators` の節を出す。status は `ran`・`skipped`・`failed`・`not_run`。stderr には各行を `AssetCook generator name=<name> status=<status>` として出し、起動直前には `AssetCook generator running name=<name>` も出す。report の保存先は `cook_report_directory=<path>` で示す。generator の失敗時も結果を report に残す。

generator は管理 store のボリューム lock の外で走る。同じ spec の generator を2つの cook が同時に実行することは協調 writer の前提の外であり、防止しない。呼び出し側で同じ生成入力・出力への同時実行を避ける必要がある。
