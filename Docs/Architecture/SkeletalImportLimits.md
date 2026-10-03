# 骨格取り込みの制約とGR86方針

2026-10-03にG2-S4推奨Aが承認された。決定の全文とv1 rest pose安全契約は
[G2ImportDecisions.md](G2ImportDecisions.md) を参照。

| 制約 | 既定 | 明示指定時/後段 | 現在の状態 |
|---|---|---|---|
| 頂点影響数 | 4本、追加セットを専用statusで拒否 | cookで上位4本へ縮約・正規化・報告 | 追加セット拒否を実装、縮約kernelあり・decode未接続 |
| CUBICSPLINE | 拒否 | 誤差制限付きLINEAR焼込 | API/CLI接続済み、native未実行 |
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
CUBICSPLINEは下記で接続、morphは別作業。raw/legacy/file decodeとcook接続は下記を参照。

## 明示policyと診断型

SkeletalGltfDecodeOptionsはInfluencePolicy=Strict(0)/ReduceToFour(1)とWarnDroppedWeight/FailDroppedWeightを持つ。
既定はStrict、閾値は0.01/0.25。閾値は有限で0<=warn<=fail<=1。Strictでは未使用の非既定閾値を拒否し、
無視した指定を成功にしない。CUBICSPLINE指定は後述のBakeで拡張し、morph指定は未実装。

SkeletalGltfDecodeReportは総頂点数/処理済みprefix数、縮約/同joint合算/再正規化/警告の頂点数、
脱落比率の最大/平均、失敗頂点index、影響走査完了flagを分ける。未走査の頂点を含む全体平均と誤認しない。

Strict既定はcanonicalがvalid/Size0でsource hashを一切変えない。
Reduceだけ25byteを生成し、既存stateへlength(u64LE)+canonical+algorithm(u32LE)を連結する。
canonicalはSRED(4byte)、schema=1(u32LE)、policy=1(u8)、warn/fail(binary64LE各8byte)。
struct paddingや文字列表現を使わず、-0を+0へ統一する。algorithm初期値は1で、縮約処理の意味を変えるときに更新する。
警告閾値の変更もhashに含み、同じ入力の別policyを旧cacheと混同しない。

固定bytesと独立3初期stateのFNV既知値、Strict不変、閾値/algorithm差、invalid enum/数値、ゼロの正規化を
通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBERで検査する。cook/preflightのhashとdecode入口は下記の明示指定に対応する。CLI指定は下記を参照。

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
この統合試験は実行できず、純kernel/policy/pair試験の実行と区別する。CLIは既定Strict、明示Reduceを下記で指定する。

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
これらの統合試験はWindows.h依存で未実行。CLI flag/skipは下記のpolicyを同一経路へ渡す。

## AssetCookの明示縮約指定

--skin-influences strict|reduce（省略strict）、--skin-warn-dropped-weight 0..1、
--skin-fail-dropped-weight 0..1をNVSKEL model cookに限り受け付ける。別引数とequalsを両方扱う。
閾値の既定は0.01/0.25で0<=warn<=fail<=1。閾値指定にはreduceの明示指定が必要。
重複/空値/非有限/範囲外/末尾ゴミ/非骨格への指定は書込前に拒否する。
--cubicsplineは後述の明示Bakeに対応する。--morph/--joint-policyは未実装のため受理しない。

本cookと--skip-if-unchangedのfingerprintへ同じpolicyを渡すため、Strictとの混同や
閾値変更時のcache誤使用を防ぐ。cache hitは縮約を再実行せず既存のskip通知を返す。
実際にcookしたReduce資産はstderrへ処理頂点数/縮約/合算/再正規化/警告数と最大/平均脱落比率を出し、
警告閾値超過を明示する。共有JSON ImportReportへの格納は下記のversion 1を用いる。

pure parserを通常/O2-NDEBUG/ASan・UBSanとMEMBERで検査する。native CLI smokeには5影響fixture、
既定拒否/縮約成功/閾値超過と出力保持/同policy cache hit/閾値変更cache missを登録する。
Main/native CLI smokeはWindows.h・PowerShell/CMake依存によりこのLinux環境では未実行。

## ImportReport JSON version 1

BuildSkeletalImportReportは版付き共通envelope（version/kind/outcome/source_hash/decode_status）と
skinセクションを生成する。固定識別子と有限の数値のみ、locale非依存、決定的な出力。
任意文字列やsourceパスを含めず、4096byteの所有結果へ生成し、不正な測定は空の失敗結果にする。
source_hashは既知のcook/cache時だけ16桁hex文字列、失敗時はnull。

CLIは骨格cook API実行後とcache hit時に、stderrへimport_report=に続く1行JSONを出す。
outcome=payload_readyはNVSKEL payloadが作れた状態で、後続package/manifest書込の完了宣言ではない。
failedはcook API失敗、cache_hitは再decode省略。引数parse・入力file読込・preflight段階での拒否は
従来のerror通知であり、このJSONの生成対象ではない。新たなreport fileは書き込まない。

