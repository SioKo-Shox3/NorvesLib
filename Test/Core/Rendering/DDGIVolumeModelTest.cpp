#include "Rendering/DDGIVolume.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;

static_assert(std::is_trivially_copyable_v<DDGIVolumeParameters>);
static_assert(std::is_standard_layout_v<DDGIVolumeParameters>);

namespace
{
    int g_FailureCount = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    bool NearlyEqual(float lhs, float rhs, float epsilon)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool IsFiniteVector(const NorvesLib::Math::Vector3& value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    void ExpectDefaultDisabled(const DDGIVolumeParameters& parameters)
    {
        const DDGIVolumeParameters defaults = MakeDefaultDDGIVolumeParameters();
        Expect(!parameters.bEnabled, "無効fallbackはvolumeを無効にする");
        Expect(parameters.Origin == defaults.Origin &&
                   parameters.ProbeSpacing == defaults.ProbeSpacing &&
                   parameters.ProbeCountX == defaults.ProbeCountX &&
                   parameters.ProbeCountY == defaults.ProbeCountY &&
                   parameters.ProbeCountZ == defaults.ProbeCountZ,
               "無効fallbackは有限な既定値へ戻る");
    }

    DDGIVolumeParameters MakeActiveVolume()
    {
        DDGIVolumeParameters parameters = MakeDefaultDDGIVolumeParameters();
        parameters.bEnabled = true;
        return parameters;
    }

    void TestDefaultAndFiniteVolumeValidation()
    {
        const DDGIVolumeParameters defaults = MakeDefaultDDGIVolumeParameters();
        Expect(!defaults.bEnabled, "既定volumeは無効");
        Expect(!IsDDGIVolumeValid(defaults), "既定volumeは有効なvolumeではない");
        Expect(IsFiniteVector(defaults.Origin) && IsFiniteVector(defaults.ProbeSpacing),
               "既定位置とspacingは有限");
        Expect(defaults.ProbeSpacing.x > 0.0f &&
                   defaults.ProbeSpacing.y > 0.0f &&
                   defaults.ProbeSpacing.z > 0.0f,
               "既定spacingは正数");
        Expect(GetDDGIProbeCount(defaults) == 0u,
               "無効volumeのprobe数は0");

        DDGIVolumeParameters valid = MakeActiveVolume();
        Expect(IsDDGIVolumeValid(valid), "有限な1 probe volumeは有効");
        Expect(GetDDGIProbeCount(valid) == 1u, "1 probe volumeの総数は1");

        DDGIVolumeParameters invalid = valid;
        invalid.Origin.x = std::numeric_limits<float>::quiet_NaN();
        Expect(!IsDDGIVolumeValid(invalid), "NaN位置を拒否");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(invalid));

        invalid = valid;
        invalid.ProbeSpacing.y = std::numeric_limits<float>::infinity();
        Expect(!IsDDGIVolumeValid(invalid), "無限spacingを拒否");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(invalid));

        invalid = valid;
        invalid.ProbeSpacing.z = 0.0f;
        Expect(!IsDDGIVolumeValid(invalid), "ゼロspacingを拒否");

