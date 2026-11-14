#include "Rendering/WorldFieldTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float SanitizeScalar(float value, float fallback, float minimum, float maximum)
        {
            return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
        }

        bool HasFiniteCoordinates(const WorldFieldInteractor& interactor)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(interactor.P0[axis]) || !std::isfinite(interactor.P1[axis]) ||
                    !std::isfinite(interactor.PrevP0[axis]) || !std::isfinite(interactor.PrevP1[axis]))
                {
                    return false;
                }
            }
            return true;
        }

        int64_t SnapCenter(double center, double texel, bool& bClamped)
        {
            constexpr int64_t limit = int64_t{1} << 60;
            const double pixel = std::floor(center / texel);
            if (pixel < -static_cast<double>(limit) || pixel > static_cast<double>(limit))
            {
                bClamped = true;
            }
            return static_cast<int64_t>(std::clamp(pixel, -static_cast<double>(limit),
                static_cast<double>(limit)));
        }

        int64_t SaturatingDifference(int64_t current, int64_t previous)
        {
            constexpr int64_t minimum = std::numeric_limits<int64_t>::min();
            constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
            if (previous > 0 && current < minimum + previous)
            {
                return minimum;
            }
            if (previous < 0 && current > maximum + previous)
            {
                return maximum;
            }
            return current - previous;
        }
    } // 無名名前空間

    WorldFieldSampleConstants BuildWorldFieldSampleConstants(const WorldFieldFrameState& state)
    {
        WorldFieldSampleConstants constants;
        const auto isUsable = [](const WorldFieldState& image)
        {
            return image.bValid && std::isfinite(image.OriginXZ[0]) && std::isfinite(image.OriginXZ[1]) &&
                std::isfinite(image.ExtentMeters) && image.ExtentMeters > 0.0f;
        };
        if (!isUsable(state.Current) || !isUsable(state.Previous))
        {
            return constants;
        }
        constants.CurrentOriginXZ[0] = static_cast<float>(state.Current.OriginXZ[0]);
        constants.CurrentOriginXZ[1] = static_cast<float>(state.Current.OriginXZ[1]);
        constants.CurrentInvExtent = 1.0f / state.Current.ExtentMeters;
        constants.PreviousOriginXZ[0] = static_cast<float>(state.Previous.OriginXZ[0]);
        constants.PreviousOriginXZ[1] = static_cast<float>(state.Previous.OriginXZ[1]);
        constants.PreviousInvExtent = 1.0f / state.Previous.ExtentMeters;
        if (!std::isfinite(constants.CurrentOriginXZ[0]) || !std::isfinite(constants.CurrentOriginXZ[1]) ||
            !std::isfinite(constants.PreviousOriginXZ[0]) || !std::isfinite(constants.PreviousOriginXZ[1]) ||
            !std::isfinite(constants.CurrentInvExtent) || !std::isfinite(constants.PreviousInvExtent))
        {
            return {};
        }
        constants.bEnabled = 1u;
        constants.BendScale = WorldFieldDefaultBendScale;
        constants.FlattenScale = WorldFieldDefaultFlattenScale;
        return constants;
    }

    WorldFieldDrynessSampleConstants BuildWorldFieldDrynessSampleConstants(const WorldFieldFrameState& state)
    {
        WorldFieldDrynessSampleConstants constants;
        const auto isUsable = [](const WorldFieldState& image)
        {
            return image.bValid && std::isfinite(image.OriginXZ[0]) && std::isfinite(image.OriginXZ[1]) &&
                std::isfinite(image.ExtentMeters) && image.ExtentMeters > 0.0f;
        };
        if (!isUsable(state.DrynessNear) || !isUsable(state.DrynessFar))
        {
            return constants;
        }
        constants.NearOriginXZ[0] = static_cast<float>(state.DrynessNear.OriginXZ[0]);
        constants.NearOriginXZ[1] = static_cast<float>(state.DrynessNear.OriginXZ[1]);
        constants.NearInvExtent = 1.0f / state.DrynessNear.ExtentMeters;
        constants.FarOriginXZ[0] = static_cast<float>(state.DrynessFar.OriginXZ[0]);
        constants.FarOriginXZ[1] = static_cast<float>(state.DrynessFar.OriginXZ[1]);
        constants.FarInvExtent = 1.0f / state.DrynessFar.ExtentMeters;
        constants.NearBlendMeters = state.DrynessNearBlendMeters;
        if (!std::isfinite(constants.NearOriginXZ[0]) || !std::isfinite(constants.NearOriginXZ[1]) ||
            !std::isfinite(constants.FarOriginXZ[0]) || !std::isfinite(constants.FarOriginXZ[1]) ||
            !std::isfinite(constants.NearInvExtent) || !std::isfinite(constants.FarInvExtent))
        {
            return {};
        }
        constants.bEnabled = 1u;
        return constants;
    }

    void WorldFieldDisplacement(const float field[4], float height, float t, float displacement[3],
        float bendScale, float flattenScale)
    {
        const float h = std::max(height, 0.0f);
        const float s = std::clamp(t, 0.0f, 1.0f);
        const float bend = h * bendScale * s * s;
        displacement[0] = field[0] * bend;
        displacement[1] = -h * s * flattenScale * std::clamp(field[2], 0.0f, 1.0f);
        displacement[2] = field[1] * bend;
    }

    float GetWorldFieldExtentMeters(WorldFieldRange range)
    {
        return range == WorldFieldRange::Range128m ? 128.0f : 64.0f;
    }

    float GetWorldFieldTexelSizeMeters(WorldFieldRange range)
    {
        return GetWorldFieldExtentMeters(range) / WorldFieldResolution;
    }

    WorldFieldSettings SanitizeWorldFieldSettings(const WorldFieldSettings& settings)
    {
        const WorldFieldSettings defaults;
        WorldFieldSettings result = settings;
        if (result.Range != WorldFieldRange::Range64m && result.Range != WorldFieldRange::Range128m)
        {
            result.Range = defaults.Range;
        }
        if (result.CenterMode != WorldFieldCenterMode::Camera &&
            result.CenterMode != WorldFieldCenterMode::CameraForward)
        {
            result.CenterMode = defaults.CenterMode;
        }
        result.ForwardOffsetMeters = SanitizeScalar(result.ForwardOffsetMeters,
            defaults.ForwardOffsetMeters, 0.0f, 100.0f);
        result.DecayTimeConstantSeconds = SanitizeScalar(result.DecayTimeConstantSeconds,
            defaults.DecayTimeConstantSeconds, 0.05f, 60.0f);
        result.DrynessNearTexelMeters = SanitizeScalar(result.DrynessNearTexelMeters,
            defaults.DrynessNearTexelMeters, 1.0f / 64.0f, 4.0f);
        result.DrynessFarTexelMeters = SanitizeScalar(result.DrynessFarTexelMeters,
            defaults.DrynessFarTexelMeters, result.DrynessNearTexelMeters, 64.0f);
        result.DrynessNearBlendMeters = SanitizeScalar(result.DrynessNearBlendMeters,
            defaults.DrynessNearBlendMeters, 0.0f,
            result.DrynessNearTexelMeters * static_cast<float>(WorldFieldDrynessResolution) * 0.5f);
        return result;
    }

    uint32_t SanitizeWorldFieldInteractors(Container::Span<const WorldFieldInteractor> input,
        Container::VariableArray<WorldFieldInteractor>& output)
    {
        // 出力自身を入力にした場合も、clearや再確保で参照を失わないよう先にコピーする。
        WorldFieldInteractor sanitized[WorldFieldMaxInteractors];
        uint32_t count = 0;
        const auto inputCount = std::min(input.size(), static_cast<size_t>(WorldFieldMaxInteractors));
        for (size_t index = 0; index < inputCount; ++index)
        {
            WorldFieldInteractor interactor = input[index];
            if (!HasFiniteCoordinates(interactor) || !std::isfinite(interactor.Radius) ||
                !std::isfinite(interactor.PrevRadius) || !std::isfinite(interactor.Strength))
            {
                continue;
            }
            interactor.Radius = std::clamp(interactor.Radius, 0.0f, 50.0f);
            interactor.PrevRadius = std::clamp(interactor.PrevRadius, 0.0f, 50.0f);
            interactor.Strength = std::clamp(interactor.Strength, 0.0f, 1.0f);
            interactor.ChannelMask &= WorldFieldChannelAll;
            if ((interactor.Radius == 0.0f && interactor.PrevRadius == 0.0f) || interactor.Strength == 0.0f)
            {
                continue;
            }
            sanitized[count++] = interactor;
        }
        output.assign(sanitized, sanitized + count);
        return count;
    }

    uint32_t SanitizeWorldFieldDrynessSources(Container::Span<const WorldFieldDrynessSource> input,
        Container::VariableArray<WorldFieldDrynessSource>& output)
    {
        // 出力自身を入力にした場合も参照を失わないよう、先に固定配列へ詰める。
        WorldFieldDrynessSource sanitized[WorldFieldDrynessMaxSources];
        uint32_t count = 0;
        const auto inputCount = std::min(input.size(), static_cast<size_t>(WorldFieldDrynessMaxSources));
        for (size_t index = 0; index < inputCount; ++index)
        {
            WorldFieldDrynessSource source = input[index];
            if (!std::isfinite(source.CenterXZ[0]) || !std::isfinite(source.CenterXZ[1]) ||
                !std::isfinite(source.RadiusA) || !std::isfinite(source.RadiusB) ||
                !std::isfinite(source.CosYaw) || !std::isfinite(source.SinYaw) ||
                !std::isfinite(source.Falloff) || !std::isfinite(source.Level) ||
                source.RadiusA <= 0.0f || source.RadiusB <= 0.0f || source.Falloff < 0.0f ||
                source.CosYaw * source.CosYaw + source.SinYaw * source.SinYaw < 0.25f)
            {
                continue;
            }
            source.Level = std::clamp(source.Level, 0.0f, 1.0f);
            if (source.Level == 0.0f)
            {
                continue;
            }
            for (uint32_t component = 0; component < 4; ++component)
            {
                source.Reserved0[component] = 0.0f;
                source.Reserved1[component] = 0.0f;
            }
            sanitized[count++] = source;
        }
        output.assign(sanitized, sanitized + count);
        return count;
    }

    WorldFieldScrollResult CalculateWorldFieldScroll(const WorldFieldOrigin& previousOrigin,
        double centerXMeters, double centerZMeters, double texelSizeMeters, uint32_t resolution)
    {
        WorldFieldScrollResult result;
        if (!std::isfinite(centerXMeters) || !std::isfinite(centerZMeters) ||
            !std::isfinite(texelSizeMeters) || texelSizeMeters <= 0.0 || resolution == 0)
        {
            return result;
        }
        bool bClamped = false;
        result.Origin.X = SnapCenter(centerXMeters, texelSizeMeters, bClamped) - resolution / 2;
        result.Origin.Z = SnapCenter(centerZMeters, texelSizeMeters, bClamped) - resolution / 2;
        result.OriginXMeters = static_cast<double>(result.Origin.X) * texelSizeMeters;
        result.OriginZMeters = static_cast<double>(result.Origin.Z) * texelSizeMeters;
        if (!std::isfinite(result.OriginXMeters) || !std::isfinite(result.OriginZMeters))
        {
            return {};
        }
        result.Origin.bValid = true;
        if (previousOrigin.bValid)
        {
            result.ShiftX = SaturatingDifference(result.Origin.X, previousOrigin.X);
            result.ShiftZ = SaturatingDifference(result.Origin.Z, previousOrigin.Z);
        }
        const int64_t extent = resolution;
        // abs(INT64_MIN)を使わず、両方向の比較で全域破棄を決める。
        result.bReset = bClamped || !previousOrigin.bValid ||
            result.ShiftX <= -extent || result.ShiftX >= extent ||
            result.ShiftZ <= -extent || result.ShiftZ >= extent;
        return result;
    }
} // 名前空間 NorvesLib::Core::Rendering
