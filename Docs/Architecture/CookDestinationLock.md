# Cook destination-volumeの協調排他

G2-S6 / GR96。WithCookDestinationLockはFINAL runtime rootが属する物理volumeの協調writerを直列化する。初期profileは並列性より安全側を選び、同volumeの親子・sibling・case違い・作成前後のrootを同じlockで扱う。CLI/publicationへは未接続。

## 同期callbackの境界

requestはFinalRuntimeRootだけ。native I/O locator、exact canonical root identity、canonical volume GUID、root存在状態、abandoned状態をcallbackへ渡す。HANDLEと解除権限は渡さない。

結果はExecuted / Busy / Error。Executedはcallbackがtrueを返し、ReleaseMutexとCloseHandleが成功したことだけを表す。stateの所有権やtransaction commit、readerのatomic可視性を意味しない。Errorでもcallbackが既に実行された場合があり、その副作用のrollbackを保証しない。

callbackは同期であり、開始した非同期処理はreturn前にjoinする。同一threadのnested invocationは別rootでも拒否する。null callback、不正locator、観測不能ではcallbackを呼ばない。callbackのfalse・std例外・その他の例外もcleanupを通し、Errorを返す。実cleanup失敗は成功として返さず、上位は自動再試行せず停止・診断する必要がある。

## volumeだけで名前を決める

rootのprofileはowner resolverと同じ。ASCII local-drive caller locator、既存non-reparse directoryまたは既存直親下の不在leafに限定する。既存directory observerを共有し、rootは作成しない。

handle由来canonical rootからvolume GUID rootだけを取り出し、GetVolumeNameForVolumeMountPointWでmount manager cacheの最初のGUIDへ統一する。1 volumeに複数のGUID名があり得るため、観測されたtokenをそのまま別lockへ分けない。このqueryはcontrol identity解決に限り、canonical rootをasset/state I/Oへ戻さない。

GUIDはstrictに解析し、ASCII hexだけlowercaseへ揃える。mutex名は次で固定する。

Global\NorvesLib.AssetCook.DestinationVolumeLock.v1.<36文字GUID>

hash、owner、spec、SourceRoot、manifest、StatePath、root leaf、存在状態、file ID、generation、user/session/process IDは名前へ入れない。取得後にrootとvolumeを再観測し、volumeが変われば解除してError。同volumeでのrootの最新観測をcallbackへ渡す。owner bindingはcallback内で独立に再解決し、仮rootの作成後のexact identity照合も省略できない。

## Win32操作

CreateMutexExWは初期owner flagなし、SYNCHRONIZE | MUTEX_MODIFY_STATEのaccess mask、default DACL、非継承handleを使う。MUTEX_MODIFY_STATEは公式にはreservedであり、ReleaseMutex必須権限とは説明しない。ACL変更やprivilege有効化はしない。

WaitForSingleObjectのtimeoutは0。WAIT_TIMEOUTではBusyを即返し、callbackを呼ばない。access deniedや同名の別種kernel object、その他の失敗はError。Local namespace、drive文字、lock fileなどへfallbackしない。Global namespaceにより別process/sessionも同じ名前へ到達するが、default DACLで開けなければfail closedとなる。

## Abandonedは永続crash印ではない

WAIT_ABANDONEDもmutexの所有権取得済みであり、bAbandoned=trueをcallbackへ明示する。上位はlockを保持したままjournal/stateとrecoveryを確認し、その完了前にCook/Skipや上書きを判断してはならない。

最後のhandleが消えるとmutex object自体が破棄される。crash時にkeepaliveがなければ次回取得はordinaryになり得る。このためbAbandoned=falseでもjournal検査は必須。本unitは永続journal、rollback、powerloss耐久性、既存出力の自動採用を実装しない。

## 検証

CookDestinationLockTestは実thread/processのBusy、同volumeの親子/sibling/case/作成前後、callback失敗/例外/再入、別threadからの再取得、実process終了後のAbandoned、全handle消滅後のordinary、同名event衝突、無関係fileの保持を扱う。子processは同じtest bundleを明示選択し、bundleが--test引数を除去する規約に合わせて専用引数を読む。

8.3/SUBST/ASCII alias経由Unicode/別volumeのflagをreceiptへ記録する。cross-processは必須。cross-sessionの実試験はこのrunnerでは行わず、設計上のGlobal namespaceと区別してflag 0を出す。private faultはCreate/Wait/Reobserve/volume差/Release/Closeを検査する。Release/Close注入でも実cleanupは行い、実OSの失敗を再現したとは扱わない。

協調protocolを使わない旧CLIや外部編集、敵対的namespace変更は排除できない。mounted-volume namespaceが安定していることを前提にし、途中のremountやmount-manager構成変更は支持しない。同volumeの排他は所有stateの包含衝突やunknown fileの上書きを許可するものではない。protocol名version変更時は旧writerとの併存を別途扱う。

## 一次仕様

- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumenameforvolumemountpointw
- https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexexw
- https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
- https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasemutex
- https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces
