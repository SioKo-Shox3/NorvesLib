// 焼きのセルと同じ向き・UV を持つ、太陽の影用インポスターの板を作る。
#ifndef IMPOSTOR_SHADOW_CARD_GLSL
#define IMPOSTOR_SHADOW_CARD_GLSL

#include "Common/ImpostorOctahedral.glsl"

// 光に最も近いセルの板の角と UV を返す。入力が不正なら false を返す。
bool ImpostorShadowCardVertex(uint corner, vec3 center, float halfSize, mat3 objectToWorld, vec3 towardLight,
                              vec2 grid, vec2 atlasSize, int layoutMode, out vec3 worldPosition, out vec2 atlasUV)
{
    if (corner >= 6u || isnan(halfSize) || isinf(halfSize) || halfSize <= 0.0 ||
        any(isnan(center)) || any(isinf(center)) ||
        any(isnan(towardLight)) || any(isinf(towardLight)) ||
        any(isnan(objectToWorld[0])) || any(isinf(objectToWorld[0])) ||
        any(isnan(objectToWorld[1])) || any(isinf(objectToWorld[1])) ||
        any(isnan(objectToWorld[2])) || any(isinf(objectToWorld[2])) ||
        any(isnan(grid)) || any(isinf(grid)) || any(lessThan(grid, vec2(1.0))) ||
        any(isnan(atlasSize)) || any(isinf(atlasSize)) || any(lessThan(atlasSize, grid)))
    {
        return false;
    }

    vec2 encoded = EncodeImpostorOctahedral(transpose(objectToWorld) * towardLight, layoutMode);
    vec2 cell = clamp(floor(encoded * (grid - vec2(1.0)) + vec2(0.5)), vec2(0.0), grid - vec2(1.0));
    vec2 cellDirectionUV = vec2(grid.x > 1.0 ? cell.x / (grid.x - 1.0) : 0.5,
                                grid.y > 1.0 ? cell.y / (grid.y - 1.0) : 0.5);
    vec3 direction = ImpostorNormalizeOrUp(DecodeImpostorOctahedral(cellDirectionUV, layoutMode));
    vec3 upHint = abs(direction.y) > 0.999 ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(upHint, direction));
    vec3 up = cross(direction, right);

    vec2 quad[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 quadUV = quad[corner];
    vec2 local = quadUV * 2.0 - vec2(1.0);
    worldPosition = center + objectToWorld * right * (local.x * halfSize) +
                    objectToWorld * up * (local.y * halfSize);

    // 焼きは右方向を x、上方向を -y で書く。右はそのまま、上は UV の y を反転して一致させる。
    // GBuffer の AtlasCellUV と同じく、セルの端を半テクセルだけ内側に寄せる。
    vec2 cellSize = 1.0 / grid;
    vec2 cellMin = cell * cellSize;
    vec2 uv = cellMin + clamp(vec2(quadUV.x, 1.0 - quadUV.y), 0.0, 1.0) * cellSize;
    vec2 texelInset = 0.5 / atlasSize;
    atlasUV = clamp(uv, cellMin + texelInset, cellMin + cellSize - texelInset);
    return !any(isnan(worldPosition)) && !any(isinf(worldPosition)) &&
           !any(isnan(atlasUV)) && !any(isinf(atlasUV));
}

#endif
