#pragma once

#include "Container/Containers.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    /**
     * @brief DDGIプローブレイのレイトレーシング結果
     *
     * Compute shaderとの共有用に、各項目を32 bit値で保持する。
     */
    struct DDGIProbeRayQueryResult
    {
        float Distance = -1.0f;
        uint32_t bHit = 0u;
        uint32_t InstanceCustomIndex = UINT32_MAX;
        uint32_t PrimitiveIndex = UINT32_MAX;
        float Radiance[4] = {};
    };

    static_assert(sizeof(DDGIProbeRayQueryResult) == 32u);

    /**
     * @brief FramePacketのTLASに対するDDGIプローブレイ問い合わせを記録する
     *
     * GPU だけが読み書きする資源（レイの結果のバッファ、照射・距離のアトラスとその履歴）はビュー × ビューポートごとに
     * 1 組持つ。アトラスは現在と履歴のピンポンで、前回 Execute したアトラスを読みながら別のアトラスへ書く。
     * CPU が書く定数・instance の表・記述子セットは Execute ごとにフレームの持ち主（ViewRenderContext::FrameResources）
     * から借りるので、飛行中のフレームの番号は扱わない。
     */
    class DDGIProbePass
    {
    public:
        bool Execute(ViewRenderContext& context);
        void Shutdown();

        /** @brief ビュー・ビューポートの組の最後のレイの結果のバッファ（無ければ nullptr） */
        RHI::BufferPtr GetResultBuffer(uint32_t viewId, uint32_t viewportId) const;
        uint32_t GetResultCount(uint32_t viewId, uint32_t viewportId) const;
        /** @brief ビュー・ビューポートの組の最後に更新できた照射のアトラス（最後の Execute が失敗していれば nullptr） */
        RHI::TexturePtr GetIrradianceAtlas(uint32_t viewId, uint32_t viewportId) const;
        RHI::TexturePtr GetDistanceAtlas(uint32_t viewId, uint32_t viewportId) const;
        uint32_t GetAtlasProbeCount(uint32_t viewId, uint32_t viewportId) const;

        // 以前の呼び出しの形（先頭にフレームの番号を渡す）。資源はビュー・ビューポートごとに 1 組なので frameIndex は見ない
        RHI::BufferPtr GetResultBuffer(uint32_t frameIndex, uint32_t viewId, uint32_t viewportId) const
        {
            (void)frameIndex;
            return GetResultBuffer(viewId, viewportId);
        }
        uint32_t GetResultCount(uint32_t frameIndex, uint32_t viewId, uint32_t viewportId) const
        {
            (void)frameIndex;
            return GetResultCount(viewId, viewportId);
        }
        RHI::TexturePtr GetIrradianceAtlas(uint32_t frameIndex, uint32_t viewId, uint32_t viewportId) const
        {
            (void)frameIndex;
            return GetIrradianceAtlas(viewId, viewportId);
        }
        RHI::TexturePtr GetDistanceAtlas(uint32_t frameIndex, uint32_t viewId, uint32_t viewportId) const
        {
            (void)frameIndex;
            return GetDistanceAtlas(viewId, viewportId);
        }
        uint32_t GetAtlasProbeCount(uint32_t frameIndex, uint32_t viewId, uint32_t viewportId) const
        {
            (void)frameIndex;
            return GetAtlasProbeCount(viewId, viewportId);
        }

    private:
        /** @brief ビュー × ビューポート 1 組分の GPU の資源と履歴の状態 */
        struct ViewResources
        {
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            /** 最後に Execute したフレームの番号（しばらく Execute されない組を消えたビューとして手放すのに使う） */
            uint64_t LastExecuteFrameNumber = UINT64_MAX;
            /** 最後に GPU の命令を記録したフレームの世代（提出しなかったら履歴と状態を捨てる。0 は未設定） */
            uint32_t RecordedGenerationSlot = UINT32_MAX;
            uint64_t RecordedGeneration = 0u;
            uint32_t ResultCount = 0u;
            uint32_t AtlasProbeCount = 0u;
            bool bAtlasValid = false;
            RHI::BufferPtr ResultBuffer;
            RHI::TexturePtr IrradianceAtlas;
            RHI::TexturePtr DistanceAtlas;
            RHI::TexturePtr HistoryIrradianceAtlas;
            RHI::TexturePtr HistoryDistanceAtlas;
            float VolumeOrigin[3] = {};
            float ProbeSpacing[3] = {};
            uint32_t ProbeCounts[3] = {};
            // device address の参照は記述子に保持されないので、次の Execute で差し替えるまで持つ
            // （手放したバッファの破棄はデバイスが提出の完了まで遅らせる）
            Container::VariableArray<RHI::BufferPtr> GeometryBuffers;
            RHI::ResourceState ResultState = RHI::ResourceState::Undefined;
            RHI::ResourceState IrradianceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState DistanceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState HistoryIrradianceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState HistoryDistanceAtlasState = RHI::ResourceState::Undefined;
        };

        bool EnsurePipeline(ViewRenderContext& context);
        ViewResources* FindViewResources(uint32_t viewId, uint32_t viewportId);
        const ViewResources* FindViewResources(uint32_t viewId, uint32_t viewportId) const;
        ViewResources* FindOrCreateViewResources(ViewRenderContext& context, uint32_t resultCount);
        void RetireStaleViewResources(uint64_t frameNumber);
        bool UploadFrameData(ViewRenderContext& context,
                             const void* data,
                             uint32_t size,
                             bool bStorage,
                             const char* unownedDebugName,
                             RHI::BufferPtr& outBuffer,
                             uint32_t& outOffset);
        RHI::DescriptorSetPtr AllocateFrameDescriptorSet(ViewRenderContext& context, bool bRayQuery);
        void DisableAfterResourceFailure(const char* reason);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ComputeShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_IrradianceUpdateShader;
        RHI::PipelinePtr m_IrradianceUpdatePipeline;
        /** 借りる記述子セットの共有のレイアウト（EnsurePipeline で 1 回引く） */
        RHI::DescriptorSetLayoutPtr m_RayQueryDescriptorSetLayout;
        RHI::DescriptorSetLayoutPtr m_IrradianceUpdateDescriptorSetLayout;
        RHI::SamplerPtr m_AtlasSampler;
        RHI::TexturePtr m_DefaultIrradianceAtlas;
        RHI::TexturePtr m_DefaultDistanceAtlas;
        RHI::ResourceState m_DefaultIrradianceAtlasState = RHI::ResourceState::Undefined;
        RHI::ResourceState m_DefaultDistanceAtlasState = RHI::ResourceState::Undefined;
        Container::VariableArray<ViewResources> m_ViewResources;
        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）で作った定数・記述子セット。
        // 提出の完了を知る手段が無いので、パスを手放す（Shutdown）まで持つ
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;
        bool m_bPipelineAttempted = false;
        bool m_bUnavailable = false;
        bool m_bFailureReported = false;
    };
} // namespace NorvesLib::Core::Rendering
