# 取り込み設定 v1

GR78のソース隣設定は <source>.import.json（例: Dog.glb.import.json）を正本とし、cook/looseで共用する。
値・JSON解析・設定file選択/読込・正規化hash・下記のgeometry変換を共有し、静的glTF/GLBのAssetCookとlooseロードへ接続する。骨格には下記の一様scale/fitを適用する。CLIの明示指定/require/disabledを共有loaderへ接続し、skipはcook前照合へ接続し、inspectは下記の無変換診断modeへ接続する。

## 共通の配置と責務

- Library/Core/Private/Resource/ImportSettings.h: 値・列挙・判定結果
- ImportSettings.cpp: 副作用の無い値検証と正規化bytes
- ImportSettingsJson.cpp: 実JsonDocumentによる共有解析
- AssetCookLibとCoreのlooseが共有するため、解析をTools専用には置かない。CoreからToolsへ逆依存しない
- 解析は成功時だけ出力を置換する。失敗時は前の設定を保持する

## JSON契約

rootはobject、version: 1が必須。省略時にv1と推測しない。
未知field・同名fieldの重複・型違いはエラー。JSONは既存JsonDocumentの厳密な文法に従い、JSON5やコメント構文は追加しない。
メモはmeta.noteへ書く。

- units: scale（有限で正、既定1）、またはfit: {axis: up/forward/longest, meters: 有限で正}
  - scaleとfitは同時指定不可。fitを指定したらaxis/metersは両方必須
  - units省略/空objectはscale=1
- axes: up/forwardは +X/-X/+Y/-Y/+Z/-Z、既定+Y/+Z
  - 同じ座標軸の組合せは符号が違っても不可
  - mirrorXはbool、既定false
- origin: modeはkeep/bounds_center/bounds_bottom_center/surface_centroid/custom、既定keep。surface_centroidは静的メッシュで使用でき、骨格では拒否する
  - offsetは有限number3要素、既定[0,0,0]
  - custom以外の非ゼロoffsetは拒否する
- mesh: windingはkeep/flip/auto、既定keep。flipU/flipVはbool、既定false
- meta: generator/noteはstring、seedは0〜2^53−1の整数。すべて任意、hashへ含めない

`<source>.import.json` に取り込み設定として置いてよいのは、値を変えると再 cook が必要になる取り込み時の変換だけとする。`version` は必須の形式情報、`meta` は任意の付随情報である。

- `units`・`axes`・`origin`・`mesh`: 上記の geometry の変換。
- `material`・`materials`: 元の材質の取り込み設定。実装済みで、詳細は [MaterialImportDocument.md](MaterialImportDocument.md) を参照する。
- 骨格の一様 scale: 独立した節はなく、`units` の scale/fit が骨格にも効く。詳細は「骨格の一様scale/fit」を参照する。
- `clip`: クリップの取り込み時の補正。詳細は「clip 節（GR78・GR82）」を参照する。
- `skinnedLod`: スキンメッシュの LOD の取り込み設定。詳細は「skinnedLod 節（GR63）」を参照する。
- `audio`: 音の取り込み設定（G16 の GR95）のための予約。現状は省略または空 object だけを受理し、非空または他の型は `UnsupportedFeature` で拒否する。
- `repair`・`lod`・`collision`: 予約。省略または空 object だけを受理し、非空または他の型は `UnsupportedFeature` で拒否する。実装する GR で許すキーと既定値を決め、黙って無視しない。

イベント・ソケット・IK・モディファイア・ループの範囲・ルート移動の正本は取り込み設定に置かず、`*.animmeta.json` などの実行時 sidecar（GR11）を正本とする。実行時に調整する値を import.json に置くと、調整のたびに再 cook が必要になり、cook 時と実行時に正本が二つできるためである。`clip` の `loop`・`analysis` は cook 時の自動判定の下書きであり、ループの範囲・ルート移動の正本ではない。

旧 `ParseSettings` の経路（`LoadImportSettingsFile` を使う NVMESH v0 の cook・実行時の glTF 読み込み・旧 NVSKEL decoder の自動探索）は `clip` を実装しておらず、非空の `clip` を `UnsupportedFeature` で拒否する。

typed値ではfit未使用時のFitMetersは1、fit使用時のScaleは1に固定する。
正の極端な有限値の読み込みは許すが、geometry変換時の表現可能性は後続の変換層が検証する。

