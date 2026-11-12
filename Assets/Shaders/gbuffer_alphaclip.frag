#version 450
// 切り抜き材質の GBuffer の断片シェーダー。
#extension GL_GOOGLE_include_directive : require
#define GBUFFER_ALPHA_CLIP 1
#include "Common/GBufferFragment.glsl"