SkeletalCookDiagnosticsは既存resultと独立した任意出力。失敗resultを維持したまま、
decodeを試したか、status、成功prefixと失敗頂点の測定を返す。decode未実行の失敗は前回の測定を残さない。
Strictやcache hitは影響の再測定をしていないためinfluence_scan=nullとし、ゼロ件測定と偽装しない。
Reduceでも総頂点数がまだ不明な失敗はinfluence_scan=null。総数を取得済みでも成功prefixが0なら、
最大/平均はnullとして数値0の測定と区別する。失敗頂点で測れた脱落量は別欄に残す。
Reduceの測定はprocessed prefixの最大/平均と失敗頂点脱落量を分離する。

CUBICSPLINEは後述のskin拡張へ追加し、morphは未実装。GR84のBVH/retarget測定はまだ存在せず、
将来はこの版付きenvelopeへ別セクションを足す。BVH受理やretargetの実装済みを意味しない。
pure serializerの3mode/MEMBERとPython独立JSON解析で構文/数値/状態を検査する。
native回帰には成功/閾値失敗/古い診断の消去・CLI JSON/警告有無/cache未測定を登録し、
CoreとMainのWindows.h依存のため未実行として区別する。

## CUBICSPLINE policy/hash定義

SkeletalCubicSplinePolicyはReject(既定)/Bake。許容はtranslation=0.001m、
rotation=0.0017453292519943296rad（0.1度）、scale=0.001（無次元）が既定。
Bakeでは有限かつvector数値予算の下限64ε、rotationの下限256εを厳密に超える値を要求し、
rotationはpi以下とする。Rejectでは使用しない許容/予算の非既定指定を拒否する。

既定depth=20（最大24）、channel sample=65536（最大1048576）、asset sample=1048576（最大4194304）。
sample上限は各2以上、assetはchannel上限より小さく設定してもよい（両方の制約を適用する）。
診断型にanimationの総数/正常prefix、bakeしたchannel/input/output key数、単位別上界、
失敗channel/status、scan開始/完了を追加する。

Bake無しのStrictはSize0、Reduceは従来SREDの25byteとhashを完全に維持する。
BakeのcanonicalはSCBK、schemaLE32、influenceU8、warn/fail各binary64LE、cubicU8、
translation/rotation/scale許容各binary64LE、depth/channel/asset各u32LE、cubic algorithm u32LEの66byte。
最後のalgorithm初期値は1。外側のlength+canonical+reduction algorithm連結は従来どおりで、
どの許容/予算/アルゴリズムも別cache鍵となる。構造体のpaddingをhashに含めない。

raw/legacy/file/cookの明示Bake指定は下記の共通decoderへ接続する。
Reject既定はUnsupportedInterpolationを維持し、JSON/CLI指定は下記で接続する。
Python独立66byteと3初期state hashのgolden、legacy25byte/hash不変、全閾値/予算/無意味指定をpureで確認する。

## CUBICSPLINEの共通decode接続

ParseAnimationContractは明示Bake時だけCUBICSPLINEを受け付け、
output.count=3*input.count、2入力キー以上、型/layout/rangeを検査する。
各in/value/out tripletをそのままbakerへ渡し、最終倍率でtranslationをメートル化してから認証する。
rotationとscaleの許容は単位を変えない。全元キー・時刻順・LINEAR出力の契約は共有bakerが守る。

fitを含むuniform倍率は未変換のmesh-node線形変換後のextentから一度だけ解決する。
Bake modeでは通常LINEAR/STEP translationも同じ倍率で変換し、最後のgeometry/IBM/mesh-node適用では
animationの再scaleを省く。Reject modeは従来の抽出後scale順を維持する。

statusは既存0〜17を保ちCubicBakeFailed=18を末尾追加。triplet/count等の構造不正はInvalidAnimation、
bakerの数値/容量/深さ等の拒否はCubicBakeFailedと具体的なFailedCubicBakeStatusへ分ける。
Reportは全animation channelの正常prefixと失敗channel、成功したCubicのinput/outputキー数・単位別上界を返す。
Translation/Rotation/Scaleごとの焼込channel数も分け、未処理の種類を誤差ゼロ測定と混同しない。
1channel途中の未認証キー列を公開しない。失敗Data/sourceは空で、cook resultは以前の成功値を保持する。
channel/asset双方のsample予算を割当前に適用し、現在の単一clipに含まれるCubic出力の合計を制限する。

