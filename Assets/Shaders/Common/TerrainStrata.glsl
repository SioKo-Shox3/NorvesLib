// 崖の砂岩の地層と砂漠ワニス。Common/TerrainLayerBlend.glsl が岩肌の層の標本に掛ける（TerrainLatticeHash・TerrainValueNoise・
// TerrainTangentAxis・TerrainBlendParams を使うので、それらの後に取り込む）。
//
// 地層: 層の境はワールドの y（共通の基準面。荒野ではエンジンの y = NAVD88 の標高 - 1452.05 m。
// Assets/Terrain/ValleyOfTheGods/height.json）からの高さで決める。離れたビュートや崖でも同じ高さに同じ層が出る。
//   - 境の高さ STRATA_CONTACT_HEIGHT より下は薄い層の重なり（谷の斜面の段。赤褐色の泥岩・シルト岩に硬い砂岩の段が挟まる）、
//     上は厚い交差層理の砂岩（垂直の崖）。境の高さは DEM の傾斜の分布（y 105 m より上で 55° を超える斜面の割合が 0.1 → 0.4 へ跳ぶ）から決めた。
//   - 層の厚さは等間隔にしない: 境の位置を層ごとの擬似乱数でずらし（厚さは平均の 0.4〜1.6 倍）、さらに横方向の低周波ノイズで
//     動かす（同じ層が横へ厚くなったり薄くなったりする）。地層全体も数百 m の波長で ±数 m うねる。
//   - 硬い層は張り出す: 層の中の高さで外への張り出し d(y) を作り、その傾き d'(y) で法線を上（段の上面）・下（張り出しの下の陰）へ倒す。
//     柔らかい層は奥へ引っ込み、AO を下げて暗くする。
//   - 途切れる帯: 下の単位の硬い段と、上の単位の砂丘の間の赤い薄層は、横方向のノイズで現れたり消えたりする（レンズ状に尖滅する）。
//   - 交差層理: 上の単位の各層（セット）の中に、層ごとに向き（方位）と傾き（18〜30°）の違う斜めの葉理の束を入れる。
//     葉理はセットの上下の面で切れる。明暗とわずかな凹凸（風化で硬い葉理が浮く）にする。
//   - 画面で層が数画素より薄く写る遠くでは、張り出しの法線・色の差・葉理を弱めて平均へ寄せる（縞のちらつきを出さない）。
// 砂漠ワニス: 崖の上端から流れ落ちた水の跡に沿う黒い筋。地表の分類の地図（露岩の重み）を、面の外向きと逆（崖の奥）へ
//   たどって露岩が切れる所（崖の上端）までの水平の距離を測り、面の傾きから上端までの高さの差を見積もる。筋は上端から
//   筋ごとの長さ（20〜80 m）だけ垂れ、下へ行くほど細く薄くなる。筋は水の落ち口（上端に沿う数十 m ごとの塊）に集まり、
//   崖全体へ同じ縦の模様を掛けない。地図の外（遠景）では上端が分からないのでワニスを置かない。

#ifndef NORVES_TERRAIN_STRATA_GLSL
#define NORVES_TERRAIN_STRATA_GLSL

// 下の薄層の単位と上の交差層理の単位の境の高さ（基準面からの m）と、その横方向の揺らぎ（± m）
const float STRATA_CONTACT_HEIGHT = 105.0;
const float STRATA_CONTACT_UNDULATION = 4.0;
// 地層全体のうねり（± m）と波長（m）
const float STRATA_WARP_AMPLITUDE = 2.5;
const float STRATA_WARP_WAVELENGTH = 420.0;
// 層の平均の厚さ（m）。下の単位は薄い層、上の単位は交差層理のセット
const float STRATA_LOWER_BED_METERS = 1.2;
const float STRATA_UPPER_SET_METERS = 7.0;
// 境の位置の層ごとのずれと、横方向のずれ（層の平均の厚さに対する割合。和の半分が 0.5 未満なら境の順は入れ替わらない）
const float STRATA_BOUNDARY_JITTER = 0.6;
const float STRATA_BOUNDARY_LATERAL = 0.3;
// 横方向に層の厚さが変わる波長（m）と、帯が途切れる波長（m）
const float STRATA_LATERAL_WAVELENGTH = 90.0;
const float STRATA_PINCH_WAVELENGTH = 70.0;
// 硬い層の張り出し（m）。下の単位の硬い段・上の単位のセット
const float STRATA_LOWER_LEDGE_METERS = 0.35;
const float STRATA_UPPER_LEDGE_METERS = 0.6;
// 上の単位のセットの底の赤い薄層の厚さ（m）の範囲
const vec2 STRATA_PARTING_METERS = vec2(0.4, 1.5);
// 交差層理の葉理の束の間隔（m）の範囲と、傾き（度）の範囲、明暗の振幅（割合）、凹凸の高さ（m）
const vec2 STRATA_LAMINA_SPACING_METERS = vec2(0.45, 1.1);
const vec2 STRATA_LAMINA_DIP_DEGREES = vec2(18.0, 30.0);
const float STRATA_LAMINA_CONTRAST = 0.09;
const float STRATA_LAMINA_RELIEF_METERS = 0.05;
// 張り出しの法線の傾きの上限（法線に足す接平面の長さ）
const float STRATA_MAX_TILT = 1.1;

