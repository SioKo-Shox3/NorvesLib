#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in vec4 fragColor;
layout(location = 2) flat in vec4 fragEdgeColor;
layout(location = 3) flat in vec4 fragDissolve;
layout(location = 0) out vec4 outColor;

#include "Common/MeshParticleDissolve.glsl"

void main()
{
    float noise = MeshParticleDissolveNoise(fragTexCoord);
    uint classification = MeshParticleDissolveClassify(noise, fragDissolve.x, fragDissolve.y);
    if (classification == MESH_PARTICLE_DISSOLVE_DISCARD)
    {
        discard;
    }
    outColor = classification == MESH_PARTICLE_DISSOLVE_EDGE ? fragEdgeColor : fragColor;
}
