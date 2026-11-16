// 草と株の切り抜き、遮蔽物のディザに共通する配置と計算。
#ifndef NORVES_VIEW_CLEARANCE_GLSL
#define NORVES_VIEW_CLEARANCE_GLSL

struct ViewClearanceData
{
    vec4 CameraRadius;
    vec4 TargetRadius;
    vec4 Shape;
    vec4 Control;
};

bool ViewClearanceIsActive(ViewClearanceData c)
{
    return c.Control.y != 0.0 && c.Control.x > 0.0;
}

float ViewClearanceSmoothStep(float start, float end, float value)
{
    if (end <= start)
    {
        return value < start ? 0.0 : 1.0;
    }
    precise float x = clamp((value - start) / (end - start), 0.0, 1.0);
    precise float result = (x * x) * (3.0 - 2.0 * x);
    return result;
}

float ViewClearanceVisibility(ViewClearanceData c, vec3 p)
{
    if (!ViewClearanceIsActive(c))
    {
        return 1.0;
    }

    precise float dx = p.x - c.CameraRadius.x;
    precise float dy = p.y - c.CameraRadius.y;
    precise float dz = p.z - c.CameraRadius.z;
    precise float ax = c.TargetRadius.x - c.CameraRadius.x;
    precise float ay = c.TargetRadius.y - c.CameraRadius.y;
    precise float az = c.TargetRadius.z - c.CameraRadius.z;
    precise float lengthSquared = (ax * ax + ay * ay) + az * az;
    precise float clearance = 1.0;
    if (lengthSquared > 1.0e-12)
    {
        precise float t = ((dx * ax + dy * ay) + dz * az) / lengthSquared;
        if (t >= 0.0 && t <= 1.0)
        {
            precise float radius = c.CameraRadius.w + (c.TargetRadius.w - c.CameraRadius.w) * t;
            if (radius > 0.0)
            {
                precise float offsetX = dx - t * ax;
                precise float offsetY = dy - t * ay;
                precise float offsetZ = dz - t * az;
                precise float distance = sqrt((offsetX * offsetX + offsetY * offsetY) + offsetZ * offsetZ);
                precise float featherEnd = radius * (1.0 + c.Shape.x);
                clearance = ViewClearanceSmoothStep(radius, featherEnd, distance);
            }
        }
    }
    precise float cameraDistance = sqrt((dx * dx + dy * dy) + dz * dz);
    precise float nearFade = ViewClearanceSmoothStep(c.Shape.y, c.Shape.z, cameraDistance);
    precise float visibility = min(clearance, nearFade);
    precise float result = 1.0 - c.Control.x * (1.0 - visibility);
    return result;
}

// smoothstep は 3 つの一様乱数の中央値（Beta(2,2)）の累積分布。
// 同じ中央値で近接と切り抜きのしきい値を決め、g < Strength のときだけ間引く。
// 中央値と g が独立なら、消える確率は Strength * (1 - 可視度)。
bool ViewClearanceKeep(ViewClearanceData c, vec3 p, float u, float g)
{
    if (!ViewClearanceIsActive(c))
    {
        return true;
    }
    precise float dx = p.x - c.CameraRadius.x;
    precise float dy = p.y - c.CameraRadius.y;
    precise float dz = p.z - c.CameraRadius.z;
    precise float dist2 = (dx * dx + dy * dy) + dz * dz;
    precise float nearT = c.Shape.y + (c.Shape.z - c.Shape.y) * u;
    precise float nearT2 = nearT * nearT;
    bool nearPass = dist2 >= nearT2;
    precise float ax = c.TargetRadius.x - c.CameraRadius.x;
    precise float ay = c.TargetRadius.y - c.CameraRadius.y;
    precise float az = c.TargetRadius.z - c.CameraRadius.z;
    precise float L2 = (ax * ax + ay * ay) + az * az;
    precise float D = (dx * ax + dy * ay) + dz * az;
    bool clearPass = true;
    if (L2 > 1.0e-12 && D >= 0.0 && D <= L2)
    {
        precise float rL = c.CameraRadius.w * L2 + (c.TargetRadius.w - c.CameraRadius.w) * D;
        if (rL > 0.0)
        {
            precise float perp = dist2 * L2 - D * D;
            precise float lhs = perp * L2;
            precise float k = rL * (1.0 + c.Shape.x * u);
            precise float k2 = k * k;
            clearPass = lhs >= k2;
        }
    }
    if (nearPass && clearPass)
    {
        return true;
    }
    return !(g < c.Control.x);
}

// 遮蔽物を残す割合。無効なら 1 を返す。
float ViewClearanceFade(ViewClearanceData c, vec3 p)
{
    return ViewClearanceVisibility(c, p);
}

float ViewClearanceInterleavedGradientNoise(vec2 pixel, uint phase)
{
    // アルファの Hash と相関しないよう、画素の軸を入れ替え、ずらしと位相の掛け方を変える。
    vec2 p = pixel.yx + vec2(17.0, 43.0) + vec2(3.175, 7.813) * float(phase & 63u);
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// pixel には floor(gl_FragCoord.xy) を渡す。
// Control.z には描く側がフレーム番号の下位 6 ビットを入れる（草・株は 0）。
bool ViewClearanceShouldDiscard(ViewClearanceData c, vec3 worldPos, vec2 pixel)
{
    if (!ViewClearanceIsActive(c))
    {
        return false;
    }
    float fade = ViewClearanceFade(c, worldPos);
    uint phase = uint(c.Control.z + 0.5) & 63u;
    float threshold = ViewClearanceInterleavedGradientNoise(pixel, phase);
    // しきい値は 0 以上 1 未満。等号を含め、fade が 0 なら必ず抜き、1 なら必ず残す。
    return fade <= threshold;
}

#endif
