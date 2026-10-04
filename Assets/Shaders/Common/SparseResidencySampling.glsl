// sparse（部分常駐）テクスチャを、常駐していないタイルを読まずに標本する関数。
// 材質のシェーダー（gbuffer.frag・megageometry.frag・forward_transparent.frag）が共有する。
//
// NORVES_SPARSE_RESIDENCY_SHADING が定義されているとき（デバイスが shaderResourceResidency を有効にしたとき。
// シェーダーコンパイラが定義する）だけ、GL_ARB_sparse_texture2 の sparseTexture*ARB で標本する。
// 常駐していない（残差コードが非常駐）ときは、1段ずつ粗いミップへ下げて読み直す。
// ミップテイルは常に常駐なので、粗いミップへ下げ続ければ必ず止まる。
// 定義されていないときは従来の texture() / textureGrad() / textureLod() のまま。
//
// 拡張の宣言（#extension GL_ARB_sparse_texture2）は取り込む側のシェーダーの先頭に書く。
// VT でない材質は bVirtualTexture を false で呼ぶ（分岐は一様なので画面微分を壊さない）。

#ifdef NORVES_SPARSE_RESIDENCY_SHADING

// ミップを明示して標本する。常駐していなければ、1段ずつ粗いミップへ下げる（最初の読みで常駐していればそのミップ）。
vec4 SampleSparseResidentLod(sampler2D tex, vec2 uv, float lod)
{
    float maxLod = float(textureQueryLevels(tex) - 1);
    float level = clamp(lod, 0.0, maxLod);
    vec4 color = vec4(0.0);
    for (int step = 0; step < 16; ++step)
    {
        int code = sparseTextureLodARB(tex, uv, level, color);
        if (sparseTexelsResidentARB(code) || level >= maxLod)
        {
            return color;
        }
        level = min(floor(level) + 1.0, maxLod);
    }
    return color;
}

// 画面微分（勾配）を明示して標本する。常駐していなければ、勾配から求めたミップから1段ずつ粗いミップへ下げて読み直す。
// 異方性の最大倍率はシェーダーから分からないので、開始ミップは「異方性が最大のときの標本ミップ」
// （長軸 / ceil(長軸 / 短軸) の大きさから求める、実際の標本ミップ以下の値）にして、常駐している中間のミップを飛ばさない。
vec4 SampleSparseResidentGrad(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy)
{
    vec4 color = vec4(0.0);
    int code = sparseTextureGradARB(tex, uv, uvDx, uvDy, color);
    if (sparseTexelsResidentARB(code))
    {
        return color;
    }
    vec2 size = vec2(textureSize(tex, 0));
    vec2 texelDx = uvDx * size;
    vec2 texelDy = uvDy * size;
    float lengthMax = max(max(length(texelDx), length(texelDy)), 1.0e-4);
    float lengthMin = max(min(length(texelDx), length(texelDy)), 1.0e-4);
    float anisotropyMax = ceil(lengthMax / lengthMin - 1.0e-3);
    float lowerBoundLod = max(log2(lengthMax / max(anisotropyMax, 1.0)), 0.0);
    return SampleSparseResidentLod(tex, uv, floor(lowerBoundLod));
}

// 暗黙の勾配（画面微分）で標本する。フラグメントシェーダー専用で、動的に一様な制御フローで呼ぶ。
// 常駐していなければ、実際に標本したミップ（異方性・バイアス込みの textureQueryLOD）から1段ずつ粗いミップへ下げて読み直す。
// textureQueryLOD も画面微分を使うので、分岐の外（一様な位置）で先に求める。
vec4 SampleSparseResident(sampler2D tex, vec2 uv)
{
    float sampledLod = textureQueryLOD(tex, uv).y;
    vec4 color = vec4(0.0);
    int code = sparseTextureARB(tex, uv, color);
    if (sparseTexelsResidentARB(code))
    {
        return color;
    }
    return SampleSparseResidentLod(tex, uv, floor(max(sampledLod, 0.0)));
}

#endif

// 勾配を明示して標本する（POM のマーチ用）。
vec4 SampleMaterialTextureGrad(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentGrad(tex, uv, uvDx, uvDy);
    }
#endif
    return textureGrad(tex, uv, uvDx, uvDy);
}

// ミップを明示して標本する。
vec4 SampleMaterialTextureLod(sampler2D tex, vec2 uv, float lod, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentLod(tex, uv, lod);
    }
#endif
    return textureLod(tex, uv, lod);
}
