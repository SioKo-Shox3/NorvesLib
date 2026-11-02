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
- 単体CLI旧基準（run 37200047966、artifact 11302304928）の `snapshot.json` はSHA-256 `09af3262f55ce75cf512cfb9086c0dbf3b81cd8344233b5f10ebda8e87f36c85` に一致。指定captureは出力先がsource配下のため拒否された。checkoutと32入力fileのSHA-256が一致する作業用コピーをsourceにしたcaptureも、Skeletalの警告文判定で停止した。完全なcandidate snapshotと79出力の正式比較は未完了。
- そのcaptureで得られた途中出力を旧79出力と照合すると、Textureは既存4一致・12追加、Audioは6一致、Importは13一致、Glbは12一致、Meshは6一致・20不一致、Rawは2不一致、Skeletalは11一致・1不一致・4未生成だった。Skeletalの不一致はmanifestの `source_hash` で、未生成分はcaptureの停止による。Rawの入力は凍結時のCRLFに対しローカルはLF。Meshはmainのクラスタ化変更に加え入力の行末差もあり、各出力差への寄与は未分離。Textureの追加分は現行smokeが生成した出力で、旧出力の不一致ではない。
