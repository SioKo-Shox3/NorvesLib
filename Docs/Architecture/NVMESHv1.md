# NVMESH v1：共有材質と将来のcluster拡張

## 現在の接続範囲

CookedMeshWireFormat/Validationは、外枠とLOD0 clusterをCoreのWindows依存から分離した検証部品。
現ParseCookedMeshとcookerは引き続きv0だけを受理・生成する。v1のruntime受理はまだ公開しない。
外枠の成功は、頂点/index・submesh所有・材質参照の意味やruntime対応を保証しない。

## 固定配置

- magic=NVMESHv1、VersionMajor=1、VersionMinor=0
- Header=256B、Vertex=32B、Submesh=64B、Material=128B、Cluster=128B、StringRef=16B
- headerのoffsetはv0と同じ。節順はsubmesh/material/cluster/string/vertex/index、8B整列
- 節間paddingは0。末尾はindex節終端と一致し、余剰byteを許さない
- FNV1a64のpayload hashは最初のsubmesh節（offset256）から末尾まで、paddingを含む。header自体は対象外
- 頂点/index/submesh/material/clusterは非空。indexは三角形数、submesh/cluster数は三角形数以下
- materialの未使用entryは外枠では禁じない。参照範囲・所有関係はreaderの意味検証で確認する
- cluster設定は現行のalgorithm=1/version=0/maxTriangles=128/maxVertices=128/flags=0
- header Flagsと予約byteは0。将来の機能は現在の検証では明示拒否する

MaterialはCookedMaterialFormatV1の共有128B配置をそのまま使う。
係数・alpha・両面などを保持するreaderとruntime adapterが揃うまで、パスだけ読んで他の情報を捨てる受理は行わない。
新規glTFのARM/nits方針はMaterialImportPolicyに従い、旧v0材質の昇格とは区別する。

## Cluster128の予約

0〜79は従来配置を維持する。80以降はGR80のため初版から確保する。

- 80/84/88 SelfBoundsCenter、92 SelfBoundsRadius
- 96/100/104 ParentBoundsCenter、108 ParentBoundsRadius
- 112 ParentError、116 GroupId、120 Reserved（u64）

現在はLOD0だけを受理し、LODLevel/LODError/ParentStart/ParentCountは0、80〜119もbyte単位で0を要求する。
拡張値の意味を推測して適用せず、将来LODの値が来たらUnsupportedFeatureにする。Reserved72/120とFlags68は0必須。
VertexOffset=0。IndexOffset/Countは絶対index列の三角形範囲で、IndexCountは1〜128三角形。
VertexCountは0（未指定）またはglobal vertex数以下の絶対index上限。unique頂点数ではないため128で制限しない。実indexがこの上限未満か、unique頂点数がalgorithmの128以内かはfull readerで別々に検査する。
MaterialIndexはmaterial数未満。所属submeshとの一致・全clusterの連続所有は次のreader接続で検証する。
Bounds/Coneは有限、radiusは非負。契約上0のfieldは純検証の出力へ複製しない。

## v0互換

CookedMeshFormatV0の定数blockはbyte単位で不変のまま純headerへ移した。
v0は旧magic/version、Material64/Cluster80、submesh/material各1の外枠を維持する。
旧full readerは無変更。純cluster部品も旧版の0件range/未指定VertexCountを勝手に新規則へ狭めない。
旧v0を将来の共通値へ昇格するときは、従来の見えを維持する値を別途明記する。

## 検証と残作業

独立Python struct.packで作った580B(v0)/692B(v1)/1120B(v1・2材質)をgoldenとして使う。
型/版/サイズ、予約、overflow、範囲、整列/packing、padding、hash、浮動小数、LOD0の拒否、失敗時出力保持と領域重複を検査する。
通常/O2-NDEBUG/ASan・UBSan（LSan除外）とMEMBER wrapperを実行し、CTestへ登録する。
Windows/Core全体・full v1 reader/writer・runtime/GPUはこの検査の対象外。

次の接続では、v0/v1 reader、4材質参照、N submeshのindex/cluster所有と材質対応、v0昇格、runtime N>1の明示拒否を揃える。
v1単材質も係数を受け渡すadapterができるまではruntime受理を開かない。
