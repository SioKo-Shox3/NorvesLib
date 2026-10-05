#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// 材質の区間ごとの定数（変換はインスタンスの表から引く。インスタンスごとに変わる値はここに置かない）
layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
    vec4 emissiveColor;  // rgb=エミッシブカラー, a=エミッシブ強度
    vec4 pomParams;      // x=heightScale, y=hasHeightMap, z=debugMode, w=debugPayloadSupported
    mat4 previousView;
    mat4 previousProjection;
    vec4 frameParams;    // x=前のカメラがあるか（1/0）, y=発光に掛けるプリエクスポージャ, z=変位の頂点の間隔（UV）, w=描画の番号がLODの段か（1/0）
} mvp;

// インスタンスの表（cluster_cull.comp の MegaInstance と一致）。描画が引くのは変換だけ。
struct MegaInstance
{
    mat4 world;
    mat4 previousWorld;   // 直前のフレームの変換（velocity 用）
    vec4 lodSphere;
    uvec4 clusterInfo;
    uvec4 drawInfo;
    uvec4 bvhInfo;        // グループの BVH の節の配列のアドレス・節の数（描画では使わない。配列の刻みを合わせるために持つ）
};

layout(std430, set = 0, binding = 8) readonly buffer InstanceBuffer
{
    MegaInstance instances[];
};

// コマンドごとの描画情報（x = インスタンスの番号, y = デバッグ表示・LODの段の payload）。
// コマンドの firstInstance が添え字（カリングが書いたコマンドの通しの位置）なので gl_InstanceIndex で引く。
layout(std430, set = 0, binding = 9) readonly buffer DrawInfoBuffer
{
    uvec2 drawInfos[];
};

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragObjectColor;
layout(location = 3) out vec4 fragEmissiveColor;
layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out vec3 fragViewDir;  // ワールド空間でのカメラ方向
layout(location = 6) flat out uint fragDebugPayload;
layout(location = 7) out vec4 fragCurrentClip;
layout(location = 8) out vec4 fragPreviousClip;

void main()
{
    uvec2 drawInfo = drawInfos[gl_InstanceIndex];
    mat4 world = instances[drawInfo.x].world;
    mat4 previousWorld = instances[drawInfo.x].previousWorld;

    vec4 worldPos = world * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(world);
    fragNormal = normalize(normalMatrix * inNormal);

    fragObjectColor = mvp.objectColor.rgb;
    fragEmissiveColor = mvp.emissiveColor;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);
    fragDebugPayload = drawInfo.y;

    gl_Position = mvp.projection * mvp.view * worldPos;
    fragCurrentClip = gl_Position;
    fragPreviousClip = mvp.previousProjection * mvp.previousView * previousWorld * vec4(inPosition, 1.0);
}
