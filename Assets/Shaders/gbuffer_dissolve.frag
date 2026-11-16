// ディゾルブ材質の GBuffer の断片シェーダー。
#version 450
#extension GL_GOOGLE_include_directive : require
// 切り抜きの定義により NORVES_VT_FEEDBACK_LATE_DEPTH_TESTS が立ち、早期の深度テストを外す。
// 材質が Masked でなければ切り抜き方式が 0 となり、被覆率の判定では破棄しない。
#define GBUFFER_ALPHA_CLIP 1
#define GBUFFER_DISSOLVE 1
#include "Common/GBufferFragment.glsl"
