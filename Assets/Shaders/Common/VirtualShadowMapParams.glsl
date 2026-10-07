// 太陽の VSM（--shadow-method=vsm）を読むパラメータの構造体。Common/VirtualShadowMap.glsl の評価が使う。
// uniform block のメンバとして宣言する側が、この include を先に取り込む。

#ifndef VIRTUAL_SHADOW_MAP_PARAMS_GLSL
#define VIRTUAL_SHADOW_MAP_PARAMS_GLSL

// std140。Rendering/VirtualShadowMapSample.h の GPUVsmSampleParams と同じ並び
struct VsmSampleParams
{
    vec4 lightRight;
    vec4 lightUp;
    // 光の進む向き。ライト空間の深度は dot(world, lightDirection)
    vec4 lightDirection;
    // xyz: カメラの位置、w: 影の最大の距離（m）
    vec4 cameraPosition;
    // x: 深度の原点（ライト空間の深度）、y: 1 / (2 × 深度の範囲)、z: 2 × 深度の範囲（m）、w: 奥の薄めの幅（影の最大の距離に対する割合）
    vec4 depth;
    // x: 画面上の 1 画素の大きさ / カメラからの距離（2 tan(fovY / 2) / 画面の高さ）
    vec4 pixel;
    // x: 1 なら有効、y: 段の数、z: 物理ページの数
    uvec4 control;
    // 段を選ぶ距離のしきい値（16 個。k 番目が [k / 4][k % 4]）
    vec4 thresholds[4];
    // x: ページの一辺（m）、y: texel の一辺（m）
    vec4 levelInfo[16];
    // x, y: 範囲の最小の絶対のページの番号
    ivec4 levelOrigin[16];
};

#endif // VIRTUAL_SHADOW_MAP_PARAMS_GLSL
