#version 450

layout(set = 0, binding = 0) uniform WorldBoardForwardData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 sceneColorParams;
} worldBoard;

layout(set = 0, binding = 1) uniform sampler2D boardTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(boardTexture, fragUV) * fragColor;
    outColor = vec4(color.rgb * color.a * worldBoard.sceneColorParams.x, color.a);
}
