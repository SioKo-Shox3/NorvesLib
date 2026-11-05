// 空の遮蔽の格子積分と鏡面円錐を解析値および標本積分と比べる。
#include "Rendering/SkyOcclusionReference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr float kSqrtHalf = 0.7071067811865475f;
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::printf("失敗: %s\n", message);
        }
    }

    SkyOcclusionReferenceGrid MakeSlab(const SkyOcclusionParameters& parameters, float opacity)
    {
        SkyOccluderProxy canopy;
        canopy.CenterY = 5.0f;
        canopy.HalfExtentY = 5.0f;
        canopy.HalfExtentX = 1.0f;
        canopy.HalfExtentZ = 1.0f;
        canopy.Opacity = opacity;
        SetSkyOccluderShape(canopy, SkyOccluderShape::Box);
        SetSkyOccluderClass(canopy, SkyOccluderClass::Canopy);
        return BuildSkyOcclusionReferenceGrid(parameters,
            NorvesLib::Core::Container::Span<const SkyOccluderProxy>(&canopy, 1u),
            -0.5f, -0.5f, 1.0f, 1u);
    }

    void TestEmpty()
    {
        const SkyOcclusionParameters parameters;
        const SkyOcclusionReferenceGrid grid = BuildSkyOcclusionReferenceGrid(
            parameters, {}, -0.5f, -0.5f, 1.0f, 1u);
        const SkyOcclusionL1 coefficients = IntegrateSkyOcclusion(grid, parameters, 0.0f, 0.0f);
        const NorvesLib::Math::Vector3 normals[] = {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
            {kSqrtHalf, kSqrtHalf, 0.0f}, {-kSqrtHalf, kSqrtHalf, 0.0f},
            {0.0f, kSqrtHalf, kSqrtHalf}, {0.0f, -kSqrtHalf, -kSqrtHalf}};
        float maxError = 0.0f;
        for (const auto& normal : normals)
        {
            maxError = std::max(maxError, std::abs(EvaluateSkyOcclusionB(
                coefficients, normal.x, normal.y, normal.z)));
            maxError = std::max(maxError, std::abs(1.0f - EvaluateSkyOcclusionVisibility(
                coefficients, normal.x, normal.y, normal.z)));
        }
        const auto bent = EvaluateSkyOcclusionBentNormal(coefficients);
        Expect(bent.x == 0.0f && bent.y == 1.0f && bent.z == 0.0f,
            "遮蔽物がないときの曲げた法線は上向き");
        Expect(EvaluateSkySpecularOcclusion(coefficients, bent, 0.3f) == 1.0f,
            "遮蔽物がないとき上向き鏡面は可視");
        std::printf("遮蔽物なし: 最大誤差 %.8f\n", maxError);
        Expect(maxError <= 1.0e-6f, "遮蔽物がないときの遮蔽率と可視率");
    }

    void TestOpaqueSlab()
    {
        const SkyOcclusionParameters parameters;
        const SkyOcclusionReferenceGrid grid = MakeSlab(parameters, 1.0f);
        const SkyOcclusionL1 coefficients = IntegrateSkyOcclusion(grid, parameters, 0.0f, 0.0f);
        const float up = EvaluateSkyOcclusionVisibility(coefficients, 0.0f, 1.0f, 0.0f);
        const float horizontal[] = {
            EvaluateSkyOcclusionVisibility(coefficients, 1.0f, 0.0f, 0.0f),
            EvaluateSkyOcclusionVisibility(coefficients, -1.0f, 0.0f, 0.0f),
            EvaluateSkyOcclusionVisibility(coefficients, 0.0f, 0.0f, 1.0f),
            EvaluateSkyOcclusionVisibility(coefficients, 0.0f, 0.0f, -1.0f)};
        float maxError = std::abs(up);
        for (float value : horizontal)
        {
            maxError = std::max(maxError, std::abs(value - 0.5f));
            Expect(std::abs(value - 0.5f) <= 0.03f, "不透明な平板の水平可視率");
        }
        Expect(up <= 0.02f, "不透明な平板の上向き可視率");
        const auto bent = EvaluateSkyOcclusionBentNormal(coefficients);
        Expect(bent.x == 0.0f && bent.y == 1.0f && bent.z == 0.0f,
            "曲げた法線の長さがゼロに近いときは上向き");
        std::printf("不透明な平板: 最大誤差 %.8f\n", maxError);
    }

    void TestTransparentSlab()
    {
        const SkyOcclusionParameters parameters;
        const SkyOcclusionReferenceGrid grid = MakeSlab(parameters, 0.5f);
        const SkyOcclusionL1 coefficients = IntegrateSkyOcclusion(grid, parameters, 0.0f, 0.0f);
        constexpr int integrationCount = 4096;
        constexpr double sigma = 0.69314718055994530942 / 10.0;
        const double dTheta = (kPi * 0.5) / integrationCount;
        double b0 = 0.0;
        double cy = 0.0;
        double exactCosineB = 0.0;
        for (int index = 0; index < integrationCount; ++index)
        {
            const double theta = (index + 0.5) * dTheta;
            const double sine = std::sin(theta);
            const double cosine = std::cos(theta);
            const double length = std::min((10.0 - parameters.ReceiverHeightMeters) / sine,
                parameters.MaxHorizonMeters / cosine);
            const double b = 1.0 - std::exp(-sigma * length);
            b0 += 0.5 * b * cosine * dTheta;
            cy += 1.5 * b * sine * cosine * dTheta;
            exactCosineB += 2.0 * b * sine * cosine * dTheta;
        }
        const double expectedUp = 1.0 - b0 - (2.0 / 3.0) * cy;
        const double expectedHorizontal = 1.0 - b0;
        const double actualUp = EvaluateSkyOcclusionVisibility(coefficients, 0.0f, 1.0f, 0.0f);
        const double actualHorizontal = EvaluateSkyOcclusionVisibility(coefficients, 1.0f, 0.0f, 0.0f);
        const double maxError = std::max(std::abs(actualUp - expectedUp),
            std::abs(actualHorizontal - expectedHorizontal));
        std::printf("半透明な平板: 最大誤差 %.8f、L1 と厳密なコサイン重みの差 %.8f\n",
            maxError, (b0 + (2.0 / 3.0) * cy) - exactCosineB);
        Expect(maxError <= 0.03, "半透明な平板の L1 可視率");
    }

    void TestWalls()
    {
        const SkyOcclusionParameters parameters;
        constexpr std::uint32_t resolution = 512u;
        constexpr float texelMeters = 0.25f;
        constexpr float origin = -64.0f;
        constexpr float distance = 8.7f;
        SkyOcclusionReferenceGrid grid = BuildSkyOcclusionReferenceGrid(
            parameters, {}, origin, origin, texelMeters, resolution);
        const int angles[] = {30, 45, 60};
        double differences[3] = {};
        double maxError = 0.0;
        for (int index = 0; index < 3; ++index)
        {
            const double theta = angles[index] * kPi / 180.0;
            const float height = parameters.ReceiverHeightMeters +
                distance * static_cast<float>(std::tan(theta));
            for (std::uint32_t iz = 0; iz < resolution; ++iz)
            {
                const float z = origin + (static_cast<float>(iz) + 0.5f) * texelMeters;
                for (std::uint32_t ix = 0; ix < resolution; ++ix)
                {
                    const float x = origin + (static_cast<float>(ix) + 0.5f) * texelMeters;
                    const bool bWall = x >= 0.0f && x * x + z * z >= distance * distance;
                    const auto column = grid.At(ix, iz);
                    column.Ground = bWall ? height : parameters.GroundHeightMeters;
                    column.CanopyBottom = column.Ground;
                    column.CanopyTop = column.Ground;
                }
            }
            const SkyOcclusionL1 coefficients = IntegrateSkyOcclusion(grid, parameters, 0.0f, 0.0f);
            const double actual = EvaluateSkyOcclusionVisibility(coefficients, 0.0f, 1.0f, 0.0f);
            const double expected = 1.0 - 0.5 * std::sin(theta) * std::sin(theta);
            differences[index] = actual - expected;
            maxError = std::max(maxError, std::abs(differences[index]));
            Expect(std::abs(differences[index]) <= 0.07, "壁の上向き可視率");
        }
        std::printf("壁: 最大誤差 %.8f、30・45・60 度の差 %+.8f %+.8f %+.8f\n",
            maxError, differences[0], differences[1], differences[2]);
    }

    void TestSpecular()
    {
        constexpr int sampleCount = 300000;
        const float roughnesses[] = {0.1f, 0.3f, 0.6f};
        const float visibilities[] = {0.3f, 0.6f, 0.9f};
        const int angles[] = {0, 20, 45, 70};
        const NorvesLib::Math::Vector3 reflection(0.0f, 1.0f, 0.0f);
        std::mt19937 generator(0x58c0ffeeu);
        double maxError = 0.0;
        float worstRoughness = 0.0f;
        float worstVisibility = 0.0f;
        int worstAngle = 0;
        double worstReference = 0.0;
        double worstApproximation = 0.0;
        for (float roughness : roughnesses)
        {
            double weightedVisible[3][4] = {};
            double totalWeight = 0.0;
            const double ggxAlpha = static_cast<double>(roughness) * roughness;
            for (int sample = 0; sample < sampleCount; ++sample)
            {
                const double u = (static_cast<double>(generator()) + 0.5) / 4294967296.0;
                const double v = (static_cast<double>(generator()) + 0.5) / 4294967296.0;
                const double tangent = ggxAlpha * std::sqrt(u / (1.0 - u));
                const double denominator = 1.0 + tangent * tangent;
                const double ly = (1.0 - tangent * tangent) / denominator;
                if (ly <= 0.0)
                {
                    continue;
                }
                const double lx = 2.0 * tangent * std::cos(2.0 * kPi * v) / denominator;
                const double smithG = 2.0 * ly /
                    (ly + std::sqrt(ggxAlpha * ggxAlpha + (1.0 - ggxAlpha * ggxAlpha) * ly * ly));
                // 半ベクトルを D・(n・h) で標本した場合、反射方向の密度は D / 4。
                const double weight = smithG;
                totalWeight += weight;
                for (int visibilityIndex = 0; visibilityIndex < 3; ++visibilityIndex)
                {
                    const double coneCosine = std::sqrt(1.0 - visibilities[visibilityIndex]);
                    for (int angleIndex = 0; angleIndex < 4; ++angleIndex)
                    {
                        const double angle = angles[angleIndex] * kPi / 180.0;
                        if (lx * std::sin(angle) + ly * std::cos(angle) >= coneCosine)
                        {
                            weightedVisible[visibilityIndex][angleIndex] += weight;
                        }
                    }
                }
            }
            for (int visibilityIndex = 0; visibilityIndex < 3; ++visibilityIndex)
            {
                for (int angleIndex = 0; angleIndex < 4; ++angleIndex)
                {
                    const double angle = angles[angleIndex] * kPi / 180.0;
                    const NorvesLib::Math::Vector3 axis(
                        static_cast<float>(std::sin(angle)), static_cast<float>(std::cos(angle)), 0.0f);
                    const double reference = weightedVisible[visibilityIndex][angleIndex] / totalWeight;
                    const double approximation = EvaluateSkySpecularOcclusionCone(
                        axis, visibilities[visibilityIndex], reflection, roughness);
                    const double error = std::abs(approximation - reference);
                    if (error > maxError)
                    {
                        maxError = error;
                        worstRoughness = roughness;
                        worstVisibility = visibilities[visibilityIndex];
                        worstAngle = angles[angleIndex];
                        worstReference = reference;
                        worstApproximation = approximation;
                    }
                    Expect(error <= 0.05, "鏡面円錐と GGX の標本積分の差");
                }
            }
        }
        std::printf("鏡面: 最大誤差 %.8f（粗さ %.1f、可視率 %.1f、角度 %d 度、標本 %.8f、近似 %.8f）\n",
            maxError, worstRoughness, worstVisibility, worstAngle, worstReference, worstApproximation);
    }
} // namespace

int main()
{
    TestEmpty();
    TestOpaqueSlab();
    TestTransparentSlab();
    TestWalls();
    TestSpecular();
    if (g_FailureCount != 0)
    {
        std::printf("空の遮蔽参照試験: %d 件失敗\n", g_FailureCount);
        return 1;
    }
    std::printf("空の遮蔽参照試験: 成功\n");
    return 0;
}