// 砂漠ワニス: 地図をたどる歩幅（m）と回数、露岩が切れたとみなす重みの割合、面の傾きの正接の範囲、筋の長さ（m）の範囲、色（線形）
const float VARNISH_MARCH_STEP_METERS = 2.0;
const int VARNISH_MARCH_STEPS = 14;
const float VARNISH_RIM_ROCK_FRACTION = 0.3;
const vec2 VARNISH_SLOPE_TANGENT = vec2(0.5, 4.0);
const vec2 VARNISH_LENGTH_METERS = vec2(20.0, 80.0);
const vec3 VARNISH_ALBEDO = vec3(0.045, 0.032, 0.026);
const float VARNISH_MAX_COVER = 0.85;

// 岩肌の層に掛ける地層とワニスの結果
struct TerrainStrataSample
{
    vec3 Tint;          // アルベドに掛ける倍率
    float Occlusion;    // AO に掛ける倍率
    vec3 Perturbation;  // 法線の傾き（ワールドの向き。幾何の法線に足す）
    float Varnish;      // ワニスの覆う割合（0〜1）
};

// 1 つの層の範囲（基準面からの m）と、層の番号
struct TerrainStrataBed
{
    float Bottom;
    float Top;
    int Index;
};

// 単位 seed の index 番目の層の境の位置（層の平均の厚さを 1 とした単位）。index ± 0.45 の中で動く
float TerrainStrataBoundary(int index, vec2 xz, int seed)
{
    const float jitter = (TerrainLatticeHash(ivec2(index, seed)) - 0.5) * STRATA_BOUNDARY_JITTER;
    const vec2 p = xz * (1.0 / STRATA_LATERAL_WAVELENGTH) + vec2(float(index) * 7.131, float(seed) * 3.173);
    const float lateral = (TerrainValueNoise(p) - 0.5) * STRATA_BOUNDARY_LATERAL;
    return float(index) + jitter + lateral;
}

// 高さ height を含む層を探す（origin から厚さ thickness ごとに並ぶ単位 seed の層）。境は index ± 0.45 にあるので、
// 最も近い境とその上下の 3 つで決まる
TerrainStrataBed FindTerrainStrataBed(float height, vec2 xz, float origin, float thickness, int seed)
{
    const float u = (height - origin) / thickness;
    const int i = int(floor(u + 0.5));
    const float b = TerrainStrataBoundary(i, xz, seed);
    TerrainStrataBed bed;
    if (u >= b)
    {
        bed.Index = i;
        bed.Bottom = b;
        bed.Top = TerrainStrataBoundary(i + 1, xz, seed);
    }
    else
    {
        bed.Index = i - 1;
        bed.Bottom = TerrainStrataBoundary(i - 1, xz, seed);
        bed.Top = b;
    }
    bed.Bottom = origin + bed.Bottom * thickness;
    bed.Top = origin + bed.Top * thickness;
    return bed;
}

// smoothstep の傾き（edge0〜edge1 の間で 6t(1-t)/(edge1-edge0)、外は 0）
float TerrainSmoothstepSlope(float edge0, float edge1, float x)
{
    const float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return 6.0 * t * (1.0 - t) / (edge1 - edge0);
}

// 層の中の高さ y での外への張り出しの傾き d'(y)。張り出しは層の底で edge の幅で立ち上がり（下の柔らかい層の上がえぐれる）、
// 層の上面で丸く edge の 2 倍の幅で落ちる（段の上面が日を受ける）
float TerrainStrataLedgeSlope(float y, float bottom, float top, float protrusion, float edge)
{
    const float rise = TerrainSmoothstepSlope(bottom, min(bottom + edge, top), y);
    const float fall = TerrainSmoothstepSlope(max(top - edge * 2.0, bottom), top, y);
    return protrusion * (rise - fall);
}

