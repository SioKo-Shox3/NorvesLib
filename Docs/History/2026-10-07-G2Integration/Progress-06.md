- G2-GR78-SOURCE-PREFLIGHT: FingerprintModelCookSourceを追加し、geometry変換/cluster/画像decode/package生成前に本cookと同じsource/buffers/正規化設定hashとowned embedded画像metadataを取得する。設定file helperをstatic/skeletal/cook前照合で共有。返却に借用bytesを含めず、成功末尾だけ公開する。既存native束へ各入力形態/設定/骨格/hash同値/画像metadata/失敗保持と巨大scaleでの照合成功・cook失敗の区別を登録。既存純設定hashの3mode再実行成功、今回のnativecompileはWindows.hで停止し実行未確認。独立レビューPASS。非blockingのrequired不在時preflight単独assert強化はnative受入れ時の追補候補として保持。
- Next: GR78-CLI-SKIPへ接続し、manifest key/source hash/要求format・出力先とmodelおよび派生画像package実体を検証してからcookを省略する。

- G2-GR78-CLI-SKIP: --skip-if-unchangedをcook前fingerprintへ接続。manifest key/source hash/format/entry/type/version/要求出力先と実NVPACK entry・payload hash・NVMESH/NVSKEL構造を照合し、全embedded画像のmanifest/package/NVTEX/colorspaceも検査したhitだけ早期return。入力未変更でも欠損・破損・要求変更なら通常cookへ戻す。実flag parserを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功。native smokeに全出力timestamp非更新、設定/meta、model/画像欠損・破損、有効表記のhash不一致/format/entry/variant/output変更、骨格buffer変更を登録。独立PASS。cache本体/Main/native smokeはWindows.h等のため未実行。競合変更に対するatomic性やFNVの認証用途は保証しない。
- Next: preflightとskipをまとめて2コミット公開後、GR78-CLI-INSPECTのcookしない診断に進む。実物4本は未入手のため手動照合は保留。

- G2-GR78-INSPECT-GEOMETRY: 無変換bounds/軸長/最長軸・頂点/三角形/完全一致位置溶接/頂点共有成分/厳密ゼロ法線の純kernelを追加。符号付きゼロは同一、近接値は別、孤立頂点込みと明記。sort/union-by-rank/path-halvingで計測し、非有限/index/容量/全Span重複と乗算overflowを先行拒否。第1周でworkspace使用prefixだけの検査と公開契約の差を指摘され、全Spanへ修正し回帰を追加、第2周PASS。最終ソースの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）実行とMEMBER compile成功。CLI接続はまだ行わない。
- Next: GR78 inspect用のPNG/JPEG寸法・チャンネル統計を実stbで診断する部品を追加し、その後geometry/画像/材質を無書込CLIへ統合する。

- G2-GR78-INSPECT-IMAGE: PNG/JPEGを実stbで診断し、幅/高さ/decoded channel/8・16bitと整数min/max/meanを取得。色空間の線形化や16bitの縮約はせず、入出力aliasと失敗公開を防ぎRAIIで解放。最終pixel payloadの512MiB上限をinfo後に判定する。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で実stbの既知PNG8/PNG16/JPEGとinvalid/巨大/aliasを実行成功、MEMBER compile成功。独立PASS、追加反証のgray/gray-alpha/palette+tRNS/1bit/16bit RGB・tRNSも成功。512MiBはstb内部総メモリ/IDAT inflate上限ではない点をheaderと仕様に明記。CLI/native統合と実物照合は別受入れ。
- Next: 幾何・画像の診断部品を公開後、材質係数と合わせて--inspect <file>の無変換・無書込診断へ接続する。

- G2-GR78-CLI-INSPECT: --inspect <file>/equalsを独立modeとして接続。単一mesh/primitiveの現行static profileを既存accessor検証で読み、mesh-local変換前の幾何と全画像の整数統計、core材質係数/規定値と任意emissive strengthを診断する。sidecar/cook/cluster/書込を通らず、成功後だけstdoutへ表示。+Y仮定の水平軸候補と符号manual、恒等設定叩き台はstdoutのみ。実引数parserの3mode/MEMBER成功、native API/CLI試験に値・不正入力・出力保持/外部画像・無書込を登録。独立PASS。nativeはWindows.hで未実行、実物4本は未入手。必須拡張の具体的診断理由保持とCLI再入力差の回帰はnonblocking追補候補。
- Next: GR78の実装/検証/保留をまとめる。GR86(S4)とGR32/GR82(S1)の推奨案は作者へ確認し、回答までは変更を適用しない。

- G2-GR78-VALIDATION: Docs/Architecture/GR78ImportValidation.mdへ承認済み実装・18実行成功（6純実装×3mode）・MEMBER compile・native/実物の未実行・surface保留を分離して記録。CMake登録と実logを最終照合し、CI空statusを成功扱いしない。次のS4/S1確認待ちをtaskへ明記し、GR78承認を他選定へ拡張しない。
- Next: inspect/検証記録を公開し、S4/S1の回答に応じてG2を継続する。未承認の形式/Strict/描画既定は変更しない。

- G2-S1-S4-DECISIONS: 作者の2026-10-03承認を記録。S4 A（Strict追加セット拒否/4本縮約/LINEAR焼込/morph明示drop/256はv1）とS1 A（0.2統一→v1）を確定。オオカミ/シ者の作り分け/共通土台張り直しを採用済みとしていた前提を未定へ訂正。v1 clip作成時rest保持、現在骨格との束縛時比較、許容超過既定拒否、明示許可と差量報告、全関節Translation回帰を必須契約へ追加。SkeletonIdだけでcache共有しない点も反映。ローカルPlans3本と追跡仕様/選定taskを照合し、追加auditでblocking誤記なし。他選定は未確定のまま。
- Next: GR86 Strictの追加ウェイトセット黙認をまず拒否する。cook前fingerprintも同じ検査を共有し、過去cache経由で新Strict拒否を迂回させない。

- G2-GR86-STRICT-INFLUENCES: 承認済みStrict変更としてJOINTS_n/WEIGHTS_nの追加セットを値/片側/null/ゼロに依らずInfluenceLimitExceededで拒否。属性名の正準/overflow/customを分類し、実decodeとcook前fingerprintで同一Json gateを共有して旧cacheのskip迂回も防ぐ。status末尾15のみ追加し既存0〜14、128関節、64B頂点、NVSKEL形式は維持。分類実装を通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBER compileで成功、独立PASS。追加重みゼロの正規fixtureとraw/legacy/cooker/preflight/CLI出力保持をnative登録。Json/decode/nativeはWindows.hで未実行。
- Next: 同一joint合算・決定的な上位4本・脱落量/閾値・失敗保持を備える縮約kernelを独立実装し、その後明示Reduce optionにだけ接続する。

- G2-GR86-REDUCE-KERNEL: 同jointを正準順で補償加算し、合算後微小値を除き、重み降順/同値joint番号順で4本へ縮約・float正規化する純kernelを追加。脱落量は除去値の直接和/元総和で求め、厳密な閾値超過と正値underflowを拒否、失敗時vertexを保持。型・容量・全Span/options aliasも検査。通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBER成功、独立PASS。追加反証の合算後閾値/総和nextafter/巨大Span/部分alias/混合桁重複jointの10000順置換も成功。恒久回帰への後二例追補はnonblocking候補。glTF raw整数総和とセット構造検査は接続側で別途行うため、現時点ではdecode/CLI未接続。
- Next: Strict既定不変を保つ明示Reduce options/reportとcanonical policy hashを定義し、その後複数セット読込へ接続する。

- G2-GR86-OPTIONS-HASH: 独立Public型へStrict/ReduceToFour・warn/failと走査prefixを明示するreportを定義。Strictは既定閾値のみ受理しhash不変、ReduceはSRED/schema/policy/warn/failの25byte LE/-0正規化と長さ/algorithmを既存FNVへ連結。公開header登録とkernel初期値の定数共有を実施。Python独立固定bytes/3state goldenと実policy・kernelの計6mode実行、MEMBER/public単独include/layout/trivial-copy確認に成功、独立PASS。decode/CLI/形式/statusは未変更。
- Next: 0始まり連続のJOINTS_n/WEIGHTS_n pairを収集・検証する部品を作り、Reduce指定のdecode接続へ進む。

