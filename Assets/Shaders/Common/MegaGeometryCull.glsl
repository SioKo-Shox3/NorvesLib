// ========================================
// Mega Geometry クラスタカリングの共通部分（cluster_cull.comp・cluster_bvh_cull.comp が取り込む）
//
// 取り込む側が先に、#version・#extension（buffer_reference）・layout(local_size_x = 64) を書く。
// ここには、GPU構造体・バインディング・視錐台/法線のコーン/Hi-Z/LODの判定・描画コマンドの積み方と、
// 1つのクラスタを判定して描画コマンドを積む ProcessCluster を置く。
//
// 遮蔽の判定は2パスで行う（cullPass）:
//   1パス目: 視錐台・法線のコーン・LODの判定を通り、前のフレームで見えたクラスタだけを描く（遮蔽の判定はしない）。
//   2パス目: 1パス目の深度から作ったHi-Zで、判定を通った全クラスタを遮蔽の判定にかける。
//            1パス目で描かなかったクラスタのうち遮蔽されないものを描き、
//            見えたクラスタの「見えた」印を更新する（次のフレームの1パス目が使う）。
//   0: 従来の1回の判定（遮蔽の判定なし。--mega-occlusion=off）。
//
// 「見えた」印は全インスタンスで1本の配列で、インスタンスの visibleOffset（そのインスタンスの区画の先頭）から
// クラスタの番号ごとに並ぶ。印は「見えたフレームの印の値」で、2パス目が見えたクラスタへ visibleWriteStamp を書き、
// 次のフレームの1パス目は visibleReadStamp（前のフレームの visibleWriteStamp）と等しいものだけを「前のフレームで見えた」
// とみなす。見えなかったクラスタを0に戻す必要が無いので、BVH の枝ごと切られて判定されなかったクラスタの印も古い値のまま
// 次のフレームで見えなかった扱いになる（全クラスタを毎フレーム書き直す従来の経路と同じ結果）。
//
// LODの段の選び方は2通り（クラスタの bakedInfo.x のビットで決まる）:
//   焼き込み済みの階層（NVMESH v1）: 自分の誤差を自分の球から画面へ投影した値がしきい値以下で、
//            親のグループの誤差を親の球から投影した値がしきい値を超えるクラスタを描く。
//            同じグループのクラスタは球と誤差が同じなので同じ判断になり、どの切り方も閉じたメッシュになる。
//   それ以外（v0・実行時に構築した階層・手続きの球）: 従来の段の選び方（ShouldDrawCluster）。
//
// ページの常駐（焼き込み済みの階層だけ）:
//   頂点・インデックスの中身はページ（128 KiB）ごとに常駐し、ページの表（binding 11）が常駐を持つ。クラスタの記録と
//   BVH の節は常駐したままなので、非常駐のページのクラスタも判定には読める（ただし描かない）。
//   クラスタのページが非常駐なら描かない。自分の誤差では粗すぎる（もっと細かい子が欲しい）のに、子（このクラスタを
//   作ったグループ）のページが非常駐なら、穴を作らず自分を描き、子のページを要求の列（binding 12）へ積む。
//   子は1つのページに収まるので、子と親が重なって描かれることは無い。親のページが先に常駐している並びが前提。
//   ページの表は1フレームの間は変わらない（ホストが記録の前に書く）ので、2パスの間で判定が食い違わない。
//
// クラスタ配列・グループのBVHの節の配列はジオメトリの共有プールの塊の中にあり、塊が複数あっても1回の dispatch で
// 読めるよう、インスタンスごとのデバイスアドレス（buffer_reference）で引く。
// ========================================

#ifndef NORVES_MEGA_GEOMETRY_CULL_GLSL
#define NORVES_MEGA_GEOMETRY_CULL_GLSL

#include "Common/HiZOcclusion.glsl"

// ========================================
// GPU構造体（MegaGeometryTypes.h・MegaGeometryPass.h と一致）
// ========================================

struct GPUClusterData
{
    vec4 boundsSphere;    // center.xyz + radius
    vec4 normalCone;      // axis.xyz + cos(halfAngle)
    uvec4 indexInfo;      // indexOffset, indexCount, vertexOffset, materialIndex
    uvec4 lodInfo;        // lodLevel, lodError(asfloat), parentStart, parentCount
    vec4 parentSphere;    // 焼き込み済みの階層: 親のグループの境界球 center.xyz + radius
    uvec4 bakedInfo;      // flags, parentError(asfloat), groupId, pageId
    uvec4 pageInfo;       // x = このクラスタを作ったグループ（もっと細かい子）のページ。最も細かい段は INVALID_PAGE_ID
};