## clip 節（GR78・GR82）

`clip` はクリップの取り込み時の補正を指定する。`--rig-split`、asset-set の skeletal（`kind: model`、`RigSplitCook`）と animation（`RigSingleCook`）が読み込む。キー・既定値は `RigClipAnalysisOptions`（`Library/Core/Private/Animation/RigClipAnalysis.h`）に対応する。

| キー | 型・値 | 省略時の既定 | `--rig-split` の CLI |
|---|---|---|---|
| `root_joint` | string。空でない関節名、UTF-8 で 4096 byte 以下 | 骨格の根の最初の子。子が無ければ根 | `--root-joint <name>` |
| `loop` | `"auto"`・`"true"`・`"false"` の文字列、または bool | `auto` | `--loop auto\|true\|false` |
| `loop_threshold` | number。有限で 0 以上 | 0.01 | なし |
| `authored_fps` | number。有限で正 | 補正なし | `--authored-fps <fps>` |
| `source_fps` | number。有限で正 | 補正なし | `--source-fps <fps>` |
| `time_scale` | number。有限で正 | 1 | `--time-scale <factor>` |
| `analysis` | bool | true | `--no-analysis`（false に当たる） |

未知のキー・重複・型違いはエラーとする。空 object（`"clip": {}`）は省略と同じ。

- fps の補正には `authored_fps` と `source_fps` の両方が必要で、片方だけの指定は `InvalidValue`。時刻の倍率は `time_scale * authored_fps / source_fps`。
- `analysis: false` は単独でだけ指定でき、他のキーと一緒なら `InvalidValue`。CLI の `--no-analysis` も他の解析の指定と併用できない。
- CLI と sidecar の同じキーの値が異なる場合は、対応する理由の名前 `clip_conflict_<キー>` で拒否する。黙ってどちらかを優先しない。値が同じなら受け、片方だけにあるキーはその値を使う。CLI のキーは、既定と同じ値を指定した場合も含め、指定したものを明示とみなす。
- 衝突の理由は `clip_conflict_root_joint`・`clip_conflict_loop`・`clip_conflict_loop_threshold`・`clip_conflict_authored_fps`・`clip_conflict_source_fps`・`clip_conflict_time_scale`・`clip_conflict_analysis`。`loop_threshold` に CLI はなく、ライブラリの呼出側が既定と異なる値を渡したときだけ衝突する。
- 重ねた結果で解析が無効なのに解析の指定がある場合は `clip_analysis_disabled`、fps の片方だけが決まる場合は `clip_fps_pair` で拒否する。
- `root_joint` の名前が骨格に無い場合は、`--rig-split` と asset-set の model では `split_clip_root_not_found`、asset-set の animation では `clip_root_not_found` で拒否する。
- リターゲット（`--retarget-clip`、asset-set の animation の `skeleton_path`/`role_profile`）は非空の `clip` を `retarget_clip_settings` で拒否する。補正は Role Profile の `output_fps`・`loop_mode` が持つ。
- 静的モデル（NVMESH v1 の clustered cook）は非空の `clip` を拒否し、材質の計画の読み込みで `static_model_clip_settings` を返す。旧経路は上記のとおり `UnsupportedFeature` で拒否する。

## skinnedLod 節（GR63）

`skinnedLod` はスキンメッシュの LOD の取り込み設定を指定する。読むのは `--rig-split` と asset-set の skeletal（`kind: model`、`RigSplitCook`）だけである。

| キー | 型・値 | 省略時の既定 |
|---|---|---|
| `parts` | 整数 2 つの配列 `[min, max]`。1 ≤ min ≤ max ≤ 256 | `[16, 64]` |
| `targets` | number の配列、1〜8 個。先頭は 1、狭義の減少、各値は有限で 0 より大きく 1 以下。LOD ごとの三角形数の LOD0 に対する比 | `[1, 0.4, 0.12, 0.03]` |
| `thetaRef` | number。有限で 0 < θ ≤ π（3.141592653589793）。スキンの変位の誤差に使う基準の回転角（rad） | `0.5` |

節が object なら取り込みを要求する（`bRequested = true`）。空 object（`"skinnedLod": {}`）も既定値で要求する。節を省略した場合は要求しない。既定値はすべて仮であり、データに応じて変える。

