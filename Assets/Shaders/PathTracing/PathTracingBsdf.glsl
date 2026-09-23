// パストレーサーの表面BSDF。評価・確率密度・標本化を同じ式で与え、光源標本とBSDF標本を整合させる。
//
// Production: ラスタのIBL端点（lighting.fragのEvaluateIblEndpoint）と同じエネルギー分配にする。
//   鏡面 = GGX単散乱 × Schlick Fresnel × 多重散乱補償 1+F0(1-Ess)/Ess（Ess=A+B、DFG LUT）
//   拡散 = (1-metallic)(1-Ed)·アルベド/π（Ed=(F0d·A+B)·補償、視線方向の鏡面反射率）
//   遮蔽項はDFG LUTの積分と同じSchlick-GGX（k=roughness^2/2）なので、アルベド1の白炉は1へ収束する。
// ValidationLambert: ラスタの検証mode 253と同じ純Lambert（アルベド/π）。
// 取り込む側でPathTracingCommon.glslとCommon/PbrMaterialEvaluation.glslを先に取り込む。

layout(set = 0, binding = 9) uniform sampler2D dfgLut;

struct PathSurface
{
    vec3 Normal;
    vec3 Tangent;
    vec3 Bitangent;
    vec3 View;
    float NdotV;
    float Alpha;
    float Roughness;
    float Metallic;
    vec3 DiffuseBrdf; // 拡散のBRDF値（入射方向に依らない）
    vec3 F0Dielectric;
    vec3 F0Conductor;
    vec3 CompDielectric;
    vec3 CompConductor;
    float SpecularProbability; // 鏡面葉を標本化する確率
    uint Mode;
};

float PathLuminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float PowerHeuristic(float pdfA, float pdfB)
{
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 + b2 > 0.0 ? a2 / (a2 + b2) : 0.0;
}

// GGXの法線分布。共通のDistributionGGXは分母を1e-4で止めるため鋭い葉の頂点を削る。
// 標本化の確率密度と一致させるため、ここでは分母を止めない厳密形を使う。
float PathGgxDistribution(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float denominator = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denominator * denominator);
}

// 可視法線分布の確率密度に使う厳密なSmith G1。
float PathSmithG1(float NdotX, float alpha)
{
    float a2 = alpha * alpha;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * NdotX * NdotX));
}

