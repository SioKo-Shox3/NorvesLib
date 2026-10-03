# 骨格取り込みの制約とGR86方針

2026-10-03にG2-S4推奨Aが承認された。決定の全文とv1 rest pose安全契約は
[G2ImportDecisions.md](G2ImportDecisions.md) を参照。

| 制約 | 既定 | 明示指定時/後段 | 現在の状態 |
|---|---|---|---|
| 頂点影響数 | 4本、追加セットを専用statusで拒否 | cookで上位4本へ縮約・正規化・報告 | 追加セット拒否を実装、縮約kernelあり・decode未接続 |
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

## 上位4本縮約kernel（decode未接続）

ReduceSkinInfluencesは呼出側workspaceを使い、値/関節範囲/入力総和を検査してから同一jointをまとめる。
足し合わせる順もjoint番号と値で正準化し、補償加算を使う。合算後のMinimumWeight以下を落とし、
残りを重み降順・同値joint番号順で4本へ絞る。微小値が同一jointへ分散している場合は合算してから判定する。

脱落量は『微小値として落とした量＋上位4本外の量』を元総和で割った比率。
元総和から残量を引く式ではなく、落とした量を直接足し、微小な脱落を丸めて消さない。
WarnDroppedWeightを超えれば警告、FailDroppedWeightを超えれば失敗（等しい場合は許可）。
これは脱落weight比率であり、実ポーズの最大変形距離を保証する値ではない。
出力4本をfloatへ正規化し、正値の完全underflowは拒否する。失敗時vertex出力は非変更で、閾値超過時の測定結果は返す。

このkernelは数値として正規化済みの影響を扱う。glTFのセット連続性/型/同数/正規化整数raw総和の検証は
接続側で別途行う。glTF自体では同一jointの複数非ゼロweightは規約外だが、明示Reduceの合算は限定した修復処理とする。
Strictの通常4影響経路をこの修復へ無言で切り替えない。
参考: [glTF skinned mesh attributes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes)。

5本の既知値、120順列、同joint/tie/微小値、警告・失敗境界、無効値/関節/総和/aliasと出力保持を純実装で確認する。
CUBICSPLINE/morphとdecode/CLI接続は別作業。
