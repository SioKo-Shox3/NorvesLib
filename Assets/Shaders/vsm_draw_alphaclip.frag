#version 450
// VSM の切り抜き材質の断片シェーダー。
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_sparse_texture2 : require
#define VSM_ALPHA_CLIP 1
#include "Common/VsmDrawFragment.glsl"