object 以外は `InvalidType`、未知のキーは `UnknownField`、重複は `DuplicateField`。各値の型違いは `InvalidType`、配列の要素数・値の範囲・順序の違反と整数でない `parts` は `InvalidValue`。解析は成功時だけ出力を置き換える。
旧 `ParseSettings` の経路では空 object だけを受理し、非空または他の型は `UnsupportedFeature` で拒否する。

節があると cook が部位に分けて LOD を焼き、NVSKEL の SkinnedMesh に `LODS`・`PART` を足す。書式は [RigSplitV1Wire.md](RigSplitV1Wire.md) を参照する。
節が無い cook の出力と `source_hash` は従来と同じである。節を変えると sidecar の bytes が `source_hash` に入っているため再 cook になる。
`skinnedLod` は geometry の正規化 bytes（下記の 52 byte）には入れず、hash は sidecar の bytes と cook が足す算法の印で決まる。

## 正規化bytes

現在の固定長は52byte。structのメモリ表現やpaddingを直接hashしない。
整数はlittle endian、doubleはIEEE754 binary64のbit列をlittle endianにする。全ての-0を+0へ正規化する。

| offset | 内容 | byte |
|---:|---|---:|
| 0 | schema version u32 = 1 | 4 |
| 4 | Scale binary64 | 8 |
| 12 | Fit: None=0 / Up=1 / Forward=2 / Longest=3 | 1 |
| 13 | FitMeters binary64 | 8 |
| 21 | Up: +X=0 / -X=1 / +Y=2 / -Y=3 / +Z=4 / -Z=5 | 1 |
| 22 | Forward（同じaxis番号） | 1 |
| 23 | mirrorX 0/1 | 1 |
| 24 | Origin: Keep=0 / BoundsCenter=1 / BoundsBottomCenter=2 / SurfaceCentroid=3 / Custom=4 | 1 |
| 25 | OriginOffset X/Y/Z binary64 | 24 |
| 49 | Winding: Keep=0 / Flip=1 / Auto=2 | 1 |
| 50 | flipU 0/1 | 1 |
| 51 | flipV 0/1 | 1 |

無効なtyped値は正規化結果Size=0。JSONの空白・キー順・meta変更はbytesへ影響しない。
sidecar無しと明示的な恒等設定は呼出側で区別する。無しなら従来source_hashへ何も連結しない。
cook algorithm versionと長さ前置きのhash連結は後続の接続層で行う。

### clip 節の正規化 bytes

geometry の bytes とは別の列とする。`clip` 節が無い場合と空 object の場合は空（0 byte）で、hash に何も連結しない。同じ入力に対して、clip 用の連結が追加される前と `source_hash` は同じである。

並びは little endian とし、double は IEEE754 binary64 の bit 列、`-0` は `+0` に正規化する。

1. schema version: u32 = 1。
2. `Keys`: u32。指定されたキーのビットを立てる。`root_joint = 1 << 0`、`loop = 1 << 1`、`loop_threshold = 1 << 2`、`authored_fps = 1 << 3`、`source_fps = 1 << 4`、`time_scale = 1 << 5`、`analysis = 1 << 6`。
3. 以下は指定されたキーだけをビットの順に並べる。
   - `root_joint`: u32 の byte 長 + UTF-8 の bytes。
   - `loop`: u8（auto = 0 / true = 1 / false = 2）。
   - `loop_threshold`・`authored_fps`・`source_fps`・`time_scale`: それぞれ binary64。
   - `analysis`: u8（0 / 1）。

`AppendClipImportSettingsHash`（公開ヘッダ `Library/Core/Public/Resource/ClipImportSettings.h`）は、非空の節があるときだけ、u64 LE の byte 長と上記 bytes を FNV-1a 64 で連結する。`--rig-split`・asset-set の model は解析の設定の後に、asset-set の animation は依存の指紋（sidecar の bytes を含む）の後に連結する。節を変えると `source_hash` が変わり、再 cook になる。空の節が clip 用の追加連結を生じさせないことと、animation の依存指紋に含まれる sidecar の bytes の変更が別に影響することは区別する。

## 共有geometry変換（GR78）

ImportTransformはinterleaved頂点の位置float3・法線float3・UV float2とuint32 indexを受けるprivateな無確保処理。
strideとfield offsetでlayoutを渡し、他のfieldやpaddingは変更しない。頂点/indexの全spanは非交差で、呼出中は不変入力とする。

