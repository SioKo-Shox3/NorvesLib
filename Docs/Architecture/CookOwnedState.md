# Cook所有stateの値形式（G2-S6 / GR96）

CookOwnedStateは共通CookOutputRecordを保存へ渡すための厳密な値形式。runtime manifestとは独立し、このcodec自体はfileを一切開かない。JSONから得たpathをI/O locatorとして使わず、codec成功を既存fileの採用・上書き許可と解釈しない。

## Scopeと所有

- schema 1、固定producer NorvesLib.AssetCook、32桁lowercase hexの非zero OwnerId、非zero Generationを持つ
- OwnerIdは所有世代の識別用であり、認証・署名・改ざん防止ではない
- RuntimeRootIdentityは現在のcallerが用意するWindows絶対rootのASCII表記、ManifestNameはroot相対名。parse時は現在のbindingと全fieldを厳密一致させる。保存文字列からbindingを復元して照合を省略しない
- RecordsはPrimaryKey（LogicalPath / Kind / Variant）と共通recordを所有する。primaryは最初のoutputのkeyと一致。keyを区切り文字で連結しない
- 所有一覧はrecordのOutputsから得る。複数recordで同じ出力keyや物理packageを共有する方式は、このschemaでは許可しない
- records空は有効な空集合。個々のrecordのoutputs空は不正

codecは現callerのrecordを無損失で保存する層であり、package payloadの実検証や独自のSkip判定を重ねない。formatやcooked versionの旧値を読めても、現要求との一致と実packageの検証はDecideCookCacheで行う。所有stateそのものの未知schema・不整合と、既知recordのcooker revision差を区別する。

## JSON規約

rootのfieldは schema / producer / owner / root / manifest / generation / records。recordは primary / schema / dependency_schema / revision / dependency_hash / outputs。outputは key / source_hash / format / package / entry / entry_type / cooked_hash / cooked_version / package_size / package_hash / skeletal。keyは logical / kind / variant。skeletalはnull、または vertices / indices / joints / clips / submeshes / slotsを全て持つobject。

- uint64値は16桁lowercase hexのstring。FourCCの保存も同じ形だが値域はuint32へ制限。JSON numberのdouble精度を経由しない
- schema・cooked version・数量は非負の整数literalでuint32範囲。指数、小数、負zeroを許可しない
- 全objectで未知・重複・欠落fieldを拒否。文字列はASCII/NUL無し。SourceHashHex等の表示用重複fieldは保存せず、読込時に数値から再生成
- pathは既に正規形であることを要求し、黙ってAssets prefixを除去したりdot/ADS/device名を受理しない
- packageは大小文字aliasとfile/directory prefixを全record横断で拒否。manifest名との衝突も拒否。物理名sortは区切りを先に並べ、a・a!・a/xのような並びでもprefix衝突を見落とさない
- 入力/出力16MiB、4096 records、16384 total outputs、各string4096 bytes、JSON nesting16を上限とする。入力byte/depthはJSON parserより前に検査。record/output/string上限はDOM構築後のため、16MiB内の大量nodeによるメモリ増幅は残る（独立node予算は未実装）
- parse / serializeのfalseでは既存outを保持する。allocation例外は伝播する。成功した結果は元JSONに借用しない

## 保存と公開の境界

fileのMissing / Loaded / Error、新規排他保存、tempの検証とno-replace renameは後続のSTATE-FILEで扱う。stateはゲーム用RuntimeRoot外に置く。missing/cache missは既存出力の上書き許可ではない。productionのrootと外部stateを別々に公開する方式は採用せず、writer lock・journal・before-image・rollbackを備えるtransactionへ接続する。atomicなreader可視性や電源断耐久性はこの形式では保証しない。

## 検証

