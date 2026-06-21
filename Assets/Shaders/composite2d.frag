#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTexture;
layout(set = 0, binding = 1) uniform sampler2D canvasTexture;

void main()
{
    vec4 sceneColor = texture(sceneTexture, fragUV);
    vec4 canvasColor = texture(canvasTexture, fragUV);
    outColor = vec4(canvasColor.rgb * canvasColor.a + sceneColor.rgb * (1.0 - canvasColor.a), 1.0);
}
