#version 450
#extension GL_GOOGLE_include_directive : require
#include "Particles/ParticleDraw.glsl"

// GPU の粒に手続きの形・深度フェード・環境光・高さ霧を適用して色または被覆率を出力する。
layout(location = 0) in vec2 vQuadUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in uint vEmitterIndex;
layout(location = 4) flat in uint vOrientation;
layout(location = 5) in float vStreakAspect;
layout(location = 6) flat in float vFlipbookPhase;
layout(location = 7) flat in float vFlipbookSeed;
layout(location = 8) flat in uint vParticleSerial;
layout(location = 0) out vec4 outColor;

// 前向き描画の拡散 IBL と同じ経緯度写像を使う。
vec2 GpuParticleEquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989);
    return uv + 0.5;
}

void main()
{
    GpuEmitterParams e = drawEmitters.emitters[vEmitterIndex];

    float shape;
    if (e.plateSides >= 3u && e.plateSides <= 8u)
    {
        float distanceToEdge = GpuParticlePlateDistance(e, vParticleSerial, vQuadUV);
        // 四角形の境界からはみ出さないよう、輪郭の内側の約１画素だけを滑らかにする。
        float edgeWidth = max(fwidth(distanceToEdge), 1.0e-6);
        shape = 1.0 - smoothstep(-edgeWidth, 0.0, distanceToEdge);
        if (shape <= 0.0)
        {
            discard;
        }
    }
    else if (vFlipbookPhase >= 0.0)
    {
        float r = length(vQuadUV);
        if (r > 1.0)
        {
            discard;
        }
        float a = r > 0.0 ? atan(vQuadUV.y, vQuadUV.x) : 0.0;
        float f = vFlipbookPhase;
        // 広がる王冠の輪と初期の水の塊を重ねる。半径・幅・突起・減衰の値は仮。
        float radius = mix(0.2, 0.85, sqrt(f));
        float width = mix(0.30, 0.06, f);
        radius *= 1.0 + 0.18 * (1.0 - f) * (0.5 + 0.5 * cos(a * 8.0 + vFlipbookSeed * 6.28));
        float ring = 1.0 - smoothstep(0.0, width, abs(r - radius));
        float core = (1.0 - f) * (1.0 - f) * (1.0 - smoothstep(0.0, 0.3, r));
        shape = max(ring, core) * (1.0 - 0.7 * f);
    }
    else if (vOrientation == 1u)
    {
        // 筋は幅を半径とするカプセルにし、中心線からの距離で薄くする。
        float s = vQuadUV.y * vStreakAspect;
        float dy = max(abs(s) - (vStreakAspect - 1.0), 0.0);
        float d2 = vQuadUV.x * vQuadUV.x + dy * dy;
        if (d2 > 1.0)
        {
            discard;
        }
        shape = 1.0 - d2;
    }
    else
    {
        float r2 = dot(vQuadUV, vQuadUV);
        if (r2 > 1.0)
        {
            discard;
        }
        shape = (1.0 - r2) * (1.0 - r2);
    }

    vec2 uv = gl_FragCoord.xy * drawConstants.targetSizeInvSize.zw;
    float sceneLin = GpuParticleSceneLinearDepth(textureLod(sceneDepthTexture, uv, 0.0).r);
    float partLin = GpuParticleViewDepth(vWorldPos);
    float soft = 1.0;
    if (vOrientation == 2u)
    {
        // 面に沿う板は深度の誤差を許容し、接触によるフェードを掛けない。
        if (partLin > sceneLin + 0.05)
        {
            discard;
        }
    }
    else
    {
        if (partLin > sceneLin)
        {
            discard;
        }
        soft = e.softDistance > 0.0 ? clamp((sceneLin - partLin) / e.softDistance, 0.0, 1.0) : 1.0;
    }

    float nearFade = e.boxHalfExtentsNearFadeDistance.w;
    float nearWeight = nearFade > 0.0 ?
        clamp((partLin - drawConstants.nearFarParams.x) / nearFade, 0.0, 1.0) : 1.0;
    float edgeWeight = 1.0;
    uint flags = e.poolCapacitySeedFlags.w;
    if ((flags & kGpuEmitterFlagShape) != 0u &&
        ((flags >> kGpuEmitterShapeShift) & kGpuEmitterShapeMask) == 4u &&
        (flags & kGpuEmitterFlagWrapInCameraBox) != 0u)
    {
        // 箱の半径の 15% の帯で縁へ向けて薄める。巻き戻りで筋が縁に急に現れ・消えるのを隠す。値は仮。
        const float kCameraBoxEdgeFadeFraction = 0.15;
        vec3 halfSize = e.boxHalfExtentsNearFadeDistance.xyz;
        vec3 cameraPosition = drawConstants.cameraPositionPreExposure.xyz;
        vec3 distanceToEdge = halfSize - abs(vWorldPos - cameraPosition);
        vec3 band = max(halfSize * kCameraBoxEdgeFadeFraction, vec3(1e-4));
        vec3 w = clamp(distanceToEdge / band, 0.0, 1.0);
        edgeWeight = min(min(w.x, w.y), w.z);
    }
    float alpha = vColor.a * shape * soft * nearWeight * edgeWeight;
    if (alpha <= 1.0 / 1024.0)
    {
        discard;
    }

    // リアクティブは同じ被覆率を使い、照明と霧の評価を省く。
    if (drawConstants.flags.z == kGpuParticleDrawOutputReactive)
    {
        outColor = vec4(1.0, 0.0, 0.0, alpha);
        return;
    }

    const float PI = 3.14159265358979323846;
    vec3 rgb = vColor.rgb;
    if (GpuParticleLighting(e) == 1u)
    {
        if (drawConstants.flags.y == 1u)
        {
            vec3 n = normalize(drawConstants.cameraPositionPreExposure.xyz - vWorldPos);
            vec3 irradiance = textureLod(diffuseIrradianceTexture, GpuParticleEquirectangularUV(n), 0.0).rgb;
            rgb = vColor.rgb * irradiance / PI * drawConstants.ambientColorIblIntensity.w;
        }
        else
        {
            rgb = vColor.rgb * drawConstants.ambientColorIblIntensity.rgb;
        }
    }
    // 非有限の成分だけをゼロにし、ほかの成分への伝播を防ぐ。
    rgb = mix(rgb, vec3(0.0), bvec3(
        isnan(rgb.r) || isinf(rgb.r),
        isnan(rgb.g) || isinf(rgb.g),
        isnan(rgb.b) || isinf(rgb.b)));
    rgb *= drawConstants.cameraPositionPreExposure.w;

    HeightFogForwardResult fog = EvaluateHeightFogForward(drawConstants.heightFog, heightFogSkyTexture, vWorldPos);
    float k = clamp(e.fogInfluence, 0.0, 1.0);
    fog.Transmittance = mix(1.0, fog.Transmittance, k);
    fog.Radiance *= k;

    if (GpuParticleBlend(e) == 0u)
    {
        outColor = ApplyHeightFogPremultiplied(vec4(rgb * alpha, alpha), fog);
    }
    else
    {
        outColor = ApplyHeightFogAdditive(vec4(rgb * alpha, 0.0), fog);
    }
}
