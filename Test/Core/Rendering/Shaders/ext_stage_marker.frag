#version 450

// パケットから取得してフレームの持ち主に借りた定数だけを出力する。
layout(set = 0, binding = 0, std140) uniform MarkerParams
{
    vec4 Color;
};
layout(location = 0) out vec4 OutColor;
void main()
{
    OutColor = Color;
}
