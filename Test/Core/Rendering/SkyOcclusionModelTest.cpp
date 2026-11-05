// 空の遮蔽の値設定と遮蔽物の鉛直区間を確かめる。
#include "Rendering/SkyOcclusion.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr float kTolerance = 1.0e-4f;
    constexpr float kQuarterTurnRadians = 0.7853981633974483f;
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

    bool NearlyEqual(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= kTolerance;
    }

    void TestDefaults()
    {
        const SkyOcclusionParameters parameters = MakeDefaultSkyOcclusionParameters();
        Expect(!parameters.bEnabled &&
                   parameters.Strength == 1.0f && parameters.DiffuseExponent == 1.0f &&
                   parameters.SpecularStrength == 1.0f &&
                   parameters.OccluderBounceColorR == 0.25f &&
                   parameters.OccluderBounceColorG == 0.30f &&
                   parameters.OccluderBounceColorB == 0.20f &&
                   parameters.OccluderBounceIntensity == 0.15f &&
                   parameters.ReceiverHeightMeters == 0.25f &&
                   parameters.GroundHeightMeters == 0.0f &&
                   parameters.LevelCount == 3u && parameters.BaseTexelMeters == 0.5f &&
                   parameters.Resolution == 256u && parameters.MaxHorizonMeters == 96.0f &&
                   !parameters.bUseCenterOverride && parameters.CenterOverrideX == 0.0f &&
                   parameters.CenterOverrideZ == 0.0f,
               "空の遮蔽は無効で、全設定が既定値");

        const SkyOccluderProxy proxy = MakeDefaultSkyOccluderProxy();
        bool bReservedZero = true;
        for (std::uint32_t reserved : proxy.Reserved)
        {
            bReservedZero = bReservedZero && reserved == 0u;
        }
        Expect(sizeof(SkyOccluderProxy) == 64u && IsValidSkyOccluderProxy(proxy) &&
                   proxy.CenterX == 0.0f && proxy.CenterY == 0.0f && proxy.CenterZ == 0.0f &&
                   proxy.Yaw == 0.0f && proxy.HalfExtentX == 1.0f &&
                   proxy.HalfExtentY == 1.0f && proxy.HalfExtentZ == 1.0f &&
                   proxy.BaseOffset == 0.0f && proxy.Opacity == 1.0f && proxy.Flags == 0u &&
                   GetSkyOccluderShape(proxy) == SkyOccluderShape::Ellipsoid &&
                   GetSkyOccluderClass(proxy) == SkyOccluderClass::Solid && bReservedZero,
               "既定の遮蔽物は 64 バイトの半寸法 1 m の不透明な楕円体");
    }

    void TestSanitize()
    {
        const SkyOcclusionParameters defaults = MakeDefaultSkyOcclusionParameters();
        SkyOcclusionParameters invalid = defaults;
        invalid.bEnabled = true;
        invalid.Strength = std::numeric_limits<float>::quiet_NaN();
        invalid.DiffuseExponent = -1.0f;
        invalid.SpecularStrength = std::numeric_limits<float>::infinity();
        invalid.OccluderBounceColorR = 1.0e30f;
        invalid.ReceiverHeightMeters = -0.5f;
        invalid.GroundHeightMeters = 1.0e30f;
        invalid.LevelCount = 0u;
        invalid.BaseTexelMeters = 0.0f;
        invalid.Resolution = 300u;
        invalid.MaxHorizonMeters = 1.0e30f;
        invalid.CenterOverrideX = std::numeric_limits<float>::infinity();
        invalid.CenterOverrideZ = std::numeric_limits<float>::quiet_NaN();
        const SkyOcclusionParameters sanitized = SanitizeSkyOcclusionParameters(invalid);
        SkyOcclusionParameters expected = defaults;
        expected.bEnabled = true;
        Expect(AreEqualSkyOcclusionParameters(sanitized, expected),
               "非有限値、負値、正数欄のゼロ、巨大値と非 2 の累乗を欄の既定値へ戻す");

        SkyOcclusionParameters valid = defaults;
        valid.bEnabled = true;
        valid.Strength = 0.0f;
        valid.DiffuseExponent = 8.0f;
        valid.SpecularStrength = 4.0f;
        valid.OccluderBounceColorR = 1.0f;
        valid.OccluderBounceColorG = 0.0f;
        valid.OccluderBounceColorB = 0.7f;
        valid.OccluderBounceIntensity = 2.0f;
        valid.ReceiverHeightMeters = 10.0f;
        valid.GroundHeightMeters = -500.0f;
        valid.LevelCount = 4u;
        valid.BaseTexelMeters = 64.0f;
        valid.Resolution = 1024u;
        valid.MaxHorizonMeters = 4096.0f;
        valid.bUseCenterOverride = true;
        valid.CenterOverrideX = -15.0f;
        valid.CenterOverrideZ = 32.0f;
        Expect(AreEqualSkyOcclusionParameters(SanitizeSkyOcclusionParameters(valid), valid),
               "範囲内の設定と境界値は変えない");

        SkyOccluderProxy proxy = MakeDefaultSkyOccluderProxy();
        SetSkyOccluderShape(proxy, SkyOccluderShape::Box);
        SetSkyOccluderClass(proxy, SkyOccluderClass::Canopy);
        SetSkyOccluderDynamic(proxy, true);
        SetSkyOccluderBlocksRain(proxy, true);
        proxy.Flags |= 1u << 10u;
        proxy.CenterX = std::numeric_limits<float>::infinity();
        proxy.Yaw = std::numeric_limits<float>::quiet_NaN();
        proxy.HalfExtentX = -1.0f;
        proxy.HalfExtentY = 1.0e30f;
        proxy.HalfExtentZ = 0.0f;
        proxy.BaseOffset = 1.0e30f;
        proxy.Opacity = -0.5f;
        proxy.Reserved[5] = 7u;
        const SkyOccluderProxy clean = SanitizeSkyOccluderProxy(proxy);
        Expect(IsValidSkyOccluderProxy(clean) && clean.CenterX == 0.0f && clean.Yaw == 0.0f &&
                   clean.HalfExtentX == 0.01f && clean.HalfExtentY == 1.0e4f &&
                   clean.HalfExtentZ == 0.01f && clean.BaseOffset == 2.0e4f &&
                   clean.Opacity == 0.0f && clean.Flags == SkyOccluderKnownFlags &&
                   clean.Reserved[5] == 0u &&
                   GetSkyOccluderShape(clean) == SkyOccluderShape::Box &&
                   GetSkyOccluderClass(clean) == SkyOccluderClass::Canopy &&
                   IsSkyOccluderDynamic(clean) && DoesSkyOccluderBlockRain(clean),
               "遮蔽物の有限な寸法と遮蔽率を範囲内にし、未定義の印と予約欄を消す");
        proxy = MakeDefaultSkyOccluderProxy();
        proxy.HalfExtentY = std::numeric_limits<float>::quiet_NaN();
        Expect(!IsValidSkyOccluderProxy(SanitizeSkyOccluderProxy(proxy)),
               "非有限の半寸法は無効な遮蔽物として判定する");
    }

    void TestEquality()
    {
        const SkyOcclusionParameters defaults = MakeDefaultSkyOcclusionParameters();
        SkyOcclusionParameters other = defaults;
        Expect(AreEqualSkyOcclusionParameters(defaults, other), "同じ空の遮蔽設定は等しい");
        other.CenterOverrideZ = 1.0f;
        Expect(!AreEqualSkyOcclusionParameters(defaults, other), "1 欄異なる空の遮蔽設定は等しくない");

        const SkyOccluderProxy proxy = MakeDefaultSkyOccluderProxy();
        SkyOccluderProxy otherProxy = proxy;
        Expect(AreEqualSkyOccluderProxy(proxy, otherProxy), "同じ遮蔽物は等しい");
        otherProxy.Reserved[5] = 1u;
        Expect(!AreEqualSkyOccluderProxy(proxy, otherProxy), "予約欄が異なる遮蔽物は等しくない");
    }

    void TestColumns()
    {
        SkyOccluderProxy proxy = MakeDefaultSkyOccluderProxy();
        proxy.CenterX = 5.0f;
        proxy.CenterY = 10.0f;
        proxy.CenterZ = -4.0f;
        proxy.HalfExtentX = 2.0f;
        proxy.HalfExtentY = 3.0f;
        proxy.HalfExtentZ = 1.0f;
        float bottom = 0.0f;
        float top = 0.0f;
        Expect(EvaluateSkyOccluderColumn(proxy, 5.0f, -4.0f, bottom, top) &&
                   NearlyEqual(bottom, 7.0f) && NearlyEqual(top, 13.0f),
               "回転しない楕円体の中心の底と頂は解析値に一致");
        Expect(!EvaluateSkyOccluderColumn(proxy, 7.1f, -4.0f, bottom, top),
               "楕円体の縁の外は交差しない");

        proxy.Yaw = kQuarterTurnRadians;
        const float expectedRadius = 3.0f * std::sqrt(1.0f - 0.25f - 0.0625f);
        Expect(EvaluateSkyOccluderColumn(proxy,
                   5.0f + 1.25f * kSqrtHalf, -4.0f - 0.75f * kSqrtHalf, bottom, top) &&
                   NearlyEqual(bottom, 10.0f - expectedRadius) &&
                   NearlyEqual(top, 10.0f + expectedRadius),
               "回転した楕円体の底と頂は解析値に一致");
        proxy.BaseOffset = 2.0f;
        Expect(EvaluateSkyOccluderColumn(proxy, 5.0f, -4.0f, bottom, top) &&
                   NearlyEqual(bottom, 9.0f) && NearlyEqual(top, 13.0f),
               "底面オフセットで楕円体の底を平らに切る");
        proxy.BaseOffset = 6.0f;
        Expect(!EvaluateSkyOccluderColumn(proxy,
                   5.0f + 1.25f * kSqrtHalf, -4.0f - 0.75f * kSqrtHalf, bottom, top),
               "切った底が頂を超えると交差しない");

        proxy.BaseOffset = 0.0f;
        proxy.Yaw = 0.0f;
        SetSkyOccluderShape(proxy, SkyOccluderShape::Box);
        Expect(EvaluateSkyOccluderColumn(proxy, 6.5f, -3.5f, bottom, top) &&
                   NearlyEqual(bottom, 7.0f) && NearlyEqual(top, 13.0f),
               "回転しない箱の内側の底と頂は解析値に一致");
        Expect(!EvaluateSkyOccluderColumn(proxy, 5.0f, -2.9f, bottom, top),
               "箱の外側は交差しない");
        proxy.Yaw = kQuarterTurnRadians;
        Expect(EvaluateSkyOccluderColumn(proxy,
                   5.0f + 2.8f * kSqrtHalf, -4.0f - 1.0f * kSqrtHalf, bottom, top) &&
                   NearlyEqual(bottom, 7.0f) && NearlyEqual(top, 13.0f),
               "回転した箱の角に近い点の底と頂は解析値に一致");
        proxy.BaseOffset = 2.0f;
        Expect(EvaluateSkyOccluderColumn(proxy, 5.0f, -4.0f, bottom, top) &&
                   NearlyEqual(bottom, 9.0f) && NearlyEqual(top, 13.0f),
               "底面オフセットで箱の底を平らに切る");
    }
} // namespace

int main()
{
    TestDefaults();
    TestSanitize();
    TestEquality();
    TestColumns();
    if (g_FailureCount != 0)
    {
        std::printf("SkyOcclusionModelTest: %d 件失敗\n", g_FailureCount);
        return 1;
    }
    std::printf("SkyOcclusionModelTest: 成功\n");
    return 0;
}
