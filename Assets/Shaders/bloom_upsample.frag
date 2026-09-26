#version 450

// ========================================
// ブルームの拡大パス（3×3テント）
// ========================================
// 1つ下（低解像度）の段を3×3のテントフィルタで拡大し、この段の縮小結果へ加える。
// 下の段から順に積み上げることで、各段のぼかしを足し合わせた広いにじみを作る。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: 1つ下の段（拡大の元）
layout(set = 0, binding = 0) uniform sampler2D lowerTexture;

// binding 1: この段の縮小結果
layout(set = 0, binding = 1) uniform sampler2D currentTexture;

// binding 2: 拡大パラメータ UBO (std140)
layout(set = 0, binding = 2) uniform BloomUpsampleParams
{
    // x = テントの半径（下の段のテクセル単位）
    // yzw = 未使用
    vec4 params;
};

vec3 SampleTent(sampler2D source, vec2 uv, float radius)
{
    vec2 offset = radius / vec2(textureSize(source, 0));

    vec3 sum = texture(source, uv).rgb * 4.0;
    sum += texture(source, uv + vec2(-offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2( offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0, -offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0,  offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(-offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2(-offset.x,  offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x,  offset.y)).rgb;
    return sum * (1.0 / 16.0);
}

void main()
{
    vec3 lower = SampleTent(lowerTexture, fragUV, params.x);
    vec3 current = texture(currentTexture, fragUV).rgb;
    outColor = vec4(current + lower, 1.0);
}
