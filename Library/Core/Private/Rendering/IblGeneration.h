// 正距円筒の放射輝度から IBL 用の資源を生成する。
#pragma once

#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class FrameResourceLane;
    class ShaderManager;

    inline constexpr uint32_t IblWorkingWidth = 256u;
    inline constexpr uint32_t IblWorkingHeight = 128u;
    inline constexpr uint32_t IblDiffuseWidth = 64u;
    inline constexpr uint32_t IblDiffuseHeight = 32u;
    inline constexpr uint32_t IblPrefilterMipCount = 9u;

    /// 正距円筒の線形放射輝度（RGBA32F、行 y = 0 が天頂）から IBL 資源を生成する。
    /// 出力は RGBA16F の環境（入力と同じ大きさ、1 ミップ）、
    /// 拡散照度（IblDiffuseWidth × IblDiffuseHeight、余弦で重み付けした全球の積分）、
    /// 鏡面の前フィルタ（IblWorkingWidth × IblWorkingHeight、IblPrefilterMipCount 段、GGX）、
    /// 強度を掛ける前の全球の平均照度である。
    /// 一定の入力では、拡散が π × 放射輝度、鏡面が放射輝度となることを照合する。
    /// 失敗した場合は出力を空、平均照度を 0 にして false を返す。
    /// ITexture::Update で同期的にアップロードするため、RenderThread でコマンド記録の外から呼ぶ。
    bool CreateIblResources(RHI::IDevice* device,
                            const Container::VariableArray<float>& sourceData,
                            uint32_t sourceWidth,
                            uint32_t sourceHeight,
                            const char* environmentName,
                            const char* diffuseName,
                            const char* prefilterName,
                            RHI::TexturePtr& outEnvironment,
                            RHI::TexturePtr& outDiffuse,
                            RHI::TexturePtr& outPrefilter,
                            float outAverageIrradiance[3]);

    /// 全ミップをゼロで初期化し、ShaderResource 状態の永続的な混ぜ先を作る。
    /// 同期的にアップロードするため、RenderThread でコマンド記録の外から呼ぶ。
    /// 作成や初期化に失敗したときは nullptr を返す。
    RHI::TexturePtr CreateSkyCloudBlendOutputTexture(RHI::IDevice* device,
                                                     uint32_t width,
                                                     uint32_t height,
                                                     uint32_t mipLevels,
                                                     const char* name);

    /// 晴天と単位曇天の入力、およびハンドルを維持する混ぜ先。
    struct SkyCloudBlendTarget
    {
        RHI::TexturePtr Clear;
        RHI::TexturePtr UnitOvercast;
        RHI::TexturePtr Output;
    };

    /// フレームの持ち主から定数と記述子を借り、空と IBL の混ぜを記録する。
    class SkyCloudBlendRecorder final
    {
    public:
        SkyCloudBlendRecorder() = default;
        SkyCloudBlendRecorder(const SkyCloudBlendRecorder&) = delete;
        SkyCloudBlendRecorder& operator=(const SkyCloudBlendRecorder&) = delete;

        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const;

        /// グラフの Declare で呼び、各出力の全ミップについて定数と記述子を借りて束ねる。
        /// 入力は同じ大きさで、単位曇天のミップ数は晴天以上、出力のミップ数は晴天と同じにする。
        /// 出力は RGBA16F の読み書き可能な 2D テクスチャとし、いずれの入力とも重複させない。
        /// 出力同士も重複させない。空の入力集合は準備する仕事がない成功として扱う。
        /// 晴天の重みが 1 かつ曇天の重みが全成分 0 のとき、シェーダーに晴天を写す印を渡す。
        /// 再度呼ぶと前の準備は捨て、失敗時も準備を空にする。
        bool Prepare(FrameResourceLane& lane,
                     const SkyCloudBlendTarget* targets,
                     size_t targetCount,
                     float clearWeight,
                     const Math::Vector3& overcastWeight);

        bool HasPrepared() const;

        /// 借りたフレームの Execute で呼ぶ。入力は ShaderResource のまま読み、
        /// グラフが UnorderedAccess に移した出力へ書き、全出力を ShaderResource に戻す。
        /// 呼び手はグラフに書き込みと最終状態を宣言する。記録後は準備を空にする。
        void Record(RHI::ICommandList& commands);

    private:
        struct PreparedDispatch
        {
            RHI::DescriptorSetPtr DescriptorSet;
            uint32_t Width = 0u;
            uint32_t Height = 0u;
        };

        RHI::ShaderPtr m_Shader;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_Sampler;
        Container::VariableArray<PreparedDispatch> m_Dispatches;
        Container::VariableArray<RHI::TexturePtr> m_Outputs;
    };
}
