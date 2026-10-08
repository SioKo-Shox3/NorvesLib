// glTF材質のsource値。まだARM畳み込み・発光単位換算・cooked化を行わない。
#pragma once
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Core
{
    class JsonValue;
}
namespace NorvesLib::Core::Gltf
{
    enum class MaterialTextureRole : uint8_t { BaseColor, Normal, MetallicRoughness, Occlusion, Emissive, Count };
    enum class MaterialAlphaMode : uint8_t { Opaque, Mask, Blend };
    struct MaterialTextureReference
    {
        bool Present = false;
        uint32_t Index = 0;
    };
    struct MaterialSource
    {
        double BaseColor[4] = {1,1,1,1};
        double Metallic = 1, Roughness = 1;
        double EmissiveFactor[3]{};
        double EmissiveStrength = 1;
        double NormalScale = 1, OcclusionStrength = 1, AlphaCutoff = .5;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        bool DoubleSided = false;
        MaterialTextureReference Textures[static_cast<size_t>(MaterialTextureRole::Count)]{};
    };
    enum class MaterialReadStatus : uint8_t
    {
        Success, InvalidType, InvalidValue, DuplicateField, InvalidKey,
        InvalidTextureIndex, UnsupportedTexCoord, UnsupportedTextureExtension, UnsupportedAlphaMode
    };
    struct MaterialReadResult
    {
        MaterialReadStatus Status = MaterialReadStatus::Success;
        const char* Field = "";
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == MaterialReadStatus::Success;
        }
    };
    [[nodiscard]] MaterialReadResult ValidateMaterialSource(const MaterialSource& material, uint32_t textureCount) noexcept;
    [[nodiscard]] const char* MaterialTextureField(MaterialTextureRole role) noexcept;
    // material省略（無効view）はglTF既定。明示nullは拒否。texture参照はtextures配列内で検査する。
    // TEXCOORD_0のみ、textureInfo.extensionsは旧cookerと同じく存在だけで拒否する。
    // optionalな未知材質拡張はglTFのfallback値として無視。extensionsRequiredの判定はdocument側の責務。
    // 成功時だけoutを置換。name/素材selector/画像path・bytesは呼出元で保持する。
    [[nodiscard]] MaterialReadResult ReadMaterialSource(const JsonValue& material,
        uint32_t textureCount, MaterialSource& out);
} // namespace NorvesLib::Core::Gltf