CookOwnedStateTestでschema全階層・uint64境界・copy/move後の値所有・scope不一致・alias/prefix・過大入力と失敗保持を検証する。CookCacheDecisionTestの全kindの実cook→record採取にcodec往復を挟み、同じDecideCookCacheでSkipへ戻ることを確認する。Windows CPU契約と固定79+10byte gateの実結果は受入れ時に記録する。

af63b209c20b433eece01d7bbbf2b1a3d35bbf35の[Windows run37231239763](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37231239763)で22 CPU契約と固定79+10出力・5診断のbyte互換が成功。全kindの実record→codec→共通Skipも合格した。これは値形式の受入れであり、実file保存・既存root公開の受入れではない。

## Windowsでの新規state file操作

CookStateFileはcaller指定のASCII絶対StatePath / RuntimeRootとExpectedBindingを受け取る。state親とRuntimeRootは既存directoryが必要。未作成runtimeの将来の8.3 aliasは確定できないため、このprimitiveでは不在rootを扱わない。callerは同じOwnerIdを設定等で独立に保持し、読みたいstate自身から採用しない。

- raw表記を正規化前に検査し、reparse・UNC/device・drive-relative・危険名を拒否する
- 親の全成分の存在を確認し、directory handleを保持する。handleのvolume GUID付き正規pathで同volumeと双方向の包含衝突を検査する。drive文字や8.3表記だけで外側と判定しない
- RuntimeRootIdentityはcallerのASCII表記との一致用、handle物理pathは実scope確認用。後者をstateへ保存しない
- Loadはshare=0で最終fileを開き、regular/non-reparse・16MiB以下・完全read/EOF/size不変を確認してからcodecへ渡す。bindingは不在判断より先に検証する
- Missingは全ての親が確認できた状態で最終leafだけがFILE_NOT_FOUNDとなった場合。PATH_NOT_FOUND・sharing/access失敗・directory/reparse・空file・破損JSONはError。Loaded時だけoutを変更する

WriteNewは事前serialize後、自分のsibling tempをCREATE_NEWで排他作成する。GENERIC_READ / GENERIC_WRITE / DELETE、share=0で得た同じhandleへ全write、FlushFileBuffers、seek、readbackのbyte一致とparseを行い、FileRenameInfo（ReplaceIfExists=FALSE）で新規公開する。RootDirectory=NULLと絶対宛先を使い、相対directory-handle renameの動作には依存しない。公開成功直後にcleanupを解除し、その後のclose失敗でも公開済みfileを消さない。

失敗時は取得済みの自分のhandleにだけFileDispositionInfoを設定して閉じる。CREATE_NEWが衝突した他者の名前は所有しない。cleanup自体の失敗はorphan path付きで返し、pathnameによる代替削除や再帰cleanupをしない。未知のtempは採用・削除しない。

通常の同時writerでは排他作成とno-replace renameにより1件だけが成功する。保持handleはsource名のすり替えによる別objectの公開/削除を防ぐが、全祖先のhostileな差し替えや特権操作への耐性は保証しない。安定した親namespaceを前提とする。flush/readback成功は後続renameの電源断耐久性や、production rootとのtransactionを証明しない。

CookStateFileTestはnative成功と、共有lock・junction・既存宛先・公開直前競合・実2thread・大きさ・失敗時保持を検証する。private probeの故障注入は失敗経路とcleanupの検証であり、注入したflush/renameが実OSで成功した証拠にはしない。8.3別名と別volumeの利用可否は試験receiptへ明示する。CookCacheDecisionTestでは全kindの実recordをsave/loadし、同じ共通判断でSkipへ戻す。

参照: [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)、[GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)、[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)、[FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)

state file操作は375a7dc64c585e85a376439de69da36a73591403の[Windows run37234005425](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37234005425)で受入れ済み。23 CPU契約、固定79+10出力と5診断のbyte互換が成功。8.3別名は実際に異なる表記を使用し、別volume拒否も実行した。productionの一括公開・既存state世代の置換・lock/journalの受入れとは区別する。
