#version 450

// ========================================
// ブルームの合成パス
// ========================================
// 縮小・拡大の段を積み上げた結果（半分の解像度）を3×3のテントで拡大し、元の色へ混ぜる。
// 積み上げた結果は各段の和なので、段数で割って平均にする。
// しきい値なし（既定）: 元の色と平均を一定の割合で線形補間し、全体のエネルギーを保つ。
// しきい値あり: 取り出した明るい部分を強度倍して元の色へ加える。
// レンズダート（強さが正のとき）: ブルームのうちしきい値を超えた明るい部分へダートの模様を掛けて加える。
// 模様は縦横の画素の比を保って画面へ貼る（横長の画面では模様の縦の中央部を使う）。
// 入力: SceneColor (HDR R16G16B16A16_FLOAT)
// 出力: Bloomed SceneColor (HDR R16G16B16A16_FLOAT)

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: SceneColor テクスチャ
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// binding 1: 積み上げたブルーム（最上段の拡大結果）
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

// binding 2: レンズダートの模様（RGBA8、リニアな値）
layout(set = 0, binding = 2) uniform sampler2D lensDirtTexture;

// binding 3: 合成パラメータ UBO (std140)
layout(set = 0, binding = 3) uniform BloomCompositeParams
{
    // x = 強度（しきい値なしでは補間の割合、ありでは加算の乗数）
    // y = テントの半径（ブルームのテクセル単位）
    // z = しきい値ありなら1
    // w = 1 / 段数
    vec4 params;
    // x = レンズダートの強さ（0で無効）
    // y = レンズダートが乗り始めるブルームの明るさ（プリエクスポージャ後）
    // z = ダートの加算の輝度をその画素の明るさ（ブルーム合成後）の何倍までに抑えるか（0で抑えない）
    vec4 dirtParams;
};

vec3 SampleTent(sampler2D source, vec2 uv, float radius)
{
    vec2 offset = radius / vec2(textureSize(source, 0));

    vec3 sum = texture(source, uv).rgb * 4.0;
    sum += texture(source, uv + vec2(-offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2( offset.x, 0.0)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0, -offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(0.0,  offset.y)).rgb * 2.0;
    sum += texture(source, uv + vec2(-offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x, -offset.y)).rgb;
    sum += texture(source, uv + vec2(-offset.x,  offset.y)).rgb;
    sum += texture(source, uv + vec2( offset.x,  offset.y)).rgb;
    return sum * (1.0 / 16.0);
}

void main()
{
    vec3 originalColor = texture(sceneColor, fragUV).rgb;
    float intensity = params.x;

    if (intensity <= 0.0)
    {
        outColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 bloom = SampleTent(bloomTexture, fragUV, params.y) * params.w;

    vec3 result = params.z > 0.5
                      ? originalColor + bloom * intensity
                      : mix(originalColor, bloom, intensity);

    if (dirtParams.x > 0.0)
    {
        vec2 sceneSize = vec2(textureSize(sceneColor, 0));
        float aspect = sceneSize.x / max(sceneSize.y, 1.0);
        vec2 dirtScale = aspect >= 1.0 ? vec2(1.0, 1.0 / aspect) : vec2(aspect, 1.0);
        vec3 dirt = texture(lensDirtTexture, (fragUV - 0.5) * dirtScale + 0.5).rgb;
        vec3 brightBloom = max(bloom - vec3(dirtParams.y), vec3(0.0));
        vec3 dirtLight = brightBloom * dirt * dirtParams.x;
        // 暗い背景の上では、光源から離れた画素でもブルームがしきい値を超えると、しみが背景より桁違いに明るい
        // 色付きの円（ゴースト）になる。加算の輝度を、その画素の明るさ L の z 倍へ向けてなめらかに頭打ちにし
        // （a·zL/(a + zL)。a ≪ zL では a のまま）、明るい空やにじみの中の模様は残して暗い背景の上では淡い斑にとどめる。
        if (dirtParams.z > 0.0)
        {
            const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);
            float limit = dirtParams.z * dot(result, kLuma);
            float dirtLuma = dot(dirtLight, kLuma);
            dirtLight *= dirtLuma > 0.0 ? limit / (limit + dirtLuma) : 0.0;
        }
        result += dirtLight;
    }

    outColor = vec4(result, 1.0);
}