- G2-GR86-INFLUENCE-SETS: JSON内順序に依らずJOINTS_n/WEIGHTS_nを正準順へ収集する所有collectorとpair検査を追加。0始まり連続/各1個/同数pair/整数u32番号を検査し、失敗時結果保持。Strict専用gateは変更せず、accessor型/count/buffer/weightは後段責務と明記。pure pairと既存分類の6mode実行/MEMBER成功、native16JSON fixtureは文法確認と登録のみ（Windows.hで未実行）。独立PASS。
- Next: Reduceをraw/legacy/file decodeへ明示optionsとして接続し、全影響のjoint範囲とUNORM raw総和を落とす前に検査する。

- G2-GR86-REDUCE-DECODE: 明示Reduceだけraw/legacy/fileに複数set読込と縮約を接続。全slot joint範囲、型/count/layout、全整数影響の65535共通分母raw総和を縮約前検査。成功prefixの統計と失敗頂点脱落量を分離し、失敗Data/sourceを非公開。Strict/status0〜15/128関節/ABI/wire不変、16/17を末尾追加。3頂点中1頂点5影響fixtureと入口同値/閾値/不正joint/負値/UNORM/optionsをnative登録。純kernel/policy6実行成功、独立確認はpair込み9実行成功。review中にExtractAnimationの不要追加引数を復元し最終PASS。nativeはWindows.hで未実行。正常prefix後失敗と複数UNORM混在の恒久回帰強化はnonblocking追補。
- Next: collector/decodeを公開し、GR86-REDUCE-COOKのcook/preflight/hash/診断へ接続する。CLIはその後、CUBICSPLINE/morphは別反復。

- G2-GR86-REDUCE-COOK: 明示optionsを骨格cook/preflightへ伝播し、source/buffer→import settings→policy/algorithmの同順hashを共有。Strict hash/wire不変、成功DecodeReport所有、失敗out保持とerror内の頂点/脱落量を追加。preflight成功はcook可能性を意味せず、静的meshへの骨格options指定は拒否。raw/GLB再読込・hash/閾値差・sidecar・旧Strict同値・失敗保持、decoder正常prefix後失敗/混在UNORMの恒久回帰を登録。純policy3mode実行成功、独立PASS。nativeはWindows.hで未実行。
- Next: GR86-REDUCE-CLIの明示指定とskip接続へ。共有JSON ImportReportは別taskとして保持し、stderr診断だけでGR86全体完了としない。

- G2-GR86-REDUCE-CLI: --skin-influences strict|reduceとwarn/fail閾値を別引数/equalsで厳密parse。重複/不正数/閾値だけ/非骨格指定を拒否し、順序に依らず最終検査する。cookとskipへ同じpolicyを伝播し、実cook時のstderr統計/警告を追加。純parser通常/O2-NDEBUG/ASan・UBSan（LSan除外）と-Werror MEMBER成功、独立PASS。5影響CLI成功/既定拒否/閾値失敗出力保持/同値cachehit・閾値差cachemissのnative smokeを登録。Main/native smokeはWindows.hとPowerShell/CMake依存で未実行。警告文/全診断値のnative assert強化はnonblocking候補。
- Next: cook/CLIを公開し、共有JSON ImportReportへ成功/失敗測定を接続する。CUBICSPLINE/morph/joint-policyは未実装のまま拒否。

- G2-GR86-IMPORT-REPORT: version1共通envelope/skinの所有JSONを追加。payload_ready（package書込前）/failed/cache_hitを区別し、cook独立診断出力で失敗resultを保持しつつstatus/prefix/失敗頂点を返す。CLIはAPI結果とcacheにstderr1行JSONを出し新file書込なし。第1周の未測定0混同を修正し、Strict/cache/総数不明はscan=null、prefix0のmax/mean=null、失敗頂点測定は独立保持。第2周PASS。pure3mode/Werror MEMBER/Python独立JSON解析成功、native診断/CLI JSON・警告閾値回帰はWindows.hにより未実行。Cubic/morph/GR84測定は将来拡張として明記。
- Next: ImportReport公開後、GR86 CUBICSPLINEの保守的誤差付きLINEAR焼込へ。実samplerの短区間処理とfloat丸めを評価し、保証不能は拒否する。

- G2-GR86-CUBIC-KERNEL: Hermite→外向き区間Bezier、deCasteljau評価/分割、保存float端点に対する理想LINEARの全区間L2/SO(3)上界を追加。quaternion符号を変えずE<mで非ゼロを認証し、角度上界4E/m+d²を保守的に計算。失敗out保持、左右alias拒否、入力片側alias許可。IEEE最近接/subnormal/精密FPを仮定しunsafe算術検出時拒否。純3mode/Werror MEMBER成功、3781認証ランダム曲線×201点の独立回転反証も違反なし。内部zero回帰を追加し規約の波括弧整形後、第2周PASS。binary64参照式の丸め偽陽性をtest専用予算で区別し、double参照版も成功（ULP級厳密包絡/Windows成功ではない）。実samplerのfloat/短区間/sidecar単位・decode/cook受理は次taskであり現段階CUBICSPLINE拒否を維持。
- Next: GR86-CUBIC-RUNTIME。全回転区間を数学dot下限>0.9996へ分割して実NLERP分岐を認証し、256*float epsilon radの保守的数値予算を条件付きで加える。保存float時刻の区間長/分割比も外向き区間で扱い、短dt/深さ/sample不足は拒否する。

- G2-GR86-CUBIC-RUNTIME: 保存float時刻の差/分割比を外向き区間に入れ、ValueScale適用後の数学上界と条件付きfloat予算で適応LINEAR列を生成。回転はnorm[.99,1.01]/dot下限>.9996を認証し実NLERP枝へ限定、256epsilon rad予算、vectorは64epsilon*端点L1規模。短dt/時刻衝突/zeroq/深さ24/sample容量を拒否し全out保持。元samplerのNormalize/Slerp/alpha式をportable private helperへ式不変で移し本物Math型と共有。pure3mode/Werror MEMBER/旧bounds回帰成功、第2周PASS。独立反証は成功534曲線・6,503,462 queryで上界違反なし。oracle hをlong doubleのまま保ち2^-60→1時刻差を追加。Core/pose/Windows全体はWindows.hにより未実行、別targetのFP条件は未確認と明記。Cubic decode受理はまだ拒否。
- Next: Cubic policy/hashを定義し、最終メートル倍率のdecode接続とCLI/JSONへ順に進む。

- G2-GR86-CUBIC-POLICY: Reject/Bake、メートル/ラジアン/無次元の許容、depth/channel/asset予算、prefix/失敗診断を定義。Bake無しStrictSize0/ReduceSRED25byteと旧hashを維持し、Bake時だけSCBK66byteへ全設定/cubic algorithmを格納。未使用指定・不正数値/enum/予算を拒否し、bakerも公開sample上限を共有。decoder接続前のBakeはUnsupportedInterpolationで明示拒否。純policy3mode/MEMBER・header独立POD・CLI/report/bake回帰成功、独立PASS（65,536 enum組合せ/256設定golden等も成功）。native raw拒否回帰は登録のみでWindows.h未実行。境界受理の恒久assert強化はnonblocking追補。
- Next: GR86-CUBIC-DECODEへ接続し、fitを含む最終translation倍率をbakerへ渡して二重scaleを避ける。

- G2-GR86-CUBIC-DECODE: 明示Bakeだけtriplet/count/型/rangeを共有bakerへ接続。channel/asset予算を割当前に制限し、正常prefix/種類別counter/失敗詳細/単位別上界を返す。fit倍率を未変換meshから一度だけ解決し、Bakeでは通常translationも早期scale、最後animation再scaleを省いて二重変換を防ぐ。既定Reject順は維持、status18を末尾追加。3種Cubic fixtureとraw/legacy/file/GLB/cook再parse、scale/fit、通常LINEAR/STEP同値、累積予算/zeroq/非有限/count不正をnative登録。第1周のnative試験namespace不整合を既存Reduce試験分も含めて修正し第2周PASS。実bakerによるfixture数値検証と純policy/bake各3mode成功。native decoder/cookはWindows.hで未実行。
- Next: policy/decodeを公開し、CUBIC-CLIの明示指定・単位別許容・JSON診断へ接続する。

