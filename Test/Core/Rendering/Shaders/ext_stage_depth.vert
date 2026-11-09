#version 450

// 床より奥、クリア深度の空より手前の全画面三角形。矩形はシザーで限定する。
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.9995, 1.0);
}
