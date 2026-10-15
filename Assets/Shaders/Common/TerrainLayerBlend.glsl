// 地形の 3 層（砂・乾いた土・岩肌）の混ぜ合わせ。ラスタの megageometry.frag と、可視バッファの解決
// （Common/VisibilityResolve.glsl の材質ごとの形）が共有する。
// 3 層ともここでワールドの位置から標本する（1 層目の砂は材質のアルベド・法線・ORM の枠のテクスチャを使う）。
// 標本は勾配を明示する（textureGrad）ので、分岐の後でも、画面微分の無い計算シェーダーでも呼べる。
// 法線はワールドの向きで返す（呼び出し側は接空間の基底を掛けない）。
// Common/PbrMaterialEvaluation.glsl・Common/SparseResidencySampling.glsl・Common/PbrMaterialTextureSamplingCore.glsl の後に取り込む。
//
// 混ぜ方:
//   1. 地表の分類の地図（R = 露岩・G = 崖錐・B = 地表（細粒）・A = 地表（礫）の重み）があり、画素が地図の範囲の中なら、
//      地図の重みを 3 層へ写す（露岩 → 岩肌、崖錐 → 土、細粒 → 砂、礫 → 決めた割合で土・残りを砂）。地図はワールドの XZ で
//      引くので、地形の LOD の段（幾何の法線）が変わっても分類は動かない。引く位置を数 m の低周波ノイズでずらし、
//      地図の画素の格子の直線の境を崩す。地図の外は下の傾斜の式で決め、外縁の内側の帯で 2 つを混ぜてつなぐ。
//      傾斜の式（地図の無い所の予備）: 傾斜（幾何の法線と真上のなす角）に、低周波ノイズ（2 つの波長の値ノイズ）の揺らぎと、
//      高さによる土への寄せを足し、土・岩肌へ移る角度の範囲（smoothstep）で重みを作る。平らな所は砂、少し傾いた所は土、
//      急な斜面は岩肌になる。
//   2. 高さ（凹凸）で重みを付ける: 各層の高さ（土・岩肌は ORM の AO、砂は一定の高さ）に重みを足し、最も高い層から
//      混ぜの深さ以内の層だけを残す。岩肌のくぼみ（AO が低い）は砂の面より低いので、境目では砂がくぼみに溜まる。
//
// 標本の仕方:
//   - 3 層ともトライプラナー（X・Y・Z の 3 方向の投影を法線の向きで重み付け。UV = ワールドの位置 ÷ テクスチャの 1 枚の広さ）で、
//     崖でも引き伸ばされない。平らな所は真上からの投影だけになる（小さな重みの投影は標本しない）。
//   - 距離で UV の倍率を変える: カメラからの距離が「変え始める距離」の r 倍になるごとに 1 段上がり、段ごとに UV を 1/r にする
//     （近くは細かく、遠くは大きく）。隣り合う 2 つの段を距離で混ぜ、段ごとに UV を回して位置をずらすので、
//     同じ模様が格子状に並ばない。
//   - 模様の 1 枚が画面で数画素まで縮む遠くでは、標本の勾配を広げて 1 枚の平均の色へ寄せ、法線マップの傾きを 0 へ落とす
//     （繰り返しの周期の縞と点の格子を出さない）。
//   - アルベドの画像は sRGB で符号化されている。UNORM の形式で張られた層（layers.w のビット）は標本ごとに線形へ戻す。
//   - 各層のアルベドに、層ごとに別の位相の低周波ノイズ（数十〜数百 m と、模様の繰り返しを崩す十数 m）で明るさと色味の揺らぎを
//     掛けてから混ぜる。揺らぎは分類の境で入れ替わり、全域を 1 つの斑の模様で覆わない。
//   - 岩肌の層には、ワールドの y（共通の基準面）からの高さで決まる砂岩の地層と、崖の上端から垂れる砂漠ワニスを掛ける
//     （Common/TerrainStrata.glsl）。
//
// 遠くの描き方（画素より細かいものを画素ごとに標本しない）:
//   - 遠景の地図（R・G = 16 m の画素の平均の法線の X・Z、B = 岩肌・A = 土の重み。Scripts/Terrain/FarMacro.py が DEM と地表の分類から
//     焼き、ミップで前もって縮小してある）の 1 画素が画面で数画素以下に縮む遠くでは、分類の重みと法線を地図の値へ移す。
//     DEM の三角形が画面で 1〜2 画素になる遠くで、画素ごとの幾何の法線から傾斜の式で分類すると、岩肌と砂が画素ごとに
//     飛び飛びに入れ替わって斑点になり、カメラが動くとちらつく。
//   - 揺らぎの値ノイズは、波長が画素の数倍より短くなると平均（0.5）へ寄せる（標本が飛び飛びの値になってざらつく）。
//   - 模様の 1 枚が縮んで法線マップの傾きを落とした分は、その層の法線の傾きの分散（params.farMix.yzw）を粗さの α² に足して
//     鏡面の広がりへ移す（細かい凹凸が画素ごとに鋭く光るちらつきを出さず、遠くの面の光り方を細かい凹凸のある面のままにする）。

