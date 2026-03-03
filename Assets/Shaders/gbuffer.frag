#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveColor;

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: Albedo (RGB) + alpha
layout(location = 1) out vec4 outNormal;    // RT1: World Normal (RGB) + unused
layout(location = 2) out vec4 outMaterial;  // RT2: Metallic(R) / Roughness(G) / AO(B) / unused(A)
layout(location = 3) out vec4 outEmissive;  // RT3: Emissive (RGB, HDR) + unused

void main()
{
    // Albedo: オブジェクトカラーをそのまま出力
    outAlbedo = vec4(fragObjectColor, 1.0);

    // Normal: ワールド法線を[-1,1]→[0,1]にエンコード（R16G16B16A16_FLOATなのでそのままでもOKだが正規化）
    vec3 normal = normalize(fragNormal);
    outNormal = vec4(normal, 0.0);

    // Material: デフォルトPBRパラメータ
    // Metallic = 0.0 (非金属)
    // Roughness = 0.5 (中間)
    // AO = 1.0 (遮蔽なし)
    outMaterial = vec4(0.0, 0.5, 1.0, 0.0);

    // Emissive: エミッシブカラー × 強度 → HDR値
    // 強度が0なら完全に黒（非発光）
    outEmissive = vec4(fragEmissiveColor.rgb * fragEmissiveColor.a, 1.0);
}
