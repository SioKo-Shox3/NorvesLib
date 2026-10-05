# 新規runtimeの管理付き公開とpending復旧

対象: G2-S6／GR96。texture spec v1と同じ集約出力形式を用いる実cook API。既存runtimeの更新とproduction `--asset-set`への切替は含めない。呼出側がspecを解析して単体要求集合とそのspec原bytesを渡す。

## 入口と条件

`BootstrapCookManagedAssetSet`は初期化済みstore、不在runtime、texture単体要求1〜4096件に限定する。共通prepare/集合guard/stage/captureで記録を作る。外部から受け取ったintent値をwrite capabilityにはしない。specは16MiB以下、packageは初期profileで各512MiB以下。これらは全heapや総出力容量の上限ではない。

Createdだけoutを更新する。固定pending公開後の失敗はNeedsRecovery、commit後にpendingを退役した後の失敗はCommittedButErrorとして公開済み安定状態を保持する。errorはrequest/outから独立して渡す。

成功outにcleanup未完了と今回の退役directoryを報告できる。資産の確定と一時物の清掃を混同しない。次processは残物をprefixで採用・掃除しない。

## scopeの区別

新cookは元callerのspec/owner/runtime/manifestから独立bindingを導く。一方、source/specを失ったbootstrap復旧では同じbindingを独立再導出できない。

復旧は次の順序で限定する。

1. caller runtime locatorからvolume lockと物理workspaceを取得。保存locatorを使わず、固定storeと全物理祖先を観測する
2. live headerのStoreId/workspace/store IDと実pending directory IDへrecovery envelopeを束縛する
3. 固定候補だけからbefore/after indexとafter stateの原objectを読む。ID・size・全byte hashと共有codec、旧claim保持/新claim1件/+1世代を検証する
4. この検査が終わった後だけ、claimに一致するroot leafとflat manifest名を読取hintとして公開する
5. 既知root.stageまたはfinal rootの同IDからmanifestを読み、full intentと全treeを検証する

保存された絶対RuntimeRootIdentityはstateとの比較専用。I/Oには戻さない。ManifestNameとRootLeafは検査済み相対leafだけを使う。復旧readerを新cookの所有許可には流用しない。初期recovery readerはBootstrapだけを受理し、Updateは拒否する。

ordinary observerのspec必須・pendingで停止・全active claim検査は維持する。controllerは別のsource非依存観測を使い、途中でroots.jsonが不在の状態も固定pendingの原objectから判定する。ancestor pendingや入れ子/control subtreeは拒否する。

## 配置と公開順

workspace直下のS=`.norves-assetcook`で、排他作成した`.transaction-stage-<token>`を準備し、完全になったら同IDでS/pendingへno-replace renameする。

- intent.json
- root.stage内のpackage、必要directory閉包、集約manifest
- index.after、state.after、receipt.stage
- 公開途中に原roots.jsonを移すindex.before
- receipt.stageを最後に移すreceipt.commit

各assetは別の空work directoryに全planを先行準備してcook/captureする。既知packageをroot.stageへ同IDで移し、検証済みfragmentと空directoryだけを清掃する。余分なwork file/directoryは拒否して残す。

固定pending公開前にspecの生bytes/ID、全依存、owner、全claim、header/index原object、runtimeとstate finalの不在を再検査する。その後は以下の順に公開する。

1. root.stage→workspace/RootLeaf
2. state.after→S/state-ClaimId.json
3. S/roots.json→pending/index.before
4. pending/index.after→S/roots.json
5. 全後像を再検証し、receipt.stage→receipt.commit
6. 完全commitを検証してpending全体を`.transaction-retired-<token>`へ退役する

全renameは同volume、no-replace、既知ID/byte印/親/実長名を照合する。rename成功直後に公開済みflagを立て、後続観測やclose失敗を未公開扱いに戻さない。manifest/package/state/indexがパスごとに途中状態を見せない保証はない。協調writerはvolume mutex、ordinary observerはpendingで停止する。

## 復旧

source/specを読まず、receipt.stageまたはreceipt.commitの既知ID/bodyで判断する。どちらも不明、両方存在、同bytes別IDならConflictで保持する。

未commitはindex.afterをstageへ戻し、原indexを元へ戻し、state.afterとroot.stageを既知候補へ戻す。before/after原objectをdeleteや再serializeで代用しない。rollback途中で再びprocessが落ちてもfull parserに必要な全原objectを残す。

commit済みは全final後像を照合して確定する。完全commitまたは完全rollback後だけpending全体を退役する。退役直後のprocess終了残物は次回には未知orphanであり、prefix探索で回収しない。固定pending不在になる時点でroot/state/indexは安定している。

今回のprocessが退役前後の同一性を確認したobjectだけはhandleで清掃できる。余分なchild、同bytes別ID、親の差替え、腐敗したside documentは削除や既存root採用の理由にしない。

## 識別と保証の境界

before/after imageとreceiptの保存IDは当該transactionの条件付き復旧用で、恒久的file lineageではない。intent.json/header.json自身の自己IDをwireへ循環して埋め込まない。同じ意味のintent bytesを別IDのfileへ置いた場合、それだけを拒否する永続sealはない。検証済み固定store/pending内で今回読んだintent IDを、そのprocess内の清掃前に再照合する。

stable namespaceと協調writerが前提。認証・悪意あるfilesystem writer対策・powerloss耐久性・atomic-reader可視性は保証しない。flushと実process終了は電源断試験の代わりではない。

## 検証

実textureの新規公開、共通cacheのSkip、sibling claim保持、未知root/child/親差替え/同bytes別ID/壊れたside document、spec/source変更、Busyとout保持を確認する。

公開・receipt・rollback・退役の16境界の実child process終了に、keepalive mutexを残すabandoned再開を追加する。source/spec不在で原indexのbytes/IDを保ってrollbackまたはcommit確定する。Prepared/Retired残物と未知prefix directoryのID/bytes不変も検査する。Release/Close故障注入は実cleanup後の失敗報告であり、実OS故障の再現ではない。

short alias、Unicode物理親のASCII alias、SUBSTは条件flagを残す。0は未実測として扱う。既存32CPU、単体7CLI/79出力byte、texture v1の固定10出力byteと5診断byteを維持し、新しいexecutor試験を含む33CPUを受入条件とする。
