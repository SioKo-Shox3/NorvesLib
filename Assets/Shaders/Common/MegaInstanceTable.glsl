#ifndef NORVES_MEGA_INSTANCE_TABLE_GLSL
#define NORVES_MEGA_INSTANCE_TABLE_GLSL

// ========================================
// MegaGeometry のインスタンスの表とメッシュの表、その読み方
//
// インスタンスの表は席ごとの値（変換・前の変換・メッシュの表の番号・最初のワークグループ・「見えた」印の先頭）だけを持ち、
// メッシュごとに決まる値（LOD の球・クラスタ配列・材質の区間・頂点とインデックスの基点・BVH・ページの表の先頭）はメッシュの表に置く。
// 読み手はどちらの表も直接読まず、LoadMegaInstance(index) で両方を合わせた MegaInstance を受け取る。
//
// 使う側は #include の前に MEGA_INSTANCE_BINDING（インスタンスの表）と MEGA_MESH_BINDING（メッシュの表）を定める（set は 0）。
// ========================================

#ifndef MEGA_INSTANCE_BINDING
#error "MEGA_INSTANCE_BINDING を定めてから Common/MegaInstanceTable.glsl を include すること"
#endif
#ifndef MEGA_MESH_BINDING
#error "MEGA_MESH_BINDING を定めてから Common/MegaInstanceTable.glsl を include すること"
#endif

// インスタンスの表の1要素（C++ の MegaGeometryPass::GPUMegaInstance と一致。144 バイト）
struct MegaInstanceRow
{
    mat4 world;
    mat4 previousWorld;   // 直前のフレームの変換（velocity 用）
    uvec4 info;           // x = メッシュの表の番号, y = 最初のワークグループの通し番号, z = 「見えた」印の先頭, w = 予約
};

// メッシュの表の1要素（C++ の MegaGeometryPass::GPUMegaMesh と一致。64 バイト）
struct MegaMeshEntry
{
    vec4 lodSphere;       // LODの選択に使うメッシュ共通の境界球（ローカル。w<=0ならクラスタごとの中心で選ぶ）
    uvec4 clusterInfo;    // x,y = クラスタ配列のデバイスアドレス（下位・上位）, z = クラスタ数, w = 予約
    uvec4 drawInfo;       // x = 材質の区間の番号, y = 頂点の基点（塊の先頭から。頂点単位）, z = インデックスの基点（塊の先頭から。インデックス単位）, w = 予約
    uvec4 bvhInfo;        // x,y = BVH の節の配列のデバイスアドレス（下位・上位。BVH が無ければ 0）, z = 節の数, w = ページの表の先頭（このメッシュのページの範囲の先頭）
};

// 読み手が受け取るインスタンスの値（2 つの表を合わせたもの）
struct MegaInstance
{
    mat4 world;
    mat4 previousWorld;   // 直前のフレームの変換（velocity 用）
    vec4 lodSphere;       // LODの選択に使うメッシュ共通の境界球（ローカル。w<=0ならクラスタごとの中心で選ぶ）
    uvec4 clusterInfo;    // x,y = クラスタ配列のデバイスアドレス（下位・上位）, z = クラスタ数, w = 最初のワークグループの通し番号
    uvec4 drawInfo;       // x = 材質の区間の番号, y = 頂点の基点（塊の先頭から。頂点単位）, z = インデックスの基点（塊の先頭から。インデックス単位）, w = 「見えた」印の先頭
    uvec4 bvhInfo;        // x,y = BVH の節の配列のデバイスアドレス（下位・上位。BVH が無ければ 0）, z = 節の数, w = ページの表の先頭（このメッシュのページの範囲の先頭）
};

layout(std430, set = 0, binding = MEGA_INSTANCE_BINDING) readonly buffer MegaInstanceRowBuffer
{
    MegaInstanceRow megaInstanceRows[];
};

layout(std430, set = 0, binding = MEGA_MESH_BINDING) readonly buffer MegaMeshBuffer
{
    MegaMeshEntry megaMeshes[];
};

// index 番目のインスタンスの値を、インスタンスの表の行とメッシュの表から組み立てる
MegaInstance LoadMegaInstance(uint index)
{
    const MegaInstanceRow row = megaInstanceRows[index];
    const MegaMeshEntry mesh = megaMeshes[row.info.x];
    MegaInstance instance;
    instance.world = row.world;
    instance.previousWorld = row.previousWorld;
    instance.lodSphere = mesh.lodSphere;
    instance.clusterInfo = uvec4(mesh.clusterInfo.xyz, row.info.y);
    instance.drawInfo = uvec4(mesh.drawInfo.xyz, row.info.z);
    instance.bvhInfo = mesh.bvhInfo;
    return instance;
}

// index 番目のインスタンスの最初のワークグループの通し番号（平らな判定がワークグループの持ち主を二分探索で探すときに読む）
uint LoadMegaInstanceFirstGroup(uint index)
{
    return megaInstanceRows[index].info.y;
}

#endif // NORVES_MEGA_INSTANCE_TABLE_GLSL
