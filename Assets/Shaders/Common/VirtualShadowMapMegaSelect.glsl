// ========================================
// VSM の MegaGeometry の投影物の 1 クラスタの選択（判定と一覧への書き込み）
// （平らな経路 vsm_mega_cull.comp と、太陽の段の BVH の経路 vsm_mega_bvh.comp の葉の段が同じ関数を使う）
//
// 取り込む側が、先に Common/MegaGeometryCull.glsl・Common/VirtualShadowMapMegaCull.glsl・Common/VirtualShadowMapMegaSphere.glsl を
// 取り込んでおくこと。出力の一覧（binding 15）・統計（binding 17）・影の表（binding 18）・VSM のページの表（binding 19）をここで宣言する。
// ========================================

#ifndef NORVES_VSM_MEGA_SELECT_GLSL
#define NORVES_VSM_MEGA_SELECT_GLSL

// binding 15: 出力の一覧
layout(std430, set = 0, binding = 15) buffer VsmMegaList
{
    uint listSelected;
    uint listOverflow;
    uint listInstanceLevels;
    uint listReserved;
    uvec4 listEntries[];
};

// binding 17: VSM の統計
layout(std430, set = 0, binding = 17) buffer VsmStats
{
    uint vsmStats[];
};
const uint STAT_MEGA_INSTANCES = 8u;
const uint STAT_MEGA_CLUSTERS = 9u;
const uint STAT_MEGA_OVERFLOW = 10u;
// 透視のスライス（点光源の面）だけの数（STAT_MEGA_INSTANCES・STAT_MEGA_CLUSTERS はこれを含む合計）
const uint STAT_MEGA_POINT_INSTANCES = 56u;
const uint STAT_MEGA_POINT_CLUSTERS = 57u;

// binding 18: 影の表（インスタンスの表と同じ並び。構造は Common/VirtualShadowMapMegaCull.glsl の ShadowInstance）
layout(std430, set = 0, binding = 18) readonly buffer ShadowInstanceBuffer
{
    ShadowInstance shadowInstances[];
};

// binding 19: VSM のページの表。溢れて書けなかったクラスタ・節の範囲のページへ再描画の印を atomicOr で書く
// （割り当て済み・dirty・物理ページの番号の欄は変えない。MegaGeometryCull.glsl の pageTable（binding 11。ジオメトリのページの表）とは別のバッファ）
layout(std430, set = 0, binding = 19) buffer VsmPageTable
{
    uint vsmPageTable[];
};

/**
 * @brief 溢れて一覧・列に書けなかったクラスタ・節の球が覆う、割り当て済みで dirty のページへ再描画の印を付ける
 *
 * 覆うページの範囲は SphereHasDirtyPage と同じ（範囲の外へはみ出す分は切り捨てる）。ほかのスレッドも同じ欄へ印を書くので、
 * 判定の読み取りも atomicOr で行う。溢れたときだけ通る経路なので、範囲を 1 スレッドで走査する。
 */
void MarkRetryPages(uint level, vec3 center, float radius)
{
    const VsmSlice slice = vsmSlices[level];
    const ivec2 origin = slice.origin.xy;
    ivec2 low;
    ivec2 high;
    if (!SphereRelativePageRange(slice, center, radius, low, high))
    {
        return;
    }
    for (int y = low.y; y <= high.y; ++y)
    {
        for (int x = low.x; x <= high.x; ++x)
        {
            // 範囲の最小のページからの相対の座標を、絶対のページを経てページの表の番地（スライスの先頭 + トーラスの番地）へ直す
            const uint entryIndex = VsmSliceEntryIndex(slice, origin.x + x, origin.y + y);
            const uint entry = atomicOr(vsmPageTable[entryIndex], 0u);
            if ((entry & VSM_MEGA_PAGE_ENTRY_ALLOCATED) != 0u && (entry & VSM_MEGA_PAGE_ENTRY_DIRTY) != 0u)
            {
                atomicOr(vsmPageTable[entryIndex], VSM_MEGA_PAGE_ENTRY_RETRY);
            }
        }
    }
}

/**
 * @brief 1 つのクラスタを判定し、通れば出力の一覧へ（インスタンスの表の番号、スライス、クラスタの番号）を書く
 *
 * 判定: 境界の球が、そのスライスの範囲・深度の範囲に入り、dirty のページを含み、LOD の判定（ShouldDrawShadowCluster）を通る。
 * 一覧の容量を超えたら書かずに溢れを数え、球が覆うページへ再描画の印を付ける。
 * このスレッドのインスタンスの変換・LOD の球・クラスタ配列・texel の値（Common/MegaGeometryCull.glsl の g_*）をここで決める。
 */
void SelectShadowCluster(uint instanceIndex, MegaInstance instance, uint level, uint clusterIndex)
{
    const VsmSlice levelSlice = vsmSlices[level];
    const bool bPerspective = IsPerspectiveSlice(levelSlice);
    g_WorldMatrix = instance.world;
    g_LODSphere = instance.lodSphere;
    g_Clusters = ClusterArray(uvec2(instance.clusterInfo.x, instance.clusterInfo.y));
    g_OrthoTexelMeters = levelSlice.info.y;
    g_ShadowPerspective = bPerspective;
    g_ShadowAxisZ = levelSlice.axisZ;
    g_ShadowTexelNdc = levelSlice.info.y;
    g_ShadowNearPlane = levelSlice.info.w;

    const GPUClusterData cluster = g_Clusters.clusters[clusterIndex];
    const vec3 center = TransformClusterCenterToWorld(cluster.boundsSphere.xyz);
    const float radius = cluster.boundsSphere.w * ComputeWorldRadiusScale();
    if (!LevelOverlapsSphere(level, center, radius) || !SphereHasDirtyPage(level, center, radius) ||
        !ShouldDrawShadowCluster(cluster, center, instance.bvhInfo.w))
    {
        return;
    }

    const uint slot = atomicAdd(listSelected, 1u);
    if (slot < megaParams.counts.y)
    {
        listEntries[slot] = uvec4(instanceIndex, level, clusterIndex, 0u);
        atomicAdd(vsmStats[STAT_MEGA_CLUSTERS], 1u);
        if (bPerspective)
        {
            atomicAdd(vsmStats[STAT_MEGA_POINT_CLUSTERS], 1u);
        }
    }
    else
    {
        atomicAdd(listOverflow, 1u);
        atomicAdd(vsmStats[STAT_MEGA_OVERFLOW], 1u);
        MarkRetryPages(level, center, radius);
    }
}

#endif // NORVES_VSM_MEGA_SELECT_GLSL
