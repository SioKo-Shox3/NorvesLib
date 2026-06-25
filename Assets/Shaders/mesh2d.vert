#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(set = 0, binding = 0) uniform Mesh2DTransform
{
    vec2 scale;
    vec2 translate;
} transform;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = vec4(inPos * transform.scale + transform.translate, 0.0, 1.0);
    outUV = inUV;
    outColor = inColor;
}
