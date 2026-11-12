#version 460

// CPU と同じ 64 バイトの遮蔽物配置を使う。
struct SkyOccluderProxy
{
    vec4 centerYaw;
    vec4 halfExtentBase;
    float opacity;
    uint flags;
    uint reserved[6];
};

layout(std140, set = 0, binding = 0) uniform OverheadHeightMapParams
{
    mat4 viewProjection;
    vec4 window;
    vec4 depth;
    uvec4 counts;
} params;

layout(std430, set = 0, binding = 1) readonly buffer OverheadHeightMapProxies
{
    SkyOccluderProxy proxies[];
} occluders;

const float OVERHEAD_PI = 3.14159265358979323846;
const vec2 OVERHEAD_BOX_CORNERS[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

// 楕円体の冠と三つの帯を、CPU と同じ三角形の順に並べる。
vec3 OverheadEllipsoidVertex(vec3 halfExtent, int vertexIndex)
{
    int ring;
    int sector;
    if (vertexIndex < 24)
    {
        int corner = vertexIndex % 3;
        if (corner == 0)
        {
            return vec3(0.0, halfExtent.y, 0.0);
        }
        ring = 1;
        sector = vertexIndex / 3 + (corner == 2 ? 1 : 0);
    }
    else
    {
        int bandVertex = vertexIndex - 24;
        int band = bandVertex / 48 + 1;
        int bandOffset = bandVertex % 48;
        int corner = bandOffset % 6;
        ring = band + ((corner == 1 || corner == 2 || corner == 4) ? 1 : 0);
        sector = bandOffset / 6 + ((corner == 2 || corner == 4 || corner == 5) ? 1 : 0);
    }

    float theta = float(ring) * (OVERHEAD_PI / 8.0);
    float phi = float(sector % 8) * (OVERHEAD_PI / 4.0);
    float horizontal = sin(theta);
    return halfExtent * vec3(horizontal * cos(phi), cos(theta), horizontal * sin(phi));
}

void main()
{
    SkyOccluderProxy proxy = occluders.proxies[gl_InstanceIndex];
    vec3 halfExtent = proxy.halfExtentBase.xyz;
    vec3 local;
    if ((proxy.flags & 1u) != 0u)
    {
        // 箱の余分な頂点は同じ点に置き、面積を持たせない。
        vec2 corner = gl_VertexIndex < 6 ? OVERHEAD_BOX_CORNERS[gl_VertexIndex] : vec2(0.0);
        local = vec3(corner.x * halfExtent.x, halfExtent.y, corner.y * halfExtent.z);
    }
    else
    {
        local = OverheadEllipsoidVertex(halfExtent, gl_VertexIndex);
    }

    float cosine = cos(proxy.centerYaw.w);
    float sine = sin(proxy.centerYaw.w);
    vec3 world = proxy.centerYaw.xyz + vec3(
        cosine * local.x + sine * local.z,
        local.y,
        -sine * local.x + cosine * local.z);
    gl_Position = params.viewProjection * vec4(world, 1.0);
}
