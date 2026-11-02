#version 450

// 集めた色を補間せずに写し、アルファを含む画素値を保つ。

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
}
