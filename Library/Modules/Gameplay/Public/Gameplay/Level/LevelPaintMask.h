// 地形格子に沿った配置編集用マスクと円形ブラシ、PNG の読み書きを提供する。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Vegetation
{
    class DrynessMap;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    struct TerrainGridLayout;
}

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 塗る層。ファイル名の幹は地形レシピの対象名に合わせる。
    enum class ELevelPaintLayer : uint8_t
    {
        GrassDensity,
        Dryness
    };

    inline constexpr uint32_t kLevelPaintLayerCount = 2;

    /// 層に対応するファイル名の幹を返す。無効な層では空文字列を返す。
    const char* GetLevelPaintLayerFileStem(ELevelPaintLayer layer);

    /// 地形の高さ格子と同じ標本の並び。
    struct LevelPaintMaskLayout
    {
        uint32_t Columns = 0;
        uint32_t Rows = 0;
        double FirstX = 0.0;
        double FirstZ = 0.0;
        double Spacing = 0.0;
        /// 地形の height_source.flip_z と同じ値にする。true なら PNG の行を反転する。
        bool bFlipZ = false;

        /// 寸法が 2 以上で、原点が有限かつ間隔が有限で正なら true。
        bool IsValid() const;
        bool operator==(const LevelPaintMaskLayout&) const = default;
    };

    /// 地形の格子からマスクの並びを作る。
    LevelPaintMaskLayout MakeLevelPaintMaskLayout(const Core::Rendering::Terrain::TerrainGridLayout& grid, bool bFlipZ = false);

    /// 枯れの基準値の作り方。既定値は配置に合わせて変更できる。
    struct LevelDrynessBaselineSettings
    {
        float BaseLevel = 0.0f;           // 源の無い所の枯れ [0, 1]。
        float TerritoryLevel = 0.8f;      // 持ち場の内側の枯れ [0, 1]。0 で無効。
        float TerritoryRadiusScale = 1.0f; // 持ち場の EngageRadius に掛ける倍率。
        float TerritoryFalloff = 6.0f;     // 半径の外側で 0 になるまでの幅 [m]。
        bool bUseWitherSources = true;     // World から枯れの源を集める。
        float EdgeNoise = 3.0f;           // 縁を乱す変位の振幅 [m]。0 で無効。
        float NoiseScale = 8.0f;          // 雑音の格子の間隔 [m]。
        uint32_t Seed = 93;
    };

    /// 持ち場 1 つ分の円。中心は世界の XZ、半径と幅の単位は m。
    struct LevelDrynessCircle
    {
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float Radius = 0.0f;
        float Falloff = 0.0f;
        float Level = 0.0f;
    };

    /// 標本位置 p を 2 種の値雑音で p + EdgeNoise × (2n1 − 1, 2n2 − 1) にずらす。
    /// その位置で各円の Level × (1 − smoothstep(Radius, Radius + Falloff, 距離)) と源を評価し、
    /// BaseLevel を含む最大値を [0, 1] に丸めて 8bit に変換する。値雑音は Seed と格子座標で決まる。
    /// 円の減衰幅が 0 なら円内だけを採る。出力は row × Columns + column の行優先。
    /// 設定と円は有限、半径・幅・倍率・雑音振幅は 0 以上、設定の枯れ値は [0, 1] とする。
    /// 雑音振幅が正なら NoiseScale は正。無効な入力や確保の失敗時は出力を変えず false。
    bool BuildLevelDrynessBaseline(const LevelPaintMaskLayout& layout,
                                   Core::Container::Span<const LevelDrynessCircle> territories,
                                   const Core::Vegetation::DrynessMap* witherSources,
                                   const LevelDrynessBaselineSettings& settings,
                                   Core::Container::VariableArray<uint8_t>& outValues);

    /// 破棄予約された部分木を除き、World の持ち場と枯れの源から基準値を作る。
    /// 個数は実際に加えた円と源の数。失敗時は値と個数の出力を変えない。
    bool BuildLevelDrynessBaseline(const Core::World& world, const LevelPaintMaskLayout& layout,
                                   const LevelDrynessBaselineSettings& settings,
                                   Core::Container::VariableArray<uint8_t>& outValues,
                                   size_t* pOutTerritoryCount = nullptr, size_t* pOutWitherCount = nullptr);

    /// 世界座標の XZ 平面で使う円形ブラシ。
    struct LevelPaintBrush
    {
        float Radius = 4.0f;   // 半径 [m]。有限で正。
        float Strength = 1.0f; // 強さ [0, 1]。
        float Falloff = 0.5f;  // 外周から減衰させる幅の割合 [0, 1]。
        uint8_t Value = 0;     // 塗るときの目標値。
    };

    /// 重みは Strength × (1 − smoothstep(Radius × (1 − Falloff), Radius, 距離))。
    /// Falloff が 0 なら円内で Strength。非有限または半径が正でないブラシは 0。
    /// Strength と Falloff は [0, 1] に丸める。
    float EvaluateLevelPaintBrushWeight(const LevelPaintBrush& brush, float distance);

    /// 1 層の値と、消去時に戻す基準値を行優先で保持する。
    class LevelPaintMask
    {
    public:
        /// 有効な配置で初期化し、値と基準値を defaultValue にする。失敗時は元の状態を保つ。
        bool Initialize(const LevelPaintMaskLayout& layout, uint8_t defaultValue);
        /// 空に戻す。
        void Reset();
        bool IsValid() const;
        const LevelPaintMaskLayout& GetLayout() const;
        Core::Container::Span<const uint8_t> GetValues() const;
        Core::Container::Span<const uint8_t> GetBaseline() const;
        /// 基準値を差し替える。bResetValues なら値も揃えて変更の印を消す。
        /// 標本数が違う場合は false を返し、状態を変えない。
        bool SetBaseline(Core::Container::Span<const uint8_t> baseline, bool bResetValues);
        /// 読み込んだ値を差し替え、変更の印を消す。標本数が違う場合は状態を変えない。
        bool SetValues(Core::Container::Span<const uint8_t> values);
        /// 列と行に対応する世界の XZ を返す。
        void GetSampleWorldXZ(uint32_t column, uint32_t row, double& outX, double& outZ) const;
        /// 世界の XZ を格子外も含む連続の標本座標に変換する。無効なマスクでは出力を変えない。
        bool WorldToSample(double x, double z, double& outColumn, double& outRow) const;
        /// 円内の値をブラシの目標値へ寄せ、変わった標本数を返す。格子外の中心にも対応する。
        /// 標本数が uint32_t の上限を超えた場合は上限を返す。
        uint32_t Paint(double centerX, double centerZ, const LevelPaintBrush& brush);
        /// 円内の値を標本ごとの基準値へ寄せ、変わった標本数を返す。上限を超えた場合は上限を返す。
        uint32_t Erase(double centerX, double centerZ, const LevelPaintBrush& brush);
        bool IsDirty() const;
        void ClearDirty();

    private:
        uint32_t ApplyBrush(double centerX, double centerZ, const LevelPaintBrush& brush, bool bErase);

        LevelPaintMaskLayout m_Layout;
        Core::Container::VariableArray<uint8_t> m_Values;
        Core::Container::VariableArray<uint8_t> m_Baseline;
        bool m_bDirty = false;
    };

    /// 8bit のグレースケール PNG を一時ファイル経由で保存する。必要なら親ディレクトリを作成する。
    /// 失敗時は既存ファイルを保ち、エラー出力は処理の最初に空にする。
    bool SaveLevelPaintMaskPng(const LevelPaintMask& mask, const Core::Container::String& filePath,
                               Core::Container::String* pOutError = nullptr);
    /// 配置と同寸法の 8bit PNG から先頭チャンネルを読む。bFlipZ に応じて行順を戻す。
    /// 失敗時は値の出力を変えず、エラー出力は処理の最初に空にする。
    bool LoadLevelPaintMaskPng(const Core::Container::String& filePath, const LevelPaintMaskLayout& layout,
                               Core::Container::VariableArray<uint8_t>& outValues,
                               Core::Container::String* pOutError = nullptr);
} // namespace NorvesLib::Modules::Gameplay::Level
