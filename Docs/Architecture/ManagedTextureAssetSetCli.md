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

この単位だけでG2-S6全体や全kindの増分統合完了とはしない。production callers、旧standalone ModelCookCache移行、model対応spec v2、glTF外部依存・sidecar、inventory追加除去、budget/jobs/reportsは後続。cross-session実測、最大規模性能、電源断保証も含めない。