const uint CLUSTER_FLAG_BAKED_LOD = 1u;
const uint INVALID_GROUP_ID = 0xFFFFFFFFu;
const uint INVALID_PAGE_ID = 0xFFFFFFFFu;
// ページの表の区画の値: 常駐していない（GeometryPageTable.h の PAGE_NON_RESIDENT と同じ）
const uint PAGE_NON_RESIDENT = 0xFFFFFFFFu;

// このメッシュのクラスタ配列（インスタンスの表の clusterInfo.xy のデバイスアドレスから引く）
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ClusterArray
{
    GPUClusterData clusters[];
};

// グループの BVH の節（GPUGroupBVHNode と一致）
struct BvhNode
{
    vec4 sphere;  // 境界球 center.xyz + radius（下のクラスタの球と親の球を包む）
    uvec4 info;   // x = 親の誤差の最大(asfloat), y = 子の節の先頭 / 葉ではクラスタの先頭, z = 子の数 / クラスタの数, w = flags
};

const uint BVH_NODE_FLAG_LEAF = 1u;

// このメッシュの BVH の節の配列（インスタンスの表の bvhInfo.xy のデバイスアドレスから引く）
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer BvhNodeArray
{
    BvhNode nodes[];
};

struct MegaInstance
{
    mat4 world;
    mat4 previousWorld;   // 頂点シェーダーが velocity に使う（ここでは読まない）
    vec4 lodSphere;       // LODの選択に使うメッシュ共通の境界球（ローカル。w<=0ならクラスタごとの中心で選ぶ）
    uvec4 clusterInfo;    // x,y = クラスタ配列のデバイスアドレス（下位・上位）, z = クラスタ数, w = 最初のワークグループの通し番号
    uvec4 drawInfo;       // x = 材質の区間の番号, y = 頂点の基点（塊の先頭から。頂点単位）, z = インデックスの基点（塊の先頭から。インデックス単位）, w = 「見えた」印の先頭
    uvec4 bvhInfo;        // x,y = BVH の節の配列のデバイスアドレス（下位・上位。BVH が無ければ 0）, z = 節の数, w = ページの表の先頭（このメッシュのページの範囲の先頭）
};

struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// ========================================
// バインディング
// ========================================

// set 0, binding 0: カメラ・カリング用ユニフォーム
layout(set = 0, binding = 0) uniform CullUniforms
{
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec4 cameraPosition;     // xyz = pos, w = unused
    vec4 frustumPlanes[6];   // 6つの視錐台平面 (ax+by+cz+d=0, normal pointing inward)
    uint instanceCount;      // インスタンスの表の要素数
    uint totalGroupCount;    // 平らな判定（cluster_cull.comp）の全インスタンスのワークグループの数
    float lodBias;           // LOD選択バイアス（ピクセル単位の許容誤差）
    float screenHeight;      // スクリーン高さ（ピクセル）
    float projectionFactor;  // screenHeight / (2 * tan(fov/2))
    uint hiZWidth;           // Hi-Zの元になった深度の幅（Hi-Zのミップ0はその半分）
    uint hiZHeight;          // Hi-Zの元になった深度の高さ（Hi-Zのミップ0はその半分）
    uint hiZMipCount;        // Hi-Zミップレベル数
    uint bHiZEnabled;        // Hi-Z有効フラグ
    uint debugPayloadMode;   // 0=none, 1=clusterIndex, 2=lodLevel
    uint cullPass;           // 0=従来（遮蔽の判定なし）, 1=1パス目, 2=2パス目
    uint bStatsEnabled;      // 1なら統計（binding 6）へ数える
    uint sectionBase;        // 区間の表・カウンタのうち、このパスの先頭（1パス目は0、2パス目は区間の数）
    uint visibleReadStamp;   // 1パス目が「前のフレームで見えた」とみなす印の値
    uint visibleWriteStamp;  // 2パス目が見えたクラスタへ書く印の値
    uint bvhStage;           // BVH のたどり: 節の判定の段の番号（BVH_STAGE_CLUSTERS なら葉のクラスタの判定）
    uint bvhInputBase;       // この段の入力の列の先頭（要素。段0は使わない）
    uint bvhNextBase;        // 次の段の列の先頭
    uint bvhLeafBase;        // 葉の列の先頭
    uint bvhRootCount;       // BVH を持つインスタンスの数（段0の入力の数。インスタンスの表の先頭からその数）
    uint pageRequestCapacity; // ページの要求の列の容量（0 ならこのフレームは要求を書かない）
} cullData;

