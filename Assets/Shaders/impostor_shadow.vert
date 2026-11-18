#version 450
// 太陽へ向いたアトラスのセルから、CSM に落とすインポスターの板を作る。
#extension GL_GOOGLE_include_directive : require

#include "Common/ImpostorShadowCard.glsl"

struct ImpostorShadowCardData
{
    vec4 centerAndHalfSize;
    vec4 axisX;
    vec4 axisY;
    vec4 axisZ;
};

layout(std140, set = 0, binding = 0) uniform ImpostorShadowMVP
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 towardLight;
    vec4 gridAndAtlasSize; // xy = セル数、zw = アトラスの寸法。
    ivec4 layoutMode;
    ImpostorShadowCardData cards[64];
} shadowMVP;

layout(location = 0) out vec2 outTexCoord;

void main()
{
    outTexCoord = vec2(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    if (uint(gl_InstanceIndex) >= 64u)
    {
        return;
    }
    ImpostorShadowCardData card = shadowMVP.cards[gl_InstanceIndex];
    vec3 worldPosition;
    vec2 atlasUV;
    if (ImpostorShadowCardVertex(uint(gl_VertexIndex), card.centerAndHalfSize.xyz, card.centerAndHalfSize.w,
                                 mat3(card.axisX.xyz, card.axisY.xyz, card.axisZ.xyz), shadowMVP.towardLight.xyz,
                                 shadowMVP.gridAndAtlasSize.xy, shadowMVP.gridAndAtlasSize.zw,
                                 shadowMVP.layoutMode.x, worldPosition, atlasUV))
    {
        outTexCoord = atlasUV;
        gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * vec4(worldPosition, 1.0);
    }
}