- G2-GR86-CUBIC-CLI: --cubicspline reject|bakeと最終メートル/度/scale許容・depth/channel/asset予算を厳密parseし、既存のcook/skip共通optionsへ接続。JSON version1はBake時だけ単位付き設定/成功prefix/種類別上界/失敗channelと理由を追加し、未開始/cache/該当種類未測定はnull。旧Reject JSON不変、変換警告も追加。純parser/report各3mode・MEMBER・独立JSON11件解析成功、独立PASS。実bakerでsmoke fixtureの101/85/45key・sample予算失敗も照合。Main/nativeCLI/CMake/PowerShellはWindows.h等により未実行。既定拒否/失敗出力保持/設定差cache/JSONをnative登録。
- Next: 明示morph Dropのpolicy/hash・decode・CLI診断へ進む。sparse拒否と128関節を維持し、256はv1まで保留。

- G2-GR86-MORPH-POLICY: Reject/Dropと除去数量/scan完了型を定義。Drop無しのSize0/SRED25/SCBK66を保持し、DropだけSMDP(schema/policy/inner size/旧canonical/morph algorithm)17/42/83byteで包む。decoderは接続までUnsupportedMorphTargets、JSONもDropを旧Rejectと偽らず未対応とする。純policy3mode/MEMBER/public単独POD/CLI・report回帰成功、独立Python固定列・12hash照合と再実行PASS。raw/legacy/GLB/file/cook拒否native回帰を登録、Windows.hにより未実行。
- Next: GR86-MORPH-DECODE。明示Dropだけtargets/初期weights/weight channelを検証して除去し、base/TRSを維持する。Strict/skipの無言無視も防ぐ。

- G2-GR86-MORPH-DECODE: 明示DropでPOSITION/NORMAL/TANGENTのデルタと初期mesh/node weight・weight channelを検証し、base/TRSへ適用せず除去。weight専用Cubicは焼込不要、共有samplerはTRS側条件を維持。数量は検証完了後だけ確定し、Strictとpreflightは同じmorph存在gateで拒否。nativeに入口/cook往復/不正12種/sparse/非有限/出力保持を登録。第1周の試験API名とcompact JSONのURI除去を修正し第2周PASS。fixture全buffer範囲/12mutation文法/URI除去をPython確認、既存純policy/CLI回帰成功。nativeはWindows.hで未実行。完全validatorではなく、POSITION bounds実値照合・weight入力min/maxとanimation不正の細分理由は残る。LINEAR/STEP weight等の追加回帰はCLI taskで補強する。
- Next: MORPH-CLIで明示指定、cook/skip同一設定、数量警告、JSON未測定/検証済みと失敗を接続する。

- G2-GR86-MORPH-CLI: --morph reject|dropを厳密parseしcook/skipへ同じ方針を伝播。数量と除去警告、JSONのmorph_policy/morph_scanを追加し、未走査/cacheはnull、実測0件と後段失敗の検証済み数量を区別。純parser/report各3mode・MEMBER・18JSON独立解析成功、独立PASS。nativeには警告/出力保持/cache/0件、LINEAR/STEP weight・複数target・weightだけのclip拒否・TRS後段失敗を追加。nativeはWindows.h/CMake/PowerShell依存で未実行。
- Next: GR86前半の最終portable回帰と検証範囲を記録する。GR79/GR32/GR82へ進むためS3(a)材質レコードとS7のmesh受理範囲は作者判断待ち。

- G2-GR86-VALIDATION: 公開実装b2aa6ffの8 portable試験をg++14.2.0で再ビルドし通常/O2-NDEBUG/ASan・UBSanの計24実行成功（LSan除外）。実InvokeBundledMainを含む8 MEMBER wrapper compileとJSON各18件・計54件の独立構文/意味照合も成功。GR86ImportValidation.mdに実装済み前半、native/実物/GPU未実行、数値条件・morph制限、v1待ち256を分離して記録。独立監査で過大主張なし。0.2は計画中と明示した。
- Next: S3(a)/S7回答待ちの間、GR32/GR82の形式移行とauthoring rest保持の変更箇所を読み取りで棚卸しする。機能実装/新wireの適用は先取りしない。

- G2-GR32-GR82-MIGRATION-INVENTORY: 現行minor0/1・256B、1primitive/1clip、cooker/loader/resource/component/起動側の単一clip依存を実ソースで棚卸しした。NVSKELMigrationInventory.mdへ統一0.2への変更境界と旧版/中間/Stage A試験、v1作成時restの現在の欠落箇所・取得→変換→cook→parse→bind→reloadの追跡と必須拒否/明示許可試験を整理。コード/wire/受理契約は未変更。S3(a)/S7とARM/emissive/S5/S6を未決のまま維持した。
- Next: 現G2の自律的に進められる実装/準備は一区切り。S3(a)/S7の回答後にGR32/GR79/GR82の次taskを具体化する。未実行native/実物、surface保留、v1 rest guardは引き続き未完として保持する。

- G2-S3(a)/S7: 2026-10-04作者がNVMESH v1/128B材質/旧v0併読と1mesh/Nprimitive案Aを承認。決定記録/元Plans/選定taskを反映し、ARM/emissive/S5/S6/BVH/FBXは別途保留を維持した。
- In progress: GR32のplain submesh型とpacked範囲検証。受理拡張・wire・描画の接続前に、旧空表互換と1..8範囲/slotのCPU契約を固定する。

- G2-GR32-SUBMESH-CONTRACT: Rendering非依存のplain SkeletalSubMeshと名前slotをSkeletalGltfData末尾へ追加し、1..8のpacked三角形範囲/slot/u32制限を純kernelで検査。両表空は旧互換1範囲/slot0、片側空拒否、失敗数量0と入力非変更を維持。通常/O2-NDEBUG/ASan・UBSan（LSan除外）と実MEMBER wrapper compile成功。第1周でCTest登録抜けを修正し第2周PASS。decoder/wire/resource/描画の受理は未変更。native全体は未実行。
- Next: GR32-V02-SCHEMA。0.2の64B名前slotとGR79/v1の128B材質を分離し、header320Bと旧版互換・複数clip所有契約を固定してから接続する。

- G2-GR32-V02-SCHEMA: 旧定数/hashの5498Bを内容不変でpure公開headerへ分離し、統一0.2の320B/64B submesh/64B名前slot/複数clip所有契約をNVSKELv0.mdへ固定。版別profile/count/packed節/拡張header/raw hash入力長の純検証を追加。旧0.0/0.1の単一clip/256B/hashと現reader/writerは未変更。通常/O2-NDEBUG/ASan・UBSan（LSan除外）/実MEMBER成功、独立Python4hashと原文一致確認、独立PASS。Core全体はWindows.hで未実行。record値/文字列/padding/clip所有の実読込検査はIO接続時に残る。旧名ASCIIを維持し0.2のUTF-8/NUL拒否はwriter公開前に接続する。
- Next: GR32-PRIMITIVE-DECODE。1mesh/1skin/1mesh-node/1clipを維持したままprimitiveを連結し、slot割当とReduce/Morphの資産集計を整合させる。未接続の下流は表を捨てず拒否する。

- G2-GR32-PRIMITIVE-DECODE: 2〜8primitiveを連結し、local index境界→絶対番号化、追加範囲のみ巻き順交換、所有表/初出slotとsource identity順の名前衝突解決を接続。material省略とindex0は別。Reduceの全体prefix、morphの総target数/共通幅（mesh_target_width）を分離し、全geometry後に一度だけfit/Bake。preflightも全primitiveを検査。単一primitiveは中間段階の旧空表互換、旧writerは新表を明示拒否。nativeに2/8/9・全入口・名前/順序/省略・越境・prefix4/6・fit・morph幅を登録、Windows.hで未実行。純report3mode/54JSON独立解析とlayout/wire/policy回帰成功、独立PASS。
- Next: 0.2 IO接続に向けた版別文字列/参照検証→reader→writer/cache移行。新表付き資産のcookはwriter接続まで意図的に失敗する。

- G2-GR32-V02-TEXT: minor0/1 printable ASCIIとminor2厳密UTF-8/NUL拒否、UTF-16/32とCore Charのcode unit/byte数を区別する名前codecを追加。参照差分境界・overflow/alignment・全out alias拒否・全入力先行検証で失敗時出力保持。全1,112,063 scalar/4,382,591Bを独立Python UTF-8黄金hashと照合し通常/O2-NDEBUG/ASan・UBSan（LSan除外）全3mode・実MEMBER compile成功。旧wire/layout回帰も成功、独立PASS。実reader/writerおよびWindows/Core全体は未変更・未検証。
- Next: GR32-V02-READERで版別表・予約/padding・clip別所有と名前を接続し、未対応下流への黙示的な情報欠落を防ぐ。