        invalid.ProbeSpacing.z = -1.0f;
        Expect(!IsDDGIVolumeValid(invalid), "負spacingを拒否");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(invalid));

        invalid = valid;
        invalid.ProbeCountX = 2u;
        invalid.Origin.x = std::numeric_limits<float>::max();
        invalid.ProbeSpacing.x = std::numeric_limits<float>::max();
        Expect(!IsDDGIVolumeValid(invalid), "格子終端の浮動小数点overflowを拒否");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(invalid));

        DDGIVolumeParameters disabled = valid;
        disabled.bEnabled = false;
        Expect(!IsDDGIVolumeValid(disabled), "無効化された設定は有効volumeではない");
        Expect(SanitizeDDGIVolumeParameters(disabled).ProbeSpacing == disabled.ProbeSpacing,
               "有限な無効設定の格子値は保持");
    }

    void TestCheckedGridIndexing()
    {
        DDGIVolumeParameters parameters = MakeActiveVolume();
        parameters.ProbeCountX = 4u;
        parameters.ProbeCountY = 2u;
        parameters.ProbeCountZ = 2u;
        parameters.ProbeSpacing = NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f);
        Expect(IsDDGIVolumeValid(parameters), "16 probe格子は有効");
        Expect(GetDDGIProbeCount(parameters) == 16u, "格子総数を計算");

        std::uint32_t index = 99u;
        Expect(TryGetDDGIProbeIndex(parameters, 3u, 1u, 1u, index) && index == 15u,
               "X最速row-majorの最終座標を変換");
        Expect(TryGetDDGIProbeIndex(parameters, 1u, 1u, 1u, index) && index == 13u,
               "X最速row-majorの内部座標を変換");

        std::uint32_t x = 99u;
        std::uint32_t y = 99u;
        std::uint32_t z = 99u;
        Expect(TryGetDDGIProbeCoordinates(parameters, 13u, x, y, z) &&
                   x == 1u && y == 1u && z == 1u,
               "probe indexから格子座標へ戻す");

        for (std::uint32_t expectedIndex = 0u; expectedIndex < 16u; ++expectedIndex)
        {
            std::uint32_t roundTripX = 0u;
            std::uint32_t roundTripY = 0u;
            std::uint32_t roundTripZ = 0u;
            std::uint32_t roundTripIndex = 0u;
            const bool bCoordinatesValid = TryGetDDGIProbeCoordinates(
                parameters, expectedIndex, roundTripX, roundTripY, roundTripZ);
            const bool bIndexValid = TryGetDDGIProbeIndex(
                parameters, roundTripX, roundTripY, roundTripZ, roundTripIndex);
            Expect(bCoordinatesValid && bIndexValid && roundTripIndex == expectedIndex,
                   "16 probe格子の全indexが座標往復する");
        }

        index = 99u;
        Expect(!TryGetDDGIProbeIndex(parameters, 4u, 0u, 0u, index) && index == 0u,
               "格子外座標を拒否し安全値を返す");
        x = 99u;
        y = 99u;
        z = 99u;
        Expect(!TryGetDDGIProbeCoordinates(parameters, 16u, x, y, z) &&
                   x == 0u && y == 0u && z == 0u,
               "総数以上のindexを拒否し安全座標を返す");

        parameters.ProbeCountX = DDGIMaxProbeCount;
        parameters.ProbeCountY = 1u;
        parameters.ProbeCountZ = 1u;
        Expect(IsDDGIVolumeValid(parameters), "上限1024 probeを受理");
        Expect(GetDDGIProbeCount(parameters) == DDGIMaxProbeCount,
               "上限volumeの総数は1024");
        Expect(TryGetDDGIProbeIndex(parameters, DDGIMaxProbeCount - 1u, 0u, 0u, index) &&
                   index == DDGIMaxProbeCount - 1u,
               "上限volumeの最終probeをindex化");

        parameters.ProbeCountX = DDGIMaxProbeCount;
        parameters.ProbeCountY = 2u;
        Expect(!IsDDGIVolumeValid(parameters), "1024を超えるprobe総数を拒否");
        Expect(GetDDGIProbeCount(parameters) == 0u,
               "上限超過volumeのprobe数は0");

        parameters.ProbeCountX = std::numeric_limits<std::uint32_t>::max();
        parameters.ProbeCountY = std::numeric_limits<std::uint32_t>::max();
        parameters.ProbeCountZ = std::numeric_limits<std::uint32_t>::max();
        Expect(!IsDDGIVolumeValid(parameters), "積がoverflowし得る格子寸法を拒否");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(parameters));

        parameters = MakeActiveVolume();
        parameters.ProbeCountX = 1u;
        parameters.ProbeCountY = 1u;
        parameters.ProbeCountZ = 1024u;
        Expect(IsDDGIVolumeValid(parameters) && GetDDGIProbeCount(parameters) == 1024u,
               "Z次元を含む総数1024 probeを受理");
        Expect(TryGetDDGIProbeIndex(parameters, 0u, 0u, 1023u, index) && index == 1023u,
               "Z次元1024 probeの最終indexを受理");

        parameters.ProbeCountZ = 1025u;
        Expect(!IsDDGIVolumeValid(parameters), "総数1025 probeを拒否");
        Expect(GetDDGIProbeCount(parameters) == 0u, "総数超過volumeのprobe数は0");
        index = 99u;
        Expect(!TryGetDDGIProbeIndex(parameters, 0u, 0u, 1024u, index) && index == 0u,
               "総数超過volumeのindex要求をfallback");
        ExpectDefaultDisabled(SanitizeDDGIVolumeParameters(parameters));

        parameters = MakeActiveVolume();
        parameters.ProbeCountY = 0u;
        Expect(!IsDDGIVolumeValid(parameters), "0 probeの次元を拒否");
        Expect(GetDDGIProbeCount(parameters) == 0u, "0 probe次元の総数は0");
    }

    void ExpectOctahedralRoundTrip(
        const NorvesLib::Math::Vector3& input,
        const NorvesLib::Math::Vector2& expectedUv,
        const char* message)
    {
        NorvesLib::Math::Vector2 uv(-1.0f, -1.0f);
        Expect(EncodeDDGIOctahedralDirection(input, uv), message);
        Expect(NearlyEqual(uv.x, expectedUv.x, 1.0e-6f) &&
                   NearlyEqual(uv.y, expectedUv.y, 1.0e-6f),
               "octahedral UVが既定座標規約と一致");

        NorvesLib::Math::Vector3 decoded;
        Expect(DecodeDDGIOctahedralDirection(uv, decoded), "有効UVを方向へ戻す");
        const NorvesLib::Math::Vector3 normalized = input / input.Length();
        Expect((decoded - normalized).Length() <= 2.0e-5f,
               "octahedral方向のencode/decode往復が一致");
    }

    void TestOctahedralDirectionMappingAndFallbacks()
    {
        ExpectOctahedralRoundTrip(NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f),
                                  NorvesLib::Math::Vector2(0.5f, 0.5f),
                                  "+Zはoctahedral UV中心");
        ExpectOctahedralRoundTrip(NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f),
                                  NorvesLib::Math::Vector2(1.0f, 0.5f),
                                  "+Xのoctahedral写像");
        ExpectOctahedralRoundTrip(NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f),
                                  NorvesLib::Math::Vector2(0.5f, 0.0f),
                                  "-Yのoctahedral写像");
        ExpectOctahedralRoundTrip(NorvesLib::Math::Vector3(0.0f, 0.0f, -1.0f),
                                  NorvesLib::Math::Vector2(1.0f, 1.0f),
                                  "-Zはoctahedral境界へ折り畳む");
        ExpectOctahedralRoundTrip(NorvesLib::Math::Vector3(1.0f, -2.0f, -3.0f),
                                  NorvesLib::Math::Vector2(5.0f / 6.0f, 1.0f / 12.0f),
                                  "負Z半球のoctahedral折り畳み");

        NorvesLib::Math::Vector2 uv(-1.0f, -1.0f);
        Expect(!EncodeDDGIOctahedralDirection(NorvesLib::Math::Vector3::Zero, uv) &&
                   uv == NorvesLib::Math::Vector2(0.5f, 0.5f),
               "ゼロ方向はUV中心へfallback");
        Expect(!EncodeDDGIOctahedralDirection(
                   NorvesLib::Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f),
                   uv) && uv == NorvesLib::Math::Vector2(0.5f, 0.5f),
               "非有限方向はUV中心へfallback");
        Expect(EncodeDDGIOctahedralDirection(
                   NorvesLib::Math::Vector3(std::numeric_limits<float>::max(),
                                            -std::numeric_limits<float>::max(),
                                            0.0f),
                   uv),
               "有限な最大float方向をoverflowせず写像");

        NorvesLib::Math::Vector3 decoded(1.0f, 1.0f, 1.0f);
        Expect(!DecodeDDGIOctahedralDirection(NorvesLib::Math::Vector2(-0.01f, 0.5f), decoded) &&
                   decoded == NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f),
               "範囲外UVは+Zへfallback");
        Expect(!DecodeDDGIOctahedralDirection(
                   NorvesLib::Math::Vector2(0.5f, std::numeric_limits<float>::infinity()), decoded) &&
                   decoded == NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f),
               "非有限UVは+Zへfallback");
    }

    void TestRayDirectionSequenceAndFallback()
    {
        constexpr float kExpectedCosGoldenAngle = -0.7373689f;
        constexpr float kExpectedSinGoldenAngle = 0.6754903f;
        NorvesLib::Math::Vector3 first;
        NorvesLib::Math::Vector3 second;
        Expect(TryGetDDGIProbeRayDirection(0u, first), "ray列の先頭方向を取得");
        Expect(TryGetDDGIProbeRayDirection(1u, second), "ray列の2番目方向を取得");
        Expect(NearlyEqual(first.x, 0.1760848f, 2.0e-6f) &&
                   NearlyEqual(first.y, 0.0f, 1.0e-6f) &&
                   NearlyEqual(first.z, 0.984375f, 1.0e-6f),
               "ray列先頭の球面Fibonacci値を固定");
        Expect(NearlyEqual(second.x, -0.2231107f, 2.0e-6f) &&
                   NearlyEqual(second.y, 0.2043877f, 2.0e-6f) &&
                   NearlyEqual(second.z, 0.953125f, 1.0e-6f),
               "ray列の2番目の球面Fibonacci値を固定");

        NorvesLib::Math::Vector3 previous;
        for (std::uint32_t index = 0u; index < DDGIProbeRayDirectionCount; ++index)
        {
            NorvesLib::Math::Vector3 direction;
            Expect(TryGetDDGIProbeRayDirection(index, direction), "64本のray方向をすべて取得");
            Expect(IsFiniteVector(direction) && NearlyEqual(direction.Length(), 1.0f, 2.0e-6f),
                   "ray方向は有限な単位ベクトル");
            const float expectedZ = 1.0f - 2.0f *
                ((static_cast<float>(index) + 0.5f) /
                 static_cast<float>(DDGIProbeRayDirectionCount));
            Expect(NearlyEqual(direction.z, expectedZ, 1.0e-6f),
                   "ray列のZ層は等間隔");
            if (index > 0u)
            {
                Expect(direction.z < previous.z, "ray列のZ層は重複しない");
                const float previousRadius =
                    std::sqrt(1.0f - previous.z * previous.z);
                const float currentRadius =
                    std::sqrt(1.0f - direction.z * direction.z);
                const float previousX = previous.x / previousRadius;
                const float previousY = previous.y / previousRadius;
                const float expectedX = previousX * kExpectedCosGoldenAngle -
                    previousY * kExpectedSinGoldenAngle;
                const float expectedY = previousX * kExpectedSinGoldenAngle +
                    previousY * kExpectedCosGoldenAngle;
                Expect(NearlyEqual(direction.x / currentRadius, expectedX, 1.0e-4f) &&
                           NearlyEqual(direction.y / currentRadius, expectedY, 1.0e-4f),
                       "連続rayの方位差はgolden angle");
            }
            previous = direction;
        }
        Expect(NearlyEqual(previous.z, -0.984375f, 1.0e-6f),
               "ray列末尾は南極側の層に達する");

        NorvesLib::Math::Vector3 fallback(1.0f, 1.0f, 1.0f);
        Expect(!TryGetDDGIProbeRayDirection(DDGIProbeRayDirectionCount, fallback) &&
                   fallback == NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f),
               "範囲外ray indexは+Zへfallback");
    }
} // namespace

int main()
{
    TestDefaultAndFiniteVolumeValidation();
    TestCheckedGridIndexing();
    TestOctahedralDirectionMappingAndFallbacks();
    TestRayDirectionSequenceAndFallback();

    if (g_FailureCount != 0)
    {
        std::cout << "DDGIVolumeModelTest: " << g_FailureCount << " 件失敗\n";
        return 1;
    }

    std::cout << "DDGIVolumeModelTest: PASS\n";
    return 0;
}
