#version 450

struct InstanceData
{
    mat4 world;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 quad[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0));

    InstanceData instanceData = instances[gl_InstanceIndex];
    vec4 pixel = instanceData.world * vec4(quad[gl_VertexIndex], 0.0, 1.0);
    vec2 viewportSize = max(instanceData.customData.xy, vec2(1.0, 1.0));
    vec2 ndc = vec2((pixel.x / viewportSize.x) * 2.0 - 1.0,
                    1.0 - (pixel.y / viewportSize.y) * 2.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor = instanceData.objectColor;
}
