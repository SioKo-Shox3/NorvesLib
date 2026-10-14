// 地形の 3 層（砂・乾いた土・岩肌）の混ぜ合わせ。ラスタの megageometry.frag と、可視バッファの解決
// （Common/VisibilityResolve.glsl の材質ごとの形）が共有する。
// 1 層目（砂）は材質の通常の標本（PbrMaterialTextureSamples）をそのまま受け取り、2・3 層目はここで標本する。
// 標本は勾配を明示する（textureGrad）ので、分岐の後でも、画面微分の無い計算シェーダーでも呼べる。
// Common/PbrMaterialEvaluation.glsl・Common/SparseResidencySampling.glsl・Common/PbrMaterialTextureSamplingCore.glsl の後に取り込む。
//
// 混ぜ方:
//   1. 傾斜（幾何の法線と真上のなす角）に、低周波ノイズ（2 つの波長の値ノイズ）の揺らぎと、高さによる土への寄せを足し、
//      土・岩肌へ移る角度の範囲（smoothstep）で重みを作る。平らな所は砂、少し傾いた所は土、急な斜面は岩肌になる。
//   2. 高さ（凹凸）で重みを付ける: 各層の高さ（土・岩肌は ORM の AO、砂は一定の高さ）に重みを足し、最も高い層から
//      混ぜの深さ以内の層だけを残す。岩肌のくぼみ（AO が低い）は砂の面より低いので、境目では砂がくぼみに溜まる。

#ifndef NORVES_TERRAIN_LAYER_BLEND_GLSL
#define NORVES_TERRAIN_LAYER_BLEND_GLSL

// 混ぜ方の値（C++ の PackTerrainBlendParams と同じ並び。Library/Core/Private/Rendering/TerrainBlendShading.h）
struct TerrainBlendParams
{
    vec4 slope;  // x,y = 乾いた土へ移る傾斜の始まり・終わり（ラジアン）、z,w = 岩肌へ移る傾斜の始まり・終わり（ラジアン）
    vec4 height; // x,y = 土へ寄せる高さ（ワールドの y、m）の始まり・終わり、z = 寄せの最大（ラジアン）、w = 凹凸の混ぜの深さ
    vec4 noise;  // x,y = 揺らぎの 2 つの波長の逆数（1/m）、z = 揺らぎの振幅（ラジアン）、w = 砂の高さ（凹凸の混ぜ）
    vec4 layers; // x = 有効（1/0）、y = 土の UV の倍率、z = 岩肌の UV の倍率、w = 法線が 2 チャンネルの層のビット（1 = 土、2 = 岩肌）
};

const uint TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL = 1u;
const uint TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL = 2u;

bool IsTerrainBlendEnabled(TerrainBlendParams params)
{
    return params.layers.x > 0.5;
}

// 整数の格子点の擬似乱数（0〜1）
float TerrainLatticeHash(ivec2 cell)
{
    uvec2 q = uvec2(cell) * uvec2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    n ^= n >> 15u;
    return float(n >> 8u) * (1.0 / 16777216.0);
}

