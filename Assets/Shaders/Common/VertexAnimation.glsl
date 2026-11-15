// 現在位置は t、前の位置は tPrev でそれぞれ呼ぶ。影は現在の時刻だけを使う。
// instanceParams は GR26 のインスタンス単位の shadingParams で、z が種類を表す。
// vertexAnimTime の並びは x=t、y=tPrev、z=試験の揺れの許可、w=0。
// 時刻は float の精度を保つため 4096 秒で折り返す。1 時間を超えると刻みが粗くなる。
// 周期の式の周波数は 1/4096 Hz の整数倍に量子化し、折り返し時の不連続を防ぐ。
// 草・樹木の風の変位は材質の値・風の値・個体のワールド原点を受け、このファイルで計算する。シェルは GR34 が持つ。
// 前の位置の評価にも現在の個体の原点を使う。樹木は頂点色が未対応のため、高さと幹の軸からの距離でマスクを近似する。
#ifndef NORVES_VERTEX_ANIMATION_GLSL
#define NORVES_VERTEX_ANIMATION_GLSL

#include "Common/Wind.glsl"

#define VERTEX_ANIMATION_KIND_NONE 0
#define VERTEX_ANIMATION_KIND_TEST_SINE 1

#define VERTEX_ANIMATION_MODE_NONE 0
#define VERTEX_ANIMATION_MODE_GRASS 1
#define VERTEX_ANIMATION_MODE_TREE 2

const float VertexAnimationWindReferenceSpeed = 5.0; // この風速[m/s]で風の強さ 1
const float VertexAnimationMaxWindStrength = 3.0; // 風の強さの上限。C++ の VertexAnimationMaxWindStrength と一致
const float VertexAnimationGrassFrequencyHz = 0.75;
const float VertexAnimationTrunkFrequencyHz = 0.25;
const float VertexAnimationBranchFrequencyHz = 1.5;

struct VertexAnimationMaterial
{
    vec4 params0;
    vec4 params1;
};

struct VertexAnimationWind
{
    vec4 windA;
    vec4 windB;
};

// 試験の揺れは位相 0 でワールド空間の +X へ動く。振幅と周波数は Rendering/VertexAnimation.h と揃える。
const float VertexAnimationTestSineAmplitudeMeters = 0.5;
const float VertexAnimationTestSineFrequencyHz = 2.0;
const vec3 VertexAnimationTestSineDirection = vec3(1.0, 0.0, 0.0);
const float VertexAnimationTwoPi = 6.283185307179586;

// インスタンスの値から頂点アニメーションの種類を返す。
int VertexAnimationKind(vec4 instanceParams)
{
    if (instanceParams.z >= 0.5)
    {
        return VERTEX_ANIMATION_KIND_TEST_SINE;
    }
    return VERTEX_ANIMATION_KIND_NONE;
}

// フレームの許可に応じて試験の揺れを無効にしたインスタンスの値を返す。
vec4 VertexAnimationParamsForFrame(vec4 instanceParams, vec4 vertexAnimTime)
{
    if (vertexAnimTime.z < 0.5)
    {
        instanceParams.z = -1.0;
    }
    return instanceParams;
}

// 時刻 time[秒] の試験の揺れを加えたワールド位置[m]を返す。
vec3 ApplyVertexAnimation(vec3 worldPos, vec3 localPos, vec4 instanceParams, float time)
{
    if (VertexAnimationKind(instanceParams) == VERTEX_ANIMATION_KIND_TEST_SINE)
    {
        return worldPos + VertexAnimationTestSineDirection * VertexAnimationTestSineAmplitudeMeters *
                          sin(VertexAnimationTwoPi * VertexAnimationTestSineFrequencyHz * time);
    }
    return worldPos;
}

// GPU に詰めた材質の 2 つの値から風アニメーションの材質を返す。
VertexAnimationMaterial MakeVertexAnimationMaterial(vec4 params0, vec4 params1)
{
    return VertexAnimationMaterial(params0, params1);
}

// GPU に詰めた風の 2 つの値から風アニメーションの風を返す。
VertexAnimationWind MakeVertexAnimationWind(vec4 windA, vec4 windB)
{
    return VertexAnimationWind(windA, windB);
}

// 材質のモードを丸め、草・樹木以外は無効として返す。
int VertexAnimationMode(VertexAnimationMaterial m)
{
    const int mode = int(m.params0.x + 0.5);
    return mode == VERTEX_ANIMATION_MODE_GRASS || mode == VERTEX_ANIMATION_MODE_TREE ? mode : VERTEX_ANIMATION_MODE_NONE;
}

