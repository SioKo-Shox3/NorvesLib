#pragma once

#include "Particle/VfxEffectTypes.h"

namespace NorvesLib::Core::Particle
{
    struct VfxParameterSchema
    {
        const char* Name;
        VfxValueType Type;
        double Min;
        double Max;
        const char* Unit;
        float DefaultVector[4];
        uint32_t DefaultUInt;
        const char* DefaultName;
        const char* const* EnumNames;
        uint32_t EnumCount;
        const char* Description;
    };

    struct VfxObjectSchema
    {
        const char* Name;
        VfxModuleType ModuleType;
        VfxModuleStage Stage;
        const VfxParameterSchema* Parameters;
        uint32_t ParameterCount;
    };

    Container::Span<const VfxObjectSchema> GetVfxModuleSchemas();
    const VfxObjectSchema* FindVfxModuleSchema(VfxModuleType type);
    const VfxObjectSchema* FindVfxModuleSchemaByName(const char* name);
    const VfxObjectSchema& GetVfxEmitterSchema();
    const VfxObjectSchema& GetVfxFlipbookSchema();
    int32_t FindVfxParameterIndex(const VfxObjectSchema& schema, const char* name);

    VfxValue MakeVfxDefaultValue(const VfxParameterSchema& schema);
    bool ValidateVfxValue(const VfxParameterSchema& schema, const VfxValue& value, Container::String* outError);
    VfxModule MakeVfxDefaultModule(VfxModuleType type);
    VfxEmitterAsset MakeVfxDefaultEmitter(const char* name);
    bool ValidateVfxModule(const VfxModule& module, Container::String* outError);
    bool ValidateVfxEmitter(const VfxEmitterAsset& emitter, Container::String* outError);
    bool ValidateVfxFlipbook(const VfxFlipbookImportSettings& flipbook, Container::String* outError);
    bool ValidateVfxEffect(const VfxEffectAsset& effect, Container::String* outError);
    bool SetVfxModuleParameter(VfxModule& module, const char* name, const VfxValue& value, Container::String* outError);
    bool SetVfxEmitterProperty(VfxEmitterAsset& emitter, const char* name, const VfxValue& value, Container::String* outError);
} // namespace NorvesLib::Core::Particle
