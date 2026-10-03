# 取り込み設定 v1

GR78のソース隣設定は <source>.import.json（例: Dog.glb.import.json）を正本とし、cook/looseで共用する。
値・JSON解析・設定file選択/読込・正規化hash・下記のgeometry変換を共有し、静的glTF/GLBのAssetCookとlooseロードへ接続する。骨格経路とCLIの明示指定/skip/inspectは後続。

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
- origin: modeはkeep/bounds_center/bounds_bottom_center/custom、既定keep。surface_centroidは名前を予約するが、現在はUnsupportedFeatureで明示拒否する
  - offsetは有限number3要素、既定[0,0,0]
  - custom以外の非ゼロoffsetは拒否する
- mesh: windingはkeep/flip/auto、既定keep。flipU/flipVはbool、既定false
- meta: generator/noteはstring、seedは0〜2^53−1の整数。すべて任意、hashへ含めない
- repair/lod/material/collision/clip: 現在未実装。省略または空objectだけ受理し、非空指定/他の型はUnsupportedFeature
  - 実装を追加するGRで許可するfieldと既定値を定める。黙って無視しない

typed値ではfit未使用時のFitMetersは1、fit使用時のScaleは1に固定する。
正の極端な有限値の読み込みは許すが、geometry変換時の表現可能性は後続の変換層が検証する。

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
| 24 | Origin: Keep=0 / BoundsCenter=1 / BoundsBottomCenter=2 / SurfaceCentroid=3（予約・拒否） / Custom=4 | 1 |
| 25 | OriginOffset X/Y/Z binary64 | 24 |
| 49 | Winding: Keep=0 / Flip=1 / Auto=2 | 1 |
| 50 | flipU 0/1 | 1 |
| 51 | flipV 0/1 | 1 |

無効なtyped値は正規化結果Size=0。JSONの空白・キー順・meta変更はbytesへ影響しない。
sidecar無しと明示的な恒等設定は呼出側で区別する。無しなら従来source_hashへ何も連結しない。
cook algorithm versionと長さ前置きのhash連結は後続の接続層で行う。

## 検証範囲

ImportSettingsValueTestは固定bytesの既知値、全軸組合せ、非有限/非正値、列挙範囲外、矛盾、-0と各field変更を検査する。
通常・最適化・ASan/UBSan（LeakSanitizer除外）およびMEMBERのWerror compileを実行済み。
ImportSettingsJsonTestは44のJSON fixtureを登録。fixture自体のJSON文法は別途確認した。
実JsonDocumentを含むsource/testはWindows.h依存でcompile停止しており、native解析試験は未実行。
この設定単体の試験だけでfile I/Oやcook/loose統合が通ったという意味ではない。

## 共有geometry変換（GR78）

ImportTransformはinterleaved頂点の位置float3・法線float3・UV float2とuint32 indexを受けるprivateな無確保処理。
strideとfield offsetでlayoutを渡し、他のfieldやpaddingは変更しない。頂点/indexの全spanは非交差で、呼出中は不変入力とする。

- up/forwardをtargetの+Y/+Zへ写す右手直交基底を作り、mirrorXはtarget Xへ別に掛ける
- units.fitは軸変換後boundsのup=Y、forward=Z、longest=最大extentでmetersへ合わせる。選択軸のextentが0なら失敗
- bounds_centerはbounds中心、bounds_bottom_centerはXZ中心とminYを原点へ移す。surface_centroidは未対応で、ParseSettingsではUnsupportedFeature、変換入口ではUnsupportedOriginとして拒否する
- custom.offsetは軸/scale適用後に加える平行移動量。ソース内のpivot指定ではない
- 演算はdouble。P' = (R×P − Pivot) × Scale + Offset と評価し、巨大な中間平行移動行列を作らない。floatへは最後に一度だけ変換する
- 法線は正の一様scaleで逆転置のscale成分が正規化時に消えるため、直交rotation/mirrorを適用して単位化する。ゼロ/非有限法線は拒否
- UVはflipU/flipVに応じ1−u / 1−v。windingは既存glTF→engineの反転後を基準とし、keepは追加反転なし、flipは常に追加、autoはmirrorX時だけ追加する
- 全layout/index/位置/変換後のfloat表現可能性を検証してから書込passへ進む。失敗時は頂点もindexも非変更。位置の非ゼロ値が乗算またはfloat変換で0へunderflowする場合も拒否する
- 非整列byte storageはmemcpyで読み書きする

実ImportTransformTestは48の軸/鏡像組合せの体積符号、3種類fit、bounds/足元原点、custom移動、
法線/UV、layout・index・NaN/inf/overflow/underflowと失敗時非変更、非整列storageと未指定field保持を確認する。
通常・最適化・ASan/UBSan（LeakSanitizer除外）を実行済み。
骨格のIBM/animation等の一様scaleは後続。静的cooker/looseへの挿入は下記の接続契約に従う。

