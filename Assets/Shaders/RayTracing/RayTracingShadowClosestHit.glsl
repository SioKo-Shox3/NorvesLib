// 不透明ジオメトリへの最初の交差を遮蔽として記録する。
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT uint visibility;

void main()
{
    visibility = 0u;
}
