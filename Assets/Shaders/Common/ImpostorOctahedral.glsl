// Y が上のオブジェクト空間の向きを、+Y を極とする八面体で符号化・復号する。
#ifndef IMPOSTOR_OCTAHEDRAL_GLSL
#define IMPOSTOR_OCTAHEDRAL_GLSL

#define IMPOSTOR_LAYOUT_HEMI_OCTAHEDRAL 0
#define IMPOSTOR_LAYOUT_FULL_OCTAHEDRAL 1

float ImpostorSignNotZero(float value)
{
    return value < 0.0 ? -1.0 : 1.0;
}

vec3 ImpostorNormalizeOrUp(vec3 direction)
{
    if (any(isnan(direction)) || any(isinf(direction)))
    {
        return vec3(0.0, 1.0, 0.0);
    }

    // 長さの計算で桁あふれや桁落ちが起きないよう、最大成分で縮尺をそろえる。
    float scale = max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
    if (scale <= 0.0)
    {
        return vec3(0.0, 1.0, 0.0);
    }
    return normalize(direction / scale);
}

vec2 EncodeImpostorOctahedral(vec3 direction, int layoutMode)
{
    vec3 n = ImpostorNormalizeOrUp(direction);
    if (layoutMode == IMPOSTOR_LAYOUT_HEMI_OCTAHEDRAL)
    {
        // 下半球は水平へ詰め、真下だけは +Y に置き換える。
        n.y = max(n.y, 0.0);
        n = ImpostorNormalizeOrUp(n);
    }

    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 p = n.xz;
    if (layoutMode == IMPOSTOR_LAYOUT_HEMI_OCTAHEDRAL)
    {
        p = vec2(n.x + n.z, n.x - n.z);
    }
    else if (n.y < 0.0)
    {
        p = vec2((1.0 - abs(p.y)) * ImpostorSignNotZero(p.x),
                 (1.0 - abs(p.x)) * ImpostorSignNotZero(p.y));
    }
    return clamp(p * 0.5 + 0.5, 0.0, 1.0);
}

vec3 DecodeImpostorOctahedral(vec2 uv, int layoutMode)
{
    vec2 p = uv * 2.0 - 1.0;
    if (layoutMode == IMPOSTOR_LAYOUT_HEMI_OCTAHEDRAL)
    {
        float x = (p.x + p.y) * 0.5;
        float z = (p.x - p.y) * 0.5;
        float y = max(1.0 - abs(x) - abs(z), 0.0);
        return ImpostorNormalizeOrUp(vec3(x, y, z));
    }

    vec3 n = vec3(p.x, 1.0 - abs(p.x) - abs(p.y), p.y);
    if (n.y < 0.0)
    {
        n.x = (1.0 - abs(p.y)) * ImpostorSignNotZero(p.x);
        n.z = (1.0 - abs(p.x)) * ImpostorSignNotZero(p.y);
    }
    return ImpostorNormalizeOrUp(n);
}

#endif