// 単位 seed の層ごとの横方向の有無（0〜1）。帯がレンズ状に尖滅する
float TerrainStrataPresence(int index, vec2 xz, int seed)
{
    const vec2 p = xz * (1.0 / STRATA_PINCH_WAVELENGTH) + vec2(float(index) * 3.917, float(seed) * 11.37);
    return smoothstep(0.32, 0.55, TerrainValueNoise(p));
}

// 地表の分類の地図を、面の外向きと逆へたどって露岩が切れる所（崖の上端）を探し、上端からの高さの差（m）を見積もる。
// 地図が無い・外・上端が見つからないときは負を返す
float EstimateTerrainRimDrop(TerrainBlendParams params, sampler2D surfaceMask, vec3 worldPosition, vec3 n)
{
    const vec2 invSize = params.mask.zw;
    const float horizontal = length(n.xz);
    if (invSize.x <= 0.0 || invSize.y <= 0.0 || horizontal < 1.0e-3)
    {
        return -1.0;
    }
    const vec2 inward = -n.xz / horizontal;
    float previousDistance = 0.0;
    float previousRock = 1.0;
    for (int index = 1; index <= VARNISH_MARCH_STEPS; ++index)
    {
        const float marched = float(index) * VARNISH_MARCH_STEP_METERS;
        const vec2 uv = (worldPosition.xz + inward * marched - params.mask.xy) * invSize;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        {
            return -1.0;
        }
        const vec4 classes = textureLod(surfaceMask, uv, 0.0);
        const float rock = classes.r / max(classes.r + classes.g + classes.b + classes.a, 1.0e-3);
        if (rock < VARNISH_RIM_ROCK_FRACTION)
        {
            // 前の歩と今の歩の間で、重みが閾値を切る位置を線形に求める
            const float t = clamp((previousRock - VARNISH_RIM_ROCK_FRACTION) / max(previousRock - rock, 1.0e-3), 0.0, 1.0);
            const float rimDistance = mix(previousDistance, marched, t);
            const float slopeTangent = clamp(horizontal / max(n.y, 1.0e-3), VARNISH_SLOPE_TANGENT.x, VARNISH_SLOPE_TANGENT.y);
            return rimDistance * slopeTangent;
        }
        previousDistance = marched;
        previousRock = rock;
    }
    return -1.0;
}

// 砂漠ワニスの覆う割合。drop は崖の上端からの高さの差（m）、steepness は面の水平の向きの長さ（0 = 平ら、1 = 垂直）
float ComputeTerrainVarnish(vec2 xz, float drop, float steepness)
{
    if (drop < 0.0)
    {
        return 0.0;
    }
    // 筋は xz だけの関数なので、垂直の面では縦の筋になる。細い筋（2.5 m）と太い筋（9 m）を重ねる
    const float fine = TerrainValueNoise(xz * (1.0 / 2.5) + vec2(41.3, -17.9));
    const float broad = TerrainValueNoise(xz * (1.0 / 9.0) + vec2(-5.7, 63.1));
    const float streak = fine * 0.55 + broad * 0.45;
    // 水の落ち口: 上端に沿う数十 m ごとの塊にだけ筋を置く
    const float outlet = smoothstep(0.42, 0.68, TerrainValueNoise(xz * (1.0 / 45.0) + vec2(13.7, 29.3)));
    // 筋ごとの長さ。下へ行くほど閾値を上げて細く、終わりへ向けて薄くする
    const float lengthMeters =
        mix(VARNISH_LENGTH_METERS.x, VARNISH_LENGTH_METERS.y, TerrainValueNoise(xz * (1.0 / 18.0) + vec2(-33.1, 7.9)));
    const float down = drop / lengthMeters;
    const float threshold = 0.46 + 0.18 * down;
    const float line = smoothstep(threshold, threshold + 0.12, streak);
    const float fade = 1.0 - smoothstep(0.55, 1.0, down);
    // 垂直に近い面（傾斜およそ 60° より急）だけ。崖錐の中の小さな露岩には置かない
    return line * outlet * fade * smoothstep(0.85, 0.95, steepness) * VARNISH_MAX_COVER;
}

