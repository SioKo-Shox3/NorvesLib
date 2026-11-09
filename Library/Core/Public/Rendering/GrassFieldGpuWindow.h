#pragma once

#include "Container/VariableArray.h"
#include "Rendering/FrameResourceOwner.h"
#include "RHI/ITexture.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    struct GrassFieldRenderData;

    /** @brief カメラ周辺の草の場を配列テクスチャに保持する。RenderThread から使う。 */
    class GrassFieldGpuWindow
    {
    public:
        struct Settings
        {
            uint32_t WindowSize = 16;
            uint32_t MaxUploadsPerFrame = 8;
        };

        GrassFieldGpuWindow() = default;
        explicit GrassFieldGpuWindow(const Settings& settings);

        /** @brief 次の Update から設定を使う。窓の大きさを変えるとテクスチャを作り直す。 */
        void SetSettings(const Settings& settings);

        /**
         * @brief 必要なチャンクを近い順にフレームの切れ端へ写し、両層への転送を記録する。
         * 同じフレームの呼び出しを合わせて MaxUploadsPerFrame 件まで送る。
         * 窓の大きさと解像度は正数、カメラとチャンク寸法は有限値とする。
         * @return 更新を記録できたか。未送信のチャンクが残っていても記録できれば true。
         */
        bool Update(RHI::IDevice* device, RHI::ICommandList* commandList, FrameResourceLane* frameResources,
                    const GrassFieldRenderData& field, float cameraX, float cameraZ);

        /**
         * @brief 直近の Update で指定した窓と field の内容が両層に載っているか。
         * そのフレームのコピーも、その後に記録する描画からは常駐として扱う。
         * フレームが中止された場合は、次の Update で提出結果を確認してから問い合わせる。
         */
        bool IsResident(int32_t cx, int32_t cz) const;

        const RHI::TexturePtr& GetLayerATexture() const { return m_LayerATexture; }
        const RHI::TexturePtr& GetLayerBTexture() const { return m_LayerBTexture; }
        uint32_t GetWindowSize() const { return m_Settings.WindowSize; }

        /** @brief 設定を残して資源と常駐の記録を手放す。 */
        void Reset();

    private:
        struct LayerRecord
        {
            const GrassFieldRenderData* Field = nullptr;
            uint64_t ContentRevision = 0;
            int32_t ChunkX = 0;
            int32_t ChunkZ = 0;
            bool bResident = false;
        };

        void ReleaseTextures();
        void SetBacklogged(bool bBacklogged);

        Settings m_Settings;
        RHI::TexturePtr m_LayerATexture;
        RHI::TexturePtr m_LayerBTexture;
        Container::VariableArray<LayerRecord> m_Layers;
        RHI::IDevice* m_ResourceDevice = nullptr;
        uint32_t m_ResourceWindowSize = 0;
        uint32_t m_ResourceLayerAResolution = 0;
        uint32_t m_ResourceLayerBResolution = 0;

        const GrassFieldRenderData* m_CurrentField = nullptr;
        RHI::IDevice* m_CurrentDevice = nullptr;
        uint64_t m_CurrentRevision = 0;
        int64_t m_MinChunkX = 0;
        int64_t m_MinChunkZ = 0;
        uint32_t m_CurrentLayerAResolution = 0;
        uint32_t m_CurrentLayerBResolution = 0;
        bool m_bCurrentWindowValid = false;
        bool m_bBacklogged = false;

        // 提出の成否だけを持ち主へ尋ねる。フレーム数から GPU の寿命は推測しない。
        FrameResourceGeneration m_PendingGeneration;
        FrameResourceGeneration m_BudgetGeneration;
        uint32_t m_UploadsInFrame = 0;
    };
} // namespace NorvesLib::Core::Rendering