const uint CULL_PASS_SINGLE = 0u;
const uint CULL_PASS_FIRST = 1u;
const uint CULL_PASS_SECOND = 2u;

const uint STAT_PASS1_DRAWN = 0u;
const uint STAT_PASS2_TESTED = 1u;
const uint STAT_PASS2_DRAWN = 2u;
const uint STAT_OCCLUDED = 3u;

const uint DEBUG_PAYLOAD_MODE_NONE = 0u;
const uint DEBUG_PAYLOAD_MODE_CLUSTER_INDEX = 1u;
const uint DEBUG_PAYLOAD_MODE_LOD_LEVEL = 2u;

// set 0, binding 1: インスタンスの表（読み取り専用）
layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer
{
    MegaInstance instances[];
};

// set 0, binding 2: IndirectDrawコマンド出力（区間ごとに連続した範囲）
layout(std430, set = 0, binding = 2) writeonly buffer IndirectDrawBuffer
{
    DrawIndexedIndirectCommand drawCommands[];
};

// set 0, binding 3: 区間ごとの可視クラスタ数カウンタ（atomicAdd用。添え字は sectionBase + 区間の番号）
layout(std430, set = 0, binding = 3) buffer DrawCountBuffer
{
    uint sectionCounts[];
};

// set 0, binding 4: Hi-Z深度ピラミッド（オクルージョンカリング用）
layout(set = 0, binding = 4) uniform sampler2D u_HiZTexture;

// set 0, binding 5: クラスタごとの「見えたフレームの印」（全インスタンスで1本）。
// 1パス目は読むだけ、2パス目は判定したクラスタが自分の要素だけを書く（競合しない）。cullPass=0 では使わない。
layout(std430, set = 0, binding = 5) buffer VisibleLastFrameBuffer
{
    uint visibleLastFrame[];
};

// set 0, binding 6: 統計（STAT_*。1フレームの全インスタンスの合計）。bStatsEnabled=0 では使わない。
layout(std430, set = 0, binding = 6) buffer StatsBuffer
{
    uint stats[4];
};

// set 0, binding 7: 材質の区間の表（x = コマンドの先頭の位置（全パス通しの添え字）, y = 区間のコマンドの最大数）。
// 添え字は sectionBase + 区間の番号。
layout(std430, set = 0, binding = 7) readonly buffer SectionBuffer
{
    uvec2 sections[];
};

// set 0, binding 8: コマンドごとの描画情報（x = インスタンスの番号, y = デバッグ表示・LODの段の payload）。
// コマンドの firstInstance がこの添え字（コマンドの通しの位置）なので、頂点シェーダーが gl_InstanceIndex で引く。
layout(std430, set = 0, binding = 8) writeonly buffer DrawInfoBuffer
{
    uvec2 drawInfos[];
};

// set 0, binding 9: BVH のたどりの列（x = インスタンスの番号, y = 節の番号）。段ごと・葉ごとに別の範囲を使う。
layout(std430, set = 0, binding = 9) buffer BvhQueueBuffer
{
    uvec2 bvhQueue[];
};

// set 0, binding 10: BVH のたどりのカウンタ。添え字 s（1以上）は段 s の入力の列の数、BVH_LEAF_COUNTER は葉の列の数。
layout(std430, set = 0, binding = 10) buffer BvhCounterBuffer
{
    uint bvhCounters[];
};

// set 0, binding 11: ページの表（全メッシュ共通。添字は instance.bvhInfo.w + メッシュの中のページの番号）。
// region: ページの中身を置いたプールの区画（PAGE_NON_RESIDENT なら常駐していない）。ホストがフレームの前に書く。
// requestStamp: このページを要求した最後のフレームの印（cullData.visibleWriteStamp）。GPU が書き、同じフレームの重複を省く。
struct PageEntry
{
    uint region;
    uint requestStamp;
};
layout(std430, set = 0, binding = 11) buffer PageTableBuffer
{
    PageEntry pageTable[];
};

// set 0, binding 12: ページの要求の列（ホストが数フレーム遅れて読み戻す）。
// [0] = 要求の数, [1] = 容量を超えて捨てた数, [2] = 容量, [3] = 予約, [PAGE_REQUEST_HEADER_WORDS..] = ページの表の位置
layout(std430, set = 0, binding = 12) buffer PageRequestBuffer
{
    uint pageRequests[];
};
const uint PAGE_REQUEST_HEADER_WORDS = 4u;

