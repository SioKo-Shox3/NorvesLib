// ========================================
// Hi-Z（HZB）による遮蔽の判定（保守的な矩形の判定）
//
// 境界の球を包む AABB の8点を画面へ投影した矩形と、その最も手前の深度で判定する。
// 矩形が HZB の 2x2 texel 以内に収まるミップを選び、その範囲の texel の最大（最も遠い深度）と比べる。
// 手前の深度が範囲の最大より遠ければ、矩形のどの画素でも既に描かれた面の後ろなので、完全に隠れている。
// 見えているものを隠れていると判定しないことを優先し、迷うときは「隠れていない」を返す。
//
// 規約:
//   - 深度は [0,1] の Less の標準の向き（手前ほど小さい）。HZB は HiZPyramid が作る（2x2 の最大で縮める）。
//   - HZB のミップ0 は深度の半分の解像度（切り上げ）。ミップ m の texel i は深度の画素 [i<<(m+1), (i+1)<<(m+1)) を覆い、
//     最後の列・行だけは深度の端まで伸びる。投影は深度の解像度（depthSize）で画素へ直してから texel を求める。
//   - viewProjection は列ベクトル規約の mat4（C++ からは Transpose して渡した View/Proj の積）。
// ========================================

#ifndef NORVES_HIZ_OCCLUSION_GLSL
#define NORVES_HIZ_OCCLUSION_GLSL

// 浮動小数点の誤差（投影の計算と描画の深度の差）で、接して見えているものを隠さないための余裕
#ifndef HIZ_OCCLUSION_DEPTH_BIAS
#define HIZ_OCCLUSION_DEPTH_BIAS 1.0e-5
#endif

// 正の整数 value に対する ceil(log2(value))
int HiZCeilLog2(int value)
{
    return value <= 1 ? 0 : findMSB(uint(value - 1)) + 1;
}

/**
 * @brief 球が HZB で完全に隠れているかを保守的に判定する
 * @param hiZ            HZB（全ミップが ShaderResource。texelFetch で読む）
 * @param depthSize      HZB の元になった深度の解像度（HZB のミップ0 はその半分）
 * @param hiZMipCount    HZB のミップ数
 * @param viewProjection 射影行列 * ビュー行列
 * @param center         球の中心（ワールド）
 * @param radius         球の半径（ワールド）
 * @return true = 完全に隠れている。近平面をまたぐ・カメラの後ろにかかる・画面の外のときは false
 */
bool HiZIsSphereOccluded(sampler2D hiZ,
                         ivec2 depthSize,
                         int hiZMipCount,
                         mat4 viewProjection,
                         vec3 center,
                         float radius)
{
    if (hiZMipCount <= 0 || depthSize.x <= 0 || depthSize.y <= 0 || !(radius >= 0.0))
    {
        return false;
    }

    // 球を包む AABB の8点を投影する。8点とも w > 0 なら、投影した凸包は8点の矩形に収まり、
    // 最も手前の深度も8点のどれかにある（射影は線分に沿って深度の順序を保つ）。
    vec2 ndcMin = vec2(1.0e30);
    vec2 ndcMax = vec2(-1.0e30);
    float nearestDepth = 1.0e30;
    for (int corner = 0; corner < 8; ++corner)
    {
        vec3 offset = vec3((corner & 1) != 0 ? radius : -radius,
                           (corner & 2) != 0 ? radius : -radius,
                           (corner & 4) != 0 ? radius : -radius);
        vec4 clip = viewProjection * vec4(center + offset, 1.0);
        if (!(clip.w > 1.0e-6) || clip.z < 0.0)
        {
            return false; // カメラの後ろにかかる、または近平面をまたぐ
        }
        vec3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        nearestDepth = min(nearestDepth, ndc.z);
    }

    // NDC [-1,1] → UV [0,1]。画面の外なら遮蔽の判定をしない（視錐台の判定の仕事）。画面にかかる部分だけを見る。
    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    if (uvMax.x < 0.0 || uvMin.x > 1.0 || uvMax.y < 0.0 || uvMin.y > 1.0)
    {
        return false;
    }

    // 矩形が覆う深度の画素の範囲（両端を含む）
    ivec2 pixelMax = depthSize - ivec2(1);
    ivec2 pixelBegin = clamp(ivec2(floor(uvMin * vec2(depthSize))), ivec2(0), pixelMax);
    ivec2 pixelEnd = clamp(ivec2(floor(uvMax * vec2(depthSize))), ivec2(0), pixelMax);

    // 画素の範囲が 2^(mip+1) 画素以内なら、ミップ mip の texel は各軸で高々2つに収まる
    ivec2 pixelSpan = pixelEnd - pixelBegin;
    int mip = max(HiZCeilLog2(max(max(pixelSpan.x, pixelSpan.y), 1)) - 1, 0);
    mip = min(mip, hiZMipCount - 1);

    ivec2 baseSize = (depthSize + ivec2(1)) / 2;
    ivec2 mipSize = max(baseSize >> mip, ivec2(1));
    ivec2 texelBegin = min(pixelBegin >> (mip + 1), mipSize - ivec2(1));
    ivec2 texelEnd = min(pixelEnd >> (mip + 1), mipSize - ivec2(1));

    float farthestDepth = 0.0;
    for (int y = texelBegin.y; y <= texelEnd.y; ++y)
    {
        for (int x = texelBegin.x; x <= texelEnd.x; ++x)
        {
            farthestDepth = max(farthestDepth, texelFetch(hiZ, ivec2(x, y), mip).r);
        }
    }

    return nearestDepth - HIZ_OCCLUSION_DEPTH_BIAS > farthestDepth;
}

#endif // NORVES_HIZ_OCCLUSION_GLSL
