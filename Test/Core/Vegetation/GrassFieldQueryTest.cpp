// 草の場の CPU クエリを、画素配置と遮蔽の解析値に照らして検証する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Vegetation/GrassField.h"
#include "Vegetation/GrassSpecies.h"
#include "Vegetation/GroundHeightSource.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Vegetation;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

namespace
{
    constexpr uint32_t LayerAResolution = 64;
    constexpr uint32_t LayerBResolution = 32;
    constexpr uint32_t LayerABytes = LayerAResolution * LayerAResolution * 4;
    constexpr uint32_t LayerBBytes = LayerBResolution * LayerBResolution * 4;
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    uint32_t FloatBits(float value)
    {
        static_assert(sizeof(float) == sizeof(uint32_t));
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    bool SameSampleBits(const GrassSample& left, const GrassSample& right)
    {
        return left.bValid == right.bValid && left.SpeciesId == right.SpeciesId &&
            FloatBits(left.Density) == FloatBits(right.Density) &&
            FloatBits(left.HeightMeters) == FloatBits(right.HeightMeters) &&
            FloatBits(left.Moisture) == FloatBits(right.Moisture) &&
            FloatBits(left.Tint.x) == FloatBits(right.Tint.x) &&
            FloatBits(left.Tint.y) == FloatBits(right.Tint.y) &&
            FloatBits(left.Tint.z) == FloatBits(right.Tint.z);
    }

    bool SetLinearChunks(GrassField& field)
    {
        uint8_t layerA[LayerABytes];
        uint8_t layerB[LayerBBytes];
        for (uint32_t chunkX = 0; chunkX < 2; ++chunkX)
        {
            for (uint32_t z = 0; z < LayerAResolution; ++z)
            {
                for (uint32_t x = 0; x < LayerAResolution; ++x)
                {
                    const uint32_t offset = (z * LayerAResolution + x) * 4;
                    layerA[offset] = static_cast<uint8_t>(chunkX * LayerAResolution + x);
                    layerA[offset + 1] = 255;
                    layerA[offset + 2] = 128;
                    layerA[offset + 3] = 0;
                }
            }
            for (uint32_t z = 0; z < LayerBResolution; ++z)
            {
                for (uint32_t x = 0; x < LayerBResolution; ++x)
                {
                    const uint32_t offset = (z * LayerBResolution + x) * 4;
                    const uint32_t globalX = chunkX * LayerBResolution + x;
                    layerB[offset] = static_cast<uint8_t>(64 + globalX);
                    layerB[offset + 1] = 128;
                    layerB[offset + 2] = static_cast<uint8_t>(192 - globalX);
                    layerB[offset + 3] = 255;
                }
            }
            if (!field.SetChunk({static_cast<int32_t>(chunkX), 0},
                                Container::Span<const uint8_t>(layerA, LayerABytes),
                                Container::Span<const uint8_t>(layerB, LayerBBytes)))
            {
                return false;
            }
        }
        return true;
    }

    bool SetUniformChunk(GrassField& field)
    {
        uint8_t layerA[LayerABytes];
        uint8_t layerB[LayerBBytes];
        for (uint32_t offset = 0; offset < LayerABytes; offset += 4)
        {
            layerA[offset] = 128;
            layerA[offset + 1] = 0;
            layerA[offset + 2] = 128;
            layerA[offset + 3] = 0;
        }
        for (uint32_t offset = 0; offset < LayerBBytes; offset += 4)
        {
            layerB[offset] = 128;
            layerB[offset + 1] = 128;
            layerB[offset + 2] = 128;
            layerB[offset + 3] = 255;
        }
        return field.SetChunk({0, 0}, Container::Span<const uint8_t>(layerA, LayerABytes),
                              Container::Span<const uint8_t>(layerB, LayerBBytes));
    }

    void TestLinearDensityAndSeam(const GrassField& field)
    {
        const float positionsZ[] = {0.25f, 5.125f, 13.25f, 20.75f, 31.75f};
        double maxDensityError = 0.0;
        uint32_t sampleCount = 0;
        for (float z : positionsZ)
        {
            for (uint32_t index = 0; index <= 256; ++index)
            {
                const float x = 0.25f + 63.5f * static_cast<float>(index) / 256.0f;
                const GrassSample sample = field.Sample(x, z);
                const double expected = (static_cast<double>(x) / 0.5 - 0.5) / 255.0;
                const double error = std::fabs(static_cast<double>(sample.Density) - expected);
                if (error > maxDensityError)
                {
                    maxDensityError = error;
                }
                if (!sample.bValid || !(error <= 1e-4))
                {
                    ++g_FailureCount;
                    std::cout << "失敗: 線形密度 x=" << x << " z=" << z
                              << " 実測=" << sample.Density << " 期待=" << expected << "\n";
                }
                ++sampleCount;
            }
        }

        const float seamPositionsZ[] = {0.25f, 0.75f, 5.125f, 13.25f, 20.75f, 31.25f, 31.75f};
        double maxSeamDensityDifference = 0.0;
        double maxSeamTintDifference = 0.0;
        double maxSeamTintError = 0.0;
        for (float z : seamPositionsZ)
        {
            const float leftX = 32.0f - 1e-3f;
            const float rightX = 32.0f + 1e-3f;
            const GrassSample left = field.Sample(leftX, z);
            const GrassSample right = field.Sample(rightX, z);
            Expect(left.bValid && right.bValid, "継ぎ目の両側のサンプルが有効");
            const double densityDifference = std::fabs(static_cast<double>(left.Density) - right.Density);
            if (densityDifference > maxSeamDensityDifference)
            {
                maxSeamDensityDifference = densityDifference;
            }
            Expect(densityDifference <= 1.0 / 255.0, "継ぎ目の密度差が 1/255 以下");

            const float leftTint[] = {left.Tint.x, left.Tint.y, left.Tint.z};
            const float rightTint[] = {right.Tint.x, right.Tint.y, right.Tint.z};
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                const double difference = std::fabs(static_cast<double>(leftTint[channel]) - rightTint[channel]);
                if (difference > maxSeamTintDifference)
                {
                    maxSeamTintDifference = difference;
                }
                Expect(difference <= 1.0 / 128.0, "継ぎ目の色の成分差が 1/128 以下");
                const double expectedLeft = channel == 0 ? (64.0 + leftX - 0.5) / 128.0 :
                    channel == 1 ? 1.0 : (192.0 - (leftX - 0.5)) / 128.0;
                const double expectedRight = channel == 0 ? (64.0 + rightX - 0.5) / 128.0 :
                    channel == 1 ? 1.0 : (192.0 - (rightX - 0.5)) / 128.0;
                const double leftError = std::fabs(static_cast<double>(leftTint[channel]) - expectedLeft);
                const double rightError = std::fabs(static_cast<double>(rightTint[channel]) - expectedRight);
                if (leftError > maxSeamTintError)
                {
                    maxSeamTintError = leftError;
                }
                if (rightError > maxSeamTintError)
                {
                    maxSeamTintError = rightError;
                }
                Expect(leftError <= 1e-4 && rightError <= 1e-4, "継ぎ目の色が線形補間の解析値に一致");
            }
        }
        std::cout << "線形密度: " << sampleCount << " 点、最大絶対誤差=" << maxDensityError << "\n";
        std::cout << "継ぎ目: 最大密度差=" << maxSeamDensityDifference
                  << " 最大色成分差=" << maxSeamTintDifference
                  << " 最大色解析誤差=" << maxSeamTintError << "\n";
    }

