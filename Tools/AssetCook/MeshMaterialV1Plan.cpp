#include "MeshMaterialV1Plan.h"
#include "MaterialImport.h"
#include "TextureCooker.h"
#include "Resource/GltfMaterialSource.h"
#include "Resource/GltfImageSource.h"
#include "Resource/GltfBufferFile.h"
#include "Resource/ImportSettingsHash.h"
#include "Asset/AssetPackageFormat.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace A = Core::Asset;
        namespace I = Core::AssetImport;
        namespace G = Core::Gltf;
        using Text = Core::Container::AnsiString;
        using View = Core::Container::AnsiStringView;
        using Bytes = Core::Container::VariableArray<uint8_t>;
        using ByteView = Core::Container::Span<const uint8_t>;
        template <class T> using Array = Core::Container::VariableArray<T>;
        struct Hash
        {
            uint64_t Value = 14695981039346656037ull;
            void Byte(uint8_t value)
            {
                Value = (Value ^ value) * 1099511628211ull;
            }
            void Integer(uint64_t value)
            {
                for (unsigned i = 0; i < 8; ++i)
                {
                    Byte(static_cast<uint8_t>(value >> (8 * i)));
                }
            }
            void Data(ByteView value)
            {
                Integer(value.size());
                for (uint8_t byte : value)
                {
                    Byte(byte);
                }
            }
            void String(View value)
            {
                Data({reinterpret_cast<const uint8_t*>(value.data()), value.size()});
            }
        };
        Text Number(uint64_t value)
        {
            char bytes[32];
            const auto end = std::to_chars(bytes, bytes + sizeof(bytes), value);
            Text out;
            out.append(bytes, static_cast<size_t>(end.ptr - bytes));
            return out;
        }
        void Name(Text& out, ByteView name)
        {
            out.push_back('"');
            constexpr char hex[] = "0123456789abcdef";
            for (uint8_t byte : name)
            {
                if (byte < 32)
                {
                    out.append("\\u00");
                    out.push_back(hex[byte >> 4]);
                    out.push_back(hex[byte & 15]);
                }
                else
                {
                    if (byte == '"' || byte == '\\')
                    {
                        out.push_back('\\');
                    }
                    out.push_back(static_cast<char>(byte));
                }
            }
            out.push_back('"');
        }
        bool Fail(Text& error, View asset, bool bHasMaterial, uint32_t index, ByteView name, const char* reason)
        {
            error = "mesh_material_v1: asset=";
            error.append(asset);
            error.append(" material=");
            if (bHasMaterial)
            {
                Name(error, name);
                error.append(" index=");
                error.append(Number(index));
            }
            else
            {
                error.append("implicit_default");
            }
            error.append(" setting=");
            error.append(reason);
            return false;
        }
        bool UInt(const Core::JsonValue& object, const char* key, uint32_t& out)
        {
            if (!object.IsObject())
            {
                return false;
            }
            size_t found = 0;
            Core::JsonValue value;
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto name = object.GetMemberName(i);
                bool bEqual = name.size() == std::strlen(key);
                for (size_t j = 0; bEqual && j < name.size(); ++j)
                {
                    bEqual = name[j] == key[j];
                }
                if (bEqual)
                {
                    ++found;
                    value = object.GetMemberValue(i);
                }
            }
            if (found != 1 || !value.IsIntegerLiteral() || value.AsNumber() < 0 || value.AsNumber() >= UINT32_MAX)
            {
                return false;
            }
            out = static_cast<uint32_t>(value.AsNumber());
            return true;
        }
        struct Image
        {
            MeshEmbeddedImage Encoded;
            DecodedTextureRgba8 Decoded;
            G::DataUriMime Mime = G::DataUriMime::Unknown;
            bool bDecoded = false, bEmit = false;
            uint8_t UsedRoles = 0;
        };
        struct Images
        {
            const Core::JsonValue& Root;
            const G::BufferSet& Buffers;
            const std::filesystem::path& SourcePath;
            View LogicalPath;
            Array<Image> Cache;
            Text& Error;
            Images(const Core::JsonValue& root, const G::BufferSet& buffers, const std::filesystem::path& source,
                   View logical, Text& error)
                : Root(root), Buffers(buffers), SourcePath(source), LogicalPath(logical), Error(error)
            {
                // 選択材質の5role以下。返したpixel viewがCache移動で無効にならないよう先に確保する。
                Cache.reserve(static_cast<size_t>(G::MaterialTextureRole::Count));
            }
            Image* Get(const G::MaterialTextureReference& reference)
            {
                const auto textures = Root.FindMember("textures");
                uint32_t index = 0;
                if (!reference.Present || !textures.IsArray() || reference.Index >= textures.GetArraySize() ||
                    !UInt(textures.GetArrayElement(reference.Index), "source", index))
                {
                    Error = "texture_source";
                    return nullptr;
                }
                for (auto& item : Cache)
                {
                    if (item.Encoded.ImageIndex == index)
                    {
                        return &item;
                    }
                }
                if (Cache.size() == static_cast<size_t>(G::MaterialTextureRole::Count))
                {
                    Error = "image_role_limit";
                    return nullptr;
                }
                G::ImageSource source;
                if (G::ImageSource::Resolve(Root, index, Buffers, source) != G::ImageSourceResult::Success)
                {
                    Error = "image_source";
                    return nullptr;
                }
                Bytes external;
                ByteView bytes;
                bool bBorrow = false;
                if (source.GetKind() == G::ImageSourceKind::ExternalFile)
                {
                    G::BufferFileContext context{SourcePath};
                    if (SourcePath.empty() || G::ReadBufferFile(source.GetExternalUri(), external, &context) !=
                                                  G::ExternalBufferReadResult::Success)
                    {
                        Error = "external_image_read";
                        return nullptr;
                    }
                    bytes = external;
                }
                else
                {
                    bytes = source.GetBytes(Buffers);
                    bBorrow = source.GetKind() == G::ImageSourceKind::BufferView &&
                              Buffers.GetSourceKind(source.GetBufferIndex()) == G::BufferStorageKind::GlbBin;
                    if (!G::MatchesEmbeddedImageMime(bytes, source.GetMime()))
                    {
                        Error = "image_mime";
                        return nullptr;
                    }
                }
                const auto mime = G::ProbeEmbeddedImageMime(bytes);
                if (mime != G::DataUriMime::Png && mime != G::DataUriMime::Jpeg)
                {
                    Error = "PNG_or_JPEG_required";
                    return nullptr;
                }
                Image image;
                image.Encoded.ImageIndex = index;
                image.Mime = mime;
                if (!image.Encoded.SetBytes(bytes, bBorrow))
                {
                    Error = "image_storage";
                    return nullptr;
                }
                image.Encoded.SourceHash = A::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
                Cache.push_back(std::move(image));
                return &Cache.back();
            }
            bool Emit(const G::MaterialTextureReference& reference, MeshImageRole role, bool bSrgb, Text& path)
            {
                if (!reference.Present)
                {
                    return true;
                }
                auto* item = Get(reference);
                if (!item)
                {
                    return false;
                }
                const char* format = bSrgb ? "nvtex.v0.rgba8.srgb" : "nvtex.v0.rgba8.linear";
                if (item->bEmit && item->Encoded.Format != format)
                {
                    Error = "image_role_color_space_conflict";
                    return false;
                }
                item->bEmit = true;
                item->UsedRoles |= role == MeshImageRole::Albedo ? 1u : role == MeshImageRole::Normal ? 2u : 16u;
                item->Encoded.Roles |= static_cast<uint8_t>(role);
                item->Encoded.Format = format;
                item->Encoded.LogicalPath = Text(LogicalPath) + ".img" + Number(item->Encoded.ImageIndex) +
                                            (item->Mime == G::DataUriMime::Png ? ".png" : ".jpg");
                path = item->Encoded.LogicalPath;
                return true;
            }
            bool Arm(const G::MaterialTextureReference& reference, G::MaterialTextureRole role, ArmImageView& view)
            {
                if (!reference.Present)
                {
                    return true;
                }
                auto* image = Get(reference);
                if (!image)
                {
                    return false;
                }
                image->UsedRoles |= static_cast<uint8_t>(1u << static_cast<unsigned>(role));
                if (!image->bDecoded && !DecodeTextureRgba8(image->Encoded.GetBytes(), image->Decoded, Error))
                {
                    return false;
                }
                image->bDecoded = true;
                view = {true, image->Decoded.Width, image->Decoded.Height, image->Decoded.Pixels};
                return true;
            }
        };
        bool UsesImage(const I::ArmChannelPolicy& policy)
        {
            return policy.Mode == I::ArmMode::Texture || policy.Mode == I::ArmMode::Auto;
        }
        bool AppendSettings(Hash& hash, const MeshMaterialV1Plan& plan, bool bHasMaterial, uint32_t materialIndex)
        {
            hash.String("NorvesLib.NVMESH.material.v1");
            hash.Integer(1);
            hash.Integer(GeometryClosureAlgorithmRevision);
            hash.Integer(std::bit_cast<uint64_t>(plan.Closure.MaximumBoundaryFraction));
            hash.Integer(bHasMaterial);
            hash.Integer(bHasMaterial ? materialIndex : UINT32_MAX);
            auto appended =
                I::AppendImportSettingsHash(hash.Value, plan.Import.bPresent, plan.Import.Settings.Geometry);
            if (!appended.bValid)
            {
                return false;
            }
            hash.Value = appended.Value;
            const auto implicit = I::AppendMaterialSettingsHash(hash.Value, plan.Resolved.ImplicitDefault);
            if (!implicit.Valid)
            {
                return false;
            }
            hash.Value = implicit.Value;
            hash.Integer(plan.Resolved.Materials.size());
            for (const auto& material : plan.Resolved.Materials)
            {
                hash.Integer(material.SourceIndex);
                const auto value = I::AppendMaterialSettingsHash(hash.Value, material.Settings);
                if (!value.Valid)
                {
                    return false;
                }
                hash.Value = value.Value;
            }
            return true;
        }
    } // namespace
    bool PrepareMeshMaterialV1(const Core::JsonValue& root, const G::BufferSet& buffers,
                               const std::filesystem::path& sourcePath, View logicalPath, bool bHasMaterial,
                               uint32_t materialIndex, uint64_t gltfSourceHash,
                               const I::ImportSettingsFileOptions* options, MeshMaterialV1Plan& out, Text& error)
    {
        error.clear();
        MeshMaterialV1Plan plan;
        I::SourceMaterialCatalog catalog;
        if (I::ReadSourceMaterialCatalog(root, catalog) != I::SettingsResult::Success ||
            (bHasMaterial && materialIndex >= catalog.size()))
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, {}, "materials");
        }
        const ByteView materialName = bHasMaterial ? ByteView(catalog[materialIndex].Name) : ByteView{};
        const I::ImportSettingsFileOptions automatic;
        const auto& effective = options ? *options : automatic;
        if (!sourcePath.empty() || effective.bRequired || !effective.OverridePath.empty())
        {
            const auto loaded = I::LoadImportSettingsDocument(sourcePath, effective, plan.Import);
            if (loaded.Result != I::SettingsFileResult::Success)
            {
                return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "import_settings");
            }
        }
        const auto resolved = I::ResolveMaterialImportPlan(catalog, plan.Import.Settings, {}, plan.Resolved);
        if (!resolved.Succeeded())
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "material_selector_or_settings");
        }
        for (const auto& entry : plan.Resolved.Materials)
        {
            if (entry.bSurfacePresent)
            {
                return Fail(error, logicalPath, true, entry.SourceIndex, entry.Name, "surface_requires_GR81");
            }
        }
        const auto& settings =
            bHasMaterial ? plan.Resolved.Materials[materialIndex].Settings : plan.Resolved.ImplicitDefault;
        plan.Sidedness = settings.DoubleSided;
        const auto textures = root.FindMember("textures");
        if ((textures.IsValid() && !textures.IsArray()) || textures.GetArraySize() >= UINT32_MAX)
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "textures");
        }
        G::MaterialSource source;
        const auto material =
            bHasMaterial ? root.FindMember("materials").GetArrayElement(materialIndex) : Core::JsonValue{};
        const auto read = G::ReadMaterialSource(material, static_cast<uint32_t>(textures.GetArraySize()), source);
        if (!read.Succeeded())
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, read.Field);
        }
        auto& record = plan.Material;
        for (size_t i = 0; i < 4; ++i)
        {
            record.BaseColor[i] = static_cast<float>(source.BaseColor[i]);
        }
        record.NormalScale = static_cast<float>(source.NormalScale);
        record.AlphaCutoff = static_cast<float>(source.AlphaCutoff);
        I::ImportedEmission emission;
        const auto emitted =
            I::ImportEmission(source.EmissiveFactor, source.EmissiveStrength, settings.Emission, emission);
        if (emitted != I::MaterialPolicyStatus::Success)
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "emissiveNitsPerUnit");
        }
        for (size_t i = 0; i < 3; ++i)
        {
            record.EmissiveColor[i] = emission.Color[i];
        }
        record.EmissiveNits = emission.Nits;
        const auto alpha =
            settings.AlphaMode == I::AlphaModeSetting::ForceOpaque ? G::MaterialAlphaMode::Opaque : source.AlphaMode;
        switch (alpha)
        {
        case G::MaterialAlphaMode::Opaque:
            break;
        case G::MaterialAlphaMode::Mask:
            record.Flags |= 1u << A::CookedMaterialFormatV1::AlphaModeShift;
            break;
        case G::MaterialAlphaMode::Blend:
            record.Flags |= 2u << A::CookedMaterialFormatV1::AlphaModeShift;
            break;
        }
        if (plan.Sidedness == I::DoubleSidedSetting::ForceTrue)
        {
            record.Flags |= A::CookedMaterialFormatV1::DoubleSided;
        }
        else if (plan.Sidedness != I::DoubleSidedSetting::ForceFalse && plan.Sidedness != I::DoubleSidedSetting::Auto)
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "doubleSided");
        }
        const auto texture = [&](G::MaterialTextureRole role) -> const G::MaterialTextureReference&
        {
            return source.Textures[static_cast<size_t>(role)];
        };
        Text imageError;
        Images images(root, buffers, sourcePath, logicalPath, imageError);
        if (!images.Emit(texture(G::MaterialTextureRole::BaseColor), MeshImageRole::Albedo, true, plan.Textures[0]) ||
            !images.Emit(texture(G::MaterialTextureRole::Normal), MeshImageRole::Normal, false, plan.Textures[1]) ||
            !images.Emit(texture(G::MaterialTextureRole::Emissive), MeshImageRole::Emissive, true, plan.Textures[3]))
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, imageError.c_str());
        }
        ArmImageInputs arm;
        if ((UsesImage(settings.Arm.Channels[0]) && !images.Arm(texture(G::MaterialTextureRole::Occlusion),
                                                                G::MaterialTextureRole::Occlusion, arm.Occlusion)) ||
            ((UsesImage(settings.Arm.Channels[1]) || UsesImage(settings.Arm.Channels[2])) &&
             !images.Arm(texture(G::MaterialTextureRole::MetallicRoughness), G::MaterialTextureRole::MetallicRoughness,
                         arm.MetallicRoughness)))
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, imageError.c_str());
        }
        const double factors[] = {source.OcclusionStrength, source.Roughness, source.Metallic};
        ArmImagePlan analyzed;
        if (AnalyzeArmImages(arm, settings.Arm, factors, analyzed) != ArmImageStatus::Success)
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "arm");
        }
        // textureのfactorはpixelへ焼込済み。scalarは未使用時だけ最終値、使用時は中立/未指定にする。
        record.Flags |= static_cast<uint32_t>(analyzed.TextureMask) << 3;
        record.OcclusionStrength = (analyzed.TextureMask & 1) ? 1.0f : static_cast<float>(analyzed.Channels[0].Scalar);
        record.Roughness = (analyzed.TextureMask & 2) ? -1.0f : static_cast<float>(analyzed.Channels[1].Scalar);
        record.Metallic = (analyzed.TextureMask & 4) ? -1.0f : static_cast<float>(analyzed.Channels[2].Scalar);
        MeshEmbeddedImage derived;
        if (analyzed.TextureMask)
        {
            Bytes pixels(analyzed.ByteCount);
            ArmImagePlan baked;
            if (BakeArmImages(arm, settings.Arm, factors, pixels, baked) != ArmImageStatus::Success ||
                !derived.SetRawRgba8(pixels, baked.Width, baked.Height))
            {
                return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "arm_bake");
            }
            derived.ImageIndex = SyntheticArmImageIndex;
            derived.Roles = static_cast<uint8_t>(MeshImageRole::Arm);
            derived.Format = "nvtex.v0.rgba8.linear";
            derived.LogicalPath = Text(logicalPath) + ".arm.rgba8";
            Hash raw;
            raw.String("NorvesLib.NVMESH.raw-arm.v1");
            raw.Integer(static_cast<uint8_t>(derived.Payload));
            raw.Integer(derived.Width);
            raw.Integer(derived.Height);
            raw.String(derived.Format);
            raw.Data(derived.GetBytes());
            derived.SourceHash = raw.Value;
            plan.Textures[2] = derived.LogicalPath;
        }
        Hash settingsHash, sourceHash;
        sourceHash.Value = gltfSourceHash;
        if (!AppendSettings(settingsHash, plan, bHasMaterial, materialIndex) ||
            !AppendSettings(sourceHash, plan, bHasMaterial, materialIndex))
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "canonical_settings");
        }
        plan.SettingsHash = settingsHash.Value;
        std::sort(images.Cache.begin(), images.Cache.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.Encoded.ImageIndex < b.Encoded.ImageIndex;
                  });
        sourceHash.String("resolved_image_sources");
        sourceHash.Integer(images.Cache.size());
        for (auto& image : images.Cache)
        {
            sourceHash.Integer(image.Encoded.ImageIndex);
            sourceHash.Integer(image.UsedRoles);
            sourceHash.Integer(static_cast<uint8_t>(image.Mime));
            sourceHash.Data(image.Encoded.GetBytes());
            if (image.bEmit)
            {
                plan.Images.push_back(std::move(image.Encoded));
            }
        }
        if (analyzed.TextureMask)
        {
            plan.Images.push_back(std::move(derived));
        }
        plan.SourceHash = sourceHash.Value;
        // wire referenceのoffsetはwriterで決める。ここでは文字列長を仮束縛して全scalarを共有codecで検査する。
        auto validation = record;
        A::CookedMaterialStringRef* refs[] = {&validation.Albedo, &validation.Normal, &validation.Arm,
                                              &validation.Emissive};
        uint64_t largest = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            if (plan.Textures[i].size() > UINT32_MAX)
            {
                return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "texture_path_size");
            }
            refs[i]->Length = static_cast<uint32_t>(plan.Textures[i].size());
            largest = std::max(largest, static_cast<uint64_t>(refs[i]->Length));
        }
        if (A::ValidateCookedMaterialRecord(validation, largest) != A::CookedMaterialStatus::Success)
        {
            return Fail(error, logicalPath, bHasMaterial, materialIndex, materialName, "material_float_or_wire_range");
        }
        out = std::move(plan);
        return true;
    }
    void WarnDuplicateMeshMaterials(View asset, uint32_t groups, uint32_t first, uint32_t second)
    {
        if (groups == 0)
        {
            return;
        }
        const int length = static_cast<int>(std::min<size_t>(asset.size(), 4096));
        std::fprintf(
            stderr,
            "AssetCook warning: asset=%.*s 同名の材質があります（index=%u,%u groups=%u）。元のファイルで名前を付け直してください。\n",
            length, asset.data(), first, second, groups);
    }
} // namespace NorvesLib::Tools::AssetCook
