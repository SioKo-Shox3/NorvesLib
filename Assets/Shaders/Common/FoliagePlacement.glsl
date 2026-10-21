// 草・低木の株の頂点を置いて風で揺らす式。GBuffer の描画（foliage.vert）と太陽の影の描画（vsm_draw.vert の株の塊）が
// 同じ式で置くよう、ここにだけ書く。定数（UBO）に依らず、値は引数で受ける
#ifndef NORVES_FOLIAGE_PLACEMENT_GLSL
#define NORVES_FOLIAGE_PLACEMENT_GLSL

const float kFoliageTwoPi = 6.2831853;

float FoliageHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 滑らかな値ノイズ（0〜1）
float FoliageValueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = FoliageHash(cell);
    float b = FoliageHash(cell + vec2(1.0, 0.0));
    float c = FoliageHash(cell + vec2(0.0, 1.0));
    float d = FoliageHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// 時刻 t の風のずれ（ワールドの XZ）。root は株の根元、flex はしなりの重み、plantHeight は株の高さ[m]、
// plantPhase は株ごとの位相、partPhase は葉・枝ごとの位相。突風の塊（低周波の値ノイズ）が風下へ流れ、
// 株は風下へ傾きながら固有の周波数で揺れ、葉・枝は横へ細かく震える。
// direction は風の吹いていく向き（XZ。長さ 1）、gust = (突風の塊の流れる速さ[m/s], 塊の大きさ[m])、
// strength は風の強さ、frequency は株の揺れの周波数[Hz]
vec2 FoliageWindOffsetFor(vec2 root, float flex, float plantHeight, float plantPhase, float partPhase, float t,
                          vec2 direction, vec2 gust, float strength, float frequency)
{
    vec2 across = vec2(-direction.y, direction.x);
    float speed = max(gust.x, 0.1);
    float scale = max(gust.y, 0.1);

    vec2 flow = direction * speed * t;
    float gustValue = 0.65 * FoliageValueNoise((root - flow) / scale) +
                      0.35 * FoliageValueNoise((root - flow * 1.3) / (scale * 0.37) + vec2(17.31, 5.73));
    // 揺れは風下へ進む波にする（隣の株と少しずつずれてそろう）
    float sway = sin(kFoliageTwoPi * (frequency * t + plantPhase) - dot(root, direction) * (kFoliageTwoPi * frequency / speed));
    float flutter = sin(kFoliageTwoPi * (frequency * 2.7 * t + partPhase + plantPhase * 0.37));

    float lean = strength * (0.2 + 0.8 * gustValue);
    float swing = strength * (0.15 + 0.35 * gustValue) * sway;
    float shiver = strength * 0.25 * (0.3 + 0.7 * gustValue) * flutter;
    return (direction * (lean + swing) + across * shiver) * (plantHeight * flex * flex);
}

// ずれた分だけ下げて、葉・枝の長さがおおよそ変わらないようにする
vec3 ApplyFoliageWind(vec3 position, vec2 offset, float heightAboveRoot)
{
    float drop = 0.5 * dot(offset, offset) / max(heightAboveRoot, 0.02);
    return position + vec3(offset.x, -min(drop, heightAboveRoot * 0.5), offset.y);
}

// 株のローカルの値を Y 軸まわりに yaw の向きへ回す（c = cos(yaw)、s = sin(yaw)）
vec3 FoliageRotateYaw(vec3 local, float c, float s)
{
    return vec3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);
}

#endif