    void TestOutside(const GrassField& field)
    {
        const float positionsX[] = {-1.0f, 100.0f};
        for (float x : positionsX)
        {
            const GrassSample sample = field.Sample(x, 8.0f);
            Expect(!sample.bValid && sample.Density == 0.0f, "範囲外の密度は 0 で無効");
            Expect(!field.QuerySurface(x, 8.0f).bValid, "範囲外の地表は無効");
            Expect(field.GetCameraClearance(Math::Vector3(x, 0.0f, 8.0f)) == 0.0f,
                   "範囲外のカメラの持ち上げ量は 0");
            Expect(field.QueryTopHeight(x, 8.0f) == 0.0f, "範囲外の草の頂点は既定地面の高さ");
        }
        std::cout << "範囲外: 2 点を検査\n";
    }

    void TestOcclusionAndDeterminism(GrassField& field, const GrassField& sameField)
    {
        struct Segment
        {
            float X0;
            float Z0;
            float X1;
            float Z1;
        };
        const Segment segments[] = {{1.0f, 2.0f, 11.0f, 2.0f},
                                    {3.25f, 12.0f, 14.25f, 12.0f},
                                    {4.0f, 4.0f, 10.0f, 12.0f}};
        const float heights[] = {0.0f, 0.5f, 2.0f};
        double maxRelativeError = 0.0;
        uint32_t occlusionCount = 0;
        for (const Segment& segment : segments)
        {
            const double dx = static_cast<double>(segment.X1) - segment.X0;
            const double dz = static_cast<double>(segment.Z1) - segment.Z0;
            const double length = std::sqrt(dx * dx + dz * dz);
            for (float height : heights)
            {
                const Math::Vector3 a(segment.X0, height, segment.Z0);
                const Math::Vector3 b(segment.X1, height, segment.Z1);
                const float transmittance = field.QueryOcclusion(a, b);
                const float repeated = field.QueryOcclusion(a, b);
                const float independent = sameField.QueryOcclusion(a, b);
                Expect(FloatBits(transmittance) == FloatBits(repeated) &&
                       FloatBits(transmittance) == FloatBits(independent),
                       "遮蔽の反復と別の場で float のビットが一致");
                if (height >= 1.0f)
                {
                    Expect(transmittance == 1.0f, "草より上の透過率は 1");
                }
                else
                {
                    const double expectedTau = (128.0 / 255.0) * (1.0 - height) * length;
                    const double actualTau = -std::log(static_cast<double>(transmittance));
                    const double relativeError = std::fabs(actualTau - expectedTau) / expectedTau;
                    if (relativeError > maxRelativeError)
                    {
                        maxRelativeError = relativeError;
                    }
                    if (!(transmittance > 0.0f && transmittance <= 1.0f && relativeError <= 0.02))
                    {
                        ++g_FailureCount;
                        std::cout << "失敗: 遮蔽 y=" << height << " 長さ=" << length
                                  << " 実測厚さ=" << actualTau << " 期待厚さ=" << expectedTau << "\n";
                    }
                }
                ++occlusionCount;
            }
        }

        const float positions[][2] = {{0.25f, 0.25f}, {4.125f, 8.75f},
                                       {16.5f, 17.5f}, {31.75f, 31.75f}, {-1.0f, 8.0f}};
        for (const auto& position : positions)
        {
            const GrassSample sample = field.Sample(position[0], position[1]);
            const GrassSample repeated = field.Sample(position[0], position[1]);
            const GrassSample independent = sameField.Sample(position[0], position[1]);
            Expect(SameSampleBits(sample, repeated) && SameSampleBits(sample, independent),
                   "サンプルの反復と別の場で各 float のビットが一致");
        }
        const GrassSample sample = field.Sample(4.125f, 8.75f);
        Expect(sample.bValid && sample.HeightMeters == 1.0f, "一定の草丈は 1 m");
        Expect(field.QueryTopHeight(4.125f, 8.75f) == 1.0f, "既定地面で草の頂点は 1 m");
        Expect(field.GetCameraClearance(Math::Vector3(4.125f, 0.0f, 8.75f)) == 1.0f,
               "一定の草丈のカメラの持ち上げ量は 1 m");
        const GrassSurface surface = field.QuerySurface(4.125f, 8.75f);
        Expect(surface.bValid && surface.SpeciesId == sample.SpeciesId &&
               FloatBits(surface.MoistureBias) == FloatBits(sample.Moisture),
               "地表の種別と濡れ度はサンプルに一致");

        ConstantGroundHeightSource raisedGround(1.25f);
        field.SetGroundHeightSource(&raisedGround);
        Expect(field.QueryTopHeight(4.125f, 8.75f) == 2.25f, "地面を高くすると草の頂点も高くなる");
        field.SetGroundHeightSource(nullptr);
        Expect(field.QueryTopHeight(4.125f, 8.75f) == 1.0f, "地面を解除すると既定の高さに戻る");
        std::cout << "遮蔽: " << occlusionCount << " 区間、最大相対誤差=" << maxRelativeError << "\n";
        std::cout << "ビット一致: サンプル 5 点、遮蔽 " << occlusionCount << " 区間を反復と別の場で検査\n";
    }
}

