#version 450

// デバッグの線。Upscale の後の最終解像度の画像へ描き、内部解像度のシーンの深度で遮蔽する。
layout(location = 0) in vec4 outColor;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    // xy: 描画先の画素座標からシーンの深度の画素座標への倍率（同じ解像度なら1）
    vec4 depthCoordScale;
} camera;

layout(set = 0, binding = 1) uniform sampler2D sceneDepthTexture;

void main()
{
    ivec2 depthSize = textureSize(sceneDepthTexture, 0);
    ivec2 depthCoord = clamp(ivec2(gl_FragCoord.xy * camera.depthCoordScale.xy), ivec2(0), depthSize - ivec2(1));
    float sceneDepth = texelFetch(sceneDepthTexture, depthCoord, 0).r;
    // 深度テスト（Less）と同じく、シーンより手前の線だけを描く。
    if (!(gl_FragCoord.z < sceneDepth))
    {
        discard;
    }
    fragColor = outColor;
}
