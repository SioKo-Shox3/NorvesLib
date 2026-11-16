// インポスターの焼き込みに渡すメッシュと材質を読み込む。
#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include <cstdint>
#include <filesystem>

namespace NorvesLib::Tools::ImpostorBaker
{
    struct ImpostorBakerMaterialImage
    {
        Core::Container::VariableArray<uint8_t> Rgba8; // 展開した画素。空なら画像なし。
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::AnsiString CookedTexturePath; // cook 済みテクスチャの論理パス。
    };

    struct ImpostorBakerMaterialInput
    {
        ImpostorBakerMaterialImage Albedo; // sRGB の色。
        ImpostorBakerMaterialImage Normal; // エンジンの cook と同じ向きの接空間法線。
        ImpostorBakerMaterialImage Orm; // R=AO・G=粗さ・B=金属度。
        float BaseColor[3] = {1, 1, 1}; // 線形の物体色。
        float Metallic = -1.0f; // 負は未指定。ORM の画像が無いときだけ使う。
        float Roughness = -1.0f; // 負は未指定。
        float OcclusionStrength = 1.0f; // 1 は遮蔽なし。
        float AlphaCutoff = 0.0f; // 0 は切り抜かない。
        float NormalScale = 1.0f;
        bool bHasNormalScale = false;
    };

    struct ImpostorBakerSection
    {
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t MaterialIndex = 0;
    };

    struct ImpostorBakerMeshInput
    {
        Core::Container::VariableArray<Core::Rendering::Mesh3DVertex> Vertices;
        Core::Container::VariableArray<uint32_t> Indices; // 頂点の基点を足し込み済みの絶対番号。
        Core::Container::VariableArray<ImpostorBakerSection> Sections;
        Core::Container::VariableArray<ImpostorBakerMaterialInput> Materials;
    };

    struct ImpostorBakerMeshLoadOptions
    {
        // 指定された PNG で材質 0 の画像を置き換える。法線はエンジンの向きで渡す。
        std::filesystem::path AlbedoOverride, NormalOverride, OrmOverride;
    };

    // 成功時だけ outMesh を置き換え、失敗の説明を日本語で返す。
    [[nodiscard]] bool LoadImpostorBakerMesh(const std::filesystem::path& inputPath,
                                           const ImpostorBakerMeshLoadOptions& options,
                                           ImpostorBakerMeshInput& outMesh,
                                           Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::ImpostorBaker