const uint BVH_LEAF_COUNTER = 16u;
const uint BVH_STAGE_CLUSTERS = 0xFFFFFFFFu;
// 葉が持つクラスタの最大数（GROUP_BVH_MAX_LEAF_CLUSTERS）。葉の列の1要素を、この数のスレッドが1クラスタずつ受け持つ
const uint BVH_LEAF_SLOTS = 8u;
// 節の LOD の判定の余裕（MegaGeometryBvhSelection.h の BVH_LOD_PRUNE_MARGIN と同じ）
const float BVH_LOD_PRUNE_MARGIN = 1.001;

// このワークグループが受け持つインスタンスの変換・LODの球・クラスタ配列（main の最初に決める）
mat4 g_WorldMatrix;
vec4 g_LODSphere;
ClusterArray g_Clusters;

// ========================================
// カリング関数
// ========================================

/**
 * @brief 球 vs 視錐台平面カリング
 * @return true = カリング（不可視）
 */
bool FrustumCullSphere(vec3 center, float radius)
{
    for (int i = 0; i < 6; ++i)
    {
        float distance = dot(cullData.frustumPlanes[i].xyz, center)
                       + cullData.frustumPlanes[i].w;
        if (distance < -radius)
        {
            return true; // 完全に外側
        }
    }
    return false; // 可視
}

/**
 * @brief 法線コーン バックフェースカリング
 *
 * クラスタの全三角形が裏向きかを、メッシュのローカル空間で判定します。
 * 非一様スケールやせん断では法線の向きの広がり（コーンの半角）がワールドで変わるため、
 * コーンをワールドへ移さず、カメラ位置をローカルへ戻して比べる。
 * 三角形の表裏（法線と視線の内積の符号）はアフィン変換で保たれ、鏡映（行列式が負）でだけ反転する。
 * coneCutoff は cos(コーンの半角)。半角が90°以上（coneCutoff <= 0）ならカリングしない。
 * @return true = カリング（全三角形が裏向き）
 */
bool NormalConeCull(vec3 localConeAxis, float coneCutoff, vec3 localCenter, float localRadius)
{
    if (coneCutoff <= 0.0)
    {
        return false; // どこから見ても表向きの三角形を含みうる
    }

    mat3 linearPart = mat3(g_WorldMatrix);
    float det = determinant(linearPart);
    if (abs(det) <= 1e-12)
    {
        return false; // 潰れた変換ではローカルへ戻せない
    }

    vec3 cameraLocal = inverse(linearPart) * (cullData.cameraPosition.xyz - g_WorldMatrix[3].xyz);
    vec3 coneAxis = det < 0.0 ? -localConeAxis : localConeAxis;

    // 法線は全てコーン軸から半角以内にあるので、視線とコーン軸のなす角が 90°−半角 未満なら
    // 全ての三角形が裏向きになる（cos(90°−半角) = sin(半角) と比べる）。
    // 視線はカメラから境界球内のどの点へ向かうものでも成り立つよう、半径の分だけ余裕を取る
    float sinHalfAngle = sqrt(max(1.0 - coneCutoff * coneCutoff, 0.0));
    vec3 toCenter = localCenter - cameraLocal;
    return dot(toCenter, coneAxis) >= sinHalfAngle * length(toCenter) + localRadius;
}

vec3 TransformClusterCenterToWorld(vec3 localCenter)
{
    return (g_WorldMatrix * vec4(localCenter, 1.0)).xyz;
}

float ComputeWorldRadiusScale()
{
    float scaleX = length(g_WorldMatrix[0].xyz);
    float scaleY = length(g_WorldMatrix[1].xyz);
    float scaleZ = length(g_WorldMatrix[2].xyz);
    return max(scaleX, max(scaleY, scaleZ));
}

/**
 * @brief Hi-Z深度ピラミッドによるオクルージョンカリング
 *
 * クラスタの境界の球を包む AABB の8点を画面へ投影した矩形と最も手前の深度を、
 * Hi-Zの該当範囲の最大（最も遠い深度）と比べて、完全に遮蔽されているかを保守的に判定する。
 * 判定の本体は Common/HiZOcclusion.glsl（GPUのテストが同じ関数を確かめる）。
 * @return true = カリング（完全に遮蔽）
 */
bool OcclusionCullSphere(vec3 center, float radius)
{
    if (cullData.bHiZEnabled == 0)
    {
        return false;
    }

    return HiZIsSphereOccluded(u_HiZTexture,
                               ivec2(int(cullData.hiZWidth), int(cullData.hiZHeight)),
                               int(cullData.hiZMipCount),
                               cullData.projectionMatrix * cullData.viewMatrix,
                               center,
                               radius);
}