表面重心は作者承認により後段へ分離した。極端値の精度確認が完了するまでは名前を認識して明示拒否し、
bounds_center等へ無言で置き換えない。候補実装は履歴に残るが、現在の変換処理からは除外している。

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

ImportSettingsHashTestの3つの既知state/期待値、無し不変、値・algorithm変更、-0、無効設定を通常・最適化・sanitizerで確認した。
hash試験とfile試験のMEMBER compileはWerrorで成功。
実file loaderはJsonDocument→Windows.hに依存しており、本体compile/実file試験は未実行。
file/hash API単体の実行確認範囲と、下記の静的cook/loose接続の未実行試験を区別する。

Windowsの自動不在判定はraw system errorのFILE_NOT_FOUND/PATH_NOT_FOUNDだけを許可する。
MSVCのfile_type::not_found/errc写像は不正名やnetwork path障害も含むため、それだけでは設定無しと判断しない。
根拠: Microsoft STLのfilesystem（symlink_statusはraw errorを保持）とxfilesystem_abi.hの不在分類。
https://github.com/microsoft/STL/blob/main/stl/inc/filesystem
https://github.com/microsoft/STL/blob/main/stl/inc/xfilesystem_abi.h
Windows不正名の実I/O回帰は登録のみで未実行。raw error判定のconstexpr境界は実file-test MEMBER compileで検証する。


## 静的cook/looseへの接続

静的モデルのgeometry抽出後、cluster/bounds生成前に同じLoadImportSettingsFileとApplyImportTransformを呼ぶ。
sidecar無しまたはdisabledは変換を一切呼ばず、旧geometryとsource_hashを保持する。
明示的な恒等sidecarはgeometryを共有変換へ通し、設定有りとしてhashを連結する。

CookGltfToNvmeshとprivate CPU staging入口の末尾optional optionsでoverride/disabled/requiredを渡せる。
通常GLTFAnalyzerのsync/asyncは自動探索を使う。これらは同期呼出中にだけoptionsを借用し保持しない。
CLIの新規フラグはまだ追加していないが、既存の静的モデルcookは自動sidecarを読む。
cooker結果は採用path/present/settings hashを所有し、Mainはsidecar診断を出す。looseはgltf_import_settingsへ同じ情報を記録する。

新しいmodel source_hashは元source/外部bufferの旧FNV stateに設定を連結する。
geometryだけの設定では埋込み画像bytesとtexture source_hashは変えない。
NVMESH形式と既存材質は変更しない。失敗時はcandidateを公開しない。
設定適用後に既存float演算でbounds/coneが非有限になる場合も、looseは公開前に拒否する。

ImportStaticIntegrationTestを既存束へ登録した。glTF/GLB、無し/恒等/disabledのpayload同値、
scale/fit/axes/原点/UV/windingのcook-loose頂点・index・bounds一致、meta/書式のhash不変、
値変更・override/required・表面重心/不正scale/fit退化/巨大scale拒否と既存出力保持を検査する。
実cooker/loose/新統合試験はWindows.hでcompile停止しており、登録試験は未実行。
純粋な共有変換・hashの成功をnative統合の成功として扱わない。

自己完結GLB/data URIのcook APIはsource locatorが空でも従来どおり使用できる。
その場合auto探索は設定無しとし、旧payload/hashを保つ。requiredは失敗し、明示overrideは読み込む。
通常file loader自体の「空source/overrideはInvalidOptions」という契約は変更しない。

## 設定正本の上書き防止

静的モデルcookは、採用したsidecarとmodel package/manifest/派生texture packageが同じfileを指す出力を拒否する。
書込開始前にweakly_canonicalによるpath一致、Windowsの通常case比較、filesystem::equivalentによるhardlink一致を検査する。
安定したfilesystem上の誤指定防止であり、競合したpath差替えを完全に防ぐsandboxではない。

AssetCookGlbSmokeへsidecarをpackage/manifestにした拒否とJSON bytes保持、通常出力成功、
派生image0 packageからsidecarへのhardlink（対応filesystemのみ）を登録する。
実Mainから同じpath判定関数のtextを切り出し、Linux上の実filesystemで通常・最適化・ASan/UBSanを実行した。
これはMain全体やWindows CLIの実行ではない。native smokeはCMake/Windows環境未整備のため未実行。
CMake CREATE_LINKは3.14以降の公式機能を使い、hardlink不能の場合はそのcaseだけを明示skipする。
https://cmake.org/cmake/help/latest/command/file.html#create-link

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