// DFG LUTの積分と同じSchlick-GGX（k=roughness^2/2）の遮蔽項。
float PathDfgGeometry(float NdotV, float NdotL, float roughness)
{
    float k = roughness * roughness * 0.5;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

PathSurface MakePathSurface(vec3 normal, vec3 view, vec3 albedo, float metallic,
                            float roughness, uint mode)
{
    PathSurface surface;
    surface.Normal = normal;
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    surface.Tangent = normalize(cross(up, normal));
    surface.Bitangent = cross(normal, surface.Tangent);
    surface.View = view;
    surface.NdotV = max(dot(normal, view), 1.0e-4);
    // 葉の粗さはDFG LUTの標本域（両端は半texel内側）に揃える。LUTはroughness 1を0.998で読むため、
    // 実際の葉をそれより粗くすると補償1/Essと積分する葉がずれ、白炉がエネルギーを失う。
    surface.Roughness = mode == PATH_BSDF_VALIDATION_LAMBERT
        ? clamp(roughness, 0.0, 1.0)
        : clamp(roughness, 0.5 / 256.0, 255.5 / 256.0);
    surface.Alpha = max(surface.Roughness * surface.Roughness, 1.0e-4);
    surface.Metallic = clamp(metallic, 0.0, 1.0);
    surface.Mode = mode;
    surface.F0Dielectric = vec3(0.04);
    surface.F0Conductor = albedo;
    surface.CompDielectric = vec3(1.0);
    surface.CompConductor = vec3(1.0);
    if (mode == PATH_BSDF_VALIDATION_LAMBERT)
    {
        surface.DiffuseBrdf = albedo / PI;
        surface.SpecularProbability = 0.0;
        return surface;
    }

    // ラスタと同じLUT座標（半texel内側へclamp）。
    vec2 dfg = textureLod(dfgLut,
                          clamp(vec2(surface.NdotV, surface.Roughness),
                                vec2(0.5 / 256.0), vec2(255.5 / 256.0)),
                          0.0).rg;
    float Ess = max(dfg.x + dfg.y, 0.0001);
    surface.CompDielectric = vec3(1.0) + surface.F0Dielectric * (1.0 - Ess) / Ess;
    surface.CompConductor = vec3(1.0) + surface.F0Conductor * (1.0 - Ess) / Ess;
    vec3 Ed = clamp((surface.F0Dielectric * dfg.x + dfg.y) * surface.CompDielectric,
                    vec3(0.0), vec3(1.0));
    vec3 Ec = clamp((surface.F0Conductor * dfg.x + dfg.y) * surface.CompConductor,
                    vec3(0.0), vec3(1.0));
    surface.DiffuseBrdf = (1.0 - surface.Metallic) * (vec3(1.0) - Ed) * albedo / PI;

    // 葉の選択確率は各葉の方向反射率の比にする。
    float specularEnergy = PathLuminance((1.0 - surface.Metallic) * Ed + surface.Metallic * Ec);
    float diffuseEnergy = PathLuminance(surface.DiffuseBrdf) * PI;
    float totalEnergy = specularEnergy + diffuseEnergy;
    surface.SpecularProbability = totalEnergy > 1.0e-6 ? specularEnergy / totalEnergy : 0.5;
    return surface;
}

// BRDF値 f(V, L)。余弦は含まない。
vec3 EvaluatePathBsdf(PathSurface surface, vec3 L)
{
    float NdotL = dot(surface.Normal, L);
    if (NdotL <= 0.0)
    {
        return vec3(0.0);
    }
    if (surface.Mode == PATH_BSDF_VALIDATION_LAMBERT)
    {
        return surface.DiffuseBrdf;
    }
    vec3 H = normalize(surface.View + L);
    float NdotH = max(dot(surface.Normal, H), 0.0);
    float VdotH = max(dot(surface.View, H), 0.0);
    float D = PathGgxDistribution(NdotH, surface.Alpha);
    float G = PathDfgGeometry(surface.NdotV, NdotL, surface.Roughness);
    float specularCommon = D * G / (4.0 * surface.NdotV * NdotL);
    vec3 Fd = FresnelSchlick(VdotH, surface.F0Dielectric);
    vec3 Fc = FresnelSchlick(VdotH, surface.F0Conductor);
    vec3 specular = specularCommon *
        ((1.0 - surface.Metallic) * Fd * surface.CompDielectric +
         surface.Metallic * Fc * surface.CompConductor);
    return surface.DiffuseBrdf + specular;
}

// 立体角あたりの確率密度。鏡面葉は可視法線分布 pdf(L)=D(H)·G1(V)/(4·NdotV)。
float PathBsdfPdf(PathSurface surface, vec3 L)
{
    float NdotL = dot(surface.Normal, L);
    if (NdotL <= 0.0)
    {
        return 0.0;
    }
    float diffusePdf = NdotL / PI;
    if (surface.Mode == PATH_BSDF_VALIDATION_LAMBERT)
    {
        return diffusePdf;
    }
    vec3 H = normalize(surface.View + L);
    float NdotH = max(dot(surface.Normal, H), 0.0);
    float specularPdf = PathGgxDistribution(NdotH, surface.Alpha) *
        PathSmithG1(surface.NdotV, surface.Alpha) / (4.0 * surface.NdotV);
    return surface.SpecularProbability * specularPdf +
           (1.0 - surface.SpecularProbability) * diffusePdf;
}

// Heitz 2018「Sampling the GGX Distribution of Visible Normals」（等方）。入出力は接空間。
vec3 SampleGgxVisibleNormal(vec3 view, float alpha, vec2 u)
{
    vec3 stretched = normalize(vec3(alpha * view.x, alpha * view.y, view.z));
    float lengthSquared = stretched.x * stretched.x + stretched.y * stretched.y;
    vec3 axis1 = lengthSquared > 0.0
        ? vec3(-stretched.y, stretched.x, 0.0) * inversesqrt(lengthSquared)
        : vec3(1.0, 0.0, 0.0);
    vec3 axis2 = cross(stretched, axis1);
    float radius = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    float t1 = radius * cos(phi);
    float t2 = radius * sin(phi);
    float s = 0.5 * (1.0 + stretched.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
    vec3 normal = t1 * axis1 + t2 * axis2 +
                  sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * stretched;
    return normalize(vec3(alpha * normal.x, alpha * normal.y, max(normal.z, 0.0)));
}

// 葉を選んで入射方向を1本引く。u.xは葉の選択、u.yzは方向に使う。
bool SamplePathBsdf(PathSurface surface, vec3 u, out vec3 L)
{
    if (surface.Mode != PATH_BSDF_VALIDATION_LAMBERT && u.x < surface.SpecularProbability)
    {
        vec3 viewTangent = normalize(vec3(dot(surface.View, surface.Tangent),
                                          dot(surface.View, surface.Bitangent),
                                          surface.NdotV));
        vec3 halfTangent = SampleGgxVisibleNormal(viewTangent, surface.Alpha, u.yz);
        vec3 H = halfTangent.x * surface.Tangent + halfTangent.y * surface.Bitangent +
                 halfTangent.z * surface.Normal;
        L = reflect(-surface.View, H);
    }
    else
    {
        float phi = 2.0 * PI * u.y;
        float radius = sqrt(u.z);
        L = normalize(surface.Tangent * (radius * cos(phi)) +
                      surface.Bitangent * (radius * sin(phi)) +
                      surface.Normal * sqrt(max(0.0, 1.0 - u.z)));
    }
    return dot(surface.Normal, L) > 0.0;
}