/**
 * @brief 視線から外れた点で透視投影が横のずれを伸ばす倍率 1/cos²α の、球の描かれる点での上限
 *
 * 描かれる点は視錐台の中なので tan²α ≤ tan²(fovX/2) + tan²(fovY/2)。球が前方にあれば、
 * α ≤ 中心の方向の角 β + 見かけの半径 asin(R/D) でもあるので、小さい方を使う
 * （MegaGeometryLODSelection.h の ComputeLODSpherePerspectiveStretch と同じ式）。
 */
float ComputePerspectiveStretch(vec3 sphereCenter, float centerDistance, float sphereRadius)
{
    float tanHalfFovX = 1.0 / max(abs(cullData.projectionMatrix[0][0]), 1e-6);
    float tanHalfFovY = 1.0 / max(abs(cullData.projectionMatrix[1][1]), 1e-6);
    float stretch = 1.0 + tanHalfFovX * tanHalfFovX + tanHalfFovY * tanHalfFovY;
    float depthScale = abs(cullData.projectionMatrix[2][3]);
    float centerDepth = depthScale > 1e-6
        ? (cullData.projectionMatrix * cullData.viewMatrix * vec4(sphereCenter, 1.0)).w / depthScale
        : 0.0;
    if (centerDistance > sphereRadius && centerDepth > 0.0)
    {
        float cosBeta = min(centerDepth / centerDistance, 1.0);
        float sinBeta = sqrt(max(1.0 - cosBeta * cosBeta, 0.0));
        float sinGamma = sphereRadius / centerDistance;
        float cosGamma = sqrt(max(1.0 - sinGamma * sinGamma, 0.0));
        float cosAlpha = cosBeta * cosGamma - sinBeta * sinGamma;
        if (cosAlpha > 0.0)
        {
            stretch = min(stretch, 1.0 / (cosAlpha * cosAlpha));
        }
    }
    return stretch;
}

/**
 * @brief 焼き込み済みの階層で、球の中の誤差（ローカルの長さ）を画面へ投影した大きさ（画素）の上限
 *
 * 形のずれは球のどの点でも最も近い点までの距離 D−R より遠くで起きるので、画面で
 * projectionFactor·e·stretch/(D−R) を超えない（親の球が子の球を包み、親の誤差が子以上なら、
 * 親の値は子の値以上）。MegaGeometryLODSelection.h の ComputeBakedSphereErrorPixels と同じ式。
 */
float ProjectBakedError(vec3 localCenter, float localRadius, float localError)
{
    float worldScale = ComputeWorldRadiusScale();
    vec3 center = TransformClusterCenterToWorld(localCenter);
    float radius = localRadius * worldScale;
    float centerDistance = distance(center, cullData.cameraPosition.xyz);
    float nearest = max(centerDistance - radius, 1e-4);
    float stretch = ComputePerspectiveStretch(center, centerDistance, radius);
    return localError * worldScale * cullData.projectionFactor * stretch / nearest;
}

/**
 * @brief ページが常駐しているか（ページの表を引く。ページの番号が無い INVALID_PAGE_ID は常駐とみなす）
 */
bool IsPageResident(uint pageTableBase, uint pageId)
{
    if (pageId == INVALID_PAGE_ID)
    {
        return true;
    }
    return pageTable[pageTableBase + pageId].region != PAGE_NON_RESIDENT;
}

/**
 * @brief ページを要求する（同じフレームの同じページは1回だけ列へ積む）
 *
 * 常駐していない子のページの要求と、描いたクラスタの常駐ページの使用の印の両方に使う。
 * どちらもページの表の位置を積むだけで、ホストが自分の記録から区別する（使用の印はホストの LRU を使われた順にする）。
 *
 * 印の交換で重複を省く: ページの表の requestStamp を今のフレームの印に替え、前の値が同じ印なら先に誰かが積んだ。
 * 列が容量を超えたら積まず、捨てた数だけ数える（ホストが溢れを見る）。
 */
void RequestPage(uint pageTableBase, uint pageId)
{
    if (cullData.pageRequestCapacity == 0u || pageId == INVALID_PAGE_ID)
    {
        return;
    }
    uint tableIndex = pageTableBase + pageId;
    if (atomicExchange(pageTable[tableIndex].requestStamp, cullData.visibleWriteStamp) == cullData.visibleWriteStamp)
    {
        return;
    }
    uint slot = atomicAdd(pageRequests[0], 1u);
    if (slot < cullData.pageRequestCapacity)
    {
        pageRequests[PAGE_REQUEST_HEADER_WORDS + slot] = tableIndex;
    }
    else
    {
        atomicAdd(pageRequests[1], 1u);
    }
}

