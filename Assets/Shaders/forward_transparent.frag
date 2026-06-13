#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragObjectColor;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 emissiveColor;
    vec4 pomParams;
} mvp;

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(albedoTexture, fragTexCoord);
    vec3 baseColor = texColor.rgb * fragObjectColor.rgb;
    float alpha = texColor.a * fragObjectColor.a;

    if (alpha <= 0.001)
    {
        discard;
    }

    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.98, 0.95);

    float diffuseFactor = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseFactor * lightColor * baseColor;
    vec3 ambient = 0.12 * baseColor;

    vec3 viewDir = normalize(mvp.cameraPosition.xyz - fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specularFactor = pow(max(dot(normal, halfDir), 0.0), 32.0);
    vec3 specular = specularFactor * lightColor * 0.2;

    outColor = vec4(ambient + diffuse + specular, alpha);
}