// 滑らかな値ノイズ（0〜1）。p は格子の単位の座標
float TerrainValueNoise(vec2 p)
{
    const vec2 cellOrigin = floor(p);
    const vec2 f = p - cellOrigin;
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const ivec2 c = ivec2(cellOrigin);
    const float a = TerrainLatticeHash(c);
    const float b = TerrainLatticeHash(c + ivec2(1, 0));
    const float d = TerrainLatticeHash(c + ivec2(0, 1));
    const float e = TerrainLatticeHash(c + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

// 傾斜・高さ・ノイズから、砂・土・岩肌の重み（合計 1）を求める。worldPosition はワールドの位置、
// geometricNormal は補間した頂点の法線（法線マップを掛ける前。正規化していなくてよい）
vec3 ComputeTerrainSlopeWeights(TerrainBlendParams params, vec3 worldPosition, vec3 geometricNormal)
{
    const vec3 n = normalize(geometricNormal);
    const float slope = acos(clamp(n.y, -1.0, 1.0));
    // 2 つの波長の値ノイズを重ね、-1〜1 の揺らぎにする（2 つ目は格子をずらして重ならないようにする）
    const float noise = TerrainValueNoise(worldPosition.xz * params.noise.x) * 0.65 +
                        TerrainValueNoise(worldPosition.xz * params.noise.y + vec2(17.31, -41.7)) * 0.35;
    const float jitter = (noise * 2.0 - 1.0) * params.noise.z;
    const float heightBias = smoothstep(params.height.x, params.height.y, worldPosition.y) * params.height.z;
    const float dirt = smoothstep(params.slope.x, params.slope.y, slope + jitter + heightBias);
    const float rock = smoothstep(params.slope.z, params.slope.w, slope + jitter);
    return vec3((1.0 - dirt) * (1.0 - rock), dirt * (1.0 - rock), rock);
}

// 高さ（凹凸）で重みを付け直す。layerHeights は各層の高さ（0〜1）。重みが 0 の層は高さを足さない
// （重みが 0 へ近づくと高さの寄与も 0 へ近づくので、層が急に現れない）。最も高い層から depth 以内だけを残して正規化する
vec3 ApplyTerrainHeightBlend(vec3 weights, vec3 layerHeights, float depth)
{
    const vec3 h = weights + layerHeights * clamp(weights * 4.0, 0.0, 1.0);
    const float top = max(max(h.x, h.y), h.z) - depth;
    const vec3 w = max(h - vec3(top), vec3(0.0)) * step(vec3(1.0e-4), weights);
    const float sum = w.x + w.y + w.z;
    return sum > 1.0e-6 ? w / sum : weights;
}

// 勾配から等方の標本ミップ（MegaGeometry の材質は等方の Linear のサンプラー）。VT の非常駐の逃げ始めのミップにだけ使う
float TerrainLayerLodFromGradient(sampler2D tex, vec2 uvDx, vec2 uvDy)
{
    const vec2 size = vec2(textureSize(tex, 0));
    const float pMax = max(length(uvDx * size), length(uvDy * size));
    return pMax > 0.0 ? clamp(log2(pMax), 0.0, float(textureQueryLevels(tex) - 1)) : 0.0;
}

// 2・3 層目の 1 層を標本して材質値へ復号する（ORM は必須）。uvScale は材質の UV に掛ける倍率
PbrMaterialTextureSamples SampleTerrainLayer(sampler2D albedoTex, sampler2D normalTex, sampler2D ormTex,
                                             vec2 texCoord, vec2 uvDx, vec2 uvDy, float uvScale,
                                             bool bNormalTwoChannel, bool bVirtualTexture)
{
    const vec2 uv = texCoord * uvScale;
    const vec2 dx = uvDx * uvScale;
    const vec2 dy = uvDy * uvScale;
    float albedoLod = 0.0;
    float normalLod = 0.0;
    float ormLod = 0.0;
    if (bVirtualTexture)
    {
        albedoLod = TerrainLayerLodFromGradient(albedoTex, dx, dy);
        normalLod = TerrainLayerLodFromGradient(normalTex, dx, dy);
        ormLod = TerrainLayerLodFromGradient(ormTex, dx, dy);
    }
    const vec4 albedo = SampleMaterialTextureGrad(albedoTex, uv, dx, dy, albedoLod, bVirtualTexture);
    const vec4 normalSample = SampleMaterialTextureGrad(normalTex, uv, dx, dy, normalLod, bVirtualTexture);
    const vec3 material = DecodePbrOrmSample(SampleMaterialTextureGrad(ormTex, uv, dx, dy, ormLod, bVirtualTexture));
    return DecodePbrMaterialTextureSamples(albedo, normalSample, material.x, material.y, material.z, bNormalTwoChannel);
}

// 1 層目（砂）の標本 sand に、土・岩肌の層を混ぜる。texCoord・uvDx・uvDy は 1 層目の UV とその勾配。
// 接空間の法線は傾き（xy / z）で混ぜる（3 層とも同じ UV の向きなので、同じ接空間の基底で表される）
PbrMaterialTextureSamples BlendTerrainLayers(TerrainBlendParams params, PbrMaterialTextureSamples sand,
                                             sampler2D dirtAlbedo, sampler2D dirtNormal, sampler2D dirtOrm,
                                             sampler2D rockAlbedo, sampler2D rockNormal, sampler2D rockOrm,
                                             vec2 texCoord, vec2 uvDx, vec2 uvDy,
                                             vec3 worldPosition, vec3 geometricNormal, bool bVirtualTexture)
{
    const vec3 slopeWeights = ComputeTerrainSlopeWeights(params, worldPosition, geometricNormal);
    const uint normalBits = uint(params.layers.w + 0.5);

    PbrMaterialTextureSamples dirt = sand;
    PbrMaterialTextureSamples rock = sand;
    if (slopeWeights.y > 0.0)
    {
        dirt = SampleTerrainLayer(dirtAlbedo, dirtNormal, dirtOrm, texCoord, uvDx, uvDy, params.layers.y,
                                  (normalBits & TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL) != 0u, bVirtualTexture);
    }
    if (slopeWeights.z > 0.0)
    {
        rock = SampleTerrainLayer(rockAlbedo, rockNormal, rockOrm, texCoord, uvDx, uvDy, params.layers.z,
                                  (normalBits & TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL) != 0u, bVirtualTexture);
    }

    // 凹凸の高さ: 砂は一定の面、土・岩肌は AO（Material.z）。くぼみほど低い
    const vec3 layerHeights = vec3(params.noise.w, dirt.Material.z, rock.Material.z);
    const vec3 w = ApplyTerrainHeightBlend(slopeWeights, layerHeights, params.height.w);

    PbrMaterialTextureSamples result;
    result.Albedo = sand.Albedo * w.x + dirt.Albedo * w.y + rock.Albedo * w.z;
    const vec2 slopeSand = sand.TangentNormal.xy / max(sand.TangentNormal.z, 0.1);
    const vec2 slopeDirt = dirt.TangentNormal.xy / max(dirt.TangentNormal.z, 0.1);
    const vec2 slopeRock = rock.TangentNormal.xy / max(rock.TangentNormal.z, 0.1);
    result.TangentNormal = normalize(vec3(slopeSand * w.x + slopeDirt * w.y + slopeRock * w.z, 1.0));
    result.Material = sand.Material * w.x + dirt.Material * w.y + rock.Material * w.z;
    return result;
}

#endif // NORVES_TERRAIN_LAYER_BLEND_GLSL
