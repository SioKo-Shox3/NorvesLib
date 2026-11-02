#pragma once

#include "CoreTypes.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <cstdint>

namespace NorvesLib::Core::Particle
{
    enum class VfxValueType : uint8_t
    {
        Float,
        UInt,
        Bool,
        Vector2,
        Vector3,
        Color,
        Enum,
        Name
    };

    struct VfxValue
    {
        VfxValueType Type = VfxValueType::Float;
        Math::Vector4 Vector = Math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        uint32_t UInt = 0u;
        Container::String Name;

        static VfxValue MakeFloat(float value);
        static VfxValue MakeUInt(uint32_t value);
        static VfxValue MakeBool(bool value);
        static VfxValue MakeVector2(const Math::Vector2& value);
        static VfxValue MakeVector3(const Math::Vector3& value);
        static VfxValue MakeColor(const Math::Vector4& value);
        static VfxValue MakeEnum(uint32_t value);
        static VfxValue MakeName(const char* value);

        bool operator==(const VfxValue& other) const;
    };

    enum class VfxModuleType : uint16_t
    {
        SpawnRate = 1,
        SpawnBurst = 2,
        SpawnOnEvent = 3,
        InitPosition = 10,
        InitVelocityBox = 11,
        InitLifetime = 12,
        InitColor = 13,
        InitSize = 14,
        Acceleration = 20,
        SpriteRenderer = 30
    };

    enum class VfxModuleStage : uint8_t
    {
        Spawn,
        Initialize,
        Update,
        Render,
        Object
    };

    enum class VfxSimulationSpace : uint8_t
    {
        Screen = 0,
        World = 1
    };

    struct VfxModule
    {
        VfxModuleType Type = VfxModuleType::SpawnRate;
        VariableArray<VfxValue> Values;
    };

    struct VfxEmitterAsset
    {
        Container::String Name;
        VariableArray<VfxValue> Properties;
        VariableArray<VfxModule> Modules;
    };

    struct VfxFlipbookImportSettings
    {
        Container::String Name;
        Container::String Texture;
        uint32_t Columns = 1u;
        uint32_t Rows = 1u;
        uint32_t FrameCount = 1u;
        float FramesPerSecond = 0.0f;
        uint32_t StartFrame = 0u;
        bool bLoop = true;
    };

    struct VfxEffectAsset
    {
        Container::String Name;
        VariableArray<VfxEmitterAsset> Emitters;
        VariableArray<VfxFlipbookImportSettings> Flipbooks;
    };
} // namespace NorvesLib::Core::Particle
