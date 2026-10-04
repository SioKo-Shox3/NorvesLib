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

// アルベドの標本が非常駐で粗いミップへ逃げたか（SamplePbrMaterialTextures が書く。VT のフィードバックが、
// 巡回の画素でなくても要求を書くための印。VT でない材質・常駐の照会が使えないデバイスでは常に false）。
bool g_VirtualTextureAlbedoEscaped = false;

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

// 画面微分（勾配）を明示して標本する。常駐していなければ、sampledLod（実際の標本ミップ）から1段ずつ粗いミップへ下げて読み直す。
// 実際の標本ミップは、サンプラーの異方性の上限・バイアス・クランプを含むのでシェーダーでは求め切れない。
// 呼び出し側が、同じ勾配を持つ UV の textureQueryLOD(tex, uv).y を、画面微分の使える一様な位置で先に取って渡す
// （コンピュートなど画面微分が無い場所では、実際の標本ミップが分かる値を渡す）。
vec4 SampleSparseResidentGrad(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy, float sampledLod)
{
    vec4 color = vec4(0.0);
    int code = sparseTextureGradARB(tex, uv, uvDx, uvDy, color);
    if (sparseTexelsResidentARB(code))
    {
        return color;
    }
    return SampleSparseResidentLod(tex, uv, floor(max(sampledLod, 0.0)));
}

// 暗黙の勾配（画面微分）で標本する。フラグメントシェーダー専用で、動的に一様な制御フローで呼ぶ。
// 常駐していなければ、実際に標本したミップ（異方性・バイアス込みの textureQueryLOD）から1段ずつ粗いミップへ下げて読み直す。
// textureQueryLOD も画面微分を使うので、分岐の外（一様な位置）で先に求める。
// bEscaped には、常駐していなくて粗いミップへ逃げたかを返す。
vec4 SampleSparseResidentTracked(sampler2D tex, vec2 uv, out bool bEscaped)
{
    float sampledLod = textureQueryLOD(tex, uv).y;
    vec4 color = vec4(0.0);
    int code = sparseTextureARB(tex, uv, color);
    if (sparseTexelsResidentARB(code))
    {
        bEscaped = false;
        return color;
    }
    bEscaped = true;
    return SampleSparseResidentLod(tex, uv, floor(max(sampledLod, 0.0)));
}

vec4 SampleSparseResident(sampler2D tex, vec2 uv)
{
    bool bEscaped;
    return SampleSparseResidentTracked(tex, uv, bEscaped);
}

#endif

// 勾配を明示して標本する（POM のマーチ用）。sampledLod は、その勾配で実際に標本されるミップ
// （textureQueryLOD(tex, 元の uv).y。bVirtualTexture のときだけ使う。非常駐のときの逃げ始めのミップ）。
vec4 SampleMaterialTextureGrad(sampler2D tex, vec2 uv, vec2 uvDx, vec2 uvDy, float sampledLod, bool bVirtualTexture)
{
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
    if (bVirtualTexture)
    {
        return SampleSparseResidentGrad(tex, uv, uvDx, uvDy, sampledLod);
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
