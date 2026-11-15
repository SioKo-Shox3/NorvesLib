#pragma once

#include "Container/Containers.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Vegetation
{
    // 種別 ID は GR43 のブレード記録で 6bit を占める。
    inline constexpr uint32_t GrassSpeciesMaxCount = 64;
    inline constexpr uint32_t GrassLodPointCount = 4;

    /** @brief 距離ごとに残す草の本数の割合。 */
    struct GrassLodPoint
    {
        float DistanceMeters = 0.0f;
        float KeepFraction = 1.0f;
    };

    /** @brief 枯れ度 1 のときの草の変わり方。重みはすべて 0〜1。 */
    struct GrassDrynessResponse
    {
        Math::Vector3 Color = Math::Vector3(0.152926f, 0.144128f, 0.107023f); // 仮の #6D6A5C を線形 RGB に変換
        float ColorWeight = 1.0f; // 近景で枯れた色へ寄せる重み
        float DensityScale = 0.6f; // 枯れ度 1 での密度の倍率
        float HeightScale = 0.7f; // 枯れ度 1 での高さの倍率
        float BendAdd = 0.3f; // 枯れ度 1 で曲がりに足す量
        float FarColorWeight = 1.0f; // 遠景の帯で枯れた色へ寄せる重み
        float FarHeightFlatten = 0.0f; // 遠景の帯で草丈を潰す重み（0 は潰さない）
    };

    /**
     * @brief 草の種別ごとの形状・材質・応答。
     * 色は線形 RGB で保持し、JSON の色は sRGB の #RRGGBB から変換する。
     * 同梱 JSON の wet_grass / dry_grass の色は仮の既定値であり、採否は未定。
     * 色を省略すると黒、LOD を省略すると全点が距離 0・残す割合 1 になる。
     * 枯れ度の応答は GrassDrynessResponse の既定値を持ち、JSON で上書きできる。
     */
    struct GrassSpecies
    {
        Container::String Name;
        float HeightMinMeters = 0.3f; // LayerA.G = 0 のときの草丈
        float HeightMaxMeters = 1.2f; // LayerA.G = 255 のときの草丈
        float BladeWidthMeters = 0.01f;
        float Bend = 0.3f; // 曲がり 0..1
        float Stiffness = 0.5f; // 剛性 0..1
        Math::Vector3 RootColor;
        Math::Vector3 TipColor;
        float Roughness = 0.6f;
        float WindResponse = 1.0f;
        float TrampleResponse = 1.0f;
        Math::Vector3 WetColor;
        float WetRoughness = 0.3f;
        float WetDroop = 0.2f; // 濡れたときのしなだれ 0..1
        float Opacity = 1.0f; // 視線の消散係数 [1/m]。密度 1・草丈いっぱいのときの値
        float BladesPerSquareMeter = 400.0f; // 密度 1 のときの本数
        GrassLodPoint Lod[GrassLodPointCount]; // JSON で指定する場合は距離の昇順
        GrassDrynessResponse Dryness;
    };

    /** @brief 配列の添字を種別 ID とする、最大 64 種の表。 */
    class GrassSpeciesTable
    {
    public:
        // ID は 0..Count-1。範囲外は nullptr を返す。
        const GrassSpecies* Find(uint32_t speciesId) const;
        uint32_t GetCount() const;
        // 64 個を超える追加は失敗し、表を変更しない。
        bool Add(const GrassSpecies& species);
        const Container::VariableArray<GrassSpecies>& GetAll() const;

    private:
        Container::VariableArray<GrassSpecies> m_Species;
    };

    // 失敗時は表を変更せず、指定されていれば理由を日本語で返す。
    bool TryParseGrassSpeciesTable(const Container::String& jsonText, GrassSpeciesTable& outTable,
                                  Container::String* pOutError = nullptr);
    // パスをそのまま開く。Assets 等のルート解決は行わない。
    bool TryLoadGrassSpeciesTable(const char* filePath, GrassSpeciesTable& outTable,
                                 Container::String* pOutError = nullptr);
} // namespace NorvesLib::Core::Vegetation
