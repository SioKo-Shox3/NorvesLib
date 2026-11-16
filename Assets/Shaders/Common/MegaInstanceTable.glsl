#ifndef NORVES_MEGA_INSTANCE_TABLE_GLSL
#define NORVES_MEGA_INSTANCE_TABLE_GLSL

// ========================================
// MegaGeometry のインスタンスの表とメッシュの表、その読み方
//
// インスタンスの表は描く順の行ごとの値（変換・前の変換・メッシュの表の番号・最初のワークグループ・色味・種）だけを持ち、
// メッシュごとに決まる値（LOD の球・クラスタ配列・材質の区間・頂点とインデックスの基点・BVH・ページの表の先頭）はメッシュの表に置く。
// 読み手はどちらの表も直接読まず、LoadMegaInstance(index) で両方を合わせた MegaInstance を受け取る。
// どちらも device-local の常駐の表で、インスタンスの表は席の表（席ごと 144 B）から GPU が組む（mega_scene_scatter.comp）。
//
// 使う側は #include の前に MEGA_INSTANCE_BINDING（インスタンスの表）と MEGA_MESH_BINDING（メッシュの表）を定める（set は 0）。
// ========================================

#ifndef MEGA_INSTANCE_BINDING
#error "MEGA_INSTANCE_BINDING を定めてから Common/MegaInstanceTable.glsl を include すること"
#endif
#ifndef MEGA_MESH_BINDING
#error "MEGA_MESH_BINDING を定めてから Common/MegaInstanceTable.glsl を include すること"
#endif

// C++ の MegaGeometryPass::MegaMeshFlagTwoSided と一致する材質の印
const uint MEGA_MESH_FLAG_TWO_SIDED = 1u;

// インスタンスの表の1要素（C++ の MegaGeometryPass::GPUMegaInstance と一致。144 バイト）
struct MegaInstanceRow
{
    mat4 world;
    mat4 previousWorld;   // 直前のフレームの変換（velocity 用）
    uvec4 info;           // x = メッシュの表の番号, y = 最初のワークグループの通し番号
                          // z = 色味（RGBA8。R が下位 8 bit。白は色を変えない）, w = 個体差の種
};

// メッシュの表の1要素（C++ の MegaGeometryPass::GPUMegaMesh と一致。80 バイト）
struct MegaMeshEntry
{
    vec4 lodSphere;       // LODの選択に使うメッシュ共通の境界球（ローカル。w<=0ならクラスタごとの中心で選ぶ）
    uvec4 clusterInfo;    // x,y = クラスタ配列のデバイスアドレス（下位・上位）, z = クラスタ数, w = 主のカメラの LOD の許容の下限（画素。float のビット。0 なら無い）
    uvec4 drawInfo;       // x = 材質の区間の番号, y = 頂点の基点（塊の先頭から。頂点単位）, z = インデックスの基点（塊の先頭から。インデックス単位）, w = 材質の印（bit 0 = 両面）
    uvec4 bvhInfo;        // x,y = BVH の節の配列のデバイスアドレス（下位・上位。BVH が無ければ 0）, z = 節の数, w = ページの表の先頭（このメッシュのページの範囲の先頭）
    vec4 boundsSphere;    // メッシュ全体の締まった境界球（ローカル。w<=0 なら無い）。インスタンスと範囲の判定が拡大率を掛けて使う
};

// 読み手が受け取るインスタンスの値（2 つの表を合わせたもの）
struct MegaInstance
{
    mat4 world;
    mat4 previousWorld;   // 直前のフレームの変換（velocity 用）
    vec4 lodSphere;       // LODの選択に使うメッシュ共通の境界球（ローカル。w<=0ならクラスタごとの中心で選ぶ）
    uvec4 clusterInfo;    // x,y = クラスタ配列のデバイスアドレス（下位・上位）, z = クラスタ数, w = 最初のワークグループの通し番号
    uvec4 drawInfo;       // x = 材質の区間の番号, y = 頂点の基点（塊の先頭から。頂点単位）, z = インデックスの基点（塊の先頭から。インデックス単位）, w = 材質の印（bit 0 = 両面）
    uvec4 bvhInfo;        // x,y = BVH の節の配列のデバイスアドレス（下位・上位。BVH が無ければ 0）, z = 節の数, w = ページの表の先頭（このメッシュのページの範囲の先頭）
    vec4 boundsSphere;    // メッシュ全体の締まった境界球（ローカル。w<=0 なら無い）
    float mainLodFloor;   // 主のカメラの LOD の許容の下限（画素。0 なら無い。MegaMeshMaterial::MainLodFloorPixels）
    uint tint;            // 色味（RGBA8）
    uint variationSeed;   // 個体差の種
};

layout(std430, set = 0, binding = MEGA_INSTANCE_BINDING) readonly buffer MegaInstanceRowBuffer
{
    MegaInstanceRow megaInstanceRows[];
};

layout(std430, set = 0, binding = MEGA_MESH_BINDING) readonly buffer MegaMeshBuffer
{
    MegaMeshEntry megaMeshes[];
};

// 色味の RGB が白か（白なら掛けずに元の色をそのまま使い、ビット単位で今と同じにする）
bool MegaTintIsWhite(uint tint) { return (tint & 0x00FFFFFFu) == 0x00FFFFFFu; }
// 色味の RGB（[0,1]）
vec3 MegaTintRgb(uint tint) { return unpackUnorm4x8(tint).rgb; }
// index 番目のインスタンスの色味だけを読む（メッシュの表を引かない）
uint LoadMegaInstanceTint(uint index) { return megaInstanceRows[index].info.z; }
// 種から [0,1) の位相（上位 24 bit。風の位相など、頂点アニメの instanceParams へ渡す値に使う）
float MegaVariationPhase(uint seed) { return float(seed >> 8u) * (1.0 / 16777216.0); }

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
    instance.drawInfo = mesh.drawInfo;
    instance.bvhInfo = mesh.bvhInfo;
    instance.boundsSphere = mesh.boundsSphere;
    instance.mainLodFloor = uintBitsToFloat(mesh.clusterInfo.w);
    instance.tint = row.info.z;
    instance.variationSeed = row.info.w;
    return instance;
}

#endif // NORVES_MEGA_INSTANCE_TABLE_GLSL
