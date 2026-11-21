// 板にテクスチャ付きの円弧・リングを描く。
// x は塗りの割合、y は真上を 0 として時計回りに測る開始角（回転数）。
// z は外径に対する内径の比で、0 は扇形。w は 0 以上で時計回り、負で反時計回り。
#version 450

layout(set = 0, binding = 0) uniform sampler2D boardTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in vec4 fragParams;
layout(location = 3) in vec2 fragLocalUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(boardTexture, fragUV) * fragColor;
    // board2d.vert は画素の Y が増えるほど画面の上に写すため、反転のない板では UV の y も上向きに増える。
    // 真上から時計回りに角度を測るため、p の y は画面の下向きに取る。
    vec2 p = vec2(fragLocalUV.x * 2.0 - 1.0, 1.0 - fragLocalUV.y * 2.0);
    float r = length(p);
    float rAA = max(fwidth(r), 1e-5);
    const float twoPi = 6.28318530718;
    float turnAA = max(length(fwidth(p)) / max(twoPi * r, 1e-4), 1e-5);

    float innerRatio = fragParams.z;
    float outerCoverage = clamp((1.0 - r) / rAA + 0.5, 0.0, 1.0);
    float innerCoverage = innerRatio > 0.0 ? clamp((r - innerRatio) / rAA + 0.5, 0.0, 1.0) : 1.0;
    float radialCoverage = outerCoverage * innerCoverage;

    float fill = clamp(fragParams.x, 0.0, 1.0);
    float angularCoverage = fill;
    if (fill > 0.0 && fill < 1.0)
    {
        // 中心では角度が定まらないため、真上と同じ角度を使う。
        float turns = r > 0.0 ? atan(p.x, -p.y) / twoPi : 0.0;
        float start = fragParams.y;
        float t = fragParams.w >= 0.0 ? fract(turns - start) : fract(start - turns);
        // 開始角をまたぐ側も含めて、近い方の縁までの符号付き距離を取る。
        float d = t <= fill ? min(t, fill - t) : -min(t - fill, 1.0 - t);
        angularCoverage = clamp(d / turnAA + 0.5, 0.0, 1.0);
    }

    color.a *= radialCoverage * angularCoverage;
    outColor = vec4(color.rgb * color.a, color.a);
}
