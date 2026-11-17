#include "ImpostorBakerMeshInput.h"

#include "Asset/AssetBlob.h"
#include "Asset/CookedMeshFormat.h"
#include "MeshCooker.h"
#include "TextureCooker.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace NorvesLib::Tools::ImpostorBaker
{
    namespace
    {
        namespace A = Core::Asset;
        namespace C = Core::Container;
        namespace Cook = AssetCook;

        bool ReadBytes(const std::filesystem::path& path, C::VariableArray<uint8_t>& bytes, C::AnsiString& error)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                error = "入力ファイルを開けません";
                return false;
            }
            const auto size = file.tellg();
            if (size <= 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max() ||
                static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
            {
                error = "入力ファイルが空か、大きすぎます";
                return false;
            }
            bytes.resize(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            {
                error = "入力ファイルを最後まで読み込めません";
                return false;
            }
            return true;
        }

        bool HasExtension(const std::filesystem::path& path, const wchar_t* extension)
        {
            return _wcsicmp(path.extension().c_str(), extension) == 0;
        }

        bool DecodeImage(C::Span<const uint8_t> bytes, ImpostorBakerMaterialImage& out, C::AnsiString& error)
        {
            Cook::DecodedTextureRgba8 decoded;
            C::AnsiString reason;
            if (!Cook::DecodeTextureRgba8(bytes, decoded, reason))
            {
                error = C::AnsiString("材質の画像を RGBA8 に展開できません: ") + reason;
                return false;
            }
            out.Rgba8 = std::move(decoded.Pixels);
            out.Width = decoded.Width;
            out.Height = decoded.Height;
            out.CookedTexturePath.clear();
            return true;
        }

        bool LoadMaterialImage(C::AnsiStringView path, const Cook::MeshCookResult* cooked,
                               ImpostorBakerMaterialImage& out, C::AnsiString& error)
        {
            if (path.empty())
            {
                return true;
            }
            if (!cooked)
            {
                out.CookedTexturePath = C::AnsiString(path);
                return true;
            }
            for (const auto& image : cooked->EmbeddedImages)
            {
                if (C::AnsiStringView(image.LogicalPath.data(), image.LogicalPath.size()) != path)
                {
                    continue;
                }
                const auto bytes = image.GetBytes();
                if (image.Payload == Cook::MeshImagePayload::Encoded)
                {
                    return DecodeImage(bytes, out, error);
                }
                if (image.Payload != Cook::MeshImagePayload::RawRgba8 || !image.Width || !image.Height ||
                    static_cast<uint64_t>(image.Width) * image.Height > Cook::MaximumMaterialImageDecodedBytes / 4 ||
                    static_cast<uint64_t>(image.Width) * image.Height * 4 != bytes.size())
                {
                    error = "材質の展開済み画像の形式か寸法が不正です";
                    return false;
                }
                out.Rgba8.assign(bytes.begin(), bytes.end());
                out.Width = image.Width;
                out.Height = image.Height;
                return true;
            }
            error = C::AnsiString("材質が参照する画像が cook 結果にありません: ") + C::AnsiString(path);
            return false;
        }

        bool AppendRange(const A::CookedMeshData& cooked, uint32_t indexOffset, uint32_t indexCount,
                         uint32_t vertexOffset, uint32_t vertexCount, uint32_t materialIndex,
                         ImpostorBakerMeshInput& mesh, C::AnsiString& error, bool bMergeSections = false)
        {
            if (indexCount % 3 != 0 || static_cast<uint64_t>(indexOffset) + indexCount > cooked.Indices.size() ||
                static_cast<uint64_t>(vertexOffset) + vertexCount > cooked.Vertices.size() ||
                indexCount > UINT32_MAX - mesh.Indices.size() ||
                materialIndex >= (mesh.Materials.empty() ? 1 : mesh.Materials.size()))
            {
                error = "メッシュの頂点・インデックス・材質の範囲が不正です";
                return false;
            }
            if (indexCount == 0)
            {
                return true;
            }
            const uint32_t start = static_cast<uint32_t>(mesh.Indices.size());
            for (uint32_t i = 0; i < indexCount; ++i)
            {
                const uint32_t local = cooked.Indices[static_cast<size_t>(indexOffset) + i];
                if (local >= vertexCount)
                {
                    error = "メッシュのインデックスが区間の頂点範囲を超えています";
                    return false;
                }
                mesh.Indices.push_back(vertexOffset + local);
            }
            if (bMergeSections && !mesh.Sections.empty() && mesh.Sections.back().MaterialIndex == materialIndex &&
                mesh.Sections.back().IndexOffset + mesh.Sections.back().IndexCount == start)
            {
                mesh.Sections.back().IndexCount += indexCount;
            }
            else
            {
                mesh.Sections.push_back({start, indexCount, materialIndex});
            }
            return true;
        }

        bool LoadGeometry(const A::CookedMeshData& cooked, ImpostorBakerMeshInput& mesh, C::AnsiString& error)
        {
            // ParseCookedMesh は v1.1 のページも展開し、クラスタの位置を全体の配列へ補正する。
            if (!cooked.Pages.empty() && (cooked.Vertices.empty() || cooked.Indices.empty()))
            {
                error = "ページに詰めた NVMESH の未展開データは未対応です。glTF を直接渡してください";
                return false;
            }
            if (cooked.Vertices.empty() || cooked.Vertices.size() > UINT32_MAX || cooked.Indices.size() > UINT32_MAX)
            {
                error = "メッシュの頂点が空か、頂点・インデックスが多すぎます";
                return false;
            }
            mesh.Vertices.reserve(cooked.Vertices.size());
            for (const auto& source : cooked.Vertices)
            {
                Core::Rendering::Mesh3DVertex vertex{
                    {source.Position.X, source.Position.Y, source.Position.Z},
                    {source.Normal.X, source.Normal.Y, source.Normal.Z},
                    {source.TexCoord.U, source.TexCoord.V}};
                mesh.Vertices.push_back(vertex);
            }
            if (cooked.Layout == A::CookedMeshLayout::LegacyV0)
            {
                if (cooked.Submeshes.empty())
                {
                    if (!AppendRange(cooked, 0, static_cast<uint32_t>(cooked.Indices.size()), 0,
                                     static_cast<uint32_t>(cooked.Vertices.size()), 0, mesh, error))
                    {
                        return false;
                    }
                }
                for (const auto& section : cooked.Submeshes)
                {
                    if (!AppendRange(cooked, section.IndexOffset, section.IndexCount, section.VertexOffset,
                                     section.VertexCount, section.MaterialIndex, mesh, error))
                    {
                        return false;
                    }
                }
            }
            else
            {
                C::VariableArray<const A::CookedMeshCluster*> clusters;
                for (const auto& cluster : cooked.Clusters)
                {
                    if (cluster.LODLevel == 0)
                    {
                        clusters.push_back(&cluster);
                    }
                }
                std::sort(clusters.begin(), clusters.end(), [](const auto* left, const auto* right)
                {
                    if (left->MaterialIndex != right->MaterialIndex)
                    {
                        return left->MaterialIndex < right->MaterialIndex;
                    }
                    return left->IndexOffset < right->IndexOffset;
                });
                for (const auto* cluster : clusters)
                {
                    if (!AppendRange(cooked, cluster->IndexOffset, cluster->IndexCount, cluster->VertexOffset,
                                     cluster->VertexCount, cluster->MaterialIndex, mesh, error, true))
                    {
                        return false;
                    }
                }
            }
            if (mesh.Indices.empty())
            {
                error = "メッシュに LOD0 の三角形がありません";
                return false;
            }
            return true;
        }

        bool ApplyOverride(const std::filesystem::path& path, ImpostorBakerMaterialImage& out, C::AnsiString& error)
        {
            if (path.empty())
            {
                return true;
            }
            C::VariableArray<uint8_t> bytes;
            if (!ReadBytes(path, bytes, error))
            {
                error = C::AnsiString("上書き画像を読めません: ") + error;
                return false;
            }
            constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (bytes.size() < sizeof(signature) || std::memcmp(bytes.data(), signature, sizeof(signature)) != 0)
            {
                error = "上書き画像には PNG を指定してください";
                return false;
            }
            // 上書きの法線もエンジンの向きなので、緑の反転はしない。
            return DecodeImage({bytes.data(), bytes.size()}, out, error);
        }
    } // namespace

    bool LoadImpostorBakerMesh(const std::filesystem::path& inputPath, const ImpostorBakerMeshLoadOptions& options,
                              ImpostorBakerMeshInput& outMesh, Core::Container::AnsiString& outError)
    {
        outError.clear();
        const bool bGltf = HasExtension(inputPath, L".gltf") || HasExtension(inputPath, L".glb");
        if (!bGltf && !HasExtension(inputPath, L".nvmesh"))
        {
            outError = "入力には .gltf・.glb・.nvmesh を指定してください";
            return false;
        }
        C::VariableArray<uint8_t> source;
        if (!ReadBytes(inputPath, source, outError))
        {
            return false;
        }
        Cook::MeshCookResult result;
        if (bGltf)
        {
            // clustered v1 はページと LOD 階層を作らず、複数の材質と PBR の値を保つ。
            constexpr C::AnsiStringView format = "nvmesh.v1.mesh3d.pnt.u32.clustered";
            if (!Cook::IsSupportedMeshCookFormat(format))
            {
                outError = "メッシュの cook が clustered v1 に対応していません";
                return false;
            }
            C::AnsiString reason;
            if (!Cook::CookGltfToNvmeshNativePath(source.data(), source.size(), format, inputPath,
                                                 "impostor/input.nvmesh", result, reason))
            {
                outError = C::AnsiString("glTF のメッシュを cook できません: ") + reason;
                return false;
            }
        }
        const auto& bytes = bGltf ? result.NvmeshBytes : source;
        const auto parsed = A::ParseCookedMesh(A::AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
        if (!parsed.Succeeded())
        {
            outError = "NVMESH の形式またはデータが不正で、読み込めません";
            return false;
        }
        ImpostorBakerMeshInput mesh;
        for (const auto& material : parsed.Mesh.Materials)
        {
            ImpostorBakerMaterialInput input;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                input.BaseColor[axis] = material.Pbr.BaseColor[axis];
            }
            const auto alphaMode = (material.Pbr.Flags & A::CookedMaterialFormatV1::AlphaModeMask) >>
                                   A::CookedMaterialFormatV1::AlphaModeShift;
            input.AlphaCutoff = alphaMode == static_cast<uint32_t>(A::CookedMaterialAlphaMode::Mask)
                                    ? material.Pbr.AlphaCutoff : 0.0f;
            input.Metallic = material.Pbr.Metallic;
            input.Roughness = material.Pbr.Roughness;
            input.OcclusionStrength = material.Pbr.OcclusionStrength;
            input.NormalScale = material.Pbr.NormalScale;
            input.bHasNormalScale = parsed.Mesh.FormatMajor == 1;
            const auto* images = bGltf ? &result : nullptr;
            if (!LoadMaterialImage(parsed.Mesh.GetString(material.AlbedoTexture), images, input.Albedo, outError) ||
                !LoadMaterialImage(parsed.Mesh.GetString(material.NormalTexture), images, input.Normal, outError) ||
                !LoadMaterialImage(parsed.Mesh.GetString(material.ArmTexture), images, input.Orm, outError))
            {
                return false;
            }
            // glTF の画像は MeshMaterialV1Plan とモデル用 CookTextureToNvtex と同じく Y を保つ。
            // 単体テクスチャ用の bFlipNormalY はモデルの cook 経路では適用されない。
            mesh.Materials.push_back(std::move(input));
        }
        if (!LoadGeometry(parsed.Mesh, mesh, outError))
        {
            return false;
        }
        if (!options.AlbedoOverride.empty() || !options.NormalOverride.empty() || !options.OrmOverride.empty())
        {
            if (mesh.Materials.empty())
            {
                mesh.Materials.emplace_back();
            }
            auto& material = mesh.Materials[0];
            if (!ApplyOverride(options.AlbedoOverride, material.Albedo, outError) ||
                !ApplyOverride(options.NormalOverride, material.Normal, outError) ||
                !ApplyOverride(options.OrmOverride, material.Orm, outError))
            {
                return false;
            }
        }
        outMesh = std::move(mesh);
        return true;
    }
} // namespace NorvesLib::Tools::ImpostorBaker
