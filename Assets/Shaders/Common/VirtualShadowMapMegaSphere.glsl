// ========================================
// VSM の MegaGeometry の投影物のカリングが共有する、ワールドの球とスライスの判定
// （vsm_mega_cull_pairs.comp が（インスタンス、スライス）の組を絞るときと、vsm_mega_cull.comp がクラスタを選ぶときに同じ関数を使う）
//
// 取り込む側が、先に Common/VirtualShadowMapMegaCull.glsl（megaParams・スライスの表 vsmSlices）を取り込んでおくこと。
// binding 16 の dirty のページの階層（vsm_dirty_mips.comp が作る）もここで宣言する。
// ========================================

#ifndef NORVES_VSM_MEGA_SPHERE_GLSL
#define NORVES_VSM_MEGA_SPHERE_GLSL

// binding 16: dirty のページの階層（vsm_dirty_mips.comp が作る）
layout(std430, set = 0, binding = 16) readonly buffer VsmDirtyBits
{
    uint dirtyBits[];
};

// ワールドの位置のライト空間の XY
vec2 LightXY(vec3 position)
{
    return vec2(dot(position, megaParams.lightRight.xyz), dot(position, megaParams.lightUp.xyz));
}

bool IsPerspectiveSlice(VsmSlice slice)
{
    return slice.extra.z == int(VSM_SLICE_PROJECTION_PERSPECTIVE);
}

/**
 * @brief ワールドの球が、スライスの範囲に入るか
 *
 * 太陽の段は、段の範囲（ライト空間の XY）と深度の範囲に入るか（球の外接の矩形で保守的に判定する）。
 * 点光源の面は、Range の内側で面の錐台と交わり、球の透視像が面のページを 1 つ以上覆うか。
 */
bool LevelOverlapsSphere(uint level, vec3 center, float radius)
{
    if (IsPerspectiveSlice(vsmSlices[level]))
    {
        ivec2 pageMin;
        uvec2 size;
        VsmPerspectivePageRange(vsmSlices[level], center, radius, pageMin, size);
        return size.x != 0u && size.y != 0u;
    }
    const float lightDepth = dot(center, megaParams.lightDirection.xyz);
    if (abs(lightDepth - megaParams.depth.x) > megaParams.depth.y + radius)
    {
        return false;
    }
    const VsmSlice slice = vsmSlices[level];
    const float pageMeters = slice.info.x;
    const vec2 rangeMin = vec2(slice.origin.xy) * pageMeters;
    const vec2 rangeMax = rangeMin + vec2(float(slice.origin.w) * pageMeters);
    const vec2 lightCenter = LightXY(center);
    return all(greaterThanEqual(lightCenter + vec2(radius), rangeMin)) && all(lessThan(lightCenter - vec2(radius), rangeMax));
}

/**
 * @brief ワールドの球が覆うページの範囲を、範囲の最小のページからの相対の座標（両端を含む）で求める。覆うページが範囲に無ければ false
 *
 * 太陽の段は球のライト空間の矩形が覆うページ、点光源の面は球の透視像が覆うページ（面の座標。範囲の原点は 0）。範囲の外へはみ出す分は切り捨てる。
 */
bool SphereRelativePageRange(VsmSlice slice, vec3 center, float radius, out ivec2 low, out ivec2 high)
{
    const ivec2 origin = slice.origin.xy;
    if (IsPerspectiveSlice(slice))
    {
        ivec2 pageMin;
        uvec2 size;
        VsmPerspectivePageRange(slice, center, radius, pageMin, size);
        low = max(pageMin - origin, ivec2(0));
        high = min(pageMin - origin + ivec2(size) - ivec2(1), ivec2(int(VSM_MEGA_TABLE_DIMENSION) - 1));
        return size.x != 0u && size.y != 0u && low.x <= high.x && low.y <= high.y;
    }
    const float pageMeters = slice.info.x;
    const vec2 lightCenter = LightXY(center);
    const vec2 lowPage = clamp(floor((lightCenter - vec2(radius)) / pageMeters), vec2(-1.0e9), vec2(1.0e9));
    const vec2 highPage = clamp(floor((lightCenter + vec2(radius)) / pageMeters), vec2(-1.0e9), vec2(1.0e9));
    low = max(ivec2(lowPage) - origin, ivec2(0));
    high = min(ivec2(highPage) - origin, ivec2(int(VSM_MEGA_TABLE_DIMENSION) - 1));
    return low.x <= high.x && low.y <= high.y;
}

