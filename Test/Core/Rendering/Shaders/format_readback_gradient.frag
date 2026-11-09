#version 450

// 画素位置に応じて、各成分に異なる浮動小数の参照値を書く。
layout(location = 0) flat in uint vDrawNumber;
layout(location = 0) out vec4 outColor;

const float VALUES[12] = float[12](
    0.0, 1.0, 0.5, 65000.0,
    0.000003814697265625, 0.00006103515625, 0.1, 0.333251953125,
    1.2345, 1000.0, 31.75, 0.003);

void main()
{
    uint index = (uint(gl_FragCoord.x) + 3u * uint(gl_FragCoord.y) + vDrawNumber) % 12u;
    outColor = vec4(VALUES[index], VALUES[(index + 4u) % 12u], VALUES[(index + 8u) % 12u], 1.0);
}
