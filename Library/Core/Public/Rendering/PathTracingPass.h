// FramePacketのRTスナップショットを使う独立パストレーシングパス。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/PathTracingCamera.h"
#include "Rendering/PathTracingTransportScope.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief パストレーサーが1フレームで束ねる材質textureの上限（重複を除いた数） */
    inline constexpr uint32_t PathTracingMaterialTextureCapacity = 256u;

    /** @brief 空が無効なときの環境光の種類。環境光はBSDF標本だけで評価する。 */
    enum class PathTracingEnvironmentMode : uint32_t
    {
        Black = 0,
        Uniform = 1,
        Equirect = 2
    };

    /**
     * @brief 空が無効なときの環境光
     *
     * 放射輝度は物理値で、パストレーサーがカメラのプリエクスポージャを掛ける。
     * Equirectの向きはLightingPassの環境マップと同じ正距円筒（atan(z, x)、asin(-y)）。
     */
    struct PathTracingEnvironment
    {
        PathTracingEnvironmentMode Mode = PathTracingEnvironmentMode::Black;
        float UniformRadiance[3] = {0.0f, 0.0f, 0.0f};
        RHI::TexturePtr EquirectTexture;
        float Intensity = 1.0f;
    };

    /**
     * @brief 空が無効なときの既定の環境光にするHDR環境マップ（ラスタのLightingPassと同じ設定）
     *
     * 読み込みと輝度換算はLightingPassと同じ共有関数で行い、同じ放射輝度の正距円筒textureを使う。
     */
    struct PathTracingEnvironmentMapSource
    {
        Container::String Path;
        float LuminanceScaleNits = 1.0f;
        float Intensity = 1.0f;
    };

    /** @brief ラスタの検証mode 252（一様環境）と同じ放射輝度 */
    inline constexpr float PathTracingValidationUniformRadiance = 100.0f;

    /** @brief 1回のdispatchで累積できる試料数の上限 */
    inline constexpr uint32_t PathTracingMaxSamplesPerFrame = 1024u;

    /**
     * @brief 表面BSDF
     *
     * Productionはラスタと同じDFG LUTで多重散乱を補償したGGX鏡面と、(1-Ed)で重みを付けた拡散。
     * ValidationLambertはラスタの検証mode 253と同じ純Lambert（アルベド/π）。
     */
    enum class PathTracingBsdfMode : uint32_t
    {
        Production = 0,
        ValidationLambert = 1
    };

    /**
     * @brief 発光三角形と太陽円盤の標本化戦略
     *
     * 既定はpower heuristic（β=2）のMIS。LightOnlyとBsdfOnlyは同じ期待値へ収束することを確かめる検証用。
     * 点・spot・方向光は常に光源標本（重み1）。
     */
    enum class PathTracingLightSampling : uint32_t
    {
        MultipleImportance = 0,
        LightOnly = 1,
        BsdfOnly = 2
    };

    class PathTracingPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "PathTracingPass"; }
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /**
         * @brief パストレーサーに必要なデバイス機能がそろっているか
         *
         * 加速構造・RT pipeline・buffer device address・shaderの64bit整数・texture配列の非一様添字。
         * 起動時の描画方式の選択、Initialize、テストのスキップ判定で同じ条件を使う。
         */
        static bool IsSupported(const RHI::DeviceCapabilities& capabilities);

        uint32_t GetAccumulatedSampleCount() const;
        RHI::TexturePtr GetAccumulatedTexture() const;

        /** @brief 検証出力を切り替える。変更すると累積履歴を捨てる。 */
        void SetDebugOutput(PathTracingDebugOutput output) { m_DebugOutput = output; }

        /** @brief 直近フレームで束ねた材質texture数（先頭の既定texture4個を含む） */
        uint32_t GetBoundMaterialTextureCount() const { return m_BoundMaterialTextureCount; }

        /**
         * @brief 空が無効なときの環境光を設定する。変更すると累積履歴を捨てる。
         *
         * 環境textureの参照を持つため、Shutdownで既定（黒）へ戻して参照を離す。
         * 同じtextureの中身を書き換えた場合は履歴を捨てないので、別textureを渡すかscene revisionを進める。
         */
        void SetEnvironment(const PathTracingEnvironment& environment) { m_Environment = environment; }

        /** @brief 表面BSDFを切り替える。変更すると累積履歴を捨てる。 */
        void SetBsdfMode(PathTracingBsdfMode mode) { m_BsdfMode = mode; }

        /** @brief 発光三角形と太陽円盤の標本化戦略を切り替える。変更すると累積履歴を捨てる。 */
        void SetLightSampling(PathTracingLightSampling sampling) { m_LightSampling = sampling; }

        /**
         * @brief 光輸送の範囲を切り替える。変更すると累積履歴を捨てる。
         *
         * Full以外では発光三角形と太陽円盤を光源標本だけで評価する（SetLightSamplingの設定より優先）。
         */
        void SetTransportScope(PathTracingTransportScope scope) { m_TransportScope = scope; }
        PathTracingTransportScope GetTransportScope() const { return m_TransportScope; }

        /** @brief 1次光線の画素内の標本位置を切り替える。変更すると累積履歴を捨てる。 */
        void SetPixelSampling(PathTracingPixelSampling sampling) { m_PixelSampling = sampling; }
        PathTracingPixelSampling GetPixelSampling() const { return m_PixelSampling; }

        /**
         * @brief 試料の組の番号（0〜255）を切り替える。変更すると累積履歴を捨てる。
         *
         * 組ごとに試料番号の範囲をずらし、同じ画素でも独立した試料の列を引く。参照画像を独立な組に分けて
         * 描き、画素ごとの中央値で外れ値に強い推定にする検証で使う。
         */
        void SetSampleBatch(uint32_t batch) { m_SampleBatch = batch & 0xFFu; }
        uint32_t GetSampleBatch() const { return m_SampleBatch; }

        /**
         * @brief 環境マップを既定の環境光にする。Initializeで読み込み、SetEnvironmentと同じ扱いにする。
         */
        void SetEnvironmentMapSource(const PathTracingEnvironmentMapSource& source)
        {
            m_EnvironmentMapSource = source;
        }

        /**
         * @brief 1フレーム（1回のdispatch）で累積する試料数（1〜PathTracingMaxSamplesPerFrame）。
         *
         * 薄レンズとシャッター時刻はdispatchごとに1回引くため、同じフレームの試料は同じ標本を共有する。
         */
        void SetSamplesPerFrame(uint32_t samples)
        {
            m_SamplesPerFrame = samples == 0u ? 1u
                : (samples > PathTracingMaxSamplesPerFrame ? PathTracingMaxSamplesPerFrame : samples);
        }
        uint32_t GetSamplesPerFrame() const { return m_SamplesPerFrame; }

        /**
         * @brief 連番の1フレームを描く経路を設定する。変更すると累積履歴を捨てる。
         *
         * 有効なとき、シャッター区間の基準の長さは設定のフレーム長にし、絞り・ピント距離・シャッター時間を
         * 設定の値で置き換える。カメラのSequenceFrameが0でなければ、その値が変わった最初のフレームの
         * 前後のカメラ・instance変換を覚え、同じSequenceFrameの間は後続のパケットの前の値の代わりに使う。
         * 設定が無効（非有限・フレーム長0以下）なら連番の経路を使わない。
         */
        void SetSequenceFrame(const PathTracingSequenceFrameSettings& settings)
        {
            m_SequenceFrame = settings;
            if (!IsValidPathTracingSequenceFrameSettings(m_SequenceFrame))
            {
                m_SequenceFrame.bEnabled = false;
            }
            m_SequenceLatch = SequenceLatch{};
        }
        const PathTracingSequenceFrameSettings& GetSequenceFrame() const { return m_SequenceFrame; }

        /** @brief 直近フレームで光源表へ載せた点・spot・方向光の数 */
        uint32_t GetPunctualLightCount() const { return m_PunctualLightCount; }

        /** @brief 直近フレームで光源標本の対象にした発光三角形の数 */
        uint32_t GetEmissiveTriangleCount() const { return m_EmissiveTriangleCount; }

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::BufferPtr InstanceBuffer;
            uint64_t InstanceBufferCapacity = 0u;
            RHI::AccelerationStructurePtr MotionTopLevel;
            uint32_t MotionInstanceCapacity = 0u;
            /** @brief このフレームの材質texture表。先頭4要素は既定texture。 */
            Container::VariableArray<RHI::TexturePtr> MaterialTextures;
            /** @brief 点・spot・方向光の表（ラスタのLightingPassと同じ詰め方） */
            RHI::BufferPtr LightBuffer;
            uint64_t LightBufferCapacity = 0u;
            /** @brief 発光instanceの表（instance番号・三角形数・先頭三角形の通し番号） */
            RHI::BufferPtr EmissiveBuffer;
            uint64_t EmissiveBufferCapacity = 0u;
        };

        struct History
        {
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint32_t Width = 0u;
            uint32_t Height = 0u;
            uint32_t CurrentIndex = 1u;
            uint32_t SampleCount = 0u;
            /**
             * @brief 累積に使ったdispatchの数。カメラ標本（レンズ位置・シャッター時刻）の添字にする。
             *
             * 1回のdispatchで束ねた試料は同じカメラ標本を使うため、試料数ではなくdispatchごとに
             * 連続した添字でHalton列を引く。
             */
            uint32_t DispatchCount = 0u;
            /** @brief 累積中の1frameあたり試料数。変わるとカメラ標本の重みが揃わないため履歴を捨てる。 */
            uint32_t SamplesPerFrame = 0u;
            uint64_t SceneRevision = 0u;
            uint64_t LightRevision = 0u;
            uint64_t CameraSignature = 0u;
            uint64_t GeometrySignature = 0u;
            uint64_t SkySignature = 0u;
            uint64_t FogSignature = 0u;
            /** @brief 解決後の材質texture実体と各instanceの表番号の署名 */
            uint64_t MaterialTextureSignature = 0u;
            uint64_t LightSignature = 0u;
            uint64_t EnvironmentSignature = 0u;
            PathTracingDebugOutput DebugOutput = PathTracingDebugOutput::None;
            bool bSkyValid = false;
            RHI::TexturePtr Textures[2];
            RHI::ResourceState TextureStates[2] = {
                RHI::ResourceState::Undefined, RHI::ResourceState::Undefined};
            Container::VariableArray<FrameResources> FrameSlots;

            void ResetAccumulation()
            {
                SampleCount = 0u;
                DispatchCount = 0u;
            }
        };

        /** @brief 連番の1フレームの間固定する前の値（カメラとinstance変換） */
        struct SequenceLatch
        {
            /** @brief 覚えたSequenceFrame（0は未取得） */
            uint64_t Frame = 0u;
            bool bHasPreviousCamera = false;
            CameraProxy PreviousCamera;
            /** @brief instanceの並び順に、前の変換（行優先3x4）と有無 */
            Container::VariableArray<float> PreviousTransforms;
            /** @brief 覚えたときの現在の変換。並びが変わっていないことを確かめる。 */
            Container::VariableArray<float> CurrentTransforms;
            Container::VariableArray<uint8_t> bHasPreviousTransforms;
        };

        /** @brief 連番の経路でカメラのSequenceFrameが変わったら前の値を覚え直す。 */
        void UpdateSequenceLatch(const ViewRenderContext& context);
        /** @brief このフレームの前のカメラ（連番の経路では覚えた値） */
        const CameraProxy* ResolvePreviousCamera(const ViewRenderContext& context) const;
        /** @brief このフレームのinstanceの前の変換（連番の経路では覚えた値）。なければnullptr。 */
        const float* ResolvePreviousTransform(const ViewRenderContext& context,
                                              size_t instanceIndex) const;
        bool IsSequenceLatchActive(const ViewRenderContext& context) const;

        History* FindOrCreateHistory(const ViewRenderContext& context, uint32_t width, uint32_t height);
        FrameResources* FindOrCreateFrameResources(const ViewRenderContext& context,
                                                  History& history);
        bool PrepareInstances(const ViewRenderContext& context,
                              FrameResources& frameResources);
        bool PrepareLights(const ViewRenderContext& context,
                           FrameResources& frameResources);

        Container::VariableArray<History> m_Histories;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_RayGenerationShader;
        RHI::ShaderPtr m_MissShader;
        RHI::ShaderPtr m_ClosestHitShader;
        RHI::SamplerPtr m_Sampler;
        /** @brief 材質texture用のsampler（GBufferと同じWrap・異方性） */
        RHI::SamplerPtr m_MaterialSampler;
        /** @brief GBufferと同じ既定texture（白アルベド・平坦法線・metallic 0・roughness中間灰） */
        RHI::TexturePtr m_DefaultWhiteTexture;
        RHI::TexturePtr m_DefaultFlatNormalTexture;
        RHI::TexturePtr m_DefaultBlackTexture;
        RHI::TexturePtr m_DefaultMidGrayTexture;
        /** @brief ラスタと同じsplit-sum DFG LUT（多重散乱補償とエネルギー分配に使う） */
        RHI::TexturePtr m_DfgLutTexture;
        RHI::SamplerPtr m_LinearClampSampler;
        /** @brief 環境textureを使わないときに束ねる1x1の黒 */
        RHI::TexturePtr m_DefaultEnvironmentTexture;
        RHI::SamplerPtr m_EnvironmentSampler;
        PathTracingDebugOutput m_DebugOutput = PathTracingDebugOutput::None;
        PathTracingEnvironment m_Environment;
        PathTracingEnvironmentMapSource m_EnvironmentMapSource;
        RHI::TexturePtr m_EnvironmentMapTexture;
        /** @brief 検証表示modeを反映した、このフレームの環境光とBSDF */
        PathTracingEnvironment m_EffectiveEnvironment;
        PathTracingBsdfMode m_EffectiveBsdfMode = PathTracingBsdfMode::Production;
        uint32_t m_SamplesPerFrame = 1u;
        PathTracingBsdfMode m_BsdfMode = PathTracingBsdfMode::Production;
        PathTracingLightSampling m_LightSampling = PathTracingLightSampling::MultipleImportance;
        PathTracingTransportScope m_TransportScope = PathTracingTransportScope::Full;
        PathTracingPixelSampling m_PixelSampling = PathTracingPixelSampling::Box;
        uint32_t m_SampleBatch = 0u;
        PathTracingSequenceFrameSettings m_SequenceFrame;
        SequenceLatch m_SequenceLatch;
        uint32_t m_BoundMaterialTextureCount = 0u;
        uint32_t m_PunctualLightCount = 0u;
        uint32_t m_EmissiveInstanceCount = 0u;
        uint32_t m_EmissiveTriangleCount = 0u;
        uint64_t m_DeclaredLightSignature = 0u;
        uint64_t m_DeclaredEnvironmentSignature = 0u;
        bool m_bMaterialTextureOverflowReported = false;
        RGTextureHandle m_OutputHandle;
        RGTextureHandle m_SkyRadianceHandle;
        RGTextureHandle m_SkyTransmittanceHandle;
        RGTextureHandle m_SunDiskHandle;
        uint32_t m_ActiveHistoryIndex = UINT32_MAX;
        uint32_t m_ActiveFrameResourceIndex = UINT32_MAX;
        uint32_t m_TargetIndex = 0u;
        uint64_t m_DeclaredCameraSignature = 0u;
        uint64_t m_DeclaredGeometrySignature = 0u;
        uint64_t m_DeclaredSkySignature = 0u;
        uint64_t m_DeclaredFogSignature = 0u;
        uint64_t m_DeclaredMaterialTextureSignature = 0u;
        bool m_bPrepared = false;
    };
}