// 材質の振幅[m]と風の有効フラグから、風の変位を計算するかを返す。
bool VertexAnimationHasWind(VertexAnimationMaterial m, VertexAnimationWind w)
{
    return VertexAnimationMode(m) != VERTEX_ANIMATION_MODE_NONE && w.windB.w > 0.5 && m.params0.y > 0.0;
}

// 周波数[Hz]を 1/4096 Hz の整数倍に量子化して返す。最小値は 1/4096 Hz。
float VertexAnimationQuantizeFrequency(float hz)
{
    return max(round(hz * 4096.0), 1.0) / 4096.0;
}

// 個体のワールド原点[m]を 0.25 m のセルに分け、位相用の値[0, 1)を返す。
float VertexAnimationPhaseHash(vec3 origin)
{
    ivec3 cell = ivec3(floor(origin * 4.0));
    uint h = WindHash(uint(cell.x) ^ WindHash(uint(cell.y) ^ WindHash(uint(cell.z))));
    return float(h >> 8) * (1.0 / 16777216.0);
}

// ローカル位置[m]と個体のワールド原点[m]から、時刻 time[秒]の風によるワールド変位[m]を返す。
// 樹木の枝先は高さと幹の軸（オブジェクトの Y 軸）からの距離で近似する。
vec3 VertexAnimationWindOffset(vec3 localPos, VertexAnimationMaterial m, VertexAnimationWind w, vec3 instanceOrigin, float time)
{
    const int mode = VertexAnimationMode(m);
    const vec3 windVelocity = WindVelocity(instanceOrigin, time, w.windA.xy, w.windA.z, w.windA.w, w.windB.x, w.windB.y, w.windB.z);
    const float speed = length(windVelocity.xz);
    const vec2 dir = speed > 1.0e-4 ? windVelocity.xz / speed : w.windA.xy;
    const float strength = min(speed / VertexAnimationWindReferenceSpeed, VertexAnimationMaxWindStrength);
    const float span = max(m.params0.w - m.params0.z, 0.001);
    const float h = clamp((localPos.y - m.params0.z) / span, 0.0, 1.0);
    const float heightMask = pow(h, mix(1.0, 3.0, clamp(m.params1.w, 0.0, 1.0))); // 硬さで上の方だけ曲がる
    const float phase = VertexAnimationTwoPi * VertexAnimationPhaseHash(instanceOrigin) * clamp(m.params1.z, 0.0, 1.0);
    const float amplitude = m.params0.y * strength;
    const vec3 windDir3 = vec3(dir.x, 0.0, dir.y);
    const float swayHz = VertexAnimationQuantizeFrequency(
        (mode == VERTEX_ANIMATION_MODE_TREE ? VertexAnimationTrunkFrequencyHz : VertexAnimationGrassFrequencyHz) * m.params1.x);
    const float sway = 0.7 + 0.3 * sin(VertexAnimationTwoPi * swayHz * time + phase);
    vec3 offset = windDir3 * (amplitude * heightMask * sway); // 幹（草は茎）の低周波の曲げ
    if (mode == VERTEX_ANIMATION_MODE_TREE)
    {
        // 枝先の高周波。マスクは幹の軸（オブジェクトの Y 軸）からの距離 × 高さ
        const float branchMask = clamp(length(localPos.xz) / (0.5 * span), 0.0, 1.0) * h;
        const float branchHz = VertexAnimationQuantizeFrequency(VertexAnimationBranchFrequencyHz * m.params1.x);
        const float vertexPhase = dot(localPos, vec3(2.3, 1.7, 3.1));
        const float flutter = sin(VertexAnimationTwoPi * branchHz * time + phase + vertexPhase);
        offset += (windDir3 * 0.5 + vec3(0.0, 0.5, 0.0)) * (amplitude * m.params1.y * branchMask * flutter);
    }
    return offset;
}

// 試験の揺れを先に適用し、風が有効ならその変位を加えた時刻 time[秒]のワールド位置[m]を返す。
vec3 ApplyVertexAnimation(vec3 worldPos, vec3 localPos, vec4 instanceParams,
                          VertexAnimationMaterial material, VertexAnimationWind wind, vec3 instanceOrigin, float time)
{
    vec3 animatedPos = ApplyVertexAnimation(worldPos, localPos, instanceParams, time);
    if (VertexAnimationHasWind(material, wind))
    {
        animatedPos += VertexAnimationWindOffset(localPos, material, wind, instanceOrigin, time);
    }
    return animatedPos;
}

// 試験の揺れと風の変位がともに無効で、元の位置を保つかを返す。
bool VertexAnimationIsIdentity(vec4 instanceParams, VertexAnimationMaterial material, VertexAnimationWind wind)
{
    return VertexAnimationKind(instanceParams) == VERTEX_ANIMATION_KIND_NONE && !VertexAnimationHasWind(material, wind);
}

#endif // NORVES_VERTEX_ANIMATION_GLSL
