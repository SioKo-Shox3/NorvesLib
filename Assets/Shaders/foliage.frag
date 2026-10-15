#version 450

// 草・低木の株を GBuffer へ書く（FoliagePass）。帯は両面を描くので、裏を見ている画素は法線を帯の面で鏡に映す

#include "Common/FoliageData.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragAlbedo;
layout(location = 3) in float fragOcclusion;
layout(location = 4) in vec4 fragCurrentClip;
layout(location = 5) in vec4 fragPreviousClip;

// GBuffer（gbuffer.frag と同じ並び）
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;

void main()
{
    vec3 normal = normalize(fragNormal);
    // 三角形の面の向き（画面の微分から）。カメラの側へ向け、法線がその裏を向いていれば面で鏡に映す
    // （法線の面に沿った成分＝上へ寄せた分は保つ）
    vec3 face = cross(dFdx(fragWorldPos), dFdy(fragWorldPos));
    float faceLength = length(face);
    if (faceLength > 1.0e-12)
    {
        face /= faceLength;
        vec3 toCamera = foliage.cameraPosition.xyz - fragWorldPos;
        if (dot(face, toCamera) < 0.0)
        {
            face = -face;
        }
        float facing = dot(normal, face);
        if (facing < 0.0)
        {
            normal = normalize(normal - 2.0 * facing * face);
        }
    }

    outAlbedo = vec4(fragAlbedo, 1.0);
    // a は地形の印（草・低木は 0）
    outNormal = vec4(normal, 0.0);
    // 金属度 0・粗さ・株の中の遮蔽
    outMaterial = vec4(0.0, foliage.stemColor.a, fragOcclusion, 0.0);
    outEmissive = vec4(0.0, 0.0, 0.0, 1.0);

    outVelocity = vec2(0.0);
    if (foliage.frameParams.x > 0.5 && abs(fragCurrentClip.w) > 1e-6 && abs(fragPreviousClip.w) > 1e-6)
    {
        vec2 currentNdc = fragCurrentClip.xy / fragCurrentClip.w;
        vec2 previousNdc = fragPreviousClip.xy / fragPreviousClip.w;
        vec2 velocity = (currentNdc - previousNdc) * 0.5;
        if (all(equal(velocity, velocity)) && dot(velocity, velocity) < 1.0e6)
        {
            outVelocity = velocity;
        }
    }
}
