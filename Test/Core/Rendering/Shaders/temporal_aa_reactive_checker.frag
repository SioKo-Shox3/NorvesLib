#version 450

// 画素単位の明暗で TAA の分散の箱を保つ。
layout(location = 0) out vec4 OutColor;
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float value = ((pixel.x + pixel.y) & 1) == 0 ? 0.1 : 0.9;
    OutColor = vec4(value, value, value, 1.0);
}