// 岩肌の層に掛ける地層とワニスを求める。n は正規化した幾何の法線
TerrainStrataSample ComputeTerrainStrata(TerrainBlendParams params, sampler2D surfaceMask, vec3 worldPosition,
                                         vec3 worldDx, vec3 worldDy, vec3 n)
{
    TerrainStrataSample result;
    result.Tint = vec3(1.0);
    result.Occlusion = 1.0;
    result.Perturbation = vec3(0.0);
    result.Varnish = 0.0;

    const vec2 xz = worldPosition.xz;
    // 地層全体のうねりを足した、層を決める高さ
    const float warp = (TerrainValueNoise(xz * (1.0 / STRATA_WARP_WAVELENGTH) + vec2(3.71, -8.13)) * 2.0 - 1.0) *
                       STRATA_WARP_AMPLITUDE;
    const float y = worldPosition.y + warp;
    const float contact =
        STRATA_CONTACT_HEIGHT +
        (TerrainValueNoise(xz * (1.0 / 160.0) + vec2(-19.3, 27.1)) * 2.0 - 1.0) * STRATA_CONTACT_UNDULATION;
    // 1 画素あたりの高さの変化（層の薄さと比べて、遠くで弱める）
    const float pixelHeight = max(max(abs(worldDx.y), abs(worldDy.y)), 1.0e-4);
    // 面に沿って上へ向かう軸。平らな所では upAxis.y が 0 へ近づき、張り出しの効きも 0 へ近づく
    const vec3 upAxis = TerrainTangentAxis(vec3(0.0, 1.0, 0.0), n);
    const float steepness = length(n.xz);

    vec3 tint = vec3(1.0);
    float occlusion = 1.0;
    float ledgeSlope = 0.0;
    float bedVisibility = 0.0;
    if (y < contact)
    {
        // 下の単位: 薄い層。硬い砂岩の段（橙で明るく張り出す）と柔らかい泥岩（暗い赤褐色で引っ込む）が入れ替わる
        const TerrainStrataBed bed = FindTerrainStrataBed(y, xz, contact, STRATA_LOWER_BED_METERS, 1);
        const float thickness = bed.Top - bed.Bottom;
        bedVisibility = smoothstep(2.0, 6.0, thickness / pixelHeight);
        const float hardHash = TerrainLatticeHash(ivec2(bed.Index, 101));
        const float hardness = smoothstep(0.45, 0.75, hardHash) * TerrainStrataPresence(bed.Index, xz, 1);
        const float toneHash = TerrainLatticeHash(ivec2(bed.Index, 211));
        const vec3 soft = vec3(1.04, 0.9, 0.86) * mix(0.72, 0.86, toneHash);
        const vec3 hard = vec3(1.0, 1.04, 1.04) * mix(1.02, 1.16, toneHash);
        tint = mix(soft, hard, hardness);
        occlusion = mix(0.78, 1.0, hardness);
        const float edge = max(min(thickness * 0.3, 0.3), pixelHeight * 1.5);
        ledgeSlope = TerrainStrataLedgeSlope(y, bed.Bottom, bed.Top, STRATA_LOWER_LEDGE_METERS * hardness, edge);
    }
    else
    {
        // 上の単位: 交差層理のセット。セットの底に赤い薄層（途切れる）があり、その上の砂岩が張り出す
        const TerrainStrataBed sandSet = FindTerrainStrataBed(y, xz, contact, STRATA_UPPER_SET_METERS, 2);
        const float partingHash = TerrainLatticeHash(ivec2(sandSet.Index, 307));
        const float partingPresence = TerrainStrataPresence(sandSet.Index, xz, 2) * step(0.5, partingHash);
        const float parting =
            mix(STRATA_PARTING_METERS.x, STRATA_PARTING_METERS.y, TerrainLatticeHash(ivec2(sandSet.Index, 311))) *
            partingPresence;
        const float sandBottom = sandSet.Bottom + parting;
        const float toneHash = TerrainLatticeHash(ivec2(sandSet.Index, 401));
        if (y < sandBottom)
        {
            // 赤い薄層: 暗い赤褐色で、上の砂岩の張り出しの下に引っ込む
            bedVisibility = smoothstep(2.0, 6.0, parting / pixelHeight);
            tint = vec3(1.06, 0.9, 0.85) * mix(0.74, 0.86, toneHash);
            occlusion = 0.8;
        }
        else
        {
            const float thickness = sandSet.Top - sandBottom;
            bedVisibility = smoothstep(2.0, 6.0, thickness / pixelHeight);
            // セットごとに明るさと色味（淡い黄白〜赤み）を変える
            const float paleness = TerrainLatticeHash(ivec2(sandSet.Index, 409));
            tint = mix(vec3(1.03, 0.98, 0.95), vec3(0.99, 1.02, 1.05), paleness) * mix(0.92, 1.1, toneHash);
            // 張り出すのは一部のセットだけで、張り出しも横方向に強まったり消えたりする
            const float hardness = smoothstep(0.35, 0.9, TerrainLatticeHash(ivec2(sandSet.Index, 503))) *
                                   mix(0.2, 1.0, TerrainStrataPresence(sandSet.Index, xz, 3));
            const float edge = max(min(thickness * 0.2, 1.0), pixelHeight * 1.5);
            ledgeSlope = TerrainStrataLedgeSlope(y, sandBottom, sandSet.Top, STRATA_UPPER_LEDGE_METERS * hardness, edge);

            // 交差層理: セットごとの向きと傾きの斜めの葉理（セットの上下の面で切れる）
            const float azimuth = TerrainLatticeHash(ivec2(sandSet.Index, 601)) * 6.2831853;
            const vec2 dipDirection = vec2(cos(azimuth), sin(azimuth));
            const float dipTangent =
                tan(radians(mix(STRATA_LAMINA_DIP_DEGREES.x, STRATA_LAMINA_DIP_DEGREES.y,
                                TerrainLatticeHash(ivec2(sandSet.Index, 607)))));
            const float spacing = mix(STRATA_LAMINA_SPACING_METERS.x, STRATA_LAMINA_SPACING_METERS.y,
                                      TerrainLatticeHash(ivec2(sandSet.Index, 613)));
            // 葉理の座標（等しい値の面が葉理）。ワールドの位置に対する勾配は (−tan·dir.x, 1, −tan·dir.y)
            const float lamina = (y - sandBottom) - dipTangent * dot(xz, dipDirection);
            const vec3 laminaGradient = vec3(-dipTangent * dipDirection.x, 1.0, -dipTangent * dipDirection.y);
            const float pixelLamina =
                max(max(abs(dot(laminaGradient, worldDx)), abs(dot(laminaGradient, worldDy))), 1.0e-4);
            const float laminaVisibility = smoothstep(1.5, 4.0, spacing / pixelLamina);
            if (laminaVisibility > 0.0)
            {
                const float row = float(sandSet.Index) * 5.31;
                const float coordinate = lamina / spacing;
                const float value = smoothstep(0.3, 0.7, TerrainValueNoise(vec2(coordinate, row)));
                tint *= 1.0 + STRATA_LAMINA_CONTRAST * (value * 2.0 - 1.0) * laminaVisibility;
                // 硬い葉理が浮く凹凸: 値の傾き（葉理の座標あたり）を、葉理の勾配の接平面の向きへ載せる
                const float slope = (smoothstep(0.3, 0.7, TerrainValueNoise(vec2(coordinate + 0.25, row))) -
                                     smoothstep(0.3, 0.7, TerrainValueNoise(vec2(coordinate - 0.25, row)))) /
                                    (0.5 * spacing);
                const vec3 tangentGradient = laminaGradient - n * dot(n, laminaGradient);
                result.Perturbation -= tangentGradient * (slope * STRATA_LAMINA_RELIEF_METERS * laminaVisibility);
            }
        }
    }
    // 薄く写る層は色の差と張り出しを平均へ寄せる
    result.Tint = mix(vec3(1.0), tint, bedVisibility);
    result.Occlusion = mix(1.0, occlusion, bedVisibility);
    const float tilt = clamp(ledgeSlope * upAxis.y, -STRATA_MAX_TILT, STRATA_MAX_TILT) * bedVisibility;
    result.Perturbation -= upAxis * tilt;

    // ワニスは上の単位の砂岩の崖（境より上）にだけ置く。下の単位の斜面の段では上端をたどらない
    const float upperUnit = smoothstep(contact - 8.0, contact, y);
    if (upperUnit > 0.0)
    {
        result.Varnish =
            ComputeTerrainVarnish(xz, EstimateTerrainRimDrop(params, surfaceMask, worldPosition, n), steepness) * upperUnit;
    }
    return result;
}

#endif // NORVES_TERRAIN_STRATA_GLSL
