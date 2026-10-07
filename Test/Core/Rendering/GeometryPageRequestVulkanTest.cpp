// ジオメトリのページの要求（Common/MegaGeometryCull.glsl のページの常駐の判定と RequestPage）を、実際のカリング
// （cluster_cull.comp）で確かめる。合成したクラスタ（根の親 3 つ。それぞれ子のクラスタ 4 つと、子の置かれたページを持つ）と
// ページの表を GPU に置いて 1 回の dispatch で判定し、描かれたクラスタと要求の列を読み戻す。
//
// 1) 子のページが全部常駐: 子だけが描かれ、要求は無い。
// 2) 2 つの親の子が同じページで、そのページが非常駐: 穴を作らず 2 つの親が描かれ、要求は 1 件（重複を省く）。
//    子のページが常駐している親は、子が描かれる。
// 3) 要求の列の容量が 0: 描かれるクラスタは同じで、要求は書かれない。
// 4) 2 つのページが非常駐で容量が 1: 件数は 2、溢れは 1 になり、書かれた要求は非常駐のページのどちらか。
// 5) 自分の誤差が許容に収まる（遠い）なら、子のページが非常駐でも親が普通に描かれ、要求は出ない。
// 6) 同じフレームの印で 2 回続けて判定すると、2 回目は要求を重ねて積まない。新しい印では積む。
// 7) 2 パス目: 1 パス目で描いたクラスタは、2 パス目で遮蔽と判定されても自分のページの使用の印を出す。
//    1 パス目で描かなかった（前のフレームで見えなかった）遮蔽されたクラスタは出さない。
// 描いたクラスタは、自分のページへも使用の印を要求の列に出す（ホストの LRU を使われた順にするため。常駐でも出る）。
// 各場合の期待は手で書いた集合で、CPU の写し（DecideBakedCluster）の結果とも一致すること。
// 8) BVH をたどる cluster_bvh_cull.comp でも、1 パス目 → 2 パス目の順に実行し、2 パス目で節ごと遮蔽されて枝が
//    打ち切られても、1 パス目で描いたクラスタの自分のページの使用の印が残る。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;

    constexpr const char* TestName = "GeometryPageRequestVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;

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

    // cluster_cull.comp の CullUniforms（std140）と同じ並び。MegaGeometryPass の CullUniformData と同じ内容
    struct alignas(16) CullUniforms
    {
        float ViewMatrix[16];
        float ProjectionMatrix[16];
        float CameraPosition[4];
        float FrustumPlanes[6][4];
        uint32_t InstanceCount;
        uint32_t TotalGroupCount;
        float LODBias;
        float ScreenHeight;
        float ProjectionFactor;
        uint32_t HiZWidth;
        uint32_t HiZHeight;
        uint32_t HiZMipCount;
        uint32_t bHiZEnabled;
        uint32_t DebugPayloadMode;
        uint32_t CullPass;
        uint32_t bStatsEnabled;
        uint32_t SectionBase;
        uint32_t VisibleReadStamp;
        uint32_t VisibleWriteStamp;
        uint32_t BvhStage;
        uint32_t BvhInputBase;
        uint32_t BvhNextBase;
        uint32_t BvhLeafBase;
        uint32_t BvhRootCount;
        uint32_t PageRequestCapacity;
        uint32_t bSwRasterEnabled;
        uint32_t SwRasterCapacity;
        float SwRasterMaxPixels;
        float SwRasterNearPlane;
        uint32_t OrthoLod; // 0 = 透視（主の経路）。1 は VSM の影（このテストは使わない）
        uint32_t OrthoReserved[3];
    };

    // MegaInstance（192 バイト）と同じ並び
    struct TestInstance
    {
        float World[16];
        float PreviousWorld[16];
        float LODSphere[4];
        uint32_t ClusterInfo[4]; // アドレスの下位・上位、クラスタ数、最初のワークグループの番号
        uint32_t DrawInfo[4];    // 区間、頂点の基点、インデックスの基点、見えた印の先頭
        uint32_t BvhInfo[4];     // BVH のアドレス（0）、節の数、ページの表の先頭
    };
    static_assert(sizeof(TestInstance) == 192, "MegaInstance と大きさが一致しません");

    constexpr uint32_t ClusterCount = 15;
    constexpr uint32_t PageCount = 4;
    constexpr uint32_t RequestCapacity = 8;
    constexpr uint32_t CommandCapacity = 32;
    constexpr uint32_t BvhQueueBytes = 64;
    constexpr uint32_t BvhCounterBytes = 256;

    struct Fixture
    {
        DevicePtr Device;
        PipelinePtr Pipeline;
        TexturePtr HiZ;
        SamplerPtr Sampler;
        BufferPtr Uniform;
        BufferPtr Instances;
        BufferPtr Clusters;
        BufferPtr Commands;
        BufferPtr Counts;
        BufferPtr Visible;
        BufferPtr Stats;
        BufferPtr Sections;
        BufferPtr DrawInfos;
        BufferPtr BvhQueue;
        BufferPtr BvhCounters;
        BufferPtr PageTable;
        BufferPtr PageRequests;
        CullUniforms Base{};
        Mega::BakedLODView CpuView;
        Mega::MeshCluster CpuClusters[ClusterCount];
    };

    DescriptorSetDesc MakeCullDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {
            ResourceBindType::ConstantBuffer,       // 0 カリング用ユニフォーム
            ResourceBindType::StructuredBuffer,     // 1 インスタンスの表
            ResourceBindType::RWBuffer,             // 2 IndirectDraw コマンド
            ResourceBindType::RWBuffer,             // 3 区間ごとのカウンタ
            ResourceBindType::CombinedImageSampler, // 4 Hi-Z
            ResourceBindType::RWBuffer,             // 5 見えた印
            ResourceBindType::RWBuffer,             // 6 統計
            ResourceBindType::StructuredBuffer,     // 7 区間の表
            ResourceBindType::RWBuffer,             // 8 描画情報
            ResourceBindType::RWBuffer,             // 9 BVH の列
            ResourceBindType::RWBuffer,             // 10 BVH のカウンタ
            ResourceBindType::RWBuffer,             // 11 ページの表
            ResourceBindType::RWBuffer,             // 12 ページの要求
            ResourceBindType::RWBuffer,             // 13 ソフトウェアラスタの一覧（このテストは振り分けない）
        };
        for (uint32_t bindingIndex = 0; bindingIndex < 14u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    BufferPtr CreateHostBuffer(const DevicePtr& device, uint64_t bytes, ResourceUsage usage, const char* name)
    {
        BufferPtr buffer = device->CreateBuffer(BufferDesc(bytes, usage, true, name));
        if (buffer)
        {
            void* mapped = buffer->Map(0u, bytes);
            if (mapped == nullptr)
            {
                return nullptr;
            }
            std::memset(mapped, 0, static_cast<size_t>(bytes));
            buffer->Unmap();
        }
        return buffer;
    }

    void WriteBuffer(const BufferPtr& buffer, const void* data, uint64_t bytes)
    {
        void* mapped = buffer->Map(0u, bytes);
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        buffer->Unmap();
    }

    BoundingSphere MakeSphere(float x, float y, float z, float radius)
    {
        BoundingSphere sphere;
        sphere.CenterX = x;
        sphere.CenterY = y;
        sphere.CenterZ = z;
        sphere.Radius = radius;
        return sphere;
    }

    // 根の親(P)1つと、その子(C 4つ)。親は自分の誤差が大きく、子は誤差 0 で親のグループの球と誤差を持つ。
    // 親はルートのページ 0 にあり、子は childPage にある（親の ChildPageId）。
    void AddFamily(Mega::MeshCluster* clusters, uint32_t& cursor, uint32_t groupId, float x, float y, float z,
                   uint32_t childPage)
    {
        const float parentError = 0.02f;
        Mega::MeshCluster parent;
        parent.Bounds = MakeSphere(x, y, z, 2.0f);
        parent.LODLevel = 1;
        parent.LODError = parentError;
        parent.GroupId = Mega::INVALID_CLUSTER_GROUP_ID;
        parent.ParentError = 3.402823466e+38f;
        parent.PageId = 0;
        parent.ChildPageId = childPage;
        parent.ConeCutoff = -1.0f;
        parent.IndexCount = 3;
        clusters[cursor++] = parent;
        for (uint32_t member = 0; member < 4; ++member)
        {
            Mega::MeshCluster child;
            child.Bounds = MakeSphere(x + ((member % 2) ? 0.8f : -0.8f), y + ((member / 2) ? 0.8f : -0.8f), z, 1.0f);
            child.LODLevel = 0;
            child.LODError = 0.0f;
            child.ParentBounds = parent.Bounds;
            child.ParentError = parentError;
            child.GroupId = groupId;
            child.PageId = childPage;
            child.ChildPageId = Mega::INVALID_PAGE_ID;
            child.ConeCutoff = -1.0f;
            child.IndexCount = 3;
            clusters[cursor++] = child;
        }
    }

    Mega::GPUClusterData ToGpu(const Mega::MeshCluster& cluster)
    {
        Mega::GPUClusterData gpu{};
        gpu.BoundsCenterX = cluster.Bounds.CenterX;
        gpu.BoundsCenterY = cluster.Bounds.CenterY;
        gpu.BoundsCenterZ = cluster.Bounds.CenterZ;
        gpu.BoundsRadius = cluster.Bounds.Radius;
        gpu.ConeCutoff = cluster.ConeCutoff;
        gpu.IndexCount = cluster.IndexCount;
        gpu.LODLevel = cluster.LODLevel;
        gpu.LODError = cluster.LODError;
        gpu.Flags = Mega::GPU_CLUSTER_FLAG_BAKED_LOD;
        gpu.ParentCenterX = cluster.ParentBounds.CenterX;
        gpu.ParentCenterY = cluster.ParentBounds.CenterY;
        gpu.ParentCenterZ = cluster.ParentBounds.CenterZ;
        gpu.ParentRadius = cluster.ParentBounds.Radius;
        gpu.ParentError = cluster.ParentError;
        gpu.GroupId = cluster.GroupId;
        gpu.PageId = cluster.PageId;
        gpu.ChildPageId = cluster.ChildPageId;
        return gpu;
    }

    struct CaseResult
    {
        uint32_t DrawnCount = 0;
        VariableArray<uint32_t> Drawn;    // 描かれたクラスタの番号（昇順）
        uint32_t RequestCount = 0;        // 要求の列の [0]
        uint32_t RequestOverflow = 0;     // [1]
        VariableArray<uint32_t> Requests; // 書かれた要求（容量まで）
        VariableArray<Mega::GeometryPageTable::Entry> Table;
    };

    // ページの表・要求の列・描画の書き込み先を初期化する（regions は区画。PAGE_NON_RESIDENT で非常駐、stamps は表に残す要求の印）
    void ResetBuffers(Fixture& fixture, const uint32_t (&regions)[PageCount], const uint32_t (&stamps)[PageCount],
                      uint32_t requestCapacity)
    {
        Mega::GeometryPageTable::Entry table[PageCount];
        for (uint32_t page = 0; page < PageCount; ++page)
        {
            table[page].Region = regions[page];
            table[page].RequestStamp = stamps[page];
        }
        WriteBuffer(fixture.PageTable, table, sizeof(table));
        {
            uint32_t requestWords[Mega::GeometryPageRequestBuffer::HeaderWords + RequestCapacity] = {};
            requestWords[Mega::GeometryPageRequestBuffer::CapacityWord] = requestCapacity;
            WriteBuffer(fixture.PageRequests, requestWords, sizeof(requestWords));
        }
        {
            VariableArray<uint32_t> zero(CommandCapacity * 5u, 0u);
            WriteBuffer(fixture.Commands, zero.data(), zero.size() * sizeof(uint32_t));
            uint32_t zeroCounts[4] = {};
            WriteBuffer(fixture.Counts, zeroCounts, sizeof(zeroCounts));
            VariableArray<uint32_t> zeroInfos(CommandCapacity * 2u, 0u);
            WriteBuffer(fixture.DrawInfos, zeroInfos.data(), zeroInfos.size() * sizeof(uint32_t));
        }
    }

    // 1 回の dispatch を実行して完了を待つ（書き込み先は初期化済みで、dispatch の間は持ち越す）
    bool DispatchOnce(Fixture& fixture, const PipelinePtr& pipeline, const CullUniforms& uniforms, uint32_t groupCount)
    {
        WriteBuffer(fixture.Uniform, &uniforms, sizeof(CullUniforms));

        DescriptorSetPtr descriptorSet = fixture.Device->CreateDescriptorSet(MakeCullDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindConstantBuffer(0u, fixture.Uniform, 0u, static_cast<uint32_t>(sizeof(CullUniforms)));
        descriptorSet->BindStorageBuffer(1u, fixture.Instances, 0u, static_cast<uint32_t>(sizeof(TestInstance)));
        descriptorSet->BindStorageBuffer(2u, fixture.Commands, 0u, CommandCapacity * 5u * 4u);
        descriptorSet->BindStorageBuffer(3u, fixture.Counts, 0u, 16u);
        descriptorSet->BindTexture(4u, fixture.HiZ);
        descriptorSet->BindSampler(4u, fixture.Sampler);
        descriptorSet->BindStorageBuffer(5u, fixture.Visible, 0u, ClusterCount * 4u);
        descriptorSet->BindStorageBuffer(6u, fixture.Stats, 0u, 48u);
        descriptorSet->BindStorageBuffer(7u, fixture.Sections, 0u, 8u);
        descriptorSet->BindStorageBuffer(8u, fixture.DrawInfos, 0u, CommandCapacity * 8u);
        descriptorSet->BindStorageBuffer(9u, fixture.BvhQueue, 0u, BvhQueueBytes);
        descriptorSet->BindStorageBuffer(10u, fixture.BvhCounters, 0u, BvhCounterBytes);
        descriptorSet->BindStorageBuffer(11u, fixture.PageTable, 0u, sizeof(Mega::GeometryPageTable::Entry) * PageCount);
        descriptorSet->BindStorageBuffer(12u, fixture.PageRequests,
                                         0u,
                                         static_cast<uint32_t>(Mega::GeometryPageRequestBuffer::GetBufferBytes(RequestCapacity)));
        descriptorSet->BindStorageBuffer(13u, fixture.Stats, 0u, 48u);
        descriptorSet->Update();

        CommandListPtr commandList = fixture.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        const BufferPtr written[] = {fixture.Commands,     fixture.Counts,   fixture.DrawInfos,  fixture.PageTable,
                                     fixture.PageRequests, fixture.Visible,  fixture.BvhQueue,   fixture.BvhCounters};
        commandList->Begin();
        for (const BufferPtr& buffer : written)
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0);
        commandList->Dispatch(groupCount, 1u, 1u);
        for (const BufferPtr& buffer : written)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        fixture.Device->WaitIdle();
        return true;
    }

    // 描かれたクラスタ・要求の列・ページの表を読み戻す
    bool ReadResult(Fixture& fixture, uint32_t requestCapacity, CaseResult& result)
    {
        result = CaseResult{};
        {
            const uint32_t* counts = static_cast<const uint32_t*>(fixture.Counts->Map(0u, 16u));
            result.DrawnCount = counts != nullptr ? counts[0] : 0xFFFFFFFFu;
            fixture.Counts->Unmap();
            const uint32_t* infos = static_cast<const uint32_t*>(fixture.DrawInfos->Map(0u, CommandCapacity * 8u));
            for (uint32_t index = 0; infos != nullptr && index < std::min(result.DrawnCount, CommandCapacity); ++index)
            {
                result.Drawn.push_back(infos[index * 2u + 1u]); // payload = クラスタの番号
            }
            fixture.DrawInfos->Unmap();
            std::sort(result.Drawn.begin(), result.Drawn.end());
        }
        {
            const uint32_t bytes = static_cast<uint32_t>(Mega::GeometryPageRequestBuffer::GetBufferBytes(RequestCapacity));
            const uint32_t* words = static_cast<const uint32_t*>(fixture.PageRequests->Map(0u, bytes));
            if (words == nullptr)
            {
                return false;
            }
            result.RequestCount = words[Mega::GeometryPageRequestBuffer::CountWord];
            result.RequestOverflow = words[Mega::GeometryPageRequestBuffer::OverflowWord];
            const uint32_t stored = std::min(result.RequestCount, requestCapacity);
            for (uint32_t index = 0; index < stored; ++index)
            {
                result.Requests.push_back(words[Mega::GeometryPageRequestBuffer::HeaderWords + index]);
            }
            std::sort(result.Requests.begin(), result.Requests.end());
            fixture.PageRequests->Unmap();
        }
        {
            const Mega::GeometryPageTable::Entry* entries = static_cast<const Mega::GeometryPageTable::Entry*>(
                fixture.PageTable->Map(0u, sizeof(Mega::GeometryPageTable::Entry) * PageCount));
            for (uint32_t page = 0; entries != nullptr && page < PageCount; ++page)
            {
                result.Table.push_back(entries[page]);
            }
            fixture.PageTable->Unmap();
        }
        return true;
    }

    // 平らな判定（cluster_cull.comp）を 1 回の dispatch で実行して読み戻す
    bool RunCase(Fixture& fixture, const uint32_t (&regions)[PageCount], const uint32_t (&stamps)[PageCount],
                 float lodBias, uint32_t requestCapacity, uint32_t writeStamp, CaseResult& result)
    {
        ResetBuffers(fixture, regions, stamps, requestCapacity);
        CullUniforms uniforms = fixture.Base;
        uniforms.LODBias = lodBias;
        uniforms.PageRequestCapacity = requestCapacity;
        uniforms.VisibleWriteStamp = writeStamp;
        return DispatchOnce(fixture, fixture.Pipeline, uniforms, 1u) && ReadResult(fixture, requestCapacity, result);
    }

    // CPU の写し（DecideBakedCluster）で、同じ常駐のときに描かれるクラスタと要求するページ（子の要求と使用の印）を求める
    void CpuExpectation(const Fixture& fixture, const uint32_t (&regions)[PageCount], float lodBias,
                        VariableArray<uint32_t>& outDrawn, VariableArray<uint32_t>& outPages)
    {
        const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        Mega::BakedLODView view = fixture.CpuView;
        view.LODBias = lodBias;
        const auto isResident = [&regions](uint32_t page) { return regions[page] != Mega::PAGE_NON_RESIDENT; };
        outDrawn.clear();
        outPages.clear();
        for (uint32_t index = 0; index < ClusterCount; ++index)
        {
            const Mega::BakedClusterDecision decision =
                Mega::DecideBakedCluster(fixture.CpuClusters[index], world, view, isResident);
            if (decision == Mega::BakedClusterDecision::NotDrawn)
            {
                continue;
            }
            outDrawn.push_back(index);
            // 描いたクラスタの自分のページへの使用の印
            const uint32_t ownPage = fixture.CpuClusters[index].PageId;
            if (std::find(outPages.begin(), outPages.end(), ownPage) == outPages.end())
            {
                outPages.push_back(ownPage);
            }
            if (decision == Mega::BakedClusterDecision::DrawnForMissingChild)
            {
                const uint32_t page = fixture.CpuClusters[index].ChildPageId;
                if (std::find(outPages.begin(), outPages.end(), page) == outPages.end())
                {
                    outPages.push_back(page);
                }
            }
        }
        std::sort(outPages.begin(), outPages.end());
    }

    template <typename Array>
    bool SameValues(const Array& actual, std::initializer_list<uint32_t> expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }
        size_t index = 0;
        for (const uint32_t value : expected)
        {
            if (actual[index++] != value)
            {
                return false;
            }
        }
        return true;
    }

    void PrintValues(const char* label, const VariableArray<uint32_t>& values)
    {
        std::cerr << "  " << label << "=[";
        for (size_t index = 0; index < values.size(); ++index)
        {
            std::cerr << (index ? "," : "") << values[index];
        }
        std::cerr << "]\n";
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が設定されています");
        }

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
            std::cerr << "ShaderManagerを初期化できませんでした\n";
            return 1;
        }

        bool bPassed = true;
        {
            // BVH をたどるカリングが同じ共通の関数を取り込んでコンパイルできる
            ShaderPtr bvhShader = shaderManager.LoadShader("cluster_bvh_cull.comp", RHI::ShaderStage::Compute);
            if (!bvhShader)
            {
                std::cerr << "cluster_bvh_cull.comp をコンパイルできませんでした\n";
                bPassed = false;
            }

            ShaderPtr shader = shaderManager.LoadShader("cluster_cull.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << "cluster_cull.comp をコンパイルできませんでした\n";
                return 1;
            }
            Fixture fixture;
            fixture.Device = device;
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeCullDescriptorSetDesc());
            fixture.Pipeline = device->CreateComputePipeline(pipelineDesc);
            PipelinePtr bvhPipeline;
            if (bvhShader)
            {
                ComputePipelineDesc bvhPipelineDesc;
                bvhPipelineDesc.computeShader = bvhShader;
                bvhPipelineDesc.descriptorSetLayouts.push_back(MakeCullDescriptorSetDesc());
                bvhPipeline = device->CreateComputePipeline(bvhPipelineDesc);
                if (!bvhPipeline)
                {
                    std::cerr << "BVH のカリングのパイプラインを作れませんでした\n";
                    bPassed = false;
                }
            }

            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            fixture.Sampler = device->CreateSampler(samplerDesc);
            {
                TextureDesc desc;
                desc.Width = 2;
                desc.Height = 2;
                desc.MipLevels = 1;
                desc.TextureFormat = Format::R32_FLOAT;
                desc.Usage = ResourceUsage::ShaderRead;
                desc.DebugName = "GeometryPageRequestHiZ";
                fixture.HiZ = device->CreateTexture(desc);
                const float depth[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                if (fixture.HiZ)
                {
                    fixture.HiZ->Update(depth, 8u, 16u, 0, 0);
                }
            }

            // クラスタ: 家族 A(中心 z=10・子のページ 2)・B(z=12・子のページ 2)・C(z=14・子のページ 3)
            uint32_t cursor = 0;
            AddFamily(fixture.CpuClusters, cursor, 0, 0.0f, 0.0f, 10.0f, 2);
            AddFamily(fixture.CpuClusters, cursor, 1, 6.0f, 0.0f, 12.0f, 2);
            AddFamily(fixture.CpuClusters, cursor, 2, -6.0f, 0.0f, 14.0f, 3);
            if (cursor != ClusterCount)
            {
                std::cerr << "クラスタの数が合いません\n";
                return 1;
            }
            Mega::GPUClusterData gpuClusters[ClusterCount];
            for (uint32_t index = 0; index < ClusterCount; ++index)
            {
                gpuClusters[index] = ToGpu(fixture.CpuClusters[index]);
            }

            const ResourceUsage storage = ResourceUsage::StorageBuffer;
            fixture.Uniform = CreateHostBuffer(device, sizeof(CullUniforms), ResourceUsage::ConstantBuffer, "GeometryPageRequestUBO");
            fixture.Instances = CreateHostBuffer(device, sizeof(TestInstance), storage, "GeometryPageRequestInstances");
            fixture.Clusters = CreateHostBuffer(device, sizeof(gpuClusters), storage | ResourceUsage::BufferDeviceAddress,
                                                "GeometryPageRequestClusters");
            fixture.Commands = CreateHostBuffer(device, CommandCapacity * 5u * 4u, storage, "GeometryPageRequestCommands");
            fixture.Counts = CreateHostBuffer(device, 16u, storage, "GeometryPageRequestCounts");
            fixture.Visible = CreateHostBuffer(device, ClusterCount * 4u, storage, "GeometryPageRequestVisible");
            fixture.Stats = CreateHostBuffer(device, 48u, storage, "GeometryPageRequestStats");
            fixture.Sections = CreateHostBuffer(device, 8u, storage, "GeometryPageRequestSections");
            fixture.DrawInfos = CreateHostBuffer(device, CommandCapacity * 8u, storage, "GeometryPageRequestDrawInfos");
            fixture.BvhQueue = CreateHostBuffer(device, BvhQueueBytes, storage, "GeometryPageRequestBvhQueue");
            fixture.BvhCounters = CreateHostBuffer(device, BvhCounterBytes, storage, "GeometryPageRequestBvhCounters");
            fixture.PageTable = CreateHostBuffer(device, sizeof(Mega::GeometryPageTable::Entry) * PageCount, storage,
                                                 "GeometryPageRequestTable");
            fixture.PageRequests = CreateHostBuffer(device, Mega::GeometryPageRequestBuffer::GetBufferBytes(RequestCapacity), storage,
                                                    "GeometryPageRequestList");
            if (!fixture.Pipeline || !fixture.Sampler || !fixture.HiZ || !fixture.Uniform || !fixture.Instances ||
                !fixture.Clusters || !fixture.Commands || !fixture.Counts || !fixture.Visible || !fixture.Stats ||
                !fixture.Sections || !fixture.DrawInfos || !fixture.BvhQueue || !fixture.BvhCounters || !fixture.PageTable ||
                !fixture.PageRequests)
            {
                std::cerr << "確認用の資源を作れませんでした\n";
                return 1;
            }
            const uint64_t clusterAddress = fixture.Clusters->GetDeviceAddress();
            if (clusterAddress == 0)
            {
                return SkipGpuTest("バッファのデバイスアドレスを利用できません");
            }
            WriteBuffer(fixture.Clusters, gpuClusters, sizeof(gpuClusters));

            // 1 つのインスタンス（単位行列）。ページの表の先頭は 0
            TestInstance instance{};
            for (uint32_t axis = 0; axis < 4; ++axis)
            {
                instance.World[axis * 4 + axis] = 1.0f;
                instance.PreviousWorld[axis * 4 + axis] = 1.0f;
            }
            instance.ClusterInfo[0] = static_cast<uint32_t>(clusterAddress & 0xFFFFFFFFull);
            instance.ClusterInfo[1] = static_cast<uint32_t>(clusterAddress >> 32);
            instance.ClusterInfo[2] = ClusterCount;
            instance.ClusterInfo[3] = 0;
            instance.BvhInfo[3] = 0;
            WriteBuffer(fixture.Instances, &instance, sizeof(instance));
            const uint32_t section[2] = {0u, CommandCapacity};
            WriteBuffer(fixture.Sections, section, sizeof(section));

            // 原点から +Z を見るカメラ（実際の描画と同じ、デバイスの規約に合わせた射影）
            CameraProxy camera;
            camera.PositionX = 0.0f;
            camera.PositionY = 0.0f;
            camera.PositionZ = 0.0f;
            camera.ForwardX = 0.0f;
            camera.ForwardY = 0.0f;
            camera.ForwardZ = 1.0f;
            camera.UpX = 0.0f;
            camera.UpY = 1.0f;
            camera.UpZ = 0.0f;
            camera.FieldOfView = 60.0f;
            camera.NearPlane = 0.1f;
            camera.FarPlane = 1000.0f;
            const float aspect = 16.0f / 9.0f;
            const CameraViewConstants constants = CameraViewConstants::BuildForDevice(camera, aspect, device.get());
            constants.CopyShaderView(fixture.Base.ViewMatrix);
            constants.CopyShaderProjection(fixture.Base.ProjectionMatrix);
            constants.CopyCameraPosition(fixture.Base.CameraPosition);
            for (auto& plane : fixture.Base.FrustumPlanes)
            {
                plane[0] = plane[1] = plane[2] = 0.0f;
                plane[3] = 1.0f; // 全てを通す
            }
            const float tanHalfY = std::tan(constants.FieldOfViewRadians * 0.5f);
            fixture.Base.InstanceCount = 1;
            fixture.Base.TotalGroupCount = 1;
            fixture.Base.ScreenHeight = 1080.0f;
            fixture.Base.ProjectionFactor = 1080.0f / (2.0f * tanHalfY);
            fixture.Base.DebugPayloadMode = 1; // 描画情報の payload = クラスタの番号
            fixture.Base.CullPass = 0;         // 遮蔽の判定なしの 1 回の判定
            fixture.Base.BvhStage = 0xFFFFFFFFu;
            fixture.CpuView.CameraPosition[2] = 0.0f;
            fixture.CpuView.Forward[2] = 1.0f;
            fixture.CpuView.TanHalfFovY = tanHalfY;
            fixture.CpuView.TanHalfFovX = tanHalfY * aspect;
            fixture.CpuView.ProjectionFactor = fixture.Base.ProjectionFactor;

            const uint32_t R = 0u; // 常駐(区画 0)
            const uint32_t N = Mega::PAGE_NON_RESIDENT;
            const uint32_t noStamps[PageCount] = {0, 0, 0, 0};

            struct Scenario
            {
                const char* Name;
                uint32_t Regions[PageCount];
                float LodBias;
                uint32_t Capacity;
                std::initializer_list<uint32_t> Drawn;
                uint32_t ExpectedRequestCount;
                uint32_t ExpectedOverflow;
                std::initializer_list<uint32_t> Requests; // 容量が足りるときの書かれた要求（ページの表の位置）
            };
            // クラスタの番号: A = 0(親) 1..4(子)、B = 5(親) 6..9(子)、C = 10(親) 11..14(子)
            const Scenario scenarios[] = {
                // 要求 = 子のページが非常駐のための要求と、描いたクラスタの自分のページへの使用の印（根の親はページ 0）
                {"全て常駐(使用の印だけ)", {R, R, R, R}, 1.0f, RequestCapacity, {1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14}, 2, 0, {2, 3}},
                {"ページ2が非常駐(AとBの子)", {R, R, N, R}, 1.0f, RequestCapacity, {0, 5, 11, 12, 13, 14}, 3, 0, {0, 2, 3}},
                {"要求の容量が0", {R, R, N, R}, 1.0f, 0, {0, 5, 11, 12, 13, 14}, 0, 0, {}},
                {"ページ2と3が非常駐", {R, R, N, N}, 1.0f, RequestCapacity, {0, 5, 10}, 3, 0, {0, 2, 3}},
                {"ページ2と3が非常駐・容量1", {R, R, N, N}, 1.0f, 1, {0, 5, 10}, 3, 2, {}},
                {"誤差が許容に収まる(遠い)", {R, R, N, N}, 1000.0f, RequestCapacity, {0, 5, 10}, 1, 0, {0}},
            };

            uint32_t stampCounter = 100;
            for (const Scenario& scenario : scenarios)
            {
                CaseResult result;
                if (!RunCase(fixture, scenario.Regions, noStamps, scenario.LodBias, scenario.Capacity, ++stampCounter, result))
                {
                    return 1;
                }
                VariableArray<uint32_t> cpuDrawn;
                VariableArray<uint32_t> cpuPages;
                CpuExpectation(fixture, scenario.Regions, scenario.LodBias, cpuDrawn, cpuPages);

                bool bCase = SameValues(result.Drawn, scenario.Drawn) && result.DrawnCount == scenario.Drawn.size() &&
                             SameValues(cpuDrawn, scenario.Drawn) && result.RequestCount == scenario.ExpectedRequestCount &&
                             result.RequestOverflow == scenario.ExpectedOverflow;
                if (scenario.Capacity == RequestCapacity)
                {
                    bCase = bCase && SameValues(result.Requests, scenario.Requests) && SameValues(cpuPages, scenario.Requests);
                }
                else if (scenario.Capacity == 1)
                {
                    // 書かれた 1 件は、要求されたページ(0・2・3)のどれか
                    bCase = bCase && result.Requests.size() == 1 &&
                            (result.Requests[0] == 0u || result.Requests[0] == 2u || result.Requests[0] == 3u);
                }
                else
                {
                    bCase = bCase && result.Requests.empty();
                }
                // 要求の印は、要求したページにだけ今のフレームの印が残る
                for (uint32_t page = 0; page < PageCount; ++page)
                {
                    const bool bRequested = std::find(result.Requests.begin(), result.Requests.end(), page) != result.Requests.end();
                    const bool bStamped = result.Table[page].RequestStamp == stampCounter;
                    bCase = bCase && (scenario.Capacity == 0 ? !bStamped : (bRequested == bStamped || scenario.Capacity == 1)) &&
                            result.Table[page].Region == scenario.Regions[page];
                }
                std::cout << "ケース「" << scenario.Name << "」描画=" << result.DrawnCount << " 要求=" << result.RequestCount
                          << " 溢れ=" << result.RequestOverflow << (bCase ? " OK" : " NG") << '\n';
                if (!bCase)
                {
                    std::cerr << "ケース「" << scenario.Name << "」が期待と違います\n";
                    PrintValues("描かれたクラスタ", result.Drawn);
                    PrintValues("CPU の写し", cpuDrawn);
                    PrintValues("要求", result.Requests);
                    bPassed = false;
                }
            }

            // 同じフレームの印で続けて判定すると、2 回目は積まない。新しい印では積む
            {
                const uint32_t regions[PageCount] = {R, R, N, N};
                CaseResult first;
                CaseResult second;
                CaseResult third;
                bool bRan = RunCase(fixture, regions, noStamps, 1.0f, RequestCapacity, 500, first);
                // 1 回目が表に残した印を持ち越す
                uint32_t carried[PageCount] = {};
                for (uint32_t page = 0; bRan && page < PageCount; ++page)
                {
                    carried[page] = first.Table[page].RequestStamp;
                }
                bRan = bRan && RunCase(fixture, regions, carried, 1.0f, RequestCapacity, 500, second);
                bRan = bRan && RunCase(fixture, regions, carried, 1.0f, RequestCapacity, 501, third);
                if (!bRan)
                {
                    return 1;
                }
                const bool bDedupe = first.RequestCount == 3 && second.RequestCount == 0 && third.RequestCount == 3 &&
                                     SameValues(third.Requests, {0, 2, 3});
                std::cout << "ケース「同じ印で続けて判定」1回目=" << first.RequestCount << " 2回目=" << second.RequestCount
                          << " 新しい印=" << third.RequestCount << (bDedupe ? " OK" : " NG") << '\n';
                if (!bDedupe)
                {
                    std::cerr << "同じフレームの印での重複の省略が期待と違います\n";
                    bPassed = false;
                }
            }

            // 2 パス目: 1 パス目で描いたクラスタは、2 パス目で遮蔽と判定されても使用の印を出す。
            // 1 パス目で描かなかった遮蔽クラスタは出さない。遮蔽されなければ、1 パス目で描いたものは 2 パス目では描かず、使用の印だけ出る
            {
                constexpr uint32_t ReadStamp = 7;
                constexpr uint32_t WriteStamp = 8;
                struct PassTwoScenario
                {
                    const char* Name;
                    float HiZDepth;      // 0 = 全てが遮蔽される、1 = 遮蔽されない
                    uint32_t VisibleLast; // 前のフレームの見えた印（ReadStamp = 1 パス目で描いた、0 = 描かなかった）
                    uint32_t ExpectedRequestCount;
                    std::initializer_list<uint32_t> Requests;
                    uint32_t ExpectedVisibleAfter; // 判定後の見えた印（遮蔽されたものは 0）
                    uint32_t ExpectedDrawn;
                };
                const PassTwoScenario passTwoScenarios[] = {
                    {"2パス目で遮蔽(1パス目で描画済み)", 0.0f, ReadStamp, 2, {2, 3}, 0u, 0},
                    {"2パス目で遮蔽(1パス目で未描画)", 0.0f, 0u, 0, {}, 0u, 0},
                    {"2パス目で可視(1パス目で描画済み)", 1.0f, ReadStamp, 2, {2, 3}, WriteStamp, 0},
                    {"2パス目で可視(1パス目で未描画)", 1.0f, 0u, 2, {2, 3}, WriteStamp, 12},
                };
                for (const PassTwoScenario& scenario : passTwoScenarios)
                {
                    const float depth[4] = {scenario.HiZDepth, scenario.HiZDepth, scenario.HiZDepth, scenario.HiZDepth};
                    fixture.HiZ->Update(depth, 8u, 16u, 0, 0);
                    uint32_t visible[ClusterCount];
                    for (uint32_t& value : visible)
                    {
                        value = scenario.VisibleLast;
                    }
                    WriteBuffer(fixture.Visible, visible, sizeof(visible));

                    Fixture passTwo = fixture;
                    passTwo.Base.CullPass = 2; // CULL_PASS_SECOND
                    passTwo.Base.bHiZEnabled = 1;
                    passTwo.Base.HiZWidth = 2;
                    passTwo.Base.HiZHeight = 2;
                    passTwo.Base.HiZMipCount = 1;
                    passTwo.Base.VisibleReadStamp = ReadStamp;
                    const uint32_t regions[PageCount] = {R, R, R, R};
                    CaseResult result;
                    if (!RunCase(passTwo, regions, noStamps, 1.0f, RequestCapacity, WriteStamp, result))
                    {
                        return 1;
                    }
                    // 子（番号 1..4・6..9・11..14）だけが基本の判定を通る（親は子が常駐しているので描かない）
                    const uint32_t* marks = static_cast<const uint32_t*>(fixture.Visible->Map(0u, sizeof(visible)));
                    bool bMarks = marks != nullptr;
                    for (uint32_t index = 0; bMarks && index < ClusterCount; ++index)
                    {
                        const bool bChild = (index % 5u) != 0u;
                        bMarks = marks[index] == (bChild ? scenario.ExpectedVisibleAfter : 0u);
                    }
                    fixture.Visible->Unmap();
                    const bool bCase = bMarks && result.DrawnCount == scenario.ExpectedDrawn &&
                                       result.RequestCount == scenario.ExpectedRequestCount &&
                                       result.RequestOverflow == 0 && SameValues(result.Requests, scenario.Requests);
                    std::cout << "ケース「" << scenario.Name << "」描画=" << result.DrawnCount << " 要求=" << result.RequestCount
                              << (bCase ? " OK" : " NG") << '\n';
                    if (!bCase)
                    {
                        std::cerr << "ケース「" << scenario.Name << "」が期待と違います\n";
                        PrintValues("要求", result.Requests);
                        bPassed = false;
                    }
                }
            }

            // BVH をたどる経路（cluster_bvh_cull.comp）: 2パス目で節ごと遮蔽されて枝が打ち切られても、
            // 1パス目で描いたクラスタの自分のページの使用の印が残る。1パス目 → 2パス目の順に、書き込み先を持ち越して実行する。
            // 節: 0 = 全体の根（内部。子の節 1..3）、1..3 = 家族ごとの葉（クラスタ 0..4・5..9・10..14）
            if (bvhPipeline)
            {
                constexpr uint32_t ReadStamp = 7;
                constexpr uint32_t WriteStamp = 8;
                constexpr uint32_t NodeCount = 4;
                constexpr uint32_t LeafQueueBase = 3; // 段 1 の入力の列（3 件）の後ろ
                const float noParentError = 3.402823466e+38f;

                Mega::GPUGroupBVHNode nodes[NodeCount] = {};
                nodes[0] = {0.0f, 0.0f, 12.0f, 9.5f, noParentError, 1u, 3u, 0u};
                nodes[1] = {0.0f, 0.0f, 10.0f, 3.0f, noParentError, 0u, 5u, Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF};
                nodes[2] = {6.0f, 0.0f, 12.0f, 3.0f, noParentError, 5u, 5u, Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF};
                nodes[3] = {-6.0f, 0.0f, 14.0f, 3.0f, noParentError, 10u, 5u, Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF};
                BufferPtr nodeBuffer = CreateHostBuffer(device, sizeof(nodes), ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress,
                                                        "GeometryPageRequestBvhNodes");
                const uint64_t nodeAddress = nodeBuffer ? nodeBuffer->GetDeviceAddress() : 0ull;
                if (nodeAddress == 0)
                {
                    std::cerr << "BVH の節のバッファを用意できませんでした\n";
                    return 1;
                }
                WriteBuffer(nodeBuffer, nodes, sizeof(nodes));

                TestInstance bvhInstance = instance;
                bvhInstance.BvhInfo[0] = static_cast<uint32_t>(nodeAddress & 0xFFFFFFFFull);
                bvhInstance.BvhInfo[1] = static_cast<uint32_t>(nodeAddress >> 32);
                bvhInstance.BvhInfo[2] = NodeCount;
                bvhInstance.BvhInfo[3] = 0;
                WriteBuffer(fixture.Instances, &bvhInstance, sizeof(bvhInstance));

                // 1 パスぶん（節の判定の 2 段 + 葉のクラスタの判定）を実行する。段のカウンタはパスごとに 0 へ戻す
                const auto runBvhPass = [&](uint32_t cullPass) -> bool
                {
                    uint32_t zeroCounters[BvhCounterBytes / 4u] = {};
                    WriteBuffer(fixture.BvhCounters, zeroCounters, sizeof(zeroCounters));
                    CullUniforms uniforms = fixture.Base;
                    uniforms.PageRequestCapacity = RequestCapacity;
                    uniforms.CullPass = cullPass;
                    uniforms.VisibleReadStamp = ReadStamp;
                    uniforms.VisibleWriteStamp = WriteStamp;
                    if (cullPass == 2)
                    {
                        uniforms.bHiZEnabled = 1;
                        uniforms.HiZWidth = 2;
                        uniforms.HiZHeight = 2;
                        uniforms.HiZMipCount = 1;
                    }
                    uniforms.BvhRootCount = 1;
                    uniforms.BvhLeafBase = LeafQueueBase;
                    uniforms.BvhStage = 0;
                    uniforms.BvhInputBase = 0;
                    uniforms.BvhNextBase = 0;
                    bool bOk = DispatchOnce(fixture, bvhPipeline, uniforms, 1u);
                    uniforms.BvhStage = 1;
                    uniforms.BvhInputBase = 0;
                    uniforms.BvhNextBase = LeafQueueBase;
                    bOk = bOk && DispatchOnce(fixture, bvhPipeline, uniforms, 1u);
                    uniforms.BvhStage = 0xFFFFFFFFu; // BVH_STAGE_CLUSTERS
                    return bOk && DispatchOnce(fixture, bvhPipeline, uniforms, 1u);
                };

                struct BvhScenario
                {
                    const char* Name;
                    float HiZDepth;       // 0 = 全てが遮蔽される、1 = 遮蔽されない
                    uint32_t VisibleLast; // 前のフレームの見えた印（ReadStamp = 1 パス目で描く、0 = 描かない）
                    uint32_t ExpectedRequestCount;
                    std::initializer_list<uint32_t> Requests;
                    uint32_t ExpectedDrawn;
                };
                const BvhScenario bvhScenarios[] = {
                    {"BVH・2パス目で節ごと遮蔽(1パス目で描画済み)", 0.0f, ReadStamp, 2, {2, 3}, 12},
                    {"BVH・2パス目で節ごと遮蔽(1パス目で未描画)", 0.0f, 0u, 0, {}, 0},
                    {"BVH・2パス目で可視(1パス目で描画済み)", 1.0f, ReadStamp, 2, {2, 3}, 12},
                    {"BVH・2パス目で可視(1パス目で未描画)", 1.0f, 0u, 2, {2, 3}, 12},
                };
                for (const BvhScenario& scenario : bvhScenarios)
                {
                    const float depth[4] = {scenario.HiZDepth, scenario.HiZDepth, scenario.HiZDepth, scenario.HiZDepth};
                    fixture.HiZ->Update(depth, 8u, 16u, 0, 0);
                    uint32_t visible[ClusterCount];
                    for (uint32_t& value : visible)
                    {
                        value = scenario.VisibleLast;
                    }
                    WriteBuffer(fixture.Visible, visible, sizeof(visible));
                    const uint32_t allResident[PageCount] = {R, R, R, R};
                    ResetBuffers(fixture, allResident, noStamps, RequestCapacity);

                    CaseResult result;
                    if (!runBvhPass(1) || !runBvhPass(2) || !ReadResult(fixture, RequestCapacity, result))
                    {
                        return 1;
                    }
                    const bool bCase = result.DrawnCount == scenario.ExpectedDrawn &&
                                       result.RequestCount == scenario.ExpectedRequestCount && result.RequestOverflow == 0 &&
                                       SameValues(result.Requests, scenario.Requests);
                    std::cout << "ケース「" << scenario.Name << "」描画=" << result.DrawnCount << " 要求=" << result.RequestCount
                              << (bCase ? " OK" : " NG") << '\n';
                    if (!bCase)
                    {
                        std::cerr << "ケース「" << scenario.Name << "」が期待と違います\n";
                        PrintValues("要求", result.Requests);
                        bPassed = false;
                    }
                }
            }

            // 要求の読み取り(GeometryPageRequestSet)が、GPU の書いた並びをそのまま読める
            {
                const uint32_t regions[PageCount] = {R, R, N, N};
                CaseResult result;
                if (!RunCase(fixture, regions, noStamps, 1.0f, RequestCapacity, 700, result))
                {
                    return 1;
                }
                const uint32_t bytes = static_cast<uint32_t>(Mega::GeometryPageRequestBuffer::GetBufferBytes(RequestCapacity));
                const uint32_t* words = static_cast<const uint32_t*>(fixture.PageRequests->Map(0u, bytes));
                Mega::GeometryPageRequestSet set;
                const Mega::GeometryPageRequestDecodeResult decoded =
                    words != nullptr ? set.AddBuffer(words, RequestCapacity, 9, 5) : Mega::GeometryPageRequestDecodeResult{};
                fixture.PageRequests->Unmap();
                const bool bDecoded = words != nullptr && decoded.Accepted == 3 && decoded.Overflow == 0 &&
                                      set.GetRequests().size() == 3 && set.GetRequests()[0].LastRequestedFrame == 9;
                bool bIndices = false;
                if (bDecoded)
                {
                    uint32_t mask = 0;
                    for (const auto& request : set.GetRequests())
                    {
                        mask |= request.TableIndex < 32u ? (1u << request.TableIndex) : 0u;
                    }
                    bIndices = mask == ((1u << 0) | (1u << 2) | (1u << 3)); // 並びは GPU のスレッドの順で決まらない
                }
                std::cout << "ケース「要求の読み取り」" << (bDecoded && bIndices ? "OK" : "NG") << '\n';
                if (!bDecoded || !bIndices)
                {
                    bPassed = false;
                }
            }

            device->WaitIdle();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        if (validationErrorCount != 0u)
        {
            std::cerr << "Vulkan の検証エラーを検出しました: " << validationErrorCount << '\n';
            bPassed = false;
        }

        std::cout << (bPassed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return bPassed ? 0 : 1;
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