/**
 * @brief 焼き込み済みの階層（NVMESH v1）のLOD DAGカット判定（ページの常駐を含む）
 *
 * 自分の誤差の投影が許容以下で、親のグループの誤差の投影が許容を超えるときだけ描く。
 * 根（親のグループが無い）は自分の誤差だけで決まる。
 *
 * ページの常駐:
 *   - 自分のページが常駐していなければ描かない（親が代わりに描く）。
 *   - 自分の誤差が許容を超える（もっと細かい子が欲しい）ときは、子のページが常駐していなければ穴を作らないよう自分を描き、
 *     outRequestPage に子のページを返す（呼び出し側が、実際に描かれたときだけ要求する）。子のページが常駐していれば、
 *     子が描くので自分は描かない。同じグループの子は同じページなので、子と親が重なって描かれることは無い。
 *   - どちらも、親のグループの誤差が許容を超える（親では粗すぎる）ときだけ描く。
 *
 * @param outRequestPage 子のページが常駐していないために自分を描くとき、その子のページ。それ以外は INVALID_PAGE_ID
 * @return true = このクラスタを描画すべき
 */
bool ShouldDrawBakedCluster(GPUClusterData cluster, uint pageTableBase, out uint outRequestPage)
{
    outRequestPage = INVALID_PAGE_ID;
    if (!IsPageResident(pageTableBase, cluster.bakedInfo.w))
    {
        return false; // 自分のページが無い → 親が代わりに描く
    }
    float selfError = ProjectBakedError(cluster.boundsSphere.xyz, cluster.boundsSphere.w,
                                        uintBitsToFloat(cluster.lodInfo.y));
    if (selfError > cullData.lodBias)
    {
        // 自分の誤差が大きすぎる → より詳細な段を使う。ただし子のページが無いなら、穴を作らず自分を描く
        uint childPage = cluster.pageInfo.x;
        if (IsPageResident(pageTableBase, childPage))
        {
            return false;
        }
        outRequestPage = childPage;
    }
    if (cluster.bakedInfo.z == INVALID_GROUP_ID)
    {
        return true; // 根
    }
    float parentError = ProjectBakedError(cluster.parentSphere.xyz, cluster.parentSphere.w,
                                          uintBitsToFloat(cluster.bakedInfo.y));
    // 親が粗すぎるときだけ自分を描く。NaN なら自分を描く側へ倒す
    return !(parentError <= cullData.lodBias);
}

/**
 * @brief LOD DAGカット判定
 *
 * このクラスタを描画すべきかを判定します。
 * - LOD 0（最詳細）: 親が存在しないか、スクリーン誤差が許容範囲内なら描画
 * - LOD N > 0: 自身の投影誤差が許容範囲内 かつ 親の投影誤差が範囲外なら描画
 *
 * @return true = このクラスタを描画すべき
 */
