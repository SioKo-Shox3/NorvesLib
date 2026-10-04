# Cook出力集合の衝突検査

G2-S6 / GR96 の読み取り専用境界。ValidateCookOutputSet は FINAL の CookPreparedPlan と、spec・state・journal 等の予約するファイルパスを受け取る。所有権の取得、stage作成、ファイル公開、CLI呼出経路の変更は行わない。

## 判定の正本

各planを共通CookCacheDecisionのPrepareで再構築し、現在の出力inventoryと依存を採取する。公開planに含まれる依存Filesの編集や削除は検査対象を減らさない。入力内容・設定・revisionが元の観測から変わっていれば拒否する。全集合の検査後にもStableを実行する。

出力のキーはKind / LogicalPath / Variantの構造化した組として比較する。primaryと派生画像、異なるassetの間でも重複を拒否する。文字列連結による擬似キーは使わない。

## 物理パスと保護範囲

- 集合全体のmanifestは同じ物理endpointを1件として扱う。存在状態が一致し、既存fileではvolume/file IDも一致する必要がある。不在同士ではcanonical native表記の完全一致を要求し、case同値だけでは統合しない
- 出力同士、および出力と入力・spec・controlの一致とfile/directory prefixを拒否する
- 依存にはsource、外部buffer、使用していないものを含む外部image、sidecarの存在または不在を含む
- 同一入力を複数assetが参照すること、および入力同士のhardlinkは許可する
- 出力に複数hardlinkがある場合、集合に列挙されない相手も保護するため拒否する
- controlはファイルendpointを予約する。既存directoryそのものの予約ではない

CookPathIdentityはWindowsのlocal drive絶対パスを扱う。Unicodeをnarrowへ変換せず、componentを検査する。reparse、ADS、device名、末尾dot/space、重複separator、directory leaf、確認不能なidentityは拒否する。

存在するleaf、または不在leafの最深既存parentをhandleで開き、volume GUID付きfinal nameを取得する。不在部分はcomponentとして付加する。既存fileはvolume identityと128-bit file IDでも比較する。既存8.3 alias等の異なる表記が同じendpointを指す場合を検出する。比較はcase-insensitiveであり、case-sensitive directoryでも安全側に過剰拒否する場合がある。比較用canonical pathを実際の書込locatorへ置き換えてはならない。

不在fileの将来の8.3 alias割当は予測しない。これは観測時点の検査であり、協調writerのlockと公開直前の再検証を置き換えない。外部からのnamespace変更に対するatomic snapshot、所有権の認証、公開許可は提供しない。

## 上限と計算量

- plan: 4096
- package出力: 16384
- 依存とcontrolの延べ出現数: 65536
- 展開したlocator・比較path・component表・logical keyの集計: 32 MiB
- 1 locator: 32767 native code units / 256 components

32 MiBは処理全体のメモリ上限ではない。元plan、Prepareの一時メモリ、source bytes、索引等は別である。上限超過はエラーとなり、部分的な成功にしない。

集合検査では同じ正規化locatorをまとめてidentityを1回観測し、キー・component列・file IDをsortして判定する。prefixは祖先stackで検査し、a / a! / a/xの順番で見落とさない。統計は集約passだけを数え、既存PrepareとStableによる読込・queryは含めない。現行profileは1assetあたり最大4出力（static primaryと画像3件）、他は1出力である。将来この上限を拡張するときは既存の個別guardも含めて計算量を再評価する。

## 検証境界

CookOutputSetGuardTestは重複key、primary/derived、prefixの全順列、source/spec/control、外部buffer/imageと不在・存在sidecar、8.3、hardlink、Unicode、reparse、lock、観測後変更、4096planとprotected count上限、metadata予算を扱う。CookCacheDecisionTestの各実profileでも集合検査を呼ぶ。実行結果と未実行の項目はPROGRESS.mdに記録する。
