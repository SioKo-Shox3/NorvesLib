#pragma once

#include "Container/Span.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedMaterialFormatV1
    {
        inline constexpr size_t RecordSize = 128;
        inline constexpr size_t StringRefSize = 16;
        namespace Offset
        {
            inline constexpr size_t Albedo = 0, Normal = 16, Arm = 32, Emissive = 48;
            inline constexpr size_t BaseColor = 64, EmissiveColor = 80, EmissiveNits = 92;
            inline constexpr size_t Metallic = 96, Roughness = 100, OcclusionStrength = 104, NormalScale = 108;
            inline constexpr size_t AlphaCutoff = 112, Flags = 116, ShadingModelId = 120, Reserved = 124;
        }
        inline constexpr uint32_t DoubleSided = 1;
        inline constexpr uint32_t AlphaModeShift = 1;
        inline constexpr uint32_t AlphaModeMask = 6;
        inline constexpr uint32_t ArmUseAO = 8, ArmUseRoughness = 16, ArmUseMetallic = 32;
        inline constexpr uint32_t ArmUseMask = 56;
        inline constexpr uint32_t KnownFlags = 63;
        // wireの番号。Rendering::ShadingModelの列挙値とは別で、変換時に明示写像する。
        inline constexpr uint32_t DefaultLit = 0;
    }
    enum class CookedMaterialAlphaMode : uint32_t
    {
        Opaque = 0, Mask = 1, Blend = 2
    };
    struct CookedMaterialStringRef
    {
        uint64_t Offset = 0;
        uint32_t Length = 0;
    };
    // これは所有側の値で、C++のpaddingをwireへmemcpyしない。
    // 初期値はtexture未参照の中立値。import時のARM/nits方針を選択する既定ではない。
    struct CookedMaterialRecord
    {
        CookedMaterialStringRef Albedo, Normal, Arm, Emissive;
        float BaseColor[4] = {1,1,1,1};
        float EmissiveColor[3]{};
        float EmissiveNits = 0;
        float Metallic = -1, Roughness = -1;
        float OcclusionStrength = 1, NormalScale = 1, AlphaCutoff = 0.5f;
        uint32_t Flags = 0;
        uint32_t ShadingModelId = CookedMaterialFormatV1::DefaultLit;
    };
    enum class CookedMaterialStatus
    {
        Success, InvalidInput, InvalidReserved, InvalidReference, InvalidNumeric,
        InvalidFlags, UnsupportedShadingModel, InvalidEmissive
    };
    // StringRefのbyte境界だけを検査する。文字列内容/論理path/texture色空間は接続側の責務。
    [[nodiscard]] CookedMaterialStatus ValidateCookedMaterialRecord(const CookedMaterialRecord& record,
        uint64_t stringTableSize) noexcept;
    // readは128Bちょうど、writeは先頭128Bだけを使用する。失敗時はout全体を保持する。
    // 入出力領域の重複は拒否する。入力storageは呼出し中不変でなければならない。
    [[nodiscard]] CookedMaterialStatus ReadCookedMaterialRecord(Container::Span<const uint8_t> bytes,
        uint64_t stringTableSize, CookedMaterialRecord& out) noexcept;
    [[nodiscard]] CookedMaterialStatus WriteCookedMaterialRecord(const CookedMaterialRecord& record,
        uint64_t stringTableSize, Container::Span<uint8_t> out) noexcept;
} // namespace NorvesLib::Core::Asset
