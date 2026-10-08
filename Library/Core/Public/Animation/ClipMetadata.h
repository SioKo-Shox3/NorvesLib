#pragma once
#include "Container/VariableArray.h"
#include "Text/IdentityPool.h"
#include <type_traits>
namespace NorvesLib::Core
{
    class JsonValue;
    namespace Animation
    {
        enum class RootMotionMode : uint8_t
        {
            None,
            InPlace,
            Extract
        };
        struct AnimEvent
        {
            Identity Name;
            float Time = 0;
            // 負値はpoint。0以上は閉じた終了時刻を持つwindow。
            float EndTime = -1;
            float MinWeight = 0.3f;
            float Value = 0;
            int32_t IntValue = 0;
        };
        static_assert(std::is_trivially_copyable_v<AnimEvent>);
        struct PhaseMarker
        {
            Identity Name;
            float Time = 0;
        };
        struct ClipLoopRange
        {
            bool bEnabled = false;
            float Start = 0, End = 0;
        };
        struct RootMotionSettings
        {
            RootMotionMode Mode = RootMotionMode::None;
            uint32_t Joint = UINT32_MAX;
            bool bX = true, bZ = true, bYaw = true;
            // 負値は既存root曲線から計算する。0も明示値として許す。
            float NominalSpeed = -1;
        };
        struct ClipMetadata
        {
            Container::VariableArray<AnimEvent> Events;
            Container::VariableArray<PhaseMarker> Markers;
            ClipLoopRange Loop;
            RootMotionSettings Root;
            float GroundOffset = 0;
            size_t AllocatedBytes() const
            {
                return Events.capacity() * sizeof(AnimEvent) + Markers.capacity() * sizeof(PhaseMarker);
            }
        };
        enum class ClipMetadataError : uint8_t
        {
            None,
            InvalidJson,
            InvalidSchema,
            InvalidTime,
            InvalidName,
            InvalidWeight,
            InvalidLoop,
            InvalidRoot,
            FileReadFailed
        };
        struct ClipMetadataReport
        {
            ClipMetadataError Error = ClipMetadataError::None;
            Container::String Detail;
            uint32_t UnknownKeys = 0;
        };
        [[nodiscard]] bool SameClipMetadata(const ClipMetadata&, const ClipMetadata&) noexcept;
        [[nodiscard]] bool ApplyClipMetadataValueOverride(const ClipMetadata&, const JsonValue&, float duration,
                                                          ClipMetadata&, ClipMetadataReport&);
        [[nodiscard]] bool ApplyClipMetadataOverride(const ClipMetadata& base, const Container::String&, float duration,
                                                     ClipMetadata&, ClipMetadataReport&);
        [[nodiscard]] bool RemapClipMetadataTime(const ClipMetadata&, float sourceDuration, double start, double end,
                                                 double timeScale, ClipMetadata&, ClipMetadataReport&);
        [[nodiscard]] bool ValidateClipMetadata(const ClipMetadata&, float duration, ClipMetadataReport&);
        // sidecarとextrasで同じ検証を使う。sidecarだけ未知キーを警告する。
        [[nodiscard]] bool ParseClipMetadata(const Container::String& json, float duration, bool sidecar, ClipMetadata&,
                                             ClipMetadataReport&);
        [[nodiscard]] bool ParseClipMetadataValue(const JsonValue&, float duration, bool sidecar, ClipMetadata&,
                                                  ClipMetadataReport&);
    } // namespace Animation
} // namespace NorvesLib::Core
