# 固定inventoryの既存asset-set更新

対象はG2-S6／GR96のtexture spec v1用controller API。productionの`--asset-set`接続、asset追加・削除、spec v2、並列cookはまだ含めない。

## 境界

`UpdateCookManagedAssetSet`は独立した現在のowner設定と解析時spec bytesを受け、既存claim・root ID・旧state bindingを確認する。`NoChange`と`Updated`だけoutを更新する。pendingを残す失敗は`NeedsRecovery`、commit/退役後の解除失敗は`CommittedButError`。未知rootを採用しない。

BootstrapとUpdateは準備を別実装にし、native transactionの公開・分類・復旧・退役はprivateな`CookManagedTransactionController`に置く。共通contextにsource要求やcook判断を持たせない。既存Bootstrap API、固定slot、wire、probeの先頭14値は保持する。

## 増分判断

共通`PrepareCookOutputPlan`と`BuildCookManagedUpdateInventory`で旧primary/output key・package名との固定対応を作る。判断は`DecideCookCache`だけを使う。manifestが不在・破損なら未読込として共通判断へ渡す。

- 全資産がSkipかつ、要求順で従来serializerが作るmanifestと現物の生bytesが等しい場合はNoChange
- NoChangeはstage作成、乱数ID採取、世代上限検査より先。index/stateがUINT64_MAXでも読み取り専用で成立する
- 全Skipでもmanifestのbytesが違う場合はmanifest-only mutation。packageをcook/置換せず、state/indexの世代を1進める
- Cook対象だけ別々の空stageでcook/capture。Skipの旧record・packageは保持する
- source、spec、旧control、manifest、Cook対象の実before imageを公開直前にも再照合する

## 更新前の像

beforeは旧stateの期待hashではなく、今ある実fileのID・size・全bytes hash、または最終leafの不在。欠落・壊れたpackageを正常な再cookで修復できる。親directoryは必ず既存で、rootと相対親をnativeで解決し、記録した親IDを確認する。親を作り直して所有を取り戻さない。

このID条件は当該transactionの復旧にだけ使う。過去のstateへ恒久file lineageを追加しない。unlisted fileは所有対象にも清掃対象にもせず、root全体を交換しない。

## 固定pending

- intent.json
- index.before / index.after
- state.before / state.after
- manifest.before / manifest.after
- package.before-00000000 / package.after-00000000（検証済みmutation配列ordinalの8桁小文字hex）
- receipt.stage / receipt.commit

元のbeforeは公開時にfinalからbackupへ、afterはstageからfinalへno-replace renameする。source、保存絶対locator、任意slot名を復旧I/Oへ戻さない。

公開順は変更package、manifest、state、index、全後像の再検査、receipt commit、terminal確認、pending退役。rootとclaimは変えない。receipt以前の復旧は逆順でindex/state/manifest/packageを元へ戻す。after原objectもstageへ戻して保持するので、rollback自体が中断しても再分類できる。

## source非依存復旧

live store/header/pendingへenvelopeを束縛し、固定index/state原objectのbytes/IDを検証してからrootとmanifestの相対hintを公開する。manifest.afterを照合してfull intent検査を終えた後だけ、packageの相対pathと固定ordinal slotを扱う。

final/before/afterの各候補は、期待原objectがそれぞれちょうど1つあることを検査する。finalが一時的に不在でもbackupとafterが揃えば復旧できる。同bytesでも別ID、親置換、未知slot、破損side document、必要原object欠落はConflictで保持する。

Updateの復旧は変更対象だけを検査する。Bootstrapの新規root用tree走査はUpdate final rootに使わない。完全commitまたは完全rollback確認後だけpendingを退役し、そのprocessが確認した既知objectだけを清掃する。後日の未知stage/retired prefixを探索・採用・削除しない。

## 保証しないものと上限

協調writerと安定したnamespaceが前提。認証、悪意ある非協調writer、電源断耐久性、readerから複数fileが同時に切り替わる可視性は保証しない。既存のvolume mutexとpending観測を使う。Global名のcross-session実測は引き続き未実施。

既存4096資産、16384出力、32MiB intent/metadata、16MiB state/manifest等の制限を維持する。個々のpackage読取は512MiBまで。packageの全payloadは分類後に解放し、全変更資産分を保持しない。ただしdirectoryの反復列挙費用は最大inventoryで未計測であり、今回の小規模fixtureを大規模性能保証としない。

## 検証予定

3資産の2Cook＋1Skipで、16公開境界・12rollback境界・abandonedの合計29実child終了とsource/spec不在復旧を検証する。NoChangeの無変更/世代上限、manifest-only、欠落/破損package/manifest、親不在、途中入力変更、別ID/未知entry/親置換/side document破損、Busy、解除故障、root/Skip/unlisted/sibling/orphanのID・bytes保持を確認する。

既存Bootstrapの17実中断と34CPUを維持し、専用試験を追加した35CPU、7単体CLI、79＋10 frozen出力の直接byte一致、5診断を受入条件にする。本文作成時点では新しい実Windows gateは未実行。