- G2-GR32-V02-READER: 旧0.0/0.1を維持して統一0.2の320B/表/UTF-8・予約byte・packed padding/hashを接続。submeshのVertexCount/NoShadow/boundsを保持し絶対index範囲を検査。clipごとにjoint/path重複とdurationを検査し、channel/sampleを表順一意所有、失敗Dataは非公開。未接続M9は新表/複数clipを明示拒否。pure wire/record・layout各3modeと実MEMBER成功、独立レビューPASS。手書き2clip/2submesh/UTF8と破損回帰をnative登録、独立Pythonは配置/名前/所有/時刻だけ照合。native/Core全体はWindows.hで未検証。UTF8は長さだけでなく再encodeした7byte一致も試験する。
- Next: GR32-V02-WRITER。統一0.2生成・旧空表具体化・再parse照合とcurrent writer minorによるcache移行を接続する。Resource/描画の実使用は引き続き別工程。

- G2-GR32-V02-WRITER: 1primitiveもslot名を保持し、統一0.2/320B/表/UTF-8/new hashを生成、再parseで表と名前を照合。旧空表は全index/Default slotへ具体化、静止頂点boundsは最終scale後に計算。cacheはminor2必須として旧0/1はload互換のみ。第1周で共通JSONのsurrogate未結合を発見し、pure escape/scalar部品で結合・不正拒否・4byte UTF-8を修正、第2周PASS。pure bounds/layout・JSON各3mode計6runと実MEMBER2件、wire/name回帰成功。全scalarのUTF8黄金hash、cache fixture JSON/配置も独立確認。raw/GLB→cook/parse・単一slot/Unicode名・旧cachemiss/新hit・出力保持はnative登録、Windows.hで本体/CLI未実行。M9新表guardはResource/描画接続まで維持。
- Next: GR32-RESOURCE-TABLES → COMPONENT-SLOTS → DRAW-RANGES。CPU契約と描画実装の接続を進め、Windows/GPU受入れはblockedの別gateに保持する。

- G2-GR32-RESOURCE-TABLES: SkinnedMeshResourceがsubmesh/slot名を所有し、geometry/有限bounds/頂点範囲/名前をLoad・Refreshで検査して不変leaseへコピーする。旧3引数/空表互換と、generation/非active/Unload後のsnapshot寿命を維持。新表FrameLeaseは範囲draw接続まで明示拒否し、情報を単一drawへ落とさない。純実index/metadata/layout3mode・MEMBER成功、独立PASS。native1/2/8表・不正slot/NUL・直前generation比較・明示表Unload後保持を登録、Windows.hでResource/Core/描画実行は未検証。
- Next: GR32-COMPONENT-SLOTS。材質slotの名前/index API、旧材質fallback、世代差し替え時の安全なbindingとframe値所有を接続する。

- G2-GR32-COMPONENT-SLOTS: index/名前APIと旧slot0別名を接続し、未割当slotはslot0へfallback、slot1以降のoverrideをmesh世代へ束縛。固定Materials[8]/MaterialCountをproxyへ値コピーし、SceneRevisionも材質列を反映。第1周でnative snapshotの既存null device・packet検証不足を確認し、実CPU生成をcapabilities引数へ分け、再同期/世代差し替え/破棄後の2slot保持を追加。原文のslot0/固定配列仕様へ整合させ第2周PASS。純binding3mode・MEMBER成功。native Component/packet/RevisionとGPUはWindows.hで未実行。
- Next: GR32のcook metadataへ表数量を追記し、範囲drawへ進む。palette共有は現/前フレーム2本1組の解釈を作者確認中で別taskに保留し、独立した部分を継続する。

- G2-GR32-COOK-METADATA: 再parse済みpayloadの表数量をcook結果/CLI/manifestへ保存。submesh_count/material_slot_countは組で任意、存在時は1〜8/index三角形数と照合し、旧省略はbool=false/0の未知として保持。mergeで既知数量を落とさず旧省略を捏造しない。純数量/layout3mode・MEMBER、既知/未知fixture JSON独立確認、独立PASS。native parser1/2/8・不正型/片側欠落、cook1/2/8、CLI1/1、audio merge既知保持/GLB merge省略保持を登録。Windows.hでnative/CLI/CMake/PowerShell未実行。
- Next: GR32-DRAW-RANGES。palette allocationの作者回答を待ちながら、独立した範囲draw/材質slot/NoShadowと検証へ進む。

- G2-GR32-DRAW-RANGES: immutable lease生成時に全表/名前を検証し、private appenderで1proxy/1frame leaseからsubmeshごとの範囲・材質・NoShadow commandを発行。記録前にtable/範囲/base0/component/prepared VB・IB・総数・前palette組を照合しDrawIndexed(count,start,0)、統計も範囲数へ。旧空表count0の全mesh互換、同handle別assetのupload混入拒否、tagged preparedのframe関連付けを維持。M9は表をResourceへ渡し、複数clipだけ保留。独立PASS、純range3mode/MEMBERとlayout/binding回帰成功。別exeのCPU RHI double契約に1/2/8・旧実記録・材質fallback・影・偽装拒否を登録、nativeはWindows.hで未実行。GPU readback/画素互換/M9実行は未検証。palette共有は未実装・作者回答待ちでGR32全体完了にはしない。
- Next: paletteの2本1組共有は作者判断待ち。Windows/GPU受入れも保留し、次のGR79の未決条件と共有材質wireを確認して、判断不要の準備を先に進める。

- G2-GR79-MATERIAL-WIRE: 共有128B材質recordをRendering非依存の明示little-endian codecへ固定。4StringRef/予約/flags/alpha/係数/発光の有限・範囲・Yとraw/再正規化後の65504上限を検査し、失敗時出力保持とalias拒否を保証。独立Python128B golden、通常/O2-NDEBUG/ASan・UBSan（LSan除外）3modeとMEMBER compile成功。独立レビュー第2周PASS。現container/parser/runtimeは未接続でARM/nits既定も未決。Windows/native/GPUは未実行。
- Next: 作者承認済みのGR32 palette共有。currentはcomponent・frameで1回、previousはGBufferのみ最大1回、影はprevious無し、パス順序非依存。

- G2-GR32-PALETTE-SHARING: 作者訂正のcurrent1/previousはGBufferのみ最大1に沿い、componentとepochでcurrentを共有しpreviousを遅延作成。影のpreparedは常にprevious無し、両順序で同currentを再利用。pose/handle/世代/asset実体一致、失敗時再作成禁止、全登録viewport leaseとsubmitted serialの寿命、tagged epoch/パス/使用flag照合、旧匿名跨frame互換を保持。独立ソースレビュー2周PASS。両順序・8範囲・viewport・別component/pose/世代・失敗回数・current/previous保持解放・偽装拒否を独立native契約へ追加。既存pure範囲3mode回帰PASS。新cache本体の実行はWindows.hで未検証、GPU受入れも未実施。計画の旧CreateBuffer総数2箇所を訂正。
- Next: GR32-POINT-SHADOW-BUDGET → POSE-HISTORY-GENERATION。GR32全体/GPU gateは未完了。

- G2-GR32-POINT-SHADOW-BUDGET: 点光源の各light/faceで16componentのUBO/descriptor表を共有し、submesh数で枠を増やさない。17番目は全範囲省略、失敗枠は再試行せず、epoch/handle/世代/palette/VB照合と旧匿名palette実体の区別を保持。全600枠の従来容量は不変、各face独立。実storage helperはpreviousを拒否し8current/9VBだけ設定。純16component×8範囲×6face・容量/失敗/identity/匿名テスト3mode成功、独立PASS。実Coreへ768draw/descriptor16face/binding10無しを登録、Windows.hでnative/GPU未実行。ShadowMapPass本体のUBO/faceData接続はソース確認のみ。
- Next: GR32-POSE-HISTORY-GENERATION。前姿勢の資産世代を照合し同骨数reloadの混同を防ぐ。

