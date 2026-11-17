#version 450
#extension GL_GOOGLE_include_directive : require
#include "Particles/ParticleDraw.glsl"

// GPU の粒を板または速度に沿った筋として描く。

layout(location = 0) out vec2 vQuadUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) flat out uint vEmitterIndex;
layout(location = 4) flat out uint vOrientation;
layout(location = 5) out float vStreakAspect;

const vec2 kParticleCorners[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, -1.0),
    vec2(1.0, 1.0));

void main()
{
    // 無効な粒はすべての頂点を同じ画面外の点に置く。
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vQuadUV = vec2(0.0);
    vColor = vec4(0.0);
    vWorldPos = vec3(0.0);
    vEmitterIndex = 0u;
    vOrientation = 0u;
    vStreakAspect = 0.0;

    uint instanceIndex = uint(gl_InstanceIndex);
    uint slot = instanceIndex;
    if (drawConstants.flags.w != kGpuParticleDrawSourceDirect)
    {
        uint bucket = drawConstants.flags.w - 1u;
        if (bucket >= kGpuParticleDrawBucketCount || instanceIndex >= kGpuParticlePoolCapacity)
        {
            return;
        }
        slot = drawList.slots[bucket * kGpuParticlePoolCapacity + instanceIndex];
    }
    if (slot >= kGpuParticlePoolCapacity)
    {
        return;
    }
    GpuParticle p = drawPool.particles[slot];
    if ((p.meta.z & kGpuParticleFlagAlive) == 0u || p.meta.y >= 1024u ||
        any(isnan(p.posAge.xyz)) || any(isinf(p.posAge.xyz)) ||
        any(isnan(p.velLife.xyz)) || any(isinf(p.velLife.xyz)))
    {
        return;
    }

    uint emitterIndex = drawSlotToEmitter.slotToEmitter[p.meta.y];
    if (emitterIndex == kGpuParticleInvalidEmitter || emitterIndex >= uint(drawEmitters.emitters.length()))
    {
        return;
    }

    GpuEmitterParams e = drawEmitters.emitters[emitterIndex];
    uint poolOffset = e.poolCapacitySeedFlags.x;
    uint capacity = e.poolCapacitySeedFlags.y;
    // 加算の桁あふれを避けて、スロットがエミッタの範囲にあるか調べる。
    if (slot < poolOffset || slot - poolOffset >= capacity || GpuParticlePass(e) != drawConstants.flags.x)
    {
        return;
    }

    GpuParticleLook look = GpuParticleEvaluateLook(e, p);
    if (!(look.color.a > 0.0) || !all(greaterThan(look.size, vec2(0.0))))
    {
        return;
    }

    vec2 corner = kParticleCorners[gl_VertexIndex];
    uint orientation = GpuParticleOrientation(e);
    if (orientation > 2u)
    {
        orientation = 0u;
    }

    vec3 normal = vec3(0.0);
    if (orientation == 2u && !GpuParticleUnpackNormal(p.meta.w, normal))
    {
        orientation = 0u;
    }

    if (orientation == 1u)
    {
        vec3 head = p.posAge.xyz;
        vec3 relativeVelocity = p.velLife.xyz - drawConstants.cameraVelocity.xyz;
        vec3 tail = head - relativeVelocity * e.streakShutterSeconds * look.stretch;
        if (any(isnan(tail)) || any(isinf(tail)))
        {
            return;
        }

        float headDepth = GpuParticleViewDepth(head);
        float tailDepth = GpuParticleViewDepth(tail);
        float nearCut = drawConstants.nearFarParams.x * 1.001;
        if (isnan(headDepth) || isinf(headDepth) || isnan(tailDepth) || isinf(tailDepth) ||
            isnan(nearCut) || isinf(nearCut) || (headDepth < nearCut && tailDepth < nearCut))
        {
            return;
        }

        // 片端がニア面の手前なら、元の線分上で深度が一致する位置へ移す。
        if (headDepth < nearCut)
        {
            head = mix(head, tail, (nearCut - headDepth) / (tailDepth - headDepth));
            headDepth = nearCut;
        }
        else if (tailDepth < nearCut)
        {
            tail = mix(tail, head, (nearCut - tailDepth) / (headDepth - tailDepth));
            tailDepth = nearCut;
        }

        vec2 halfTargetSize = 0.5 * drawConstants.targetSizeInvSize.xy;
        float pixelScale = halfTargetSize.y * abs(drawConstants.projection[1][1]);
        if (!all(greaterThan(halfTargetSize, vec2(0.0))) || !(pixelScale > 0.0) ||
            any(isinf(halfTargetSize)) || isinf(pixelScale))
        {
            return;
        }

        vec4 headClip = drawConstants.projection * drawConstants.view * vec4(head, 1.0);
        vec4 tailClip = drawConstants.projection * drawConstants.view * vec4(tail, 1.0);
        if (any(isnan(headClip)) || any(isinf(headClip)) || any(isnan(tailClip)) || any(isinf(tailClip)) ||
            !(headClip.w > 0.0) || !(tailClip.w > 0.0))
        {
            return;
        }

        vec2 headPixel = headClip.xy / headClip.w * halfTargetSize;
        vec2 tailPixel = tailClip.xy / tailClip.w * halfTargetSize;
        vec2 direction = headPixel - tailPixel;
        float pixelLength = length(direction);
        if (isnan(pixelLength) || isinf(pixelLength))
        {
            return;
        }
        vec2 along = pixelLength < 1.0e-3 ? vec2(1.0, 0.0) : direction / pixelLength;
        vec2 across = vec2(-along.y, along.x);

        bool orthographic = drawConstants.projection[3][3] == 1.0;
        float headWidth = look.size.x * pixelScale;
        float tailWidth = headWidth;
        if (!orthographic)
        {
            headWidth /= headDepth;
            tailWidth /= tailDepth;
        }
        if (isnan(headWidth) || isinf(headWidth) || isnan(tailWidth) || isinf(tailWidth))
        {
            return;
        }

        // 端ごとに幅を広げ、元の画素幅を不透明度に掛ける。
        float headCoverage = 1.0;
        float tailCoverage = 1.0;
        if (GpuParticleSubPixelCompensation(e))
        {
            if (headWidth < 1.0)
            {
                headCoverage = headWidth;
                headWidth = 1.0;
            }
            if (tailWidth < 1.0)
            {
                tailCoverage = tailWidth;
                tailWidth = 1.0;
            }
        }

        float averageWidth = 0.5 * (headWidth + tailWidth);
        if (!(averageWidth > 0.0) || isinf(averageWidth))
        {
            return;
        }

        bool isHead = corner.y > 0.0;
        float width = isHead ? headWidth : tailWidth;
        vec2 pixelOffset = 0.5 * width * (along * corner.y + across * corner.x);
        vec4 clip = isHead ? headClip : tailClip;
        clip.xy += pixelOffset / halfTargetSize * clip.w;

        // 投影の軸反転を戻し、端の深度で画素のずれをワールド位置へ近似する。
        float depthScale = orthographic ? 1.0 : (isHead ? headDepth : tailDepth);
        vec2 worldOffset = pixelOffset * sign(vec2(drawConstants.projection[0][0], drawConstants.projection[1][1]));
        vec3 worldPos = (isHead ? head : tail) +
            (drawConstants.cameraRight.xyz * worldOffset.x + drawConstants.cameraUp.xyz * worldOffset.y) *
                (depthScale / pixelScale);

        gl_Position = clip;
        vWorldPos = worldPos;
        vColor = vec4(look.color.rgb, look.color.a * (isHead ? headCoverage : tailCoverage));
        vStreakAspect = (pixelLength + averageWidth) / averageWidth;
    }
    else
    {
        vec3 right = drawConstants.cameraRight.xyz;
        vec3 up = drawConstants.cameraUp.xyz;
        vec3 center = p.posAge.xyz;
        if (orientation == 2u)
        {
            vec3 reference = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            right = normalize(cross(normal, reference));
            up = cross(normal, right);
            center += normal * 0.02;
        }

        vec3 worldPos = center + right * (0.5 * corner.x * look.size.x) +
            up * (0.5 * corner.y * look.size.y);
        gl_Position = drawConstants.projection * drawConstants.view * vec4(worldPos, 1.0);
        vWorldPos = worldPos;
        vColor = look.color;
        vStreakAspect = 1.0;
    }

    vQuadUV = corner;
    vEmitterIndex = emitterIndex;
    vOrientation = orientation;
}
