#version 450

// 画面に向く歪みの円盤を、インスタンスごとに四角形として描く。
// 頂点バッファを使わず、頂点番号から局所の座標を作る。

layout(set = 0, binding = 0, std140) uniform DistortionData
{
    mat4 viewProjection;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 params;
} distortion;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inRadius;
layout(location = 2) in float inStrength;
layout(location = 3) in float inPatternScale;
layout(location = 4) in float inScrollSpeed;
layout(location = 5) in uint inSeed;

layout(location = 0) out vec2 corner;
layout(location = 1) flat out float strength;
layout(location = 2) flat out float patternScale;
layout(location = 3) flat out float scrollSpeed;
layout(location = 4) flat out uint seed;

const vec2 corners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main()
{
    corner = corners[gl_VertexIndex];
    vec3 worldPosition = inPosition
        + (distortion.cameraRight.xyz * corner.x + distortion.cameraUp.xyz * corner.y) * inRadius;
    gl_Position = distortion.viewProjection * vec4(worldPosition, 1.0);
    strength = inStrength;
    patternScale = inPatternScale;
    scrollSpeed = inScrollSpeed;
    seed = inSeed;
}