#ifndef NORVES_TERRAIN_LAYER_BLEND_GLSL
#define NORVES_TERRAIN_LAYER_BLEND_GLSL

// 混ぜ方の値（C++ の PackTerrainBlendParams と同じ並び。Library/Core/Private/Rendering/TerrainBlendShading.h）
struct TerrainBlendParams
{
    vec4 slope;  // x,y = 乾いた土へ移る傾斜の始まり・終わり（ラジアン）、z,w = 岩肌へ移る傾斜の始まり・終わり（ラジアン）
    vec4 height; // x,y = 土へ寄せる高さ（ワールドの y、m）の始まり・終わり、z = 寄せの最大（ラジアン）、w = 凹凸の混ぜの深さ
    vec4 noise;  // x,y = 揺らぎの 2 つの波長の逆数（1/m）、z = 揺らぎの振幅（ラジアン）、w = 砂の高さ（凹凸の混ぜ）
    vec4 layers; // x = 有効（1/0）、y = 土の UV の倍率、z = 岩肌の UV の倍率、w = ビット（法線が 2 チャンネル: 1 = 土、2 = 岩肌。
                 // アルベドを sRGB から線形へ戻す: 4 = 砂、8 = 土、16 = 岩肌）
    vec4 macro;  // x,y = 明るさ・色味の揺らぎの 2 つの波長の逆数（1/m）、z = 明るさの揺らぎの振幅（割合）、w = 色味の揺らぎの振幅
    vec4 detail; // x = UV の倍率を変え始める距離（m）、y = 1 段ごとの距離の比の log2、z = 段の上限、w = テクスチャの 1 枚の広さ（m）
    vec4 mask;    // x,y = 地表の分類の地図の外縁のワールドの X・Z の最小、z,w = X・Z の広さの逆数（1/m。z が 0 なら地図なし）
    vec4 maskMix; // x = 礫の重みを土へ回す割合、y = 外縁の内側で傾斜の式から地図へ移す帯の幅（m）、z = 引く位置のずらしの幅（m）、
                  // w = 確認用の色分け（1 ならアルベドを岩肌 = 赤・土 = 緑・砂 = 青の重みにする。TerrainBlendShading.h）
    vec4 farMap;  // x,y = 遠景の地図の外縁のワールドの X・Z の最小、z,w = X・Z の広さの逆数（1/m。z が 0 なら地図なし）
    vec4 farMix;  // x = 遠景の地図の 1 画素の広さ（m）、y,z,w = 砂・土・岩肌の法線マップの傾きの分散（細かい法線を落とした分を粗さへ）
};

const uint TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL = 1u;
const uint TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL = 2u;
const uint TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE = 4u;
const uint TERRAIN_BLEND_DIRT_ALBEDO_SRGB_DECODE = 8u;
const uint TERRAIN_BLEND_ROCK_ALBEDO_SRGB_DECODE = 16u;

// 地図を引く位置をずらすノイズの波長（m）
const float TERRAIN_SURFACE_MASK_WARP_WAVELENGTH = 7.0;
// 地図の重みのうち捨てる小さな値（圧縮（BC7）の誤差で平らな所に残るわずかな重みで、その層を標本しない）
const float TERRAIN_SURFACE_MASK_MIN_WEIGHT = 0.01;

// トライプラナーの重みの鋭さ（法線の成分の何乗で重みにするか）と、捨てる小さな重み（その投影は標本しない）
const float TERRAIN_TRIPLANAR_SHARPNESS = 4.0;
const float TERRAIN_TRIPLANAR_CUTOFF = 0.05;
// 段ごとの UV の回転（黄金角。段を重ねても向きがそろわない）と、位置のずらし（UV の単位）
const float TERRAIN_DETAIL_LEVEL_ROTATION = 2.39996323;
const vec2 TERRAIN_DETAIL_LEVEL_OFFSET = vec2(0.3183, 0.6180);
// 模様の 1 枚の画面上の大きさ（画素）で、1 枚の中の模様を残す範囲。1 枚が数画素まで縮むと、ミップの標本は 1 枚ごとの低い周波数の
// 明暗と傾き（写真の層理・割れ目の偏り）になり、繰り返しの周期で並ぶ縞と点の格子として出る（日の当たる向きの境の斜面では
// 法線の傾きで NdotL の符号が入れ替わり、暗い点になる）。1 枚が FULL 画素から ZERO 画素へ縮む間に、標本の勾配を広げて
// アルベド・法線・ORM を 1 枚の平均（最も粗いミップ）へ寄せ、法線の傾きも 0 へ落とす。ZERO 画素で広げた足跡が 1 枚を覆う
const float TERRAIN_TILE_FADE_PIXELS_FULL = 32.0;
const float TERRAIN_TILE_FADE_PIXELS_ZERO = 8.0;
// 勾配を広げる倍率の指数。倍率 (FULL / 1 枚の画素数)^指数 は、ZERO 画素で ZERO 倍（足跡が 1 枚 = 最も粗いミップ）になる
const float TERRAIN_TILE_FADE_GRADIENT_EXPONENT =
    log2(TERRAIN_TILE_FADE_PIXELS_ZERO) / log2(TERRAIN_TILE_FADE_PIXELS_FULL / TERRAIN_TILE_FADE_PIXELS_ZERO);
