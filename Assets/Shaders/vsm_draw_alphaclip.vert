#version 450
// VSM の切り抜き材質の頂点シェーダー。
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_GOOGLE_include_directive : require
#define VSM_ALPHA_CLIP 1
#include "Common/VsmDrawVertex.glsl"
