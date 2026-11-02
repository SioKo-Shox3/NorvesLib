#version 450

layout(set = 0, binding = 0) uniform RibbonTrailData
{
    mat4 view;
    mat4 projection;
    vec4 sceneColorParams;
} ribbon;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main()
{
    gl_Position = ribbon.projection * ribbon.view * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragColor = inColor;
}