// 遠景の地図の 1 画素の画面上の大きさ（画素）で、分類と法線を地図へ移す範囲。FULL 画素以下で地図だけ、ZERO 画素以上で画素ごとの式
// （DEM の三角形が 1〜2 画素に縮むと幾何の法線が画素ごとに飛ぶ。地図の 1 画素が 6 画素ほどに写る所から移し始める）
const float TERRAIN_FAR_MAP_PIXELS_FULL = 2.0;
const float TERRAIN_FAR_MAP_PIXELS_ZERO = 6.0;
// 地図の外縁から内側へ、画素ごとの式から地図へ移す帯の幅（地図の画素の数）
const float TERRAIN_FAR_MAP_EDGE_TEXELS = 16.0;
// 値ノイズを平均へ寄せる範囲（1 画素の広さが波長の何倍か）。波長の 1/4 で寄せ始め、3/4 で平均だけにする
const vec2 TERRAIN_NOISE_FILTER_RANGE = vec2(0.25, 0.75);

// 画素の面（ワールドの位置とその画素あたりの変化、幾何の法線、カメラからの距離）
struct TerrainSurfaceInput
{
    vec3 WorldPosition;
    vec3 WorldDx; // 画素の x が 1 増えたときのワールドの位置の変化
    vec3 WorldDy; // 画素の y が 1 増えたときのワールドの位置の変化
    vec3 GeometricNormal; // 補間した頂点の法線（法線マップを掛ける前。正規化していなくてよい）
    float CameraDistance;
};

// 1 層の標本（重み付きの和）。Perturbation は法線の傾き（ワールドの向き。幾何の法線に足して正規化する）。
// LostDetail は法線マップの傾きを落とした割合（傾きの分散の 1 - 残した割合^2。粗さへ移す）
struct TerrainLayerSample
{
    vec4 Albedo;
    vec3 Material;
    vec3 Perturbation;
    float LostDetail;
};

// 遠景の地図の標本。Weights は砂・土・岩肌の重み（合計 1）、Normal は画素の平均の法線、Amount は地図へ移す割合（0〜1）
struct TerrainFarSample
{
    vec3 Weights;
    vec3 Normal;
    float Amount;
};

// 3 方向の投影の、面に沿った UV の向き（幾何の法線の接平面へ落とした軸）
struct TerrainProjectionFrame
{
    vec3 TopU;   // 真上からの投影（UV = XZ）: X・Z
    vec3 TopV;
    vec3 SideXU; // X の向きの投影（UV = ZY）: Z・Y
    vec3 SideXV;
    vec3 SideZU; // Z の向きの投影（UV = XY）: X・Y
    vec3 SideZV;
};

// 距離で選ぶ 2 つの段と、その重み
struct TerrainDetailLevels
{
    vec2 Level;
    vec2 Weight;
    float Log2Ratio;
};

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

// 値ノイズを、1 画素の広さ cellsPerPixel（格子の単位）に応じて平均（0.5）へ寄せる。画素が格子の数分の 1 より広いと、
// 画素ごとの標本は飛び飛びの値になり、ざらつきとちらつきになる
float TerrainValueNoiseFiltered(vec2 p, float cellsPerPixel)
{
    const float fade = smoothstep(TERRAIN_NOISE_FILTER_RANGE.x, TERRAIN_NOISE_FILTER_RANGE.y, cellsPerPixel);
    return fade >= 1.0 ? 0.5 : mix(TerrainValueNoise(p), 0.5, fade);
}

// 1 画素のワールドでの広さ（m。画面の x・y の向きの長い方）
float TerrainPixelMeters(TerrainSurfaceInput surface)
{
    return max(length(surface.WorldDx), length(surface.WorldDy));
}

