#version 450

layout(set = 0, binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

vec3 SrgbToLinear(vec3 color)
{
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, step(color, vec3(0.04045)));
}

void main()
{
    vec4 textureColor = texture(tex, inUV);
    vec3 textureLinear = SrgbToLinear(textureColor.rgb);
    vec3 vertexLinear = SrgbToLinear(inColor.rgb);
    outColor.a = textureColor.a * inColor.a;
    outColor.rgb = textureLinear * vertexLinear * outColor.a;
}
