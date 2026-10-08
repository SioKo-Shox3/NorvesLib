# Cookの出力計画とstage捕捉（G2-S6 / GR96）

CookPreparedPlanは、単体cookと同じPrepare / BuildInventory / dependency collectorが作る読み取り専用の観測を値所有する。命名や増分判定の別実装は置かない。Skipの決定、所有権の取得、公開の許可は行わない。

## 出力一覧

PrepareCookOutputPlanは正規化済みCookDecisionContextと、ExpectedIdentity / TargetPathの組を返す。primaryが先で、参照された内包画像はImageIndex順。外部画像は依存に含むが追加packageにはしない。ExpectedIdentityはcook前に分かるkey・source hash・format・package相対名・entryの照合用で、CookedHashや骨格数量の実測ではない。ContextのReasonはForcedを格納するだけで、cache判定の結果として使わない。

失敗時のoutは保持する。計画のためにsource・外部依存・選択sidecarを読むが、runtime / stage / state / temp / journalは作成しない。mtimeや内容を変更する処理も行わない（readによるaccess timeの扱いはfilesystem側の規則による）。

## finalからstageへの写し替え

PrepareCookStagingPlanは、callerが排他所有している空の既存stage directoryを受け取る。final manifestの親を基準rootとして、packageの相対名とmanifestのfilenameをそのままstageへ写す。入力locatorと外部fileの基準、logical/kind/variant/format/entry、sidecar選択と骨格policyは変更しない。

- stageはWindows local-driveのASCII絶対directory。reparseを通さない。final / stageともdrive root直下そのもの（C:\等）は対象外
- 既存directoryはvolume GUID付きhandle-final pathで解決し、8.3表記の違いを跨いでfinalとの包含を拒否する
- final rootの不在部分は存在する祖先からの観測として扱う。stageは必ず実在し、finalとの同一・親子関係を許さない
- 同じ始点の依存snapshotと現在の再採取を照合する。新しく得たstage側snapshotで古い始点を置き換えない
- 骨格policy等の意味も既存dependency fingerprintに含まれる。非暗号学的な増分印であり、改ざん防止tokenではない

空directoryの所有はcallerの責任であり、この関数が既存directoryを採用するわけではない。stageを作るproducer、他assetとの衝突確認、同volumeでの公開、排他writer lockは後続の別境界。

## 実cook後の捕捉

callerが返されたstage要求をcookしたあと、CaptureStagedCookOutputRecordへfinal plan、stage plan、stage manifest fragmentを渡す。両planを再検証し、root変更以外の意味・全package相対名・始点依存を照合する。fragmentのkey/package identityと件数は期待一覧と完全一致が必要で、無関係な追加rowも許さない。

実packageの型・hash・全体印とrecordの採取は既存CaptureCookOutputRecordへ委ねる。最後にもfinal始点の依存を再確認し、成功時だけout recordを置換する。recordには相対package名だけが入り、stage locatorは保存stateに入らない。

新しい入力をstageで正常にcookできても、元のfinal planから入力・外部buffer/image・sidecarが変わっていれば失敗する。raw sidecarの空白だけが変わりruntime source hashが同じ場合も、依存snapshotの差として止める。

## 保証の境界

planは公開permitではない。final manifest / 所有state / namespaceが準備後に変わる可能性があり、将来のpublisherがwriter lockの下で再検証する必要がある。hostileなnamespace変更やABA入力競合、複数fileのatomic snapshotは保証しない。

asset集合を跨ぐkey・physical alias・依存/spec/control locatorの衝突検査はOUTPUT-SET-GUARDで別途扱う。両者とjournal / before-image / rollbackが揃うまでproduction batchの既存root受理や書込経路は変更しない。旧standalone ModelCookCacheの移行も未実施。

## 検証

CookOutputPlanTestは不在/既存final、非包含・空stage・8.3別表記・reparse拒否、別の有効要求やrevision、入力を変更して正常recookした場合、外部依存、意味が同じraw sidecarの差、余分なfragmentと失敗保持を検証する。CookCacheDecisionTestの全kind実cookに同じplan→stage cook→captureを追加し、一覧とpackage印の一致を照合する。Windows CPUと固定79+10byte gateの結果は受入れ時に記録する。

62e8c81ea0b61a3d367bddc6538a5deed8cde998の[Windows run37238379793](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37238379793)で24 CPU契約と固定79+10出力・5診断のbyte互換が成功。元snapshotと異なる入力を正常recookした場合、意味が同じsidecarの生byte差、外部依存変更、余分fragmentも拒否できた。8.3の異なる表記を使った包含拒否も実行済み。集合横断guardや公開transactionの受入れとは区別する。
