#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(std140, set = 0, binding = 1) uniform VignetteParams
{
    float intensity;
    float radius;
    float softness;
    uint bEnabled;
} params;

float ComputeVignette(vec2 uv, float intensity, float radius, float softness)
{
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(radius - softness, radius, dist);
    return mix(1.0, 1.0 - vignette, intensity);
}

void main()
{
    if (params.bEnabled == 0u)
    {
        outColor = texture(inputTexture, fragUV);
        return;
    }

    vec4 color = texture(inputTexture, fragUV);
    float vignette = ComputeVignette(fragUV, params.intensity, params.radius, params.softness);
    outColor = vec4(color.rgb * vignette, color.a);
}