bool ShouldDrawCluster(GPUClusterData cluster, vec3 center)
{
    uint lodLevel = cluster.lodInfo.x;
    float lodError = uintBitsToFloat(cluster.lodInfo.y);
    uint parentStart = cluster.lodInfo.z;
    uint parentCount = cluster.lodInfo.w;

    // 誤差1を画面の画素へ直す倍率。メッシュ共通のLOD球があれば、全クラスタが同じ倍率で選ぶ
    // （どの段も閉じたメッシュなら、メッシュ全体で同じ段が選ばれて段の境目に割れ目ができない）。
    // 誤差は球の法線方向のずれなので、見える面での角度の変化の最大 projectionFactor·D/(D²−R²) に、
    // 視線から外れた点で透視投影が横のずれを伸ばす倍率 1/cos²α の上限を掛けた値を使う
    // （MegaGeometryLODSelection.h の ComputeLODSphereErrorPixelsPerMeter と同じ式）。
    float errorScale;
    if (g_LODSphere.w > 0.0)
    {
        float worldScale = ComputeWorldRadiusScale();
        vec3 lodCenter = TransformClusterCenterToWorld(g_LODSphere.xyz);
        float lodRadius = g_LODSphere.w * worldScale;
        float centerDistance = distance(lodCenter, cullData.cameraPosition.xyz);
        float denominator = max(centerDistance * centerDistance - lodRadius * lodRadius, 1e-6);
        float angularScale = cullData.projectionFactor * centerDistance / denominator;

        float stretch = ComputePerspectiveStretch(lodCenter, centerDistance, lodRadius);
        errorScale = angularScale * stretch * worldScale;
    }
    else
    {
        float dist = max(distance(center, cullData.cameraPosition.xyz), 0.001); // 0除算防止
        errorScale = cullData.projectionFactor / dist;
    }

    // このクラスタのスクリーン投影誤差（ピクセル単位）
    float projectedError = lodError * errorScale;

    // LOD 0: 最詳細レベル → 常に描画候補（ただし親が十分なら親に任せる）
    if (lodLevel == 0)
    {
        // 親がない場合は常に描画
        if (parentCount == 0)
        {
            return true;
        }

        // 親の投影誤差をチェック: 親がlodBias以下なら親で十分 → 描画しない
        for (uint i = 0; i < parentCount; ++i)
        {
            GPUClusterData parent = g_Clusters.clusters[parentStart + i];
            float parentError = uintBitsToFloat(parent.lodInfo.y);
            float parentProjectedError = parentError * errorScale;
            if (parentProjectedError <= cullData.lodBias)
            {
                return false; // 親で十分
            }
        }

        return true;
    }

    // LOD N > 0: 自身の投影誤差が許容範囲内のとき描画候補
    if (projectedError > cullData.lodBias)
    {
        return false; // 自身の誤差が大きすぎる → より詳細なレベルを使う
    }

    // 親がない場合（最粗レベル）は描画
    if (parentCount == 0)
    {
        return true;
    }

    // 親の投影誤差が許容範囲外なら、このレベルを描画
    for (uint i = 0; i < parentCount; ++i)
    {
        GPUClusterData parent = g_Clusters.clusters[parentStart + i];
        float parentError = uintBitsToFloat(parent.lodInfo.y);
        float parentProjectedError = parentError * errorScale;
        if (parentProjectedError > cullData.lodBias)
        {
            return true; // 親が粗すぎるので自身を描画
        }
    }

    // 親の誤差も許容範囲内 → 親に任せる
    return false;
}

uint ComputeDebugPayload(uint clusterIndex, GPUClusterData cluster)
{
    if (cullData.debugPayloadMode == DEBUG_PAYLOAD_MODE_CLUSTER_INDEX)
    {
        return clusterIndex;
    }

    if (cullData.debugPayloadMode == DEBUG_PAYLOAD_MODE_LOD_LEVEL)
    {
        return cluster.lodInfo.x;
    }

    return 0u;
}

// ========================================
// 描画コマンドの積み方と、1クラスタの判定
// ========================================

/**
 * @brief 描画するクラスタのIndirectDrawコマンドを、そのインスタンスの材質の区間へ1つ積む
 */
void EmitDrawCommand(uint instanceIndex, MegaInstance instance, uint clusterIndex, GPUClusterData cluster)
{
    uint sectionSlot = cullData.sectionBase + instance.drawInfo.x;
    uvec2 section = sections[sectionSlot];
    uint slot = atomicAdd(sectionCounts[sectionSlot], 1u);

    // 区間の最大数を超えた分は積まない（描画の数も区間の最大数で頭打ちになる）
    if (slot < section.y)
    {
        uint commandIndex = section.x + slot;
        drawCommands[commandIndex].indexCount = cluster.indexInfo.y;                       // IndexCount
        drawCommands[commandIndex].instanceCount = 1;
        drawCommands[commandIndex].firstIndex = instance.drawInfo.z + cluster.indexInfo.x; // IndexOffset（塊の先頭から）
        drawCommands[commandIndex].vertexOffset = int(instance.drawInfo.y) + int(cluster.indexInfo.z); // VertexOffset（塊の先頭から）
        drawCommands[commandIndex].firstInstance = commandIndex;
        drawInfos[commandIndex] = uvec2(instanceIndex, ComputeDebugPayload(clusterIndex, cluster));
    }
}

void CountStat(uint statIndex)
{
    if (cullData.bStatsEnabled != 0u)
    {
        atomicAdd(stats[statIndex], 1u);
    }
}

/**
 * @brief 1つのクラスタを判定し、描くなら描画コマンドを積む（インスタンスの変換・LODの球・クラスタ配列は設定済み）
 *
 * 平らな判定（cluster_cull.comp）も、BVH の葉のクラスタ（cluster_bvh_cull.comp）も、同じ判定を通る。
 * 各クラスタはどちらか一方の1スレッドだけが受け持つ（visibleLastFrame への書き込みが競合しない）。
 */
