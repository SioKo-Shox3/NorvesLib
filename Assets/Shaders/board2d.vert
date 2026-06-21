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
layout(location = 1) out vec2 fragUV;

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
    vec2 axisX = instanceData.world[0].xy;
    vec2 axisY = instanceData.world[1].xy;
    vec2 origin = instanceData.world[3].xy;
    vec2 sizePx = instanceData.normalRows[0].xy;
    vec2 pivot = instanceData.normalRows[0].zw;
    vec2 flipFlags = instanceData.normalRows[1].xy;
    vec4 uvRect = vec4(instanceData.normalRows[1].zw, instanceData.normalRows[2].xy);
    vec2 geometryUV = quad[gl_VertexIndex];
    vec2 sampleUV = quad[gl_VertexIndex];

    if (flipFlags.x > 0.5)
    {
        geometryUV.x = 1.0 - geometryUV.x;
    }

    if (flipFlags.y > 0.5)
    {
        geometryUV.y = 1.0 - geometryUV.y;
    }

    vec2 local = geometryUV - pivot;

    if (sizePx.x > 0.0 && sizePx.y > 0.0)
    {
        float axisXLength = length(axisX);
        float axisYLength = length(axisY);
        axisX = axisXLength > 0.0001 ? axisX / axisXLength * sizePx.x : vec2(0.0, 0.0);
        axisY = axisYLength > 0.0001 ? axisY / axisYLength * sizePx.y : vec2(0.0, 0.0);
    }

    vec2 pixel = origin + axisX * local.x + axisY * local.y;
    vec2 viewportSize = max(instanceData.customData.xy, vec2(1.0, 1.0));
    vec2 ndc = vec2((pixel.x / viewportSize.x) * 2.0 - 1.0,
                    1.0 - (pixel.y / viewportSize.y) * 2.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor = instanceData.objectColor;
    fragUV = uvRect.xy + sampleUV * uvRect.zw;
}