- G2-GR32-POSE-HISTORY-GENERATION: GT/RTの前姿勢を共通SkinnedPoseHistoryへ集約し、componentに加えてhandle/generation・immutable asset実体weak・骨数を照合。同骨数reload/別資産/不正frame/匿名をfallbackし、submesh/viewportは1回保存、一致しない混在は次frame不使用。GT直前frame/RTgapの時間条件とmesh/MegaGeometry経路は不変。2viewport8submesh・pose/資産混在・null/component不一致・gap/reset/weak寿命をnative登録、独立ソースレビュー2周PASS。Windows.h依存でnativecompile/実行/GPU未検証。
- Next: GR32の描画受入れはWindows/Vulkan gateへ残す。承認済みGR82 Stage Aの複数clip取り込み/選択を進め、旧strict入口の拒否互換を保持する。

- G2-GR82-A1-MULTI-CLIP-DECODE: 新DecodeRigGltfだけanimations>=1を受けcookへ接続。旧bytes/String/GLTFAnalyzerのstrict TwoClips/中間親拒否は維持し、現128joint/1mesh/1skin/1mesh-node/8primitive範囲を保つ。全clip順のCubic total/prefix/失敗添字・共通出力予算、全clip成功時complete。Morph rootは1回・weight channelsは各clip検査し全成功後のみ報告公開。旧設定hash/JSONキー不変。3clip(2primitive/duration2・3・4)raw/GLB/cook/parse・3本目失敗保持・Cubic全体予算/scale・Morph集計/後続失敗をnative登録、AssertEquivalentも全clip比較。pure report通常/O2/ASanUBSan(LSan除外)3mode・各19JSON計57parse・MEMBERcompile成功、fixture宣言範囲独立確認、独立PASS。実Core/decoder/cook/GLBはWindows.hで未実行。
- Next: GR82-A2-CLIP-RESOURCES。v1/restguard・M9複数clip接続は別工程。

- G2-GR82-A2-CLIP-RESOURCES: SetClipResourcesでclip列をコピー所有し、単数SetResourcesと安定した先頭GetAnimationClipを維持。index/一意完全一致name引き、空/欠落/重複拒否、全子Loaded/Valid、Unloadとcapacityメモリ計上を接続。第1周でStringViewのNUL終端比較を検出し、長さ+全codeunitへ修正。実保持NUL後B/Cと誤一致/誤重複、子内容増加時の束メモリ不変も反証追加し第2周PASS。新SkeletalAssetResourceTestをMEMBER/CTest登録、旧LifetimeTest無変更。Windows.hでnativeコンパイル/実行は未検証。M9/SkinnedMeshComponent/選択alias/v1は未変更。
- Next: ARM/nits既定は作者回答待ち。GR79 NVMESH v1の形式/検証を独立に進め、受理後の材質情報をruntimeで黙って捨てないよう接続gateを分離する。

- G2-GR79-IMPORT-POLICY-CORE: 作者追加指定のAI profile AO/metallic ignore・roughness auto、素材mode上書き、histogram Type7のp1〜p99有効幅(既定4/255)で定数判定を純処理化。診断min/max/meanは維持し、AOは1+strength*(sample-1)。emissiveFactor×strengthの非0なら換算明示必須、textureだけ/strength0は非発光。素材>asset>asset-setの選択、不正高優先値のfallback拒否、Y正規化/physical nits/codec上限を固定。第1周の中間underflowをmax成分正規化+frexp/scalbnで修正、第2周PASS。通常/O2/ASanUBSan(LSan除外)3mode・MEMBER成功、300sorted列の独立百分位照合・巨大count・極小factor回復・byte恒等を確認。JSON/asset-set/CLIと資産名/材質名診断、実画像・撮影・runtimeは未接続/未検証。
- Next: GR79 NVMESH v1の純wire検証 → reader/runtime明示gate → material設定/画像処理/診断・asset-set指定とwriterへの接続。生成元は明示profileで扱い、GLB拡張子だけで推測しない。

- G2-GR79-MESH-V1-WIRE: v0定数blockをbyte不変の純headerへ分離し、NVMESHv1/major1minor0/Header256/Material128/Cluster128/8B整列を定義。外枠のprofile/節範囲・packed/padding/FNV/予約/有限boundsとLOD0 clusterを純検証。第1周でVertexCountの絶対index上限をunique128と混同していた制限を修正し、Multi goldenもcap6へ訂正、第2周PASS。独立Python580/692/1120B、通常/O2/ASanUBSan(LSan除外)3mode・MEMBER成功、cap129/三角形128境界・Multi実index照合を追跡。旧full parser/cookerはv0のまま、v1 reader/runtime/GPU未接続。
- Next: GR79 v0/v1 readerとruntime gate。v1単材質も係数adapter未接続では受理しない。全submesh/index/clusterの所有と材質対応、unique頂点数128と任意絶対上限を分けて検査する。

- G2-GR79-MESH-V1-READER: v0の受理条件を保ちPbr既定/ARM3chへ昇格、v1を4ref/Pbr/全finite/表所有/材質対応/実indexとunique128付きで読込。全成功時だけBlob所有を公開。ModelAssetLoaderはv1(単材質含む)/N>1をログ拒否し、旧手組み空submeshの搬送は維持。pure partition3mode/MEMBER成功、wire回帰成功。独立第2周PASS、nativefixtureは4異なるpath/相対offset0,3,6,9・係数bit・寿命・不正群・runtimeguardを登録、Windows.hで未実行。v1 writer/manifest/runtime adapterは未接続。
- Next: 既存Core::Resource class/namespace衝突を別taskで修正。CookedSkeletalAssetTestの同TUincludeに既存の衝突があり、言語最小例でも拒否を確認。新V1Testはそのclassをincludeせず今回の新規衝突ではないが、bundle全体のbuildを妨げるため先に除く。S6 C++asset-set方式は作者回答待ち。

- G2-RESOURCE-IO-NAMESPACE: Core::Resource基底クラスと読込namespaceの既存衝突をResourceIOへ分離。29既存ソースは逆置換でHEADとbyte一致、基底/反射/継承実装は不変。全追跡sourceの旧namespace残存ゼロ、両include順の実native契約をMEMBER/CTest登録。独立レビューPASS、行末/diff検査正常。Windows.hでnative compileは未実行。独立言語例の両順成功は実Core成功とは扱わない。
- Next: GR79材質設定のtyped/JSON/hashと接続を具体化。G2-S6 Aは作者が追加条件付き承認、単体CLI分割前後および旧ps1 spec v1の実byte一致は未検証gateとして保持する。

- G2-S6-CLI-BYTE-SMOKE: 既存raw/texture/audio/mesh/GLB/import/骨格の7 smokeを変更せず実行するcaptureとpackage/manifestの非正規化snapshot比較を追加。既存出力拒否・case全成功/生成物必須・recipe固定/実行中変更拒否・改竄/一覧/byte差を検査。比較器10 unittest通常/O成功、独立第2周PASS。CMakeのsource列挙は分割で変わるためrecipeから除外し、rawコマンドはdriverSHAで固定。Windows実CLI/前後byte比較は未実行でMain分割gateは未達。
- G2-S6作者訂正: origin/main CookTextureAssetSet.ps1とSilverTextures/SilverGltfTexturesの2specを比較元に確定。手元確認用CookAssets.ps1/StartupMaterialsは対象外、比較元未発見の保留は解除。glTF外部ファイルとsidecarを増分印へ含める。実texture spec v1互換確認は未実行。
- Next: GR79材質設定の解決/JSON/hashへ進み、一括cook実byte受入れは別gateで保持する。

- G2-GR79-MATERIAL-SETTINGS-VALUE: 明示profileとchannel存在mask、素材>資産>既定、発光換算の素材>資産>asset-setを解決し、存在する不正下位値も先行拒否/出力保持。両面・alphaのInherit/FromSourceを区別して親強制から復帰可能。換算不在はImportEmissionの発光判定まで許容。解決済み67B canonical（未使用値/-0正規化）とdomain/長さ付きFNVを追加。独立Python golden/FNVff4637ca142e511e、通常/O2/ASanUBSan(LSan除外)3mode/MEMBER、独立5283反証PASS。旧幾何52B/hash/file/Mainはbyte不変。JSON/素材selector/file/asset-set/cook/cacheへの実接続は未実装。
- Next: GR79材質設定の厳密JSONと素材指定の照合を接続し、未知/重複/名前不一致を無言適用しない。実CLIのMain分割前gateとWindows実比較は未達のまま保持。