void ProcessCluster(uint instanceIndex, MegaInstance instance, uint clusterIndex)
{
    uint visibleIndex = instance.drawInfo.w + clusterIndex;

    GPUClusterData cluster = g_Clusters.clusters[clusterIndex];

    vec3 localCenter = cluster.boundsSphere.xyz;
    float localRadius = cluster.boundsSphere.w;
    vec3 center = TransformClusterCenterToWorld(localCenter);
    float radius = localRadius * ComputeWorldRadiusScale();

    // 視錐台・法線のコーン・LODの判定（遮蔽の判定の前。1パス目と2パス目で同じ式なので、
    // 1パス目で描いたクラスタは2パス目でも必ずここを通る。ページの表はフレームの間は変わらないので、
    // ページの常駐の判定もパスの間で食い違わない）
    uint requestPage = INVALID_PAGE_ID;
    // 描いたときに使用の印を出す、このクラスタ自身のページ（焼き込みの階層を持たないクラスタはページを持たない）
    uint ownPage = ((cluster.bakedInfo.x & CLUSTER_FLAG_BAKED_LOD) != 0u) ? cluster.bakedInfo.w : INVALID_PAGE_ID;
    bool bPassesBasicTests =
        !FrustumCullSphere(center, radius) &&
        !NormalConeCull(cluster.normalCone.xyz, cluster.normalCone.w, localCenter, localRadius) &&
        (((cluster.bakedInfo.x & CLUSTER_FLAG_BAKED_LOD) != 0u)
             ? ShouldDrawBakedCluster(cluster, instance.bvhInfo.w, requestPage)
             : ShouldDrawCluster(cluster, center));

    if (cullData.cullPass == CULL_PASS_FIRST)
    {
        // 前のフレームで見えたクラスタだけを描く。遮蔽の判定はしない（この描画の深度から2パス目のHi-Zを作る）
        if (bPassesBasicTests && visibleLastFrame[visibleIndex] == cullData.visibleReadStamp)
        {
            EmitDrawCommand(instanceIndex, instance, clusterIndex, cluster);
            CountStat(STAT_PASS1_DRAWN);
        }
        return;
    }

    if (cullData.cullPass == CULL_PASS_SECOND)
    {
        // 1パス目で描いたか（描くのは「前のフレームで見えた」かつ判定を通ったクラスタ）。印を書き換える前に読む
        bool bDrawnInFirstPass = bPassesBasicTests && visibleLastFrame[visibleIndex] == cullData.visibleReadStamp;

        // 判定を通った全クラスタを、1パス目の深度のHi-Zで判定し直す
        bool bOccluded = false;
        if (bPassesBasicTests)
        {
            CountStat(STAT_PASS2_TESTED);
            bOccluded = OcclusionCullSphere(center, radius);
            if (bOccluded)
            {
                CountStat(STAT_OCCLUDED);
            }
        }

        // 次のフレームのために、見えたクラスタへ今のフレームの印を書く。判定に落ちた・LODで選ばれなかった
        // クラスタは0に戻すので、LODが切り替わったときに古い印が残らない
        bool bVisible = bPassesBasicTests && !bOccluded;
        visibleLastFrame[visibleIndex] = bVisible ? cullData.visibleWriteStamp : 0u;

        if (bVisible && !bDrawnInFirstPass)
        {
            EmitDrawCommand(instanceIndex, instance, clusterIndex, cluster);
            CountStat(STAT_PASS2_DRAWN);
        }
        // 子のページが無いために描いたクラスタは、見えているときだけ子のページを要求する。
        // 1パス目で描いたものも、2パス目が判定し直すのでここで要求が出る（1パス目では要求しない）
        // 描いたクラスタの自分のページへは使用の印を出す（常駐ページの最後に使われたフレームを、ホストの LRU へ渡す）
        if (bVisible)
        {
            RequestPage(instance.bvhInfo.w, requestPage);
            RequestPage(instance.bvhInfo.w, ownPage);
        }
        return;
    }

    // 従来の経路: 遮蔽の判定なしの1回の判定（bHiZEnabled=0）
    if (bPassesBasicTests && !OcclusionCullSphere(center, radius))
    {
        EmitDrawCommand(instanceIndex, instance, clusterIndex, cluster);
        RequestPage(instance.bvhInfo.w, requestPage);
        RequestPage(instance.bvhInfo.w, ownPage);
    }
}

#endif // NORVES_MEGA_GEOMETRY_CULL_GLSL