- up/forwardをtargetの+Y/+Zへ写す右手直交基底を作り、mirrorXはtarget Xへ別に掛ける
- units.fitは軸変換後boundsのup=Y、forward=Z、longest=最大extentでmetersへ合わせる。選択軸のextentが0なら失敗
- bounds_centerはbounds中心、bounds_bottom_centerはXZ中心とminYを原点へ移す。surface_centroidは面積重みの重心を原点へ移す（方式は「表面重心の精度」を参照）。総面積0はDegenerateSurfaceで失敗する
- custom.offsetは軸/scale適用後に加える平行移動量。ソース内のpivot指定ではない
- 演算はdouble。P' = (R×P − Pivot) × Scale + Offset と評価し、巨大な中間平行移動行列を作らない。floatへは最後に一度だけ変換する
- 法線は正の一様scaleで逆転置のscale成分が正規化時に消えるため、直交rotation/mirrorを適用して単位化する。ゼロ/非有限法線は拒否
- UVはflipU/flipVに応じ1−u / 1−v。windingは既存glTF→engineの反転後を基準とし、keepは追加反転なし、flipは常に追加、autoはmirrorX時だけ追加する
- 全layout/index/位置/変換後のfloat表現可能性を検証してから書込passへ進む。失敗時は頂点もindexも非変更。位置の非ゼロ値が乗算またはfloat変換で0へunderflowする場合も拒否する
- 非整列byte storageはmemcpyで読み書きする

骨格のIBM/animation等の一様scaleと静的cooker/looseへの挿入は、下記の接続契約に従う。

表面重心は静的メッシュのcookとlooseで有効。骨格ではSupportsSkeletalScaleImportがOrigin == Keepを要求するため拒否する。

## 表面重心の精度

原点は軸/鏡像変換後・scale前の空間で求める。適用順はbounds_centerと同じく、
P' = (R×P − Pivot) × Scale + Offset とする。ImportTransformOutcome::SurfaceAreaは同じ空間の総面積を返す。

- ExactSumは2^-1074を単位とする2の補数の多倍長整数。uint64のlimb 34本（2176ビット）の固定配列で、binary64の全範囲と2^64回分の加算の余裕を持ち、追加確保はない。加算順に依らず正確な和を保持し、doubleへ戻すときだけround-to-nearest-evenで1回丸める。
- SurfaceWeightは各面の外積の各成分を6つの積の正確な和から1回だけ丸め、w = hypot(|cx|, |cy|, |cz|)を求める。float×floatの積はdoubleで正確で、wは面積の2倍。頂点の巡回は同じ6項の並べ替え、巻きの反転は符号の反転になるため、wはビット単位で一致する。
- 各軸のモーメントΣ w·vは、各面の3頂点について積とfmaの残差の2項を正確に足す。float座標の範囲では残差も正確に表せる。3·Σwも同じ方式で正確に足す。
- 原点 = round(Σ w·v) ÷ round(3·Σw)、総面積 = round(Σw) / 2。面の順・面内の巡回・巻きの反転によらず、原点・総面積・変換後の頂点がビット単位で一致する。
- 総面積0（全ての面が縮退）はDegenerateSurfaceで失敗し、bounds_center等へ置き換えない。和や丸めの結果が非有限、またはdoubleへ戻せない場合はUnrepresentableで失敗する。失敗時は頂点もindexも変更しない。

和そのものの誤差は0。累算後の丸めはΣw・Σ w·v・3·Σwの各1回と原点の最後の割り算1回で、
原点の相対誤差の目安は約1.5 ulp。相殺で原点が小さくなる場合も、モーメントを丸めるときの相対誤差0.5 ulpが
そのまま効くため、原点の大きさに対して数ulpとなる。ここでroundはdoubleへの丸めを表す。

これとは別に残る誤差は、面ごとの重みwの丸め（外積の各成分で0.5 ulp、hypotで約1 ulp）。
各面の重みの相対誤差をε（数ulp）とすると、原点の移動は高々ε × max|v − 原点| ≲ ε × 2·max|座標|。
したがって絶対誤差の上限の目安は「最大の座標の大きさ × 数ulp」となる。

表面重心は、面順・巡回・巻きの反転で原点・総面積・変換後頂点のビットを変えない。
総面積0では失敗し、頂点/indexを変更しない。

## 設定file選択とsource_hash連結の基盤