- G2-GR79-MATERIAL-SETTINGS-JSON: 設定block用の実JsonDocument parserを追加し資産profile/ARM各mode・constant/両面/alpha/nitsを厳密に型・範囲・未知・重複検査、全成功時だけprofile/layer置換。素材blockのprofile指定は拒否、空/省略は継承、換算不在は保持。pure ARM tokenのASCII全消費・finite0..1/非終端span/NUL/末尾余剰を通常/O2/ASanUBSan(LSan除外)/MEMBERと独立guard-pageで反証PASS。30JSON fixture構文をPython確認、native JSON MEMBER/CTest登録はWindows.hで未コンパイル/未実行。旧ParseSettings/LoadImportSettingsFile/Mainはbyte不変、sidecar非空materialの明示拒否を維持。独立レビューPASS。
- Next: 素材selectorは08:06作者回答待ち。一方glTF sourceの材質係数/発光strength/texture参照の読み取りは独立して進められる。sidecar/cook/cache/asset-setの実接続・Windows実比較・実物撮影は未完のまま保持する。

- G2-GR79-GLTF-MATERIAL-SOURCE: glTF PBR/normal/AO/emissiveFactor+KHR emissiveStrength/alpha/両面と5texture参照をdouble source値へ読み取るAPIを追加。既知field重複/型/範囲/キー・name NUL/参照範囲を検査し成功時だけ公開。texCoord0限定・textureInfo.extensions存在拒否は旧cooker同様。optional未知材質拡張はobject検査後fallback、document必須拡張gateは不変。公式schemaのnormalScale負/alphaCutoff>1/strength0以上を保持。純数値/参照/極小factorからの発光回復を通常/O2/ASanUBSan(LSan除外)/MEMBERと独立実行でPASS。nativeJson42fixture構文確認・MEMBER/CTest登録、Windows.hにより実JSONコンパイル/実行は未検証。独立レビューPASS。旧cook/profile/emission policy無変更。
- Next: GR79(6)再読でdoubleSided autoをFromSourceと名付けた値層の不整合を発見。別taskでAutoと明示し、geometry判定待ちのmodeを保持する。素材selectorは作者回答待ち、source→cookの実接続は続く工程。

- G2-GR79-DOUBLE-SIDED-AUTO: GR79(6)へ整合させprivate DoubleSidedSettingのFromSourceをAutoへ訂正。元値復帰ではなく後段の位置溶接・閉鎖性判定待ちmodeを保持する説明に修正。alphaのFromSource、enum数値1、67B canonical/FNVは不変。4source/testは逆rename+comment除去でHEADとbyte一致、通常/O2/ASanUBSan(LSan除外)3mode PASS、独立静的review PASS。geometry判定・JSON実行・cook/renderer接続は追加していない。
- Next: 素材selector回答待ちの間、ARMの画像pixelからhistogramと焼込出力を作る処理を接続する。glTF読込sourceと設定値の準備は済み、cook/cache/runtimeの受入れは未完。

- G2-GR79-ARM-PIXELS: 解凍linearRGBA8 AO(R)/MR(G,B)を実histogramへつなぎ、全定数ならARMなし、残るtextureのみ同寸法検査してfactorを焼込。省略textureは白sample/HasSourceImage=false、実測と区別。activeMR/なければAOのalphaを保持、factor1全textureで全RGBA byte不変。input/policy/factor/view/planのalias・範囲/overflow・不足出力を先行拒否し失敗保持。第1周で半端量子化の1byte下振れを発見、byte領域BakeとQuantizedScalarへ修正し、素材定数も正規化往復しない。通常/O2/ASanUBSan(LSan除外)/MEMBER成功、第2周PASS。全256値×全8mask×quarter factorsと独立混色312120例/constant768例、合成AI犬の外れ値/metallicignore/textureoverrideを確認。画像IO/NVTEX/manifest/実cook/実物/GPUは未接続・未検証。
- 作者09:00追加条件: 共通selectorをGR79/GR78 SurfaceName/GR32 slot名へ共用、未一致・同材質への名前/番号の二重指定拒否、同名GLBへ改名推奨警告。TRELLIS/Pixal素は無名1、Blender経由Material_0。元indexとslot初出index、元名と生成名は分けて扱う。
- Next: 共通resolver。raw/escape名のTCHAR幅問題も別taskへ追加。現ANSIではUTF8 bytes一致、wideには既存JSON数値tokenのcompile問題とbyte拡幅/char lexer縮約がある（実native実行でなく実ソースと純helper言語例の確認）。

- G2-MATERIAL-SELECTION-SHARED（GR79/GR78/GR32共通核）: strictUTF8/NUL拒否の一意元名・番号+期待元名をResolveMaterialSelectionへ集約。元catalogと生成slotを別domainとし、行順とidentity順を区別。未一致/曖昧/同target二重指定拒否、全catalog同名警告情報、失敗時出力保持/alias検査。第1周で後続未一致の診断に前行が残る問題を修正、回帰追加で第2周PASS。通常/O2/ASanUBSan(LSan除外)/実MEMBER wrapper compile成功、独立100000例とroot20000例の総当たり参照比較一致。nativeCore/GR79設定/GR78SurfaceName/GR32component接続は次工程で未完。
- Next: 作者10:02承認のWindows標準CIで実CLI基準採取を準備し、common resolverの設定/runtime接続も続ける。Main分割前baseline、--asset-setと旧main2specの実byte一致は未達。

- G2-S6-WINDOWS-CLI-CI: 作者10:02承認に基づきfeature限定Windows2022 workflowを準備。最小read権限/credential非永続、公式固定Vulkan SDK checksum検証+debug/copy_only導入、実Core/AssetCook/CookedMeshTestのReleaseビルド、CPU契約と既存7smoke capture、exe hash照合/明示artifact保存。YAML構造と比較器10単体は確認済み。初回run/実build/7smoke/保存物は未確認でdoingを維持。
- 独立workflowレビューPASS。alwaysログの未設定env参照を固定runner.temp/runId/attemptへ修正。実行成功の証明はpush後のrunで確認する。
- Windows CI初回run37194998794はSDK汎用名checksum APIの404で導入前停止。公式掲載の実ファイル名と固定SHAへ修正し、公式vulkanホストの実名receipt HTTP200/値一致を確認。ビルド/試験未到達、baseline未生成。

- G2-GR32-MATERIAL-SELECTION-ADAPTER: SkinnedMeshComponentのslot検索をstrict native→UTF8変換+GeneratedSlot共通resolverへ接続。元番号とは混ぜず、返るcatalog行をslot IdentityIndexへ変換。独立callback照合を廃止し、Default/一意空名/最大8/重複拒否、番号APIと世代fallbackを維持。純bindings通常/O2/ASanUBSan(LSan除外)/MEMBER wrapper compile成功、native snapshotへNUL query拒否を追加、独立review PASS。実component/native試験は未実行（現在のWindows runにはこの差分は含まれない）。公開旧helperをリポ外利用するコードは新UTF8catalog APIへの移行が必要。
- Next: GR79設定とGR78SurfaceNameのcatalog/selector接続。WindowsCIの基準採取結果も確認継続。

- G2-S6-NATIVE-TEST-COMPILE: run37195379624の実WindowsログでCore.libとAssetCook.exe生成成功を確認。CookedMeshTestはAnsiStringView==literalのtemplate推論不可(C2678)とWindows nearマクロによるlambda名消失(C2513)の2件で停止。前者を長さ3+memcmp、後者をisNearへ変更し、逆差分が親とbyte一致することを確認。値/許容幅/検証対象は維持。実再ビルド・CLI smokeは未確認のためdoing。

- G2-JSON-UNICODE-INPUT: strict UTF8 byte入口とUTF8/16/32 native検証をJsonDocumentへ追加し、lexerのchar縮約/広い空白判定/数値String型を修正。glTF/GLB/sidecar/cookのJSON入口6か所を接続、BOM/container/BIN借用/path変換は維持。第1周でescaped NUL後のTString容量拡張破損を指摘され、文字列単位のsource上限を事前scan/reserveして長いkey/value/node増加回帰を追加、第2周PASS。全Unicode scalar3幅の通常/O2/ASanUBSan(LSan除外)/MEMBER compileと独立JSON fixture確認成功。実JsonUnicodeInputTestとCI登録済み、native実行前なのでdoingを維持。Core全体のUNICODE構成成功とは区別する。
- G2-S6-NATIVE-TEST-COMPILE: run37196379322で2件修正後のCore/AssetCook/CookedMeshTestビルドと指定7CPU契約成功を確認し、この修正taskはdone。実CLIはRaw/Texture/Audio/Mesh成功、Glbのverify.ps1内Get-FileHash不在で停止。基準snapshotは未生成、Main分割gateは未達のまま。

