// ファーの材質の定数（std140）の宣言。C++ の写しは Library/Core/Public/Rendering/FurTypes.h の FurMaterialGpuParams。
// 並びと意味は Docs/Architecture/GpuLayoutRegistry.md の FurMaterialGpuParams の節を参照し、変更時は C++ と同時に直す。
// uniform block のメンバとして宣言する側が、この include を先に取り込む。

#ifndef NORVES_FUR_MATERIAL_PARAMS_GLSL
#define NORVES_FUR_MATERIAL_PARAMS_GLSL

#define FUR_MAX_REGION_COUNT 8
#define FUR_MAP_MASK_LENGTH 1u
#define FUR_MAP_MASK_FLOW 2u
#define FUR_MAP_MASK_DENSITY_CLUMP 4u
#define FUR_MAP_MASK_REGION 8u

struct FurMaterialRegion
{
    vec4 shading; // x: 根元の暗さ、y: 毛先の明るさ、z: 長さの倍率、w: 太さの倍率。
    vec4 lobes; // x: 一つ目のローブのずれ、y: 二つ目のずれ、z: 一つ目の指数、w: 二つ目の指数。
    vec4 wet; // x: 濡れの応答への倍率、yzw: 空き（0）。
};

struct FurMaterialParams
{
    uvec4 counts; // x: 殻数、y: 領域数（1〜8）、z: マップの有無の印、w: 空き（0）。
    vec4 growth; // x: 毛の最大長×長さの倍率（m）、y: 密度、z: 束の幅、w: 束の強さ。
    vec4 strand; // x: 根元の半径、y: 毛先の半径、z: 細りの指数、w: 毛の傾き。
    vec4 undercoat; // rgb: 下毛の線形色、a: アルベドとの混合量。
    vec4 occlusion; // x: 根元の遮蔽の明るさ、y: 遮蔽の指数、zw: 空き（0）。
    vec4 wetGeometry; // x: 長さの縮み、y: 半径の増し分、z: 束の強さ、w: 垂れ。各値は濡れ度を乗算済み。
    vec4 wetShading; // x: 暗化、y: 彩度低下、z: 粗さの減少、w: 鏡面の強まり。各値は濡れ度を乗算済み。
    FurMaterialRegion regions[FUR_MAX_REGION_COUNT]; // 使用領域と未使用枠の既定値。
};

// 領域マップの texelFetch の値を表の範囲内の領域 ID に変換する。
uint FurRegionIndex(float regionTexel, uint regionCount)
{
    uint id = uint(clamp(round(regionTexel * 255.0), 0.0, 255.0));
    return min(id, max(regionCount, 1u) - 1u);
}

#endif // NORVES_FUR_MATERIAL_PARAMS_GLSL
