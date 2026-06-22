#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D layerTexture;

layout(set = 0, binding = 1) uniform CanvasLayerOpacityParams
{
    vec4 Params;
} opacityParams;

void main()
{
    vec4 layerColor = texture(layerTexture, fragUV);
    float opacity = clamp(opacityParams.Params.x, 0.0, 1.0);
    outColor = vec4(layerColor.rgb * opacity, layerColor.a * opacity);
}