// 傾斜・高さ・ノイズから、砂・土・岩肌の重み（合計 1）を求める。n は正規化した法線、pixelMeters は 1 画素の広さ（m）
vec3 ComputeTerrainSlopeWeights(TerrainBlendParams params, vec3 worldPosition, vec3 n, float pixelMeters)
{
    const float slope = acos(clamp(n.y, -1.0, 1.0));
    // 2 つの波長の値ノイズを重ね、-1〜1 の揺らぎにする（2 つ目は格子をずらして重ならないようにする）
    const float noise =
        TerrainValueNoiseFiltered(worldPosition.xz * params.noise.x, pixelMeters * params.noise.x) * 0.65 +
        TerrainValueNoiseFiltered(worldPosition.xz * params.noise.y + vec2(17.31, -41.7), pixelMeters * params.noise.y) * 0.35;
    const float jitter = (noise * 2.0 - 1.0) * params.noise.z;
    const float heightBias = smoothstep(params.height.x, params.height.y, worldPosition.y) * params.height.z;
    const float dirt = smoothstep(params.slope.x, params.slope.y, slope + jitter + heightBias);
    const float rock = smoothstep(params.slope.z, params.slope.w, slope + jitter);
    return vec3((1.0 - dirt) * (1.0 - rock), dirt * (1.0 - rock), rock);
}

