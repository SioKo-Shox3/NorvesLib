// 板の中の UV で内接円の外を透明にし、小地図などのテクスチャを丸く切り抜く。
// 板ごとの追加の値は使わず、テクスチャの参照には指定された範囲の UV を使う。
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
    vec2 p = fragLocalUV * 2.0 - 1.0;
    float r = length(p);
    float aa = max(fwidth(r), 1e-5);
    float mask = clamp((1.0 - r) / aa + 0.5, 0.0, 1.0);

    color.a *= mask;
    outColor = vec4(color.rgb * color.a, color.a);
}
