#version 450

layout(set = 0, binding = 1) uniform sampler2D impostorAtlas;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragQuadUV;
layout(location = 2) flat in vec3 fragViewDirection;
layout(location = 3) flat in vec4 fragAtlasInfo0;
layout(location = 4) flat in vec4 fragAtlasInfo1;
layout(location = 0) out vec4 outColor;

float SignNotZero(float value)
{
    return value < 0.0 ? -1.0 : 1.0;
}

vec2 EncodeOctahedral(vec3 direction)
{
    vec3 n = length(direction) > 0.0 ? normalize(direction) : vec3(0.0, 0.0, 1.0);
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 0.00001);

    vec2 encoded = n.xy;
    if (n.z < 0.0)
    {
        encoded = vec2((1.0 - abs(encoded.y)) * SignNotZero(encoded.x),
                       (1.0 - abs(encoded.x)) * SignNotZero(encoded.y));
    }

    return clamp(encoded * 0.5 + 0.5, vec2(0.0), vec2(1.0));
}

vec4 SampleAtlasCell(vec2 cell, vec2 grid, vec2 atlasSize, vec2 quadUV)
{
    vec2 cellSize = 1.0 / grid;
    vec2 cellMin = cell * cellSize;
    vec2 uv = cellMin + clamp(quadUV, vec2(0.0), vec2(1.0)) * cellSize;
    vec2 texelInset = 0.5 / atlasSize;
    return texture(impostorAtlas, clamp(uv, cellMin + texelInset, cellMin + cellSize - texelInset));
}

void main()
{
    vec2 grid = max(fragAtlasInfo0.xy, vec2(1.0));
    float cellResolution = max(fragAtlasInfo1.x, 1.0);
    vec2 atlasSize = max(fragAtlasInfo0.zw, grid * cellResolution);
    vec2 encoded = EncodeOctahedral(fragViewDirection);
    vec2 cellSpace = encoded * max(grid - vec2(1.0), vec2(0.0));
    vec2 cell0 = floor(cellSpace);
    vec2 cell1 = min(cell0 + vec2(1.0), grid - vec2(1.0));
    vec2 blendWeight = clamp(cellSpace - cell0, vec2(0.0), vec2(1.0));

    vec4 c00 = SampleAtlasCell(cell0, grid, atlasSize, fragQuadUV);
    vec4 c10 = SampleAtlasCell(vec2(cell1.x, cell0.y), grid, atlasSize, fragQuadUV);
    vec4 c01 = SampleAtlasCell(vec2(cell0.x, cell1.y), grid, atlasSize, fragQuadUV);
    vec4 c11 = SampleAtlasCell(cell1, grid, atlasSize, fragQuadUV);
    vec4 atlasColor = mix(mix(c00, c10, blendWeight.x),
                          mix(c01, c11, blendWeight.x),
                          blendWeight.y);
    vec4 color = atlasColor * fragColor;
    outColor = vec4(color.rgb * color.a, color.a);
}
