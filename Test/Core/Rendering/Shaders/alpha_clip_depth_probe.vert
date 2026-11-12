#version 450

// 板の 0.25 m 奥へ射影した深度で、頂点バッファを使わず全画面三角形を描く。
layout(set = 0, binding = 0, std140) uniform DepthProbeParams
{
    vec4 Emissive;
    vec4 Depth;
};

void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(position * 2.0 - 1.0, Depth.x, 1.0);
}
