// 地形格子に沿った配置編集用マスクと円形ブラシ、PNG の読み書きを提供する。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstdint>

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
