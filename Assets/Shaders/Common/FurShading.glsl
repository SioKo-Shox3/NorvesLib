// 毛の画素の直接光（ラップ拡散・Kajiya-Kay の 2 ローブ・逆光の透過）と GBuffer の P1 に置く流れの角の符号化・復号。
// C++ の参照は Library/Core/Private/Rendering/FurGBufferEncoding.cpp（同じ式に保つ）。
#ifndef NORVES_FUR_SHADING_GLSL
#define NORVES_FUR_SHADING_GLSL

const float FurFlowTwoPi = 6.283185307179586;

// 正規化された書き込み法線から、Duff ら（2017）の式で同じ正規直交基底を作る。
// z = 0 を境に基底の符号が反転する（Duff らの式の継ぎ目）。書き込みと読み出しで同じ法線を使うので、復元した接線は一致する。
void FurFlowBasis(vec3 n, out vec3 b1, out vec3 b2)
{
    float signZ = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (signZ + n.z);
    float b = n.x * n.y * a;
    b1 = vec3(1.0 + signZ * n.x * n.x * a, signZ * b, -signZ * n.x);
    b2 = vec3(b, signZ + n.y * n.y * a, -n.y);
}

// Emissive.a は半精度で分解能 1/2048 ≒ 0.18 度。
// 流れを法線の面へ射影し、半精度への丸めでも 1.0 にならない角度を返す。
float FurEncodeFlowAngle(vec3 n, vec3 t)
{
    vec3 projected = t - n * dot(n, t);
    if (dot(projected, projected) <= 1.0e-12)
    {
        return 0.0;
    }
    vec3 b1;
    vec3 b2;
    FurFlowBasis(n, b1, b2);
    float angle = atan(dot(projected, b2), dot(projected, b1));
    return min(fract(angle / FurFlowTwoPi), 1.0 - 1.0 / 2048.0);
}

// 書き込み法線と同じ基底を使い、保存した角度から照明用の接線を戻す。
vec3 FurDecodeFlowTangent(vec3 n, float p1)
{
    vec3 b1;
    vec3 b2;
    FurFlowBasis(n, b1, b2);
    float angle = fract(p1) * FurFlowTwoPi;
    return cos(angle) * b1 + sin(angle) * b2;
}

struct FurShadingConstants
{
    float wrap;                   // c0.x: ラップの量 w（0 で Lambert）。
    float primaryShift;           // c1.x: 一つ目（色なし）のローブで接線を法線の方向へずらす量。
    float secondaryShift;         // c1.y: 二つ目（毛色）のローブのずらし。
    float primaryStrength;        // c1.z: 一つ目のローブの強さ。
    float secondaryStrength;      // c1.w: 二つ目のローブの強さ。
    float secondaryExponentScale; // c2.x: 二つ目の指数 = 一つ目の指数 × この値。
    float transmissionPower;      // c2.y: 逆光の透過の指数 p。
    float transmissionStrength;   // c2.z: 逆光の透過の強さ。
};

// 照明の shadingModelParams[0]・[1] と furShadingParams から定数を組み立てる。
FurShadingConstants MakeFurShadingConstants(vec4 c0, vec4 c1, vec4 c2)
{
    FurShadingConstants k;
    k.wrap = c0.x;
    k.primaryShift = c1.x;
    k.secondaryShift = c1.y;
    k.primaryStrength = c1.z;
    k.secondaryStrength = c1.w;
    k.secondaryExponentScale = c2.x;
    k.transmissionPower = c2.y;
    k.transmissionStrength = c2.z;
    return k;
}

// GBuffer の粗さを Blinn-Phong と GGX の対応に基づく毛の指数に変換する。
float FurStrandExponentFromRoughness(float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = max(alpha * alpha, 1.0e-6);
    return clamp(2.0 / alphaSquared - 2.0, 1.0, 2048.0);
}

// 濡れ度（GBuffer の P0）で指数 ×(1 + kGloss·w) と強さの増しを入れる口。今は恒等。
void FurApplyWetLobes(float wetness, inout float primaryExponent, inout float secondaryExponent,
                      inout float primaryStrength, inout float secondaryStrength)
{
}

// 毛の接線を法線の方向へずらす。
vec3 FurShiftTangent(vec3 T, vec3 N, float shift)
{
    return normalize(T + N * shift);
}

// Kajiya-Kay の一つのローブ。(e + 2)/(2π) は正規化の近似。
float FurKajiyaKayLobe(vec3 T, vec3 H, float exponent)
{
    float TdotH = dot(T, H);
    float sinTH = sqrt(max(1.0 - TdotH * TdotH, 0.0));
    float dirAtten = smoothstep(-1.0, 0.0, TdotH);
    return dirAtten * pow(sinTH, exponent) * (exponent + 2.0) / FurFlowTwoPi;
}

// N・T・V・L・H は正規化済み。T は FurDecodeFlowTangent で復元した毛の向き。
// strandRoughness は GBuffer に書かれた粗さ（照明が下限へ上げる前の値）。
// ao は GBuffer の材質の AO（画面空間 AO を掛ける前）、wetness は P0。
// lightRadiance は減衰込みの光の放射輝度（N·L・影を含まない）。
void EvaluateFurDirect(vec3 N, vec3 T, vec3 V, vec3 L, vec3 H,
                       vec3 albedo, vec3 diffuseBrdf, float strandRoughness, float ao, float wetness,
                       vec3 lightRadiance, float NdotLSigned, float shadow,
                       FurShadingConstants k,
                       inout vec3 diffuse, inout vec3 specular)
{
    // 毛の AO は殻の深さの自己遮蔽で、影（殻を含まない）が解けない根元の暗さなので直接光にも掛ける。
    float wrap = clamp((NdotLSigned + k.wrap) / (1.0 + k.wrap), 0.0, 1.0);
    diffuse += diffuseBrdf * lightRadiance * wrap * ao * shadow;

    float e1 = FurStrandExponentFromRoughness(strandRoughness);
    float e2 = max(e1 * k.secondaryExponentScale, 1.0);
    float s1 = k.primaryStrength;
    float s2 = k.secondaryStrength;
    FurApplyWetLobes(wetness, e1, e2, s1, s2);
    vec3 T1 = FurShiftTangent(T, N, k.primaryShift);
    vec3 T2 = FurShiftTangent(T, N, k.secondaryShift);
    vec3 lobes = vec3(s1 * FurKajiyaKayLobe(T1, H, e1)) + albedo * (s2 * FurKajiyaKayLobe(T2, H, e2));
    specular += lobes * lightRadiance * max(NdotLSigned, 0.0) * shadow;

    float back = pow(clamp(dot(V, -L), 0.0, 1.0), k.transmissionPower);
    diffuse += back * (1.0 - ao) * albedo * lightRadiance * k.transmissionStrength * shadow;
}

#endif // NORVES_FUR_SHADING_GLSL
