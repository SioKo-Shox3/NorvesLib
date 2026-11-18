#version 450
// 段の遷移をディザで描く GBuffer の断片シェーダー。
#extension GL_GOOGLE_include_directive : require
#define GBUFFER_LOD_CROSSFADE 1
#include "Common/GBufferFragment.glsl"
