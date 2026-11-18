#version 450
// アトラスの被覆率で板の影を切り抜き、残る画素の深度を CSM へ書く。
#extension GL_ARB_sparse_texture2 : require
#extension GL_GOOGLE_include_directive : require

#include "Common/ShadowAlphaClip.glsl"

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(location = 0) in vec2 inTexCoord;

void main()
{
    if (ShadowAlphaClipShouldDiscard(albedoTexture, inTexCoord, 1.0, 0.5, false))
    {
        discard;
    }
}
