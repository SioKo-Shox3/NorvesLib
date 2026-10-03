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
CUBICSPLINE/morphとCLI接続は別作業。raw/legacy/file decodeとcook接続は下記を参照。

## 明示policyと診断型

SkeletalGltfDecodeOptionsはInfluencePolicy=Strict(0)/ReduceToFour(1)とWarnDroppedWeight/FailDroppedWeightを持つ。
既定はStrict、閾値は0.01/0.25。閾値は有限で0<=warn<=fail<=1。Strictでは未使用の非既定閾値を拒否し、
無視した指定を成功にしない。CUBICSPLINE/morphの指定はまだこの型にもCLIにも追加しない。

SkeletalGltfDecodeReportは総頂点数/処理済みprefix数、縮約/同joint合算/再正規化/警告の頂点数、
脱落比率の最大/平均、失敗頂点index、影響走査完了flagを分ける。未走査の頂点を含む全体平均と誤認しない。

Strict既定はcanonicalがvalid/Size0でsource hashを一切変えない。
Reduceだけ25byteを生成し、既存stateへlength(u64LE)+canonical+algorithm(u32LE)を連結する。
canonicalはSRED(4byte)、schema=1(u32LE)、policy=1(u8)、warn/fail(binary64LE各8byte)。
struct paddingや文字列表現を使わず、-0を+0へ統一する。algorithm初期値は1で、縮約処理の意味を変えるときに更新する。
警告閾値の変更もhashに含み、同じ入力の別policyを旧cacheと混同しない。

固定bytesと独立3初期stateのFNV既知値、Strict不変、閾値/algorithm差、invalid enum/数値、ゼロの正規化を
通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBERで検査する。cook/preflightのhashとdecode入口は下記の明示指定に対応する。CLI指定は別作業。

## 複数セット記述の収集

CollectSkeletalInfluenceSetsはJOINTS_n/WEIGHTS_nの属性名とaccessor番号を収集し、JSON内の並びに依らず
set番号/joint→weight順へ正準化する。0始まり連続・各setのjoint/weight各1つを必須にし、
片側欠落、番号飛び、重複名、非正準名、不正な整数accessor番号を拒否する。
成功結果はset0..N-1順の所有配列で、失敗時は以前の結果を保持する。

ここではaccessorの存在/型/count/bufferやweight数値は検査しない。decode側がそれらを検査する。
既定Strictは引き続き専用gateで追加セット自体を拒否し、このcollectorを使って受理へ緩めない。
正準pair検査と既存属性名分類を通常/O2-NDEBUG/ASan・UBSanで実行し、JsonDocument経由の収集/失敗保持試験は
native束へ登録する。実JSON試験はWindows.h依存で未実行として区別する。

## 明示Reduceのdecode入口

raw bytes/旧StringのDecodeSkeletalGltfおよびGLTFAnalyzer::AnalyzeSkeletalは、末尾の任意decodeOptionsを呼出中だけ借用する。
省略はStrictの従来経路で、ReduceToFourの明示時だけ複数セットを収集し共有kernelへ渡す。
各setのVEC4/component/normalized/count/offset/layoutと、全slotのjoint範囲を縮約前に検査する。
ゼロweightや捨てるslotもjoint範囲の例外にしない。整数weightだけならUNORM8を257倍した65535分母の
raw総和を厳密に照合する。FLOATを含む場合は全セットの数値総和へkernelの0.001許容を適用する。
どちらもsetごとに1を要求せず、頂点の全影響を合わせて検査する。128関節、頂点ABI、cooked wireは不変。

statusは既存0〜15を保ち、InvalidImportOptions=16、InfluenceReductionExceeded=17を追加する。
縮約reportは成功した頂点prefixの最大/平均/各件数を持ち、失敗頂点をその母集団へ混ぜない。
閾値超過時だけ別欄FailedVertexDroppedWeight/bHasFailedVertexDroppedWeightへ測定を返す。
bInfluenceScanCompleteは影響走査の完了だけを表し、後段のindex/骨格/clip/import失敗とは別である。
Strictのreportは既定値。失敗Dataは空、source buffer出力も空のままで、部分decode結果を公開しない。

FiveInfluences.gltfは3頂点中1頂点だけ5本の影響を持つ検証用fixtureで、binはnative試験が生成する。
raw/GLB/String/fileの同値、5→4の値と測定、閾値超過、追加slotの不正joint/ゼロweight/負値、
UNORM16のraw総和1不足とoptions不正をnative試験へ登録する。LinuxではCoreのWindows.h依存により
この統合試験は実行できず、純kernel/policy/pair試験の実行と区別する。CLIはまだStrictのみ。

## 骨格cookと事前hash照合

CookGltfToNvskelとFingerprintModelCookSourceの末尾decodeOptionsは省略時Strict。
本cookは設定snapshotとoptionsを同じdecodeへ渡し、成功したDecodeReportをSkeletalCookResultへ所有する。
縮約失敗は出力result全体を保持し、errorへstatus/処理済み頂点数/失敗頂点/測定できた脱落比率を返す。
診断のために失敗資産の一部payloadを公開しない。

source hashは元source/buffer bytes → 正規化import settings → 正規化skeletal policy/algorithmの順で連結する。
Strictでは最後の連結が恒等なので、既定hashとNVSKEL bytesは従来どおり。Reduceではwarnだけの変更もhashが変わる。
preflightはStrict追加set拒否を維持し、Reduceなら連続pair記述を検査するが、geometry読込/縮約はしない。
よってpreflight成功はcook成功を意味せず、閾値超過や不正weightは本cookで拒否する。
静的meshのfingerprintへdecodeOptionsを指定した場合は、既定値でも指定自体を拒否する。

native回帰にraw/GLBのcook/preflight hash一致、cooked再読込値、warn/failのhash差、
sidecarとの連結順、Strict明示値と省略値の旧hash一致、失敗out保持と診断を登録する。
decodeの正常prefix後失敗とUNORM8/16複数setの全総和/1不足も恒久回帰へ加える。
これらの統合試験はWindows.h依存で未実行。CLI flag/skipへの接続はまだ行わない。