ImportSettingsFileはソース全名へ.import.jsonを追加する。拡張子を置き換えない。
明示OverridePathがあればそちらを使用し、不在なら失敗。bRequiredも不在を拒否する。
bDisabledは読込をせず恒等設定へ戻すが、required/overrideと同時指定した場合はInvalidOptions。
自動探索のfile不在だけを恒等として扱い、権限/状態取得失敗・directory・dangling linkは無視しない。

設定fileは通常fileか確認し、最大1MiBまで読む。UTF-8 BOMは除去し、NUL・切断/読込時の増大・不正JSON・不正設定を拒否する。
失敗時はLoadedImportSettingsを変更しない。確保例外は伝播する。
symlink先の通常fileは許すが、競合書換えに対するatomic snapshotやfilesystem sandboxではない。
読み込んだPath/bPresentを返すので、接続層が採用設定をログへ示せる。

AppendImportSettingsHashは既存FNV-1a64のstateへ、u64LEの長さ52、正規化52byte、u32LEのImportCookAlgorithmVersion（現在1）を連結する。
既存hash値自体をもう一度byte化してhashし直す処理ではない。
sidecar無しでは設定やalgorithm versionに依らず既存hashをそのまま返す。
有りの不正typed設定は失敗する。将来algorithmの意味が変わるときはversionを更新する。

Windowsの自動不在判定はraw system errorのFILE_NOT_FOUND/PATH_NOT_FOUNDだけを許可する。
MSVCのfile_type::not_found/errc写像は不正名やnetwork path障害も含むため、それだけでは設定無しと判断しない。
根拠: Microsoft STLのfilesystem（symlink_statusはraw errorを保持）とxfilesystem_abi.hの不在分類。
https://github.com/microsoft/STL/blob/main/stl/inc/filesystem
https://github.com/microsoft/STL/blob/main/stl/inc/xfilesystem_abi.h


## 静的cook/looseへの接続

静的モデルのgeometry抽出後、cluster/bounds生成前に同じLoadImportSettingsFileとApplyImportTransformを呼ぶ。
sidecar無しまたはdisabledは変換を一切呼ばず、旧geometryとsource_hashを保持する。
明示的な恒等sidecarはgeometryを共有変換へ通し、設定有りとしてhashを連結する。

CookGltfToNvmeshとprivate CPU staging入口の末尾optional optionsでoverride/disabled/requiredを渡せる。
通常GLTFAnalyzerのsync/asyncは自動探索を使う。これらは同期呼出中にだけoptionsを借用し保持しない。
既存の静的モデルcookは自動sidecarを読み、下記のCLI設定選択で上書き/必須化/無効化できる。
cooker結果は採用path/present/settings hashを所有し、Mainはsidecar診断を出す。looseはgltf_import_settingsへ同じ情報を記録する。

新しいmodel source_hashは元source/外部bufferの旧FNV stateに設定を連結する。
geometryだけの設定では埋込み画像bytesとtexture source_hashは変えない。
NVMESH形式と既存材質は変更しない。失敗時はcandidateを公開しない。
設定適用後に既存float演算でbounds/coneが非有限になる場合も、looseは公開前に拒否する。

glTF/GLBとも、設定無し/恒等/disabledではpayloadを維持する。cookとlooseで設定適用後の頂点・index・boundsを揃え、
metaや書式だけの変更ではhashを変えない。不正scale・fit退化・巨大scaleの拒否時は既存出力を保持する。

自己完結GLB/data URIのcook APIはsource locatorが空でも従来どおり使用できる。
その場合auto探索は設定無しとし、旧payload/hashを保つ。requiredは失敗し、明示overrideは読み込む。
通常file loader自体の「空source/overrideはInvalidOptions」という契約は変更しない。

## 設定正本の上書き防止

静的モデルcookは、採用したsidecarとmodel package/manifest/派生texture packageが同じfileを指す出力を拒否する。
書込開始前にweakly_canonicalによるpath一致、Windowsの通常case比較、filesystem::equivalentによるhardlink一致を検査する。
安定したfilesystem上の誤指定防止であり、競合したpath差替えを完全に防ぐsandboxではない。

## 静的モデルの最小設定例

Dog.glbの隣のDog.glb.import.jsonへ置く例。sourceの上/前方向が+Y/+Zである場合に限る。
高さを0.6mへ合わせ、XZ中心と足元を原点へ移す。headの向きの符号はモデルを見て作者が決める。