// 地表の分類の地図から、砂・土・岩肌の重み（xyz。合計 1）と、地図の効きの割合（w。0 = 傾斜の式だけ、1 = 地図だけ）を求める。
// 地図の外・地図なし・重みの無い画素は w = 0 を返す（地図のテクスチャを読まない）
vec4 ComputeTerrainSurfaceMaskWeights(TerrainBlendParams params, sampler2D surfaceMask, TerrainSurfaceInput surface)
{
    const vec2 invSize = params.mask.zw;
    if (invSize.x <= 0.0 || invSize.y <= 0.0)
    {
        return vec4(0.0);
    }
    vec2 xz = surface.WorldPosition.xz;
    if (params.maskMix.z > 0.0)
    {
        // ずらしのノイズも、画素が波長に近づく遠くでは平均（ずらし 0）へ寄せる
        const vec2 p = xz * (1.0 / TERRAIN_SURFACE_MASK_WARP_WAVELENGTH);
        const float cellsPerPixel = TerrainPixelMeters(surface) * (1.0 / TERRAIN_SURFACE_MASK_WARP_WAVELENGTH);
        const vec2 warp = vec2(TerrainValueNoiseFiltered(p + vec2(-23.17, 5.71), cellsPerPixel),
                               TerrainValueNoiseFiltered(p + vec2(11.39, 37.53), cellsPerPixel));
        xz += (warp * 2.0 - 1.0) * params.maskMix.z;
    }
    const vec2 uv = (xz - params.mask.xy) * invSize;
    // 外縁からの距離（m。外は負）で、傾斜の式から地図へ移す
    const vec2 edgeMeters = min(uv, vec2(1.0) - uv) / invSize;
    const float coverage = clamp(min(edgeMeters.x, edgeMeters.y) / max(params.maskMix.y, 1.0e-3), 0.0, 1.0);
    if (coverage <= 0.0)
    {
        return vec4(0.0);
    }
    const vec4 classes = textureGrad(surfaceMask, uv, surface.WorldDx.xz * invSize, surface.WorldDy.xz * invSize);
    const float total = classes.r + classes.g + classes.b + classes.a;
    if (total < 1.0e-3)
    {
        return vec4(0.0);
    }
    const float gravelToDirt = params.maskMix.x;
    const vec3 weights = vec3(classes.b + classes.a * (1.0 - gravelToDirt), classes.g + classes.a * gravelToDirt, classes.r);
    const vec3 kept = max(weights / total - vec3(TERRAIN_SURFACE_MASK_MIN_WEIGHT), vec3(0.0));
    return vec4(kept / max(kept.x + kept.y + kept.z, 1.0e-6), coverage);
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

// 軸 axis を法線 n の接平面へ落として正規化する（退化したら軸のまま）
vec3 TerrainTangentAxis(vec3 axis, vec3 n)
{
    const vec3 t = axis - n * dot(n, axis);
    const float lengthSq = dot(t, t);
    return lengthSq > 1.0e-8 ? t * inversesqrt(lengthSq) : axis;
}

TerrainProjectionFrame MakeTerrainProjectionFrame(vec3 n)
{
    TerrainProjectionFrame frame;
    frame.TopU = TerrainTangentAxis(vec3(1.0, 0.0, 0.0), n);
    frame.TopV = TerrainTangentAxis(vec3(0.0, 0.0, 1.0), n);
    frame.SideXU = TerrainTangentAxis(vec3(0.0, 0.0, 1.0), n);
    frame.SideXV = TerrainTangentAxis(vec3(0.0, 1.0, 0.0), n);
    frame.SideZU = TerrainTangentAxis(vec3(1.0, 0.0, 0.0), n);
    frame.SideZV = TerrainTangentAxis(vec3(0.0, 1.0, 0.0), n);
    return frame;
}

// トライプラナーの重み（x = X の向きの投影、y = 真上、z = Z の向き。合計 1）。小さな重みは捨てて標本の数を減らす
vec3 ComputeTerrainTriplanarWeights(vec3 n)
{
    vec3 w = pow(abs(n), vec3(TERRAIN_TRIPLANAR_SHARPNESS));
    w /= max(w.x + w.y + w.z, 1.0e-6);
    w = max(w - vec3(TERRAIN_TRIPLANAR_CUTOFF), vec3(0.0));
    return w / max(w.x + w.y + w.z, 1.0e-6);
}

// カメラからの距離で、混ぜる 2 つの段とその重みを選ぶ
TerrainDetailLevels ComputeTerrainDetailLevels(TerrainBlendParams params, float cameraDistance)
{
    TerrainDetailLevels levels;
    levels.Log2Ratio = max(params.detail.y, 1.0e-3);
    const float start = max(params.detail.x, 1.0e-3);
    const float maxLevel = max(params.detail.z, 0.0);
    const float level = clamp(log2(max(cameraDistance, start) / start) / levels.Log2Ratio, 0.0, maxLevel);
    const float base = min(floor(level), maxLevel);
    const float t = smoothstep(0.0, 1.0, level - base);
    levels.Level = vec2(base, base + 1.0);
    levels.Weight = vec2(1.0 - t, t);
    return levels;
}

// 勾配から等方の標本ミップ。VT の非常駐の逃げ始めのミップにだけ使う
float TerrainLayerLodFromGradient(sampler2D tex, vec2 uvDx, vec2 uvDy)
{
    const vec2 size = vec2(textureSize(tex, 0));
    const float pMax = max(length(uvDx * size), length(uvDy * size));
    return pMax > 0.0 ? clamp(log2(pMax), 0.0, float(textureQueryLevels(tex) - 1)) : 0.0;
}

// sRGB の符号化した値を線形へ戻す（UNORM の形式で張られたアルベド）
vec3 TerrainSrgbToLinear(vec3 encoded)
{
    const vec3 c = clamp(encoded, vec3(0.0), vec3(1.0));
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}

// 1 つの投影を標本して acc へ重み付きで足す。uv・uvDx・uvDy は回す前の UV とその勾配、rotation は段の回転。
// 接空間の法線の傾きは回した UV の向きで表されるので、回転を戻してから面の軸（axisU・axisV）へ載せる
void AccumulateTerrainProjection(inout TerrainLayerSample acc, float weight,
                                 sampler2D albedoTex, sampler2D normalTex, sampler2D ormTex,
                                 bool bNormalTwoChannel, bool bAlbedoSrgbDecode, bool bVirtualTexture,
                                 vec2 uv, vec2 uvDx, vec2 uvDy, mat2 rotation, vec2 offset, vec3 axisU, vec3 axisV)
{
    const vec2 ruv = rotation * uv + offset;
    // 1 画素あたりの UV の変化は 1 枚の何分の 1 か（UV の 1 が模様の 1 枚）。その逆数が 1 枚の画面上の画素数。
    // 1 枚が小さく写るほど勾配を広げ（向きと縦横の比は保つ）、法線の傾きを弱める
    const float uvPerPixel = max(length(uvDx), length(uvDy));
    const float tilePixels = uvPerPixel > 0.0 ? 1.0 / uvPerPixel : TERRAIN_TILE_FADE_PIXELS_FULL;
    const float gradientScale =
        pow(max(TERRAIN_TILE_FADE_PIXELS_FULL / max(tilePixels, 1.0e-3), 1.0), TERRAIN_TILE_FADE_GRADIENT_EXPONENT);
    const float normalFade = smoothstep(TERRAIN_TILE_FADE_PIXELS_ZERO, TERRAIN_TILE_FADE_PIXELS_FULL, tilePixels);
    const vec2 dx = rotation * uvDx * gradientScale;
    const vec2 dy = rotation * uvDy * gradientScale;
    float albedoLod = 0.0;
    float normalLod = 0.0;
    float ormLod = 0.0;
    if (bVirtualTexture)
    {
        albedoLod = TerrainLayerLodFromGradient(albedoTex, dx, dy);
        normalLod = TerrainLayerLodFromGradient(normalTex, dx, dy);
        ormLod = TerrainLayerLodFromGradient(ormTex, dx, dy);
    }
    vec4 albedo = SampleMaterialTextureGrad(albedoTex, ruv, dx, dy, albedoLod, bVirtualTexture);
    if (bAlbedoSrgbDecode)
    {
        albedo.rgb = TerrainSrgbToLinear(albedo.rgb);
    }
    const vec3 tangentNormal =
        DecodePbrTangentNormal(SampleMaterialTextureGrad(normalTex, ruv, dx, dy, normalLod, bVirtualTexture),
                               bNormalTwoChannel);
    const vec3 material = DecodePbrOrmSample(SampleMaterialTextureGrad(ormTex, ruv, dx, dy, ormLod, bVirtualTexture));
    const vec2 slope = transpose(rotation) * (tangentNormal.xy / max(tangentNormal.z, 0.1)) * normalFade;
    acc.Albedo += albedo * weight;
    acc.Material += material * weight;
    acc.Perturbation += (axisU * slope.x + axisV * slope.y) * weight;
    acc.LostDetail += (1.0 - normalFade * normalFade) * weight;
}

// 1 層を、距離の 2 つの段 × 投影（projection の重みが 0 でないもの）で標本する。uvScale は層の UV の倍率
TerrainLayerSample SampleTerrainLayer(sampler2D albedoTex, sampler2D normalTex, sampler2D ormTex,
                                      bool bNormalTwoChannel, bool bAlbedoSrgbDecode, bool bVirtualTexture,
                                      TerrainSurfaceInput surface, TerrainProjectionFrame frame, vec3 projection,
                                      TerrainDetailLevels levels, float uvScale, float tileMeters)
{
    TerrainLayerSample acc;
    acc.Albedo = vec4(0.0);
    acc.Material = vec3(0.0);
    acc.Perturbation = vec3(0.0);
    acc.LostDetail = 0.0;
    const vec3 p = surface.WorldPosition;
    const vec3 pdx = surface.WorldDx;
    const vec3 pdy = surface.WorldDy;
    for (int index = 0; index < 2; ++index)
    {
        const float levelWeight = levels.Weight[index];
        if (levelWeight <= 1.0e-3)
        {
            continue;
        }
        const float level = levels.Level[index];
        const float scale = uvScale * exp2(-level * levels.Log2Ratio) / tileMeters;
        const float angle = level * TERRAIN_DETAIL_LEVEL_ROTATION;
        const float c = cos(angle);
        const float s = sin(angle);
        const mat2 rotation = mat2(c, s, -s, c);
        const vec2 offset = TERRAIN_DETAIL_LEVEL_OFFSET * level;
        if (projection.y > 0.0)
        {
            AccumulateTerrainProjection(acc, levelWeight * projection.y, albedoTex, normalTex, ormTex,
                                        bNormalTwoChannel, bAlbedoSrgbDecode, bVirtualTexture, p.xz * scale, pdx.xz * scale,
                                        pdy.xz * scale, rotation, offset, frame.TopU, frame.TopV);
        }
        if (projection.x > 0.0)
        {
            AccumulateTerrainProjection(acc, levelWeight * projection.x, albedoTex, normalTex, ormTex,
                                        bNormalTwoChannel, bAlbedoSrgbDecode, bVirtualTexture, p.zy * scale, pdx.zy * scale,
                                        pdy.zy * scale, rotation, offset, frame.SideXU, frame.SideXV);
        }
        if (projection.z > 0.0)
        {
            AccumulateTerrainProjection(acc, levelWeight * projection.z, albedoTex, normalTex, ormTex,
                                        bNormalTwoChannel, bAlbedoSrgbDecode, bVirtualTexture, p.xy * scale, pdx.xy * scale,
                                        pdy.xy * scale, rotation, offset, frame.SideZU, frame.SideZV);
        }
    }
    return acc;
}

// 層ごとの明るさと色味（赤み・黄み）の揺らぎの倍率。数十〜数百 m の低周波ノイズに、模様の繰り返し（数 m の 1 枚）を崩す
// 波長 1/4 のノイズを足す。seed で層ごとに位相を変えるので、揺らぎは分類（層）の境で入れ替わる。
// pixelMeters は 1 画素の広さ（m）で、画素より細かい揺らぎは平均へ寄せる
vec3 TerrainMacroVariation(TerrainBlendParams params, vec2 xz, vec2 seed, float pixelMeters)
{
    const float a = TerrainValueNoiseFiltered(xz * params.macro.x + vec2(-7.31, 3.17) + seed, pixelMeters * params.macro.x);
    const float b = TerrainValueNoiseFiltered(xz * params.macro.y + vec2(29.71, -13.93) + seed, pixelMeters * params.macro.y);
    const float d = TerrainValueNoiseFiltered(xz * params.macro.x * 4.0 + vec2(17.03, -41.57) + seed,
                                              pixelMeters * params.macro.x * 4.0);
    const float brightness = 1.0 + params.macro.z * ((a * 0.45 + b * 0.3 + d * 0.25) * 2.0 - 1.0);
    const float c = TerrainValueNoiseFiltered(xz * params.macro.x * 1.7 + vec2(61.37, 44.21) + seed,
                                              pixelMeters * params.macro.x * 1.7);
    const float hue = params.macro.w * (c * 2.0 - 1.0);
    return brightness * vec3(1.0 + hue, 1.0, 1.0 - hue);
}

// 遠景の地図から、分類の重みと法線と、地図へ移す割合を引く。地図なし・地図の外・地図の画素が画面で大きく写る近くは Amount = 0
// （地図のテクスチャを読まない）
TerrainFarSample SampleTerrainFarMap(TerrainBlendParams params, sampler2D farMap, TerrainSurfaceInput surface)
{
    TerrainFarSample result;
    result.Weights = vec3(1.0, 0.0, 0.0);
    result.Normal = vec3(0.0, 1.0, 0.0);
    result.Amount = 0.0;
    const vec2 invSize = params.farMap.zw;
    const float texelMeters = params.farMix.x;
    if (invSize.x <= 0.0 || invSize.y <= 0.0 || texelMeters <= 0.0)
    {
        return result;
    }
    const float texelPixels = texelMeters / max(TerrainPixelMeters(surface), 1.0e-4);
    float amount = 1.0 - smoothstep(TERRAIN_FAR_MAP_PIXELS_FULL, TERRAIN_FAR_MAP_PIXELS_ZERO, texelPixels);
    const vec2 uv = (surface.WorldPosition.xz - params.farMap.xy) * invSize;
    const vec2 edgeMeters = min(uv, vec2(1.0) - uv) / invSize;
    amount *= clamp(min(edgeMeters.x, edgeMeters.y) / (TERRAIN_FAR_MAP_EDGE_TEXELS * texelMeters), 0.0, 1.0);
    if (amount <= 0.0)
    {
        return result;
    }
    const vec4 texel = textureGrad(farMap, uv, surface.WorldDx.xz * invSize, surface.WorldDy.xz * invSize);
    const vec2 horizontal = texel.rg * 2.0 - 1.0;
    result.Normal = normalize(vec3(horizontal.x, sqrt(max(1.0 - dot(horizontal, horizontal), 0.0)), horizontal.y));
    const float rock = clamp(texel.b, 0.0, 1.0);
    const float dirt = clamp(texel.a, 0.0, 1.0 - rock);
    result.Weights = vec3(max(1.0 - rock - dirt, 0.0), dirt, rock);
    result.Amount = amount;
    return result;
}

// 岩肌の層に掛ける地層と砂漠ワニス（上のノイズと接平面の軸を使う）
#include "Common/TerrainStrata.glsl"

// 地形の 3 層を混ぜた材質値を返す（TangentNormal は使わない。法線は worldNormal にワールドの向きで返す）。
// 1 層目（砂）は材質のアルベド・法線・ORM の枠のテクスチャ（ORM の 1 枚を前提にする）。surfaceMask は地表の分類の地図
// （params.mask.z が 0 なら読まないので、地図の無い材質は何を張ってもよい）。farMap は遠景の地図（params.farMap.z が 0 なら読まない）
PbrMaterialTextureSamples ShadeTerrainLayers(TerrainBlendParams params,
                                             sampler2D sandAlbedo, sampler2D sandNormal, sampler2D sandOrm,
                                             bool bSandNormalTwoChannel,
                                             sampler2D dirtAlbedo, sampler2D dirtNormal, sampler2D dirtOrm,
                                             sampler2D rockAlbedo, sampler2D rockNormal, sampler2D rockOrm,
                                             sampler2D surfaceMask, sampler2D farMap,
                                             TerrainSurfaceInput surface, bool bVirtualTexture, out vec3 worldNormal)
{
    const float pixelMeters = TerrainPixelMeters(surface);
    // 遠くでは、幾何の法線を遠景の地図の画素の平均の法線へ移す（画素ごとに飛ぶ DEM の三角形の向きで光らない）
    const TerrainFarSample far = SampleTerrainFarMap(params, farMap, surface);
    vec3 n = normalize(surface.GeometricNormal);
    if (far.Amount > 0.0)
    {
        n = normalize(mix(n, far.Normal, far.Amount));
    }
    // 地図の中は地図の重み、外は傾斜の式。外縁の帯で混ぜる（地図だけの所では傾斜の式を求めない）
    const vec4 maskWeights = ComputeTerrainSurfaceMaskWeights(params, surfaceMask, surface);
    vec3 layerWeights = maskWeights.xyz;
    if (maskWeights.w < 1.0)
    {
        layerWeights =
            mix(ComputeTerrainSlopeWeights(params, surface.WorldPosition, n, pixelMeters), maskWeights.xyz, maskWeights.w);
    }
    // 遠くでは、分類も遠景の地図の画素の中の割合へ移す（画素ごとの傾斜で岩肌と砂が飛び飛びに入れ替わらない）
    if (far.Amount > 0.0)
    {
        layerWeights = mix(layerWeights, far.Weights, far.Amount);
    }
    const uint normalBits = uint(params.layers.w + 0.5);
    const bool bSandDecode = (normalBits & TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE) != 0u;
    const TerrainDetailLevels levels = ComputeTerrainDetailLevels(params, surface.CameraDistance);
    const TerrainProjectionFrame frame = MakeTerrainProjectionFrame(n);
    const float tileMeters = max(params.detail.w, 1.0e-3);
    const vec3 projection = ComputeTerrainTriplanarWeights(n);

    const TerrainLayerSample sand = SampleTerrainLayer(sandAlbedo, sandNormal, sandOrm, bSandNormalTwoChannel,
                                                       bSandDecode, bVirtualTexture, surface, frame, projection, levels, 1.0,
                                                       tileMeters);
    TerrainLayerSample dirt = sand;
    TerrainLayerSample rock = sand;
    if (layerWeights.y > 0.0)
    {
        dirt = SampleTerrainLayer(dirtAlbedo, dirtNormal, dirtOrm,
                                  (normalBits & TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL) != 0u,
                                  (normalBits & TERRAIN_BLEND_DIRT_ALBEDO_SRGB_DECODE) != 0u, bVirtualTexture,
                                  surface, frame, projection, levels, params.layers.y, tileMeters);
    }
    if (layerWeights.z > 0.0)
    {
        rock = SampleTerrainLayer(rockAlbedo, rockNormal, rockOrm,
                                  (normalBits & TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL) != 0u,
                                  (normalBits & TERRAIN_BLEND_ROCK_ALBEDO_SRGB_DECODE) != 0u, bVirtualTexture,
                                  surface, frame, projection, levels, params.layers.z, tileMeters);
        // 砂岩の地層（基準面からの高さで決まる層の色・張り出し・交差層理）と、崖の上端から垂れる砂漠ワニス
        const TerrainStrataSample strata =
            ComputeTerrainStrata(params, surfaceMask, surface.WorldPosition, surface.WorldDx, surface.WorldDy, n);
        rock.Albedo.rgb = mix(rock.Albedo.rgb * strata.Tint, VARNISH_ALBEDO, strata.Varnish);
        rock.Material.y *= mix(1.0, 0.85, strata.Varnish);
        rock.Material.z *= strata.Occlusion;
        rock.Perturbation += strata.Perturbation;
    }

    // 凹凸の高さ: 砂は一定の面、土・岩肌は AO（Material.z）。くぼみほど低い
    const vec3 layerHeights = vec3(params.noise.w, dirt.Material.z, rock.Material.z);
    const vec3 w = ApplyTerrainHeightBlend(layerWeights, layerHeights, params.height.w);

    PbrMaterialTextureSamples result;
    const vec2 xz = surface.WorldPosition.xz;
    result.Albedo = sand.Albedo * w.x + dirt.Albedo * w.y + rock.Albedo * w.z;
    result.Albedo.rgb =
        clamp(sand.Albedo.rgb * TerrainMacroVariation(params, xz, vec2(0.0), pixelMeters) * w.x +
                  dirt.Albedo.rgb * TerrainMacroVariation(params, xz, vec2(113.7, -71.3), pixelMeters) * w.y +
                  rock.Albedo.rgb * TerrainMacroVariation(params, xz, vec2(-257.1, 191.9), pixelMeters) * w.z,
              vec3(0.0), vec3(1.0));
    if (params.maskMix.w > 0.5)
    {
        result.Albedo.rgb = vec3(w.z, w.y, w.x) * 0.6;
    }
    result.TangentNormal = vec3(0.0, 0.0, 1.0);
    result.Material = sand.Material * w.x + dirt.Material * w.y + rock.Material * w.z;
    // 落とした細かい法線の傾きの分散を粗さの α²（= 粗さ^4）に足す（Toksvig と同じ考え方で、鏡面の山を細かい凹凸の分だけ広げる）
    const float lostVariance = dot(vec3(sand.LostDetail, dirt.LostDetail, rock.LostDetail) * params.farMix.yzw, w);
    if (lostVariance > 0.0)
    {
        const float roughness = clamp(result.Material.y, 0.0, 1.0);
        result.Material.y = sqrt(sqrt(min(roughness * roughness * roughness * roughness + lostVariance, 1.0)));
    }
    worldNormal = normalize(n + sand.Perturbation * w.x + dirt.Perturbation * w.y + rock.Perturbation * w.z);
    return result;
}

#endif // NORVES_TERRAIN_LAYER_BLEND_GLSL
