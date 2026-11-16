// スキンメッシュのディゾルブ材質用にバインド姿勢の位置と保護の重みを渡す。
#version 450
#extension GL_GOOGLE_include_directive : require
#define GBUFFER_DISSOLVE 1
#include "Common/SkinnedGBufferVertex.glsl"