- G2-S6-SMOKE-ENVIRONMENT: MicrosoftのPS7→Python/中間process→Windows PSのPSModulePath継承問題に沿って、driverがWindowsの子環境辞書だけから当該keyを大小文字非依存で除去。親/他変数/Skeletal memberを保持し12単体通常/-Oと独立review PASS。smoke command/fixture/生byte条件は不変、成功基準前のdriver hash更新を明記。Python UTF8 modeもCIに固定。実Glb/Import/Skeletal再実行と基準snapshotは未確認のためdoing。

- G2-JSON-UNICODE-INPUT実Windows検証: run37197864299で更新Core/AssetCookはビルド成功。JsonUnicodeInputTestのraw literalをCHECKの#式へ直接渡した箇所がMSVC C2017/C3688となったため、同一literalを局所変数へ移しmacro文字列化の対象から外す。fixture byte/期待値は不変。native試験/7smokeは未達、doingを継続。

- G2-MATERIAL-IMPORT-PLAN: 新document/元index+UTF8名catalog/解決済みplanを値所有化。幾何52Bと旧loaderの拒否を維持し、新file入口だけが材質設定を読む。全rowのARM/発光/surfaceを共通resolver1回へ渡し、名前/番号二重指定・不在を拒否。無名/Material_0・catalog順とsource順・重複診断source index・寿命/後段失敗保持・legacy/BOMをnative試験へ登録。45JSON literal構文確認、独立review PASS、非blockingの不正手組みcatalog/後続layerとsurface失敗保持も補強。実nativeは未実行のためdoing。CLI/v1 cook/cache/SurfaceName永続化は未接続。
- run37198756161でCore/AssetCook/CookedMeshTestビルドと9CPU試験成功を確認。G2-JSON-UNICODE-INPUTはnative ANSI/TCHAR経路も合格してdone（Core全体UNICODE構成は未検証）。PSModulePath修正でGlb/Importも通り環境修正taskはdone。全7smokeはSkeletalのfixture helperがextra_zero.binにfixture.bin固定needleを当てて停止し未完。出力/受入れ条件は緩めず別修正する。

- G2-S6-SKELETAL-FIXTURE-URI: 実run37198756161の最終Skeletal停止を特定。ChangeBufferUri呼出順の先行fixtureはすべてpretty fixture.bin、ExtraZeroInfluencesだけextra_zero.binで固定needleに不一致。期待URI引数（既定fixture.bin）と一意一致assertを設け、該当callだけextra_zero.binを指定。source fixture/binary/エンジン判定は無変更。実再試験まではdoing。

- run37200047966/d1307c32: 実Windows ReleaseでCore/AssetCook/CookedMeshTest、12+12比較器試験、10CPU契約、Raw/Texture/Audio/Mesh/Glb/Import/Skeletal全7CLI成功。before artifact11302304928（SHA256 7491b7178b1be87f79269624c337183629e770145b41ea6ca8477521f642d385）を外部検証storageへ保存し、79file=50package+29JSON全hash/size/一覧と2exe hashを独立検証＋root再確認。recipe31項目はgit blob直接20/WindowsCRLF11で説明可能。Main分割前gateを閉じ、材質import-plan・fixtureURI・WindowsCI基盤taskをdone。分割後byte比較、--asset-setとorigin/main2spec比較、依存hashは未完。
- Next: G2-S6-MAIN-SPLIT。argv/help/inspect外殻と単体cookサービスの境界を設け、同一79出力を実比較する。現Mainの2993行・振る舞いは基準採取時まで不変。

- G2-S6-MAIN-SPLIT: Mainを317行のargv/help/inspect外殻へ縮小し、単体cookサービスとprivate出力処理へ既存bodyを移動。公開requestは値所有、既存validation/dispatchを保持。機械的body照合・比較器12件・行末確認成功。固定before取得/recipe照合/79出力比較と日本語診断5literalのbyte照合をCIへ接続。実after検証前なのでdoing。

- G2-S6-MAIN-SPLIT受入: e8ac121ce8d2e9b92a5469ec321adb00b8a92ff3/run37203280358で実Core/AssetCook/CookedMeshTest build、11CPU、7CLI成功。保存したcandidate artifact11304570033とbeforeの全79file=50package+29JSONを独立raw byte比較し完全一致、recipe31項目・5診断literalも一致。rootで比較器を再実行して一致確認。単体分割taskをdone。--asset-set/texture v1/増分依存は別gateのまま。

- G2-S6-TEXTURE-BASELINE開始: upstream main b8c5df1のPS1/2spec/8source画像をblob固定。分割前exeとWindows PowerShell5.1を明示した専用job、2回全byte照合と10file snapshot採取器を追加。純拒否契約11件の通常/最適化とYAML parse成功。実Windows採取は未実行のためdoing。

- G2-S6-TEXTURE-BASELINE受入: 0893a213/run37205025949の実Windowsで固定旧PS1・2spec各2回cook成功、合計16texture cook。Windows PowerShell5.1.20348.5622、11入力blob/checkout、固定exe不変、8package+2manifestの2回byte一致を確認。保存artifact11303728946の全10file=296408686byteを独立検証＋root hash再確認。texture v1互換の比較元が確定、native --asset-setとの比較は次工程。

- G2-S6-TEXTURE-SPEC開始: v1の値所有parserと独立Json整数token情報を追加。旧variant/prefix/usage無視を保持、出力path安全制約とO(NlogN)重複検査、実2spec/寿命/失敗保持のnative契約を登録。単体CLI比較器12件は成功。実native parser試験前なのでdoing。

- G2-S6-TEXTURE-SPEC実検証: 441eeed/run37206606155は実buildと既存11CPU/診断byte成功、新spec試験のJson生成helperで停止（実2spec試験へは未到達）。TString::replaceがmemmove後にstrncpy_sの終端NULでsuffix先頭を潰すことをsourceで特定。fixtureだけを3区間appendへ変更し、失敗時hex記録を追加。汎用文字列修正は別taskに起票。parser/fixture taskとも実再試験までdoing。

- G2-S6-TEXTURE-SPEC/FIXTURE受入: f3100ed/run37207785735で実build、12CPU（実Silver2spec/8row全所有field・UTF8・失敗保持）、7CLI、79出力byte一致、診断5literal一致を確認。fixture修復後にparser本体無変更で全合格。両taskをdone。--asset-set実行/集約serializer/増分印は未接続、汎用String::replace修正は独立TODO。

- G2-S6-TEXTURE-BATCH開始: CLI/サービス/PS5.1集約serializer/同volume no-replace新規root公開を実装。全入力先行検査、private stage、単体manifest所有、最終cooked-only再解決を接続。固定10file×2回と全ASCII PS実probe、failure/既存root/junction/競合検査をCIへ追加。Python比較器4件成功、実native検証前のためdoing。既存root増分とproduction caller切替は後続。

- G2-S6-TEXTURE-BATCH受入: 79d019d/run37210701091の実Windowsで13CPU、単体7CLI/79byte/5診断、native2spec×2/10file、全ASCII PS5.1実probe成功。25native起動=9成功/16拒否を確認し、日本語source/cwd、junction、既存root、late画像不正、prefix衝突、公開先競合を検証。native artifact11306368465（SHA2568797453fad3cf42e8bc354300726984c24fa2a704885e97858bbe3a158ef368f）の10file296408686byteを独立全byte比較しroot再確認。新規root用v1 sliceをdone、既存root増分とglTF/sidecar stampは未完。

- G2-S6-DEPENDENCY-SNAPSHOT開始: source/glTF外部buffer・image/選択sidecarの生byte・presenceと要求/revisionを印へ集約。sidecar loaderに同じreadのRawSourceBytesを保持し、外部readerのcanonical path出力を共有。root/設定/URIの既存runtime SourceHashは不変。実file変更・不在復帰・policy・permission・失敗保持のnative契約を登録、実Windows前なのでdoing。Skip決定と既存root公開は未接続。

- G2-S6-DEPENDENCY-SNAPSHOT実検証: edc9a5c/run37213672528は実build/診断と15CPU成功、新依存試験がbuffer解決失敗で停止。コピーしたM9Skinnedに必須fixture.bin（416byte）が無いことを確認。既存skeletal試験の生成bodyをbyte書込callback付き共通headerへ機械移動し、新依存試験でも同じbufferを生成する。既存bodyの逆置換一致を確認。依存実装は変更せず、失敗時source表示を追加して再検証する。

