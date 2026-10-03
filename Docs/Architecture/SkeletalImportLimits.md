# 骨格取り込みの制約とGR86方針

2026-10-03にG2-S4推奨Aが承認された。決定の全文とv1 rest pose安全契約は
[G2ImportDecisions.md](G2ImportDecisions.md) を参照。

| 制約 | 既定 | 明示指定時/後段 | 現在の状態 |
|---|---|---|---|
| 頂点影響数 | 4本、追加セットを専用statusで拒否 | cookで上位4本へ縮約・正規化・報告 | 追加セット拒否を実装、縮約は未実装 |
| CUBICSPLINE | 拒否 | 誤差制限付きLINEAR焼込 | 未実装 |
| morph | 拒否 | dropと数量報告 | 未実装 |
| sparse | 拒否 | 変更なし | 既存拒否を維持 |
| 関節数 | 現行/0.2は128 | v1で256 | 128共有定数化済み、256はStage B |

縮約は同一関節をまとめ、重み降順・同値は関節番号順で決定的に選ぶ。
負値・不正なjoint index・仕様外の重み総和を縮約で正当化しない。
脱落量/再正規化数と最大・平均を報告し、許容超過では結果を公開しない。
WarnDroppedWeight=0.01 / FailDroppedWeight=0.25を計画上の初期値としてパラメータ化し、
境界条件は実装時の試験で固定する。CUBICSPLINEの並進/回転/scale誤差も別パラメータとする。

実装順: Strict追加セット検出 → options/reportと縮約kernel → decode/cook接続 → morph drop → cubic焼込。
関節256化はNVSKEL v1の形式とloader変更まで行わない。未実装のoptionは受け付けない。

## Strictの追加セット検査

JOINTS_n / WEIGHTS_nのn>=1は、片方だけ・値がnull・追加重みが全部0でもInfluenceLimitExceededで拒否する。
既存のstatus番号は動かさず、末尾へ15として追加する。基底JOINTS_0/WEIGHTS_0の既存4影響検証は維持する。
予約prefixを持つ不正名（負値・先頭ゼロの非正準表記・非数字・u32範囲外）はInvalidAccessor。
`_JOINTS_1`などcustom属性は追加セットとして扱わず、既存の未知属性の扱いを保つ。

実decodeとcook前fingerprintが同じStrict gateを使う。古いcooked cacheのsource hashが一致しても、
追加セット入力を新Strict経路がskipで受理することはない。NVSKEL形式・64B頂点・関節128上限は変更しない。

属性名分類の実コードは通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実行する。
Json bridge/decode/legacy/cooker/preflightとCLI出力保持の回帰はnative束へ登録し、Windows.h制約で未実行として区別する。
