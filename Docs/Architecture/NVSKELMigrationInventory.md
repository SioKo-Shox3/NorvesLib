# NVSKEL 0.2 / v1移行の影響棚卸し

## 目的と決定状態

これは実装前の変更箇所と受入れ試験の棚卸しであり、wire/APIの確定仕様ではない。
基準は [b2aa6ff](https://github.com/SioKo-Shox3/NorvesLib/commit/b2aa6ff708898522809f1ab9d861d81f69fdb679) のソース。
作者が確定した事項は [G2ImportDecisions.md](G2ImportDecisions.md) を正とする。

承認済み:

- GR32のsubmesh/材質slotとGR82 Stage Aの複数clipを、同じNVSKEL 0.2へ統一する
- 0.2のheaderは320Bを計画基準とし、異なる内容のminor=2を並立させない
- 現行0.0/0.1は128関節。0.2も128を維持し、256はStage Bのv1と同時
- v1のSkeletonIdにrest poseを含めない。clip作成時restを保持し、現在restとの差が許容超過なら既定拒否する
- 明示許可でのみrest差を許し、差量/超過関節/閾値と許可した事実を報告する

2026-10-04追加承認:

- S3(a): NVMESH v1の128B材質レコード/v0併読を採用
- S7: 1mesh/Nprimitiveを採用
- ARM/emissiveの既定、S5/S6、BVH/FBXは引き続き未選定

0.2/v1はまだ実装済みではない。以下の整理だけで現行の受理条件を変更しない。
オオカミ/シ者の作り分けや共通土台の張り直しは未定であり、移行の前提にしない。

## GR32 / GR82 Stage Aの変更箇所

| 境界 | 現行ソースの事実 | 0.2へ向けた確認点 |
|---|---|---|
| wire定数 | `Library/Core/Public/Asset/CookedSkeletalFormat.h` はmajor=0、minor=0/1、header=256、頂点64B | 旧0.0/0.1の解釈と、新0.2の320B/表/整列/hash範囲を版別に整理する。現行定数だけを置換しない |
| glTF primitive | `SkeletalGltfDecode.cpp` のParsePrimitiveは1mesh/1primitive、PrimitiveInfoも単一範囲 | 承認済みS7 Aに沿って複数primitiveの所有範囲、頂点/indexの再基準化、材質slot対応を確定する |
| glTF clip | 同ファイルのParseAnimationContractは1clip限定 | Stage Aで複数clipを検証・保持する単位、clip名/重複名と失敗診断を整理する |
| cooked生成 | `Tools/AssetCook/MeshCooker.cpp` はClips.size()!=1を拒否する。一方、後続集計・書込にはclipループがある | ガード除去だけで完了にせず、各clipのchannel/sample/string範囲とcount積/和の検証を再確認する |
| cooked読込 | `Library/Core/Private/Asset/CookedSkeletalLoader.cpp` はminor0/1、256B、clipCount==1を要求 | 旧形式の拒否条件を保持し、新形式だけの表と複数clip検査を分離する |
| runtime資産 | `Library/Core/Public/Animation/SkeletalAssetResource.h` はmesh/skeleton/単一AnimationClipを保持 | clip集合の所有権、名前等による選択、旧呼出しとの互換を設計する |
| component | `SkinnedMeshComponent.cpp` のTick/EvaluatePose/HasValidPoseResources/HasCurrentPoseは単一GetAnimationClipに依存 | clip変更時の時間・dirty状態・pose cache・resource参照の更新を漏らさない |
| 起動側接続 | `Game/GameApplicationHandler.cpp` はClips[0]を渡す | 無言の先頭固定を残さず、既定シーンを維持できる明示選択へ移す |
| 材質共有 | 現骨格形式には新しい共有材質レコード/slot表が無い | 承認済みS3(a)と0.2のslot表/v1のMATSを混同せず具体化する |

GR32とGR82はdecoder/cooker/loaderを共有する。GR32でprimitive受理だけを先行する中間状態と、
Stage Aで複数clipまで到達した状態を区別する。GR79の材質方針を飛ばして、v1のMATS等を先に実装しない。

## 版と受理条件の試験マトリクス

- 旧0.0/0.1: 従来のgolden/hash/256B headerと既存資産を読める。旧形式の複数clipや壊れた範囲を、新形式の緩和で通さない
- 新0.2: 320B header、submesh/index/slot/clip/channel/sample/stringの範囲・整列・所有関係を検査する
- GR32中間段階: 複数primitiveの成功/不正slot/壊れたindex範囲を追加し、まだ未接続ならTwoClips拒否を維持する
- Stage A到達段階: 複数clipの読込/cook/parse/resource選択を成功ケースへ変え、clip間の範囲混入・重複/欠落名・空clipを検査する
- S7 Aに従い、複数meshの拒否を維持する
- 128の境界と129の拒否は0.2でも維持し、v1の256試験と混ぜない
- Reduce/Bake/Dropの数量・予算・失敗診断を、primitive/clipの集計拡張でも維持する。現在1clip/1mesh前提のJSON検証も同時に見直す
- 失敗時は部分的な資産/出力を成功として返さない。cache鍵には形式・policy・変更後の設定を含める

`Test/Core/Rendering/GLTFSkeletalAnalyzerTest.cpp` は現在TwoSkins/TwoPrimitives/TwoClipsを拒否している。
将来の段階に応じて期待値を分ける。すべてを一律成功へ変えることも、Stage A後に古いTwoClips拒否を残すこともしない。
`Test/Core/Asset/CookedSkeletalAssetTest.cpp` の往復/破損/CLI出力保持を同じ段階へ揃える。

## GR82 Stage B: 作成時restが現在どこで失われるか

現行の `Library/Core/Public/Resource/SkeletalGltfData.h` では、jointはName/ParentIndex/InverseBindMatrix、
clipはName/DurationSeconds/Channelsのみを持つ。clip作成時restのsnapshotは保持されていない。

`SkeletalGltfDecode.cpp` のParseNodeContractはnodeのlocal/global変換を計算するが、
それらを作者時点のrestとしてSkeletalJointやclipへ引き渡す経路は無い。
ExtractSkeletonは名前/親/inverse-bindを出力し、SkeletonResourceもそのjoint配列を受け取る。
このままwireだけ分離すると、古いclipがどのrestに対して作られたかを復元できない。

必要な追跡経路:

1. import: 元の作者時点restを、node/joint対応とともに取得する
2. 単位・座標変換: 現在骨格とclip snapshotが同じ比較空間になるよう、両者への変換規約を固定する
3. cook: 各clipから、作成時restの不変snapshotを失わず参照/保存できるようにする
4. parse: snapshot欠落/非有限/対応不能を構造検証で拒否する
5. bind: SkeletonId一致だけでは通さず、snapshotと現在restの差を検査する
6. reload/cache: asset identity/versionも確認し、rest変更後は現在データへ再束縛・再検査する

snapshotのwire配置、共有方法、比較空間、閾値の既定はStage Bの仕様で固定する。
この棚卸しでは具体的なrecordやAPIを先に作らない。現在のinverse-bindだけから作者時点restが揃ったと仮定しない。
G3のイベント・マーカー・ソケットの置き場を確保することはv1化の理由として残る。

## v1 rest安全契約の必須試験

| 入力/操作 | 必須の期待結果 |
|---|---|
| 同じSkeletonId、同じrest | 通常束縛が成功 |
| 同じ名前/階層、並進・回転・scale差が許容超過 | 既定拒否。差量/該当joint/閾値を報告 |
| 上記の古いclipに全joint Translationキーがある | Translationの有無で安全検査を免除せず拒否 |
| 明示の許可がある | 許可した事実と差量を報告して通す |
| 許容内の数値差、q/-qの同値 | 不必要に拒否しない |
| snapshot欠落・不正・joint対応不能 | 比較不能を互換成功へ変えない。正しい作成元からの再cookが必要 |
| cook→parse→bind | 作者時点snapshotを失わず同じ判定になる |
| 同SkeletonIdだが別asset version/rest、reload後 | 古い束縛/cacheを再利用して比較を迂回しない |

骨長の比率補正やretargetを将来追加できることは、現時点の不一致を無条件に許可する理由にならない。

## 次の適用条件

S3(a)/S7は2026-10-04に承認済み。まずGR32の受理profileと共通材質/0.2の変更表を確定し、関連taskの完了条件へ展開する。
この棚卸し自体はwire/API確定仕様ではなく、後続の実装仕様と試験で具体化する。

## GR32最初のCPU契約

SkeletalSubMeshはIndexStart/IndexCount/MaterialSlotのplain型とし、SkeletalGltfDataにSubMeshesと
名前を持つMaterialSlotsを追加する。Rendering/RHI型をAssetデータへ混ぜない。
ResolveSkeletalSubmeshLayoutは非0・3の倍数・u32内の総index数と、1〜8のpacked範囲/slot境界を検査する。
両表空だけは旧データとして全index・slot0の1件へ解釈する。片側空は不正で、実配列は書き換えない。
slot名の妥当性/重複、実indexの頂点境界、bounds、wireのreserved/flagsは後続の検査責務とする。
この部品の追加時点ではdecoder/writer/loader/resource/描画の受理範囲は変えない。

後続の設計注意:
- 0.2の64B名前slotとGR79/v1の128B PBR材質を同一recordとしない。0.2の予約48Bを同じminorの途中で別用途へ転用しない
- GR79のNVMESH v1とGR80のcluster拡張も、初回schema時にrecordSize/版の関係を固定する
- Nprimitive化でReduceの頂点prefixとMorphのprimitive別target総数/mesh-level target幅を区別し、JSONも合わせる
- 0.2 writerへの切替後、旧0.0/0.1は通常loadで読めても最新cookのcache hitとして残さない
- 下流未接続の複数submeshを黙って単一材質へ平坦化しない。段階移行中は未対応箇所を明示拒否する

## GR32 Nprimitive decodeの中間段階

1mesh/1skin/1mesh-node/1clipを維持して2〜8primitiveを連結し、所有SubMeshes/MaterialSlotsを返す。
各primitiveのlocal indexをそのprimitiveのvertex数で検査してからbaseVertexを加え、巻き順交換も追加範囲だけへ行う。
scale/fitは全primitiveを連結した後に一度だけ解決する。新表を保存できない旧writerは新表付きdataを明示拒否する。
現段階の1primitiveは従来の空表互換を維持し、既存cookのバイトを変えない。1primitiveの名前slot具体化は0.2 IO接続と同時に行う。

material省略は実material[0]とは別の既定材質identity。参照されたsource materialごとに最初の出現順でslotを作る。
未指定はDefault、名前無しはMaterial_<source index>を候補とする。名前が参照材質間で重複する場合は
source identity順に「候補 [source index]」（未指定は[default]）を割り当て、元候補/確定名と衝突した場合は_1以降を付ける。
一意の元候補はそのまま保持する。大小文字/Unicode正規化はせず、埋込NULは拒否する。
primitiveの順序を入れ替えても、同じ参照材質集合のsource identityと確定名の対応は変えない。

ReduceのTotalVertexCount/ProcessedVertexCount/FailedVertexIndexは連結資産全体の座標で報告する。
morphはprimitiveごとのdelta数を各primitiveのvertex数で検査し、mesh内のtarget幅は共通とする。
対象を持たないprimitiveはゼロdelta相当として許す。DroppedMorphTargetCountは全primitiveの除去target総数、
MorphTargetWidth（JSONのmesh_target_width）はmesh/node初期weightsおよびweight animationの幅であり、別に報告する。

raw/legacy/file/GLB、2/8primitive、名前衝突/順序/省略、local index越境、全体Reduce prefix、全体fit、
morphの総数/幅/対象なしprimitiveと幅不一致、および旧writerの出力保持をnative試験へ登録する。
Windows.hに依存するためこのLinux環境では登録・静的照合に留まり、実行済みの純JSON/範囲検査とは区別する。