int main()
{
    GrassFieldDesc desc;
    Expect(desc.ChunkSizeMeters == 32.0f && desc.LayerAResolution == LayerAResolution &&
           desc.LayerBResolution == LayerBResolution, "既定のチャンク寸法と画素数");
    GrassSpecies species;
    species.HeightMinMeters = 1.0f;
    species.HeightMaxMeters = 1.0f;
    species.Opacity = 1.0f;
    GrassSpeciesTable table;
    if (!table.Add(species))
    {
        std::cout << "失敗: 試験用の草の種別を追加できません\n";
        return 1;
    }

    GrassField linearField(desc);
    GrassField uniformField(desc);
    GrassField sameUniformField(desc);
    linearField.SetSpeciesTable(table);
    uniformField.SetSpeciesTable(table);
    sameUniformField.SetSpeciesTable(table);
    if (!SetLinearChunks(linearField) || !SetUniformChunk(uniformField) || !SetUniformChunk(sameUniformField))
    {
        std::cout << "失敗: 試験用のチャンクを設定できません\n";
        return 1;
    }

    std::cout.precision(9);
    TestLinearDensityAndSeam(linearField);
    TestOutside(linearField);
    TestOcclusionAndDeterminism(uniformField, sameUniformField);
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "GrassFieldQueryTest: すべての検査に合格\n";
    return 0;
}
