// 現在位置は t、前の位置は tPrev でそれぞれ呼ぶ。影は現在の時刻だけを使う。
// instanceParams は GR26 のインスタンス単位の shadingParams で、z が種類を表す。
// vertexAnimTime の並びは x=t、y=tPrev、z=試験の揺れの許可、w=0。
// 時刻は float の精度を保つため 4096 秒で折り返す。1 時間を超えると刻みが粗くなる。
// GR46 の風など周期の式の周波数は 1/4096 Hz の整数倍に量子化し、折り返し時の不連続を防ぐ。
// 風・シェルの処理は GR46 が持つ。
#ifndef NORVES_VERTEX_ANIMATION_GLSL
#define NORVES_VERTEX_ANIMATION_GLSL

#define VERTEX_ANIMATION_KIND_NONE 0
#define VERTEX_ANIMATION_KIND_TEST_SINE 1

// 試験の揺れは位相 0 でワールド空間の +X へ動く。振幅と周波数は Rendering/VertexAnimation.h と揃える。
const float VertexAnimationTestSineAmplitudeMeters = 0.5;
const float VertexAnimationTestSineFrequencyHz = 2.0;
const vec3 VertexAnimationTestSineDirection = vec3(1.0, 0.0, 0.0);
const float VertexAnimationTwoPi = 6.283185307179586;

int VertexAnimationKind(vec4 instanceParams)
{
    if (instanceParams.z >= 0.5)
    {
        return VERTEX_ANIMATION_KIND_TEST_SINE;
    }
    return VERTEX_ANIMATION_KIND_NONE;
}

vec4 VertexAnimationParamsForFrame(vec4 instanceParams, vec4 vertexAnimTime)
{
    if (vertexAnimTime.z < 0.5)
    {
        instanceParams.z = -1.0;
    }
    return instanceParams;
}

vec3 ApplyVertexAnimation(vec3 worldPos, vec3 localPos, vec4 instanceParams, float time)
{
    if (VertexAnimationKind(instanceParams) == VERTEX_ANIMATION_KIND_TEST_SINE)
    {
        return worldPos + VertexAnimationTestSineDirection * VertexAnimationTestSineAmplitudeMeters *
                          sin(VertexAnimationTwoPi * VertexAnimationTestSineFrequencyHz * time);
    }
    return worldPos;
}

#endif // NORVES_VERTEX_ANIMATION_GLSL
