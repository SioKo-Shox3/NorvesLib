#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragCameraPos;
layout(location = 3) in vec3 fragObjectColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.98, 0.95);

    vec3 baseColor = fragObjectColor;

    vec3 ambient = 0.15 * baseColor;

    vec3 normal = normalize(fragNormal);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * baseColor;

    vec3 viewDir = normalize(fragCameraPos - fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);
    vec3 specular = spec * lightColor * 0.5;

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, 1.0);
}