```json
{
  "version": 1,
  "units": { "fit": { "axis": "up", "meters": 0.6 } },
  "axes": { "up": "+Y", "forward": "+Z", "mirrorX": false },
  "origin": { "mode": "bounds_bottom_center" },
  "mesh": { "winding": "auto", "flipU": false, "flipV": false }
}
```


## 骨格の一様scale/fit

現行NVSKEL形式を変えず、骨格decoder（raw GLB/JSON・legacy String・AnalyzeSkeletal）とcookerへ同じ設定を適用する。
対応は正の一様scale/fitだけ。up=+Y/forward=+Z、mirrorX=false、origin=keep、winding=keep、UV反転無しを要求し、
他の指定は未対応として拒否する。表面重心は引き続き拒否する。

- 頂点位置XYZをs倍
- 全jointの逆バインド行列の平行移動（flat index12〜14）をs倍
- 全clipのTranslationチャンネルのXYZをs倍。time/WとRotation/Scaleチャンネルは不変
- MeshNodeGlobalTransformの平行移動をs倍。全行列の線形成分は不変
- 法線・UV・joint名/階層/indices/weightsは不変

これは行ベクトル規約の単位変更に相当する。変換中のdataは未公開candidateなので、途中のoverflow/underflowで部分結果を返さない。
scale/fitの倍率式とfloatへの安全な倍率変換は共有primitiveを使う。
fitはmesh-nodeの線形変換後に測ったasset内boundsのextentを使い、平行移動は除外する。
Scene上の所有Entityのworld変換は含めない。非identityのmesh-node scaleも二重適用しない。

cookerは設定を一度だけ読み、同じLoadedImportSettingsをdecoderへ渡してgeometryとhashの食違いを防ぐ。
Core decoderは呼出中のみ借用し、結果へ設定pointerを保持しない。引数省略時はsource隣を自動探索する。
空source locatorの自己完結入力はauto設定無し、cookerのrequiredは失敗、明示overrideは採用する。
設定無し/disabledは旧payload/hashを維持する。Mainの骨格出力も採用sidecarへのaliasを事前に拒否する。

raw/legacy/loose/GLB/cookerで設定の意味を揃え、恒等/disabledやmetaのみの変更ではpayload/hashを変えない。
fitではmesh-node scaleを考慮し、対象外のfieldと拒否時の既存出力を保持する。
一様scaleは各時刻の変形結果へ1回適用し、法線を変えない。CLIは設定正本をpackage/manifestで上書きする要求を拒否する。

## CLIの設定選択

- model cookの `--import-settings <file>` / `--import-settings=<file>` は、source隣の自動探索より優先する。相対pathはCLIの作業directory基準。
- `--require-sidecar` は採用設定の存在を必須にする。明示overrideとの併用は可能。
- `--no-sidecar` は設定fileを一切読まず、従来のbytes/hashを使用する。override/requireとは相互排他。
- 重複指定、空path、値付きboolean flag、model以外のkindでは拒否する。static/skeletalとも同じ共有loaderへ渡す。
- 明示overrideもpackage/manifest/派生textureの出力alias保護対象。`--skip-if-unchanged` は下記のcook前照合を使用する。`--inspect <file>` は独立した無書込診断modeで、cook用引数とは混在させない。

## cook前の入力照合

`FingerprintModelCookSource` はJSON/container、buffers、設定の検証とhash計算だけを行う。
本cookと同じ元source全量/BOM・外部buffer全量（宣言後余剰を含む）・正規化設定を使用し、
設定file選択のhelperも共通化する。staticでは同じ材質参照resolverからembedded画像の
index/論理path/format/source hashを取り出し、すべて所有metadataにする（画像bytesは返さない）。
geometryの展開・変換・cluster化・画像decode・package生成は行わない。
そのため成功は入力modelのcook可能性を保証せず、cache missなら通常cookの検証が必要。
失敗時は既存結果を保持する。sourceと設定は安定したfilesystemを前提とし、競合変更のatomic snapshotではない。

既存native束へglTF/GLB/data URI・設定有無/変更/meta/disabled・外部buffer余剰・骨格・
embedded画像のcook結果との照合と失敗保持を追加する。これらはWindows依存により未実行で、
native実行でのhash同値確認は別途必要。

## 未変更モデルの再cook省略

