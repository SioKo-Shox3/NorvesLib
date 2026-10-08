# 管理資産更新の intent 値契約

対象: G2-S6／GR96。実装は AssetCook の純値層であり、production CLI、公開 executor、journal 復旧にはまだ接続しない。

## 保存内容と権限

- Bootstrap は既知 staged root と必要 directory 閉包、全 package と集約 manifest を対象にする
- Update は既存 root の ID と固定 key/package 集合を維持し、Cook 対象の全派生出力だけを変更する。Skip record は値を保持し、unlisted file を含めない
- owner/root/manifest binding は独立に渡す比較値。保存した絶対 root 文字列から I/O を行わない
- 値 factory の成功は filesystem の所有、現在性、書込許可、認証を意味しない。controller は同じ volume lock 内で live header・claim・親 directory・file type・link・alias・ID・bytes と依存を再確認する
- 同bytesでも別IDの after-image は復旧対象にしない。これは当該 transaction の conditional rollback の制約であり、通常再cookの恒久的なfile lineage制約ではない
- 既存 package の破損・最終 leaf 不在は修復可能。before の実bytes印を旧state印と同一であることまでは要求しない。初期 update は欠落親directoryを新規作成しない

## 二段読込

1. `ParseCookManagedTransactionEnvelope` は独立 store anchor と32MiB以下の strict wire を照合する。未知/重複 field、escape、未知mode、ID重複、scope、件数とmetadata予算を検査する
2. envelope が公開する読取要求は固定4 role（IndexBefore／IndexAfter／StateBefore／StateAfter）、claim token と after manifest imageのみ。任意のpackage pathはまだ公開しない
3. `ParseCookManagedTransactionIntent` へ実際に読んだ bounded side bytes と observed ID を渡す。exact bytes の size/hash/ID を検査し、共有index/state codecと既存 AssetManifest readerへ渡す
4. full intent は全意味検査後だけ値所有される。source/specがなくてもこの読込は可能。ただし復旧controllerのlive所有照合は別途必須で、新cookの許可にはならない

before control は固定 final または固定 backup、after control は固定 stage または final の既知object候補から読む想定。保存文字列をlocatorに変換せず、roleと安全なclaim tokenからslotを導出する。stage/backupを実際に作る処理は本単位にない。

## 世代・固定集合

- 実変更は index generation をちょうど+1。上限からの加算は禁止
- Bootstrap は state generation=1、旧indexの全claimを保持し、新claimを1つだけ追加
- Update は state generationをちょうど+1、root/claim/他rootの登録を保持
- stateのrecord/output順変更は許すが、primary/output key・package名の追加、削除、変更は拒否
- 1assetの派生出力は全て変更または全てSkip。Skipは共通record値を保持する
- NoChangeはcontrollerが共通cache判断で処理し、intent/after objectを作らず世代を維持する。このcodecへNoChange判定を複製しない
- live factoryは共通prepare/capture/Skip由来の値と新state、manifest全referenceを照合する。file I/Oや依存の再採取はしない

## receiptとbefore-image

receipt body は schema、transaction/store/claim、root scope/ID、提案世代から先に作る。intent digestを含めないため循環しない。作成済みreceiptのID・size・hashをintentへ入れ、parserが同じbodyを生成して検査する。署名や改ざん防止ではない。

before state/indexの意味検査に再serializeしたbytesを代用しない。side documentsはexact raw bytesで照合する。共有serializerを使うのは新after documentの作成とlive値検査だけ。

公開executorは未実装。将来、known beforeをID保持でbackupへ移し、既知afterだけを公開し、最後に既知receiptをcommit slotへ移す。同IDでないもの、未知entry、receipt不明、親の置換は自動削除や採用の理由にしない。

## 上限と検証範囲

- intent wire/保持metadata 32MiB、record4096、package16384、bootstrap tree65536
- state各16MiB、index各1MiB、manifest16MiB、receipt16KiB
- string4096byte、root leaf255byte、JSON深さ8・事前token200万
- これらは全heap上限ではない。side bytesをwireへinlineしない

共有index codecは従来observerの全祖先合計4096claim予算を保持する。initializerの空index bytesは専用fixtureで固定する。共通reference/fixed-key比較をcacheとinventoryから抽出して同じ意味を再利用する。

純値試験はbootstrap/update、固定集合、Cook/Skip、世代、同bytes別ID、型/未知field、失敗時out保持、入力寿命を反証する。全kindの実cook/capture試験では実native ID・生side bytes・source不在での読込を接続する。公開・rollback・実process中断・powerloss・GPUはこの値単位の合格範囲に含めない。

## 実Windows受入れ

af3609a7f9ff1871b966ad2252f2e7604095985b、[run 37272790699](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37272790699) attempt 1で実build・32CPU・5診断byte・既存7CLI/79出力byteとtexture spec v1の固定10出力byte一致を確認した。native textureは2spec×2実行・16拒否も成功。2回目の一致はrunner記録、直接比較は保存された初回10出力による。

15個の実cook fixture（raw/custom/empty、srgb/linear/rg/r、Unicode texture、audio、mesh、skeletal、sidecar有効/無効、Unicode model、外部依存）から実native IDsと生side bytesを使うupdate/bootstrapを検証した。source不在parse、250段の共有prefixと2000末端directory/package、移動元wrapperの無効化、失敗時out保持も通った。共有helper抽出73項目、71 immutable source receipts、3ZIPのAPI digest/size・CRC・安全な完全inventoryとCNG importを独立検証し再実行した。

既存initializerの8条件flagとobserverの5条件flagは全て1。lockのcross-sessionは従来どおり未実測。実資産の公開executor、中断rollback、production CLI接続の合格ではない。
