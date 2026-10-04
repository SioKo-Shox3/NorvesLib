# NVMESH v1：共有材質と将来のcluster拡張

## 現在の接続範囲

CookedMeshWireFormat/Validationは、外枠とLOD0 clusterをCoreのWindows依存から分離した検証部品。
ParseCookedMeshはv0/v1を受理する。cookerは引き続きv0を生成し、v1のruntime受理はまだ公開しない。
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
MaterialIndexはmaterial数未満。所属submeshとの一致・全clusterの連続所有はfull readerで検証する。
Bounds/Coneは有限、radiusは非負。契約上0のfieldは純検証の出力へ複製しない。

## v0互換

CookedMeshFormatV0の定数blockはbyte単位で不変のまま純headerへ移した。
v0は旧magic/version、Material64/Cluster80、submesh/material各1の外枠を維持する。
旧v0読込の受理条件は維持し、成功値だけ共有材質へ昇格する。純cluster部品も旧版の0件range/未指定VertexCountを勝手に新規則へ狭めない。
旧v0を将来の共通値へ昇格するときは、従来の見えを維持する値を別途明記する。

## 検証と残作業

独立Python struct.packで作った580B(v0)/692B(v1)/1120B(v1・2材質)をgoldenとして使う。
型/版/サイズ、予約、overflow、範囲、整列/packing、padding、hash、浮動小数、LOD0の拒否、失敗時出力保持と領域重複を検査する。
通常/O2-NDEBUG/ASan・UBSan（LSan除外）とMEMBER wrapperを実行し、CTestへ登録する。
Windows/Core全体・full v1 reader/writer・runtime/GPUはこの検査の対象外。

v0/v1 reader・4参照・表所有・v0昇格・runtime拒否は下記の接続範囲。writerと材質runtime adapterは残る。
v1単材質も係数を受け渡すadapterができるまではruntime受理を開かない。

## full readerの接続

v1は4つのtexture参照と共有Pbr recordを保持する。string節は既存のprintable ASCII・安全な論理path検査を使う。
空StringRefはoffset/lengthとも0を要求する。共有record codecのbyte範囲検査より厳しい、containerのcanonical path条件である。
submeshは全index/clusterを表順に隙間・重複なく分割し、clusterのMaterialIndexは所属submeshと一致する。
すべてのindexはglobal vertex数未満で、非0のsubmesh/cluster VertexCountを絶対上限として満たす。
unique頂点数128は実index集合から別に検査する。VertexCountが129以上でも、unique数が上限内なら正当である。
頂点属性・bounds/cone・材質の有限性/予約/数値範囲を検証し、全成功時だけSourceBlobと値を公開する。
文字列はSourceBlobを保持して借用するため、元の入力配列が破棄されてもGetStringで取得できる。

v0はBaseColor=1、emission=0、Metallic/Roughness=-1、AO/NormalScale=1、Opaque/片面、wireDefaultLit=0へ昇格。
ARM参照があれば従来どおり3ch使用flagを立てる。新規AI素材のmetallic ignoreを過去のv0へ遡及しない。

ModelAssetLoaderはv1（単材質も含む）と複数submesh/materialをログ付きで拒否する。
v1係数をpathだけへ落とす受理はしない。従来テストの手組みv0・空submesh表の搬送互換は維持する。
共通材質→runtimeとv1 manifest/cookerの接続は残る。材質情報のCPU読込成功を描画成功と扱わない。

pure partition試験は通常/O2/ASan・UBSan（LSan除外）を実行。
CookedMeshV1Testへv0昇格、係数bit/4参照/Blob寿命、N表と不正所有、runtime拒否をMEMBER/CTest登録するが、Windows依存でnative未実行。