modelの `--skip-if-unchanged` は上記preflightの後、既存manifestの同じlogical_path/kind/variantを照合する。
source hashだけでなくformat・entry name/type・cooked version・要求package pathも一致が必要。
実packageを全量読み、NVPACK構造/entry hash/実payload hashとNVMESH/NVSKELの内部構造を確認する。
embedded画像は期待される全画像のmanifest/source hash/format/default variant/派生package pathを同様に検査し、
NVTEX内部構造とRGBA8/colorspaceも確認する。画像やmodelが欠損/破損していればmissとして通常cookへ戻す。
外部画像はもともとmodel cookの生成対象ではないため、この単体cache照合には含めない。

hit時だけ `AssetCook skipped unchanged model` を表示して終了コード0を返す。
geometry cook/cluster/画像decode/package書込/manifest書込は行わず、更新時刻も維持する。
miss時の既存manifest保護や出力path衝突規約は従来のcookへ引き継ぐ。
FNV64と安定したfilesystemを前提とするcache照合であり、競合変更/攻撃的hash衝突への認証機構ではない。

cache本体とCLIはWindows依存により未実行。

## 幾何診断の計測契約

診断kernel `InspectGeometry` は、変換前のpositions/normalsと三角形indexからbounds・軸長・最長軸、
頂点/三角形/位置溶接後頂点/成分/ゼロ法線の数を返す。最長軸が同値ならX→Y→Zの順で選ぶ。
溶接はfloatの数値完全一致（+0/-0は同じ）で、epsilon丸めはしない。成分は三角形の頂点共有による連結を数え、
未参照の溶接頂点も孤立成分に含める。ゼロ法線は3成分が厳密に0の場合だけで、微小値はゼロ扱いしない。
sourceは不変で、独立workspaceを用いてsort/union-by-rankとpath compressionを行う。
非有限値、不正index、workspace不足/重複は拒否し、失敗時の計測結果を保持する。

診断kernelは下記CLIから共有する。

## 画像診断の計測契約

`InspectImage` はPNG/JPEGを既存stbで展開し、幅/高さ/decoded channel数/8または16bitと、
各channelの整数sampleのmin/max/meanを返す。16bit PNGは16bitとして取得し、8bitへの縮約や
色空間の線形化を行わない。JPEGはstbのdecoded色成分に対する統計で、元の圧縮成分の統計ではない。
画像は1枚ずつ処理し、展開前のinfoで最終pixel payloadの512MiB上限を検査する。stb内部の作業領域/IDAT inflateを含む
総メモリの制限や敵対入力のsandboxではない。decoded storageは呼出中だけ所有し、
失敗時の出力は保持する。対応外形式は明示拒否し、source画像に書き込まない。


## --inspect の使用と範囲

`AssetCook --inspect <model.gltf|model.glb>` または `--inspect=<file>` は、cookの必須引数なしで診断する独立mode。
余分な引数/cook出力先/設定指定との混在は拒否する。source、外部buffers/画像だけを読み、sidecarは読まない。
package、manifest、sidecarの作成/変更は行わず、全診断が成功した場合だけstdoutへ値を出す。

現行static profileと同じ単一mesh/primitiveのindexed TRIANGLES、float POSITION/NORMAL/UV、u16/u32 indicesが対象。
sparse/圧縮/必須拡張/複数primitiveなどの未対応入力は明示拒否する。診断空間はmesh-localの変換前であり、
node/worldやimport設定の適用後ではない。全images/materialsを配列順に診断し、未使用項目も含める。
外部画像は共有URI/containment規則でsource隣から読み、PNG/JPEGだけを診断する。

材質はcore PBRのbaseColor/metallic/roughness/emissive、alphaMode/alphaCutoff/doubleSided、normal scale/occlusion strength、
任意のKHR_materials_emissive_strength係数を原値/規定値として表示する。描画やcookへの材質取り込みを有効化する変更ではない。
他拡張の見た目全体を再現する診断やglTF完全validatorではない。
値域と規定値はKhronosの [material schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json) と
[PBR schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.pbrMetallicRoughness.schema.json) に合わせる。
alphaCutoffは1超も受理し、normal scaleは有限の負値もそのまま表示する。

+Yを仮のupとした水平X/Zのforward候補を軸長順で2つ示すが、符号はmanualと明記する。
stdoutのimport.json叩き台は恒等設定のみで、実寸と向きは作者の目視/指定で埋める。source隣へ自動保存しない。

native統合はWindows.hにより未確認、実物4本も未配置のため未照合。
