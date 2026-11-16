#version 450

layout(std140, set = 0, binding = 0) uniform SilhouetteData
{
    mat4 view;
    mat4 projection;
    vec4 vertexAnimInstanceParams;
    vec4 vertexAnimTime;
    vec4 color;
} silhouette;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = silhouette.color;
}