/**
 * @brief ワールドの球が覆うページ（SphereRelativePageRange）に、dirty で割り当て済みのものが 1 つでもあるか（dirty の階層で判定する）
 *
 * 範囲の最小のページからの相対の座標で、矩形が 2×2 以下のセルに収まる最も細かい mip から始め、そのセルのビットを調べる。
 * ビットが立っていても、セルが矩形からはみ出しているときは矩形の外の dirty のページかもしれないので、子のセルへ下りて
 * 矩形と交わる子だけを調べる。矩形に完全に含まれるセルのビットが立っていれば true。
 * 範囲の外へはみ出す分は切り捨てる（覆うページが範囲に無ければ false）。
 */
bool SphereHasDirtyPage(uint level, vec3 center, float radius)
{
    const VsmSlice slice = vsmSlices[level];
    ivec2 low;
    ivec2 high;
    if (!SphereRelativePageRange(slice, center, radius, low, high))
    {
        return false;
    }

    uint mip = 0u;
    while (mip < VSM_MEGA_DIRTY_MIP_COUNT - 1u && ((high.x >> mip) - (low.x >> mip) > 1 || (high.y >> mip) - (low.y >> mip) > 1))
    {
        ++mip;
    }

    // 調べるセルの積み(mip と相対の座標を 1 語に詰める)。1 つのセルは最大 4 つの子を積み、深さは mip の数なので 32 で足りる
    // （最初の 4 セル + 下りるごとに 3 つ増える ≦ 4 + 3 × 7）
    uint stack[32];
    uint depth = 0u;
    const uvec2 cellLow = uvec2(low) >> mip;
    const uvec2 cellHigh = uvec2(high) >> mip;
    for (uint y = cellLow.y; y <= cellHigh.y; ++y)
    {
        for (uint x = cellLow.x; x <= cellHigh.x; ++x)
        {
            stack[depth++] = (mip << 16u) | (y << 8u) | x;
        }
    }
    while (depth > 0u)
    {
        const uint packed = stack[--depth];
        const uint cellMip = packed >> 16u;
        const uvec2 cell = uvec2(packed & 0xFFu, (packed >> 8u) & 0xFFu);
        const uint bit = VsmMegaDirtyBitIndex(level, cellMip, cell);
        if ((dirtyBits[bit >> 5u] & (1u << (bit & 31u))) == 0u)
        {
            continue;
        }
        // セルが覆うページの範囲が矩形に完全に含まれるなら、立っているビットは矩形の中の dirty のページを意味する
        const ivec2 cellMin = ivec2(cell << cellMip);
        const ivec2 cellMax = cellMin + ivec2(int(1u << cellMip) - 1);
        if (all(greaterThanEqual(cellMin, low)) && all(lessThanEqual(cellMax, high)))
        {
            return true;
        }
        // mip 0 のセルは 1 ページなので、立っていれば矩形に含まれている(ここへは来ない)。念のため下りない
        if (cellMip == 0u)
        {
            continue;
        }
        // 矩形と交わる子だけを積む
        const uint childMip = cellMip - 1u;
        for (uint child = 0u; child < 4u; ++child)
        {
            const uvec2 childCell = (cell << 1u) + uvec2(child & 1u, child >> 1u);
            const ivec2 childMin = ivec2(childCell << childMip);
            const ivec2 childMax = childMin + ivec2(int(1u << childMip) - 1);
            if (childMax.x < low.x || childMin.x > high.x || childMax.y < low.y || childMin.y > high.y)
            {
                continue;
            }
            stack[depth++] = (childMip << 16u) | (childCell.y << 8u) | childCell.x;
        }
    }
    return false;
}

#endif // NORVES_VSM_MEGA_SPHERE_GLSL