CubicChannels.gltfはTranslation/Rotation/Scaleの正しいtripletを持つfixture（binaryはnative試験で生成）。
raw/legacy/file/GLB/cook再parse、明示Bakeでも通常LINEAR/STEP不変、設定scale/fitで二重scaleなし、
既定拒否・channel/asset予算・内部zero回転・非有限tangent・triplet count不正とprefix診断をnativeへ登録する。
独立Hermite式と実共有sampler helperでbaked channel値も確認する試験だが、Core/decoder/cookの実行は
Windows.h依存によりこの環境では未確認。純baker/policyの実行とは区別する。CLI引数とJSONのCubic項目は下記で接続する。

## CUBICSPLINEのCLI・JSON診断

NVSKEL model cookで --cubicspline reject|bake を指定する（既定reject）。
Bake時だけ以下の設定を受理する。別引数とequals形式の両方に対応し、重複・空値・不正数・未使用設定は拒否する。

- --cubic-translation-tolerance: 最終単位変換後のメートル。既定0.001
- --cubic-rotation-tolerance-deg: 度。既定0.1度相当。API/hash/JSONへはラジアンに変換
- --cubic-scale-tolerance: 無次元。既定0.001
- --cubic-max-depth: 0〜24。既定20
- --cubic-max-channel-samples: 2〜1048576。既定65536
- --cubic-max-asset-samples: 2〜4194304。既定1048576

並進/scale許容は64*float epsilonより大きい有限値、回転は256*float epsilon radより大きく180度以下。
これは入力設定の下限であり、すべての曲線が焼込可能になる保証ではない。値の大きさや短時間隔等で拒否し得る。
cookとskipには同じDecode optionsを渡すため、許容/予算の変更もsource hashとcache判定へ反映される。
成功時はstderrに焼込channel数・入力/出力key数・種類別上界と変換警告を出す。

ImportReport version 1はBake時だけskin内へ以下を追加する。Reject時の旧JSONは不変。

- cubic_policy: bake
- cubic_settings: translation_tolerance_m / rotation_tolerance_rad / scale_tolerance / max_depth / max_channel_samples / max_asset_samples
- cubic_scan: 未開始またはcache hitではnull。開始後はtotal_channels / processed_channelsとbaked_channels、baked_translation_channels / baked_rotation_channels / baked_scale_channels、input_keys / output_keysを持つ
- scan内のmax_translation_error_m / max_rotation_error_rad / max_scale_errorは成功した焼込channelだけの最大上界。該当種類が未測定ならnull（通常LINEAR/STEPも未測定）
- scan_complete、failed_channel（全channel中の0始まり番号またはnull）、failure_status（CubicBakeStatus数値またはnull）で完走/失敗を区別する

途中失敗は成功prefixの計測だけを保持する。失敗channelの未認証誤差を成功した最大誤差へ混ぜない。
failure_statusは0=成功以外を報告する。1入力不正/2設定不正/3算術未対応/4容量不足/5領域重複/6短区間/
7時刻衝突/8深さ超過/9sample予算超過/10保存値表現不可/11quaternion不正/12数値予算不足/13認証不能。
Bakeを指定しても通常補間だけならbaked_channels=0、各誤差=nullで、変換警告も出ない。

純parser/JSON検査とnative CLI smokeを分ける。後者はglTFの3種Cubic、既定拒否・予算超過・出力保持、
同設定cache hit・設定差cache miss、JSON単位と未測定nullを登録。Windows.h/PowerShell/CMakeが必要な統合実行は未確認。

## morph Dropのpolicy/hash定義（decode接続前）

SkeletalMorphPolicyはReject=0/Drop=1で既定Reject。Dropは明示指定でのみ有効になる。
この段階では公開options・canonical・診断型のみ定義する。decoderはDropをUnsupportedMorphTargetsで拒否し、
JSON serializerもDropを未接続として失敗する。CLI引数はまだ受理しない。

Drop無しはStrictのSize0、ReduceのSRED25byte、BakeのSCBK66byteと既存hashをそのまま保つ。
DropはSMDPで、MorphPolicyだけRejectへ戻した既存canonicalを包む。
magic SMDP（4byte）/schema=1（u32 LE）/Drop=1（u8）/inner size（u32 LE）/
inner bytes（0/25/66byte）/morph algorithm=1（u32 LE）で計17/42/83byte。
既存外側hash規約のu64 size + bytes + u32 reduction algorithmを維持する。
これによりDrop有無、縮約/焼込設定、除去algorithmの違いをcache鍵で区別する。

診断型にはDroppedMorphTargetCount（target数）、DroppedMorphMeshWeightCount（mesh初期weight配列要素数）、
DroppedMorphNodeWeightCount（node初期weight配列要素数）、DroppedMorphAnimationChannelCount（weight channel数）、
bMorphScanCompleteを定義する。完了前のゼロは未測定であり、ゼロ除去として報告しない。
除去はbase geometryへmorphを焼き付ける処理ではない。sparse拒否と128関節上限は維持する。