- G2-S6-DEPENDENCY-SNAPSHOT受入: f96dc155/run37215038245の実Windowsで16CPU（snapshot/sidecar loader/外部buffer readerを含む）、単体7CLI/79byte/5診断、native2spec×2/10fileとPS5.1・Unicode・16拒否契約が成功。logs/native archive SHAと実行driverを確認。fixture補完後に依存collectorを変更せず合格しtaskをdone。Cook/Skip/Errorの共通決定、stamp永続化、既存root増分公開は別gate。

- G2-S6-OUTPUT-PACKAGE開始: 共通decisionに先立ち、読み込み済みpackageの型別照合を小さく分離。V1単一entry/数値hash/4texture形式/audio/mesh v0/骨格v0.2と6数量、全wrapper印を検証する。実単体cook、版別既存golden、padding差・失敗保持のnative契約を追加。純比較器12件成功、実Windows前なのでdoing。既存cache/Skip/path解決は変更しない。

- G2-S6-OUTPUT-PACKAGE受入: 7ba94fed/run37218137820の実Windowsで18CPU（新しい型別package検証とmesh v1拒否を含む）、7CLI/79byte/5診断、native texture2spec×2/10byte/16拒否が成功。単体cook全形式・派生画像3件・正常旧骨格版の拒否・padding印・失敗保持を実証。3 artifact ZIP SHAを確認しcandidate79file/2exeを独立検証。taskをdone。保存recordとの比較・共通Cook/Skip/Errorは未接続。

- G2-S6-CACHE-DECISION開始: 単体要求の正規化を既存private境界へ集約し、現入力からの完全な出力一覧・値所有record・共通Cook/Skip/Errorを追加。保存pathは採用せず、source/不在sidecar/他keyのaliasを先行拒否し、依存を操作前後で再採取する。全kind・外部依存・manifest無関係変更・Windows alias・途中入力変更のnative契約を登録。永続化と既存root公開は未接続、実Windows前なのでdoing。

- G2-S6-CACHE-DECISION実検証: 48bdd655/run37220986849は実buildと既存18CPU・5診断が成功、新cache試験の危険出力名拒否（line203）で停止。Windowsのabsolute化より前にraw物理名の検査を追加し、末尾dot/space等が正規化で消えてから検査される経路を塞ぐ。packageの各leaf診断とmanifest名の対称回帰を追加。判定条件は弱めず、79+10byte gateは未到達のためdoingを維持。

- G2-S6-CACHE-DECISION受入: cc9aa9e1/run37222258176で実Windows19CPU・7CLI/79byte・5診断・native2spec×2/10byte/16拒否が成功。保存record→Skip、全kind、外部依存・sidecar・他key、hardlink/case/junction、Cook/Skip中とrecord採取中の入力変更・保持を実証。実traceでa.とa空白がraw/aへ、CONがdevice pathへ、a:streamがdrive pathへ変わることを確認し、raw表記を先に拒否する修正も合格。3ZIP SHAと固定79+10出力の直接byte比較を独立検証。共通in-memory判断をdone、state永続化・既存root公開・旧CLI cache移行は未完。

- G2-S6-COOK-STYLE開始: 新規出力/cache検証10fileを規約の制御文brace/Allmanへ整理。bool識別子とheaderの明示修飾以外はbraceを除くtoken一致を確認。旧単体cookerは新設adapter範囲だけを対象とする。挙動の受入れは既存Windows gateで再確認する。

- G2-S6-COOK-STYLE受入: e3c919e9/run37224909032で実Windows19CPU、7CLI/79byte/5診断、native2spec×2/10byte/16拒否が成功。3 artifact ZIPと79+10出力の固定基準との直接byte一致を独立確認。書式と明示名修正がcook/増分判定の挙動を変えていないことを確認しtaskをdone。

- G2-S6-MODEL-NATIVE-PATH開始: source/sidecar locatorをnative pathのままfingerprint・実cook・検査・骨格decodeへ通し、文字列化を診断境界だけへ限定。日本語/非BMPの静的・骨格glTF/GLBと外部依存・sidecar・cache・失敗保持のWindows契約を追加。既存narrow APIはASCII互換として残し、argv/外部URI leaf/runtime論理pathの規約は拡張しない。実Windows前のためdoing。
- G2-S6-MODEL-NATIVE-PATH検証準備: 既存sidecar結合試験をnative path比較へ変更し、Windows対象を21CPUへ拡張。比較器12件×通常/最適化とportable native UTF8 locatorの空/日本語/非BMP/NUL/不正byte検査が成功。Linuxで全test TUをcompileする試行はWindows.h不在で未実施扱い。実Windowsと79+10byte gateは未確認。

- G2-S6-MODEL-NATIVE-PATH実検証: 4b9fd5b8/run37228182164でCore/AssetCook buildは成功、追加testのmainで局所alias TextとCore::Text、およびDetail名前空間が曖昧となりbundle compile失敗。test aliasをNativeTextへ変更し、診断encoderのnamespaceを明示する。production実装/試験値は変更せず、21CPU・79+10byteは未到達のためdoingを維持。

- G2-S6-MODEL-NATIVE-PATH受入: d2556d3c/run37229272949で実Windows build・21CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。test名衝突の修正後production実装は不変。日本語/非BMPの静的・骨格glTF/GLB、source/base/override、外部依存、共通record→Skip、lock/不在/不正locatorの拒否を実証。3ZIPのdigestと79+10固定出力の直接byte一致を独立確認しroot再実行。taskをdone。argv/非ASCII外部URI/runtime出力path拡張は含めない。

- G2-S6-STATE-CODEC開始: 永続化の前段として、owner/root/manifest bindingと共通CookOutputRecordを厳密JSONへ値所有で写す。uint64は固定hex、重複/未知field・所有衝突・過大入力を拒否する。file新規保存と既存root transactionは別taskに分け、codecの成功を上書き権限として扱わない。
- G2-S6-STATE-CODEC検証準備: JSON拒否fixtureの3文字列をC++ UCNではなくJSON escape byteへ訂正し、literalだけの独立compileでbackslash列を確認。全kind実cook recordへcodec往復を追加、22CPUへ登録。比較器12件と独立prefix順序10000例が成功。native Windows/79+10は未実行。DOM前node予算は未実装の制約として明記。

- G2-S6-STATE-CODEC受入: af63b209/run37231239763で実Windows build・22CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。全kindの実recordをJSON往復して共通Skipへ戻り、strict schema・uint64境界・scope・alias/prefix・失敗保持を実証。3ZIPと79+10固定出力の直接byte一致を独立確認しroot再実行。値codecをdone。実file保存/読込と既存root transactionは未接続。

- G2-S6-STATE-FILE開始: callerが保持するbindingとASCII絶対locatorを使い、RuntimeRoot外/同volumeをhandleの物理pathで照合する。新規tempをCREATE_NEWで排他取得し、write/flush/readback後に同じhandleでno-replace renameする案を採る。Missingは既存親の下の最終leaf不在だけ。所有handle以外をcleanupせず、production transactionへは未接続。
- G2-S6-STATE-FILE境界調整: 不在runtimeの将来8.3 aliasがstate新規fileと衝突する余地を除くため、このprimitiveはRuntimeRootも既存directory必須へ限定する。新規rootと外部stateの順序/rollbackは後続transactionで扱う。root不在はMissingではなくErrorとして保存前に止める。
- G2-S6-STATE-FILE検証準備: root不在試験は新しいstate leafを指定し、旧実装のMissing/新規保存を直接反証する形へ補強。既存file/directory/junction/sharing・実2writer・temp衝突・故障注入・orphan保持と全kind save/load→Skipを23CPUへ接続。比較器12件×通常/最適化とBOM/CRLF検査が成功。Win32 handle動作と79+10byte gateは未実行。

- G2-S6-STATE-FILE受入: 375a7dc6/run37234005425で実Windows build・23CPU・7CLI/79byte/5診断・native texture2spec×2/10byte/16拒否が成功。native handleの保存/読戻し、全kind save/load→Skip、実2writerと競合/故障時の保持を確認。short_alias_distinct=1、different_volume_checked=1で両条件も実行済み。3ZIPと固定79+10出力の直接byte一致を独立確認しroot再実行。taskをdone。production一括更新/lock/journalは未接続。

