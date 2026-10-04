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
