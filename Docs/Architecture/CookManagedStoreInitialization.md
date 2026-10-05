# fresh管理領域の初期化

## 対象

G2-S6／GR96のInitializeNewCookManagedStoreは、runtime直親workspaceに新しい.norves-assetcookだけを作る。runtime root、claim、state、package、pending、journalは作らない。既存のproduction --asset-set経路へは接続しない。

既存のvolume mutexを1回取得し、observerと共通のprivate ObserveLockedを使う。初回StoreMissingかつruntime不在の場合だけ作成する。ObservedはStoreExists、pending・unknown root/store・入れ子等は既存の拒否規則のまま返す。storeの登録を持つことは認証でも資産所有の証明でもない。

Createdだけoutを更新する。StoreExists／NeedsRecovery／Conflict／Busy／Error／PublishedButErrorではoutを保持し、診断を返す。errorはrequest/outと独立に渡す。PublishedButErrorは公開済みの可能性という曖昧な成功扱いではなく、この呼出しがrename成功を確認した後の失敗である。公開済みstoreを削除したり自動再試行したりせず、明示的に再観測する。

## 手順

1. 共通観測でroot/owner/物理祖先を確認し、workspace handleとvolume/file IDを保持する
2. Windows CNGのsystem-preferred RNGで非zero 128bit StoreIdを作る。lowercase hex32の識別子でありcredentialではない。時刻/PID等へのfallbackはない
3. workspace直下の.assetcook-store-stage-＋random hex32をCreateDirectoryで新規作成する。既存衝突は触らず、最大32候補まで。成功後にDELETE＋READ_ATTRIBUTESのdirectory handleを保持し、type/reparse/volume/直親/実長名/非zero IDを照合する
4. stageの作成でroot候補の8.3 aliasや存在状態が変わっていないか、同lock内で全観測を再実行する
5. header.jsonとroots.jsonをCREATE_NEW、共有0、read/write/delete handleで作る。headerにworkspaceとstageの実ID、StoreId、正規化volume GUIDを入れ、indexはgeneration1・空rootsにする
6. 各fileをwrite（部分writeを処理、zero進捗拒否）→flush→seek→size/EOFを含むreadback→byte一致→共通Header/Index parserで検証する。regular/non-reparse/単一link、親/長名/IDも確認する
7. stageを列挙し、既知のheader.jsonとroots.jsonだけであることを確認する。child/Find handleを閉じてから、StoreMissingとowner/root/workspaceの不変を再観測する
8. stageの同じhandleへFileRenameInfoを渡す。ReplaceIfExists=FALSE、RootDirectoryは保持するworkspace handle、FileNameは固定leafのみ。同volumeのrename以外へfallbackしない
9. rename成功直後にpublishedを記録する。同じhandleでvolume/ID保持、新しい固定長名、同じ直親を照合する
10. DELETE handleを閉じてから全観測を再実行し、Observed、今回のStoreId/両directory ID、generation1・空roots、元のroot/ownerが一致するときだけCreated候補にする。mutex解除まで成功して初めてoutへ渡す

公開後のobserverはDELETE共有を許さないため、stage handleを残したまま呼ばない。共有規約を緩めて回避することもしない。作成前の将来alias/tunnelingを予測せず、stage作成後とstore公開後の両方で再観測する。

## abortとorphan

公開前は、今回作成し、所有を確認でき、開いたままのchild handleだけにdispositionを設定して閉じる。空の既知stageも同じdirectory handleで削除する。remove_all、名前だけのDeleteFile/RemoveDirectory、prefixからの所有推定は使わない。

この初期profileでは、一度閉じたchildをcleanupのために再openしない。child close後の失敗ではcomplete stageをorphanとして残す。CreateDirectory後にhandle/IDを得られなかったstage、未知child、cleanup/close失敗も診断して残す。次回はそのorphanを採用・掃除せず、新しいstageを用意する。正常終了ではorphanは残らない。失敗時の残物は小さいcontrol fileだけで、package payloadではない。

rename後はidentity検査、close、再観測、例外、mutex解除のどこで失敗してもPublishedButErrorでstoreを保持する。faultのChildClose/AfterPublishClose/MutexReleaseは実cleanupを行った後の失敗報告注入であり、OSが本当にclose/releaseに失敗した実験とは区別する。

stable namespaceと協調writerが前提。既定ACLの継承以外にACL/privilege/system設定を変更しない。file flushとprocess終了試験はpowerloss耐久性やatomic reader可視性の保証ではない。

## 検証

専用Windows試験で独立native IDとheader byteを照合し、stage→storeのID維持、gen1/空index、runtime/state/package/pending不生成、既存store無変更、unknown root/ancestor/pending拒否、既存stage衝突、未知child保持、各故障とPublishedButErrorを確認する。

専用子processをstage作成、header完了、index完了、child close、rename直前、rename直後に終了させる。公開前はorphan不採用、公開後は完全storeが通常観測されることを検査する。8.3/SUBST/Unicode物理親/case-sensitive/reparseと、stage/fixed-storeによる新aliasは条件flagを残し、0は未実測とする。alias割当てそのものは仮定しない。

既存observerの30CPUと79＋10固定出力byte、5診断byteを維持し、initializerを含む31CPUを受入条件とする。production更新、journal/rollback、既存root管理付き更新の受入れではない。

## API根拠

- [BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)
- [FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)
- [renameの権限・同volume・open childの制約](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)
