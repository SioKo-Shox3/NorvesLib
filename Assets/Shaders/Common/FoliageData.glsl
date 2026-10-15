// 草・低木の株の描画（FoliagePass）の定数。foliage.vert と foliage.frag が同じ定義を読む（FoliagePass.cpp の FoliageUniform と同じ並び）
#ifndef NORVES_FOLIAGE_DATA_GLSL
#define NORVES_FOLIAGE_DATA_GLSL

layout(set = 0, binding = 0) uniform FoliageData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    // xyz = カメラの位置, w = 距離 1 m あたりの 1 画素の大きさ[m]（2 / (投影の [1][1] × 描画の高さ)）
    vec4 cameraPosition;
    // xy = 風の吹いていく向き（XZ。長さ 1）, z = 今の風の時刻[s], w = 前のフレームの風の時刻[s]
    vec4 wind;
    // x = 突風の塊の流れる速さ[m/s], y = 塊の大きさ[m], z = 強さ, w = 株の揺れの周波数[Hz]
    vec4 windParams;
    // rgb = 茎・根元の色（線形のアルベド）, a = 粗さ
    vec4 stemColor;
    // rgb = 先の色 A, a = 帯を広げて保つ画面上の最小の幅[画素]（0 で広げない）
    vec4 tipColorA;
    // rgb = 先の色 B, a = 元の幅に対する広げる上限の倍率
    vec4 tipColorB;
    // x = 前のフレームのカメラが有効か（1/0）, y = 株の高さ[m]（インスタンスの高さの倍率が 1 のとき）
    vec4 frameParams;
} foliage;

#endif
