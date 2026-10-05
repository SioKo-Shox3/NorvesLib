// 間接 dispatch（ICommandList::DispatchIndirect。Vulkan は vkCmdDispatchIndirect）の GPU テスト。
// 確認用の計算シェーダー（indirect_dispatch_probe.comp）を、GPU 側の引数のバッファから間接 dispatch し、引数どおりに
// スレッドグループが走ること（グループの数・走った回数・グループの番号ごとの訪問が 1 回）を読み戻して確かめる。
//   ケース A: 計算シェーダーが書いた引数（3, 2, 1）を UnorderedAccess から GenericRead へ遷移させて読む（材質のタイルの分類と同じ経路）。
//   ケース B: 引数が表の途中（エントリ 2。オフセット 64 バイト）にあるとき、そのオフセットの引数で走ること
//     （前後のエントリは (7, 7, 7) の見張り。オフセットを無視すると別の引数で走って落ちる）。
//   ケース C: x の上限を超える分を y へ広げた形（x = 4、y = 3、タイルの数 10）。消費側の番号
//     g = (z * 数y + y) * 数x + x が 0..9 を 1 回ずつ得て、10・11 のグループは上限以上で戻ること（材質の解決の求め方）。
//   ケース D: 引数の x が 0 なら、グループは 1 つも走らないこと。
//   ケース E: 転送（CopyBuffer）で書いた引数を CopyDest から IndirectArgument へ遷移させて読む。
//   ケース F: z も 1 より大きい引数（2, 2, 2）で、8 グループが 1 回ずつ走ること。
//   ケース G: 引数が不正（バッファが無い・オフセットが 4 の倍数でない・引数がバッファの外）なら false を返し、何も記録しないこと。
//   バリアの段: GenericRead・IndirectArgument が、間接引数の読み取りの段（DRAW_INDIRECT）とアクセス
//     （INDIRECT_COMMAND_READ）を含むこと。
//   どのケースも Vulkan の validation error が 0 件（引数のバッファが IndirectBuffer の用途で作られていることも検査される）。
// Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Container/Containers.h"
#include "Rendering/ShaderManager.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "IndirectDispatchVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    // 引数の表: 8 語（32 バイト）のエントリが 4 つ。材質ごとの引数（MaterialTiles::ARGS_STRIDE_WORDS）と同じ並び
    constexpr uint32_t EntryWords = 8;
    constexpr uint32_t EntryCount = 4;
    constexpr uint32_t VisitWords = 64;
    constexpr uint32_t MetaWords = 8;
    constexpr uint32_t ParamsBytes = 32;
    constexpr uint32_t SentinelWord = 7;

    int g_failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << std::endl;
            ++g_failures;
        }
    }

    bool IsGpuTestSkipForced()
    {
        char* forceSkip = nullptr;
        size_t forceSkipLength = 0;
        if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
        {
            return false;
        }
        const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        return bForceSkip;
    }

    int SkipGpuTest(const char* reason)
    {
        std::cout << TestName << " スキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // 引数の書き方。ComputeWrite = 計算シェーダーが書く（UnorderedAccess → 読む状態）、
    // TransferCopy = 転送でコピーする（CopyDest → 読む状態）
    enum class ArgsPath : uint8_t
    {
        ComputeWrite,
        TransferCopy,
    };

    struct CaseDesc
    {
        const char* Name = "";
        ArgsPath Path = ArgsPath::ComputeWrite;
        // 引数を置くエントリの番号（オフセット = Entry * 32 バイト）
        uint32_t Entry = 0;
        uint32_t X = 1;
        uint32_t Y = 1;
        uint32_t Z = 1;
        // 消費側が数える上限（これ以上の g のグループは戻る）
        uint32_t TileCount = VisitWords;
        // 間接 dispatch が読む前に、引数のバッファを遷移させる状態
        ResourceState ReadState = ResourceState::GenericRead;
    };

    struct ProbeOutputs
    {
        Container::VariableArray<uint32_t> Visits;
        Container::VariableArray<uint32_t> Meta;
        bool bRecorded = false;
    };

    // 4 つのバインディング（訪問・メタ・パラメータ・引数）を持つ記述子のセット
    RHI::DescriptorSetDesc MakeDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;
        const RHI::ResourceBindType types[] = {
            RHI::ResourceBindType::RWBuffer,       // 0 訪問
            RHI::ResourceBindType::RWBuffer,       // 1 メタ
            RHI::ResourceBindType::ConstantBuffer, // 2 パラメータ
            RHI::ResourceBindType::RWBuffer,       // 3 引数
        };
        for (uint32_t bindingIndex = 0; bindingIndex < 4u; ++bindingIndex)
        {
            RHI::DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    bool FillBuffer(const BufferPtr& buffer, uint32_t value)
    {
        uint32_t* mapped = static_cast<uint32_t*>(buffer->Map(0u, buffer->GetSize()));
        if (mapped == nullptr)
        {
            return false;
        }
        for (uint64_t word = 0; word < buffer->GetSize() / sizeof(uint32_t); ++word)
        {
            mapped[word] = value;
        }
        buffer->Unmap();
        return true;
    }

    bool ReadWords(const BufferPtr& buffer, Container::VariableArray<uint32_t>& out)
    {
        const uint32_t* mapped = static_cast<const uint32_t*>(buffer->Map(0u, buffer->GetSize()));
        if (mapped == nullptr)
        {
            return false;
        }
        out.assign(mapped, mapped + buffer->GetSize() / sizeof(uint32_t));
        buffer->Unmap();
        return true;
    }

    // 1 つのケースを、新しいバッファで記録して提出し、読み戻す。バッファ作成の失敗などは false
    bool RunCase(const DevicePtr& device, const PipelinePtr& pipeline, const CaseDesc& desc,
                 ResourceUsage argsUsage, ProbeOutputs& outputs)
    {
        const uint64_t argsBytes = static_cast<uint64_t>(EntryCount) * EntryWords * sizeof(uint32_t);
        BufferPtr visits = device->CreateBuffer(BufferDesc(VisitWords * sizeof(uint32_t),
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst, true, "IndirectProbeVisits"));
        BufferPtr meta = device->CreateBuffer(BufferDesc(MetaWords * sizeof(uint32_t),
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst, true, "IndirectProbeMeta"));
        BufferPtr args = device->CreateBuffer(BufferDesc(argsBytes, argsUsage, true, "IndirectProbeArgs"));
        BufferPtr staging = device->CreateBuffer(BufferDesc(3 * sizeof(uint32_t),
            ResourceUsage::TransferSrc, true, "IndirectProbeStaging"));
        CommandListPtr commandList = device->CreateCommandList();
        if (!visits || !meta || !args || !staging || !commandList)
        {
            std::cerr << TestName << " バッファかコマンドリストを作れませんでした" << std::endl;
            return false;
        }
        // 訪問・メタは 0、引数の表は見張り（7, 7, 7, ...）で埋める
        if (!FillBuffer(visits, 0u) || !FillBuffer(meta, 0u) || !FillBuffer(args, SentinelWord))
        {
            std::cerr << TestName << " バッファをマップできませんでした" << std::endl;
            return false;
        }
        const uint32_t argValues[3] = {desc.X, desc.Y, desc.Z};
        void* mappedStaging = staging->Map(0u, sizeof(argValues));
        if (mappedStaging == nullptr)
        {
            return false;
        }
        std::memcpy(mappedStaging, argValues, sizeof(argValues));
        staging->Unmap();

        // 引数を書くパラメータと、訪問のパラメータ。1 つのコマンドリストで 2 つの dispatch をするので別のセットにする
        BufferPtr writerUniform = device->CreateBuffer(BufferDesc(ParamsBytes, ResourceUsage::ConstantBuffer, true, "IndirectProbeWriterParams"));
        BufferPtr visitorUniform = device->CreateBuffer(BufferDesc(ParamsBytes, ResourceUsage::ConstantBuffer, true, "IndirectProbeVisitorParams"));
        DescriptorSetPtr writerSet = device->CreateDescriptorSet(MakeDescriptorSetDesc());
        DescriptorSetPtr visitorSet = device->CreateDescriptorSet(MakeDescriptorSetDesc());
        if (!writerUniform || !visitorUniform || !writerSet || !visitorSet)
        {
            std::cerr << TestName << " 定数バッファか記述子のセットを作れませんでした" << std::endl;
            return false;
        }
        const uint32_t writerParams[8] = {1u, 0u, desc.X, desc.Y, desc.Entry * EntryWords, VisitWords, desc.Z, 0u};
        const uint32_t visitorParams[8] = {0u, desc.TileCount, 0u, 0u, 0u, VisitWords, 0u, 0u};
        writerUniform->Update(writerParams, ParamsBytes);
        visitorUniform->Update(visitorParams, ParamsBytes);
        const DescriptorSetPtr sets[2] = {writerSet, visitorSet};
        const BufferPtr uniforms[2] = {writerUniform, visitorUniform};
        for (uint32_t index = 0; index < 2u; ++index)
        {
            sets[index]->BindStorageBuffer(0, visits, 0, static_cast<uint32_t>(visits->GetSize()));
            sets[index]->BindStorageBuffer(1, meta, 0, static_cast<uint32_t>(meta->GetSize()));
            sets[index]->BindConstantBuffer(2, uniforms[index], 0, ParamsBytes);
            sets[index]->BindStorageBuffer(3, args, 0, static_cast<uint32_t>(args->GetSize()));
            sets[index]->Update();
        }

        commandList->Begin();
        commandList->BufferBarrier(visits, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, visits->GetSize());
        commandList->BufferBarrier(meta, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, meta->GetSize());
        if (desc.Path == ArgsPath::ComputeWrite)
        {
            commandList->BufferBarrier(args, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, argsBytes);
            commandList->SetPipeline(pipeline);
            commandList->SetDescriptorSet(writerSet, 0);
            commandList->Dispatch(1u, 1u, 1u);
            commandList->BufferBarrier(args, ResourceState::UnorderedAccess, desc.ReadState, 0u, argsBytes);
        }
        else
        {
            commandList->BufferBarrier(staging, ResourceState::Undefined, ResourceState::CopySource, 0u, staging->GetSize());
            commandList->BufferBarrier(args, ResourceState::Undefined, ResourceState::CopyDest, 0u, argsBytes);
            commandList->CopyBuffer(staging, args, sizeof(argValues), 0u,
                                    static_cast<uint64_t>(desc.Entry) * EntryWords * sizeof(uint32_t));
            commandList->BufferBarrier(args, ResourceState::CopyDest, desc.ReadState, 0u, argsBytes);
        }
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(visitorSet, 0);
        outputs.bRecorded = commandList->DispatchIndirect(
            args, static_cast<uint64_t>(desc.Entry) * EntryWords * sizeof(uint32_t));
        commandList->BufferBarrier(visits, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, visits->GetSize());
        commandList->BufferBarrier(meta, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, meta->GetSize());
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        if (!ReadWords(visits, outputs.Visits) || !ReadWords(meta, outputs.Meta))
        {
            std::cerr << TestName << " 出力を読み戻せませんでした" << std::endl;
            return false;
        }
        return true;
    }

    // 引数 (X, Y, Z)・数える上限から期待する結果を求めて照合する
    void CheckCase(const CaseDesc& desc, const ProbeOutputs& outputs)
    {
        std::cout << TestName << " ケース " << desc.Name << ": 引数=(" << desc.X << "," << desc.Y << "," << desc.Z
                  << ") 上限=" << desc.TileCount << " 走った=" << outputs.Meta[3] << " 戻った=" << outputs.Meta[4]
                  << std::endl;
        Expect(outputs.bRecorded, "有効な引数の間接 dispatch は記録できなければならない");
        const uint32_t groups = desc.X * desc.Y * desc.Z;
        const uint32_t expectedRan = std::min(groups, desc.TileCount);
        const uint32_t expectedNumX = groups == 0 ? 0u : desc.X;
        const uint32_t expectedNumY = groups == 0 ? 0u : desc.Y;
        const uint32_t expectedNumZ = groups == 0 ? 0u : desc.Z;
        Expect(outputs.Meta[0] == expectedNumX && outputs.Meta[1] == expectedNumY && outputs.Meta[2] == expectedNumZ,
               "シェーダーが見たグループの数が引数と違う");
        Expect(outputs.Meta[3] == expectedRan, "走ったグループの数が違う");
        Expect(outputs.Meta[4] == groups - expectedRan, "上限以上で戻ったグループの数が違う");
        Expect(outputs.Meta[5] == 0u, "visits の外の番号のグループが出てはならない");
        for (uint32_t index = 0; index < VisitWords; ++index)
        {
            Expect(outputs.Visits[index] == (index < expectedRan ? 1u : 0u),
                   "グループの番号ごとの訪問が、上限未満は 1 回・以上は 0 回でなければならない");
        }
    }

    void RunCaseAndCheck(const DevicePtr& device, const PipelinePtr& pipeline, const CaseDesc& desc)
    {
        ProbeOutputs outputs;
        if (!RunCase(device, pipeline, desc, ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer |
                                                 ResourceUsage::TransferDst | ResourceUsage::ShaderRead, outputs))
        {
            ++g_failures;
            return;
        }
        CheckCase(desc, outputs);
    }

    // バリアの段: 間接引数を読む状態が、DRAW_INDIRECT の段と INDIRECT_COMMAND_READ のアクセスを含むこと
    void CheckBarrierStages()
    {
        RHI::Vulkan::ResourceBarrierTracker tracker;
        for (const ResourceState state : {ResourceState::GenericRead, ResourceState::IndirectArgument})
        {
            const bool bGeneric = state == ResourceState::GenericRead;
            Expect(static_cast<bool>(tracker.ResourceStateToAccessFlags(state) & vk::AccessFlagBits::eIndirectCommandRead),
                   bGeneric ? "GenericRead は INDIRECT_COMMAND_READ のアクセスを含まなければならない"
                            : "IndirectArgument は INDIRECT_COMMAND_READ のアクセスを含まなければならない");
            for (const bool bRayTracing : {false, true})
            {
                Expect(static_cast<bool>(tracker.ResourceStateToPipelineStageFlags(state, bRayTracing) &
                                         vk::PipelineStageFlagBits::eDrawIndirect),
                       bGeneric ? "GenericRead は DRAW_INDIRECT の段を含まなければならない"
                                : "IndirectArgument は DRAW_INDIRECT の段を含まなければならない");
            }
        }
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
        }

        CheckBarrierStages();

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManagerを初期化できませんでした" << std::endl;
            return 1;
        }

        {
            ShaderPtr shader = shaderManager.LoadShader("indirect_dispatch_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << TestName << " 確認用の計算シェーダーを読み込めませんでした" << std::endl;
                return 1;
            }
            RHI::ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
            PipelinePtr pipeline = device->CreateComputePipeline(pipelineDesc);
            if (!pipeline)
            {
                std::cerr << TestName << " 計算パイプラインを作れませんでした" << std::endl;
                return 1;
            }

            // ----- ケース A: 計算シェーダーが書いた引数を GenericRead で読む -----
            {
                CaseDesc desc;
                desc.Name = "A(計算の書き込み→GenericRead)";
                desc.X = 3;
                desc.Y = 2;
                RunCaseAndCheck(device, pipeline, desc);
            }
            // ----- ケース B: 引数が表の途中（オフセット 64 バイト）にある -----
            {
                CaseDesc desc;
                desc.Name = "B(オフセット 64)";
                desc.Entry = 2;
                desc.X = 2;
                desc.Y = 2;
                RunCaseAndCheck(device, pipeline, desc);
            }
            // ----- ケース C: x の上限を超える分を y へ広げた形 -----
            {
                CaseDesc desc;
                desc.Name = "C(yへの広げ)";
                desc.X = 4;
                desc.Y = 3;
                desc.TileCount = 10;
                RunCaseAndCheck(device, pipeline, desc);
            }
            // ----- ケース D: x が 0 -----
            {
                CaseDesc desc;
                desc.Name = "D(x=0)";
                desc.X = 0;
                desc.Y = 1;
                RunCaseAndCheck(device, pipeline, desc);
            }
            // ----- ケース E: 転送で書いた引数を IndirectArgument で読む -----
            {
                CaseDesc desc;
                desc.Name = "E(転送→IndirectArgument)";
                desc.Path = ArgsPath::TransferCopy;
                desc.Entry = 1;
                desc.X = 2;
                desc.Y = 3;
                desc.ReadState = ResourceState::IndirectArgument;
                RunCaseAndCheck(device, pipeline, desc);
            }
            // ----- ケース F: z も 1 より大きい -----
            {
                CaseDesc desc;
                desc.Name = "F(z=2)";
                desc.X = 2;
                desc.Y = 2;
                desc.Z = 2;
                RunCaseAndCheck(device, pipeline, desc);
            }

            // ----- ケース G: 不正な引数は false で何も記録しない -----
            {
                BufferPtr args = device->CreateBuffer(BufferDesc(
                    static_cast<uint64_t>(EntryCount) * EntryWords * sizeof(uint32_t),
                    ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer, true, "IndirectProbeInvalidArgs"));
                CommandListPtr commandList = device->CreateCommandList();
                if (!args || !commandList)
                {
                    std::cerr << TestName << " ケース G のバッファかコマンドリストを作れませんでした" << std::endl;
                    return 1;
                }
                // IndirectBuffer の用途が無いバッファ（VUID-vkCmdDispatchIndirect-buffer-02709 に当たる）
                BufferPtr noIndirectArgs = device->CreateBuffer(BufferDesc(
                    static_cast<uint64_t>(EntryCount) * EntryWords * sizeof(uint32_t),
                    ResourceUsage::StorageBuffer | ResourceUsage::TransferDst, true, "IndirectProbeNoIndirectUsage"));
                if (!noIndirectArgs)
                {
                    std::cerr << TestName << " ケース G の用途違いのバッファを作れませんでした" << std::endl;
                    return 1;
                }
                commandList->Begin();
                Expect(!commandList->DispatchIndirect(noIndirectArgs, 0u), "IndirectBuffer の用途が無いバッファの間接 dispatch は false でなければならない");
                Expect(!commandList->DispatchIndirect(BufferPtr{}, 0u), "バッファが無い間接 dispatch は false でなければならない");
                Expect(!commandList->DispatchIndirect(args, 2u), "オフセットが 4 の倍数でない間接 dispatch は false でなければならない");
                Expect(!commandList->DispatchIndirect(args, args->GetSize()), "引数がバッファの外にある間接 dispatch は false でなければならない");
                Expect(!commandList->DispatchIndirect(args, args->GetSize() - 4u), "引数の 12 バイトがバッファに収まらない間接 dispatch は false でなければならない");
                Expect(!commandList->DispatchIndirect(args, UINT64_MAX - 3u), "巨大なオフセットの間接 dispatch は false でなければならない");
                commandList->End();
                commandList->Submit(true);
                device->WaitIdle();
            }

            device->WaitIdle();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        Expect(validationErrorCount == 0u, "Vulkan の検証エラーが出てはならない");

        std::cout << (g_failures == 0 ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return g_failures == 0 ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
