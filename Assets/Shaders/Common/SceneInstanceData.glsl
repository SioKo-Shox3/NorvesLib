// 描画のインスタンスの表の 1 件（C++ の GPUSceneInstanceData（Rendering/MeshTypes.h）と同じ並び・大きさ）。
#ifndef NORVES_SCENE_INSTANCE_DATA_GLSL
#define NORVES_SCENE_INSTANCE_DATA_GLSL

// この struct の写しをほかのシェーダーに書かない（ShadingModelContractTest が走査する）。
struct InstanceData
{
    mat4 world;
    mat4 previousWorld;
    vec4 normalRows[3];
    vec4 objectColor;
    // impostor.vert ではアトラス、board2d.vert ではビューポートの大きさなどを格納する。
    vec4 customData;
};

#endif // NORVES_SCENE_INSTANCE_DATA_GLSL
