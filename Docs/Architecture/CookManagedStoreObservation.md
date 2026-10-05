# 管理領域の読み取り専用観測

## 目的と範囲

G2-S6／GR96の管理済み更新に先立ち、ownerやspecの変更で中断した更新の探索先が変わらないようにする。ObserveCookManagedStoreは既存destination-volume mutexを取得し、独立owner解決、固定管理領域と物理祖先の観測を行う。fileの生成・更新・削除、初期化、state採用、journal replay、production CLIへの接続は行わない。

StoreMissing/Observedだけ値所有のoutを更新する。Conflict/NeedsRecovery/Busy/Errorはoutを保持する。errorはrequest/out内文字列とは独立に渡す。成功は現在の観測結果であり、filesystem所有、Skip、書込capability、回復完了、認証を意味しない。最終的な更新controllerは同じlockを保持したまま再観測と実state照合を行う必要がある。現wrapperへの同thread再入は既存lockが拒否する。

## 固定配置と重なりの規則

RuntimeRootの物理直親がworkspace W、W直下の.norves-assetcookがstore S。新しいmanaged rootはWの直接の子だけとする。物理volume rootをWにすること、S自身またはcontrol subtreeをruntime/workspaceとして使うことは拒否する。SUBSTのdrive rootが通常の物理directoryを指す場合は、物理直親で判定する。

state/stage/backup/receiptは将来S内の固定slotへ閉じる。任意external StatePathをproduction配置として採用しない。既存の汎用StateFile primitiveの契約を変更するものではない。

- 同じWのsibling rootsは共存できる
- 物理祖先storeにあるactive rootがWを含めば入れ子として拒否する
- 祖先storeにpendingがあれば、root leafが無関係に見えてもNeedsRecoveryで止める
- 先にdescendant storeができていた場合、そのancestor directoryは既存unknown rootなので後からbootstrap採用しない
- 別探索経路のdisjoint storesは共存できる。同volume lockの直列化は維持する
- この探索を全volumeのjournal回復と表現しない

stable namespaceと協調writerが前提。非協調processによる改変、file ID再利用に対する認証、powerloss耐久性、atomic reader snapshotは保証しない。

## 物理探索とI/O境界

callerの検査済みlocal-drive locatorからworkspaceをhandleで観測する。GetFinalPathNameByHandleWのGUID形式から実物理祖先をvolume rootまで辿る。ここでだけ、handle由来のcanonical ancestorを固定control entryの読み取りに使う。保存された絶対pathやcanonical文字列をその入口に渡さない。

各物理祖先の直下をFindExInfoStandardで列挙し、長名とshort aliasを検査する。固定名のordinal case-insensitive一致が複数、実長名のcase違い、通常file、reparseはConflict。exact lowercaseの実長名1個だけを認識する。header.json、roots.json、pendingにも同じ固定名規則を使う。列挙失敗・上限超過を「storeなし」としない。

候補はhandleで再観測し、non-reparse/type、volume、物理直親、長名、directory IDを照合する。既存rootがstoreと同じID、またはworkspace祖先ID列にstore IDがあれば別locatorでも拒否する。directory IDはvolume serialと組み合わせ、認証とは扱わない。

## schema v1

header.jsonは次のfieldだけを認める。未知/重複/missing fieldを拒否する。

- producer: NorvesLib.AssetCook
- schema: 整数1
- store_id: 非zero小文字hex32
- volume_guid: 既存lockがmount managerで正規化したGUID token
- volume_serial: 小文字hex16
- workspace_id、store_directory_id: FILE_ID_128のbyte順を保つ非zero小文字hex32

roots.json:

- schema: 整数1
- store_id: headerと完全一致
- generation: 非zero uint64、小文字hex16
- roots: claim配列
  - claim_id、owner_id: 非zero小文字hex32
  - leaf: ASCIIの安全なsingle component。separator/ADS/device名/末尾dot・space/control予約名を拒否
  - directory_id: FILE_ID_128のbyte順を保つ非zero小文字hex32

state slotは将来claim_idから導出するため、保存path fieldを持たない。claim_id、directory_id、ASCII case-fold leafの重複をsortしたindexで拒否し、record順は保持する。

headerを独立物理identityと照合した後、fixed pendingが存在すればindexの安定状態を仮定せず停止する。pendingの内容や保存locatorは読まず、replayもしない。壊れたpendingや通常fileでも勝手に削除しない。

pendingがなければ発見した各storeの全active claimを検証する。保存leafは実長名の列挙entryとexact一致させ、実際に開くpathは現在のworkspaceとそのentryから導出する。欠落、rename、別directoryへの置換、type/ID差、観測不能はstore全体の停止理由であり、祖先ID列に一致しないから無視することはない。現在の既存rootは同workspace storeのclaimとownerに一致しなければ採用しない。

control fileはread共有だけで開き、単一linkのregular file、長名/親/ID/sizeを確認して読み戻す。最終owner/workspace/rootも再照合する。stable namespace前提の観測であり、観測終了後の有効性を持ち越さない。

## 上限

物理祖先256、1directoryのentry16384、全列挙entry65536、全store合計4096 roots。header16KiB、index1MiB、JSON深さ8、字句区切り等の予算100000。観測する名前・祖先path・読み込むJSONの延べmetadata32MiBを上限とする。32MiBはDOM/container/値コピーを含むプロセス全heap上限ではない。package payloadは読まない。

## 検証と後続

専用Windows試験は独立OS identityからschema fixtureを作り、sibling/disjoint、両順の入れ子、owner/spec/manifest変更とpending、欠落/置換claim、保存path罠、別store header、型/case/共有拒否/サイズ、4096件、alias/SUBST/Unicode親、case-sensitive/reparseの実条件を検査する。無変更確認はfixtureの全entry・file byte hash/size・native handle由来write timeとvolume/file IDの前後比較。子を作成した直後のdirectory時刻は列挙cacheが古い場合があるため、directory_entryのcached timeを使わずFileBasicInfoを照合する。条件付きtestのflagが0なら未実測として記録する。

未完はfresh store初期化、root claim登録、完全intentとreceipt、before/after image、条件付きrollback、root/state/indexの一体公開。既存production new-root-only経路は変更しない。新規store作成後には将来short aliasやtunnelingを予測せず、再観測が必須となる。

## API根拠

- [FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw): 長短名を含む列挙、属性の再観測、FindCloseでの終了
- [GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew): handleからのnormalized/GUID path取得

## 実Windows受入れ

759267420b55b0acbaf0a0c98d6a08de01e997e9、[run 37260128285](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37260128285) attempt 1で実build、30CPU、既存7CLIの79出力byte一致、5診断byte一致、texture spec v1の2spec×2実行・固定10出力byte一致・16拒否が成功した。3成果物ZIPのAPI digest/size、CRC、安全な完全inventoryを照合し、独立比較を再実行した。2回目のtexture出力一致はrunner記録、独立比較は保存した初回10出力による。

storeのshort alias、SUBST、Unicode物理親へのASCII alias、case-sensitive directory、reparseは各条件flagが1。専用testでは4096実root claim、固定pending、全active claimの欠落/置換、別owner・入れ子・unknown root/store拒否、snapshotと値寿命を確認した。古いdirectory列挙時刻で失敗した2runの証拠を残し、比較条件を減らさずnative handleのwrite timeとvolume/file IDへ切り替えた。production実装は初回公開から不変であり、store初期化・journal replay・実更新の受入れは含まない。
