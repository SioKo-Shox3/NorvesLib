#version 450

// 頂点バッファなしの全画面三角形。矩形の切り抜きはシザーで行う。
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
