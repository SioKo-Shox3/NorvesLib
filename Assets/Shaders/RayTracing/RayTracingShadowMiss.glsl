// レイがシーンの不透明ジオメトリに当たらなかったことを記録する。
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT uint visibility;

void main()
{
    visibility = 1u;
}
