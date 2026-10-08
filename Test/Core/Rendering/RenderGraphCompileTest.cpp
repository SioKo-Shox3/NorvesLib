#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/HiZPyramidPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/BloomPass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/LightingPass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/SSRPass.h"
#include "Rendering/ToneMappingPass.h"
#if __has_include("Rendering/VignettePass.h")
#include "Rendering/VignettePass.h"
#define NORVES_HAS_VIGNETTE_PASS 1
#else
#define NORVES_HAS_VIGNETTE_PASS 0
#endif
#include "Rendering/UpscalePass.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/ShadowProbePass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/VirtualShadowMapCasters.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VirtualShadowMapRaster.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/VisibilityResolvePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Test/Core/Rendering/GeometryUploadTestSupport.h"
#include "Container/PointerTypes.h"
#include "Debug/Stats.h"
#include "FileStream/FileStream.h"
#include "Logging/Logger.h"
#include "Math/MatrixUtils.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include "RHI/Vulkan/VulkanShaderCompiler.h"
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;
namespace DebugStats = NorvesLib::Debug;
namespace Logging = NorvesLib::Core::Logging;

namespace
{
    // ShaderManagerはGLSLの#include展開のためにファイルを読むため、実在するシェーダー置き場を渡す。
    // compilerはテスト用の偽物なので、読んだ内容から実際のバイトコードは作らない。
    constexpr const char* TestShaderDirectory = NORVES_SOURCE_ROOT "/Assets/Shaders";

    void ConfigureAssertOutput()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }

    // FakeBuffer が返す、スキニングの変形した頂点（今・前）のバッファのデバイスアドレス
    constexpr uint64_t SkinningCurrentVerticesAddress = 0x200000000ull;
    constexpr uint64_t SkinningPreviousVerticesAddress = 0x300000000ull;

    struct BarrierEvent
    {
        RGBarrierKind Kind = RGBarrierKind::Texture;
        RHI::ITexture* Texture = nullptr;
        RHI::IBuffer* Buffer = nullptr;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint64_t BufferSize = 0;
        /** @brief バリアを記録した時点の CallSequence の長さ（直後に記録された B・E・D・I・J の位置と同じ） */
        size_t SequencePosition = 0;
    };

    enum class FakeRenderEvent
    {
        LightSsboUpdate,
        BindStorageBuffer5,
        DescriptorSetUpdate,
        CommandSetDescriptorSet,
        Draw
    };

    struct FakeBufferLifetimeTracker
    {
        bool bDestroyed = false;
    };

    struct BufferCreationRecord
    {
        RHI::BufferDesc Desc;
        RHI::TSharedPtr<FakeBufferLifetimeTracker> Tracker;
    };

    Container::VariableArray<FakeRenderEvent> GRenderEvents;
    // スキニングのパレットのバッファ（"SkinnedPalette"・"SkinnedPreviousPalette"）の更新の記録（更新ごとの float の列）
    struct SkinnedPaletteUpdate
    {
        bool bPrevious = false;
        Container::VariableArray<float> Values;
    };
    Container::VariableArray<SkinnedPaletteUpdate> GSkinnedPaletteUpdates;
    // MegaGeometryPass が毎フレーム書くインスタンスの表（"MegaGeometry_InstanceTable"）の更新の記録（更新ごとの中身）
    Container::VariableArray<Container::VariableArray<uint8_t>> GMegaInstanceTableUpdates;
    // MegaGeometryPass のカリングの定数バッファ（"MegaGeometry_CullUBO"。パスごとに 1 つ）の更新の記録（更新ごとの中身）
    Container::VariableArray<Container::VariableArray<uint8_t>> GMegaCullUniformUpdates;
    // VSM の MegaGeometry の投影物のカリングの定数バッファ（"VsmMegaCullUniform"・"VsmMegaCullParams"）の更新の記録（更新ごとの中身）
    Container::VariableArray<Container::VariableArray<uint8_t>> GVsmMegaCullUniformUpdates;
    Container::VariableArray<Container::VariableArray<uint8_t>> GVsmMegaCullParamsUpdates;
    // VisibilitySwRaster が dispatch ごとに書く定数バッファ（"VisBuffer_SwRasterParams"）の更新の記録（更新ごとの中身）
    Container::VariableArray<Container::VariableArray<uint8_t>> GSwRasterParamsUpdates;
    // 影の標本のパスが毎フレーム書く定数（ShadowProbeParams）。--shadow-probe の検査に使う
    Container::VariableArray<Container::VariableArray<uint8_t>> GShadowProbeParamUpdates;
    Container::VariableArray<uint8_t> GLastDescriptorBinding4UpdateBytes;
    Container::VariableArray<uint8_t> GLastDescriptorBinding5UpdateBytes;
    RHI::IBuffer* GLastDescriptorBinding4Buffer = nullptr;
    RHI::IBuffer* GLastDescriptorBinding5Buffer = nullptr;
    uint32_t GLastDescriptorBinding4Offset = 0;
    uint32_t GLastDescriptorBinding4Size = 0;
    uint32_t GLastDescriptorBinding5Offset = 0;
    uint32_t GLastDescriptorBinding5Size = 0;

    bool HasUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag)
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
    }

    bool IsDebugName(const char* actual, const char* expected)
    {
        return actual != nullptr && std::strcmp(actual, expected) == 0;
    }

    void PushRenderEvent(FakeRenderEvent event)
    {
        GRenderEvents.push_back(event);
    }

    uint32_t FindRenderEventIndex(FakeRenderEvent event)
    {
        for (uint32_t i = 0; i < GRenderEvents.size(); ++i)
        {
            if (GRenderEvents[i] == event)
            {
                return i;
            }
        }

        assert(false);
        return 0;
    }

    uint32_t FindRenderEventIndexAfter(FakeRenderEvent event, uint32_t afterIndex)
    {
        for (uint32_t i = afterIndex + 1; i < GRenderEvents.size(); ++i)
        {
            if (GRenderEvents[i] == event)
            {
                return i;
            }
        }

        assert(false);
        return 0;
    }

    void ResetLightingDescriptorCapture()
    {
        GRenderEvents.clear();
        GLastDescriptorBinding4UpdateBytes.clear();
        GLastDescriptorBinding5UpdateBytes.clear();
        GLastDescriptorBinding4Buffer = nullptr;
        GLastDescriptorBinding5Buffer = nullptr;
        GLastDescriptorBinding4Offset = 0;
        GLastDescriptorBinding4Size = 0;
        GLastDescriptorBinding5Offset = 0;
        GLastDescriptorBinding5Size = 0;
    }

    class FakeTexture final : public RHI::ITexture
    {
    public:
        FakeTexture()
        {
            m_Desc.Width = 64;
            m_Desc.Height = 32;
            m_Desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            m_Desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        }

        explicit FakeTexture(const RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        RHI::TextureDesc m_Desc;
    };

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        FakeBuffer()
            : m_Desc(256, RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::ShaderRead)
        {
        }

        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : m_Desc(desc)
        {
        }

        FakeBuffer(const RHI::BufferDesc& desc, RHI::TSharedPtr<FakeBufferLifetimeTracker> tracker)
            : m_Desc(desc), m_Tracker(tracker)
        {
        }

        ~FakeBuffer() override
        {
            if (m_Tracker)
            {
                m_Tracker->bDestroyed = true;
            }
        }

        uint64_t GetSize() const override { return m_Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            // ジオメトリの区画へ書くステージングのリングだけは、写像して書き込めるようにバイト列を持つ
            if ((IsDebugName(m_Desc.DebugName, "TileUploadRing") || IsDebugName(m_Desc.DebugName, "ShadowProbe_Stats") ||
                 IsDebugName(m_Desc.DebugName, "VSM_StatsReadback") || IsDebugName(m_Desc.DebugName, "LightingVsmStats")) &&
                m_Desc.Size > 0)
            {
                if (MappedBytes.empty())
                {
                    MappedBytes.resize(static_cast<size_t>(m_Desc.Size));
                }
                return offset < MappedBytes.size() ? MappedBytes.data() + offset : nullptr;
            }
            (void)offset;
            return nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            ++UpdateCallCount;
            LastUpdateOffset = offset;
            LastUpdateSize = size;
            LastUpdateBytes.resize(static_cast<size_t>(size));
            if (size > 0)
            {
                assert(data != nullptr);
                std::memcpy(LastUpdateBytes.data(), data, static_cast<size_t>(size));
            }

            if (IsDebugName(m_Desc.DebugName, "LightArraySSBO"))
            {
                PushRenderEvent(FakeRenderEvent::LightSsboUpdate);
            }
            if (IsDebugName(m_Desc.DebugName, "MegaGeometry_InstanceTable"))
            {
                GMegaInstanceTableUpdates.push_back(LastUpdateBytes);
            }
            if (IsDebugName(m_Desc.DebugName, "MegaGeometry_CullUBO"))
            {
                GMegaCullUniformUpdates.push_back(LastUpdateBytes);
            }
            if (IsDebugName(m_Desc.DebugName, "VisBuffer_SwRasterParams"))
            {
                GSwRasterParamsUpdates.push_back(LastUpdateBytes);
            }
            if (IsDebugName(m_Desc.DebugName, "VsmMegaCullUniform"))
            {
                GVsmMegaCullUniformUpdates.push_back(LastUpdateBytes);
            }
            if (IsDebugName(m_Desc.DebugName, "VsmMegaCullParams"))
            {
                GVsmMegaCullParamsUpdates.push_back(LastUpdateBytes);
            }
            if (IsDebugName(m_Desc.DebugName, "ShadowProbeParams"))
            {
                GShadowProbeParamUpdates.push_back(LastUpdateBytes);
            }
            const bool bSkinnedPalette = IsDebugName(m_Desc.DebugName, "SkinnedPalette");
            const bool bSkinnedPreviousPalette = IsDebugName(m_Desc.DebugName, "SkinnedPreviousPalette");
            if (bSkinnedPalette || bSkinnedPreviousPalette)
            {
                SkinnedPaletteUpdate update;
                update.bPrevious = bSkinnedPreviousPalette;
                update.Values.resize(static_cast<size_t>(size / sizeof(float)));
                if (size > 0)
                {
                    std::memcpy(update.Values.data(), data, update.Values.size() * sizeof(float));
                }
                GSkinnedPaletteUpdates.push_back(std::move(update));
            }
        }
        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }
        uint64_t GetDeviceAddress() const override
        {
            // ジオメトリの共有プールの塊だけが、まとめたカリングの引けるデバイスアドレスを持つ（実機の塊は
            // BufferDeviceAddress の用途で作る）。他のバッファは従来どおり 0（要求されていない）
            if (IsDebugName(m_Desc.DebugName, "GeometryPoolBlock"))
            {
                return 0x100000000ull + (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) & 0xFFFFFFF0ull);
            }
            // スキニングの変形した頂点（今・前）。インスタンスごとの先頭のアドレスの検査に使う固定の値
            if (IsDebugName(m_Desc.DebugName, "Skinning_CurrentVertices"))
            {
                return SkinningCurrentVerticesAddress;
            }
            if (IsDebugName(m_Desc.DebugName, "Skinning_PreviousVertices"))
            {
                return SkinningPreviousVerticesAddress;
            }
            // 手続きメッシュの頂点・インデックスと、スキニングのインデックス（影の塊の記録が、メッシュのバッファのアドレスを引く）
            if (IsDebugName(m_Desc.DebugName, "MeshVB") || IsDebugName(m_Desc.DebugName, "MeshIB") ||
                IsDebugName(m_Desc.DebugName, "SkinnedMeshIB"))
            {
                return 0x400000000ull + (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) & 0xFFFFFF0ull);
            }
            return 0;
        }

        const RHI::BufferDesc& GetDesc() const
        {
            return m_Desc;
        }

        uint64_t LastUpdateOffset = 0;
        uint64_t LastUpdateSize = 0;
        uint32_t UpdateCallCount = 0;
        Container::VariableArray<uint8_t> LastUpdateBytes;
        Container::VariableArray<uint8_t> MappedBytes;

    private:
        RHI::BufferDesc m_Desc;
        RHI::TSharedPtr<FakeBufferLifetimeTracker> m_Tracker;
    };

    // 記述子セットへ束縛したバッファの記録（束縛の番号と名前）。計算の dispatch の時点で、どのバッファを読み書きするかの検査に使う
    struct BoundBufferName
    {
        uint32_t Binding = 0;
        char Name[48] = {};
    };
    struct DescriptorBindingRecord
    {
        const void* Set = nullptr;
        Container::VariableArray<BoundBufferName> Buffers;
    };
    Container::VariableArray<DescriptorBindingRecord> GDescriptorBindingRecords;

    void RecordDescriptorBufferBinding(const void* set, uint32_t binding, const RHI::BufferPtr& buffer)
    {
        DescriptorBindingRecord* record = nullptr;
        for (DescriptorBindingRecord& candidate : GDescriptorBindingRecords)
        {
            if (candidate.Set == set)
            {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr)
        {
            GDescriptorBindingRecords.push_back(DescriptorBindingRecord{});
            record = &GDescriptorBindingRecords.back();
            record->Set = set;
        }
        BoundBufferName* entry = nullptr;
        for (BoundBufferName& candidate : record->Buffers)
        {
            if (candidate.Binding == binding)
            {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr)
        {
            record->Buffers.push_back(BoundBufferName{});
            entry = &record->Buffers.back();
            entry->Binding = binding;
        }
        std::memset(entry->Name, 0, sizeof(entry->Name));
        const FakeBuffer* fake = static_cast<const FakeBuffer*>(buffer.get());
        const char* name = fake != nullptr ? fake->GetDesc().DebugName : nullptr;
        if (name != nullptr)
        {
            std::memcpy(entry->Name, name, std::min(std::strlen(name), sizeof(entry->Name) - 1));
        }
    }

    // 記述子セットを手放したときに記録も消す（同じ番地に別の記述子セットができても、古い束縛が混ざらない）
    void ClearDescriptorBufferBindings(const void* set)
    {
        for (size_t index = 0; index < GDescriptorBindingRecords.size(); ++index)
        {
            if (GDescriptorBindingRecords[index].Set == set)
            {
                GDescriptorBindingRecords.erase(GDescriptorBindingRecords.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
        }
    }

    Container::VariableArray<BoundBufferName> SnapshotDescriptorBindings(const void* set)
    {
        for (const DescriptorBindingRecord& candidate : GDescriptorBindingRecords)
        {
            if (candidate.Set == set)
            {
                return candidate.Buffers;
            }
        }
        return {};
    }

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        Container::VariableArray<BarrierEvent> Barriers;
        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
        uint32_t DrawCallCount = 0;
        uint32_t DispatchCount = 0;
        // Dispatch に渡されたグループ数の記録
        struct DispatchSize
        {
            uint32_t X = 0;
            uint32_t Y = 0;
            uint32_t Z = 0;
        };
        Container::VariableArray<DispatchSize> DispatchGroups;
        // 材質のタイル分類の資源（MaterialTile_ で始まる名前）への 0 埋めの記録（バッファの名前と大きさ）
        struct MaterialTileFill
        {
            char BufferName[40] = {};
            uint64_t SizeBytes = 0;
        };
        Container::VariableArray<MaterialTileFill> MaterialTileFills;
        // 「前のフレームで見えた」ビットのバッファへの0埋め（範囲）とコピー（古いバッファからの引き継ぎ）の記録
        struct VisibilityFill
        {
            uint64_t OffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };
        struct VisibilityCopy
        {
            uint64_t SourceOffsetBytes = 0;
            uint64_t DestinationOffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };
        Container::VariableArray<VisibilityFill> VisibilityFills;
        Container::VariableArray<VisibilityCopy> VisibilityCopies;
        // 記録の表（VisBuffer_DrawRecords）へのコピー（ホストが書いた記録を GPU 専用の表へ渡す）
        Container::VariableArray<VisibilityCopy> RecordTableCopies;
        // 64bit のバッファ（VisBuffer_Key64）の埋め。埋めた値・大きさと、その時点の CallSequence の長さ（render pass の外のどの位置か）
        struct Key64Fill
        {
            uint64_t SizeBytes = 0;
            uint32_t Value = 0;
            size_t SequencePosition = 0;
        };
        Container::VariableArray<Key64Fill> Key64Fills;
        // VSM の資源（VSM_ で始まるバッファ）の埋め。バッファ名・大きさ・値の順に呼ばれた通りに残す
        struct VsmFill
        {
            char BufferName[32] = {};
            uint64_t SizeBytes = 0;
            uint32_t Value = 0;
        };
        Container::VariableArray<VsmFill> VsmFills;
        // ソフトウェアラスタの一覧（MegaGeometry_SwRaster）の頭の埋め（0 埋め。大きさ・値・その時点の CallSequence の長さ）
        Container::VariableArray<Key64Fill> SwRasterFills;
        // Draw（頂点だけの描画）が呼ばれた時点の CallSequence の長さ。全画面の合流が render pass（B と E の間）で描かれたかを確かめる
        Container::VariableArray<size_t> DrawPositions;
        // 間接描画の記録（コマンドの先頭のバイト位置と、描画の最大数）。区間ごとに1回ずつ呼ばれる
        struct IndirectDrawRecord
        {
            uint64_t OffsetBytes = 0;
            uint32_t MaxDrawCount = 0;
        };
        Container::VariableArray<IndirectDrawRecord> IndirectDraws;
        // SetPipeline に渡されたパイプライン（呼ばれた順）。線の描き方（PolygonMode::Line）で描いたかを確かめる
        Container::VariableArray<RHI::PipelinePtr> SetPipelines;
        // 呼ばれた順の記録（B=BeginRenderPass、E=EndRenderPass、D=Dispatch、I=間接描画）。パスの並びの検査用
        Container::VariableArray<char> CallSequence;
        // DrawIndexedInstanced（手続き・スキニングの塊の描画）が呼ばれた時点の CallSequence の長さ。render pass の中のどの位置かを確かめる
        Container::VariableArray<size_t> InstancedDrawSequencePositions;
        uint32_t LastDrawIndexedInstancedIndexCount = 0;
        uint32_t LastDrawIndexedInstancedStartIndexLocation = 0;
        int32_t LastDrawIndexedInstancedBaseVertexLocation = 0;

        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            assert(renderPass);
            assert(framebuffer);
            ++BeginRenderPassCount;
            CallSequence.push_back('B');
        }
        void EndRenderPass() override
        {
            ++EndRenderPassCount;
            CallSequence.push_back('E');
        }
        void SetViewport(const RHI::Viewport& viewport) override { (void)viewport; }
        void SetScissor(const RHI::ScissorRect& scissor) override { (void)scissor; }
        void SetPipeline(RHI::PipelinePtr pipeline) override { SetPipelines.push_back(pipeline); }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)buffer;
            (void)offset;
            (void)slot;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, RHI::IndexType = RHI::IndexType::Uint32) override
        {
            (void)buffer;
            (void)offset;
        }
        void SetConstantBuffer(RHI::BufferPtr buffer, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)buffer;
            (void)slot;
            (void)stage;
        }
        void SetTexture(RHI::TexturePtr texture, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)texture;
            (void)slot;
            (void)stage;
        }
        void SetSampler(RHI::SamplerPtr sampler, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)sampler;
            (void)slot;
            (void)stage;
        }
        void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
        {
            m_LastDescriptorSet = descriptorSet.get();
            (void)slot;
            PushRenderEvent(FakeRenderEvent::CommandSetDescriptorSet);
        }
        void DrawIndexed(uint32_t indexCount,
                         uint32_t startIndexLocation = 0,
                         int32_t baseVertexLocation = 0) override
        {
            (void)indexCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)vertexCount;
            (void)startVertexLocation;
            DrawPositions.push_back(CallSequence.size());
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void DrawIndexedInstanced(uint32_t indexCount,
                                  uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0,
                                  int32_t baseVertexLocation = 0,
                                  uint32_t startInstanceLocation = 0) override
        {
            (void)instanceCount;
            (void)startInstanceLocation;
            LastDrawIndexedInstancedIndexCount = indexCount;
            LastDrawIndexedInstancedStartIndexLocation = startIndexLocation;
            LastDrawIndexedInstancedBaseVertexLocation = baseVertexLocation;
            InstancedDrawSequencePositions.push_back(CallSequence.size());
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void DrawInstanced(uint32_t vertexCount,
                           uint32_t instanceCount,
                           uint32_t startVertexLocation = 0,
                           uint32_t startInstanceLocation = 0) override
        {
            (void)vertexCount;
            (void)instanceCount;
            (void)startVertexLocation;
            (void)startInstanceLocation;
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer,
                                 uint64_t offset,
                                 uint32_t drawCount,
                                 uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)stride;
            IndirectDraws.push_back(IndirectDrawRecord{offset, drawCount});
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
            CallSequence.push_back('I');
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer,
                                      uint64_t indirectOffset,
                                      RHI::BufferPtr countBuffer,
                                      uint64_t countOffset,
                                      uint32_t maxDrawCount,
                                      uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)countBuffer;
            (void)countOffset;
            (void)stride;
            IndirectDraws.push_back(IndirectDrawRecord{indirectOffset, maxDrawCount});
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
            CallSequence.push_back('I');
        }
        void FillBuffer(RHI::BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override
        {
            if (buffer &&
                IsDebugName(static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName,
                            "MegaGeometry_VisibleLastFrame"))
            {
                VisibilityFills.push_back(VisibilityFill{offset, size});
            }
            if (buffer && IsDebugName(static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName, "VisBuffer_Key64"))
            {
                Key64Fills.push_back(Key64Fill{size, value, CallSequence.size()});
            }
            if (buffer && IsDebugName(static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName, "MegaGeometry_SwRaster"))
            {
                SwRasterFills.push_back(Key64Fill{size, value, CallSequence.size()});
            }
            if (buffer)
            {
                const char* vsmName = static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName;
                if (vsmName != nullptr && std::strncmp(vsmName, "VSM_", 4) == 0)
                {
                    VsmFill fill;
                    std::memcpy(fill.BufferName, vsmName, std::min(std::strlen(vsmName), sizeof(fill.BufferName) - 1));
                    fill.SizeBytes = size;
                    fill.Value = value;
                    VsmFills.push_back(fill);
                }
            }
            if (buffer)
            {
                const char* megaName = static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName;
                if (megaName != nullptr && std::strncmp(megaName, "VsmMega_", 8) == 0)
                {
                    VsmMegaFill fill;
                    std::memcpy(fill.BufferName, megaName, std::min(std::strlen(megaName), sizeof(fill.BufferName) - 1));
                    fill.SizeBytes = size;
                    fill.Value = value;
                    fill.SequencePosition = CallSequence.size();
                    VsmMegaFills.push_back(fill);
                }
            }
            if (buffer)
            {
                const char* name = static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName;
                if (name != nullptr && std::strncmp(name, "MaterialTile_", 13) == 0 && value == 0u)
                {
                    MaterialTileFill fill;
                    std::memcpy(fill.BufferName, name, std::min(std::strlen(name), sizeof(fill.BufferName) - 1));
                    fill.SizeBytes = size;
                    MaterialTileFills.push_back(fill);
                }
            }
            (void)value;
        }
        void Dispatch(uint32_t threadGroupCountX,
                      uint32_t threadGroupCountY,
                      uint32_t threadGroupCountZ) override
        {
            DispatchGroups.push_back({threadGroupCountX, threadGroupCountY, threadGroupCountZ});
            DispatchBindings.push_back(SnapshotDescriptorBindings(m_LastDescriptorSet));
            ++DispatchCount;
            CallSequence.push_back('D');
        }
        // Dispatch ごとの、その時点で最後に設定した記述子セットへ束縛されていたバッファ（DispatchGroups と同じ並び）
        Container::VariableArray<Container::VariableArray<BoundBufferName>> DispatchBindings;
        // VSM の MegaGeometry の投影物のカリングの資源（VsmMega_ で始まる名前）への 0 埋めの記録（バッファの名前・大きさ・値・記録した時点の CallSequence の長さ）
        struct VsmMegaFill
        {
            char BufferName[40] = {};
            uint64_t SizeBytes = 0;
            uint32_t Value = 0;
            size_t SequencePosition = 0;
        };
        Container::VariableArray<VsmMegaFill> VsmMegaFills;
        // 最後に SetDescriptorSet へ渡された記述子セット（Dispatch の時点の束縛を引く）
        const void* m_LastDescriptorSet = nullptr;
        // GPU タイムスタンプの区間（開いた順の名前と、開いた時点の CallSequence の長さ）
        struct GpuScopeRecord
        {
            Container::String Name;
            size_t SequencePosition = 0;
        };
        Container::VariableArray<GpuScopeRecord> GpuScopes;
        RHI::GPUTimestampScopeHandle BeginGPUTimestampScope(const char* scopeName) override
        {
            GpuScopeRecord record;
            record.Name = scopeName != nullptr ? scopeName : "";
            record.SequencePosition = CallSequence.size();
            GpuScopes.push_back(record);
            return {};
        }
        // 間接 dispatch の記録（引数のバッファの名前と先頭のバイト位置）。Dispatch とは別に数える（CallSequence では 'J'）
        struct IndirectDispatchRecord
        {
            char BufferName[40] = {};
            uint64_t OffsetBytes = 0;
            // 記録した時点で最後に設定した記述子セットへ束縛されていたバッファ
            Container::VariableArray<BoundBufferName> Bindings;
        };
        Container::VariableArray<IndirectDispatchRecord> IndirectDispatches;
        // true なら、間接 dispatch を何も記録せず false で断る（ICommandList の既定の実装と同じ）
        bool bRejectDispatchIndirect = false;
        bool DispatchIndirect(RHI::BufferPtr indirectBuffer, uint64_t offset) override
        {
            if (bRejectDispatchIndirect || !indirectBuffer)
            {
                return false;
            }
            IndirectDispatchRecord record;
            const char* name = static_cast<const FakeBuffer*>(indirectBuffer.get())->GetDesc().DebugName;
            if (name != nullptr)
            {
                std::memcpy(record.BufferName, name, std::min(std::strlen(name), sizeof(record.BufferName) - 1));
            }
            record.OffsetBytes = offset;
            record.Bindings = SnapshotDescriptorBindings(m_LastDescriptorSet);
            IndirectDispatches.push_back(record);
            CallSequence.push_back('J');
            return true;
        }
        void CopyBuffer(RHI::BufferPtr src,
                        RHI::BufferPtr dst,
                        uint64_t size = 0,
                        uint64_t srcOffset = 0,
                        uint64_t dstOffset = 0) override
        {
            (void)src;
            if (dst &&
                IsDebugName(static_cast<const FakeBuffer*>(dst.get())->GetDesc().DebugName,
                            "MegaGeometry_VisibleLastFrame"))
            {
                VisibilityCopies.push_back(VisibilityCopy{srcOffset, dstOffset, size});
            }
            if (dst && IsDebugName(static_cast<const FakeBuffer*>(dst.get())->GetDesc().DebugName, "VisBuffer_DrawRecords"))
            {
                RecordTableCopies.push_back(VisibilityCopy{srcOffset, dstOffset, size});
            }
        }
        void CopyBufferToTexture(RHI::BufferPtr src,
                                 RHI::TexturePtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTextureToBuffer(RHI::TexturePtr src,
                                 RHI::BufferPtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTexture(RHI::TexturePtr src,
                         RHI::TexturePtr dst,
                         uint32_t width,
                         uint32_t height,
                         uint32_t srcMipLevel = 0,
                         uint32_t srcArrayIndex = 0,
                         uint32_t dstMipLevel = 0,
                         uint32_t dstArrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)srcMipLevel;
            (void)srcArrayIndex;
            (void)dstMipLevel;
            (void)dstArrayIndex;
        }
        void GenerateMipmaps(RHI::TexturePtr texture) override { (void)texture; }
        void BufferBarrier(RHI::BufferPtr buffer,
                           RHI::ResourceState beforeState,
                           RHI::ResourceState afterState,
                           uint64_t offset = 0,
                           uint64_t size = 0) override
        {
            (void)offset;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Buffer;
            event.Buffer = buffer.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            event.BufferSize = size;
            event.SequencePosition = CallSequence.size();
            Barriers.push_back(event);
        }
        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Texture;
            event.Texture = texture.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            Barriers.push_back(event);
        }
    };

    class FakeRenderPass final : public RHI::IRenderPass
    {
    public:
        explicit FakeRenderPass(const RHI::RenderPassDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorAttachments.size());
        }

        bool HasDepthStencilAttachment() const override
        {
            return m_Desc.hasDepthStencil;
        }

        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index < m_Desc.colorAttachments.size()
                       ? m_Desc.colorAttachments[index].format
                       : RHI::Format::UNKNOWN;
        }

        RHI::Format GetDepthStencilFormat() const override
        {
            return m_Desc.depthStencilAttachment.format;
        }

    private:
        RHI::RenderPassDesc m_Desc;
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(const RHI::FramebufferDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.width; }
        uint32_t GetHeight() const override { return m_Desc.height; }
        RHI::RenderPassPtr GetRenderPass() const override { return m_Desc.renderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            return index < m_Desc.colorTargets.size() ? m_Desc.colorTargets[index] : nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return m_Desc.depthStencilTarget; }
        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorTargets.size());
        }
        bool HasDepthStencilAttachment() const override { return m_Desc.depthStencilTarget != nullptr; }

    private:
        RHI::FramebufferDesc m_Desc;
    };

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(const RHI::ShaderDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::ShaderStage GetStage() const override { return m_Desc.stage; }
        Container::String GetEntryPoint() const override { return m_Desc.entryPoint; }
        const Container::VariableArray<uint8_t>& GetByteCode() const override
        {
            return m_Desc.byteCode;
        }

    private:
        RHI::ShaderDesc m_Desc;
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        explicit FakeSampler(const RHI::SamplerDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::FilterMode GetFilterMin() const override { return m_Desc.filterMin; }
        RHI::FilterMode GetFilterMag() const override { return m_Desc.filterMag; }
        RHI::FilterMode GetFilterMip() const override { return m_Desc.filterMip; }
        RHI::TextureAddressMode GetAddressModeU() const override { return m_Desc.addressU; }
        RHI::TextureAddressMode GetAddressModeV() const override { return m_Desc.addressV; }
        RHI::TextureAddressMode GetAddressModeW() const override { return m_Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return m_Desc.maxAnisotropy; }
        RHI::CompareFunc GetCompareFunc() const override { return m_Desc.compareFunc; }

    private:
        RHI::SamplerDesc m_Desc;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        FakePipeline(RHI::PipelineType type, uint32_t bindPointCount,
                     RHI::PolygonMode polygonMode = RHI::PolygonMode::Fill)
            : m_Type(type), m_BindPointCount(bindPointCount), m_PolygonMode(polygonMode)
        {
        }

        RHI::PipelineType GetPipelineType() const override { return m_Type; }
        uint32_t GetBindPointCount() const override { return m_BindPointCount; }
        RHI::PolygonMode GetPolygonMode() const { return m_PolygonMode; }

    private:
        RHI::PipelineType m_Type = RHI::PipelineType::Graphics;
        uint32_t m_BindPointCount = 0;
        RHI::PolygonMode m_PolygonMode = RHI::PolygonMode::Fill;
    };

    RHI::ITexture* GLastDescriptorBinding6Texture = nullptr;

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        ~FakeDescriptorSet() override
        {
            ClearDescriptorBufferBindings(this);
        }

        void BindConstantBuffer(uint32_t binding,
                                RHI::BufferPtr buffer,
                                uint32_t offset,
                                uint32_t size) override
        {
            RecordDescriptorBufferBinding(this, binding, buffer);
            if (binding == 4)
            {
                m_Binding4Buffer = buffer;
                GLastDescriptorBinding4Buffer = buffer.get();
                GLastDescriptorBinding4Offset = offset;
                GLastDescriptorBinding4Size = size;
            }
        }

        void BindTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            if (binding == 6)
            {
                GLastDescriptorBinding6Texture = texture.get();
            }
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            (void)binding;
            (void)sampler;
        }

        void BindStorageBuffer(uint32_t binding,
                               RHI::BufferPtr buffer,
                               uint32_t offset,
                               uint32_t size) override
        {
            RecordDescriptorBufferBinding(this, binding, buffer);
            if (binding == 5)
            {
                m_Binding5Buffer = buffer;
                GLastDescriptorBinding5Buffer = buffer.get();
                GLastDescriptorBinding5Offset = offset;
                GLastDescriptorBinding5Size = size;
                PushRenderEvent(FakeRenderEvent::BindStorageBuffer5);
            }
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            (void)binding;
            (void)texture;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture, uint32_t mipLevel) override
        {
            (void)binding;
            (void)texture;
            (void)mipLevel;
        }

        void Update() override
        {
            CopyLastUpdateBytes(m_Binding4Buffer, GLastDescriptorBinding4UpdateBytes);
            CopyLastUpdateBytes(m_Binding5Buffer, GLastDescriptorBinding5UpdateBytes);
            if (m_Binding4Buffer || m_Binding5Buffer)
            {
                PushRenderEvent(FakeRenderEvent::DescriptorSetUpdate);
            }
        }

    private:
        static void CopyLastUpdateBytes(const RHI::BufferPtr& buffer,
                                        Container::VariableArray<uint8_t>& outBytes)
        {
            outBytes.clear();
            RHI::TSharedPtr<FakeBuffer> fakeBuffer = Container::DynamicPointerCast<FakeBuffer>(buffer);
            if (!fakeBuffer)
            {
                return;
            }

            outBytes.resize(fakeBuffer->LastUpdateBytes.size());
            if (!outBytes.empty())
            {
                std::memcpy(outBytes.data(), fakeBuffer->LastUpdateBytes.data(), outBytes.size());
            }
        }

        RHI::BufferPtr m_Binding4Buffer;
        RHI::BufferPtr m_Binding5Buffer;
    };

    class FakeShaderCompiler final : public RHI::IShaderCompiler
    {
    public:
        RHI::ShaderCompileResult CompileFromSource(const Container::String& source,
                                                   RHI::ShaderStage stage,
                                                   const Container::String& filename = "shader",
                                                   const Container::String& entryPoint = "main") override
        {
            (void)source;
            (void)stage;
            (void)filename;
            (void)entryPoint;
            return MakeResult();
        }

        RHI::ShaderCompileResult CompileFromFile(const Container::String& filePath,
                                                 RHI::ShaderStage stage,
                                                 const Container::String& entryPoint = "main") override
        {
            (void)filePath;
            (void)stage;
            (void)entryPoint;
            return MakeResult();
        }

    private:
        RHI::ShaderCompileResult MakeResult() const
        {
            RHI::ShaderCompileResult result;
            result.bSuccess = true;
            result.ByteCode.push_back(0x03);
            result.ByteCode.push_back(0x02);
            result.ByteCode.push_back(0x23);
            result.ByteCode.push_back(0x07);
            return result;
        }
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            if (FailBufferDebugName && IsDebugName(desc.DebugName, FailBufferDebugName))
            {
                return nullptr;
            }
            BufferCreationRecord record;
            record.Desc = desc;
            record.Tracker = RHI::MakeShared<FakeBufferLifetimeTracker>();
            CreatedBuffers.push_back(record);

            if (IsDebugName(desc.DebugName, "LightArraySSBO"))
            {
                ++LightArraySSBOCreateCount;
                if (FailLightArraySSBOCreateIndex != 0 &&
                    LightArraySSBOCreateCount == FailLightArraySSBOCreateIndex)
                {
                    return nullptr;
                }
            }

            RHI::BufferPtr buffer = RHI::MakeShared<FakeBuffer>(desc, record.Tracker);
            if (IsDebugName(desc.DebugName, "VisBuffer_SectionMaterials"))
            {
                VisBufferSectionMaterials = buffer;
            }
            if (IsDebugName(desc.DebugName, "LightingVsmSampleParams"))
            {
                LightingVsmSampleBuffer = buffer;
            }
            if (IsDebugName(desc.DebugName, "VisBuffer_RecordUpload"))
            {
                VisBufferRecordUpload = buffer;
            }
            if (IsDebugName(desc.DebugName, "ShadowProbe_Stats"))
            {
                ShadowProbeStatsBuffers.push_back(buffer);
            }
            return buffer;
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return RHI::MakeShared<FakeTexture>(desc);
        }

        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
        {
            return RHI::MakeShared<FakeSampler>(desc);
        }

        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            return RHI::MakeShared<FakeShader>(desc);
        }

        RHI::CommandListPtr CreateCommandList() override
        {
            return RHI::MakeShared<FakeCommandList>();
        }

        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            CreatedRenderPassDescs.push_back(desc);
            return RHI::MakeShared<FakeRenderPass>(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            if (bFailGraphicsPipelines ||
                (bFailLineGraphicsPipelines && desc.rasterState.polygonMode == RHI::PolygonMode::Line))
            {
                return nullptr;
            }
            LastGraphicsPipelineDescriptorSetLayouts = desc.descriptorSetLayouts;
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Graphics,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()),
                                                desc.rasterState.polygonMode);
        }

        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc& desc) override
        {
            if (bFailComputePipelines)
            {
                return nullptr;
            }
            if (FailComputePipelineCreationNumber != 0 && ++ComputePipelineCreations == FailComputePipelineCreationNumber)
            {
                return nullptr;
            }
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Compute,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()));
        }

        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc& desc) override
        {
            LastDescriptorSetDesc = desc;
            ++DescriptorSetCreations;
            return RHI::MakeShared<FakeDescriptorSet>();
        }

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return RHI::MakeShared<FakeShaderCompiler>();
        }

        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::Vulkan; }
        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return m_Capabilities;
        }

        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        // MegaGeometryPass のまとめたカリング・描画が要るデバイスの機能（デバイスアドレス・firstInstance つきの間接描画）を
        // 表明する。それ以外のテストの分岐を変えないよう、MegaGeometryPass を使うテストだけが呼ぶ
        void EnableMegaGeometryBatchCapabilities()
        {
            m_Capabilities.bBufferDeviceAddress = true;
            m_Capabilities.bDrawIndirectFirstInstance = true;
        }

        // ビジビリティバッファの描画（フラグメントシェーダーの gl_PrimitiveID）が要る機能を表明する
        void EnableVisibilityBufferCapabilities()
        {
            EnableMegaGeometryBatchCapabilities();
            m_Capabilities.bGeometryShader = true;
            // 64bit のバッファ（深度 + ID）への atomicMin。ソフトウェアラスタの結果の合流が使う
            m_Capabilities.bShaderInt64 = true;
            m_Capabilities.bShaderBufferInt64Atomics = true;
        }

        // 64bit のバッファへの atomicMin に対応しない装置にする（ソフトウェアラスタの合流が資源もパスも作らない）
        void DisableInt64Atomics()
        {
            m_Capabilities.bShaderInt64 = false;
            m_Capabilities.bShaderBufferInt64Atomics = false;
        }

        // VSM の物理ページのプール（storage buffer）が要る機能と、storage buffer 1 つの束縛の上限（0 は不明）を決める
        void SetVirtualShadowMapCapabilities(bool bFragmentStoresAndAtomics, bool bBufferDeviceAddress, uint64_t maxStorageBufferRange)
        {
            m_Capabilities.bFragmentStoresAndAtomics = bFragmentStoresAndAtomics;
            m_Capabilities.bBufferDeviceAddress = bBufferDeviceAddress;
            m_Capabilities.MaxStorageBufferRange = maxStorageBufferRange;
            // 影の塊の展開・描画（VirtualShadowMapRaster）が、塊ごとの間接描画の firstInstance を要る
            m_Capabilities.bDrawIndirectFirstInstance = true;
            // MegaGeometry のクラスタの記録の間接描画（DrawIndexedIndirectCount）も要る。無い装置は VSM を使わず CSM で描く
            m_Capabilities.bDrawIndirectCount = true;
        }

        // DrawIndexedIndirectCount に対応する装置にする（VSM の MegaGeometry のクラスタの記録は、件数を GPU から読むこの間接描画で描く）
        void EnableDrawIndirectCount()
        {
            m_Capabilities.bDrawIndirectCount = true;
        }

        // DrawIndexedIndirectCount に対応しない装置にする（VSM は MegaGeometry の影を描けず、CSM へ戻る）
        void DisableDrawIndirectCount()
        {
            m_Capabilities.bDrawIndirectCount = false;
        }

        // バッファのアドレスに対応しない装置にする（ソフトウェアラスタの計算シェーダーが頂点を引けない）
        void DisableBufferDeviceAddress()
        {
            m_Capabilities.bBufferDeviceAddress = false;
        }

        // ビジビリティバッファの幾何の解決（頂点のデバイスアドレス・RG16F などの storage image）が要る機能も表明する
        void EnableVisibilityResolveCapabilities()
        {
            EnableVisibilityBufferCapabilities();
            m_Capabilities.bBufferDeviceAddress = true;
            m_Capabilities.bShaderStorageImageExtendedFormats = true;
        }

        Container::VariableArray<RHI::RenderPassDesc> CreatedRenderPassDescs;
        Container::VariableArray<BufferCreationRecord> CreatedBuffers;
        /** @brief ビジビリティバッファの「区間から材質の表の番号への対応」のバッファ（最後に作られたもの） */
        RHI::BufferPtr VisBufferSectionMaterials;
        // 照明が太陽の VSM の読み出しのパラメータを書く定数バッファ（LightingPass が作る）
        RHI::BufferPtr LightingVsmSampleBuffer;
        /** @brief 影の標本の統計の読み戻し先（作った順）。テストが GPU の書き込みの代わりに値を置く */
        Container::VariableArray<RHI::BufferPtr> ShadowProbeStatsBuffers;
        /** @brief ビジビリティバッファのホストが書く記録の置き場（最後に作られたもの。記録の表へコピーされる元） */
        RHI::BufferPtr VisBufferRecordUpload;
        RHI::DescriptorSetDesc LastDescriptorSetDesc;
        Container::VariableArray<RHI::DescriptorSetDesc> LastGraphicsPipelineDescriptorSetLayouts;
        uint32_t LightArraySSBOCreateCount = 0;
        uint32_t FailLightArraySSBOCreateIndex = 0;
        /** @brief nullptr でなければ、この DebugName のバッファの作成が nullptr を返す（資源を作れない装置の再現） */
        const char* FailBufferDebugName = nullptr;
        /** @brief true の間、グラフィックス・計算のパイプラインの作成が nullptr を返す（作れない装置の再現） */
        bool bFailGraphicsPipelines = false;
        /** @brief true の間、線の描き方（PolygonMode::Line）のグラフィックスのパイプラインだけ作成が nullptr を返す */
        bool bFailLineGraphicsPipelines = false;
        bool bFailComputePipelines = false;
        // 0 でなければ、数え始めてから n 番目の計算パイプラインの作成だけを失敗させる（ほかは作れる）
        uint32_t FailComputePipelineCreationNumber = 0;
        uint32_t ComputePipelineCreations = 0;
        // 作った記述子セットの数
        size_t DescriptorSetCreations = 0;

    private:
        RHI::DeviceCapabilities m_Capabilities;
    };

    class MockAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto buffer = Container::MakeUnique<FakeBuffer>(desc);

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Offset = 0;
            allocation.Size = desc.Size;
            allocation.Type = type;

            m_Buffers.push_back(std::move(buffer));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_BufferAllocationCount;
            return allocation;
        }

        void FreeBuffer(RHI::BufferAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Buffers.begin(); it != m_Buffers.end(); ++it)
            {
                if (it->get() == allocation.Buffer)
                {
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Buffers.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc& desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto texture = Container::MakeUnique<FakeTexture>(desc);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;

            m_Textures.push_back(std::move(texture));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_TextureAllocationCount;
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it)
            {
                if (it->get() == allocation.Texture)
                {
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Textures.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        size_t GetAllocatedMemory() const override
        {
            return m_AllocatedMemory;
        }

        size_t GetUsedMemory() const override
        {
            return m_UsedMemory;
        }

        void Trim() override
        {
        }

        size_t GetLiveAllocationCount() const
        {
            return m_Textures.size() + m_Buffers.size();
        }

        uint32_t GetTextureAllocationCount() const
        {
            return m_TextureAllocationCount;
        }

        uint32_t GetBufferAllocationCount() const
        {
            return m_BufferAllocationCount;
        }

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
        size_t m_AllocatedMemory = 0;
        size_t m_UsedMemory = 0;
        uint32_t m_TextureAllocationCount = 0;
        uint32_t m_BufferAllocationCount = 0;
    };

    class EmptyPass final : public IRenderGraphPass
    {
    public:
        EmptyPass(const char* name, uint32_t* executeCount)
            : m_Name(name), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return m_Name; }
        void Declare(RenderGraphBuilder& builder) override { (void)builder; }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_ExecuteCount)
            {
                ++(*m_ExecuteCount);
            }
        }

    private:
        const char* m_Name = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class LogicalPass final : public IRenderGraphPass
    {
    public:
        explicit LogicalPass(uint32_t id, Container::VariableArray<uint32_t>* executed)
            : m_Id(id), m_Executed(executed)
        {
        }

        const char* GetName() const override { return "LogicalPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            if (m_CreateTarget)
            {
                *m_CreateTarget = builder.CreateLogical("LogicalPass.Resource");
            }

            if (m_ReadA)
            {
                builder.Read(*m_ReadA);
            }

            if (m_ReadB)
            {
                builder.Read(*m_ReadB);
            }

            if (m_CreateTarget)
            {
                builder.Write(*m_CreateTarget);
            }

            if (m_WriteExisting)
            {
                builder.Write(*m_WriteExisting);
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_Executed)
            {
                m_Executed->push_back(m_Id);
            }
        }

        RGResourceHandle* m_CreateTarget = nullptr;
        RGResourceHandle* m_ReadA = nullptr;
        RGResourceHandle* m_ReadB = nullptr;
        RGResourceHandle* m_WriteExisting = nullptr;

    private:
        uint32_t m_Id = 0;
        Container::VariableArray<uint32_t>* m_Executed = nullptr;
    };

    class ImportedProducerPass final : public IRenderGraphPass
    {
    public:
        ImportedProducerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.ImportTexture(m_Texture, RHI::ResourceState::Undefined, "ImportedTexture");
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            *m_BufferHandle = builder.ImportBuffer(m_Buffer, RHI::ResourceState::Common, "ImportedBuffer");
            builder.Write(*m_BufferHandle, RHI::ResourceState::CopyDest);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class ImportedConsumerPass final : public IRenderGraphPass
    {
    public:
        ImportedConsumerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
            builder.Read(*m_BufferHandle, RHI::ResourceState::GenericRead);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class TransientProducerPass final : public IRenderGraphPass
    {
    public:
        TransientProducerPass(RGResourceHandle* textureHandle,
                              RGResourceHandle* bufferHandle,
                              RHI::ITexture** resolvedTexture,
                              RHI::IBuffer** resolvedBuffer,
                              uint32_t* executeCount)
            : m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ResolvedTexture(resolvedTexture),
              m_ResolvedBuffer(resolvedBuffer),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "TransientProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "TransientTexture"));
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            RGBufferDesc bufferDesc;
            bufferDesc.Size = 512;
            bufferDesc.Usage = RHI::ResourceUsage::StorageBuffer;
            bufferDesc.DebugName = "TransientBuffer";
            *m_BufferHandle = builder.CreateBuffer(bufferDesc);
            builder.Write(*m_BufferHandle, RHI::ResourceState::UnorderedAccess);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            *m_ResolvedTexture = resources.GetTextureRaw(*m_TextureHandle);
            *m_ResolvedBuffer = resources.GetBufferRaw(*m_BufferHandle);
            assert(*m_ResolvedTexture);
            assert(*m_ResolvedBuffer);
            ++(*m_ExecuteCount);
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        RHI::ITexture** m_ResolvedTexture = nullptr;
        RHI::IBuffer** m_ResolvedBuffer = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class InvalidHandlePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "InvalidHandlePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(RGResourceHandle{});
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }
    };

    class ContextProbePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "ContextProbePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            assert(context);
            m_Width = context->GetActiveRenderWidth();
            m_Height = context->GetActiveRenderHeight();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        uint32_t GetWidth() const
        {
            return m_Width;
        }

        uint32_t GetHeight() const
        {
            return m_Height;
        }

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };

    class PublishSceneDepthAliasPass final : public IRenderGraphPass
    {
    public:
        explicit PublishSceneDepthAliasPass(RGResourceHandle* publishedHandle)
            : m_PublishedHandle(publishedHandle)
        {
        }

        const char* GetName() const override { return "PublishSceneDepthAliasPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle sceneDepth = builder.CreateTextureHandle(
                RGTextureDesc::DepthStencil(32, 16, RHI::Format::D32_FLOAT, "PrepublishedSceneDepth"));
            assert(sceneDepth.IsValid());
            assert(builder.PublishTexture(RenderGraphResourceNames::SceneDepth, sceneDepth));
            if (m_PublishedHandle)
            {
                *m_PublishedHandle = sceneDepth.ToResourceHandle();
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_PublishedHandle = nullptr;
    };

    class NamedShadowMapProducerPass final : public IRenderGraphPass
    {
    public:
        NamedShadowMapProducerPass(RHI::TexturePtr texture, RGResourceHandle* publishedHandle)
            : m_Texture(texture), m_PublishedHandle(publishedHandle)
        {
        }

        const char* GetName() const override { return "NamedShadowMapProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle shadowMap = builder.ImportTexture(m_Texture,
                                                               RHI::ResourceState::ShaderResource,
                                                               "NamedShadowMap");
            assert(shadowMap.IsValid());
            builder.Read(shadowMap, RHI::ResourceState::ShaderResource);
            assert(builder.PublishTexture(RenderGraphResourceNames::ShadowMap, shadowMap));
            if (m_PublishedHandle)
            {
                *m_PublishedHandle = shadowMap;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
        RGResourceHandle* m_PublishedHandle = nullptr;
    };

    class NamedGBufferAlbedoProducerPass final : public IRenderGraphPass
    {
    public:
        explicit NamedGBufferAlbedoProducerPass(RHI::TexturePtr texture)
            : m_Texture(texture)
        {
        }

        const char* GetName() const override { return "NamedGBufferAlbedoProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle albedo = builder.ImportTexture(m_Texture,
                                                            RHI::ResourceState::ShaderResource,
                                                            "PartialGBufferAlbedo");
            assert(albedo.IsValid());
            builder.Read(albedo, RHI::ResourceState::ShaderResource);
            assert(builder.PublishTexture(RenderGraphResourceNames::GBufferAlbedo, albedo));
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
    };

    class FinalStateProducerPass final : public IRenderGraphPass
    {
    public:
        explicit FinalStateProducerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "FinalStateProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "FinalStateTexture"));
            builder.Write(*m_TextureHandle,
                          RHI::ResourceState::RenderTarget,
                          RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class ShaderReadConsumerPass final : public IRenderGraphPass
    {
    public:
        explicit ShaderReadConsumerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "ShaderReadConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class LegacyFXAAProducerPass final : public FXAAPass
    {
    public:
        const char* GetName() const override { return "LegacyFXAAProducerPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
            const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

            m_OutputTextureHandle = builder.CreateTextureHandle(
                RGTextureDesc::RenderTarget(width, height, RHI::Format::R8G8B8A8_UNORM, "LegacyFXAAOutput"));
            m_OutputHandle = m_OutputTextureHandle.ToResourceHandle();
            builder.Write(m_OutputHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    void AssertOrder(const Container::VariableArray<uint32_t>& order,
                     uint32_t a,
                     uint32_t b,
                     uint32_t c)
    {
        assert(order.size() == 3);
        assert(order[0] == a);
        assert(order[1] == b);
        assert(order[2] == c);
    }

    void TestLinearDependencyOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        EmptyPass passC("C", &executeCount);

        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        const uint32_t indexC = graph.AddPass(&passC);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexC));

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), indexA, indexB, indexC);
    }

    void TestDiamondStableOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle root;
        RGResourceHandle left;
        RGResourceHandle right;
        Container::VariableArray<uint32_t> executed;

        LogicalPass rootPass(0, &executed);
        rootPass.m_CreateTarget = &root;
        LogicalPass leftPass(1, &executed);
        leftPass.m_ReadA = &root;
        leftPass.m_CreateTarget = &left;
        LogicalPass rightPass(2, &executed);
        rightPass.m_ReadA = &root;
        rightPass.m_CreateTarget = &right;
        LogicalPass sinkPass(3, &executed);
        sinkPass.m_ReadA = &left;
        sinkPass.m_ReadB = &right;

        graph.AddPass(&rootPass);
        graph.AddPass(&leftPass);
        graph.AddPass(&rightPass);
        graph.AddPass(&sinkPass);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == 0);
        assert(order[1] == 1);
        assert(order[2] == 2);
        assert(order[3] == 3);
    }

    void TestWriteAfterWriteDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass firstWrite(0, &executed);
        firstWrite.m_CreateTarget = &resource;
        LogicalPass secondWrite(1, &executed);
        secondWrite.m_WriteExisting = &resource;

        graph.AddPass(&firstWrite);
        graph.AddPass(&secondWrite);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    void TestWriteAfterReadDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass producer(0, &executed);
        producer.m_CreateTarget = &resource;
        LogicalPass reader(1, &executed);
        reader.m_ReadA = &resource;
        LogicalPass laterWrite(2, &executed);
        laterWrite.m_WriteExisting = &resource;

        graph.AddPass(&producer);
        graph.AddPass(&reader);
        graph.AddPass(&laterWrite);

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), 0, 1, 2);
    }

    void TestImportedResourceBarriers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>();
        RHI::BufferPtr buffer = RHI::MakeShared<FakeBuffer>();
        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        uint32_t executeCount = 0;

        ImportedProducerPass producer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        ImportedConsumerPass consumer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 4);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].BeforeState == RHI::ResourceState::Common);
        assert(barriers[1].AfterState == RHI::ResourceState::CopyDest);
        assert(barriers[1].CompiledOrderIndex == 0);
        assert(barriers[1].BufferSize == buffer->GetSize());
        assert(barriers[2].Kind == RGBarrierKind::Texture);
        assert(barriers[2].BeforeState == RHI::ResourceState::RenderTarget);
        assert(barriers[2].AfterState == RHI::ResourceState::ShaderResource);
        assert(barriers[2].CompiledOrderIndex == 1);
        assert(barriers[3].Kind == RGBarrierKind::Buffer);
        assert(barriers[3].BeforeState == RHI::ResourceState::CopyDest);
        assert(barriers[3].AfterState == RHI::ResourceState::GenericRead);
        assert(barriers[3].CompiledOrderIndex == 1);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));
        assert(executeCount == 2);
        assert(graph.GetLastExecutedPassCount() == 2);
        assert(commandList.Barriers.size() == 4);
        assert(commandList.Barriers[0].Texture == texture.get());
        assert(commandList.Barriers[1].Buffer == buffer.get());
        assert(commandList.Barriers[1].BufferSize == buffer->GetSize());
        assert(commandList.Barriers[2].Texture == texture.get());
        assert(commandList.Barriers[3].Buffer == buffer.get());
    }

    void TestTransientResourcesResolveThroughPool()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        RHI::ITexture* resolvedTexture = nullptr;
        RHI::IBuffer* resolvedBuffer = nullptr;
        uint32_t executeCount = 0;
        TransientProducerPass pass(&textureHandle,
                                   &bufferHandle,
                                   &resolvedTexture,
                                   &resolvedBuffer,
                                   &executeCount);
        graph.AddPass(&pass);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 2);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].AfterState == RHI::ResourceState::UnorderedAccess);
        assert(barriers[1].BufferSize == 512);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));

        assert(executeCount == 1);
        assert(resolvedTexture);
        assert(resolvedBuffer);
        assert(allocator.GetTextureAllocationCount() == 1);
        assert(allocator.GetBufferAllocationCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(commandList.Barriers[0].Texture == resolvedTexture);
        assert(commandList.Barriers[1].Buffer == resolvedBuffer);
        assert(commandList.Barriers[1].BufferSize == 512);

        pool.EndFrame();
        graph.Shutdown();
        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
    }

    void TestCycleDetectionDoesNotExecute()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexA));

        assert(!graph.Compile());
        ViewRenderContext context;
        assert(!graph.Execute(context));
        assert(executeCount == 0);
        assert(graph.GetLastExecutedPassCount() == 0);
    }

    void TestInvalidHandleRejected()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        InvalidHandlePass pass;
        graph.AddPass(&pass);
        assert(!graph.Compile());
    }

    void TestCompileContextPassedToDeclare()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ContextProbePass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 320;
        context.RenderHeight = 180;

        assert(graph.Compile(context));
        assert(pass.GetWidth() == 320);
        assert(pass.GetHeight() == 180);
    }

    void TestWriteFinalStateSuppressesFollowupReadBarrier()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle textureHandle;
        FinalStateProducerPass producer(&textureHandle);
        ShaderReadConsumerPass consumer(&textureHandle);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 1);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    // ビジビリティバッファの資源（VisBuffer.Id と描画の記録の表）を名前つきで書くパス
    class VisibilityBufferProducerPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "VisibilityBufferProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const RGTextureHandle id = builder.WriteTextureAttachment(
                RenderGraphResourceNames::VisBufferId,
                VisibilityBuffer::MakeIdTextureDesc(64, 32),
                RGAttachmentKind::Color,
                RHI::AttachmentLoadOp::Clear,
                RHI::AttachmentStoreOp::Store,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            const RGBufferHandle records = builder.WriteBuffer(RenderGraphResourceNames::VisBufferDrawRecords,
                                                               VisibilityBuffer::MakeRecordTableBufferDesc(16),
                                                               RHI::ResourceState::UnorderedAccess,
                                                               RHI::ResourceState::GenericRead);
            assert(id.IsValid() && records.IsValid());
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    // 名前で VisBuffer.Id と記録の表を引いて読むパス（後の材質の分類・解決が同じ形で読む）
    class VisibilityBufferConsumerPass final : public IRenderGraphPass
    {
    public:
        explicit VisibilityBufferConsumerPass(bool* foundBoth)
            : m_FoundBoth(foundBoth)
        {
        }

        const char* GetName() const override { return "VisibilityBufferConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle id;
            RGBufferHandle records;
            *m_FoundBoth = builder.TryGetTexture(RenderGraphResourceNames::VisBufferId, id) &&
                           builder.TryGetBuffer(RenderGraphResourceNames::VisBufferDrawRecords, records);
            if (*m_FoundBoth)
            {
                builder.Read(id.ToResourceHandle(), RHI::ResourceState::ShaderResource);
                builder.Read(records.ToResourceHandle(), RHI::ResourceState::GenericRead);
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        bool* m_FoundBoth = nullptr;
    };

    void TestVisibilityBufferResourcesDeclareAndRead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        bool consumerFoundBoth = false;
        VisibilityBufferProducerPass producer;
        VisibilityBufferConsumerPass consumer(&consumerFoundBoth);
        const uint32_t producerIndex = graph.AddPass(&producer);
        const uint32_t consumerIndex = graph.AddPass(&consumer);

        assert(graph.Compile());
        assert(consumerFoundBoth);

        uint32_t version = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferId, version));
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferDrawRecords, version));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == producerIndex);
        assert(order[1] == consumerIndex);

        // 書く側の最初の状態（添付・UAV）への遷移が積まれる。
        bool sawIdTarget = false;
        bool sawRecordsWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.Kind == RGBarrierKind::Texture && barrier.AfterState == RHI::ResourceState::RenderTarget)
            {
                sawIdTarget = true;
            }
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::UnorderedAccess)
            {
                sawRecordsWrite = true;
            }
        }
        assert(sawIdTarget);
        assert(sawRecordsWrite);
    }

    // 最後のシーンの色（Scene.Color）を書くだけのパス。VisibilityDebugPass が重ねて書く先を用意する
    class SceneColorProducerPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "SceneColorProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const RGTextureHandle color = builder.WriteTextureAttachment(
                RenderGraphResourceNames::SceneColor,
                RGTextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "Test_SceneColor"),
                RGAttachmentKind::Color,
                RHI::AttachmentLoadOp::Clear,
                RHI::AttachmentStoreOp::Store,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::RenderTarget);
            assert(color.IsValid());
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    // ビジビリティバッファの描画（--visibility-buffer=on）の記録に使う、2つの MegaMesh インスタンスを持つシーンの一式。
    // GBufferPass → MegaGeometryPass（描画の写しを作る）→ VisibilityRasterPass の順に RenderGraph で実行する
    struct VisibilityRasterScene
    {
        RHI::TSharedPtr<FakeDevice> Device;
        ShaderManager ShaderMgr;
        MockAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderResources Resources;
        SceneRenderer Renderer;
        RenderGraph Graph;
        GBufferPass GBuffer;
        MegaGeometryPass Mega;
        VisibilityRasterPass Raster;
        MaterialTileClassifyPass Classify;
        SkinningComputePass Skinning;
        VisibilityResolvePass Resolve;
        SceneColorProducerPass SceneColorProducer;
        VisibilityDebugPass DebugView;
        FakeCommandList CommandList;
        Container::VariableArray<DrawCommand> OpaqueCommands;
        Container::VariableArray<Container::TSharedPtr<const SkinnedMeshFrameLease>> SkinnedLeases;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        Container::VariableArray<MegaGeometryProxy> Proxies;
        /** @brief 材質の違う描画を足した構成で、手続きメッシュの描画が引く描画のインスタンスの表 */
        RHI::BufferPtr InstanceData;
        MaterialHandle MaterialA;
        MaterialHandle MaterialB;
        CameraProxy Camera;
        /** @brief false なら、64bit のバッファへの atomicMin に対応しない装置で動かす */
        bool bInt64Atomics = true;
        /**
         * @brief false なら、MegaGeometry の初期化の後・ID のラスタの初期化の前に、バッファのアドレスに対応しない装置へ落とす
         *
         * MegaGeometry の初期化はバッファのアドレスを要るので、初期化の前から落とすとパスごと無効になる。
         * 落とした後は、ID のラスタの合流の作成と、MegaGeometry の振り分けの判定が、バッファのアドレスの有無だけを見る
         */
        bool bBufferDeviceAddress = true;
        /** @brief ID のラスタがソフトウェアラスタ（64bit のバッファの埋め・合流）を持つか。false は --sw-raster=off と同じ */
        bool bSwRasterMerge = true;
        /** @brief MegaGeometry がソフトウェアラスタへの振り分けを要求するか（--sw-raster=on のカリング） */
        bool bSwRasterBin = false;
        /** @brief 振り分けるクラスタの画面上の半径（画素）のしきい値 */
        float SwRasterMaxPixels = 8.0f;
        /** @brief 0 でなければ、ID のラスタの初期化で、この番号（1 から数える）の計算パイプラインの作成を失敗させる（3 番がソフトウェアラスタ） */
        uint32_t FailRasterComputePipelineNumber = 0;
        /** @brief nullptr でなければ、Execute の間この DebugName のバッファの作成を失敗させる（初期化の後の資源の作成の失敗） */
        const char* FailBufferDebugName = nullptr;
        /** @brief MegaGeometry のメッシュ A が持つクラスタの数（B は 1 つ）。間接 dispatch の x の上限（65535）を超える一覧の検査に使う */
        uint32_t ClusterCountA = 1;
        /** @brief Normal 以外のときは、ビューポートの計画（表示だけを持つ）を現在のビューポートにして、この表示で描く */
        DebugViewMode DebugMode = DebugViewMode::Normal;
        ViewportRenderPlan ViewportPlan;
        ViewRenderContext Context;
    };

    // 材質のタイル分類のパスの足し方
    enum class ClassifyMode
    {
        None,
        /** @brief 描画のパスから記録の表を受け取る */
        LinkedToRaster,
        /** @brief 記録の表の取り出し元を渡さない（分類できないときの安全側の動き） */
        WithoutRaster,
        /** @brief SceneView と同じく、記録の表の取り出し元を渡してグラフへ足すが、有効にしない（既定のまま） */
        AddedDisabled,
        /** @brief SceneView の On と同じ配線: 有効にして解決より前に足し、解決のパスへ分類のパスを渡す（解決を使う構成で使う） */
        BeforeResolve,
        /** @brief BeforeResolve で、記録の表の取り出し元を分類へ渡さない（そのフレームの分類が記録できない） */
        BeforeResolveWithoutRaster,
        /** @brief BeforeResolve で、分類の計算パイプラインが作れない（分類は何も宣言しない） */
        BeforeResolvePipelineUnavailable,
        /** @brief BeforeResolve で、解決の材質ごとの形の計算パイプラインだけが作れない（直接 dispatch の形のパイプラインは作れる） */
        BeforeResolveTilePipelineUnavailable,
    };

    // 幾何の解決（--visibility-buffer=on）の足し方
    enum class ResolveMode
    {
        /** @brief 解決を使わない（GBuffer の描画が残る） */
        None,
        /** @brief SceneView の On と同じ配線（GBufferPass・MegaGeometryPass の描画を止め、解決のパスを足す）。装置は解決に対応する */
        Supported,
        /** @brief 配線は同じだが、装置が解決に対応しない（頂点のデバイスアドレス・拡張形式の storage image が無い） */
        UnsupportedDevice,
        /** @brief 配線は同じで装置も対応するが、ID のラスタのパイプラインが作れない */
        RasterPipelineUnavailable,
        /** @brief 配線は同じで装置も対応するが、解決の計算パイプラインが作れない */
        ResolvePipelineUnavailable,
        /** @brief 配線は同じで装置も対応し、塗りのパイプラインは作れるが、ID のラスタの線の描き方のパイプラインが作れない */
        RasterWireframePipelineUnavailable,
        /** @brief 配線は同じで装置も対応するが、計算スキニングのパイプラインだけが作れない（bSkinning と合わせて使う） */
        SkinningComputePipelineUnavailable,
    };

    // bVisibilityPlan=false は --visibility-buffer=off（MegaGeometryPass が描画の写しを作らず、VisibilityRasterPass も足さない）
    // bSkinning=true は、スキニングの描画1件と貸し出しを渡して SkinningComputePass を足す。
    // SkinnedMeshes は渡さない（変形を1つも記録できないフレーム）ので、dispatch は増えない。
    // bMaterialDraws=true（bSkinning も true）は、実物の材質 A・B を作り、手続きメッシュの描画 3 件（A・A・B）と
    // 材質 A のスキニングの描画 1 件を足す。SkinnedMeshes を渡すので、スキニングの描画も記録になる。
    // 描画のコマンドの元の MaterialIndex は、記録の番号と取り違えないよう描画ごとに違う値（5・6・7・9）にする
    // bDebugView=true（bVisibilityPlan も true）は、最後のシーンの色を書くパスと ID の検証表示（--visibility-buffer=debug）を
    // 描画のパスの後ろ（分類・解決より後）に足す
    void RunVisibilityRasterScene(VisibilityRasterScene& scene,
                                  bool bVisibilityPlan,
                                  bool bOcclusionCulling,
                                  ClassifyMode classifyMode = ClassifyMode::None,
                                  bool bSkinning = false,
                                  bool bMaterialDraws = false,
                                  ResolveMode resolveMode = ResolveMode::None,
                                  uint32_t skinnedInstanceCount = 1,
                                  bool bDebugView = false)
    {
        scene.Device = RHI::MakeShared<FakeDevice>();
        if (resolveMode != ResolveMode::None && resolveMode != ResolveMode::UnsupportedDevice)
        {
            scene.Device->EnableVisibilityResolveCapabilities();
        }
        else
        {
            scene.Device->EnableVisibilityBufferCapabilities();
        }
        if (!scene.bInt64Atomics)
        {
            scene.Device->DisableInt64Atomics();
        }
        assert(scene.ShaderMgr.Initialize(scene.Device.get(), TestShaderDirectory));
        assert(scene.Pool.Initialize(&scene.Allocator, 1));
        scene.Pool.BeginFrame(0);
        assert(scene.Resources.Initialize(scene.Device));
        scene.Resources.MegaGeometry().SetOcclusionCullingEnabled(bOcclusionCulling);
        assert(scene.Renderer.Initialize(scene.Device.get(), nullptr, &scene.Pool));
        assert(scene.Graph.Initialize(&scene.Pool));
        scene.Graph.BeginFrame(0);
        scene.GBuffer.SetSceneRenderer(&scene.Renderer);
        if (resolveMode != ResolveMode::None)
        {
            scene.GBuffer.SetVisibilityResolveActive(true);
            scene.Mega.SetSkipGBufferDraw(true);
        }

        float vertices[12] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};
        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;
        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        for (uint32_t clusterIndex = 0; clusterIndex < scene.ClusterCountA; ++clusterIndex)
        {
            createInfo.Clusters.push_back(cluster);
        }
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "VisRasterMegaA";
        const auto megaMeshA = scene.Resources.MegaGeometry().CreateMegaMesh(createInfo);
        createInfo.Clusters.clear();
        createInfo.Clusters.push_back(cluster);
        createInfo.DebugName = "VisRasterMegaB";
        const auto megaMeshB = scene.Resources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshA.IsValid() && megaMeshB.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(scene.Resources));

        MegaGeometryProxy proxyA;
        proxyA.ObjectId = 1;
        proxyA.ComponentId = 10;
        proxyA.MegaMeshHandle = megaMeshA;
        proxyA.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxyA.WorldBounds = createInfo.TotalBounds;
        scene.Proxies.push_back(proxyA);
        MegaGeometryProxy proxyB = proxyA;
        proxyB.ObjectId = 2;
        proxyB.ComponentId = 20;
        proxyB.MegaMeshHandle = megaMeshB;
        scene.Proxies.push_back(proxyB);

        if (bMaterialDraws)
        {
            MaterialCreateData createA;
            createA.BaseColor[0] = 0.2f;
            createA.BaseColor[1] = 0.4f;
            createA.BaseColor[2] = 0.6f;
            createA.Roughness = 0.3f;
            scene.MaterialA = scene.Resources.Materials().Create(createA);
            MaterialCreateData createB;
            createB.BaseColor[0] = 0.9f;
            createB.BaseColor[1] = 0.1f;
            createB.BaseColor[2] = 0.1f;
            scene.MaterialB = scene.Resources.Materials().Create(createB);
            assert(scene.MaterialA.IsValid() && scene.MaterialB.IsValid());

            MeshDataHandle meshHandle;
            meshHandle.Id = 7701;
            assert(scene.Resources.Meshes().Register(meshHandle, vertices, sizeof(vertices), indices, 3));
            const auto addMeshDraw = [&](MaterialHandle material, uint32_t legacyMaterialIndex, uint64_t objectId)
            {
                DrawCommand meshCommand;
                meshCommand.Draw.PayloadKind = DrawPayloadKind::Mesh;
                meshCommand.Draw.MeshHandle = meshHandle;
                meshCommand.Draw.MaterialHandle = material;
                meshCommand.Draw.MaterialIndex = legacyMaterialIndex;
                meshCommand.Draw.ObjectId = objectId;
                scene.OpaqueCommands.push_back(meshCommand);
            };
            addMeshDraw(scene.MaterialA, 5, 101);
            addMeshDraw(scene.MaterialA, 6, 102);
            addMeshDraw(scene.MaterialB, 7, 103);
            scene.InstanceData = RHI::MakeShared<FakeBuffer>();
        }

        if (bSkinning)
        {
            // 2 体目以降は頂点数を変えて（3 + 3 * 体の番号）、出力の中の先頭の頂点番号（VertexBase）を 0 以外にする
            for (uint32_t body = 0; body < skinnedInstanceCount; ++body)
            {
                const uint32_t vertexCount = 3 + 3 * body;
                Container::VariableArray<SkinnedMeshVertex> skinVertices;
                skinVertices.resize(vertexCount);
                for (SkinnedMeshVertex& vertex : skinVertices)
                {
                    vertex.BoneWeights[0] = 1.0f;
                }
                Container::VariableArray<uint32_t> skinIndices;
                for (uint32_t index = 0; index < 3; ++index)
                {
                    skinIndices.push_back(index);
                }
                auto assetLease = Container::MakeShared<SkinnedMeshAssetLease>(
                    SkinnedMeshHandle{1 + body, 1}, std::move(skinVertices), std::move(skinIndices));
                scene.SkinnedLeases.push_back(Container::MakeShared<SkinnedMeshFrameLease>(assetLease));

                DrawCommand skinnedCommand;
                skinnedCommand.Draw.PayloadKind = DrawPayloadKind::Skinned;
                if (body > 0)
                {
                    skinnedCommand.Draw.ObjectId = 200 + body;
                }
                skinnedCommand.Skinned.FrameLeaseIndex = body;
                skinnedCommand.Skinned.BonePalette.push_back(NorvesLib::Math::Matrix4x4::Identity);
                if (bMaterialDraws)
                {
                    skinnedCommand.Draw.MaterialHandle = scene.MaterialA;
                    skinnedCommand.Draw.MaterialIndex = 9;
                }
                scene.OpaqueCommands.push_back(skinnedCommand);
            }
        }

        scene.Camera.Viewport.Width = 128.0f;
        scene.Camera.Viewport.Height = 64.0f;

        ViewRenderContext& context = scene.Context;
        context.CommandList = &scene.CommandList;
        context.Device = scene.Device.get();
        context.TransientPool = &scene.Pool;
        context.ShaderMgr = &scene.ShaderMgr;
        context.Renderer = &scene.Renderer;
        context.PendingFrameCommands = &scene.PendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.MainCamera = &scene.Camera;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(scene.OpaqueCommands);
        context.SnapshotMegaGeometryProxies = &scene.Proxies;
        if (bSkinning)
        {
            context.SnapshotSkinnedMeshFrameLeases = &scene.SkinnedLeases;
        }
        if (bMaterialDraws)
        {
            scene.Resources.SkinnedMeshes().BeginFrame(0);
            context.SkinnedMeshes = &scene.Resources.SkinnedMeshes();
            context.InstanceDataBuffer = scene.InstanceData;
        }
        context.Resources.Textures = &scene.Resources.Textures();
        context.Resources.Materials = &scene.Resources.Materials();
        context.Resources.Meshes = &scene.Resources.Meshes();
        context.Resources.MegaGeometry = &scene.Resources.MegaGeometry();
        if (scene.DebugMode != DebugViewMode::Normal)
        {
            // 現在のビューポートがあると、描画のコマンドは Current* から取る。同じ描画を渡し、表示だけを変える
            scene.ViewportPlan.DebugMode = scene.DebugMode;
            context.CurrentViewport = &scene.ViewportPlan;
            context.CurrentDrawCommands = DrawCommandView::FromArray(scene.OpaqueCommands);
            context.CurrentOpaqueCommands = DrawCommandView::FromArray(scene.OpaqueCommands);
        }

        scene.Mega.SetVisibilityDrawPlanEnabled(bVisibilityPlan);
        scene.Mega.SetSwRasterBinning(scene.bSwRasterBin, scene.SwRasterMaxPixels);
        scene.Raster.SetSwRasterEnabled(scene.bSwRasterMerge);
        scene.Raster.SetMegaGeometryPass(&scene.Mega);
        assert(scene.Mega.Initialize(context));
        if (!scene.bBufferDeviceAddress)
        {
            scene.Device->DisableBufferDeviceAddress();
        }
        scene.Graph.AddPass(&scene.GBuffer);
        scene.Graph.AddPass(&scene.Mega);
        if (bVisibilityPlan)
        {
            if (bSkinning)
            {
                // SceneView と同じく、描画のパスへつないでからグラフへ足す（既定は無効なので、有効にする）
                scene.Skinning.SetEnabled(true);
                scene.Raster.SetSkinningComputePass(&scene.Skinning);
                scene.Device->bFailComputePipelines = resolveMode == ResolveMode::SkinningComputePipelineUnavailable;
                assert(scene.Skinning.Initialize(context));
                scene.Device->bFailComputePipelines = false;
                assert(scene.Skinning.IsComputeReady() == (resolveMode != ResolveMode::SkinningComputePipelineUnavailable));
                scene.Graph.AddPass(&scene.Skinning);
            }
            scene.Device->bFailGraphicsPipelines = resolveMode == ResolveMode::RasterPipelineUnavailable;
            scene.Device->bFailLineGraphicsPipelines = resolveMode == ResolveMode::RasterWireframePipelineUnavailable;
            if (scene.FailRasterComputePipelineNumber != 0)
            {
                scene.Device->ComputePipelineCreations = 0;
                scene.Device->FailComputePipelineCreationNumber = scene.FailRasterComputePipelineNumber;
            }
            assert(scene.Raster.Initialize(context));
            scene.Device->bFailGraphicsPipelines = false;
            scene.Device->bFailLineGraphicsPipelines = false;
            scene.Device->FailBufferDebugName = scene.FailBufferDebugName;
            scene.Device->FailComputePipelineCreationNumber = 0;
            scene.Graph.AddPass(&scene.Raster);

            const bool bClassifyBeforeResolve = classifyMode == ClassifyMode::BeforeResolve ||
                                                classifyMode == ClassifyMode::BeforeResolveWithoutRaster ||
                                                classifyMode == ClassifyMode::BeforeResolvePipelineUnavailable ||
                                                classifyMode == ClassifyMode::BeforeResolveTilePipelineUnavailable;
            const auto addClassifyPass = [&]()
            {
                if (classifyMode == ClassifyMode::None)
                {
                    return;
                }
                if (classifyMode != ClassifyMode::AddedDisabled)
                {
                    scene.Classify.SetEnabled(true);
                }
                if (classifyMode != ClassifyMode::WithoutRaster && classifyMode != ClassifyMode::BeforeResolveWithoutRaster)
                {
                    scene.Classify.SetRasterPass(&scene.Raster);
                }
                scene.Device->bFailComputePipelines = classifyMode == ClassifyMode::BeforeResolvePipelineUnavailable;
                assert(scene.Classify.Initialize(context));
                scene.Device->bFailComputePipelines = false;
                // View::Render と同じく、無効なパスはグラフへ足さない
                if (scene.Classify.IsEnabled())
                {
                    scene.Graph.AddPass(&scene.Classify);
                }
            };
            // SceneView の On は、分類を解決より前に足す（解決が分類の引数・一覧を読むため）
            if (bClassifyBeforeResolve)
            {
                addClassifyPass();
            }
            if (resolveMode != ResolveMode::None)
            {
                // SceneView の On と同じく、描画のパスの後に足し、GBufferPass・MegaGeometryPass から使えるかを問い合わせられるようにする
                scene.Resolve.SetRasterPass(&scene.Raster);
                if (bSkinning)
                {
                    scene.Resolve.SetSkinningComputePass(&scene.Skinning);
                    scene.Skinning.SetResolvePass(&scene.Resolve);
                }
                if (bClassifyBeforeResolve)
                {
                    scene.Resolve.SetClassifyPass(&scene.Classify);
                }
                scene.GBuffer.SetVisibilityResolvePass(&scene.Resolve);
                scene.Mega.SetVisibilityResolvePass(&scene.Resolve);
                // 2パスの遮蔽の HZB を ID のラスタの深度から作るため、MegaGeometryPass の記録をラスタの Execute へ移す
                scene.Mega.SetVisibilityRasterPass(&scene.Raster);
                scene.Raster.SetResolvePass(&scene.Resolve);
                scene.Device->bFailComputePipelines = resolveMode == ResolveMode::ResolvePipelineUnavailable;
                if (classifyMode == ClassifyMode::BeforeResolveTilePipelineUnavailable)
                {
                    // 解決の初期化は直接 dispatch の形・材質ごとの形の順に 2 つ作る。2 つ目（材質ごとの形）だけが作れない
                    scene.Device->ComputePipelineCreations = 0;
                    scene.Device->FailComputePipelineCreationNumber = 2;
                }
                assert(scene.Resolve.Initialize(context));
                scene.Device->bFailComputePipelines = false;
                scene.Device->FailComputePipelineCreationNumber = 0;
                scene.Graph.AddPass(&scene.Resolve);
            }
            if (!bClassifyBeforeResolve)
            {
                addClassifyPass();
            }
            if (bDebugView)
            {
                // SceneView と同じく、記録の表の取り出し元を渡して、最後のパスとして足す
                scene.DebugView.SetRasterPass(&scene.Raster);
                const bool bDebugReady = scene.DebugView.Initialize(context);
                assert(bDebugReady);
                scene.Graph.AddPass(&scene.SceneColorProducer);
                scene.Graph.AddPass(&scene.DebugView);
            }
        }
        assert(scene.Graph.Compile(context));
        const RenderGraphExecutionResult result = scene.Graph.ExecuteWithResult(context);
        assert(result.bSuccess);
    }

    void ShutdownVisibilityRasterScene(VisibilityRasterScene& scene)
    {
        scene.DebugView.Shutdown();
        scene.Resolve.Shutdown();
        scene.Classify.Shutdown();
        scene.Raster.Shutdown();
        scene.Skinning.Shutdown();
        scene.Mega.Shutdown();
        scene.GBuffer.Shutdown();
        scene.Renderer.Shutdown();
        scene.Resources.Shutdown();
        scene.Graph.Shutdown();
        scene.Pool.EndFrame();
        scene.Pool.Shutdown();
        scene.ShaderMgr.Shutdown();
    }

    // --visibility-buffer=on: MegaGeometry の2パスのコマンドを、そのまま ID と深度のレンダーパスで描き直す。
    // コマンドは MegaGeometryPass が積んだ範囲（1パス目・2パス目）を使い、記録は GPU（計算）が1回の dispatch で書く。
    void TestVisibilityRasterOnRecordsMegaDrawsAndIdPass()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（2パス）・ビジビリティバッファ（ID）の4つのレンダーパスと、64bit のバッファの合流の 1 つ
        // （GBuffer が先に描く構成は 1 回の render pass なので、合流も描画の後の 1 回）
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(commandList.BeginRenderPassCount == 5);
        assert(commandList.EndRenderPassCount == 5);

        // 間接描画: MegaGeometry の 1・2 パス目（コマンドの範囲は 0 と 2 * 20 バイト）の後に、同じ範囲をもう一度
        assert(commandList.IndirectDraws.size() == 4);
        for (size_t drawIndex = 0; drawIndex < 2; ++drawIndex)
        {
            assert(commandList.IndirectDraws[2 + drawIndex].OffsetBytes == commandList.IndirectDraws[drawIndex].OffsetBytes);
            assert(commandList.IndirectDraws[2 + drawIndex].MaxDrawCount == commandList.IndirectDraws[drawIndex].MaxDrawCount);
        }
        assert(commandList.IndirectDraws[3].OffsetBytes == 2 * 20);

        // dispatch: カリング 2 回 + HZB 7 段 + 記録を書く計算の引数を作る計算 1 回（記録を書く計算は間接 dispatch）
        assert(commandList.DispatchCount == 10);

        // 記録を書く計算は、引数を作る計算が書いたバッファの先頭から間接 dispatch で走る（固定の大きさの dispatch ではない）
        assert(commandList.IndirectDispatches.size() == 1);
        assert(std::strcmp(commandList.IndirectDispatches[0].BufferName, "VisBuffer_RecordArgs") == 0);
        assert(commandList.IndirectDispatches[0].OffsetBytes == 0);

        // 並びの最後は、記録を書く計算の引数を作る計算（D）→ 記録を書く計算（J）→ ID のレンダーパス（B → 間接描画 2 回 → E）
        // → 64bit のバッファの合流（B → E。全画面の描画 1 回）
        const auto& sequence = commandList.CallSequence;
        const char tail[] = {'D', 'J', 'B', 'I', 'I', 'E', 'B', 'E'};
        assert(sequence.size() > sizeof(tail));
        for (size_t i = 0; i < sizeof(tail); ++i)
        {
            assert(sequence[sequence.size() - sizeof(tail) + i] == tail[i]);
        }

        // 記録の枠: 0 番の空 + MegaGeometry のコマンド（1 パス 2 つ × 2 パス）。手続き・スキニングの描画は無い
        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(stats.MegaCommandSlots == 4);
        assert(stats.ProceduralRecords == 0 && stats.SkinnedRecords == 0);
        assert(stats.TotalSlots == 1 + 4);
        assert(scene.Raster.GetRecordTable());
        assert(scene.Raster.GetRecordTableBytes() == 5 * sizeof(VisibilityBuffer::DrawRecord));

        // 64bit のバッファ: 画面の画素数（128x64）× 8 バイトを、すべてのビットが 1 で 1 回埋め、1 回合流した
        assert(stats.bMerged && stats.MergeCount == 1);
        assert(stats.KeyBufferBytes == 128u * 64u * 8u);
        assert(commandList.Key64Fills.size() == 1);
        assert(commandList.Key64Fills[0].SizeBytes == 128u * 64u * 8u);
        assert(commandList.Key64Fills[0].Value == 0xFFFFFFFFu);

        // ID は R32_UINT の1枚のカラー添付（空の ID で消す）と、GBuffer が書いた深度の Load。どちらも ShaderResource で終わる
        // もう 1 つは、2 パスの遮蔽の途中で続けて開く 2 回目の render pass（ID・深度とも Load で、ShaderResource から始まる）。
        // この構成（GBuffer が先に描く）は 1 回目の render pass だけを使う
        bool bFoundIdRenderPass = false;
        bool bFoundSecondIdRenderPass = false;
        for (const RHI::RenderPassDesc& desc : scene.Device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 1 || desc.colorAttachments[0].format != RHI::Format::R32_UINT)
            {
                continue;
            }
            const RHI::AttachmentDesc& id = desc.colorAttachments[0];
            assert(id.finalState == RHI::ResourceState::ShaderResource);
            assert(desc.hasDepthStencil);
            assert(desc.depthStencilAttachment.loadOp == RHI::AttachmentLoadOp::Load);
            assert(desc.depthStencilAttachment.finalState == RHI::ResourceState::ShaderResource);
            if (id.initialState == RHI::ResourceState::ShaderResource)
            {
                assert(id.loadOp == RHI::AttachmentLoadOp::Load);
                assert(desc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource);
                bFoundSecondIdRenderPass = true;
                continue;
            }
            assert(id.loadOp == RHI::AttachmentLoadOp::Clear);
            assert(id.clearColorUint[0] == VisibilityBuffer::EMPTY_ID);
            assert(id.initialState == RHI::ResourceState::RenderTarget);
            assert(desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite);
            bFoundIdRenderPass = true;
        }
        assert(bFoundIdRenderPass);
        assert(bFoundSecondIdRenderPass);

        // MegaGeometryPass が残した IndirectDraw・カウンタ・描画情報は、ID の描画の後に Common へ戻る（記録を書く計算のために
        // コマンド・カウンタは GenericRead へ渡され、そこから戻る）。ID の描画より前には戻らない
        size_t commonBarriers = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::Common &&
                barrier.BeforeState == RHI::ResourceState::GenericRead)
            {
                const auto* fakeBuffer = static_cast<const FakeBuffer*>(barrier.Buffer);
                const char* name = fakeBuffer->GetDesc().DebugName;
                if (IsDebugName(name, "MegaGeometry_IndirectDraw") || IsDebugName(name, "MegaGeometry_DrawCount") ||
                    IsDebugName(name, "MegaGeometry_DrawInfo"))
                {
                    ++commonBarriers;
                }
            }
        }
        assert(commonBarriers == 3);

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質のタイルの一覧の大きさ: 既定の見込み（1 タイル MAX_MATERIALS_PER_TILE）は、どの画面の大きさでも
    // 一覧が溢れない（タイルごとの材質の数は画素の数を超えない）。溢れると落ちたタイルの物が欠けるので、
    // 既定の見込みを小さくすると落ちる。1080p・4K の大きさ（VRAM の予算の記録）も固定する
    void TestMaterialTileListCapacityNeverOverflowsAtDefault()
    {
        const uint32_t sizes[][2] = {{1, 1}, {7, 9}, {128, 64}, {1280, 720}, {1920, 1080}, {3840, 2160}, {7680, 4320}};
        for (const auto& size : sizes)
        {
            const MaterialTiles::Layout layout = MaterialTiles::ComputeLayout(size[0], size[1]);
            assert(layout.IsValid());
            assert(layout.ListCapacity == layout.TileCount * MaterialTiles::MAX_MATERIALS_PER_TILE);
            // タイルは画面を覆う最小の数。タイルの外の画素も数えた画素数以上なら、全画素が別の材質でも収まる
            assert(static_cast<uint64_t>(layout.ListCapacity) >= static_cast<uint64_t>(size[0]) * size[1]);
        }

        assert(MaterialTiles::ComputeLayout(1920, 1080).ListBytes() == 8294400ull);
        assert(MaterialTiles::ComputeLayout(3840, 2160).ListBytes() == 33177600ull);
        // 材質の表の上限は 1 タイルの画素数より大きいので、表の上限からは見込みを小さくできない
        assert(MaterialTiles::DEFAULT_MAX_MATERIALS >= MaterialTiles::MAX_MATERIALS_PER_TILE);
    }

    // 材質のタイル分類のパス: VisBuffer.Id を読み、引数・一覧・カーソル・統計を書く。3 回の dispatch で分類し、
    // 引数は間接 dispatch の引数として読める用途と状態（GenericRead）で後のパスへ渡す
    void TestMaterialTileClassifyDispatchesAndPublishesArgs()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::LinkedToRaster);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（2パス）・ビジビリティ・材質のタイル分類の4つのパス
        assert(scene.Graph.GetLastExecutedPassCount() == 4);
        // 記録を書く計算まで 10 回 + 分類の 3 回（数える・位置を決める・書き出す）
        assert(commandList.DispatchCount == 13);
        assert(scene.Classify.WasClassified());

        // 画面は 128x64 なので 16x8 のタイル。一覧は 1 タイルが出せる材質の最大数ぶん
        const MaterialTiles::Layout& layout = scene.Classify.GetLayout();
        assert(layout.IsValid());
        assert(layout.TilesX == 16 && layout.TilesY == 8 && layout.TileCount == 128);
        assert(layout.MaxMaterials == MaterialTiles::DEFAULT_MAX_MATERIALS);
        assert(layout.ListCapacity == 128 * MaterialTiles::MAX_MATERIALS_PER_TILE);
        assert(layout.ArgsBytes() ==
               static_cast<uint64_t>(MaterialTiles::DEFAULT_MAX_MATERIALS) * MaterialTiles::ARGS_STRIDE_BYTES);
        // 引数の x は実機が保証する上限（65535）以下に抑える。超える分は y へ広げる
        assert(layout.GroupCountXLimit == MaterialTiles::MAX_GROUP_COUNT_X);

        // 数え始める前に、引数と統計だけを 0 にする（一覧とカーソルは分類が書く範囲しか読まれない）
        bool bArgsFilled = false;
        bool bStatsFilled = false;
        for (const FakeCommandList::MaterialTileFill& fill : commandList.MaterialTileFills)
        {
            if (IsDebugName(fill.BufferName, "MaterialTile_Args"))
            {
                assert(fill.SizeBytes == layout.ArgsBytes());
                bArgsFilled = true;
            }
            else if (IsDebugName(fill.BufferName, "MaterialTile_Stats"))
            {
                assert(fill.SizeBytes == MaterialTiles::STATS_BYTES);
                bStatsFilled = true;
            }
            else
            {
                assert(false);
            }
        }
        assert(bArgsFilled && bStatsFilled);

        // 引数と一覧は、後の材質の解決が読めるよう、このパスが GenericRead へ遷移させて渡す。引数のバッファは間接引数の用途を持つ
        bool bArgsToGenericRead = false;
        bool bListToGenericRead = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind != RGBarrierKind::Buffer || barrier.BeforeState != RHI::ResourceState::UnorderedAccess ||
                barrier.AfterState != RHI::ResourceState::GenericRead)
            {
                continue;
            }
            const auto* fakeBuffer = static_cast<const FakeBuffer*>(barrier.Buffer);
            const char* name = fakeBuffer->GetDesc().DebugName;
            if (IsDebugName(name, "MaterialTile_Args"))
            {
                assert((fakeBuffer->GetDesc().Usage & RHI::ResourceUsage::IndirectBuffer) == RHI::ResourceUsage::IndirectBuffer);
                assert((fakeBuffer->GetDesc().Usage & RHI::ResourceUsage::StorageBuffer) == RHI::ResourceUsage::StorageBuffer);
                bArgsToGenericRead = true;
            }
            else if (IsDebugName(name, "MaterialTile_List"))
            {
                bListToGenericRead = true;
            }
        }
        assert(bArgsToGenericRead && bListToGenericRead);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録の表の取り出し元が無く分類できないときは、dispatch せずに引数と統計を 0 にして終える
    // （後の材質の解決が、材質ごとの dispatch のグループ数 0 として安全に読める）
    void TestMaterialTileClassifyWithoutRecordTableClearsArgs()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::WithoutRaster);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Graph.GetLastExecutedPassCount() == 4);
        assert(!scene.Classify.WasClassified());
        assert(commandList.DispatchCount == 10); // 分類の dispatch は無い

        bool bArgsCleared = false;
        bool bStatsCleared = false;
        for (const FakeCommandList::MaterialTileFill& fill : commandList.MaterialTileFills)
        {
            bArgsCleared = bArgsCleared || IsDebugName(fill.BufferName, "MaterialTile_Args");
            bStatsCleared = bStatsCleared || IsDebugName(fill.BufferName, "MaterialTile_Stats");
        }
        assert(bArgsCleared && bStatsCleared);
        assert(commandList.MaterialTileFills.size() == 2);

        // 分類できなくても、宣言した最終の状態（GenericRead）へ渡す
        bool bArgsToGenericRead = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.BeforeState == RHI::ResourceState::UnorderedAccess &&
                barrier.AfterState == RHI::ResourceState::GenericRead &&
                IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, "MaterialTile_Args"))
            {
                bArgsToGenericRead = true;
            }
        }
        assert(bArgsToGenericRead);

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質のタイル分類を足さない構成では、資源も dispatch も増えない
    void TestMaterialTileClassifyAbsentWhenNotAdded()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true);
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(scene.CommandList.DispatchCount == 10);
        assert(scene.CommandList.MaterialTileFills.empty());
        assert(!scene.Classify.GetLayout().IsValid());
        ShutdownVisibilityRasterScene(scene);
    }

    // パスの生成時の既定は無効（SceneView は解決を使う --visibility-buffer=on のときだけ有効にする）。
    // View::Render は無効なパスをグラフへ足さないので、有効にするまでは資源も dispatch も増えない。
    // パスの生成時の既定が無効でなくなると、グラフへ足されて 4 パスになりこのテストが落ちる
    void TestMaterialTileClassifyAddedButDisabledByDefault()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::AddedDisabled);
        assert(!scene.Classify.IsEnabled());
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(scene.CommandList.DispatchCount == 10);
        assert(scene.CommandList.MaterialTileFills.empty());
        assert(!scene.Classify.GetLayout().IsValid());
        assert(!scene.Classify.WasClassified());
        ShutdownVisibilityRasterScene(scene);
    }

    // スキニングの頂点のバッファ: SkinningComputePass が書いた今・前の2本を、Execute の中で dispatch の後に
    // UnorderedAccess から宣言した最終の状態（GenericRead）へ遷移させる。RenderGraph は終わった状態を信じて後のパスの前に
    // バリアを足さないので、書いたパスが出す。SkinnedMeshes を渡さない（変形を1つも記録できない）フレームでも、
    // 宣言したバッファは遷移させる。後の VisibilityRasterPass の読み取りの前に、GenericRead 起点の二重のバリアは出ない
    void TestSkinningComputePassTransitionsDeclaredBuffersToGenericRead()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true);

        // GBuffer・MegaGeometry・SkinningCompute・VisibilityRaster
        assert(scene.Graph.GetLastExecutedPassCount() == 4);
        // 変形は1つも記録できないので、スキニングの dispatch は増えない（カリング 2 回 + HZB 7 段 + 記録を書く計算 1 回）
        assert(scene.Skinning.GetInstances().empty());
        assert(scene.CommandList.DispatchCount == 10);
        assert(scene.Skinning.GetCurrentVerticesHandle().IsValid());
        assert(scene.Skinning.GetPreviousVerticesHandle().IsValid());

        for (const char* name : {"Skinning_CurrentVertices", "Skinning_PreviousVertices"})
        {
            size_t finalBarriers = 0;
            size_t followupReadBarriers = 0;
            size_t writeBarrierIndex = 0;
            size_t finalBarrierIndex = 0;
            size_t barrierIndex = 0;
            for (const BarrierEvent& barrier : scene.CommandList.Barriers)
            {
                ++barrierIndex;
                if (barrier.Kind != RGBarrierKind::Buffer ||
                    !IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, name))
                {
                    continue;
                }
                if (barrier.AfterState == RHI::ResourceState::UnorderedAccess)
                {
                    writeBarrierIndex = barrierIndex;
                }
                if (barrier.BeforeState == RHI::ResourceState::UnorderedAccess &&
                    barrier.AfterState == RHI::ResourceState::GenericRead)
                {
                    ++finalBarriers;
                    finalBarrierIndex = barrierIndex;
                    assert(barrier.BufferSize == static_cast<const FakeBuffer*>(barrier.Buffer)->GetSize());
                }
                if (barrier.BeforeState == RHI::ResourceState::GenericRead)
                {
                    ++followupReadBarriers;
                }
            }
            // 書く前の遷移（グラフ）→ 書いた後の GenericRead への遷移（パス）が1回ずつ。それ以外のバリアは出ない
            assert(writeBarrierIndex != 0);
            assert(finalBarriers == 1);
            assert(finalBarrierIndex > writeBarrierIndex);
            assert(followupReadBarriers == 0);
        }
        ShutdownVisibilityRasterScene(scene);
    }

    // ---- スキニングの継続の検査（Declare / Execute の詰め方・代用・外し方、dispatch と束縛の上限） ----

    NorvesLib::Math::Matrix4x4 MakeMarkedMatrix(float x, float y, float z)
    {
        return NorvesLib::Math::Matrix4x4(1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f,
                                          x, y, z, 1.0f);
    }

    // 行列をシェーダーへ渡す 16 個の float にした値（パレットのバッファの中身と比べる）
    Container::VariableArray<float> ToShaderFloats(const NorvesLib::Math::Matrix4x4& matrix)
    {
        Container::VariableArray<float> values;
        values.resize(16);
        NorvesLib::Math::MatrixUtils::CopyToShaderData(matrix, values.data());
        return values;
    }

    // パレットのバッファの更新の matrixIndex 番目の行列（16 個の float）が、行列と一致するか
    bool PaletteMatrixEquals(const SkinnedPaletteUpdate& update, size_t matrixIndex, const NorvesLib::Math::Matrix4x4& matrix)
    {
        const Container::VariableArray<float> expected = ToShaderFloats(matrix);
        if (update.Values.size() < (matrixIndex + 1) * 16)
        {
            return false;
        }
        return std::memcmp(update.Values.data() + matrixIndex * 16, expected.data(), 16 * sizeof(float)) == 0;
    }

    // SkinnedMeshes（パレットの貸し出し）を渡し、SkinningComputePass だけをグラフに載せて実行するシーン
    struct SkinningPassScene
    {
        RHI::TSharedPtr<FakeDevice> Device;
        ShaderManager ShaderMgr;
        MockAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderResources Resources;
        RenderGraph Graph;
        SkinningComputePass Skinning;
        FakeCommandList CommandList;
        Container::VariableArray<DrawCommand> OpaqueCommands;
        Container::VariableArray<Container::TSharedPtr<const SkinnedMeshFrameLease>> SkinnedLeases;
        CameraProxy Camera;
        ViewRenderContext Context;
    };

    // 頂点数 vertexCount・骨 boneCount 本のスキニングの描画（非インスタンス、直前のフレームなし）を足し、その番号を返す。
    // 現在の変換・骨の行列は handleIndex ごとに違う値の並進にして、パレットの中身を見分けられるようにする
    uint32_t AddSkinnedCommand(SkinningPassScene& scene, uint32_t handleIndex, uint32_t vertexCount, uint32_t boneCount)
    {
        Container::VariableArray<SkinnedMeshVertex> skinVertices;
        skinVertices.resize(vertexCount);
        for (SkinnedMeshVertex& vertex : skinVertices)
        {
            vertex.BoneWeights[0] = 1.0f;
        }
        Container::VariableArray<uint32_t> skinIndices;
        skinIndices.push_back(0u);
        skinIndices.push_back(1u);
        skinIndices.push_back(2u);
        auto assetLease = Container::MakeShared<SkinnedMeshAssetLease>(
            SkinnedMeshHandle{handleIndex, 1}, std::move(skinVertices), std::move(skinIndices));
        scene.SkinnedLeases.push_back(Container::MakeShared<SkinnedMeshFrameLease>(assetLease));

        DrawCommand command;
        command.Draw.PayloadKind = DrawPayloadKind::Skinned;
        command.Draw.ObjectId = 10 + handleIndex;
        command.Draw.WorldMatrix = MakeMarkedMatrix(static_cast<float>(handleIndex) * 100.0f, 5.0f, 6.0f);
        command.Skinned.FrameLeaseIndex = static_cast<uint32_t>(scene.SkinnedLeases.size() - 1);
        for (uint32_t bone = 0; bone < boneCount; ++bone)
        {
            command.Skinned.BonePalette.push_back(
                MakeMarkedMatrix(static_cast<float>(handleIndex * 10 + bone), 1.0f, 2.0f));
        }
        scene.OpaqueCommands.push_back(command);
        return static_cast<uint32_t>(scene.OpaqueCommands.size() - 1);
    }

    void RunSkinningPassScene(SkinningPassScene& scene, uint32_t maxOutputVertices)
    {
        GSkinnedPaletteUpdates.clear();
        scene.Device = RHI::MakeShared<FakeDevice>();
        assert(scene.ShaderMgr.Initialize(scene.Device.get(), TestShaderDirectory));
        assert(scene.Pool.Initialize(&scene.Allocator, 1));
        scene.Pool.BeginFrame(0);
        assert(scene.Resources.Initialize(scene.Device));
        scene.Resources.SkinnedMeshes().BeginFrame(0);
        assert(scene.Graph.Initialize(&scene.Pool));
        scene.Graph.BeginFrame(0);

        scene.Camera.Viewport.Width = 128.0f;
        scene.Camera.Viewport.Height = 64.0f;
        ViewRenderContext& context = scene.Context;
        context.CommandList = &scene.CommandList;
        context.Device = scene.Device.get();
        context.TransientPool = &scene.Pool;
        context.ShaderMgr = &scene.ShaderMgr;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.MainCamera = &scene.Camera;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(scene.OpaqueCommands);
        context.SnapshotSkinnedMeshFrameLeases = &scene.SkinnedLeases;
        context.SkinnedMeshes = &scene.Resources.SkinnedMeshes();

        scene.Skinning.SetEnabled(true);
        scene.Skinning.SetMaxOutputVertices(maxOutputVertices);
        assert(scene.Skinning.Initialize(context));
        scene.Graph.AddPass(&scene.Skinning);
        assert(scene.Graph.Compile(context));
        const RenderGraphExecutionResult result = scene.Graph.ExecuteWithResult(context);
        assert(result.bSuccess);
    }

    // RunSkinningPassScene の後の 2 フレーム目以降。同じ描画の構成のまま、グラフを組み直して Declare・Execute をやり直す
    void RunSkinningPassNextFrame(SkinningPassScene& scene, uint64_t frameIndex)
    {
        scene.Pool.EndFrame();
        scene.Pool.BeginFrame(frameIndex);
        scene.Graph.BeginFrame(frameIndex);
        scene.Graph.AddPass(&scene.Skinning);
        assert(scene.Graph.Compile(scene.Context));
        const RenderGraphExecutionResult result = scene.Graph.ExecuteWithResult(scene.Context);
        assert(result.bSuccess);
    }

    void ShutdownSkinningPassScene(SkinningPassScene& scene)
    {
        scene.Skinning.Shutdown();
        scene.Resources.Shutdown();
        scene.Graph.Shutdown();
        scene.Pool.EndFrame();
        scene.Pool.Shutdown();
        scene.ShaderMgr.Shutdown();
    }

    // パスが書いたバッファ（名前のもの）の大きさ。パスが最後に出す GenericRead への遷移のバリアから読む
    uint64_t FindSkinningBufferSize(const SkinningPassScene& scene, const char* name)
    {
        for (const BarrierEvent& barrier : scene.CommandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::GenericRead &&
                IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, name))
            {
                return barrier.BufferSize;
            }
        }
        return 0;
    }

    // 2 つ以上のインスタンスの詰め方（2 つ目以降の VertexBase）、直前のパレットが無いとき・数が違うときに今の値で
    // 代用すること、インスタンス描画のものを外すこと
    void TestSkinningComputePassPacksInstancesAndSubstitutesMissingPrevious()
    {
        SkinningPassScene scene;
        // 0: 5 頂点・骨 2 本・直前のフレームあり
        const uint32_t withPrevious = AddSkinnedCommand(scene, 1, 5, 2);
        scene.OpaqueCommands[withPrevious].Skinned.bHasPrevious = true;
        scene.OpaqueCommands[withPrevious].Skinned.PreviousWorldMatrix = MakeMarkedMatrix(-7.0f, -8.0f, -9.0f);
        scene.OpaqueCommands[withPrevious].Skinned.PreviousBonePalette.push_back(MakeMarkedMatrix(-1.0f, -2.0f, -3.0f));
        scene.OpaqueCommands[withPrevious].Skinned.PreviousBonePalette.push_back(MakeMarkedMatrix(-4.0f, -5.0f, -6.0f));
        // 1: 4 頂点・インスタンス描画（外れる）
        const uint32_t instanced = AddSkinnedCommand(scene, 2, 4, 1);
        scene.OpaqueCommands[instanced].Draw.bInstanced = true;
        scene.OpaqueCommands[instanced].Draw.InstanceCount = 2;
        // 2: 7 頂点・骨 1 本・直前のフレームなし
        const uint32_t withoutPrevious = AddSkinnedCommand(scene, 3, 7, 1);
        // 3: 3 頂点・骨 2 本・直前のパレットの数が違う（1 本）
        const uint32_t mismatched = AddSkinnedCommand(scene, 4, 3, 2);
        scene.OpaqueCommands[mismatched].Skinned.bHasPrevious = true;
        scene.OpaqueCommands[mismatched].Skinned.PreviousWorldMatrix = MakeMarkedMatrix(-70.0f, -80.0f, -90.0f);
        scene.OpaqueCommands[mismatched].Skinned.PreviousBonePalette.push_back(MakeMarkedMatrix(-10.0f, -20.0f, -30.0f));

        RunSkinningPassScene(scene, SKINNING_MAX_OUTPUT_VERTICES);

        assert(scene.Graph.GetLastExecutedPassCount() == 1);
        assert(scene.Skinning.GetDroppedInstanceCount() == 0);

        // 詰め方: 0 → [0, 5)、2 → [5, 12)、3 → [12, 15)。インスタンス描画（1）は外れる
        const auto& instances = scene.Skinning.GetInstances();
        assert(instances.size() == 3);
        assert(instances[0].ObjectId == 11 && instances[0].VertexBase == 0 && instances[0].VertexCount == 5);
        assert(instances[1].ObjectId == 13 && instances[1].VertexBase == 5 && instances[1].VertexCount == 7);
        assert(instances[2].ObjectId == 14 && instances[2].VertexBase == 12 && instances[2].VertexCount == 3);
        assert(scene.CommandList.DispatchCount == 3);
        assert(FindSkinningBufferSize(scene, "Skinning_CurrentVertices") == 15u * sizeof(SkinnedOutputVertex));
        assert(FindSkinningBufferSize(scene, "Skinning_PreviousVertices") == 15u * sizeof(SkinnedOutputVertex));

        // パレットの更新は 1 インスタンスにつき（今, 前）の順。インスタンス描画のぶんは作られない
        assert(GSkinnedPaletteUpdates.size() == 6);
        for (size_t index = 0; index < GSkinnedPaletteUpdates.size(); ++index)
        {
            assert(GSkinnedPaletteUpdates[index].bPrevious == (index % 2 == 1));
        }

        // 今のパレット: 変換・法線の行列・骨ごとの（位置, 法線）の行列。骨 0 の位置の行列は行列 2
        const SkinnedPaletteUpdate& currentOfFirst = GSkinnedPaletteUpdates[0];
        const SkinnedPaletteUpdate& previousOfFirst = GSkinnedPaletteUpdates[1];
        assert(PaletteMatrixEquals(currentOfFirst, 0, scene.OpaqueCommands[withPrevious].Draw.WorldMatrix));
        assert(PaletteMatrixEquals(currentOfFirst, 2, scene.OpaqueCommands[withPrevious].Skinned.BonePalette[0]));
        // 直前のフレームがあるときは、その変換・骨の行列（前のパレットは変換 + 骨ごとの位置の行列）
        assert(previousOfFirst.Values.size() == 3u * 16u);
        assert(PaletteMatrixEquals(previousOfFirst, 0, scene.OpaqueCommands[withPrevious].Skinned.PreviousWorldMatrix));
        assert(PaletteMatrixEquals(previousOfFirst, 1, scene.OpaqueCommands[withPrevious].Skinned.PreviousBonePalette[0]));
        assert(PaletteMatrixEquals(previousOfFirst, 2, scene.OpaqueCommands[withPrevious].Skinned.PreviousBonePalette[1]));

        // 直前のフレームが無いときは、今の変換と今の骨の行列で代用する（動きは 0）
        const SkinnedPaletteUpdate& previousOfSecond = GSkinnedPaletteUpdates[3];
        assert(previousOfSecond.Values.size() == 2u * 16u);
        assert(PaletteMatrixEquals(previousOfSecond, 0, scene.OpaqueCommands[withoutPrevious].Draw.WorldMatrix));
        assert(PaletteMatrixEquals(previousOfSecond, 1, scene.OpaqueCommands[withoutPrevious].Skinned.BonePalette[0]));

        // 直前のパレットの数が違うときも、今の値で代用する（前の値は使わない）
        const SkinnedPaletteUpdate& previousOfThird = GSkinnedPaletteUpdates[5];
        assert(previousOfThird.Values.size() == 3u * 16u);
        assert(PaletteMatrixEquals(previousOfThird, 0, scene.OpaqueCommands[mismatched].Draw.WorldMatrix));
        assert(PaletteMatrixEquals(previousOfThird, 1, scene.OpaqueCommands[mismatched].Skinned.BonePalette[0]));
        assert(PaletteMatrixEquals(previousOfThird, 2, scene.OpaqueCommands[mismatched].Skinned.BonePalette[1]));

        ShutdownSkinningPassScene(scene);
    }

    // 頂点の合計が上限を超えるインスタンスは黙って捨てず、数を数えて外す。大きいものを外しても、後ろの小さいものは詰める
    void TestSkinningComputePassCountsInstancesDroppedByVertexLimit()
    {
        SkinningPassScene scene;
        AddSkinnedCommand(scene, 1, 5, 1);
        AddSkinnedCommand(scene, 2, 7, 1); // 5 + 7 > 8: 外れる
        AddSkinnedCommand(scene, 3, 3, 1); // 5 + 3 = 8: 収まる
        AddSkinnedCommand(scene, 4, 1, 1); // 8 + 1 > 8: 外れる

        RunSkinningPassScene(scene, 8);

        assert(scene.Skinning.GetDroppedInstanceCount() == 2);
        const auto& instances = scene.Skinning.GetInstances();
        assert(instances.size() == 2);
        assert(instances[0].ObjectId == 11 && instances[0].VertexBase == 0 && instances[0].VertexCount == 5);
        assert(instances[1].ObjectId == 13 && instances[1].VertexBase == 5 && instances[1].VertexCount == 3);
        assert(scene.CommandList.DispatchCount == 2);
        assert(FindSkinningBufferSize(scene, "Skinning_CurrentVertices") == 8u * sizeof(SkinnedOutputVertex));
        ShutdownSkinningPassScene(scene);

        // 上限に収まるときは 1 つも外さない
        SkinningPassScene fitting;
        AddSkinnedCommand(fitting, 1, 5, 1);
        AddSkinnedCommand(fitting, 2, 3, 1);
        RunSkinningPassScene(fitting, 8);
        assert(fitting.Skinning.GetDroppedInstanceCount() == 0);
        assert(fitting.Skinning.GetInstances().size() == 2);
        ShutdownSkinningPassScene(fitting);
    }

#if NORVES_ENABLE_STATS
    // テキストのファイルを丸ごと読み、終端に '\0' を付ける
    bool ReadTextFile(const char* path, Container::VariableArray<char>& outText)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return false;
        }
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        outText.resize(static_cast<size_t>(size) + 1);
        file.read(outText.data(), size);
        outText[static_cast<size_t>(size)] = '\0';
        return true;
    }
#endif

#if NORVES_ENABLE_LOGGING
    // 計算スキニングが出す警告（カテゴリ SkinningComputePass）の数を数える
    struct SkinningWarningCounter final : Logging::ILogSink
    {
        uint32_t Count = 0;

        void OnLog(const Logging::LogEntry& entry) override
        {
            if (entry.level == Logging::LogLevel::Warning && entry.category == "SkinningComputePass")
            {
                ++Count;
            }
        }
    };
#endif

    // 外したインスタンスの数は Declare のたびに数え直し（累計にしない）、ログはパスの寿命で 1 回だけ出す。
    // 統計へ渡る数は Declare がフレームの通し番号つきで置く（同じ通し番号の Declare は合算、別の番号は数え直し、
    // Declare が無かった番号は 0）。数は統計の欄・ToString・CSV に出る
    void TestSkinningComputePassDropCountIsPerFrameAndLoggedOnce()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        SkinningWarningCounter warnings;
        logger.AddSink(&warnings);
#endif

        SkinningPassScene scene;
        AddSkinnedCommand(scene, 1, 5, 1);
        AddSkinnedCommand(scene, 2, 7, 1); // 5 + 7 > 8: 外れる
        AddSkinnedCommand(scene, 3, 3, 1); // 5 + 3 = 8: 収まる
        AddSkinnedCommand(scene, 4, 1, 1); // 8 + 1 > 8: 外れる

        RunSkinningPassScene(scene, 8);
        assert(scene.Skinning.GetDroppedInstanceCount() == 2);
        // Declare が最初のフレームの通し番号へ数を置く（置く行を消す・数える前へ動かす・別の通し番号で置くと 0 になる）
        const uint64_t firstSerial = scene.Context.ResolveRenderFrameSerial();
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(firstSerial) == 2);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(firstSerial + 1) == 0);
#if NORVES_ENABLE_LOGGING
        assert(warnings.Count == 1);
#endif

        // 同じ通し番号でもう一度 Declare（1 つの SceneView が 2 つ目のビューポートを描く形）: 1 回ごとの数は 2 のまま
        // （累計の 4 にならない）、通し番号ごとの数は 2 つのビューポートの合算の 4。ログは増えない
        RunSkinningPassNextFrame(scene, 1);
        assert(scene.Skinning.GetDroppedInstanceCount() == 2);
        assert(scene.Skinning.GetInstances().size() == 2);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(firstSerial) == 4);
#if NORVES_ENABLE_LOGGING
        assert(warnings.Count == 1);
#endif

        // 通し番号を進めたフレーム: 新しい番号で 2（前の番号の 4 を引き継がない）、古い番号は 0（描かれなかったビューが
        // 古い数を足し続けない）
        const uint64_t secondSerial = firstSerial + 1;
        scene.Context.RenderFrameSerial = secondSerial;
        RunSkinningPassNextFrame(scene, 2);
        assert(scene.Skinning.GetDroppedInstanceCount() == 2);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(secondSerial) == 2);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(firstSerial) == 0);
#if NORVES_ENABLE_LOGGING
        assert(warnings.Count == 1);
#endif

#if NORVES_ENABLE_STATS
        // 統計: 通し番号ごとの数を欄へ設定して UpdateRenderingStats へ渡すと、フレームの後も欄が残り、ToString と CSV に出る
        DebugStats::StatsManager& stats = DebugStats::StatsManager::Get();
        const char* tracePath = "RenderGraphCompileTest.skinning.trace.csv";
        std::remove(tracePath);
        stats.ResetAll();
        assert(stats.StartTrace(tracePath));
        stats.BeginFrame(7, 0.016f);
        DebugStats::RenderingStats frameStats;
        frameStats.SkinningComputeDroppedInstances = scene.Skinning.GetDroppedInstanceCountForFrame(secondSerial);
        stats.UpdateRenderingStats(frameStats);
        assert(stats.GetRenderingStats().SkinningComputeDroppedInstances == 2);
        const auto text = frameStats.ToString();
        assert(std::strstr(text.c_str(), "droppedInstances=2") != nullptr);
        stats.EndFrame();
        stats.StopTrace();
        assert(stats.GetRenderingStats().SkinningComputeDroppedInstances == 2);

        Container::VariableArray<char> traceText;
        assert(ReadTextFile(tracePath, traceText));
        // 1 行目（ヘッダー）に欄がある
        const char* const headerEnd = std::strchr(traceText.data(), '\n');
        assert(headerEnd != nullptr);
        const char* const headerColumn = std::strstr(traceText.data(), ",SkinningComputeDroppedInstances");
        assert(headerColumn != nullptr && headerColumn < headerEnd);
        // フレーム 7 の行の最後の欄が外したインスタンスの数
        const char* frameLine = std::strstr(headerEnd, "\nFrame,7,");
        assert(frameLine != nullptr);
        ++frameLine;
        const char* lineEnd = std::strchr(frameLine, '\n');
        if (lineEnd == nullptr)
        {
            lineEnd = frameLine + std::strlen(frameLine);
        }
        if (lineEnd > frameLine && lineEnd[-1] == '\r')
        {
            --lineEnd;
        }
        assert(lineEnd - frameLine >= 2 && std::strncmp(lineEnd - 2, ",2", 2) == 0);
        // フレーム 7 の行は 1 つだけ
        assert(std::strstr(lineEnd, "\nFrame,7,") == nullptr);
        stats.ResetAll();
        std::remove(tracePath);
#endif

        // 次のフレーム: 上限を上げれば全部収まり、数は 0 に戻る
        scene.Skinning.SetMaxOutputVertices(100);
        scene.Context.RenderFrameSerial = secondSerial + 1;
        RunSkinningPassNextFrame(scene, 3);
        assert(scene.Skinning.GetDroppedInstanceCount() == 0);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(secondSerial + 1) == 0);
        assert(scene.Skinning.GetInstances().size() == 4);

        // その次のフレーム: 上限を戻せばまた外れる。ログはパスの寿命で 1 回のまま
        scene.Skinning.SetMaxOutputVertices(8);
        scene.Context.RenderFrameSerial = secondSerial + 2;
        RunSkinningPassNextFrame(scene, 4);
        assert(scene.Skinning.GetDroppedInstanceCount() == 2);
        assert(scene.Skinning.GetDroppedInstanceCountForFrame(secondSerial + 2) == 2);
#if NORVES_ENABLE_LOGGING
        assert(warnings.Count == 1);
        // 共有の Logger を、このテストが初期化し直す前の状態へ戻す（この実行ファイルは他で Logger を初期化しない = 未初期化）
        logger.RemoveSink(&warnings);
        logger.Shutdown();
#endif
        ShutdownSkinningPassScene(scene);
    }

    // dispatch のグループ数（1 次元目に収まらなければ 2 次元目へ広げる）と、束縛が maxStorageBufferRange の保証された
    // 最小値を超える入力の拒否
    void TestSkinningComputeGroupCountsAndBindingLimit()
    {
        uint32_t x = 0;
        uint32_t y = 0;
        assert(SkinningCompute::ComputeGroupCounts(1, SKINNING_MAX_GROUP_COUNT, x, y) && x == 1 && y == 1);
        assert(SkinningCompute::ComputeGroupCounts(64, SKINNING_MAX_GROUP_COUNT, x, y) && x == 1 && y == 1);
        assert(SkinningCompute::ComputeGroupCounts(65, SKINNING_MAX_GROUP_COUNT, x, y) && x == 2 && y == 1);
        assert(SkinningCompute::ComputeGroupCounts(64u * 65535u, SKINNING_MAX_GROUP_COUNT, x, y) && x == 65535 && y == 1);
        // 65535 グループを 1 つ超えると 2 行になる
        assert(SkinningCompute::ComputeGroupCounts(64u * 65535u + 1u, SKINNING_MAX_GROUP_COUNT, x, y) && x == 65535 && y == 2);
        // 上限を小さくすると 2 次元目へ広がる（10 グループ・上限 3 → 3 x 4）
        assert(SkinningCompute::ComputeGroupCounts(640, 3, x, y) && x == 3 && y == 4);
        // 上限は SKINNING_MAX_GROUP_COUNT を超えない
        assert(SkinningCompute::ComputeGroupCounts(64u * 65535u + 1u, 1000000u, x, y) && x == 65535 && y == 2);
        // 頂点が 0 個・上限が 0 は求められない。2 次元目も上限を超えるときも
        assert(!SkinningCompute::ComputeGroupCounts(0, SKINNING_MAX_GROUP_COUNT, x, y) && x == 0 && y == 0);
        assert(!SkinningCompute::ComputeGroupCounts(64, 0, x, y));
        assert(!SkinningCompute::ComputeGroupCounts(0xFFFFFFFFu, 1, x, y));

        auto device = RHI::MakeShared<FakeDevice>();
        ShaderManager shaderMgr;
        assert(shaderMgr.Initialize(device.get(), TestShaderDirectory));
        SkinningCompute compute;
        assert(compute.Initialize(device.get(), &shaderMgr));
        compute.BeginFrame(0, 1);
        FakeCommandList commandList;

        const RHI::ResourceUsage usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
        auto makeBuffer = [&](uint64_t size) {
            return RHI::BufferPtr(RHI::MakeShared<FakeBuffer>(RHI::BufferDesc(size, usage, false, "SkinningLimitProbe")));
        };
        auto makeDispatch = [&](uint32_t vertexCount, uint32_t groupCountXLimit) {
            SkinningComputeDispatch dispatch;
            dispatch.SkinVertices = makeBuffer(static_cast<uint64_t>(vertexCount) * sizeof(SkinnedMeshVertex));
            dispatch.Palette = makeBuffer(256);
            dispatch.PreviousPalette = makeBuffer(256);
            dispatch.CurrentVertices = makeBuffer(static_cast<uint64_t>(vertexCount) * sizeof(SkinnedOutputVertex));
            dispatch.PreviousVertices = makeBuffer(static_cast<uint64_t>(vertexCount) * sizeof(SkinnedOutputVertex));
            dispatch.VertexCount = vertexCount;
            dispatch.GroupCountXLimit = groupCountXLimit;
            return dispatch;
        };

        // 小さい上限: 768 頂点 = 12 グループ、上限 5 → (5, 3)
        assert(compute.Record(&commandList, makeDispatch(768, 5)));
        assert(commandList.DispatchGroups.size() == 1);
        assert(commandList.DispatchGroups[0].X == 5 && commandList.DispatchGroups[0].Y == 3 &&
               commandList.DispatchGroups[0].Z == 1);

        // 束縛の上限ちょうどの入力（2^27 バイト = 2,097,152 頂点）は記録でき、グループ数は 1 次元目に収まる
        constexpr uint32_t MaxBoundVertices = static_cast<uint32_t>(SKINNING_MAX_BINDING_BYTES / sizeof(SkinnedMeshVertex));
        assert(compute.Record(&commandList, makeDispatch(MaxBoundVertices, SKINNING_MAX_GROUP_COUNT)));
        assert(commandList.DispatchGroups.size() == 2);
        assert(commandList.DispatchGroups[1].X == MaxBoundVertices / SkinningCompute::ThreadsPerGroup &&
               commandList.DispatchGroups[1].Y == 1);

        // 束縛の上限を 1 頂点でも超える入力は記録しない
        assert(!compute.Record(&commandList, makeDispatch(MaxBoundVertices + 1u, SKINNING_MAX_GROUP_COUNT)));
        // グループ数が上限に収まらない（上限 0）ときも記録しない
        assert(!compute.Record(&commandList, makeDispatch(64, 0)));
        assert(commandList.DispatchGroups.size() == 2);
        assert(commandList.DispatchCount == 2);

        compute.Shutdown();
        shaderMgr.Shutdown();
    }

    // 名前のバッファを作った回数
    uint32_t CountBufferCreations(const FakeDevice& device, const char* debugName)
    {
        uint32_t count = 0;
        for (const BufferCreationRecord& record : device.CreatedBuffers)
        {
            if (IsDebugName(record.Desc.DebugName, debugName))
            {
                ++count;
            }
        }
        return count;
    }

    struct FrameUseRingProbe
    {
        uint32_t Id = 0;
    };

    // FrameUseRing: 1フレームに、枠の数（旧 FrameSlotCount = 2・MaxInFlightSlots）を超える回数を割り当てても全部が別の資源になり、
    // 同じフレームの間は BeginFrame を呼び直しても位置が戻らず、次のフレーム（通し番号が変わる）で、GPU の完了の後に同じ資源を
    // 最初から使い回す。飛行中のフレームの番号が違う枠は別の資源で、互いの位置に触らない
    void TestFrameUseRingGivesDistinctUsesWithinAFrameAndReusesNextFrame()
    {
        FrameUseRing<FrameUseRingProbe> ring;
        uint32_t nextId = 1;
        constexpr uint32_t ManyCalls = 9;
        static_assert(ManyCalls > FrameUseRing<FrameUseRingProbe>::MaxInFlightSlots);

        uint32_t frame1[ManyCalls] = {};
        for (uint32_t index = 0; index < ManyCalls; ++index)
        {
            // 複数のビューポートは同じ通し番号で BeginFrame を呼ぶ
            ring.BeginFrame(0, 1);
            FrameUseRingProbe& use = ring.Acquire();
            assert(use.Id == 0);
            use.Id = nextId++;
            frame1[index] = use.Id;
            assert(ring.GetUsedCount() == index + 1);
        }
        for (uint32_t first = 0; first < ManyCalls; ++first)
        {
            for (uint32_t second = first + 1; second < ManyCalls; ++second)
            {
                assert(frame1[first] != frame1[second]);
            }
        }
        assert(ring.GetCapacity() == ManyCalls);

        // 次のフレーム: 同じ資源を最初から使い回し、増やさない。足りなくなったときだけ増える
        ring.BeginFrame(0, 2);
        assert(ring.GetUsedCount() == 0);
        for (uint32_t index = 0; index < ManyCalls; ++index)
        {
            assert(ring.Acquire().Id == frame1[index]);
        }
        assert(ring.GetCapacity() == ManyCalls);
        assert(ring.Acquire().Id == 0);
        assert(ring.GetCapacity() == ManyCalls + 1);

        // 飛行中のフレームの番号が違う枠は別の資源。もう一方の枠の位置を戻さない
        ring.BeginFrame(1, 3);
        assert(ring.GetActiveSlot() == 1);
        assert(ring.GetUsedCount() == 0);
        FrameUseRingProbe& other = ring.Acquire();
        assert(other.Id == 0);
        other.Id = nextId++;
        for (uint32_t index = 0; index < ManyCalls; ++index)
        {
            assert(other.Id != frame1[index]);
        }
        ring.BeginFrame(0, 4);
        assert(ring.GetActiveSlot() == 0);
        assert(ring.GetUsedCount() == 0);
        assert(ring.Acquire().Id == frame1[0]);
        ring.BeginFrame(1, 3);
        assert(ring.GetUsedCount() == 1);

        // 区別できる飛行中のフレームの番号の上限いっぱいまで、枠は互いに重ならない
        for (uint32_t slot = 0; slot < FrameUseRing<FrameUseRingProbe>::MaxInFlightSlots; ++slot)
        {
            ring.BeginFrame(slot, 10);
            assert(ring.GetActiveSlot() == slot);
        }

        ring.Clear();
        ring.BeginFrame(0, 1);
        assert(ring.GetCapacity() == 0);
        assert(ring.Acquire().Id == 0);
    }

    // 同じパスが1フレームに何回 Execute されても（複数のビューポート）、まだ提出していない UBO・ディスクリプタセット・
    // ホストが書くバッファを上書きしない。枠の数（旧 FrameSlotCount = 2）を超える回数でも、Execute ごとに別の資源を作り、
    // 次のフレームではそれを使い回して増やさない。Context::RenderFrameSerial がフレームの境目になる
    void TestComputePassFrameResourcesAreNotReusedWithinAFrame()
    {
        constexpr uint32_t ViewportsPerFrame = 7;
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::LinkedToRaster);
        const FakeDevice& device = *scene.Device;
        const char* const classifyUniform = "MaterialTileClassifyParams";
        const char* const megaFrameSlot = "MegaGeometry_InstanceTable";

        // 1回目の Execute: 分類は 3 回の dispatch ぶん、MegaGeometry は 1 組
        assert(CountBufferCreations(device, classifyUniform) == MaterialTileClassify::DispatchesPerRecord);
        assert(CountBufferCreations(device, megaFrameSlot) == 1);

        // 同じフレーム（同じ通し番号）のあと 6 回のビューポート: 毎回別の資源
        for (uint32_t viewport = 1; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
            assert(CountBufferCreations(device, classifyUniform) ==
                   MaterialTileClassify::DispatchesPerRecord * (viewport + 1));
            assert(CountBufferCreations(device, megaFrameSlot) == viewport + 1);
        }

        // 次のフレーム: 同じ回数の Execute でも資源を作り足さない
        scene.Context.RenderFrameSerial = scene.Context.ResolveRenderFrameSerial() + 1;
        for (uint32_t viewport = 0; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
        }
        assert(CountBufferCreations(device, classifyUniform) == MaterialTileClassify::DispatchesPerRecord * ViewportsPerFrame);
        assert(CountBufferCreations(device, megaFrameSlot) == ViewportsPerFrame);

        // さらに次のフレームで1回多く Execute すると、その1回ぶんだけ増える
        scene.Context.RenderFrameSerial += 1;
        for (uint32_t viewport = 0; viewport <= ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
        }
        assert(CountBufferCreations(device, classifyUniform) ==
               MaterialTileClassify::DispatchesPerRecord * (ViewportsPerFrame + 1));
        assert(CountBufferCreations(device, megaFrameSlot) == ViewportsPerFrame + 1);

        ShutdownVisibilityRasterScene(scene);
    }

    // ID の検証表示（VisibilityDebugPass）も同じ。1フレームに何回 Execute されても（複数のビューポート）、まだ提出していない
    // パラメータの UBO とディスクリプタセットを上書きしない。枠の数（旧 FrameSlotCount = 2）を超える回数でも Execute ごとに別の資源を作り、
    // 次のフレームではそれを使い回して増やさない。Execute の回数 % 2 で枠を選ぶ作りに戻すと、3 回目以降で増えず落ちる
    void TestVisibilityDebugPassFrameResourcesAreNotReusedWithinAFrame()
    {
        constexpr uint32_t ViewportsPerFrame = 7;
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, false, ResolveMode::None, 1, true);
        const FakeDevice& device = *scene.Device;
        const char* const debugUniform = "VisBuffer_DebugParams";

        // 1回目の Execute: 検証表示が描かれ（記録の表・パイプライン・サンプラーがそろった）、UBO は 1 組
        assert(scene.Graph.GetLastExecutedPassCount() == 5);
        assert(CountBufferCreations(device, debugUniform) == 1);

        // 同じフレーム（同じ通し番号）のあと 6 回のビューポート: 毎回別の資源
        for (uint32_t viewport = 1; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
            assert(CountBufferCreations(device, debugUniform) == viewport + 1);
        }

        // 次のフレーム: 同じ回数の Execute でも資源を作り足さない
        scene.Context.RenderFrameSerial = scene.Context.ResolveRenderFrameSerial() + 1;
        for (uint32_t viewport = 0; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
        }
        assert(CountBufferCreations(device, debugUniform) == ViewportsPerFrame);

        // さらに次のフレームで1回多く Execute すると、その1回ぶんだけ増える
        scene.Context.RenderFrameSerial += 1;
        for (uint32_t viewport = 0; viewport <= ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
        }
        assert(CountBufferCreations(device, debugUniform) == ViewportsPerFrame + 1);

        ShutdownVisibilityRasterScene(scene);
    }

    // ID のラスタ（VisibilityRasterPass）の本体の資源（定数・記録の表・ディスクリプタセット）も同じ。1フレームに何回 Execute されても
    // （複数のビューポート）、Execute ごとに別の組を作り、次のフレームでは使い回して増やさない。Execute の回数 % 2 で組を選ぶ作りに
    // 戻すと、3 回目以降で増えず落ちる。塊の作業配列・材質の表の作業配列（GPU へ上げる並び・区間ごとの表の番号）・表の積み上げの
    // 件の配列と索引は最初の Execute で容量を取り、以降のフレームでは増やさない（毎フレームの確保をしない。呼ぶたびに作る作りに
    // 戻すと、容量が 0 のままで落ちる）
    void TestVisibilityRasterFrameResourcesFollowFramesAndChunkScratchIsReused()
    {
        constexpr uint32_t ViewportsPerFrame = 7;
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true);
        const FakeDevice& device = *scene.Device;
        const char* const frameUniform = "VisBuffer_FrameUBO";
        // 64bit のバッファの合流（ID のラスタの Execute ごとに 1 回）の定数。ディスクリプタセットと同じく Execute ごとの組
        const char* const mergeParams = "VisBuffer_MergeParams";
        const char* const keyBuffer = "VisBuffer_Key64";

        // 1回目の Execute: 手続き 3 件・スキニング 1 件の塊を集め、作業配列が容量を持つ
        assert(scene.Raster.GetLastFrameStats().ProceduralRecords == 3);
        assert(scene.Raster.GetLastFrameStats().SkinnedRecords == 1);
        assert(CountBufferCreations(device, frameUniform) == 1);
        assert(CountBufferCreations(device, mergeParams) == 1);
        assert(CountBufferCreations(device, keyBuffer) == 1);
        const size_t proceduralScratchCapacity = scene.Raster.GetProceduralChunkScratchCapacity();
        const size_t skinnedScratchCapacity = scene.Raster.GetSkinnedChunkScratchCapacity();
        assert(proceduralScratchCapacity > 0 && skinnedScratchCapacity > 0);
        const size_t materialEntryScratchCapacity = scene.Raster.GetMaterialEntryScratchCapacity();
        const size_t sectionMaterialScratchCapacity = scene.Raster.GetSectionMaterialScratchCapacity();
        const size_t materialTableEntryCapacity = scene.Raster.GetMaterialTableBuilder().GetEntryCapacity();
        const size_t materialTableSlotCount = scene.Raster.GetMaterialTableBuilder().GetIndexSlotCount();
        assert(materialEntryScratchCapacity > 0 && sectionMaterialScratchCapacity > 0 && materialTableEntryCapacity > 0 &&
               materialTableSlotCount > 0);
        const auto expectMaterialScratchStable = [&]() -> void
        {
            assert(scene.Raster.GetMaterialEntryScratchCapacity() == materialEntryScratchCapacity &&
                   scene.Raster.GetSectionMaterialScratchCapacity() == sectionMaterialScratchCapacity &&
                   scene.Raster.GetMaterialTableBuilder().GetEntryCapacity() == materialTableEntryCapacity &&
                   scene.Raster.GetMaterialTableBuilder().GetIndexSlotCount() == materialTableSlotCount);
        };

        // 同じフレーム（同じ通し番号）のあと 6 回のビューポート: 毎回別の組
        for (uint32_t viewport = 1; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
            assert(CountBufferCreations(device, frameUniform) == viewport + 1);
            // 64bit のバッファは同じ大きさの間は作り直さず、1 つを Execute 間で使い回す（バリアで順序づける）
            assert(CountBufferCreations(device, mergeParams) == viewport + 1);
            assert(CountBufferCreations(device, keyBuffer) == 1);
            expectMaterialScratchStable();
            assert(scene.Raster.GetProceduralChunkScratchCapacity() == proceduralScratchCapacity &&
               scene.Raster.GetSkinnedChunkScratchCapacity() == skinnedScratchCapacity);
        }

        // 次のフレーム: 同じ回数の Execute でも組を作り足さず、作業配列の容量も増やさない
        scene.Context.RenderFrameSerial = scene.Context.ResolveRenderFrameSerial() + 1;
        for (uint32_t viewport = 0; viewport < ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
            assert(scene.Raster.GetLastFrameStats().ProceduralRecords == 3);
            assert(scene.Raster.GetProceduralChunkScratchCapacity() == proceduralScratchCapacity &&
               scene.Raster.GetSkinnedChunkScratchCapacity() == skinnedScratchCapacity);
        }
        assert(CountBufferCreations(device, frameUniform) == ViewportsPerFrame);
        assert(CountBufferCreations(device, mergeParams) == ViewportsPerFrame);
        assert(CountBufferCreations(device, keyBuffer) == 1);
        expectMaterialScratchStable();

        // さらに次のフレームで 1 回多く Execute すると、その 1 回ぶんだけ増える
        scene.Context.RenderFrameSerial += 1;
        for (uint32_t viewport = 0; viewport <= ViewportsPerFrame; ++viewport)
        {
            assert(scene.Graph.ExecuteWithResult(scene.Context).bSuccess);
        }
        assert(CountBufferCreations(device, frameUniform) == ViewportsPerFrame + 1);
        assert(CountBufferCreations(device, mergeParams) == ViewportsPerFrame + 1);
        assert(CountBufferCreations(device, keyBuffer) == 1);
        expectMaterialScratchStable();
        assert(scene.Raster.GetProceduralChunkScratchCapacity() == proceduralScratchCapacity &&
               scene.Raster.GetSkinnedChunkScratchCapacity() == skinnedScratchCapacity);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録の材質の番号は、フレームごとの材質の表の番号になる。MegaGeometry の区間・手続きメッシュ・スキニングの描画が
    // 同じ表を引き、同じ材質は同じ番号・違う材質は違う番号・番号は 0 から詰まる。元の MaterialIndex（区間の番号・描画のコマンドの値）に
    // 戻す配線の誤りを、パスを通して検出する
    void TestVisibilityRasterRecordsFrameUniqueMaterialTableIndices()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true);

        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(stats.ProceduralRecords == 3 && stats.SkinnedRecords == 1);
        // 材質は 3 件: MegaGeometry の既定の材質（区間はすべて同じ値）・A・B
        assert(stats.MaterialUnique == 3 && stats.MaterialOverflowed == 0);

        // ホストが書く記録（手続き 3 件 → スキニング 1 件の順）。GPU 専用の記録の表へは、置き場からのコピーで入る
        const auto* recordTable = static_cast<const FakeBuffer*>(scene.Raster.GetRecordTable().get());
        assert(recordTable != nullptr);
        assert(!recordTable->GetDesc().CPUAccessible);
        const auto* recordUpload = static_cast<const FakeBuffer*>(scene.Device->VisBufferRecordUpload.get());
        assert(recordUpload != nullptr && recordUpload->GetDesc().CPUAccessible);
        constexpr size_t RecordCount = 4;
        assert(recordUpload->LastUpdateBytes.size() == RecordCount * sizeof(VisibilityBuffer::DrawRecord));
        assert(scene.CommandList.RecordTableCopies.size() == 1);
        assert(scene.CommandList.RecordTableCopies[0].SourceOffsetBytes == 0);
        assert(scene.CommandList.RecordTableCopies[0].SizeBytes == RecordCount * sizeof(VisibilityBuffer::DrawRecord));
        // 書き込み先は MegaGeometry の範囲（0 番の空 + コマンドの枠）の後ろ
        assert(scene.CommandList.RecordTableCopies[0].DestinationOffsetBytes ==
               (1 + stats.MegaCommandSlots) * sizeof(VisibilityBuffer::DrawRecord));
        VisibilityBuffer::DrawRecord records[RecordCount];
        std::memcpy(records, recordUpload->LastUpdateBytes.data(), sizeof(records));
        const uint32_t indexA = records[0].MaterialIndex;
        const uint32_t indexB = records[2].MaterialIndex;
        assert(records[1].MaterialIndex == indexA);  // 別のコマンドでも同じ材質は同じ番号
        assert(records[3].MaterialIndex == indexA);  // スキニングの描画でも同じ番号
        assert(indexB != indexA);                    // 違う材質は違う番号
        assert(indexA < 3 && indexB < 3);

        // MegaGeometry の区間から表の番号への対応: 区間の材質はどれも既定の 1 件で、A・B とは違う番号
        const auto* sectionMaterials = static_cast<const FakeBuffer*>(scene.Device->VisBufferSectionMaterials.get());
        assert(sectionMaterials != nullptr);
        const size_t sectionCount = sectionMaterials->LastUpdateBytes.size() / sizeof(uint32_t);
        assert(sectionCount >= 1);
        Container::VariableArray<uint32_t> sections;
        sections.resize(sectionCount);
        std::memcpy(sections.data(), sectionMaterials->LastUpdateBytes.data(), sectionCount * sizeof(uint32_t));
        for (const uint32_t section : sections)
        {
            assert(section == sections[0]);
            assert(section != indexA && section != indexB);
        }
        // 番号は 0 から詰まる（区間・A・B で {0, 1, 2} をちょうど使う）
        assert(sections[0] < 3 && sections[0] + indexA + indexB == 3);

        // 表の中身: その番号の件が、その材質の値（基本色）を持つ
        const auto* materialTable = static_cast<const FakeBuffer*>(scene.Raster.GetMaterialTable().get());
        assert(materialTable != nullptr && scene.Raster.GetMaterialTableCount() == 3);
        assert(materialTable->LastUpdateBytes.size() == 3 * sizeof(VisibilityBuffer::MaterialEntry));
        VisibilityBuffer::MaterialEntry entries[3];
        std::memcpy(entries, materialTable->LastUpdateBytes.data(), sizeof(entries));
        assert(entries[indexA].BaseColor[0] == 0.2f && entries[indexA].BaseColor[2] == 0.6f);
        assert(entries[indexB].BaseColor[0] == 0.9f && entries[indexB].BaseColor[1] == 0.1f);

        ShutdownVisibilityRasterScene(scene);
    }

    // 遮蔽カリングを使わない（1パスだけの）経路でも、その1パスのコマンドを描き直す
    void TestVisibilityRasterOnSinglePassMegaGeometry()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, false);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（1パス）・ID・64bit のバッファの合流
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[1].OffsetBytes == commandList.IndirectDraws[0].OffsetBytes);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 2);
        // カリング 1 回 + 記録を書く計算 1 回
        assert(commandList.DispatchCount == 2);
        assert(scene.Raster.GetLastFrameStats().MegaCommandSlots == 2);
        assert(scene.Raster.GetLastFrameStats().TotalSlots == 3);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録された直接の描画（間接描画を除く描画の呼び出し）の数。GeometryPass のコマンドは描画のパスの実行で消費されるので、
    // GBufferPass が積んだ描画は、コマンドリストの描画の呼び出しから間接描画（MegaGeometry・ID の描画）を引いて数える。
    // 解決を使わない On の場面（手続きメッシュ 3 件・スキニング 1 件）では、GBufferPass の描画 3 件（手続きメッシュ。この場面の
    // スキニングは GBufferPass の描画として記録されない）と、ID の描画の直接の描画 4 件（手続きの塊 3・スキニングの塊 1）になる
    constexpr size_t SceneGBufferDirectDraws = 3;
    constexpr size_t SceneIdDirectDraws = 4;
    // 64bit のバッファの合流の全画面の描画（直接の描画）。1 回の render pass の構成は描画の後の 1 回、
    // 2 パスの遮蔽で ID のラスタが記録を駆動する構成は「1 回目の描画の後・HZB の前」と「2 回目の描画の後」の 2 回
    constexpr size_t SceneMergeDrawsOnePass = 1;
    constexpr size_t SceneMergeDrawsStaged = 2;
    size_t CountDirectDraws(const FakeCommandList& commandList)
    {
        assert(commandList.DrawCallCount >= commandList.IndirectDraws.size());
        return commandList.DrawCallCount - commandList.IndirectDraws.size();
    }

    // 解決を使わない On（ID の描画に加えて GBuffer へも描く）の、描画・dispatch・レンダーパスの数。解決を使う構成と比べる基準。
    // 直接の描画の数（DirectDraws）も数えるので、GBufferPass の描画の抑制は記録された描画で確かめられる
    struct OnWithoutResolveBaseline
    {
        size_t DirectDraws = 0;
        size_t IndirectDraws = 0;
        uint32_t Dispatches = 0;
        uint32_t RenderPasses = 0;
        bool bGBufferVelocityHasShaderWrite = false;
        size_t GBufferVelocityBarriers = 0;
    };

    // GBuffer.Velocity（RG16F）のバリアに現れるテクスチャが ShaderWrite の使い道を持つか。barrierCount にバリアの数を返す
    bool VelocityTexturesHaveShaderWrite(const FakeCommandList& commandList, size_t& barrierCount)
    {
        bool bAllHaveShaderWrite = true;
        barrierCount = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Texture && barrier.Texture && barrier.Texture->GetFormat() == RHI::Format::R16G16_FLOAT)
            {
                ++barrierCount;
                bAllHaveShaderWrite = bAllHaveShaderWrite && HasUsage(barrier.Texture->GetUsage(), RHI::ResourceUsage::ShaderWrite);
            }
        }
        return barrierCount > 0 && bAllHaveShaderWrite;
    }

    OnWithoutResolveBaseline MeasureOnWithoutResolve()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true);
        OnWithoutResolveBaseline baseline;
        baseline.DirectDraws = CountDirectDraws(scene.CommandList);
        baseline.IndirectDraws = scene.CommandList.IndirectDraws.size();
        baseline.Dispatches = scene.CommandList.DispatchCount;
        baseline.RenderPasses = scene.CommandList.BeginRenderPassCount;
        baseline.bGBufferVelocityHasShaderWrite = VelocityTexturesHaveShaderWrite(scene.CommandList, baseline.GBufferVelocityBarriers);
        ShutdownVisibilityRasterScene(scene);
        return baseline;
    }

    // --visibility-buffer=on（解決が GBuffer を書く）: GBuffer の描画を止めるので、2 パスの遮蔽の HZB は ID のラスタの深度から作る。
    // MegaGeometryPass は記録を ID のラスタの Execute へ移し、順は
    //   1 パス目のカリング → ID・深度の 1 回目の render pass（手続き・スキニングの塊 → MegaGeometry の 1 パス目）
    //   → HZB（その深度から）→ 2 パス目のカリング → 記録を書く計算 → ID・深度の 2 回目の render pass（2 パス目）
    // MegaGeometryPass の記録をラスタへ移す配線（Execute の判定・IDrawSink）や、2 つの呼び出しの順を戻すと落ちる
    void TestVisibilityRasterOnBuildsHiZFromIdDepthBetweenTwoPasses()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Resolve.CanResolve(scene.Device.get()));
        assert(scene.Resolve.WasResolved());
        // 移したフレームの記録は、ラスタの Execute が取り出して済んでいる
        assert(!scene.Mega.IsFrameRecordDeferred());

        const auto& sequence = commandList.CallSequence;
        const char expected[] = {
            'B', 'E',                                   // GBufferPass: 描画を止めてクリアと添付の遷移だけ
            'B', 'E',                                   // MegaGeometryPass: GBuffer の添付の遷移だけ（描画も記録もラスタへ移した）
            'D',                                        // 計算スキニング（この場面のスキニングの塊 1 体）
            'D',                                        // 1 パス目のカリング（ラスタの Execute の中）
            'B', 'I', 'E',                              // ID・深度の 1 回目: 塊（直接描画。並びには出ない）→ MegaGeometry の 1 パス目
            'B', 'E',                                   // 64bit のバッファの合流（HZB の前。全画面の描画 1 回）
            'D', 'D', 'D', 'D', 'D', 'D', 'D',          // HZB の 7 段（128x64 の深度から。ID の 1 回目と合流が書いた深度）
            'D',                                        // 2 パス目のカリング
            'D', 'J',                                   // 記録を書く計算の引数を作る計算 → 記録を書く計算（間接 dispatch）
            'B', 'I', 'E',                              // ID・深度の 2 回目: MegaGeometry の 2 パス目
            'B', 'E',                                   // 64bit のバッファの合流（2 パス目の後）
        };
        assert(sequence.size() >= sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(sequence[i] == expected[i]);
        }
        // 合流の後は、解決の dispatch だけ（描画も render pass も無い）
        for (size_t i = sizeof(expected); i < sequence.size(); ++i)
        {
            assert(sequence[i] == 'D');
        }
        assert(commandList.BeginRenderPassCount == 6);
        assert(commandList.EndRenderPassCount == 6);

        // 64bit のバッファは、ラスタの最初の render pass の前（GBuffer・MegaGeometry の遷移・計算スキニング・1 パス目のカリングの後）に、
        // すべてのビットが 1 で 1 回だけ埋める。ソフトウェアラスタが書く前に空にするため、1 回目の描画より後ろへ動かすと落ちる
        assert(commandList.Key64Fills.size() == 1);
        assert(commandList.Key64Fills[0].SizeBytes == 128u * 64u * 8u);
        assert(commandList.Key64Fills[0].Value == 0xFFFFFFFFu);
        assert(commandList.Key64Fills[0].SequencePosition == 6);
        assert(sequence[6] == 'B' && sequence[7] == 'I' && sequence[8] == 'E');

        // 合流は全画面の描画が 1 回ずつ、「1 回目の render pass の直後・HZB の前」と「2 回目の render pass の直後」の
        // 空の render pass（B と E の間に描画の呼び出しだけ）として記録される。HZB の後ろへ動かす・2 回目を外すと落ちる
        Container::VariableArray<size_t> mergeDrawPositions;
        for (const size_t position : commandList.DrawPositions)
        {
            if (position > 0 && position < sequence.size() && sequence[position - 1] == 'B' && sequence[position] == 'E')
            {
                mergeDrawPositions.push_back(position);
            }
        }
        assert(mergeDrawPositions.size() == SceneMergeDrawsStaged);
        assert(mergeDrawPositions[0] == 10); // 1 回目の B・I・E（6〜8）の直後の B（9）の中。この後に HZB の 7 段
        assert(mergeDrawPositions[1] == 25); // 2 回目の B・I・E（21〜23）の直後の B（24）の中
        for (size_t i = 11; i < 18; ++i)
        {
            assert(sequence[i] == 'D'); // 1 つ目の合流の後は HZB の 7 段
        }

        // 間接描画は 1 パス目（範囲 0）・2 パス目（範囲は 1 パスのコマンド数ぶん後ろ）で 1 回ずつ。GBuffer へは描かない
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[1].OffsetBytes == 2 * 20);

        // 手続き・スキニングの塊は、すべて 1 回目の render pass の中で、MegaGeometry の 1 パス目より前（HZB の元の深度に入る）
        assert(commandList.InstancedDrawSequencePositions.size() == SceneIdDirectDraws);
        for (const size_t position : commandList.InstancedDrawSequencePositions)
        {
            assert(position == 7); // 'B' の直後（1 パス目の間接描画 'I' より前）
        }

        // 記録の表は MegaGeometry の 2 パスぶんのコマンド（4）と塊（手続き 3・スキニング 1）の記録を持つ
        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(stats.MegaCommandSlots == 4);
        assert(stats.ProceduralRecords == 3 && stats.SkinnedRecords == 1);
        assert(stats.TotalSlots == 1 + 4 + 4);
        assert(scene.Raster.GetRecordTable());
        assert(scene.Raster.GetMegaInstanceBuffer());

        // MegaGeometryPass が残したコマンド・カウンタ・描画情報は、記録を移した後も ID の描画の後に Common へ戻る
        size_t commonBarriers = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::Common &&
                barrier.BeforeState == RHI::ResourceState::GenericRead)
            {
                const auto* fakeBuffer = static_cast<const FakeBuffer*>(barrier.Buffer);
                const char* name = fakeBuffer->GetDesc().DebugName;
                if (IsDebugName(name, "MegaGeometry_IndirectDraw") || IsDebugName(name, "MegaGeometry_DrawCount") ||
                    IsDebugName(name, "MegaGeometry_DrawInfo"))
                {
                    ++commonBarriers;
                }
            }
        }
        assert(commonBarriers == 3);

        ShutdownVisibilityRasterScene(scene);
    }

    // 遮蔽カリングを使わない（1 回の判定）構成では、HZB が要らないので記録を移しても 2 回に分けず、描画の写しを残して
    // ラスタが MegaGeometry の全部と塊を 1 回の render pass で描く（記録を書く計算は描画の前に 1 回）
    void TestVisibilityRasterOnSinglePassCullingDrawsEverythingInOneRenderPass()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, false, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Resolve.WasResolved());
        const auto& sequence = commandList.CallSequence;
        const char expected[] = {
            'B', 'E',      // GBufferPass
            'B', 'E',      // MegaGeometryPass: GBuffer の添付の遷移だけ
            'D',           // 計算スキニング
            'D',           // 1 回の判定のカリング（ラスタの Execute の中）
            'D', 'J',      // 記録を書く計算の引数を作る計算 → 記録を書く計算（間接 dispatch）
            'B', 'I', 'E', // ID・深度: MegaGeometry（1 パス）→ 塊
            'B', 'E',      // 64bit のバッファの合流（HZB を作らないので、描画の後に 1 回）
        };
        assert(sequence.size() >= sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(sequence[i] == expected[i]);
        }
        assert(commandList.IndirectDraws.size() == 1);
        assert(commandList.InstancedDrawSequencePositions.size() == SceneIdDirectDraws);
        for (const size_t position : commandList.InstancedDrawSequencePositions)
        {
            assert(position == 10); // 間接描画 'I' の後（塊は MegaGeometry の後）
        }
        // 1 パスぶんのコマンド（同じ材質の 2 インスタンスは 1 つの区間でクラスタ数の合計 2）だけ
        assert(scene.Raster.GetLastFrameStats().MegaCommandSlots == 2);

        ShutdownVisibilityRasterScene(scene);
    }

    // --visibility-buffer=on: GBufferPass・MegaGeometryPass は GBuffer の描画を止め（クリアと添付の遷移だけ残す）、
    // 幾何の解決のパスが GBuffer の Albedo・Normal・Material・Velocity・Emissive を storage image として書く。
    // 止める配線（SetVisibilityResolveActive・SetSkipGBufferDraw）や、storage image の使い道・書き込みの状態を戻すと落ちる
    void TestVisibilityResolveOnReplacesGBufferDrawsWithStorageImageWrites()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();
        // 解決を使わない On は、ID の描画 + MegaGeometry の GBuffer への描画の間接描画 4 回。GBufferPass は手続きメッシュ 3 件を
        // GBuffer へ描く
        assert(baseline.IndirectDraws == 4);
        assert(baseline.DirectDraws == SceneGBufferDirectDraws + SceneIdDirectDraws + SceneMergeDrawsOnePass);
        // 基準では GBuffer.Velocity は storage image として使われない
        assert(baseline.GBufferVelocityBarriers > 0 && !baseline.bGBufferVelocityHasShaderWrite);

        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        // GBufferPass は描画を 1 件も積まない（クリアの GeometryPass だけが残り、ID の描画の直接の描画だけが残る）
        assert(CountDirectDraws(commandList) == SceneIdDirectDraws + SceneMergeDrawsStaged);
        assert(scene.Resolve.CanResolve(scene.Device.get()));

        // MegaGeometry の GBuffer への間接描画（1・2 パス目）は止まり、ID の描画の 2 回だけが残る。render pass の数は、
        // 2 パスの遮蔽で ID のラスタが駆動する構成の合流が基準（描画の後の 1 回）より 1 回多いぶんだけ増える
        assert(commandList.IndirectDraws.size() == baseline.IndirectDraws - 2);
        assert(commandList.BeginRenderPassCount == baseline.RenderPasses + 1);
        assert(commandList.EndRenderPassCount == baseline.RenderPasses + 1);

        // 解決: 画面を 8x8 のタイルに分けた 1 回の dispatch（128x64 → 16x8 グループ）が最後に足される
        assert(scene.Resolve.WasResolved());
        assert(commandList.DispatchCount == baseline.Dispatches + 1);
        const FakeCommandList::DispatchSize& resolveGroups = commandList.DispatchGroups.back();
        assert(resolveGroups.X == 16 && resolveGroups.Y == 8 && resolveGroups.Z == 1);

        // GBuffer の 5 枚（Albedo・Normal・Material・Velocity・Emissive）が storage image の状態（UnorderedAccess）へ遷移し、
        // 書き込みの使い道（ShaderWrite）を持つ。Albedo と Material はどちらも RGBA8、Normal と Emissive はどちらも RGBA16F なので
        // 形式では区別できず、それぞれ 2 枚と数える
        uint32_t albedoWrites = 0;
        uint32_t normalWrites = 0;
        uint32_t velocityWrites = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind != RGBarrierKind::Texture || !barrier.Texture || barrier.AfterState != RHI::ResourceState::UnorderedAccess)
            {
                continue;
            }
            switch (barrier.Texture->GetFormat())
            {
            case RHI::Format::R8G8B8A8_UNORM:
                albedoWrites += HasUsage(barrier.Texture->GetUsage(), RHI::ResourceUsage::ShaderWrite) ? 1 : 0;
                break;
            case RHI::Format::R16G16B16A16_FLOAT:
                normalWrites += HasUsage(barrier.Texture->GetUsage(), RHI::ResourceUsage::ShaderWrite) ? 1 : 0;
                break;
            case RHI::Format::R16G16_FLOAT:
                velocityWrites += HasUsage(barrier.Texture->GetUsage(), RHI::ResourceUsage::ShaderWrite) ? 1 : 0;
                break;
            default:
                break;
            }
        }
        assert(albedoWrites == 2 && normalWrites == 2 && velocityWrites == 1);
        size_t velocityBarriers = 0;
        assert(VelocityTexturesHaveShaderWrite(commandList, velocityBarriers));

        ShutdownVisibilityRasterScene(scene);
    }

    // 装置が解決に対応しない（頂点のデバイスアドレス・拡張形式の storage image が無い）ときは、配線が On でも
    // GBuffer の描画を止めず、解決のパスは何もしない（見える物が消えない）。
    void TestVisibilityResolveUnsupportedDeviceKeepsGBufferDraws()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();

        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::UnsupportedDevice);
        FakeCommandList& commandList = scene.CommandList;
        // 予備の GBuffer の描画へ戻るので、ID のラスタは何も描かない（MegaGeometry の描画の写しは、ラスタが取り出して捨てる）。
        // GBuffer への間接描画（1・2 パス目）と手続きメッシュの直接描画だけが残る
        assert(commandList.IndirectDraws.size() == baseline.IndirectDraws - 2);
        assert(CountDirectDraws(commandList) == SceneGBufferDirectDraws);
        assert(!scene.Raster.GetRecordTable());
        assert(!scene.Resolve.WasResolved());
        assert(scene.Resolve.GetFallbackReason(scene.Device.get()) ==
               VisibilityResolveGeometry::FallbackReason::DeviceUnsupported);
        // ID のラスタの記録の表を作る dispatch 1 回と、計算スキニングの dispatch 1 回が、基準（解決を使わない On）より少ない
        // （予備の GBuffer の描画は頂点シェーダーでスキニングするので、計算スキニングの結果は誰も読まない）
        assert(scene.Skinning.GetInstances().empty());
        assert(commandList.DispatchCount + 2 == baseline.Dispatches);
        size_t velocityBarriers = 0;
        assert(!VelocityTexturesHaveShaderWrite(commandList, velocityBarriers) && velocityBarriers > 0);

        ShutdownVisibilityRasterScene(scene);
    }

    // 装置が対応していても、ID のラスタのパイプラインや解決の計算パイプラインが作れていないときは、GBufferPass・MegaGeometryPass が
    // 描画を止めない（止めると画面が空になる）。従来の GBuffer の描画が残り、解決は何も記録せず、理由が分かる。
    // GBufferPass・MegaGeometryPass の判定を装置の機能だけに戻す（解決への問い合わせを外す）と落ちる
    void TestVisibilityResolveFallsBackToGBufferDrawsWhenPipelinesAreUnavailable()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();
        assert(baseline.DirectDraws == SceneGBufferDirectDraws + SceneIdDirectDraws + SceneMergeDrawsOnePass);

        struct Case
        {
            ResolveMode Mode;
            VisibilityResolveGeometry::FallbackReason Reason;
        };
        const Case cases[] = {
            {ResolveMode::RasterPipelineUnavailable, VisibilityResolveGeometry::FallbackReason::RasterUnavailable},
            {ResolveMode::ResolvePipelineUnavailable, VisibilityResolveGeometry::FallbackReason::ResolveUnavailable},
        };
        for (const Case& testCase : cases)
        {
            VisibilityRasterScene scene;
            RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, testCase.Mode);
            FakeCommandList& commandList = scene.CommandList;

            assert(scene.Resolve.GetFallbackReason(scene.Device.get()) == testCase.Reason);
            assert(!scene.Resolve.CanResolve(scene.Device.get()));
            assert(!scene.Resolve.WasResolved());

            // GBufferPass は従来どおり描画を積み、MegaGeometryPass も GBuffer への間接描画（1・2 パス目）を止めない。
            // 予備へ戻っている間は ID を誰も読まないので、ID のラスタは（パイプラインが揃っていても）何も描かない
            assert(CountDirectDraws(commandList) == SceneGBufferDirectDraws);
            assert(commandList.IndirectDraws.size() == baseline.IndirectDraws - 2);
            assert(!scene.Raster.GetRecordTable());
            // GBuffer の 3 枚は storage image として使われない（解決が書かない）
            size_t velocityBarriers = 0;
            assert(!VelocityTexturesHaveShaderWrite(commandList, velocityBarriers) && velocityBarriers > 0);

            ShutdownVisibilityRasterScene(scene);
        }
    }

#if NORVES_ENABLE_LOGGING
    // ソフトウェアラスタが BDA に対応しない装置で使えないときのログ（カテゴリ VisibilityRasterPass の SW_RASTER_FALLBACK reason=bda_unsupported）の数を数える
    struct SwRasterBdaFallbackCounter final : Logging::ILogSink
    {
        uint32_t Count = 0;

        void OnLog(const Logging::LogEntry& entry) override
        {
            if (entry.category == "VisibilityRasterPass" &&
                std::strstr(entry.message.c_str(), "SW_RASTER_FALLBACK reason=bda_unsupported") != nullptr)
            {
                ++Count;
            }
        }
    };

    // 幾何の解決のフォールバックのログ（カテゴリ VisibilityResolvePass の VISBUFFER_FALLBACK）の数を数える
    struct ResolveFallbackCounter final : Logging::ILogSink
    {
        uint32_t Count = 0;
        uint32_t SkinningComputeCount = 0;

        void OnLog(const Logging::LogEntry& entry) override
        {
            if (entry.level == Logging::LogLevel::Warning && entry.category == "VisibilityResolvePass" &&
                std::strstr(entry.message.c_str(), "VISBUFFER_FALLBACK") != nullptr)
            {
                ++Count;
                if (std::strstr(entry.message.c_str(), "reason=skinning_compute_unavailable") != nullptr)
                {
                    ++SkinningComputeCount;
                }
            }
        }
    };
#endif

    // 計算スキニングのパイプラインだけが作れないとき、解決へ進むとスキニングの頂点が無く、ID のラスタがスキニングの塊を描けない。
    // 解決は使えない（SkinningComputeUnavailable）として、GBufferPass・MegaGeometryPass は描画を止めず、解決は何も記録しない。
    // フォールバックのログ（VISBUFFER_FALLBACK）は、何フレーム Declare されても 1 回だけ。計算スキニングが作れている構成では出ない。
    // 判定から計算スキニングの準備を外す、または SceneView が計算スキニングのパスを解決へ渡す配線を外すと落ちる
    void TestVisibilityResolveFallsBackToGBufferDrawsWhenSkinningComputeUnavailable()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        ResolveFallbackCounter fallbackLogs;
        logger.AddSink(&fallbackLogs);
#endif

        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();
        {
            // 対照: 計算スキニングが作れていれば解決を使い、フォールバックのログは出ない
            VisibilityRasterScene supported;
            RunVisibilityRasterScene(supported, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
            assert(supported.Resolve.GetFallbackReason(supported.Device.get()) == VisibilityResolveGeometry::FallbackReason::None);
            assert(supported.Resolve.WasResolved());
#if NORVES_ENABLE_LOGGING
            assert(fallbackLogs.Count == 0);
#endif
            ShutdownVisibilityRasterScene(supported);
        }

        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::SkinningComputePipelineUnavailable);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Skinning.IsEnabled() && !scene.Skinning.IsComputeReady());
        assert(scene.Resolve.GetFallbackReason(scene.Device.get()) ==
               VisibilityResolveGeometry::FallbackReason::SkinningComputeUnavailable);
        assert(!scene.Resolve.CanResolve(scene.Device.get()));
        assert(!scene.Resolve.WasResolved());

        // GBufferPass は従来どおり描画を積み、MegaGeometryPass も GBuffer への間接描画を止めない（スキニングの物も GBuffer に描かれる）。
        // ID のラスタは予備へ戻っている間は何も描かない（スキニングの頂点が無いので、描いてもスキニングの塊は欠ける）
        assert(commandList.IndirectDraws.size() == baseline.IndirectDraws - 2);
        assert(CountDirectDraws(commandList) == SceneGBufferDirectDraws);
        assert(!scene.Raster.GetRecordTable());
        size_t velocityBarriers = 0;
        assert(!VelocityTexturesHaveShaderWrite(commandList, velocityBarriers) && velocityBarriers > 0);
#if NORVES_ENABLE_LOGGING
        assert(fallbackLogs.Count == 1 && fallbackLogs.SkinningComputeCount == 1);
#endif

        // 続くフレームでも Declare は呼ばれるが、ログは増えない（解決のパスだけをグラフへ足して回す）
        for (uint64_t frame = 1; frame <= 2; ++frame)
        {
            scene.Pool.EndFrame();
            scene.Pool.BeginFrame(frame);
            scene.Graph.BeginFrame(frame);
            scene.Graph.AddPass(&scene.Resolve);
            assert(scene.Graph.Compile(scene.Context));
            const RenderGraphExecutionResult result = scene.Graph.ExecuteWithResult(scene.Context);
            assert(result.bSuccess);
            assert(!scene.Resolve.WasResolved());
        }
#if NORVES_ENABLE_LOGGING
        assert(fallbackLogs.Count == 1 && fallbackLogs.SkinningComputeCount == 1);
        // 共有の Logger を、このテストが初期化し直す前の状態へ戻す（この実行ファイルは他で Logger を初期化しない = 未初期化）
        logger.RemoveSink(&fallbackLogs);
        logger.Shutdown();
#endif
        ShutdownVisibilityRasterScene(scene);
    }

    // SetPipeline に渡された、グラフィックスのパイプラインのうち、polygonMode が mode のものの数
    size_t CountGraphicsPipelineSets(const FakeCommandList& commandList, RHI::PolygonMode mode)
    {
        size_t count = 0;
        for (const RHI::PipelinePtr& pipeline : commandList.SetPipelines)
        {
            if (pipeline && pipeline->GetPipelineType() == RHI::PipelineType::Graphics &&
                static_cast<const FakePipeline*>(pipeline.get())->GetPolygonMode() == mode)
            {
                ++count;
            }
        }
        return count;
    }

    // --visibility-buffer=on でワイヤーフレームを選ぶと、ID のラスタは三角形を線で描き（MegaGeometry・手続き・スキニングの
    // 3 種とも線のパイプライン）、GBuffer は描画を止めたまま解決が線の画素を書く。通常の表示では塗りの 3 種だけを使う。
    // ラスタが表示で線のパイプラインを選ぶ配線、解決・GBufferPass・MegaGeometryPass が表示を渡す配線を外すと落ちる
    void TestVisibilityRasterWireframeDrawsLinesAndResolveWritesThem()
    {
        {
            VisibilityRasterScene normalScene;
            RunVisibilityRasterScene(normalScene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
            // 手続き・スキニング 1 回ずつと、MegaGeometry の 1 パス目・2 パス目（render pass が 2 回に分かれる）で 1 回ずつ
            // （64bit のバッファの合流の塗りのパイプラインも、1 回目・2 回目の後に 1 回ずつ使われる）
            assert(CountGraphicsPipelineSets(normalScene.CommandList, RHI::PolygonMode::Fill) == 4 + SceneMergeDrawsStaged);
            assert(CountGraphicsPipelineSets(normalScene.CommandList, RHI::PolygonMode::Line) == 0);
            ShutdownVisibilityRasterScene(normalScene);
        }

        VisibilityRasterScene scene;
        scene.DebugMode = DebugViewMode::Wireframe;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Raster.IsDrawReady(DebugViewMode::Wireframe));
        assert(scene.Resolve.CanResolve(scene.Device.get(), DebugViewMode::Wireframe));
        assert(CountGraphicsPipelineSets(commandList, RHI::PolygonMode::Line) == 4);
        assert(CountGraphicsPipelineSets(commandList, RHI::PolygonMode::Fill) == 0);

        // GBuffer の描画は止まったまま（ID の描画だけが残り）、解決が GBuffer を書く
        assert(CountDirectDraws(commandList) == SceneIdDirectDraws);
        assert(commandList.IndirectDraws.size() == 2);
        assert(scene.Resolve.WasResolved());

        ShutdownVisibilityRasterScene(scene);
    }

    // 線のパイプラインが作れない装置では、ワイヤーフレームの表示だけ従来の GBuffer の描画（線）へ戻す。
    // この表示のフレームは予備へ戻るので ID のラスタは描かず、通常の表示では解決を使う。表示を解決へ問い合わせる配線を外す
    // （表示を渡さない）と、GBuffer の描画が止まって線の画素を誰も書かなくなり、この検査が落ちる
    void TestVisibilityRasterWireframeFallsBackToGBufferWhenLinePipelinesUnavailable()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();

        VisibilityRasterScene scene;
        scene.DebugMode = DebugViewMode::Wireframe;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true,
                                 ResolveMode::RasterWireframePipelineUnavailable);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Raster.IsDrawReady());
        assert(!scene.Raster.IsDrawReady(DebugViewMode::Wireframe));
        assert(scene.Resolve.GetFallbackReason(scene.Device.get(), DebugViewMode::Wireframe) ==
               VisibilityResolveGeometry::FallbackReason::RasterUnavailable);
        assert(scene.Resolve.GetFallbackReason(scene.Device.get()) == VisibilityResolveGeometry::FallbackReason::None);
        assert(!scene.Resolve.WasResolved());

        // GBufferPass・MegaGeometryPass は GBuffer へ描き続ける。ID のラスタは何も描かない（解決を使わない On より ID の描画の分だけ少ない）
        assert(CountDirectDraws(commandList) == SceneGBufferDirectDraws);
        assert(commandList.IndirectDraws.size() == baseline.IndirectDraws - 2);
        assert(!scene.Raster.GetRecordTable());
        // GBuffer の描画は線のパイプライン。塗りのパイプラインは ID のラスタのものだけなので、使われない
        assert(CountGraphicsPipelineSets(commandList, RHI::PolygonMode::Line) > 0);
        assert(CountGraphicsPipelineSets(commandList, RHI::PolygonMode::Fill) == 0);

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質ごとの解決の構成（分類を解決より前に足す）の dispatch の並びの検査。CallSequence の末尾は、分類の 3 回の dispatch（D）の
    // 後に、間接 dispatch（J）が材質の数だけ続く（解決は最後のパス）
    void AssertTileResolveSequence(const FakeCommandList& commandList, size_t materialCount)
    {
        const auto& sequence = commandList.CallSequence;
        assert(sequence.size() >= materialCount + MaterialTileClassify::DispatchesPerRecord);
        const size_t firstIndirect = sequence.size() - materialCount;
        for (size_t index = firstIndirect; index < sequence.size(); ++index)
        {
            assert(sequence[index] == 'J');
        }
        for (size_t index = firstIndirect - MaterialTileClassify::DispatchesPerRecord; index < firstIndirect; ++index)
        {
            assert(sequence[index] == 'D');
        }
    }

    // --visibility-buffer=on の解決は、分類（MaterialTileClassifyPass）が作る材質ごとのタイルの引数・一覧で、材質ごとに
    // 1 回ずつ間接 dispatch する。画面全体の直接 dispatch は記録しない。分類 → 解決の順で、引数は分類の最終のバリア
    // （UnorderedAccess → GenericRead）の 1 回だけで、解決の読み取りのために足されるバリアは無い。
    // SceneView の配線（分類を有効にして解決より前に足す・解決へ分類を渡す）、解決の dispatch の数（材質の表の数）、
    // 解決が分類を読む宣言を戻す・外すと落ちる
    void TestVisibilityResolveDispatchesPerMaterialFromClassification()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();

        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::BeforeResolve, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry・スキニング・ビジビリティ・材質の分類・解決の 6 パス
        assert(scene.Graph.GetLastExecutedPassCount() == 6);
        assert(scene.Classify.WasClassified());
        assert(scene.Resolve.WasResolved());
        assert(scene.Resolve.WasResolvedWithTiles());

        // 材質ごとに 1 回ずつ。数はそのフレームの材質の表の数（引数の表の件数の 1024 ではない）
        const uint32_t materialCount = scene.Raster.GetMaterialTableCount();
        assert(materialCount >= 2 && materialCount < MaterialTiles::DEFAULT_MAX_MATERIALS);
        assert(scene.Resolve.GetLastTileDispatchCount() == materialCount);
        // 資源の並べ替えの作業配列は Record の外のメンバで、材質の数ぶんの容量を持つ（Record のたびに作り直す形へ戻すと 0 になる）
        assert(scene.Resolve.GetTileUseScratchCapacity() >= materialCount);
        // 先頭の 1 回は記録を書く計算（ラスタの Execute の中）の間接 dispatch。その後ろが材質ごとの解決
        assert(commandList.IndirectDispatches.size() == materialCount + 1);
        assert(IsDebugName(commandList.IndirectDispatches[0].BufferName, "VisBuffer_RecordArgs"));
        for (uint32_t material = 0; material < materialCount; ++material)
        {
            const FakeCommandList::IndirectDispatchRecord& record = commandList.IndirectDispatches[1 + material];
            assert(IsDebugName(record.BufferName, "MaterialTile_Args"));
            assert(record.OffsetBytes == static_cast<uint64_t>(material) * MaterialTiles::ARGS_STRIDE_BYTES);
        }

        // 直接 dispatch の解決（画面全体）は記録しない。増えた dispatch は分類の 3 回だけ
        // （分類の dispatch もタイルごとに 16x8 グループなので、グループ数では区別せず、数で確かめる）
        assert(commandList.DispatchCount == baseline.Dispatches + MaterialTileClassify::DispatchesPerRecord);
        AssertTileResolveSequence(commandList, materialCount);

        // 引数は分類が GenericRead へ遷移させた 1 回だけで、解決の読み取りのためのバリアは足されない
        size_t argsToGenericRead = 0;
        size_t argsFromGenericRead = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind != RGBarrierKind::Buffer ||
                !IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, "MaterialTile_Args"))
            {
                continue;
            }
            argsToGenericRead += barrier.AfterState == RHI::ResourceState::GenericRead ? 1 : 0;
            argsFromGenericRead += barrier.BeforeState == RHI::ResourceState::GenericRead ? 1 : 0;
        }
        assert(argsToGenericRead == 1 && argsFromGenericRead == 0);

        // 解決は分類の引数・一覧・統計を GenericRead で読むと宣言している（グラフの依存。書くのは分類のパスだけ）
        for (const RGResourceHandle handle : {scene.Classify.GetArgsHandle(), scene.Classify.GetListHandle(), scene.Classify.GetStatsHandle()})
        {
            assert(handle.IsValid());
            uint32_t readers = 0;
            uint32_t writers = 0;
            for (uint32_t pass = 0; pass < scene.Graph.GetPassCount(); ++pass)
            {
                for (uint32_t access = 0; access < scene.Graph.GetDeclaredPassAccessCount(pass); ++access)
                {
                    RGResourceHandle resource;
                    RGAccessMode mode = RGAccessMode::Read;
                    RHI::ResourceState state = RHI::ResourceState::Common;
                    RHI::ResourceState finalState = RHI::ResourceState::Common;
                    const bool bGotAccess = scene.Graph.TryGetDeclaredPassAccess(pass, access, resource, mode, state, finalState);
                    assert(bGotAccess);
                    if (!(resource == handle))
                    {
                        continue;
                    }
                    if (mode == RGAccessMode::Read)
                    {
                        assert(state == RHI::ResourceState::GenericRead);
                        ++readers;
                    }
                    else
                    {
                        ++writers;
                    }
                }
            }
            assert(readers == 1 && writers == 1);
        }

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質ごとの解決を使えないときは、画面全体の直接 dispatch に戻る（画面を空にしない）。間接 dispatch は 1 回も記録しない。
    // 分類のパイプラインが無い（分類は何も宣言せず、解決は分類を読まない）・そのフレームの分類が記録できなかった
    // （引数を 0 にして終えた。タイルの形では何も解決されない）・解決の材質ごとの形のパイプラインが作れない
    void TestVisibilityResolveFallsBackToDirectDispatchWhenTilesUnavailable()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();

        struct Case
        {
            ClassifyMode Mode;
            /** @brief 分類が記録する dispatch の数 */
            uint32_t ClassifyDispatches;
            bool bClassifyRecorded;
        };
        const Case cases[] = {
            {ClassifyMode::BeforeResolvePipelineUnavailable, 0, false},
            {ClassifyMode::BeforeResolveWithoutRaster, 0, false},
            {ClassifyMode::BeforeResolveTilePipelineUnavailable, MaterialTileClassify::DispatchesPerRecord, true},
        };
        for (const Case& testCase : cases)
        {
            VisibilityRasterScene scene;
            RunVisibilityRasterScene(scene, true, true, testCase.Mode, true, true, ResolveMode::Supported);
            FakeCommandList& commandList = scene.CommandList;

            assert(scene.Classify.WasClassified() == testCase.bClassifyRecorded);
            assert(scene.Resolve.WasResolved());
            assert(!scene.Resolve.WasResolvedWithTiles());
            assert(scene.Resolve.GetLastTileDispatchCount() == 0);
            // 間接 dispatch は記録を書く計算の 1 回だけ（材質ごとの解決は無い）
            assert(commandList.IndirectDispatches.size() == 1);
            assert(IsDebugName(commandList.IndirectDispatches[0].BufferName, "VisBuffer_RecordArgs"));
            // 直接 dispatch の解決 1 回（画面全体 = 16x8 グループ）が最後に記録される
            assert(commandList.DispatchCount == baseline.Dispatches + testCase.ClassifyDispatches + 1);
            const FakeCommandList::DispatchSize& resolveGroups = commandList.DispatchGroups.back();
            assert(resolveGroups.X == 16 && resolveGroups.Y == 8 && resolveGroups.Z == 1);

            ShutdownVisibilityRasterScene(scene);
        }
    }

    // 材質ごとの間接 dispatch をコマンドリストが断ったとき（既定の ICommandList::DispatchIndirect は false を返す）も、
    // 画面全体の直接 dispatch に戻る。断られた後に何も走らない画面にしない
    void TestVisibilityResolveFallsBackToDirectDispatchWhenIndirectDispatchRejected()
    {
        const OnWithoutResolveBaseline baseline = MeasureOnWithoutResolve();

        VisibilityRasterScene scene;
        scene.CommandList.bRejectDispatchIndirect = true;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::BeforeResolve, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Classify.WasClassified());
        assert(scene.Resolve.WasResolved());
        assert(!scene.Resolve.WasResolvedWithTiles());
        assert(commandList.IndirectDispatches.empty());
        // 記録を書く計算も直接の dispatch に切り替わる（+ 1）
        assert(commandList.DispatchCount == baseline.Dispatches + MaterialTileClassify::DispatchesPerRecord + 1 + 1);
        const FakeCommandList::DispatchSize& resolveGroups = commandList.DispatchGroups.back();
        assert(resolveGroups.X == 16 && resolveGroups.Y == 8 && resolveGroups.Z == 1);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録を書く計算が間接 dispatch を断られたとき（既定の ICommandList::DispatchIndirect）も、直接の dispatch で記録を書く。
    // 断られた後に記録が書かれない画面にしない。グループ数は区間の容量から数えた上限（余りはシェーダーが引数の合計で捨てる）で、
    // 積まれうる全コマンドを覆う。DispatchIndirect の失敗を無視する形に戻すと、直接の dispatch が記録されず落ちる
    void TestVisibilityRasterRecordsFallBackToDirectDispatchWhenIndirectDispatchRejected()
    {
        VisibilityRasterScene scene;
        scene.CommandList.bRejectDispatchIndirect = true;
        RunVisibilityRasterScene(scene, true, true);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Raster.GetLastFrameStats().bRendered);
        assert(commandList.IndirectDispatches.empty());
        // dispatch: カリング 2 回 + HZB 7 段 + 記録の引数を作る計算 1 回 + 記録を書く計算（直接）1 回
        assert(commandList.DispatchCount == 11);

        // 並びの最後は、引数を作る計算（D）→ 記録を書く計算（D）→ ID のレンダーパス → 64bit のバッファの合流
        const auto& sequence = commandList.CallSequence;
        const char tail[] = {'D', 'D', 'B', 'I', 'I', 'E', 'B', 'E'};
        assert(sequence.size() > sizeof(tail));
        for (size_t i = 0; i < sizeof(tail); ++i)
        {
            assert(sequence[sequence.size() - sizeof(tail) + i] == tail[i]);
        }

        // 記録を書く計算のグループ数: x は 65535 までで y へ折り返し、積まれうる全コマンド（1 グループ 64 スレッド）を覆う
        const FakeCommandList::DispatchSize& groups = commandList.DispatchGroups.back();
        assert(groups.X >= 1 && groups.X <= 65535 && groups.Y >= 1 && groups.Z == 1);
        const uint64_t totalGroups = static_cast<uint64_t>(groups.X) * groups.Y;
        assert(totalGroups * 64u >= scene.Raster.GetLastFrameStats().MegaCommandSlots);
        // この場面は区間が 1 つ（容量 2）の 2 パスなので、上限は 2 グループ
        assert(groups.X == 2 && groups.Y == 1);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録の表（VisBuffer_DrawRecords）と記録の引数（VisBuffer_RecordArgs）のバリア。
    //   引数: 作る計算が書く前（→ UnorderedAccess）と、間接 dispatch が読む前（UnorderedAccess → GenericRead）
    //   表: ホストが書いた記録のコピーの前後（→ CopyDest、CopyDest → GenericRead）、記録を書く計算の前後（→ UnorderedAccess、
    //       UnorderedAccess → GenericRead）。どれかを外すと、GPU が書き終える前に読む・コピーと書き込みが競合する
    void TestVisibilityRasterRecordsBarriers()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true);
        FakeCommandList& commandList = scene.CommandList;
        assert(scene.CommandList.RecordTableCopies.size() == 1);

        struct Transition
        {
            RHI::ResourceState Before;
            RHI::ResourceState After;
        };
        const auto collect = [&](const char* name)
        {
            Container::VariableArray<Transition> transitions;
            for (const BarrierEvent& event : commandList.Barriers)
            {
                if (event.Kind == RGBarrierKind::Buffer && event.Buffer != nullptr &&
                    IsDebugName(static_cast<const FakeBuffer*>(event.Buffer)->GetDesc().DebugName, name))
                {
                    transitions.push_back(Transition{event.BeforeState, event.AfterState});
                }
            }
            return transitions;
        };

        const Container::VariableArray<Transition> args = collect("VisBuffer_RecordArgs");
        assert(args.size() == 2);
        assert(args[0].After == RHI::ResourceState::UnorderedAccess);
        assert(args[1].Before == RHI::ResourceState::UnorderedAccess && args[1].After == RHI::ResourceState::GenericRead);

        const Container::VariableArray<Transition> table = collect("VisBuffer_DrawRecords");
        assert(table.size() == 4);
        assert(table[0].After == RHI::ResourceState::CopyDest);
        assert(table[1].Before == RHI::ResourceState::CopyDest && table[1].After == RHI::ResourceState::GenericRead);
        assert(table[2].Before == RHI::ResourceState::GenericRead && table[2].After == RHI::ResourceState::UnorderedAccess);
        assert(table[3].Before == RHI::ResourceState::UnorderedAccess && table[3].After == RHI::ResourceState::GenericRead);

        ShutdownVisibilityRasterScene(scene);
    }

    // スキニングのインスタンスが 2 体のとき、2 体目の記録は、頂点のアドレスが 2 体目の先頭（変形した頂点の列の中の位置）を指し、
    // 頂点の基点は 0（アドレスに加算済みなので二重に足さない）。1 体だけの場面では基点が 0 になり検出できないので 2 体で確かめる。
    // 記録の頂点の基点へ出力の先頭の頂点番号を入れる（二重加算）形に戻すと、2 体目の解決が範囲外の頂点を読むので落ちる
    void TestVisibilityRasterSkinnedRecordsAddressEachBodyOnce()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::None, 2);

        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(stats.ProceduralRecords == 3 && stats.SkinnedRecords == 2);
        const auto& instances = scene.Skinning.GetInstances();
        assert(instances.size() == 2);
        // 1 体目は列の先頭、2 体目は 1 体目の頂点数（3）の後ろから始まる
        assert(instances[0].VertexBase == 0 && instances[0].VertexCount == 3);
        assert(instances[1].VertexBase == 3 && instances[1].VertexCount == 6);

        const auto* recordUpload = static_cast<const FakeBuffer*>(scene.Device->VisBufferRecordUpload.get());
        assert(recordUpload != nullptr);
        constexpr size_t RecordCount = 5; // 手続き 3 件 → スキニング 2 件
        assert(recordUpload->LastUpdateBytes.size() == RecordCount * sizeof(VisibilityBuffer::DrawRecord));
        VisibilityBuffer::DrawRecord records[RecordCount];
        std::memcpy(records, recordUpload->LastUpdateBytes.data(), sizeof(records));
        constexpr uint64_t OutputVertexBytes = 32;
        for (uint32_t body = 0; body < 2; ++body)
        {
            const VisibilityBuffer::DrawRecord& record = records[3 + body];
            assert(record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::SkinnedChunk));
            assert(record.InstanceIndex == body);
            assert(record.VertexBase == 0);
            assert(record.VertexAddress == SkinningCurrentVerticesAddress + instances[body].VertexBase * OutputVertexBytes);
            assert(record.PreviousVertexAddress == SkinningPreviousVerticesAddress + instances[body].VertexBase * OutputVertexBytes);
            assert(record.TriangleCount == 1);
        }
        // 2 体目は 1 体目と違う位置を指す（取り違えない）
        assert(records[4].VertexAddress == records[3].VertexAddress + 3 * OutputVertexBytes);
        assert(records[4].PreviousVertexAddress == records[3].PreviousVertexAddress + 3 * OutputVertexBytes);

        ShutdownVisibilityRasterScene(scene);
    }

    // SceneView の配線: On だけが解決のパスを足し、GBufferPass・MegaGeometryPass の描画を止める。
    // Off は従来の GBuffer の描画だけ（予備の経路と同じ）、Debug は今の GBuffer の描画を残して ID の検証表示だけを足す
    void TestSceneViewWiresVisibilityResolveOnlyForOnMode()
    {
        struct ModeExpectation
        {
            VisibilityBufferMode Mode;
            bool bResolve;
            /** @brief 材質のタイル分類のパスがあるか（Off は無い。On・Debug は足し、有効なのは解決を使う On だけ） */
            bool bClassify;
        };
        const ModeExpectation expectations[] = {
            {VisibilityBufferMode::Off, false, false},
            {VisibilityBufferMode::On, true, true},
            {VisibilityBufferMode::Debug, false, true},
        };
        for (const ModeExpectation& expectation : expectations)
        {
            SceneRenderer renderer;
            SceneView sceneView;
            sceneView.SetupDeferredPipeline(&renderer, RasterDirectBrdf::Analytic, expectation.Mode);
            const auto* gbuffer = static_cast<const GBufferPass*>(sceneView.FindPass("GBufferPass"));
            const auto* mega = static_cast<const MegaGeometryPass*>(sceneView.FindPass("MegaGeometryPass"));
            assert(gbuffer != nullptr && mega != nullptr);
            assert(gbuffer->IsVisibilityResolveActive() == expectation.bResolve);
            assert(mega->IsSkipGBufferDraw() == expectation.bResolve);
            assert((sceneView.FindPass("VisibilityResolvePass") != nullptr) == expectation.bResolve);
            // 止める側は、解決のパスへ使えるかを問い合わせる（参照が無いと装置の機能だけの判定になり、パイプラインが作れない
            // 構成でも描画を止めて画面が空になる）。Off・Debug は解決のパスが無いので参照も無い
            const auto* resolvePass = static_cast<const VisibilityResolvePass*>(sceneView.FindPass("VisibilityResolvePass"));
            assert(gbuffer->GetVisibilityResolvePass() == resolvePass);
            assert(mega->GetVisibilityResolvePass() == resolvePass);
            assert((sceneView.FindPass("VisibilityRasterPass") != nullptr) == (expectation.Mode != VisibilityBufferMode::Off));
            // ID のラスタは、解決が使えず予備へ戻るフレームを描かないために解決へ問い合わせる（Debug は解決が無いので問い合わせない）
            const auto* rasterPass = static_cast<const VisibilityRasterPass*>(sceneView.FindPass("VisibilityRasterPass"));
            assert(rasterPass == nullptr || rasterPass->GetResolvePass() == resolvePass);
            // 解決が GBuffer を書く On だけ、MegaGeometry は記録を ID のラスタへ移す（2 パスの遮蔽の HZB を ID の深度から作る）。
            // Off・Debug は GBuffer へ描くので、GBuffer の深度から作る従来の経路のまま
            assert(mega->GetVisibilityRasterPass() == (expectation.bResolve ? rasterPass : nullptr));
            // 計算スキニングも同じ問い合わせで、予備のフレームは変形しない（Off・Debug は解決が無いので問い合わせない）
            const auto* skinningPass = static_cast<const SkinningComputePass*>(sceneView.FindPass("SkinningComputePass"));
            assert(skinningPass != nullptr && skinningPass->GetResolvePass() == resolvePass);

            // 材質のタイル分類: 解決を使う On だけ有効（debug は足しても無効のまま。既定の描画は変えない）。
            // 解決はこの分類を読むので、描画のパスの後・解決の前に並び、解決へ分類が渡される
            const auto* classify = static_cast<const MaterialTileClassifyPass*>(sceneView.FindPass("MaterialTileClassifyPass"));
            assert((classify != nullptr) == expectation.bClassify);
            if (classify != nullptr)
            {
                assert(classify->IsEnabled() == expectation.bResolve);
            }
            if (expectation.bResolve)
            {
                const auto* resolve = static_cast<const VisibilityResolvePass*>(sceneView.FindPass("VisibilityResolvePass"));
                assert(resolve->GetClassifyPass() == classify);
                // 解決の取り出し元（ID のラスタ・計算スキニング）。外すと、解決が何も読めない・スキニングの準備を見られない
                assert(resolve->GetRasterPass() == sceneView.FindPass("VisibilityRasterPass"));
                assert(resolve->GetSkinningComputePass() == sceneView.FindPass("SkinningComputePass"));
                assert(resolve->GetSkinningComputePass() != nullptr);
                int rasterIndex = -1;
                int classifyIndex = -1;
                int resolveIndex = -1;
                for (uint32_t index = 0; index < sceneView.GetPassCount(); ++index)
                {
                    const IViewPass* pass = sceneView.GetPassAt(index);
                    rasterIndex = pass == sceneView.FindPass("VisibilityRasterPass") ? static_cast<int>(index) : rasterIndex;
                    classifyIndex = pass == classify ? static_cast<int>(index) : classifyIndex;
                    resolveIndex = pass == resolve ? static_cast<int>(index) : resolveIndex;
                }
                assert(rasterIndex >= 0 && rasterIndex < classifyIndex && classifyIndex < resolveIndex);
            }
        }
    }

    // 64bit のバッファへの atomicMin（64bit 整数つき）に対応しない装置では、合流の資源（64bit のバッファ・定数・パイプライン）も
    // パスも作らず、ID の描画は合流が無いときと同じ（render pass の数も直接の描画の数も増えない）。
    // 能力の判定（VisibilityMerge::IsSupported）を外す、または能力が無くても合流を記録すると落ちる
    void TestVisibilityMergeAbsentWithoutInt64Atomics()
    {
        VisibilityRasterScene scene;
        scene.bInt64Atomics = false;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(!VisibilityMerge::IsSupported(scene.Device->GetCapabilities()));
        assert(!scene.Raster.GetMerge().IsReady());
        assert(CountBufferCreations(*scene.Device, "VisBuffer_Key64") == 0);
        assert(CountBufferCreations(*scene.Device, "VisBuffer_MergeParams") == 0);
        assert(commandList.Key64Fills.empty());

        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(!stats.bMerged && stats.MergeCount == 0 && stats.KeyBufferBytes == 0);

        // 解決は使え、2 パスの遮蔽の ID・深度の描画は従来どおり（GBuffer・MegaGeometry の遷移 + ID・深度の 2 回）
        assert(scene.Resolve.WasResolved());
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);
        assert(CountDirectDraws(commandList) == SceneIdDirectDraws);

        ShutdownVisibilityRasterScene(scene);
    }

    // --sw-raster=off では、64bit のバッファ・定数・パイプラインも、その埋めと合流のパスも作らず、
    // ID の描画は合流が無いときと同じ（render pass の数も直接の描画の数も増えない）。装置が 64bit に対応していても同じ。
    // SetSwRasterEnabled(false) の判定を外して合流を作ると落ちる
    void TestVisibilityMergeAbsentWhenSwRasterOff()
    {
        VisibilityRasterScene scene;
        scene.bSwRasterMerge = false;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, true, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(VisibilityMerge::IsSupported(scene.Device->GetCapabilities()));
        assert(!scene.Raster.IsSwRasterEnabled());
        assert(!scene.Raster.GetMerge().IsReady());
        assert(CountBufferCreations(*scene.Device, "VisBuffer_Key64") == 0);
        assert(CountBufferCreations(*scene.Device, "VisBuffer_MergeParams") == 0);
        assert(commandList.Key64Fills.empty());

        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(!stats.bMerged && stats.MergeCount == 0 && stats.KeyBufferBytes == 0);

        assert(scene.Resolve.WasResolved());
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);
        assert(CountDirectDraws(commandList) == SceneIdDirectDraws);
        // ソフトの一覧は振り分けを要求していないので作らない
        assert(!scene.Mega.DidSwRasterBin());
        assert(CountBufferCreations(*scene.Device, "MegaGeometry_SwRaster") == 0);
        assert(commandList.SwRasterFills.empty());

        ShutdownVisibilityRasterScene(scene);
    }

    // カリングの定数バッファの、ソフトウェアラスタの 4 語（bSwRasterEnabled・容量・しきい値・近平面）を、パス（0・1）ごとに読む。
    // 場所は cluster_cull.comp の CullUniforms の並び（行列 2 つ・視点・平面 6 つ・語 21 個の後ろ）と同じ
    struct SwRasterUniformWords
    {
        uint32_t bEnabled = 0;
        uint32_t Capacity = 0;
        float MaxPixels = 0.0f;
        float NearPlane = 0.0f;
    };
    constexpr size_t CullUniformSwRasterOffset = (16 + 16 + 4 + 24 + 21) * sizeof(uint32_t);

    SwRasterUniformWords ReadSwRasterUniformWords(size_t updateIndex)
    {
        assert(updateIndex < GMegaCullUniformUpdates.size());
        const Container::VariableArray<uint8_t>& bytes = GMegaCullUniformUpdates[updateIndex];
        assert(bytes.size() >= CullUniformSwRasterOffset + 4 * sizeof(uint32_t));
        SwRasterUniformWords words;
        std::memcpy(&words.bEnabled, bytes.data() + CullUniformSwRasterOffset, sizeof(uint32_t));
        std::memcpy(&words.Capacity, bytes.data() + CullUniformSwRasterOffset + 4, sizeof(uint32_t));
        std::memcpy(&words.MaxPixels, bytes.data() + CullUniformSwRasterOffset + 8, sizeof(float));
        std::memcpy(&words.NearPlane, bytes.data() + CullUniformSwRasterOffset + 12, sizeof(float));
        return words;
    }

    // --sw-raster=on のカリング: 2 パスの遮蔽の構成で、パスごとのソフトの一覧（頭 + 容量）を 1 つ作り、頭を 0 で埋めてから
    // カリングの dispatch を並べ、両パスの定数バッファへ有効・容量・しきい値・近平面を渡す。
    // 振り分けの判定（深度・半径）を外す・頭を埋めない・定数を渡さないと落ちる
    void TestSwRasterBinningWritesListAndUniforms()
    {
        VisibilityRasterScene scene;
        scene.bSwRasterBin = true;
        scene.SwRasterMaxPixels = 6.5f;
        GMegaCullUniformUpdates.clear();
        // スキニングの dispatch が先に並ばないよう、スキニングは足さない（最初の dispatch が MegaGeometry のカリング）
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Mega.IsSwRasterBinningRequested());
        assert(scene.Mega.DidSwRasterBin());
        const uint32_t capacity = scene.Mega.GetSwRasterListCapacity();
        assert(capacity > 0 && capacity <= 65535u);
        assert(CountBufferCreations(*scene.Device, "MegaGeometry_SwRaster") == 1);

        // 頭（パスごとに 4 語 × 2 パス = 8 語）だけを 0 で埋める。カリングの最初の dispatch より前
        assert(commandList.SwRasterFills.size() == 1);
        assert(commandList.SwRasterFills[0].SizeBytes == 8u * sizeof(uint32_t));
        assert(commandList.SwRasterFills[0].Value == 0u);
        size_t firstDispatch = commandList.CallSequence.size();
        for (size_t index = 0; index < commandList.CallSequence.size(); ++index)
        {
            if (commandList.CallSequence[index] == 'D')
            {
                firstDispatch = index;
                break;
            }
        }
        assert(firstDispatch < commandList.CallSequence.size());
        assert(commandList.SwRasterFills[0].SequencePosition <= firstDispatch);

        // 1 パス目・2 パス目の定数バッファ（この場面は BVH を持たないので、各パスで 1 回ずつ）
        assert(GMegaCullUniformUpdates.size() >= 2);
        for (size_t pass = 0; pass < 2; ++pass)
        {
            const SwRasterUniformWords words = ReadSwRasterUniformWords(pass);
            // ID のラスタがソフトウェアラスタを走らせる構成（既定の場面）なので、積めたクラスタのハードのコマンドを空振りにする（2）
            assert(words.bEnabled == 2u);
            assert(words.Capacity == capacity);
            assert(words.MaxPixels == 6.5f);
            assert(words.NearPlane == scene.Camera.NearPlane);
        }

        ShutdownVisibilityRasterScene(scene);
    }

    // 振り分けを使えない構成では、振り分けず、ソフトの一覧も作らず、定数バッファは無効（0）を渡す（SW_RASTER_FALLBACK を 1 回出す）。
    // 64bit アトミックが無い装置・ビジビリティバッファが無い（描画の写しを作らない）構成・遮蔽の判定が 1 回の構成の 3 つ。
    // 判定（IsSupported・sink の有無・2 パス）のどれかを外すと、その構成で振り分けてしまい落ちる
    void TestSwRasterBinningFallsBackWhenUnavailable()
    {
        struct Case
        {
            const char* Name;
            bool bInt64Atomics;
            bool bVisibilityPlan;
            bool bOcclusionCulling;
        };
        const Case cases[] = {
            {"int64_atomics_unsupported", false, true, true},
            {"visibility_buffer_off", true, false, true},
            {"occlusion_off", true, true, false},
        };
        for (const Case& testCase : cases)
        {
            VisibilityRasterScene scene;
            scene.bSwRasterBin = true;
            scene.bInt64Atomics = testCase.bInt64Atomics;
            GMegaCullUniformUpdates.clear();
            RunVisibilityRasterScene(scene, testCase.bVisibilityPlan, testCase.bOcclusionCulling, ClassifyMode::None,
                                     testCase.bVisibilityPlan, true,
                                     testCase.bVisibilityPlan ? ResolveMode::Supported : ResolveMode::None);
            assert(scene.Mega.IsSwRasterBinningRequested());
            assert(!scene.Mega.DidSwRasterBin());
            assert(scene.Mega.GetSwRasterListCapacity() == 0);
            assert(CountBufferCreations(*scene.Device, "MegaGeometry_SwRaster") == 0);
            assert(scene.CommandList.SwRasterFills.empty());
            assert(!GMegaCullUniformUpdates.empty());
            for (size_t update = 0; update < GMegaCullUniformUpdates.size(); ++update)
            {
                const SwRasterUniformWords words = ReadSwRasterUniformWords(update);
                assert(words.bEnabled == 0u && words.Capacity == 0u);
            }
            ShutdownVisibilityRasterScene(scene);
        }
    }

    // 間接 dispatch の x の上限（Vulkan の保証する最小値。一覧がこれを超えると y へ折り返す）
    constexpr uint32_t SwRasterOneDimensionLimit = 65535u;

    // ソフトの一覧（MegaGeometry_SwRaster）を引数にする間接 dispatch（ソフトウェアラスタの dispatch）の、CallSequence での位置（呼ばれた順）
    Container::VariableArray<size_t> FindSwRasterDispatchPositions(const FakeCommandList& commandList)
    {
        Container::VariableArray<size_t> positions;
        size_t indirectIndex = 0;
        for (size_t index = 0; index < commandList.CallSequence.size(); ++index)
        {
            if (commandList.CallSequence[index] != 'J')
            {
                continue;
            }
            assert(indirectIndex < commandList.IndirectDispatches.size());
            if (IsDebugName(commandList.IndirectDispatches[indirectIndex].BufferName, "MegaGeometry_SwRaster"))
            {
                positions.push_back(index);
            }
            ++indirectIndex;
        }
        return positions;
    }

    // 名前のバッファへのバリアを、記録した順に集める
    Container::VariableArray<BarrierEvent> CollectBufferBarriers(const FakeCommandList& commandList, const char* debugName)
    {
        Container::VariableArray<BarrierEvent> barriers;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.Buffer != nullptr &&
                IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, debugName))
            {
                barriers.push_back(barrier);
            }
        }
        return barriers;
    }

    // --sw-raster=on で ID のラスタがソフトウェアラスタを走らせる構成（2 パスの遮蔽・解決あり・64bit のバッファあり）の記録:
    //  - カリングの定数は両パスとも 2（積めたクラスタのハードのコマンドを空振りにする）で、容量を渡す。
    //  - ソフトの間接 dispatch は 1 パス目・2 パス目に 1 回ずつ（引数は一覧のパスごとの頭）。1 パス目は、記録を書く計算の直後・
    //    HZB の前の合流の直前、2 パス目は、2 パス目の描画（render pass）の後・合流の直前。
    //  - 64bit のバッファは dispatch の直前に UnorderedAccess、直後に GenericRead（合流が読む）。一覧は直前に GenericRead、直後に
    //    UnorderedAccess（次のカリング・最後の Common への戻しが続く）。1 パス目の後は、2 パス目のカリングが書くコマンド・カウンタ・
    //    描画情報を UnorderedAccess へ戻す。
    // dispatch を外す・バリアを外す・定数を 1 にする・記録を書く計算の前へ dispatch を移す（順序の入れ替え）のどれでも落ちる
    void TestSwRasterDispatchesBetweenRecordsAndMerges()
    {
        VisibilityRasterScene scene;
        scene.bSwRasterBin = true;
        GMegaCullUniformUpdates.clear();
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::Supported);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Mega.DidSwRasterBin());
        assert(scene.Raster.GetSwRaster().IsReady());
        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.SwRasterDispatchCount == 2);
        assert(stats.MergeCount == 2);

        // 両パスのカリングの定数: 2（ソフトが走る）と一覧の容量
        const uint32_t capacity = scene.Mega.GetSwRasterListCapacity();
        assert(capacity > 0);
        assert(GMegaCullUniformUpdates.size() >= 2);
        for (size_t pass = 0; pass < 2; ++pass)
        {
            const SwRasterUniformWords words = ReadSwRasterUniformWords(pass);
            assert(words.bEnabled == 2u);
            assert(words.Capacity == capacity);
        }

        // ソフトの間接 dispatch は 1 パスにつき 1 回で、引数は一覧のパスごとの頭（1 パス 4 語 = 16 バイト）
        const Container::VariableArray<size_t> swPositions = FindSwRasterDispatchPositions(commandList);
        assert(swPositions.size() == 2);
        uint32_t swIndirectCount = 0;
        for (const FakeCommandList::IndirectDispatchRecord& record : commandList.IndirectDispatches)
        {
            if (IsDebugName(record.BufferName, "MegaGeometry_SwRaster"))
            {
                assert(record.OffsetBytes == static_cast<uint64_t>(swIndirectCount) * 4u * sizeof(uint32_t));
                ++swIndirectCount;
            }
        }
        assert(swIndirectCount == 2);

        // 並び（D = 直接 dispatch、J = 間接 dispatch、B・E = render pass の開始・終了）:
        //   1 パス目: D（記録の引数）→ J（記録）→ J（ソフト）→ B・E（HZB の前の合流）
        //   2 パス目: J（記録。1 パス目と 2 パス目の間にこの 1 回だけ）→ B … E（2 パス目の描画）→ J（ソフト）→ B・E（合流）
        const Container::VariableArray<char>& sequence = commandList.CallSequence;
        const size_t sw0 = swPositions[0];
        const size_t sw1 = swPositions[1];
        assert(sw0 >= 2 && sequence[sw0 - 1] == 'J' && sequence[sw0 - 2] == 'D');
        assert(sw0 + 2 < sequence.size() && sequence[sw0 + 1] == 'B' && sequence[sw0 + 2] == 'E');
        assert(sw1 > sw0 + 2 && sequence[sw1 - 1] == 'E');
        assert(sw1 + 2 < sequence.size() && sequence[sw1 + 1] == 'B' && sequence[sw1 + 2] == 'E');
        uint32_t indirectBetween = 0;
        for (size_t index = sw0 + 1; index < sw1; ++index)
        {
            if (sequence[index] == 'J')
            {
                ++indirectBetween;
            }
        }
        assert(indirectBetween == 1);

        // 64bit のバッファ: 埋め（Common → CopyDest → GenericRead）の後、dispatch ごとに GenericRead → UnorderedAccess → GenericRead
        const Container::VariableArray<BarrierEvent> keyBarriers = CollectBufferBarriers(commandList, "VisBuffer_Key64");
        assert(keyBarriers.size() == 6);
        assert(keyBarriers[0].BeforeState == RHI::ResourceState::Common && keyBarriers[0].AfterState == RHI::ResourceState::CopyDest);
        assert(keyBarriers[1].BeforeState == RHI::ResourceState::CopyDest && keyBarriers[1].AfterState == RHI::ResourceState::GenericRead);
        for (size_t pass = 0; pass < 2; ++pass)
        {
            const BarrierEvent& begin = keyBarriers[2 + pass * 2];
            const BarrierEvent& end = keyBarriers[3 + pass * 2];
            assert(begin.BeforeState == RHI::ResourceState::GenericRead && begin.AfterState == RHI::ResourceState::UnorderedAccess);
            assert(begin.SequencePosition == swPositions[pass]);
            assert(end.BeforeState == RHI::ResourceState::UnorderedAccess && end.AfterState == RHI::ResourceState::GenericRead);
            assert(end.SequencePosition == swPositions[pass] + 1);
        }

        // 一覧: dispatch ごとに UnorderedAccess → GenericRead → UnorderedAccess（GenericRead を含むバリアはこの 4 つだけ）。
        // 最後は次のフレーム用の Common への戻し（MegaGeometryPass が持つ形のまま）
        const Container::VariableArray<BarrierEvent> listBarriers = CollectBufferBarriers(commandList, "MegaGeometry_SwRaster");
        Container::VariableArray<BarrierEvent> listReadBarriers;
        for (const BarrierEvent& barrier : listBarriers)
        {
            if (barrier.BeforeState == RHI::ResourceState::GenericRead || barrier.AfterState == RHI::ResourceState::GenericRead)
            {
                listReadBarriers.push_back(barrier);
            }
        }
        assert(listReadBarriers.size() == 4);
        for (size_t pass = 0; pass < 2; ++pass)
        {
            const BarrierEvent& toRead = listReadBarriers[pass * 2];
            const BarrierEvent& toWrite = listReadBarriers[pass * 2 + 1];
            assert(toRead.BeforeState == RHI::ResourceState::UnorderedAccess && toRead.AfterState == RHI::ResourceState::GenericRead);
            assert(toRead.SequencePosition == swPositions[pass]);
            assert(toWrite.BeforeState == RHI::ResourceState::GenericRead && toWrite.AfterState == RHI::ResourceState::UnorderedAccess);
            assert(toWrite.SequencePosition == swPositions[pass] + 1);
        }
        assert(!listBarriers.empty());
        assert(listBarriers[listBarriers.size() - 1].BeforeState == RHI::ResourceState::UnorderedAccess);
        assert(listBarriers[listBarriers.size() - 1].AfterState == RHI::ResourceState::Common);
        assert(listBarriers[listBarriers.size() - 1].SequencePosition > sw1 + 1);

        // 1 パス目の記録が読んだコマンド・カウンタ・描画情報を、2 パス目のカリングが書く前に UnorderedAccess へ戻す（1 回ずつ。1 パス目と 2 パス目の間）
        const char* const cullOutputs[] = {"MegaGeometry_IndirectDraw", "MegaGeometry_DrawCount", "MegaGeometry_DrawInfo"};
        for (const char* name : cullOutputs)
        {
            uint32_t restoreCount = 0;
            for (const BarrierEvent& barrier : CollectBufferBarriers(commandList, name))
            {
                if (barrier.BeforeState == RHI::ResourceState::GenericRead && barrier.AfterState == RHI::ResourceState::UnorderedAccess)
                {
                    assert(barrier.SequencePosition > sw0 && barrier.SequencePosition < sw1);
                    ++restoreCount;
                }
            }
            assert(restoreCount == 1);
        }

        // ハードの描画は従来どおり記録する（空振りにするのは GPU のカリングが書くコマンドの instanceCount で、描画の呼び出しの数は変わらない）
        assert(commandList.IndirectDraws.size() == 2);

        ShutdownVisibilityRasterScene(scene);
    }

    // ソフトウェアラスタを使えない構成では、ソフトの dispatch を記録せず、カリングの定数は 1 以下（ソフトが走らないので、
    // ハードのコマンドを空振りにしない）。Debug（GBuffer が先に MegaGeometry を描くので ID のラスタが記録を駆動しない）・
    // 64bit アトミックが無い装置・合流が無い（--sw-raster の ID のラスタ側が無効）・ソフトの計算パイプラインが作れない・ワイヤーフレーム
    // （64bit のバッファを誰も書かない表示）・ソフトの dispatch の定数バッファが作れない（初期化の後の資源の作成の失敗）の 6 つ。
    // 使えるかの問い合わせ（IsSwRasterAvailable）を外す・定数を常に 2 にする・資源をカリングの前に確保しないと、その構成で空振りにして落ちる
    void TestSwRasterDispatchAbsentWhenUnavailable()
    {
        struct Case
        {
            const char* Name;
            bool bInt64Atomics;
            bool bSwRasterMerge;
            uint32_t FailComputePipelineNumber;
            DebugViewMode DebugMode;
            ResolveMode Resolve;
            bool bExpectBinned;
            const char* FailBufferDebugName;
        };
        const Case cases[] = {
            {"debug", true, true, 0, DebugViewMode::Normal, ResolveMode::None, false, nullptr},
            {"int64_atomics_unsupported", false, true, 0, DebugViewMode::Normal, ResolveMode::Supported, false, nullptr},
            {"merge_unavailable", true, false, 0, DebugViewMode::Normal, ResolveMode::Supported, true, nullptr},
            {"sw_pipeline_unavailable", true, true, 3, DebugViewMode::Normal, ResolveMode::Supported, true, nullptr},
            {"wireframe", true, true, 0, DebugViewMode::Wireframe, ResolveMode::Supported, true, nullptr},
            {"sw_resources_unavailable", true, true, 0, DebugViewMode::Normal, ResolveMode::Supported, true, "VisBuffer_SwRasterParams"},
        };
        for (const Case& testCase : cases)
        {
            VisibilityRasterScene scene;
            scene.bSwRasterBin = true;
            scene.bInt64Atomics = testCase.bInt64Atomics;
            scene.bSwRasterMerge = testCase.bSwRasterMerge;
            scene.FailRasterComputePipelineNumber = testCase.FailComputePipelineNumber;
            scene.FailBufferDebugName = testCase.FailBufferDebugName;
            scene.DebugMode = testCase.DebugMode;
            GMegaCullUniformUpdates.clear();
            RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, testCase.Resolve);

            assert(scene.Mega.DidSwRasterBin() == testCase.bExpectBinned);
            assert(scene.Raster.GetLastFrameStats().SwRasterDispatchCount == 0);
            assert(FindSwRasterDispatchPositions(scene.CommandList).empty());
            assert(!GMegaCullUniformUpdates.empty());
            for (size_t update = 0; update < GMegaCullUniformUpdates.size(); ++update)
            {
                assert(ReadSwRasterUniformWords(update).bEnabled <= 1u);
            }
            if (testCase.bExpectBinned)
            {
                // 一覧へ積むだけ（統計用）で、ハードがすべてのクラスタを描く
                assert(ReadSwRasterUniformWords(0).bEnabled == 1u);
            }
            if (testCase.FailComputePipelineNumber == 3)
            {
                // 失敗したのがソフトの計算だけであること（合流は作れている）
                assert(scene.Raster.GetMerge().IsReady() && !scene.Raster.GetSwRaster().IsReady());
            }
            ShutdownVisibilityRasterScene(scene);
        }
    }

    // bInt64Atomics は、64bit のバッファへの atomicMin に対応する装置か（BDA は常に無い）
    void RunSwRasterAbsentWithoutBufferDeviceAddress(bool bInt64Atomics)
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        SwRasterBdaFallbackCounter fallbackLogs;
        logger.AddSink(&fallbackLogs);
#endif

        VisibilityRasterScene scene;
        scene.bInt64Atomics = bInt64Atomics;
        scene.bBufferDeviceAddress = false;
        scene.bSwRasterBin = true;
        GMegaCullUniformUpdates.clear();
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::UnsupportedDevice);
        FakeCommandList& commandList = scene.CommandList;

        // BDA が無いのでソフトウェアラスタも合流も作らない（64bit のバッファへの atomicMin に対応していても）
        assert(VisibilityMerge::IsSupported(scene.Device->GetCapabilities()) == bInt64Atomics);
        assert(!VisibilitySwRaster::IsSupported(scene.Device->GetCapabilities()));
        assert(scene.Raster.IsSwRasterEnabled());
        assert(!scene.Raster.GetMerge().IsReady());
        assert(!scene.Raster.GetSwRaster().IsReady());
        assert(CountBufferCreations(*scene.Device, "VisBuffer_Key64") == 0);
        assert(CountBufferCreations(*scene.Device, "VisBuffer_MergeParams") == 0);
        assert(commandList.Key64Fills.empty());
        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(!stats.bMerged && stats.MergeCount == 0 && stats.KeyBufferBytes == 0);
        assert(!scene.Resolve.WasResolved());

        // 振り分けず、ソフトの一覧も頭の埋めも dispatch も無く、カリングの定数は無効（0）
        assert(scene.Mega.IsSwRasterBinningRequested());
        assert(!scene.Mega.DidSwRasterBin());
        assert(scene.Mega.GetSwRasterListCapacity() == 0);
        assert(CountBufferCreations(*scene.Device, "MegaGeometry_SwRaster") == 0);
        assert(commandList.SwRasterFills.empty());
        assert(stats.SwRasterDispatchCount == 0);
        assert(FindSwRasterDispatchPositions(commandList).empty());
        assert(!GMegaCullUniformUpdates.empty());
        for (size_t update = 0; update < GMegaCullUniformUpdates.size(); ++update)
        {
            const SwRasterUniformWords words = ReadSwRasterUniformWords(update);
            assert(words.bEnabled == 0u && words.Capacity == 0u);
        }

        ShutdownVisibilityRasterScene(scene);

#if NORVES_ENABLE_LOGGING
        // 出す側は ID のラスタ（初期化で 1 回）だけで、フレームを描いても増えない
        assert(fallbackLogs.Count == 1);
        logger.RemoveSink(&fallbackLogs);
        logger.Shutdown();
#endif
    }

    // バッファのアドレス（BDA）に対応しない装置では、ソフトウェアラスタ（VisibilitySwRaster）が使えないので、--sw-raster=on でも
    // 64bit のバッファ・定数・パイプライン・埋め・合流のパスを作らず、振り分けず（ソフトの一覧も作らない）、
    // カリングの定数は無効（0）、ソフトの dispatch も記録しない。ID の描画は合流が無いときと同じ。
    // 解決（頂点のアドレスを引く）も使えないので、ID のラスタは予備の GBuffer の描画へ戻って何も描かず、MegaGeometry も振り分けない
    // （sink が無い）。この場面の FakeDevice は bShaderStorageImageExtendedFormats を持たず解決が使えないので、フレームの資源・埋め・
    // 合流のパスは BDA の有無によらず元から生じない。ID のラスタの合流の作成を VisibilitySwRaster::IsSupported でなく
    // VisibilityMerge::IsSupported のままにしたときに落ちうるのは、合流の IsReady の assert だけ。
    // 理由 bda_unsupported のログ（SW_RASTER_FALLBACK）は、64bit アトミックの有無によらず 1 回だけ出る
    // （どちらも無い装置でも出る。出す条件から VisibilityMerge::IsSupported を外した形が正しい）
    void TestSwRasterAbsentWithoutBufferDeviceAddress()
    {
        for (int variant = 0; variant < 2; ++variant)
        {
            RunSwRasterAbsentWithoutBufferDeviceAddress(variant == 0);
        }
    }

    // 振り分けのしきい値（画素）から、1 スレッドが走査する矩形の一辺の上限（画素）を決める式 max(64, ceil(2r) + 2)。
    // 下限（64）に張り付く範囲（しきい値 0・31 以下）、下限を 1 つ超える最初の値（31.5 → 65）、既定より大きい値（64 → 130）、
    // 負・NaN（下限）、巨大な値（4096 で頭打ち。8194）を確かめる。係数や下限を変える・丸め方を変えると落ちる
    void TestSwRasterMaxScanSpanFollowsThreshold()
    {
        struct Case
        {
            float MaxPixels;
            uint32_t Expected;
        };
        const Case cases[] = {
            {0.0f, 64u},
            {8.0f, 64u},
            {31.0f, 64u},
            {31.5f, 65u},
            {32.0f, 66u},
            {64.0f, 130u},
            {-5.0f, 64u},
            {std::numeric_limits<float>::quiet_NaN(), 64u},
            {4096.0f, 8194u},
            {1.0e9f, 8194u},
        };
        for (const Case& testCase : cases)
        {
            assert(VisibilitySwRaster::ComputeMaxScanSpan(testCase.MaxPixels) == testCase.Expected);
        }
        static_assert(VisibilitySwRaster::MinScanSpan == 64u, "下限は 64 画素");
    }

    // 振り分けのしきい値がソフトの矩形の上限まで届く: MegaGeometryPass が VisibilityDrawPlan へしきい値を入れ、VisibilityRasterPass が
    // それを VisibilitySwRaster の定数（SwRasterParams の flags.y = 5 組目の 2 語目 = 68 バイト目）へ渡す。
    // 既定の 8 は下限（64）に張り付いて値が見えないので、下限を超える 40（82）と 100（202）で確かめる。
    // MegaGeometryPass が plan へしきい値を入れる行を外す・VisibilityRasterPass が plan の値を入力へ渡さないと、上限が 64 に戻って落ちる
    void TestSwRasterThresholdReachesScanSpanConstant()
    {
        constexpr size_t ScanSpanOffset = (4 + 4 + 4 + 4 + 1) * sizeof(uint32_t);
        const float thresholds[] = {40.0f, 100.0f};
        for (const float threshold : thresholds)
        {
            const uint32_t expectedSpan = VisibilitySwRaster::ComputeMaxScanSpan(threshold);
            assert(expectedSpan > VisibilitySwRaster::MinScanSpan);

            VisibilityRasterScene scene;
            scene.bSwRasterBin = true;
            scene.SwRasterMaxPixels = threshold;
            GSwRasterParamsUpdates.clear();
            RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::Supported);

            assert(scene.Mega.DidSwRasterBin());
            assert(scene.Raster.GetLastFrameStats().SwRasterDispatchCount == 2);
            assert(GSwRasterParamsUpdates.size() == 2);
            for (const Container::VariableArray<uint8_t>& bytes : GSwRasterParamsUpdates)
            {
                assert(bytes.size() >= ScanSpanOffset + sizeof(uint32_t));
                uint32_t span = 0;
                std::memcpy(&span, bytes.data() + ScanSpanOffset, sizeof(uint32_t));
                assert(span == expectedSpan);
            }
            ShutdownVisibilityRasterScene(scene);
        }
    }

    // 一覧の容量はパスごとのコマンド数まで（65535 で頭打ちにしない）。間接 dispatch の x の上限（65535）を超えるクラスタ数でも一覧は
    // 全部収まり、間接 dispatch を断るコマンドリストでは (65535, ceil(容量 / 65535), 1) の直接 dispatch で走らせる
    // （GPU のカリングは間接 dispatch の引数を同じ 2 次元の形で書く）。容量を 65535 に抑える・直接 dispatch の y を 1 にすると落ちる
    void TestSwRasterListCapacityExceedsOneDimension()
    {
        constexpr uint32_t clusterCountA = 70000;
        for (int variant = 0; variant < 2; ++variant)
        {
            const bool bRejectIndirect = variant == 1;
            VisibilityRasterScene scene;
            scene.bSwRasterBin = true;
            scene.ClusterCountA = clusterCountA;
            scene.CommandList.bRejectDispatchIndirect = bRejectIndirect;
            GMegaCullUniformUpdates.clear();
            RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::Supported);
            FakeCommandList& commandList = scene.CommandList;

            // メッシュ A（70000）と B（1）のクラスタ数の合計がパスごとのコマンド数（MaxDrawCount の範囲内）
            const uint32_t capacity = scene.Mega.GetSwRasterListCapacity();
            assert(scene.Mega.DidSwRasterBin());
            assert(capacity == clusterCountA + 1u);
            assert(capacity > SwRasterOneDimensionLimit);
            assert(GMegaCullUniformUpdates.size() >= 2);
            for (size_t pass = 0; pass < 2; ++pass)
            {
                const SwRasterUniformWords words = ReadSwRasterUniformWords(pass);
                assert(words.bEnabled == 2u && words.Capacity == capacity);
            }

            // 一覧のバッファは、頭（8 語）とパスごとの容量 2 つぶんの語を収める
            uint64_t listBytes = 0;
            for (const BufferCreationRecord& record : scene.Device->CreatedBuffers)
            {
                if (IsDebugName(record.Desc.DebugName, "MegaGeometry_SwRaster"))
                {
                    listBytes = record.Desc.Size;
                }
            }
            assert(listBytes >= (8u + 2ull * capacity) * sizeof(uint32_t));

            assert(scene.Raster.GetLastFrameStats().SwRasterDispatchCount == 2);
            const Container::VariableArray<size_t> swPositions = FindSwRasterDispatchPositions(commandList);
            if (bRejectIndirect)
            {
                assert(swPositions.empty());
                assert(scene.Raster.GetSwRaster().GetDirectFallbackCount() == 2);
                uint32_t twoDimensional = 0;
                for (const FakeCommandList::DispatchSize& size : commandList.DispatchGroups)
                {
                    if (size.X == SwRasterOneDimensionLimit && size.Y == (capacity + SwRasterOneDimensionLimit - 1u) / SwRasterOneDimensionLimit &&
                        size.Z == 1u)
                    {
                        ++twoDimensional;
                    }
                }
                assert(twoDimensional == 2);
            }
            else
            {
                assert(swPositions.size() == 2);
                assert(scene.Raster.GetSwRaster().GetDirectFallbackCount() == 0);
            }
            ShutdownVisibilityRasterScene(scene);
        }
    }

    // SceneView の配線: --sw-raster=on は、ビジビリティバッファが On（材質の解決を使う）のときだけ ID のラスタの 64bit の資源を有効にし、
    // どのモードでも MegaGeometry へ要求としきい値を渡す（使えない理由は MegaGeometry が SW_RASTER_FALLBACK へ出す）。
    // Debug は GBuffer が先に MegaGeometry を描くのでソフトに回せず、64bit の資源・埋め・合流を作らない。Off は両方とも無効のまま
    void TestSceneViewWiresSwRasterMode()
    {
        struct Expectation
        {
            VisibilityBufferMode Visibility;
            SwRasterMode SwRaster;
            bool bRasterEnabled;
            bool bBinRequested;
        };
        const Expectation expectations[] = {
            {VisibilityBufferMode::On, SwRasterMode::Off, false, false},
            {VisibilityBufferMode::On, SwRasterMode::On, true, true},
            {VisibilityBufferMode::Debug, SwRasterMode::On, false, true},
            {VisibilityBufferMode::Off, SwRasterMode::On, false, true},
            {VisibilityBufferMode::Off, SwRasterMode::Off, false, false},
        };
        for (const Expectation& expectation : expectations)
        {
            SceneRenderer renderer;
            SceneView sceneView;
            sceneView.SetupDeferredPipeline(&renderer, RasterDirectBrdf::Analytic, expectation.Visibility, expectation.SwRaster, 12.0f);
            const auto* mega = static_cast<const MegaGeometryPass*>(sceneView.FindPass("MegaGeometryPass"));
            const auto* raster = static_cast<const VisibilityRasterPass*>(sceneView.FindPass("VisibilityRasterPass"));
            assert(mega != nullptr);
            assert(mega->IsSwRasterBinningRequested() == expectation.bBinRequested);
            assert((raster != nullptr && raster->IsSwRasterEnabled()) == expectation.bRasterEnabled);
        }

        // 引数を渡さない呼び出し（既存の呼び出しと同じ）は On（既定）。--sw-raster=off だけが両方を無効にする
        SceneRenderer renderer;
        SceneView sceneView;
        sceneView.SetupDeferredPipeline(&renderer);
        const auto* mega = static_cast<const MegaGeometryPass*>(sceneView.FindPass("MegaGeometryPass"));
        const auto* raster = static_cast<const VisibilityRasterPass*>(sceneView.FindPass("VisibilityRasterPass"));
        assert(mega != nullptr && mega->IsSwRasterBinningRequested());
        assert(raster != nullptr && raster->IsSwRasterEnabled());
    }

    // SceneView に渡した振り分けのしきい値は、MegaGeometryPass が持ち、カリングの定数バッファ（SwRasterMaxPixels）へ届く。
    // SceneView が既定の値（DefaultSwRasterMaxPixels）やしきい値を渡さない呼び出しにすると、MegaGeometryPass の値が 12 にならず、定数も 12 にならず落ちる。
    // SceneView が組んだ MegaGeometryPass の設定（要求・しきい値）を、カリングを実際に記録する場面へ移して、定数の語を読む
    void TestSceneViewThresholdReachesCullUniform()
    {
        constexpr float Threshold = 12.0f;
        static_assert(Threshold != DefaultSwRasterMaxPixels, "既定の値と違うしきい値で確かめる");
        SceneRenderer renderer;
        SceneView sceneView;
        sceneView.SetupDeferredPipeline(&renderer, RasterDirectBrdf::Analytic, VisibilityBufferMode::On, SwRasterMode::On, Threshold);
        const auto* viewMega = static_cast<const MegaGeometryPass*>(sceneView.FindPass("MegaGeometryPass"));
        assert(viewMega != nullptr);
        assert(viewMega->IsSwRasterBinningRequested());
        assert(viewMega->GetSwRasterMaxPixels() == Threshold);

        VisibilityRasterScene scene;
        scene.bSwRasterBin = viewMega->IsSwRasterBinningRequested();
        scene.SwRasterMaxPixels = viewMega->GetSwRasterMaxPixels();
        GMegaCullUniformUpdates.clear();
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::None, false, true, ResolveMode::Supported);
        assert(scene.Mega.DidSwRasterBin());
        assert(GMegaCullUniformUpdates.size() >= 2);
        for (size_t pass = 0; pass < 2; ++pass)
        {
            assert(ReadSwRasterUniformWords(pass).MaxPixels == Threshold);
        }
        ShutdownVisibilityRasterScene(scene);

        // 引数を渡さない呼び出しは既定のしきい値（DefaultSwRasterMaxPixels）
        SceneRenderer defaultRenderer;
        SceneView defaultView;
        defaultView.SetupDeferredPipeline(&defaultRenderer, RasterDirectBrdf::Analytic, VisibilityBufferMode::On, SwRasterMode::On);
        const auto* defaultMega = static_cast<const MegaGeometryPass*>(defaultView.FindPass("MegaGeometryPass"));
        assert(defaultMega != nullptr && defaultMega->GetSwRasterMaxPixels() == DefaultSwRasterMaxPixels);
    }

#if NORVES_ENABLE_STATS
    // 影の標本（--shadow-probe）: SceneView は既定では標本のパスを持たず、足すと照明の後に並ぶ
    void TestShadowProbeAbsentWithoutOptionAndAfterLightingWhenEnabled()
    {
        SceneRenderer renderer;
        SceneView offView;
        assert(!offView.IsShadowProbeEnabled());
        offView.SetupDeferredPipeline(&renderer);
        assert(offView.FindPass("ShadowProbePass") == nullptr);

        SceneRenderer onRenderer;
        SceneView onView;
        onView.SetShadowProbeEnabled(true);
        onView.SetupDeferredPipeline(&onRenderer);
        const IViewPass* probe = onView.FindPass("ShadowProbePass");
        const IViewPass* lighting = onView.FindPass("LightingPass");
        assert(probe != nullptr && lighting != nullptr);
        int probeIndex = -1;
        int lightingIndex = -1;
        for (uint32_t index = 0; index < onView.GetPassCount(); ++index)
        {
            probeIndex = onView.GetPassAt(index) == probe ? static_cast<int>(index) : probeIndex;
            lightingIndex = onView.GetPassAt(index) == lighting ? static_cast<int>(index) : lightingIndex;
        }
        assert(lightingIndex >= 0 && lightingIndex < probeIndex);
        // 有効にしても、足すのは標本のパス 1 つだけ（ほかのパスの数・順は変えない）
        assert(onView.GetPassCount() == offView.GetPassCount() + 1);
    }

    // 影の標本のテストで、GBuffer の深度・法線・影の地図・シーンの色を書くだけのパス（照明の代わり）
    class ShadowProbeInputsPass final : public IRenderGraphPass
    {
    public:
        explicit ShadowProbeInputsPass(bool bWriteShadowMap = true)
            : m_bWriteShadowMap(bWriteShadowMap)
        {
        }
        const char* GetName() const override { return "ShadowProbeInputsPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            Depth = builder.WriteTexture(RenderGraphResourceNames::GBufferDepth,
                                         RGTextureDesc::RenderTarget(128, 64, RHI::Format::R32_FLOAT, "Test_Depth"),
                                         RHI::ResourceState::RenderTarget,
                                         RHI::ResourceState::ShaderResource);
            Normal = builder.WriteTexture(RenderGraphResourceNames::GBufferNormal,
                                          RGTextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "Test_Normal"),
                                          RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            SceneColor = builder.WriteTexture(RenderGraphResourceNames::SceneColor,
                                              RGTextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "Test_SceneColor"),
                                              RHI::ResourceState::RenderTarget,
                                              RHI::ResourceState::ShaderResource);
            if (m_bWriteShadowMap)
            {
                RGTextureDesc shadowDesc = RGTextureDesc::RenderTarget(64, 64, RHI::Format::R32_FLOAT, "Test_ShadowMap");
                shadowDesc.ArraySize = 4;
                ShadowMap = builder.WriteTexture(RenderGraphResourceNames::ShadowMap,
                                                 shadowDesc,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource);
            }
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        RGTextureHandle Depth;
        RGTextureHandle Normal;
        RGTextureHandle SceneColor;
        RGTextureHandle ShadowMap;

    private:
        bool m_bWriteShadowMap = true;
    };

    // 標本のパスは、照明が書く Scene.Color・GBuffer の深度と法線・CSM の影の地図を読む（RenderGraph の依存で照明の後になる）。
    // 影の地図が無い構成では何も宣言せず、何も測らない
    void TestShadowProbeReadsCsmResourcesAfterLighting()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));
        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        ShadowProbePass probe;
        assert(probe.Initialize(context));
        {
            ShadowProbeInputsPass inputs;
            RenderGraph graph;
            assert(graph.Initialize(nullptr));
            const uint32_t inputsIndex = graph.AddPass(&inputs);
            const uint32_t probeIndex = graph.AddPass(&probe);
            assert(graph.Compile(context));
            const Container::VariableArray<uint32_t>& order = graph.GetCompiledPassOrder();
            assert(order.size() == 2 && order[0] == inputsIndex && order[1] == probeIndex);

            bool bReadsShadowMap = false;
            bool bReadsDepth = false;
            bool bReadsNormal = false;
            bool bReadsSceneColor = false;
            const uint32_t accessCount = graph.GetDeclaredPassAccessCount(probeIndex);
            assert(accessCount == 4);
            for (uint32_t access = 0; access < accessCount; ++access)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Write;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(probeIndex, access, resource, mode, state, finalState));
                assert(mode == RGAccessMode::Read && state == RHI::ResourceState::ShaderResource);
                bReadsShadowMap = bReadsShadowMap || resource == inputs.ShadowMap.ToResourceHandle();
                bReadsDepth = bReadsDepth || resource == inputs.Depth.ToResourceHandle();
                bReadsNormal = bReadsNormal || resource == inputs.Normal.ToResourceHandle();
                bReadsSceneColor = bReadsSceneColor || resource == inputs.SceneColor.ToResourceHandle();
            }
            assert(bReadsShadowMap && bReadsDepth && bReadsNormal && bReadsSceneColor);
        }
        {
            // 影の地図を書くパスが無い構成: 何も宣言しない
            ShadowProbeInputsPass inputs(false);
            RenderGraph graph;
            assert(graph.Initialize(nullptr));
            graph.AddPass(&inputs);
            const uint32_t probeIndex = graph.AddPass(&probe);
            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(probeIndex) == 0);
        }
        probe.Shutdown();
        shaderManager.Shutdown();
    }

    uint32_t ReadShadowProbeParamWord(const Container::VariableArray<uint8_t>& bytes, size_t byteOffset)
    {
        assert(byteOffset + sizeof(uint32_t) <= bytes.size());
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + byteOffset, sizeof(value));
        return value;
    }

    // 影の標本の実行の一式。決定的な撮影の形（エポックの最初のフレームで標本を固定し、以後は測る）か、
    // そうでない形（起動から一定の実行の後に固定）で毎フレーム RenderGraph を回し、統計の読み戻しは GPU の代わりにテストが置く
    struct ShadowProbeRun
    {
        RHI::TSharedPtr<FakeDevice> Device = RHI::MakeShared<FakeDevice>();
        ShaderManager ShaderMgr;
        MockAllocator Allocator;
        RHI::TransientResourcePool Pool;
        FakeCommandList CommandList;
        CameraProxy Camera;
        ViewRenderContext Context;
        ShadowProbeInputsPass Inputs;
        ShadowProbePass Probe;
        RenderGraph Graph;
    };

    void InitializeShadowProbeRun(ShadowProbeRun& run, bool bDeterministic)
    {
        assert(run.ShaderMgr.Initialize(run.Device.get(), TestShaderDirectory));
        assert(run.Pool.Initialize(&run.Allocator, 1));
        run.Camera.Viewport.Width = 128.0f;
        run.Camera.Viewport.Height = 64.0f;
        ViewRenderContext& context = run.Context;
        context.Device = run.Device.get();
        context.CommandList = &run.CommandList;
        context.ShaderMgr = &run.ShaderMgr;
        context.TransientPool = &run.Pool;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.MainCamera = &run.Camera;
        context.bDeterministicCapture = bDeterministic;
        assert(run.Graph.Initialize(&run.Pool));
        assert(run.Probe.Initialize(context));
    }

    // CSM を公開した状態にする（4 カスケード・有限の行列・増える分割）。false なら影の地図が無い状態にする
    void PublishShadowProbeCascades(ShadowProbeRun& run, bool bPublished)
    {
        PhysicalLightingResources& lighting = run.Context.PhysicalLighting;
        lighting.bShadowPublished = bPublished;
        if (!bPublished)
        {
            lighting.ShadowMapTexture.reset();
            return;
        }
        RHI::TextureDesc shadowDesc;
        shadowDesc.Width = 64;
        shadowDesc.Height = 64;
        shadowDesc.ArraySize = PhysicalLightingShadowCascadeCount;
        shadowDesc.TextureFormat = RHI::Format::R32_FLOAT;
        lighting.ShadowMapTexture = run.Device->CreateTexture(shadowDesc);
        CascadedDirectionalShadowShaderValues& cascaded = lighting.CascadedShadow;
        cascaded.bEnabled = true;
        cascaded.CascadeCount = PhysicalLightingShadowCascadeCount;
        for (uint32_t cascade = 0; cascade < PhysicalLightingShadowCascadeCount; ++cascade)
        {
            for (uint32_t element = 0; element < 16u; ++element)
            {
                cascaded.View[cascade][element] = element % 5 == 0 ? 1.0f : 0.0f;
                cascaded.Projection[cascade][element] = element % 5 == 0 ? 1.0f : 0.0f;
            }
        }
        const float splits[PhysicalLightingShadowSplitCount] = {0.1f, 10.0f, 20.0f, 40.0f, 80.0f};
        std::memcpy(cascaded.SplitDistances, splits, sizeof(splits));
    }

    // 1 フレーム回す。frameIndex は 0 から数える（実行の番号は frameIndex + 1）
    void RunShadowProbeFrame(ShadowProbeRun& run, uint64_t frameIndex, bool bEpochStart)
    {
        run.Context.bTemporalEpochStart = bEpochStart;
        run.Context.FrameIndex = static_cast<uint32_t>(frameIndex % 2);
        run.Context.RenderFrameSerial = frameIndex + 1;
        run.Pool.EndFrame();
        run.Pool.BeginFrame(frameIndex);
        run.Graph.BeginFrame(frameIndex);
        run.Graph.AddPass(&run.Inputs);
        run.Graph.AddPass(&run.Probe);
        assert(run.Graph.Compile(run.Context));
        const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
        assert(result.bSuccess);
    }

    void ShutdownShadowProbeRun(ShadowProbeRun& run)
    {
        run.Probe.Shutdown();
        run.Graph.Shutdown();
        run.Pool.EndFrame();
        run.Pool.Shutdown();
        run.ShaderMgr.Shutdown();
    }

    // 決定的な撮影: エポックの前は何も dispatch しない。エポックが来ても影の地図が公開されていなければ測れないので、
    // 次に測れるフレームで標本を固定する（mode 0）。以後は毎フレーム測る（mode 1）。
    // 統計は数フレーム遅れて読み戻して足し、2 実行以内の分（GPU が書き終えていないかもしれない）は足さない。
    // 比は定義どおり: mean_abs_delta・changed・flip は pairs、partial・texel は見えていた標本の延べ数に対する値
    void TestShadowProbeCapturesAfterEpochThenMeasuresAndAggregates()
    {
        ShadowProbeRun run;
        InitializeShadowProbeRun(run, true);
        GShadowProbeParamUpdates.clear();

        // フレーム 0〜2: エポックの前（影の地図は公開済み）。dispatch しない
        PublishShadowProbeCascades(run, true);
        for (uint64_t frame = 0; frame < 3; ++frame)
        {
            RunShadowProbeFrame(run, frame, false);
        }
        assert(run.CommandList.DispatchCount == 0 && !run.Probe.HasCapturedProbes());

        // フレーム 3: エポックの最初のフレームだが影の地図が無い。測れない
        PublishShadowProbeCascades(run, false);
        RunShadowProbeFrame(run, 3, true);
        assert(run.CommandList.DispatchCount == 0 && !run.Probe.HasCapturedProbes());

        // フレーム 4: 影の地図が来た最初のフレームで標本を固定する（エポックの合図は 1 回だけ。ここでは立てない）
        PublishShadowProbeCascades(run, true);
        RunShadowProbeFrame(run, 4, false);
        assert(run.CommandList.DispatchCount == 1 && run.Probe.HasCapturedProbes());
        assert(GShadowProbeParamUpdates.size() == 1);
        {
            const Container::VariableArray<uint8_t>& bytes = GShadowProbeParamUpdates.back();
            // screen（幅・高さ・格子の横・縦）と control（モード・カスケード数・有効・格子の間隔）の位置は std140 で 704・720
            assert(ReadShadowProbeParamWord(bytes, 704) == 128 && ReadShadowProbeParamWord(bytes, 708) == 64);
            assert(ReadShadowProbeParamWord(bytes, 712) == 32 && ReadShadowProbeParamWord(bytes, 716) == 16);
            assert(ReadShadowProbeParamWord(bytes, 720) == 0);
            assert(ReadShadowProbeParamWord(bytes, 724) == PhysicalLightingShadowCascadeCount);
            assert(ReadShadowProbeParamWord(bytes, 728) == 1 && ReadShadowProbeParamWord(bytes, 732) == ShadowProbe::GRID_STEP);
        }
        assert(run.Device->ShadowProbeStatsBuffers.size() == 4);

        // 実行の番号 = フレーム + 1。標本を固定したのは実行 5 で、統計の置き場は実行の番号 % 4
        auto statsWords = [&](uint64_t executeIndex) -> uint32_t*
        {
            auto* buffer = static_cast<FakeBuffer*>(run.Device->ShadowProbeStatsBuffers[executeIndex % 4].get());
            assert(buffer->MappedBytes.size() >= ShadowProbe::STATS_BYTES);
            return reinterpret_cast<uint32_t*>(buffer->MappedBytes.data());
        };
        auto fillCapture = [&](uint64_t executeIndex)
        {
            uint32_t* words = statsWords(executeIndex);
            std::memset(words, 0, ShadowProbe::STATS_BYTES);
            words[ShadowProbe::StatCaptured] = 300;
        };
        auto fillMeasure = [&](uint64_t executeIndex, bool bFirst)
        {
            uint32_t* words = statsWords(executeIndex);
            std::memset(words, 0, ShadowProbe::STATS_BYTES);
            words[ShadowProbe::StatVisible] = 100;
            words[ShadowProbe::StatPartial] = 20;
            words[ShadowProbe::StatTexelSum] = 100 * 16 * 40; // 1 点 40 mm
            if (!bFirst)
            {
                words[ShadowProbe::StatPairs] = 90;
                words[ShadowProbe::StatDeltaSum] = 90 * 1024; // 1 組 0.25
                words[ShadowProbe::StatChanged] = 45;
                words[ShadowProbe::StatFlip] = 9;
            }
        };
        fillCapture(5);

        // フレーム 5〜12: 測る（mode 1）。実行 6〜13
        constexpr uint64_t LastFrame = 12;
        for (uint64_t frame = 5; frame <= LastFrame; ++frame)
        {
            RunShadowProbeFrame(run, frame, false);
            assert(run.CommandList.DispatchCount == 1 + (frame - 4));
            assert(ReadShadowProbeParamWord(GShadowProbeParamUpdates.back(), 720) == 1);
            fillMeasure(frame + 1, frame == 5);
        }

        // 読み戻せたのは、最後の 2 実行（12・13）を除く固定の 1 回と測った 6 回（実行 6〜11）
        run.Probe.LogSummary();
        const ShadowProbe::Totals& totals = run.Probe.GetTotals();
        assert(totals.Probes == 300);
        assert(totals.Frames == 6);
        assert(totals.Visible == 600 && totals.Pairs == 90 * 5);
        assert(std::abs(totals.MeanAbsDelta() - 0.25) < 1.0e-9);
        assert(std::abs(totals.ChangedRatio() - 0.5) < 1.0e-9);
        assert(std::abs(totals.FlipRatio() - 0.1) < 1.0e-9);
        assert(std::abs(totals.PartialRatio() - 0.2) < 1.0e-9);
        assert(std::abs(totals.MeanTexelMm() - 40.0) < 1.0e-9);

        ShutdownShadowProbeRun(run);
    }

    // --shadow-method=vsm の統計の語（9 以降）の合計と比。VSM を測ったフレームだけが VSM の合計へ入る
    void TestShadowProbeTotalsAggregateVsmWords()
    {
        ShadowProbe::Totals totals;
        uint32_t words[ShadowProbe::STATS_WORD_COUNT] = {};
        words[ShadowProbe::StatVisible] = 100;
        words[ShadowProbe::StatVsmVisible] = 100;
        words[ShadowProbe::StatVsmPairs] = 80;
        words[ShadowProbe::StatVsmDeltaSum] = 80 * 2048; // 1 組 0.5
        words[ShadowProbe::StatVsmChanged] = 40;
        words[ShadowProbe::StatVsmFlip] = 8;
        words[ShadowProbe::StatVsmPartial] = 10;
        words[ShadowProbe::StatVsmTexelSum] = 100 * 16 * 20; // 1 点 20 mm
        words[ShadowProbe::StatBothDefinite] = 50;
        words[ShadowProbe::StatAgree] = 49;
        words[ShadowProbe::StatFiner] = 90;
        words[ShadowProbe::StatVsmFallbackSamples] = 160;

        // VSM を測っていないフレームは、VSM の合計に入らない（csm の構成では VSM の行を出さない）
        totals.AddMeasuredFrame(words, false);
        assert(totals.Frames == 1 && totals.VsmFrames == 0 && totals.VsmVisible == 0 && totals.BothDefinite == 0);

        totals.AddMeasuredFrame(words, true);
        totals.AddMeasuredFrame(words, true);
        assert(totals.Frames == 3 && totals.VsmFrames == 2);
        assert(totals.VsmVisible == 200 && totals.VsmPairs == 160 && totals.BothDefinite == 100 && totals.Agree == 98);
        assert(std::abs(totals.VsmMeanAbsDelta() - 0.5) < 1.0e-9);
        assert(std::abs(totals.VsmChangedRatio() - 0.5) < 1.0e-9);
        assert(std::abs(totals.VsmFlipRatio() - 0.1) < 1.0e-9);
        assert(std::abs(totals.VsmPartialRatio() - 0.1) < 1.0e-9);
        assert(std::abs(totals.VsmMeanTexelMm() - 20.0) < 1.0e-9);
        assert(std::abs(totals.AgreeRatio() - 0.98) < 1.0e-9);
        assert(std::abs(totals.FinerRatio() - 0.9) < 1.0e-9);
        // 逃げた標本は 1 標本 16 点: 320 / (200 × 16)
        assert(std::abs(totals.FallbackRatio() - 0.1) < 1.0e-9);
    }

    // 決定的な撮影のエポックは、読み込みが落ち着くまで何度も始め直される。始まるたびに標本を固定し直し、
    // それまでに足した合計（読み込み前のシーンで測った値）を捨てる
    void TestShadowProbeEpochRestartRecapturesAndResetsTotals()
    {
        ShadowProbeRun run;
        InitializeShadowProbeRun(run, true);
        PublishShadowProbeCascades(run, true);
        GShadowProbeParamUpdates.clear();

        RunShadowProbeFrame(run, 0, true);
        assert(run.CommandList.DispatchCount == 1 && run.Probe.HasCapturedProbes());
        assert(run.Device->ShadowProbeStatsBuffers.size() == 4);
        for (uint64_t frame = 1; frame <= 8; ++frame)
        {
            RunShadowProbeFrame(run, frame, false);
            // 実行 frame + 1 の置き場に、見えた標本が 10 あったと置く
            auto* buffer = static_cast<FakeBuffer*>(run.Device->ShadowProbeStatsBuffers[(frame + 1) % 4].get());
            uint32_t* words = reinterpret_cast<uint32_t*>(buffer->MappedBytes.data());
            std::memset(words, 0, ShadowProbe::STATS_BYTES);
            words[ShadowProbe::StatVisible] = 10;
        }
        assert(run.CommandList.DispatchCount == 9);
        assert(ReadShadowProbeParamWord(GShadowProbeParamUpdates.back(), 720) == 1);

        // 2 回目のエポック: その場で固定し直す（mode 0）。それまでの合計は 0 に戻る
        RunShadowProbeFrame(run, 9, true);
        assert(run.CommandList.DispatchCount == 10);
        assert(ReadShadowProbeParamWord(GShadowProbeParamUpdates.back(), 720) == 0);
        assert(run.Probe.HasCapturedProbes());
        assert(run.Probe.GetTotals().Frames == 0 && run.Probe.GetTotals().Visible == 0);

        // 固定し直した後は、また測る
        RunShadowProbeFrame(run, 10, false);
        assert(run.CommandList.DispatchCount == 11);
        assert(ReadShadowProbeParamWord(GShadowProbeParamUpdates.back(), 720) == 1);
        ShutdownShadowProbeRun(run);
    }

    // 決定的な撮影でない起動: エポックが無いので、起動から FALLBACK_CAPTURE_EXECUTE_COUNT 回目の実行で標本を固定する
    void TestShadowProbeFallbackCapturesAfterFixedExecuteCount()
    {
        ShadowProbeRun run;
        InitializeShadowProbeRun(run, false);
        PublishShadowProbeCascades(run, true);
        const uint64_t captureFrame = ShadowProbe::FALLBACK_CAPTURE_EXECUTE_COUNT - 1;
        for (uint64_t frame = 0; frame < captureFrame; ++frame)
        {
            // エポックの合図は決定的な撮影でなければ無視する
            RunShadowProbeFrame(run, frame, frame == 7);
        }
        assert(run.CommandList.DispatchCount == 0 && !run.Probe.HasCapturedProbes());
        RunShadowProbeFrame(run, captureFrame, false);
        assert(run.CommandList.DispatchCount == 1 && run.Probe.HasCapturedProbes());
        ShutdownShadowProbeRun(run);
    }

    // 標本の集計の語から、ログの値を定義どおりに求める（ゼロ除算しない）
    void TestShadowProbeTotalsHandleEmptyDenominators()
    {
        ShadowProbe::Totals totals;
        assert(totals.MeanAbsDelta() == 0.0 && totals.ChangedRatio() == 0.0 && totals.FlipRatio() == 0.0);
        assert(totals.PartialRatio() == 0.0 && totals.MeanTexelMm() == 0.0 && totals.OutOfRangeRatio() == 0.0);
        const ShadowProbe::Grid grid = ShadowProbe::ComputeGrid(1280, 720);
        assert(grid.CountX == 320 && grid.CountY == 180 && grid.Count() == 57600);
        assert(ShadowProbe::ComputeGrid(1281, 721).CountX == 321);
        assert(!ShadowProbe::ComputeGrid(0, 720).IsValid());
    }
#endif // NORVES_ENABLE_STATS

    // ========================================
    // 太陽の VSM の資源（VirtualShadowMapPass。--shadow-method=vsm）
    // ========================================

    // csm（既定）の構成にはパスが無く、vsm の構成では照明の前に 1 つだけ入る（ほかのパスの数・順は変えない）。要求したページの数はパスへ届く
    void TestVirtualShadowMapPassAbsentForCsmAndBeforeLightingForVsm()
    {
        SceneRenderer csmRenderer;
        SceneView csmView;
        csmView.SetupDeferredPipeline(&csmRenderer);
        assert(csmView.GetShadowMethod() == ShadowMethod::Csm);
        assert(csmView.FindPass("VirtualShadowMapPass") == nullptr);

        SceneRenderer vsmRenderer;
        SceneView vsmView;
        vsmView.SetShadowMethod(ShadowMethod::Vsm);
        vsmView.SetVsmPoolPages(777);
        vsmView.SetupDeferredPipeline(&vsmRenderer);
        const auto* vsmPass = static_cast<const VirtualShadowMapPass*>(vsmView.FindPass("VirtualShadowMapPass"));
        const IViewPass* lighting = vsmView.FindPass("LightingPass");
        assert(vsmPass != nullptr && lighting != nullptr);
        assert(vsmPass->GetRequestedPoolPages() == 777);
        int vsmIndex = -1;
        int lightingIndex = -1;
        for (uint32_t index = 0; index < vsmView.GetPassCount(); ++index)
        {
            vsmIndex = vsmView.GetPassAt(index) == vsmPass ? static_cast<int>(index) : vsmIndex;
            lightingIndex = vsmView.GetPassAt(index) == lighting ? static_cast<int>(index) : lightingIndex;
        }
        assert(vsmIndex >= 0 && vsmIndex < lightingIndex);
        // 深度が確定した後: ビジビリティの解決・GBuffer・MegaGeometry のどれよりも後
        for (const char* earlier : {"GBufferPass", "MegaGeometryPass", "VisibilityResolvePass"})
        {
            const IViewPass* earlierPass = vsmView.FindPass(earlier);
            for (uint32_t index = 0; earlierPass != nullptr && index < vsmView.GetPassCount(); ++index)
            {
                if (vsmView.GetPassAt(index) == earlierPass)
                {
                    assert(static_cast<int>(index) < vsmIndex);
                }
            }
        }
        assert(vsmView.GetPassCount() == csmView.GetPassCount() + 1);

        // MegaGeometry の投影物のカリングは、vsm の構成が主の MegaGeometryPass の入力を読む（csm の構成には VSM のパスが無く、カリングも無い）
        assert(vsmPass->GetMegaGeometryPass() != nullptr);
        assert(static_cast<const IViewPass*>(vsmPass->GetMegaGeometryPass()) == vsmView.FindPass("MegaGeometryPass"));

        // スキニングの計算は VSM のパスより前（変形した頂点を影の描画が読む）
        const IViewPass* vsmSkinning = vsmView.FindPass("SkinningComputePass");
        assert(vsmSkinning != nullptr && vsmSkinning->IsEnabled());
        int skinningIndex = -1;
        for (uint32_t index = 0; index < vsmView.GetPassCount(); ++index)
        {
            skinningIndex = vsmView.GetPassAt(index) == vsmSkinning ? static_cast<int>(index) : skinningIndex;
        }
        assert(skinningIndex >= 0 && skinningIndex < vsmIndex);

        // ビジビリティバッファを使わない構成（off）: csm ではスキニングの計算は無効のままで何も足されず、
        // vsm では VSM の投影物のために有効になる（VSM のパスより前）
        SceneRenderer csmOffRenderer;
        SceneView csmOffView;
        csmOffView.SetupDeferredPipeline(&csmOffRenderer, RasterDirectBrdf::Analytic, VisibilityBufferMode::Off);
        const IViewPass* csmOffSkinning = csmOffView.FindPass("SkinningComputePass");
        assert(csmOffSkinning != nullptr && !csmOffSkinning->IsEnabled());
        assert(csmOffView.FindPass("VirtualShadowMapPass") == nullptr);

        SceneRenderer vsmOffRenderer;
        SceneView vsmOffView;
        vsmOffView.SetShadowMethod(ShadowMethod::Vsm);
        vsmOffView.SetupDeferredPipeline(&vsmOffRenderer, RasterDirectBrdf::Analytic, VisibilityBufferMode::Off);
        const IViewPass* vsmOffSkinning = vsmOffView.FindPass("SkinningComputePass");
        const IViewPass* vsmOffPass = vsmOffView.FindPass("VirtualShadowMapPass");
        assert(vsmOffSkinning != nullptr && vsmOffSkinning->IsEnabled() && vsmOffPass != nullptr);
        int offSkinningIndex = -1;
        int offVsmIndex = -1;
        for (uint32_t index = 0; index < vsmOffView.GetPassCount(); ++index)
        {
            offSkinningIndex = vsmOffView.GetPassAt(index) == vsmOffSkinning ? static_cast<int>(index) : offSkinningIndex;
            offVsmIndex = vsmOffView.GetPassAt(index) == vsmOffPass ? static_cast<int>(index) : offVsmIndex;
        }
        assert(offSkinningIndex >= 0 && offSkinningIndex < offVsmIndex);
    }

    // VSM のテストで、GBuffer の深度・法線を書くだけのパス（深度の確定の代わり）
    class VsmInputsPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "VsmInputsPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            Depth = builder.WriteTexture(RenderGraphResourceNames::GBufferDepth,
                                         RGTextureDesc::RenderTarget(128, 64, RHI::Format::R32_FLOAT, "Test_Depth"),
                                         RHI::ResourceState::RenderTarget,
                                         RHI::ResourceState::ShaderResource);
            Normal = builder.WriteTexture(RenderGraphResourceNames::GBufferNormal,
                                          RGTextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "Test_Normal"),
                                          RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        RGTextureHandle Depth;
        RGTextureHandle Normal;
    };

    // VSM のテストで、公開された 6 つの資源を名前で読む後のパス（後のパス・照明の代わり）
    class VsmConsumerPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "VsmConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const NorvesLib::Core::Identity names[] = {RenderGraphResourceNames::VsmPhysicalPool,
                                                         RenderGraphResourceNames::VsmPageTable,
                                                         RenderGraphResourceNames::VsmRequestBits,
                                                         RenderGraphResourceNames::VsmFreeList,
                                                         RenderGraphResourceNames::VsmStats,
                                                         RenderGraphResourceNames::VsmDirtyList};
            // 照明・影の標本が読むかどうかを決める問い合わせ（公開の有無を、グラフのエラーにせず返す）
            bSawPublication = builder.HasBuffer(RenderGraphResourceNames::VsmPageTable) && builder.HasBuffer(RenderGraphResourceNames::VsmPhysicalPool);
            for (uint32_t index = 0; index < 6; ++index)
            {
                Handles[index] = builder.ReadBuffer(names[index], RHI::ResourceState::ShaderResource);
            }
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        RGBufferHandle Handles[6];
        bool bSawPublication = false;
    };

    // VSM のパスが無いグラフ（csm の構成・VSM を作れなかった装置）で、公開の問い合わせがグラフをエラーにしないこと
    // （TryGetBuffer は未公開の名前をグラフのエラーにするので、照明・影の標本は HasBuffer で読むかどうかを決める）
    class VsmPublicationQueryPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "VsmPublicationQueryPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            bHasPageTable = builder.HasBuffer(RenderGraphResourceNames::VsmPageTable);
            bHasPool = builder.HasBuffer(RenderGraphResourceNames::VsmPhysicalPool);
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        bool bHasPageTable = true;
        bool bHasPool = true;
    };

#if NORVES_ENABLE_LOGGING
    // VSM のログ（カテゴリ VirtualShadowMapPass）を残し、部分文字列に一致する行の数を数える
    struct VsmLogCollector final : Logging::ILogSink
    {
        Container::VariableArray<Container::String> Messages;

        void OnLog(const Logging::LogEntry& entry) override
        {
            if (entry.category == "VirtualShadowMapPass" || entry.category == "VirtualShadowMapRaster" ||
                entry.category == "VirtualShadowMapMegaCull")
            {
                Messages.push_back(entry.message);
            }
        }

        uint32_t Count(const char* needle) const
        {
            uint32_t count = 0;
            for (const Container::String& message : Messages)
            {
                count += std::strstr(message.c_str(), needle) != nullptr ? 1u : 0u;
            }
            return count;
        }
    };
#endif

    struct VsmRun
    {
        RHI::TSharedPtr<FakeDevice> Device = RHI::MakeShared<FakeDevice>();
        MockAllocator Allocator;
        RHI::TransientResourcePool Pool;
        FakeCommandList CommandList;
        CameraProxy Camera;
        ShaderManager ShaderMgr;
        ViewRenderContext Context;
        VsmInputsPass Inputs;
        VsmConsumerPass Consumer;
        RenderGraph Graph;
        // スワップチェーンの飛行中のフレームの数（製品は 1。FrameIndex は飛行中の番号で、完了済みの通し番号は
        // 「記録を始めたフレームの 1 つ前から数えて飛行中の数だけ前」までとする）
        uint32_t FramesInFlight = 2;
        // 0 以外なら、GPU がこの数だけ遅れて完了するものとして、完了済みの通し番号を渡す（飛行中の数より遅い GPU）
        uint32_t CompletedLag = 0;
    };

    void InitializeVsmRun(VsmRun& run)
    {
        assert(run.Pool.Initialize(&run.Allocator, 1));
        run.Camera.Viewport.Width = 128.0f;
        run.Camera.Viewport.Height = 64.0f;
        ViewRenderContext& context = run.Context;
        context.Device = run.Device.get();
        context.CommandList = &run.CommandList;
        context.TransientPool = &run.Pool;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.MainCamera = &run.Camera;
        // 印付け・割り当て・消去の計算シェーダー（vsm_*.comp）を読む
        assert(run.ShaderMgr.Initialize(run.Device.get(), TestShaderDirectory));
        context.ShaderMgr = &run.ShaderMgr;
        assert(run.Graph.Initialize(&run.Pool));
    }

    // フレームの通し番号・飛行中の番号・完了済みの通し番号を文脈へ入れる。
    // スワップチェーンのフェンスは、同じ飛行中の番号の前のフレーム（飛行中の数だけ前）の完了を待ってから記録を始めさせる
    void SetVsmFrame(VsmRun& run, uint64_t frameIndex)
    {
        run.Context.FrameIndex = static_cast<uint32_t>(frameIndex % run.FramesInFlight);
        run.Context.RenderFrameSerial = frameIndex + 1;
        const uint64_t lag = run.CompletedLag != 0 ? run.CompletedLag : run.FramesInFlight;
        run.Context.CompletedRenderFrameSerial = run.Context.RenderFrameSerial > lag ? run.Context.RenderFrameSerial - lag : 0;
    }

    // 1 フレーム回す。入力 → VSM → 読むパスの順に足す。bConsume が false なら読むパスは足さない
    // 1 つのビューポートの Execute を回す。フレーム（通し番号と飛行中の番号）と、プール・グラフの世代は別に数える
    // （同じフレームに複数のビューポートを描くとき、フレームは同じでプール・グラフの世代だけが進む）
    void RunVsmViewport(VsmRun& run, VirtualShadowMapPass& pass, uint64_t frameIndex, uint64_t graphGeneration, bool bConsume)
    {
        SetVsmFrame(run, frameIndex);
        run.Pool.EndFrame();
        run.Pool.BeginFrame(graphGeneration);
        run.Graph.BeginFrame(graphGeneration);
        run.Graph.AddPass(&run.Inputs);
        run.Graph.AddPass(&pass);
        if (bConsume)
        {
            run.Graph.AddPass(&run.Consumer);
        }
        assert(run.Graph.Compile(run.Context));
        const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
        assert(result.bSuccess);
    }

    // 1 フレーム（ビューポート 1 つ）回す。入力 → VSM → 読むパスの順に足す。bConsume が false なら読むパスは足さない
    void RunVsmFrame(VsmRun& run, VirtualShadowMapPass& pass, uint64_t frameIndex, bool bConsume)
    {
        RunVsmViewport(run, pass, frameIndex, frameIndex, bConsume);
    }

    // 実際の GBuffer と照明のパスの間に VSM のパスを置いて 2 フレーム回し、照明が VSM と CSM のどちらを読むかを確かめる（定義は照明の VSM の統計のテストの前）
    void RunVsmPassThroughLighting(VsmRun& run, VirtualShadowMapPass& pass, bool bExpectVsm);

    void ShutdownVsmRun(VsmRun& run, VirtualShadowMapPass& pass)
    {
        pass.Shutdown();
        run.ShaderMgr.Shutdown();
        run.Graph.Shutdown();
        run.Pool.EndFrame();
        run.Pool.Shutdown();
    }

    const BufferCreationRecord* FindBufferCreation(const FakeDevice& device, const char* debugName)
    {
        for (const BufferCreationRecord& record : device.CreatedBuffers)
        {
            if (IsDebugName(record.Desc.DebugName, debugName))
            {
                return &record;
            }
        }
        return nullptr;
    }

    uint32_t CountVsmBufferCreations(const FakeDevice& device)
    {
        uint32_t count = 0;
        for (const BufferCreationRecord& record : device.CreatedBuffers)
        {
            count += record.Desc.DebugName != nullptr && std::strncmp(record.Desc.DebugName, "VSM_", 4) == 0 ? 1u : 0u;
        }
        return count;
    }

    // 対応した装置では、プール（5120 ページ = 320 MiB・1 ページ 64 KiB）・ページの表（段 × 128 × 128 の uint32）・要求のビット列・
    // 空きページの一覧・統計を 1 回ずつ作り、深度の後・読むパスの前に並べて名前で公開する。
    // 最初の実行だけがプールを 1.0 のビット・表を 0 で埋め、要求と統計は毎フレーム 0 から数える。台帳（VRAM_LEDGER）は作成時に 1 回ずつ
    void TestVirtualShadowMapPassCreatesAndPublishesResources()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);
#endif

        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);

        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context));
        assert(pass.IsActive() && pass.GetFallbackReason() == VirtualShadowMap::FallbackReason::None);
        assert(pass.GetPoolPages() == 5120);

        // 資源の大きさと用途
        struct Expected
        {
            const char* Name;
            uint64_t Bytes;
        };
        const Expected expected[] = {
            {"VSM_PhysicalPool", 5120ull * 65536ull},
            {"VSM_PageTable", 10ull * 128ull * 128ull * 4ull},
            {"VSM_RequestBits", 10ull * 128ull * 128ull / 8ull},
            {"VSM_FreeList", (5120ull * 3ull + 1ull) * 4ull},
            {"VSM_Stats", 212ull},
            {"VSM_DirtyList", (5120ull + 4ull) * 4ull},
        };
        for (const Expected& entry : expected)
        {
            assert(CountBufferCreations(*run.Device, entry.Name) == 1);
            const BufferCreationRecord* record = FindBufferCreation(*run.Device, entry.Name);
            assert(record != nullptr && record->Desc.Size == entry.Bytes);
            assert((record->Desc.Usage & RHI::ResourceUsage::StorageBuffer) == RHI::ResourceUsage::StorageBuffer);
            assert((record->Desc.Usage & RHI::ResourceUsage::TransferDst) == RHI::ResourceUsage::TransferDst);
        }
        assert(VirtualShadowMap::PoolBytes(5120) == 320ull * 1024ull * 1024ull);
        assert(VirtualShadowMap::PAGE_BYTES == 64ull * 1024ull);
        // 6 つの資源と、統計の読み戻しの 4 枠
        assert(CountVsmBufferCreations(*run.Device) == 10);
        assert(CountBufferCreations(*run.Device, "VSM_StatsReadback") == 4);
        const BufferCreationRecord* dirtyRecord = FindBufferCreation(*run.Device, "VSM_DirtyList");
        assert((dirtyRecord->Desc.Usage & RHI::ResourceUsage::IndirectBuffer) == RHI::ResourceUsage::IndirectBuffer);
        const BufferCreationRecord* poolRecord = FindBufferCreation(*run.Device, "VSM_PhysicalPool");
        assert((poolRecord->Desc.Usage & RHI::ResourceUsage::BufferDeviceAddress) == RHI::ResourceUsage::BufferDeviceAddress);

        // 空きページの一覧: 先頭が数、続いて 0 〜 5119
        {
            const FakeBuffer* freeList = static_cast<const FakeBuffer*>(pass.GetFreeList().get());
            assert(freeList->UpdateCallCount == 1);
            assert(freeList->LastUpdateBytes.size() == (5120u * 3u + 1u) * 4u);
            const uint32_t* words = reinterpret_cast<const uint32_t*>(freeList->LastUpdateBytes.data());
            assert(words[0] == 5120u);
            for (uint32_t page = 0; page < 5120u; ++page)
            {
                assert(words[page + 1u] == page);
            }
        }

#if NORVES_ENABLE_LOGGING
        // 台帳は作成時に 1 回ずつ。VSM_FALLBACK は出ない
        assert(logs.Count("VRAM_LEDGER vsm_pool pages=5120 mb=320.000") == 1);
        assert(logs.Count("VRAM_LEDGER vsm_page_table mb=0.625") == 1);
        assert(logs.Count("VSM_FALLBACK") == 0);
#endif

        // 1 フレーム目: 深度 → VSM → 読むパス。6 つの資源が公開され、読むパスが名前で取れる
        RunVsmFrame(run, pass, 0, true);
        assert(run.Consumer.bSawPublication);
        {
            const Container::VariableArray<uint32_t>& order = run.Graph.GetCompiledPassOrder();
            assert(order.size() == 3 && order[0] == 0 && order[1] == 1 && order[2] == 2);
            // 深度・法線の読み 2 つ + 資源 6 つの書き込み
            assert(run.Graph.GetDeclaredPassAccessCount(1) == 8);
            assert(run.Graph.GetDeclaredPassAccessCount(2) == 6);
            for (const RGBufferHandle& handle : run.Consumer.Handles)
            {
                assert(handle.IsValid());
            }
        }
        {
            // 最初の実行だけプールを 1.0 のビットで埋め、続いて毎フレームの記録が要求・ページの表・統計を 0 から数え直す
            const auto& fills = run.CommandList.VsmFills;
            assert(fills.size() == 4);
            assert(IsDebugName(fills[0].BufferName, "VSM_PhysicalPool") && fills[0].SizeBytes == 5120ull * 65536ull &&
                   fills[0].Value == 0x3F800000u);
            assert(IsDebugName(fills[1].BufferName, "VSM_RequestBits") && fills[1].SizeBytes == 20480ull && fills[1].Value == 0u);
            assert(IsDebugName(fills[2].BufferName, "VSM_PageTable") && fills[2].SizeBytes == 655360ull && fills[2].Value == 0u);
            assert(IsDebugName(fills[3].BufferName, "VSM_Stats") && fills[3].SizeBytes == VirtualShadowMap::STATS_BYTES && fills[3].Value == 0u);
        }

        // 2 フレーム目: 要求・ページの表・統計だけを 0 から数え直す（プールは埋め直さない。消去は dirty のページだけ）
        RunVsmFrame(run, pass, 1, true);
        {
            const auto& fills = run.CommandList.VsmFills;
            assert(fills.size() == 7);
            assert(IsDebugName(fills[4].BufferName, "VSM_RequestBits") && fills[4].Value == 0u);
            assert(IsDebugName(fills[5].BufferName, "VSM_PageTable") && fills[5].Value == 0u);
            assert(IsDebugName(fills[6].BufferName, "VSM_Stats") && fills[6].Value == 0u);
        }

#if NORVES_ENABLE_LOGGING
        // フレームを描いても台帳は増えない
        assert(logs.Count("VRAM_LEDGER vsm_pool") == 1 && logs.Count("VRAM_LEDGER vsm_page_table") == 1);
#endif

        ShutdownVsmRun(run, pass);
        assert(!pass.IsActive() && pass.GetPoolPages() == 0 && !pass.GetPool());

#if NORVES_ENABLE_LOGGING
        logger.RemoveSink(&logs);
        logger.Shutdown();
#endif
    }

    // VSM のパスが無いグラフでは、公開の問い合わせ（HasBuffer）が false を返すだけで、グラフのコンパイル・実行は成功する
    void TestRenderGraphHasBufferIsQuietWithoutVsmPass()
    {
        VsmRun run;
        InitializeVsmRun(run);
        VsmPublicationQueryPass query;
        run.Context.FrameIndex = 0;
        run.Context.RenderFrameSerial = 1;
        run.Pool.EndFrame();
        run.Pool.BeginFrame(0);
        run.Graph.BeginFrame(0);
        run.Graph.AddPass(&run.Inputs);
        run.Graph.AddPass(&query);
        assert(run.Graph.Compile(run.Context));
        const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
        assert(result.bSuccess);
        assert(!query.bHasPageTable && !query.bHasPool);
        run.ShaderMgr.Shutdown();
        run.Graph.Shutdown();
        run.Pool.EndFrame();
        run.Pool.Shutdown();
    }

    // 作れない装置（断片シェーダーの storage の書き込み・アトミックが無い、BDA が無い、プールが 512 ページ未満しか取れない、確保に失敗する）では、
    // 資源もパスの宣言も作らず（読むのは深度・法線だけでなく何も宣言しない）、VSM_FALLBACK reason=<…> を 1 回だけ出す。
    // 何も設定しない Fake の装置（bFragmentStoresAndAtomics が無い）も同じ
    void TestVirtualShadowMapPassFallsBackWhenUnsupported()
    {
        struct Case
        {
            bool bConfigure;
            bool bFragmentStoresAndAtomics;
            bool bBufferDeviceAddress;
            uint64_t MaxStorageBufferRange;
            const char* FailBufferDebugName;
            VirtualShadowMap::FallbackReason Reason;
            const char* ReasonText;
        };
        const Case cases[] = {
            {false, false, false, 0, nullptr, VirtualShadowMap::FallbackReason::FragmentAtomics, "fragment_atomics"},
            {true, false, true, 0xFFFFFFFFull, nullptr, VirtualShadowMap::FallbackReason::FragmentAtomics, "fragment_atomics"},
            {true, false, false, 0xFFFFFFFFull, nullptr, VirtualShadowMap::FallbackReason::FragmentAtomics, "fragment_atomics"},
            {true, true, false, 0xFFFFFFFFull, nullptr, VirtualShadowMap::FallbackReason::BufferDeviceAddress, "bda"},
            {true, true, true, 16ull * 1024ull * 1024ull, nullptr, VirtualShadowMap::FallbackReason::PoolSize, "pool_size"},
            {true, true, true, 512ull * 65536ull - 1ull, nullptr, VirtualShadowMap::FallbackReason::PoolSize, "pool_size"},
            // 確保に失敗する装置（プールを作れない・後ろの資源を作れない）: 作れたぶんも手放し、何も残さない
            {true, true, true, 0xFFFFFFFFull, "VSM_PhysicalPool", VirtualShadowMap::FallbackReason::PoolSize, "pool_size"},
            {true, true, true, 0xFFFFFFFFull, "VSM_Stats", VirtualShadowMap::FallbackReason::PoolSize, "pool_size"},
            // MegaGeometry の影の経路の資源（カリングの出力の一覧・dirty の階層・クラスタの記録）を作れない装置: 影を欠いた VSM にせず CSM へ戻る
            {true, true, true, 0xFFFFFFFFull, "VsmMega_List", VirtualShadowMap::FallbackReason::MegaGeometry, "mega_geometry"},
            {true, true, true, 0xFFFFFFFFull, "VsmMega_DirtyBits", VirtualShadowMap::FallbackReason::MegaGeometry, "mega_geometry"},
            {true, true, true, 0xFFFFFFFFull, "VsmMega_Chunks", VirtualShadowMap::FallbackReason::MegaGeometry, "mega_geometry"},
        };

        for (const Case& testCase : cases)
        {
#if NORVES_ENABLE_LOGGING
            Logging::LogConfig logConfig;
            logConfig.minLevel = Logging::LogLevel::Trace;
            logConfig.outputType = Logging::LogOutput::None;
            logConfig.bAsyncLogging = false;
            logConfig.bAutoFlush = false;
            Logging::Logger& logger = Logging::Logger::GetInstance();
            logger.Shutdown();
            assert(logger.Initialize(logConfig));
            VsmLogCollector logs;
            logger.AddSink(&logs);
#endif
            VsmRun run;
            if (testCase.bConfigure)
            {
                run.Device->SetVirtualShadowMapCapabilities(
                    testCase.bFragmentStoresAndAtomics, testCase.bBufferDeviceAddress, testCase.MaxStorageBufferRange);
            }
            run.Device->FailBufferDebugName = testCase.FailBufferDebugName;
            InitializeVsmRun(run);

            VirtualShadowMapPass pass;
            assert(pass.Initialize(run.Context));
            assert(!pass.IsActive());
            assert(pass.GetFallbackReason() == testCase.Reason);
            assert(pass.GetPoolPages() == 0);
            assert(!pass.GetPool() && !pass.GetPageTable() && !pass.GetRequestBits() && !pass.GetFreeList() && !pass.GetStats() &&
                   !pass.GetDirtyList());
            // 失敗させた資源の作成は試みても、それが成功した後ろの資源は持たない（作成記録は残るが、パスは何も持たない）
            if (testCase.FailBufferDebugName == nullptr)
            {
                assert(CountVsmBufferCreations(*run.Device) == 0);
            }

            // 何も宣言せず、何も埋めない。実際の照明のパスを後ろに置き、VSM の資源が公開されないので CSM のまま描くことを確かめる
            //（照明の VSM のパラメータは無効、束縛 22・23 は VSM のバッファではない、CSM のテクスチャは束縛される）
            RunVsmPassThroughLighting(run, pass, false);
            assert(run.CommandList.VsmFills.empty());

#if NORVES_ENABLE_LOGGING
            char expectedLine[64] = {};
            std::snprintf(expectedLine, sizeof(expectedLine), "VSM_FALLBACK reason=%s", testCase.ReasonText);
            assert(logs.Count(expectedLine) == 1);
            assert(logs.Count("VSM_FALLBACK") == 1);
            assert(logs.Count("VRAM_LEDGER vsm_pool") == 0 && logs.Count("VRAM_LEDGER vsm_page_table") == 0);
            logger.RemoveSink(&logs);
            logger.Shutdown();
#endif
            ShutdownVsmRun(run, pass);
        }
    }

    // 印付け・割り当て・消去の計算パイプラインを作れない装置では、資源を作らず VSM_FALLBACK reason=pipeline を 1 回出して CSM のまま描く
    void TestVirtualShadowMapPassFallsBackWhenPipelineFails()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);
#endif
        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        run.Device->bFailComputePipelines = true;

        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context));
        assert(!pass.IsActive() && pass.GetFallbackReason() == VirtualShadowMap::FallbackReason::Pipeline);
        assert(!pass.GetPool() && !pass.GetDirtyList());
        assert(CountVsmBufferCreations(*run.Device) == 0);
        RunVsmPassThroughLighting(run, pass, false);
        assert(run.CommandList.VsmFills.empty() && run.CommandList.DispatchGroups.empty());
        assert(std::strcmp(VirtualShadowMap::FallbackReasonName(VirtualShadowMap::FallbackReason::Pipeline), "pipeline") == 0);
#if NORVES_ENABLE_LOGGING
        assert(logs.Count("VSM_FALLBACK reason=pipeline") == 1 && logs.Count("VSM_FALLBACK") == 1);
        logger.RemoveSink(&logs);
        logger.Shutdown();
#endif
        ShutdownVsmRun(run, pass);
    }

    // バリアのうち、バッファ名・前後の状態・記録した時点の CallSequence の長さが一致するものがあるか
    bool HasBufferBarrierAt(const FakeCommandList& commandList,
                            const char* debugName,
                            RHI::ResourceState before,
                            RHI::ResourceState after,
                            size_t sequencePosition)
    {
        for (const BarrierEvent& barrier : CollectBufferBarriers(commandList, debugName))
        {
            if (barrier.BeforeState == before && barrier.AfterState == after && barrier.SequencePosition == sequencePosition)
            {
                return true;
            }
        }
        return false;
    }

    // vsm の構成の 1 フレーム: 印付け（画面を 8x8 で覆う）→ 割り当て（11 段階のうち、無効化の矩形が無いので矩形の段階を除く 10 回）→
    // 消去（間接 dispatch）の順に記録する。
    //  - dispatch は 印付け (16, 8, 1)、続けて 引き継ぎ・年齢・計画・古い順に戻す・使用中の印を 0 に・印を付ける・空きへ詰める・割り当て・
    //    消去の一覧・締める の 10 回（欄は 10 段 × 128 × 128 を 256 で割った 640、物理ページは 5120 ÷ 256、要求の語は REQUEST_WORDS ÷ 256）の後に、
    //    消去の間接 dispatch が 1 回（引数は VSM_DirtyList の先頭）。
    //  - その間のバリア: 要求のビット列は印付けの後（割り当てが読む前）、空きの一覧・ページの表・統計・消去の一覧は割り当ての各段階の後、
    //    消去する一覧は締めた後に GenericRead へ進めてから間接 dispatch が読み、読んだ後に UnorderedAccess へ戻す。物理ページは消去の後。
    //  - 深度かクリップマップが無い構成では、印付けの dispatch は無く、割り当てと消去だけが走る（要求は 0 のまま、キャッシュは引き継がない）。
    void TestVirtualShadowMapPassRecordsMarkAllocateClearInOrder()
    {
        for (const bool bWithClipmap : {true, false})
        {
            VsmRun run;
            run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
            InitializeVsmRun(run);
            if (bWithClipmap)
            {
                run.Context.PhysicalLighting.SunClipmap = BuildVirtualShadowMapClipmap(
                    NorvesLib::Math::Vector3(0.35f, -0.8f, 0.45f), 1u, NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), VirtualShadowMapClipmapSettings{});
                assert(run.Context.PhysicalLighting.SunClipmap.bEnabled);
            }

            VirtualShadowMapPass pass;
            assert(pass.Initialize(run.Context));
            assert(pass.IsActive());
            RunVsmFrame(run, pass, 0, true);

            const FakeCommandList& commandList = run.CommandList;
            assert(pass.WasMarked() == bWithClipmap);
            const size_t markCount = bWithClipmap ? 1u : 0u;
            constexpr size_t AllocateStageDispatches = 10u;
            // 順序: [印付け] 割り当ての 10 回、消去（間接）
            assert(commandList.DispatchGroups.size() == markCount + AllocateStageDispatches);
            size_t groupIndex = 0;
            if (bWithClipmap)
            {
                const auto& mark = commandList.DispatchGroups[groupIndex++];
                assert(mark.X == 16u && mark.Y == 8u && mark.Z == 1u);
            }
            const uint32_t entryGroups = VirtualShadowMap::LEVEL_COUNT * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL / 256u;
            const uint32_t pageGroups = 5120u / 256u;
            const uint32_t requestGroups = VirtualShadowMap::REQUEST_WORDS / 256u;
            // 引き継ぎ・年齢・計画・古い順に戻す・使用中の印を 0 に・印を付ける・空きへ詰める・割り当てる・消去の一覧・締める
            const uint32_t expectedGroups[AllocateStageDispatches] = {
                entryGroups, entryGroups, 1u, entryGroups, pageGroups, entryGroups, pageGroups, requestGroups, entryGroups, 1u,
            };
            for (const uint32_t expected : expectedGroups)
            {
                const auto& stage = commandList.DispatchGroups[groupIndex++];
                assert(stage.X == expected && stage.Y == 1u && stage.Z == 1u);
            }
            assert(commandList.IndirectDispatches.size() == 1);
            assert(IsDebugName(commandList.IndirectDispatches[0].BufferName, "VSM_DirtyList") &&
                   commandList.IndirectDispatches[0].OffsetBytes == 0);
            // 並び: 印付け（D）・割り当て（D × 10）・消去（J）
            const char* expectedSequence = bWithClipmap ? "DDDDDDDDDDDJ" : "DDDDDDDDDDJ";
            assert(commandList.CallSequence.size() == std::strlen(expectedSequence));
            for (size_t index = 0; index < commandList.CallSequence.size(); ++index)
            {
                assert(commandList.CallSequence[index] == expectedSequence[index]);
            }

            // バリア（SequencePosition は、直前までに記録した B・E・D・J の数）
            const size_t afterMark = markCount;
            const size_t afterFreeReset = markCount + 5u;
            const size_t afterAllocate = markCount + 8u;
            const size_t afterFinalize = markCount + AllocateStageDispatches;
            const size_t afterClear = markCount + AllocateStageDispatches + 1u;
            const RHI::ResourceState uav = RHI::ResourceState::UnorderedAccess;
            assert(HasBufferBarrierAt(commandList, "VSM_RequestBits", uav, uav, afterMark));
            assert(HasBufferBarrierAt(commandList, "VSM_FreeList", uav, uav, afterFreeReset));
            assert(HasBufferBarrierAt(commandList, "VSM_FreeList", uav, uav, afterAllocate));
            assert(HasBufferBarrierAt(commandList, "VSM_PageTable", uav, uav, afterAllocate));
            assert(HasBufferBarrierAt(commandList, "VSM_Stats", uav, uav, afterAllocate));
            assert(HasBufferBarrierAt(commandList, "VSM_DirtyList", uav, uav, afterAllocate));
            assert(HasBufferBarrierAt(commandList, "VSM_DirtyList", uav, uav, afterFinalize));
            assert(HasBufferBarrierAt(commandList, "VSM_DirtyList", uav, RHI::ResourceState::GenericRead, afterFinalize));
            assert(HasBufferBarrierAt(commandList, "VSM_DirtyList", RHI::ResourceState::GenericRead, uav, afterClear));
            assert(HasBufferBarrierAt(commandList, "VSM_PhysicalPool", uav, uav, afterClear));
            // 割り当ての前に、要求のビット列は 0 で埋められ（印付けの前）、最初のフレームは前フレームの表が無いのでページの表も 0 で埋められ、
            // 統計は毎フレーム 0 で埋められる
            assert(commandList.VsmFills.size() == 4);
            assert(IsDebugName(commandList.VsmFills[1].BufferName, "VSM_RequestBits"));
            assert(IsDebugName(commandList.VsmFills[2].BufferName, "VSM_PageTable"));
            assert(IsDebugName(commandList.VsmFills[3].BufferName, "VSM_Stats"));

            ShutdownVsmRun(run, pass);
        }
    }

#if NORVES_ENABLE_LOGGING
    // 統計の枠へ、GPU が書き終えた体の値を直接書く（偽の装置のコピーは中身を写さない）
    void WriteVsmReadbackStats(const VirtualShadowMapPass& pass, uint32_t requested, uint32_t allocated, uint32_t overflow, uint32_t levels)
    {
        for (uint32_t slotIndex = 0; slotIndex < VirtualShadowMapPass::StatsReadbackSlotCount; ++slotIndex)
        {
            const RHI::BufferPtr& buffer = pass.GetStatsReadbackBuffer(slotIndex);
            assert(buffer);
            uint32_t* words = reinterpret_cast<uint32_t*>(static_cast<FakeBuffer*>(buffer.get())->MappedBytes.data());
            words[VirtualShadowMap::StatRequested] = requested;
            words[VirtualShadowMap::StatAllocated] = allocated;
            words[VirtualShadowMap::StatOverflow] = overflow;
            words[VirtualShadowMap::StatLevelsUsed] = levels;
        }
    }

    // 統計の読み戻しは、Execute の回数ではなくフレームの通し番号で数える。通し番号の差が 2 以上で、書いたフレームの完了が
    // 確かめられた枠だけを読む（飛行中が 2 枠のこの場面では、フレーム k がフレーム k - 2 の枠を読む）。
    // 同じフレームの複数のビューポートは提出前の枠を読まない。
    // 値が変わったときと、変わらなくても 60 回読むごとに VSM_PAGES を出す。統計のコピー元には TransferSrc が要る
    void TestVirtualShadowMapPassReadsStatsOnlyAfterFrameFence()
    {
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);

        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context));
        assert(pass.IsActive());

        // 統計は読み戻しのコピー元になるので TransferSrc を持ち、読み戻し先はコピー先になる
        const BufferCreationRecord* statsRecord = FindBufferCreation(*run.Device, "VSM_Stats");
        assert(statsRecord != nullptr);
        assert((statsRecord->Desc.Usage & RHI::ResourceUsage::TransferSrc) == RHI::ResourceUsage::TransferSrc);
        const BufferCreationRecord* readbackRecord = FindBufferCreation(*run.Device, "VSM_StatsReadback");
        assert(readbackRecord != nullptr);
        assert((readbackRecord->Desc.Usage & RHI::ResourceUsage::TransferDst) == RHI::ResourceUsage::TransferDst);

        const char* const logA = "VSM_PAGES requested=5 allocated=4 overflow=1 levels_used=0x3";
        const char* const logB = "VSM_PAGES requested=6 allocated=6 overflow=0 levels_used=0x7";
        uint64_t generation = 0;

        // 枠が前のフレームの値を持っているものとして、同じフレームの 3 つのビューポートが読まないことを確かめる
        // （Execute の回数で数えると、3 回目が 1 回目の提出前の枠を読む）
        WriteVsmReadbackStats(pass, 5, 4, 1, 0x3);
        for (uint32_t viewport = 0; viewport < 3u; ++viewport)
        {
            RunVsmViewport(run, pass, 0, generation++, true);
            assert(logs.Count("VSM_PAGES") == 0);
        }
        // フレーム 1（別の飛行中の番号）: 枠にまだ何も写していないので読まない
        RunVsmViewport(run, pass, 1, generation++, true);
        assert(logs.Count("VSM_PAGES") == 0);
        // フレーム 2（フレーム 0 と同じ番号）: フレーム 0 の枠を読む（初回は必ず出す）
        RunVsmViewport(run, pass, 2, generation++, true);
        assert(logs.Count("VSM_PAGES") == 1 && logs.Count(logA) == 1);
        // フレーム 3: フレーム 1 の枠を読む。値は変わらず、60 回に届かないので出さない
        RunVsmViewport(run, pass, 3, generation++, true);
        assert(logs.Count("VSM_PAGES") == 1);

        // 値が変わったら次に読んだときに出す。フレーム 4 がフレーム 2 の枠を読む
        WriteVsmReadbackStats(pass, 6, 6, 0, 0x7);
        RunVsmViewport(run, pass, 4, generation++, true);
        assert(logs.Count("VSM_PAGES") == 2 && logs.Count(logB) == 1);

        // 変わらない間は出さず、出してから 60 回目の読み取りで出す（フレーム 4 の次から数えて 59 回までは出ない）
        for (uint64_t frame = 5; frame <= 63; ++frame)
        {
            RunVsmViewport(run, pass, frame, generation++, true);
            assert(logs.Count("VSM_PAGES") == 2);
        }
        RunVsmViewport(run, pass, 64, generation++, true);
        assert(logs.Count("VSM_PAGES") == 3 && logs.Count(logB) == 2);

        ShutdownVsmRun(run, pass);
        logger.RemoveSink(&logs);
        logger.Shutdown();
    }

    // 統計の読み戻しは、スワップチェーンの飛行中のフレームの数に依らず、通し番号の差が 2 以上で、かつ書いたフレームの
    // GPU の完了が確かめられた枠だけを読む。製品は飛行中が 1 枠（翌フレームに読むと遅れが足りない）、3 枠以上では差が 2 でも
    // 書いたフレームがまだ終わっていない。GPU が飛行中の数より遅れて完了するときも、完了まで読まない
    void RunVsmStatsReadbackCase(uint32_t framesInFlight, uint32_t completedLag, uint64_t expectedFirstReadFrame)
    {
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);

        VsmRun run;
        run.FramesInFlight = framesInFlight;
        run.CompletedLag = completedLag;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context));
        assert(pass.IsActive());

        const char* const logA = "VSM_PAGES requested=5 allocated=4 overflow=1 levels_used=0x3";
        WriteVsmReadbackStats(pass, 5, 4, 1, 0x3);
        uint64_t generation = 0;
        for (uint64_t frame = 0; frame < expectedFirstReadFrame; ++frame)
        {
            RunVsmViewport(run, pass, frame, generation++, true);
            assert(logs.Count("VSM_PAGES") == 0);
        }
        // 最初に読むフレーム: 通し番号の差が 2 以上で、書いたフレーム（フレーム 0）の完了が確かめられている
        RunVsmViewport(run, pass, expectedFirstReadFrame, generation++, true);
        assert(logs.Count("VSM_PAGES") == 1 && logs.Count(logA) == 1);

        ShutdownVsmRun(run, pass);
        logger.RemoveSink(&logs);
        logger.Shutdown();
    }

    void TestVirtualShadowMapPassStatsReadbackAcrossFlightCounts()
    {
        // 飛行中が 1 枠（製品）: フレーム 1 は翌フレームなので読まない。フレーム 2 が、フレーム 0 の統計を読む
        RunVsmStatsReadbackCase(1, 0, 2);
        // 飛行中が 2 枠: フレーム 2 が、フレーム 0 の統計を読む
        RunVsmStatsReadbackCase(2, 0, 2);
        // 飛行中が 3 枠: フレーム 2 は差が 2 でも、フレーム 0 の完了が確かめられていない。フレーム 3 が読む
        RunVsmStatsReadbackCase(3, 0, 3);
        // 飛行中が 4 枠（上限）: フレーム 4 が、フレーム 0 の統計を読む
        RunVsmStatsReadbackCase(4, 0, 4);
        // GPU が飛行中の数（2）より遅れて 6 フレーム後に完了する: フレーム 6 まで読まない（差が 2 以上でも完了を待つ）
        RunVsmStatsReadbackCase(2, 6, 6);
    }

    // 展開の統計の語（塊・インスタンス・溢れ）は、投影物を描かない間（0 のまま）は VSM_RASTER を出さず、
    // 0 以外になったとき・値が変わったとき・変わらなくても 60 回読むごとに出す
    void TestVirtualShadowMapPassReportsRasterStats()
    {
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);

        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context));
        assert(pass.IsActive());

        auto writeRasterStats = [&pass](uint32_t chunks, uint32_t instances, uint32_t overflow) {
            for (uint32_t slotIndex = 0; slotIndex < VirtualShadowMapPass::StatsReadbackSlotCount; ++slotIndex)
            {
                const RHI::BufferPtr& buffer = pass.GetStatsReadbackBuffer(slotIndex);
                assert(buffer);
                uint32_t* words = reinterpret_cast<uint32_t*>(static_cast<FakeBuffer*>(buffer.get())->MappedBytes.data());
                words[VirtualShadowMap::StatRasterChunks] = chunks;
                words[VirtualShadowMap::StatRasterInstances] = instances;
                words[VirtualShadowMap::StatRasterOverflow] = overflow;
            }
        };

        uint64_t generation = 0;
        // 投影物を描かない間（語が 0 のまま）は、60 回を超えて読んでも出さない
        writeRasterStats(0, 0, 0);
        for (uint64_t frame = 0; frame < 70; ++frame)
        {
            RunVsmViewport(run, pass, frame, generation++, true);
            assert(logs.Count("VSM_RASTER") == 0);
        }
        // 0 以外になったら次に読んだときに出す
        writeRasterStats(3, 40, 0);
        RunVsmViewport(run, pass, 70, generation++, true);
        assert(logs.Count("VSM_RASTER") == 1 && logs.Count("VSM_RASTER chunks=3 instances=40 overflow=0") == 1);
        // 変わらない間は出さず、出してから 60 回目の読み取りで出す
        for (uint64_t frame = 71; frame <= 129; ++frame)
        {
            RunVsmViewport(run, pass, frame, generation++, true);
            assert(logs.Count("VSM_RASTER") == 1);
        }
        RunVsmViewport(run, pass, 130, generation++, true);
        assert(logs.Count("VSM_RASTER") == 2);
        // 溢れが出たら次に読んだときに出す
        writeRasterStats(3, 40, 7);
        RunVsmViewport(run, pass, 131, generation++, true);
        assert(logs.Count("VSM_RASTER") == 3 && logs.Count("VSM_RASTER chunks=3 instances=40 overflow=7") == 1);

        ShutdownVsmRun(run, pass);
        logger.RemoveSink(&logs);
        logger.Shutdown();
    }
#endif

    // 展開の統計の報告の決め方（ログの出力に依らない）: 0 のままなら出さず、0 以外になる・値が変わる・60 回報告するごとに出す。
    // 一度出したら、0 に戻ったときも出す
    void TestVirtualShadowMapRasterStatsReporterDecidesWhenToLog()
    {
        VirtualShadowMapRasterStatsReporter reporter;
        for (uint32_t index = 0; index < 100u; ++index)
        {
            assert(!reporter.Report(0, 0, 0));
        }
        assert(reporter.Report(1, 2, 0));
        for (uint32_t index = 0; index + 1u < VirtualShadowMapRasterStatsReporter::LogIntervalReports; ++index)
        {
            assert(!reporter.Report(1, 2, 0));
        }
        assert(reporter.Report(1, 2, 0));
        assert(!reporter.Report(1, 2, 0));
        assert(reporter.Report(1, 2, 5));
        assert(reporter.Report(0, 0, 0));
        assert(!reporter.Report(0, 0, 0));
    }

    // ãã¼ã«ã®ãã¼ã¸ã®æ°ã¯、要求（0 は既定の 5120）を装置の maxStorageBufferRange に収まる数へ締める（不明は Vulkan の保証する最小値 2^27）。
    // 512 ページちょうどは作れ、511 ページしか取れない装置は作れない。表の欄の幅（20 ビット）も超えない
    void TestVirtualShadowMapPoolPlanClampsToDeviceLimit()
    {
        auto makeCaps = [](bool bFragment, bool bBda, uint64_t range)
        {
            RHI::DeviceCapabilities caps;
            caps.bFragmentStoresAndAtomics = bFragment;
            caps.bBufferDeviceAddress = bBda;
            caps.MaxStorageBufferRange = range;
            return caps;
        };
        const uint64_t unlimited = 0xFFFFFFFFull;

        VirtualShadowMap::PoolPlan plan = VirtualShadowMap::PlanPool(makeCaps(true, true, unlimited), 0);
        assert(plan.IsSupported() && plan.Pages == VirtualShadowMap::DEFAULT_POOL_PAGES && plan.Pages == 5120);
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, unlimited), 100);
        assert(plan.IsSupported() && plan.Pages == 100);
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, unlimited), 100000);
        assert(plan.IsSupported() && plan.Pages == 65535);
        // 不明（0）は保証された最小値 2^27 = 128 MiB = 2048 ページ
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, 0), 0);
        assert(plan.IsSupported() && plan.Pages == 2048);
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, 64ull * 1024ull * 1024ull), 4096);
        assert(plan.IsSupported() && plan.Pages == 1024);
        // 境界: 512 ページちょうどは作れ、1 バイト足りないと 511 ページで作れない
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, 512ull * 65536ull), 4096);
        assert(plan.IsSupported() && plan.Pages == 512);
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, 512ull * 65536ull - 1ull), 4096);
        assert(!plan.IsSupported() && plan.Reason == VirtualShadowMap::FallbackReason::PoolSize);
        // 理由の優先: 断片の機能 → BDA → プールの大きさ
        assert(VirtualShadowMap::PlanPool(makeCaps(false, false, 0), 0).Reason == VirtualShadowMap::FallbackReason::FragmentAtomics);
        assert(VirtualShadowMap::PlanPool(makeCaps(true, false, 0), 0).Reason == VirtualShadowMap::FallbackReason::BufferDeviceAddress);
        // 表の欄の幅（20 ビット）を超える束縛の上限でも、ページの数は欄に収まる
        plan = VirtualShadowMap::PlanPool(makeCaps(true, true, ~0ull), 2000000);
        assert(plan.IsSupported() && plan.Pages == VirtualShadowMap::MAX_POOL_PAGES);
        // 理由の名前（VSM_FALLBACK reason= の値）
        assert(std::strcmp(VirtualShadowMap::FallbackReasonName(VirtualShadowMap::FallbackReason::FragmentAtomics), "fragment_atomics") == 0);
        assert(std::strcmp(VirtualShadowMap::FallbackReasonName(VirtualShadowMap::FallbackReason::BufferDeviceAddress), "bda") == 0);
        assert(std::strcmp(VirtualShadowMap::FallbackReasonName(VirtualShadowMap::FallbackReason::PoolSize), "pool_size") == 0);

        // 実際の確保も装置の上限に収まる（64 MiB の上限 → 1024 ページ = 64 MiB）
        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 64ull * 1024ull * 1024ull);
        InitializeVsmRun(run);
        VirtualShadowMapPass pass(4096);
        assert(pass.Initialize(run.Context));
        assert(pass.IsActive() && pass.GetPoolPages() == 1024);
        const BufferCreationRecord* poolRecord = FindBufferCreation(*run.Device, "VSM_PhysicalPool");
        assert(poolRecord != nullptr && poolRecord->Desc.Size == 64ull * 1024ull * 1024ull);
        ShutdownVsmRun(run, pass);
    }


    // ========================================
    // 影を落とす投影物の塊の記録（VirtualShadowMapCasters.h。VSM の手続きメッシュ・スキニング）
    // ========================================

    VirtualShadowMapClipmap MakeCasterClipmap()
    {
        VirtualShadowMapClipmap clipmap = BuildVirtualShadowMapClipmap(
            NorvesLib::Math::Vector3(0.35f, -0.8f, 0.45f), 1u, NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), VirtualShadowMapClipmapSettings{});
        assert(clipmap.bEnabled && clipmap.LevelCount == VirtualShadowMap::LEVEL_COUNT);
        return clipmap;
    }

    VirtualShadowMap::CasterBounds MakeCubeBounds(float x, float y, float z, float half)
    {
        VirtualShadowMap::CasterBounds bounds;
        const float center[3] = {x, y, z};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            bounds.Min[axis] = center[axis] - half;
            bounds.Max[axis] = center[axis] + half;
        }
        return bounds;
    }

    // 境界がどの段の範囲に入るかは、展開が使う範囲（段ごとの絶対のページの範囲）と同じ判定になる。
    // ライトの右向きに動かした小さな境界で、段ごとの結果が VirtualShadowMapLevelFindPage（中心の位置が範囲に入るか）と一致し、
    // 範囲の外（どの段にも入らない）・クリップマップが無効・境界が有限でないときは 0
    void TestVirtualShadowMapCasterLevelMaskMatchesLevelRanges()
    {
        const VirtualShadowMapClipmap clipmap = MakeCasterClipmap();
        const uint32_t allLevels = (1u << VirtualShadowMap::LEVEL_COUNT) - 1u;

        // 原点のそば: すべての段
        assert(VirtualShadowMap::LevelMaskForBounds(clipmap, MakeCubeBounds(0.0f, 0.0f, 0.0f, 0.1f)) == allLevels);
        // 段 0（幅 4 m）の外で段 1（幅 8 m）の内側: 段 0 だけ外れ、粗い段はすべて入る
        {
            const NorvesLib::Math::Vector3 right = clipmap.LightRight;
            const uint32_t mask = VirtualShadowMap::LevelMaskForBounds(clipmap, MakeCubeBounds(right.x * 3.0f, right.y * 3.0f, right.z * 3.0f, 0.01f));
            assert((mask & 1u) == 0u && (mask & 2u) != 0u && (mask >> 1u) == (allLevels >> 1u));
        }
        // 最も粗い段（幅 2048 m）の外: どの段にも入らない
        {
            const NorvesLib::Math::Vector3 right = clipmap.LightRight;
            assert(VirtualShadowMap::LevelMaskForBounds(clipmap, MakeCubeBounds(right.x * 5000.0f, right.y * 5000.0f, right.z * 5000.0f, 1.0f)) == 0u);
        }
        // 大きな境界はすべての段にかかる（境界の半幅が範囲を覆う）
        assert(VirtualShadowMap::LevelMaskForBounds(clipmap, MakeCubeBounds(5000.0f, 0.0f, 0.0f, 20000.0f)) == allLevels);

        // 右向きに掃引: 段ごとの結果が、点の位置が範囲に入るかの判定と一致する
        uint32_t checkedPoints = 0;
        for (float distance = -3000.0f; distance <= 3000.0f; distance += 7.31f)
        {
            const NorvesLib::Math::Vector3 position(clipmap.LightRight.x * distance, clipmap.LightRight.y * distance, clipmap.LightRight.z * distance);
            const uint32_t mask = VirtualShadowMap::LevelMaskForBounds(clipmap, MakeCubeBounds(position.x, position.y, position.z, 1.0e-4f));
            double lightX = 0.0;
            double lightY = 0.0;
            double lightDepth = 0.0;
            VirtualShadowMapWorldToLightSpace(clipmap, position, lightX, lightY, lightDepth);
            for (uint32_t level = 0; level < VirtualShadowMap::LEVEL_COUNT; ++level)
            {
                int64_t pageX = 0;
                int64_t pageY = 0;
                // 範囲の縁（ページの境目）の丸めで食い違いうる点は比べない（小さな境界の幅 1e-4 m が縁をまたぐ場合）
                const double pageMeters = static_cast<double>(clipmap.Levels[level].PageMeters);
                const double edgeX = std::abs(lightX / pageMeters - std::round(lightX / pageMeters)) * pageMeters;
                if (edgeX < 1.0e-3)
                {
                    continue;
                }
                const bool bInRange = VirtualShadowMapLevelFindPage(clipmap, level, lightX, lightY, pageX, pageY);
                assert(bInRange == (((mask >> level) & 1u) != 0u));
                ++checkedPoints;
            }
        }
        assert(checkedPoints > 500u);

        VirtualShadowMapClipmap disabled = clipmap;
        disabled.bEnabled = false;
        assert(VirtualShadowMap::LevelMaskForBounds(disabled, MakeCubeBounds(0.0f, 0.0f, 0.0f, 0.1f)) == 0u);
        VirtualShadowMap::CasterBounds notFinite = MakeCubeBounds(0.0f, 0.0f, 0.0f, 0.1f);
        notFinite.Max[1] = std::numeric_limits<float>::infinity();
        assert(VirtualShadowMap::LevelMaskForBounds(clipmap, notFinite) == 0u);
        VirtualShadowMap::CasterBounds inverted = MakeCubeBounds(0.0f, 0.0f, 0.0f, 0.1f);
        std::swap(inverted.Min[0], inverted.Max[0]);
        assert(VirtualShadowMap::LevelMaskForBounds(clipmap, inverted) == 0u);
    }

    // 手続きメッシュを 128 三角形以下の塊（BuildMeshIndexChunks）に分け、塊ごとのローカルの境界を決める。
    // ブロックの境界があり頂点の基点が 0 のときは、メッシュの先頭から整列した 384 インデックスの境目で区切り、その塊のブロックの境界を使う。
    // 使えないとき（基点が 0 でない・ブロックの境界が無い・先頭が 3 の倍数でない）はメッシュ全体の境界で、境目で区切らない。
    // アドレス・メッシュの境界が無いときは塊を作らない
    void TestVirtualShadowMapCasterPlansProceduralChunks()
    {
        BoundingBox meshBounds{-10.0f, -10.0f, -10.0f, 10.0f, 10.0f, 10.0f};
        // 900 インデックス（300 三角形）= 384・384・132 の 3 ブロック。ブロックごとに別の境界
        const BoundingBox blocks[3] = {{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f, 3.0f, 3.0f, 3.0f}, {4.0f, 4.0f, 4.0f, 5.0f, 5.0f, 5.0f}};

        VirtualShadowMap::ProceduralDrawInput draw;
        draw.VertexAddress = 0x1000u;
        draw.IndexAddress = 0x2000u;
        draw.FirstIndex = 0u;
        draw.IndexCount = 900u;
        draw.MeshBounds = &meshBounds;
        draw.BlockBounds = blocks;
        draw.BlockBoundsCount = 3u;

        VirtualShadowMap::ProceduralPlanScratch scratch;
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> plan;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan));
        assert(plan.size() == 3u);
        assert(plan[0].FirstIndex == 0u && plan[0].TriangleCount == 128u && plan[0].LocalBounds.MaxX == 1.0f);
        assert(plan[1].FirstIndex == 384u && plan[1].TriangleCount == 128u && plan[1].LocalBounds.MaxX == 3.0f);
        assert(plan[2].FirstIndex == 768u && plan[2].TriangleCount == 44u && plan[2].LocalBounds.MaxX == 5.0f);

        // 範囲がブロックの途中から始まる（先頭 300 から 480 個）: 境目 384・768 で区切り、塊が 1 つのブロックの中に収まる
        draw.FirstIndex = 300u;
        draw.IndexCount = 480u;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan));
        assert(plan.size() == 3u);
        assert(plan[0].FirstIndex == 300u && plan[0].TriangleCount == 28u && plan[0].LocalBounds.MaxX == 1.0f);
        assert(plan[1].FirstIndex == 384u && plan[1].TriangleCount == 128u && plan[1].LocalBounds.MaxX == 3.0f);
        assert(plan[2].FirstIndex == 768u && plan[2].TriangleCount == 4u && plan[2].LocalBounds.MaxX == 5.0f);
        // 全部の三角形をちょうど 1 回ずつ覆う
        uint32_t covered = 0;
        for (const VirtualShadowMap::ProceduralChunkPlan& entry : plan)
        {
            covered += entry.TriangleCount * 3u;
        }
        assert(covered == 480u);

        // 頂点の基点が 0 でない: ブロックの境界は使えず、メッシュ全体の境界で、境目では区切らない（先頭から 384 ずつ）
        draw.FirstIndex = 300u;
        draw.IndexCount = 900u;
        draw.VertexOffset = 5u;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan));
        assert(plan.size() == 3u && plan[0].FirstIndex == 300u && plan[0].TriangleCount == 128u && plan[1].FirstIndex == 684u);
        for (const VirtualShadowMap::ProceduralChunkPlan& entry : plan)
        {
            assert(entry.LocalBounds.MinX == -10.0f && entry.LocalBounds.MaxX == 10.0f);
        }
        // ブロックの境界が無い・先頭が 3 の倍数でないときも、メッシュ全体の境界
        draw.VertexOffset = 0u;
        draw.BlockBounds = nullptr;
        draw.BlockBoundsCount = 0u;
        draw.FirstIndex = 0u;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan) && plan.size() == 3u && plan[1].LocalBounds.MaxX == 10.0f);
        draw.BlockBounds = blocks;
        draw.BlockBoundsCount = 3u;
        draw.FirstIndex = 1u;
        draw.IndexCount = 600u;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan) && plan[0].LocalBounds.MaxX == 10.0f);

        // 作れない入力: アドレスが無い・メッシュの境界が無い・三角形が無い
        draw.FirstIndex = 0u;
        draw.IndexCount = 900u;
        VirtualShadowMap::ProceduralDrawInput broken = draw;
        broken.VertexAddress = 0u;
        assert(!VirtualShadowMap::PlanProceduralChunks(broken, scratch, plan) && plan.empty());
        broken = draw;
        broken.IndexAddress = 0u;
        assert(!VirtualShadowMap::PlanProceduralChunks(broken, scratch, plan));
        broken = draw;
        broken.MeshBounds = nullptr;
        assert(!VirtualShadowMap::PlanProceduralChunks(broken, scratch, plan));
        broken = draw;
        broken.IndexCount = 2u;
        assert(!VirtualShadowMap::PlanProceduralChunks(broken, scratch, plan));
    }

    // 手続きメッシュの 1 インスタンスの記録: インスタンスの変換（列優先の 16 個の float）を記録の 3×4 の行へ写し、
    // 境界をローカルの境界×変換から求め、どの段の範囲にも入らない塊は省き、上限を超えたら書かずに数える
    void TestVirtualShadowMapCasterAppendsProceduralInstances()
    {
        const VirtualShadowMapClipmap clipmap = MakeCasterClipmap();
        BoundingBox meshBounds{-1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f};
        VirtualShadowMap::ProceduralDrawInput draw;
        draw.VertexAddress = 0xAAAA0000u;
        draw.IndexAddress = 0xBBBB0000u;
        draw.FirstIndex = 6u;
        draw.IndexCount = 12u;
        draw.VertexOffset = 9u;
        draw.MeshBounds = &meshBounds;
        VirtualShadowMap::ProceduralPlanScratch scratch;
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> plan;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan) && plan.size() == 1u);

        // x 方向 2 倍・並進 (0.5, 0.25, -0.125) の変換（行ベクトル規約のワールド行列をそのままシェーダーへ渡した並び）
        const NorvesLib::Math::Matrix4x4 matrix(2.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, 1.0f, 0.0f, 0.0f,
                                                0.0f, 0.0f, 1.0f, 0.0f,
                                                0.5f, 0.25f, -0.125f, 1.0f);
        float world[16] = {};
        NorvesLib::Math::MatrixUtils::CopyToShaderData(matrix, world);

        Container::VariableArray<VsmShadowChunk> chunks;
        VirtualShadowMap::CasterStats stats;
        VirtualShadowMap::AppendProceduralInstance(draw, plan, world, clipmap, chunks, stats);
        assert(chunks.size() == 1u && stats.ProceduralChunks == 1u && stats.ProceduralDraws == 1u && stats.CulledChunks == 0u);
        const VsmShadowChunk& chunk = chunks[0];
        assert(chunk.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk));
        assert(chunk.Record.TriangleCount == 4u && chunk.Record.FirstIndex == 6u && chunk.Record.VertexBase == 9u);
        assert(chunk.Record.VertexAddress == 0xAAAA0000u && chunk.Record.IndexAddress == 0xBBBB0000u);
        // 境界 = ローカルの境界 × 変換: x は 2 倍して 0.5 を足す
        assert(chunk.BoundsMin[0] == -1.5f && chunk.BoundsMax[0] == 2.5f);
        assert(chunk.BoundsMin[1] == -1.75f && chunk.BoundsMax[1] == 2.25f);
        assert(chunk.BoundsMin[2] == -3.125f && chunk.BoundsMax[2] == 2.875f);
        // 変換の行: ワールド x = 2·x + 0.5、y = y + 0.25、z = z − 0.125
        const float expectedRows[12] = {2.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.25f, 0.0f, 0.0f, 1.0f, -0.125f};
        assert(std::memcmp(chunk.World, expectedRows, sizeof(expectedRows)) == 0);

        // どの段の範囲にも入らない位置（ライトの右向きに 5000 m）は省く。メッシュ全体の境界で判定するので、塊の境界は見ない
        float farWorld[16] = {};
        std::memcpy(farWorld, world, sizeof(farWorld));
        farWorld[12] += clipmap.LightRight.x * 5000.0f;
        farWorld[13] += clipmap.LightRight.y * 5000.0f;
        farWorld[14] += clipmap.LightRight.z * 5000.0f;
        VirtualShadowMap::AppendProceduralInstance(draw, plan, farWorld, clipmap, chunks, stats);
        assert(chunks.size() == 1u && stats.CulledChunks == 1u && stats.ProceduralChunks == 1u);

        // 上限: 記録がいっぱいなら書かずに数える（省いた数には入れない）
        chunks.resize(VirtualShadowMap::MAX_CASTER_CHUNKS);
        VirtualShadowMap::AppendProceduralInstance(draw, plan, world, clipmap, chunks, stats);
        assert(chunks.size() == VirtualShadowMap::MAX_CASTER_CHUNKS && stats.DroppedChunks == 1u && stats.ProceduralChunks == 1u);

        // 空の計画・メッシュの境界が無い入力は何もしない
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> emptyPlan;
        VirtualShadowMap::CasterStats untouched;
        Container::VariableArray<VsmShadowChunk> empty;
        VirtualShadowMap::AppendProceduralInstance(draw, emptyPlan, world, clipmap, empty, untouched);
        assert(empty.empty() && untouched == VirtualShadowMap::CasterStats{});
    }

    // スキニングの 1 インスタンスの記録: 塊は同じ描画の境界を持ち、頂点のアドレスはインスタンスの先頭、頂点の基点は 0、変換は単位行列。
    // 範囲に入らない描画は塊ごと省き、上限を超えたら書かずに数える
    void TestVirtualShadowMapCasterAppendsSkinnedInstances()
    {
        const VirtualShadowMapClipmap clipmap = MakeCasterClipmap();
        Container::VariableArray<MeshIndexChunk> meshChunks;
        meshChunks.push_back(MeshIndexChunk{0u, 384u});
        meshChunks.push_back(MeshIndexChunk{384u, 30u});

        Container::VariableArray<VsmShadowChunk> chunks;
        VirtualShadowMap::CasterStats stats;
        const VirtualShadowMap::CasterBounds bounds = MakeCubeBounds(0.25f, 0.5f, 0.75f, 0.5f);
        VirtualShadowMap::AppendSkinnedInstance(0x3000u, 0x4000u, bounds, meshChunks, clipmap, chunks, stats);
        assert(chunks.size() == 2u && stats.SkinnedChunks == 2u && stats.SkinnedDraws == 1u);
        for (const VsmShadowChunk& chunk : chunks)
        {
            assert(chunk.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::SkinnedChunk));
            assert(chunk.Record.VertexBase == 0u && chunk.Record.VertexAddress == 0x3000u && chunk.Record.IndexAddress == 0x4000u);
            assert(chunk.BoundsMin[0] == -0.25f && chunk.BoundsMax[0] == 0.75f && chunk.BoundsMin[2] == 0.25f && chunk.BoundsMax[2] == 1.25f);
            const float identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
            assert(std::memcmp(chunk.World, identity, sizeof(identity)) == 0);
        }
        assert(chunks[0].Record.TriangleCount == 128u && chunks[0].Record.FirstIndex == 0u);
        assert(chunks[1].Record.TriangleCount == 10u && chunks[1].Record.FirstIndex == 384u);

        // 範囲の外: 塊ごと省く
        const VirtualShadowMap::CasterBounds farBounds = MakeCubeBounds(5000.0f * clipmap.LightRight.x, 5000.0f * clipmap.LightRight.y, 5000.0f * clipmap.LightRight.z, 1.0f);
        VirtualShadowMap::AppendSkinnedInstance(0x3000u, 0x4000u, farBounds, meshChunks, clipmap, chunks, stats);
        assert(chunks.size() == 2u && stats.CulledChunks == 2u && stats.SkinnedDraws == 1u);

        // 上限
        chunks.resize(VirtualShadowMap::MAX_CASTER_CHUNKS - 1u);
        VirtualShadowMap::AppendSkinnedInstance(0x3000u, 0x4000u, bounds, meshChunks, clipmap, chunks, stats);
        assert(chunks.size() == VirtualShadowMap::MAX_CASTER_CHUNKS && stats.DroppedChunks == 1u);
    }

    // 記録は、境界がかかる段の集合（LevelMask）を持つ。展開はこの集合の外の段を処理しない。
    // 既定（CPU が絞らない塊）は全段。手続き・スキニングとも、境界から LevelMaskForBounds が決めた集合を書く
    void TestVirtualShadowMapCasterRecordsLevelMask()
    {
        const VirtualShadowMapClipmap clipmap = MakeCasterClipmap();
        assert(VsmShadowChunk{}.LevelMask == 0xFFFFFFFFu);

        // 段 0（幅 4 m）の外で段 1 の内側に置いた、小さな四角形とスキニングの境界
        const NorvesLib::Math::Vector3 right = clipmap.LightRight;
        const VirtualShadowMap::CasterBounds placed = MakeCubeBounds(right.x * 3.0f, right.y * 3.0f, right.z * 3.0f, 0.01f);
        const uint32_t expectedMask = VirtualShadowMap::LevelMaskForBounds(clipmap, placed);
        assert((expectedMask & 1u) == 0u && (expectedMask & 2u) != 0u);

        BoundingBox meshBounds{-0.01f, -0.01f, -0.01f, 0.01f, 0.01f, 0.01f};
        VirtualShadowMap::ProceduralDrawInput draw;
        draw.VertexAddress = 0x1000u;
        draw.IndexAddress = 0x2000u;
        draw.IndexCount = 6u;
        draw.MeshBounds = &meshBounds;
        VirtualShadowMap::ProceduralPlanScratch scratch;
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> plan;
        assert(VirtualShadowMap::PlanProceduralChunks(draw, scratch, plan));
        const NorvesLib::Math::Matrix4x4 matrix = MakeMarkedMatrix(right.x * 3.0f, right.y * 3.0f, right.z * 3.0f);
        float world[16] = {};
        NorvesLib::Math::MatrixUtils::CopyToShaderData(matrix, world);
        Container::VariableArray<VsmShadowChunk> chunks;
        VirtualShadowMap::CasterStats stats;
        VirtualShadowMap::AppendProceduralInstance(draw, plan, world, clipmap, chunks, stats);
        assert(chunks.size() == 1u && chunks[0].LevelMask == expectedMask);

        Container::VariableArray<MeshIndexChunk> meshChunks;
        meshChunks.push_back(MeshIndexChunk{0u, 3u});
        VirtualShadowMap::AppendSkinnedInstance(0x3000u, 0x4000u, placed, meshChunks, clipmap, chunks, stats);
        assert(chunks.size() == 2u && chunks[1].LevelMask == expectedMask);

        // 原点のそばはすべての段
        const uint32_t allLevels = (1u << VirtualShadowMap::LEVEL_COUNT) - 1u;
        VirtualShadowMap::AppendSkinnedInstance(0x3000u, 0x4000u, MakeCubeBounds(0.0f, 0.0f, 0.0f, 0.1f), meshChunks, clipmap, chunks, stats);
        assert(chunks.size() == 3u && chunks[2].LevelMask == allLevels);
    }

    // FakeBuffer の Update に渡された塊の記録（VsmShadowChunk の並び）を取り出す
    Container::VariableArray<VsmShadowChunk> ReadUploadedChunks(const RHI::IBuffer* buffer)
    {
        const FakeBuffer* fake = static_cast<const FakeBuffer*>(buffer);
        assert(fake != nullptr && fake->LastUpdateBytes.size() % sizeof(VsmShadowChunk) == 0u);
        Container::VariableArray<VsmShadowChunk> chunks;
        chunks.resize(fake->LastUpdateBytes.size() / sizeof(VsmShadowChunk));
        if (!chunks.empty())
        {
            std::memcpy(chunks.data(), fake->LastUpdateBytes.data(), fake->LastUpdateBytes.size());
        }
        return chunks;
    }

    // 手続きメッシュとスキニングを足した、vsm の構成の 1 つのシーン（スキニングの計算 → 影の塊の記録 → 展開 → 描画）
    struct VsmCasterScene
    {
        VsmRun Run;
        RenderResources Resources;
        SkinningComputePass Skinning;
        VirtualShadowMapPass Pass;
        /** @brief 描画コマンドの一覧（スキニングの 1 件だけ。手続きメッシュは描画コマンドに載せず、プロキシから集める） */
        Container::VariableArray<DrawCommand> AllCommands;
        Container::VariableArray<Container::TSharedPtr<const SkinnedMeshFrameLease>> SkinnedLeases;
        Container::VariableArray<MeshProxy> MeshProxies;
        Container::VariableArray<SkinnedMeshProxy> SkinnedProxies;
        MeshDataHandle Mesh;
    };

    // 4 頂点の四角形（XZ 平面、半幅 0.4）を登録した手続きメッシュのプロキシ 4 件（MeshProxies）と、スキニングの描画 1 件（AllCommands）を足す。
    // 手続きメッシュは、主カメラの錐台で省かれた後の描画コマンドの一覧ではなく、カリング前のプロキシの一覧から集める:
    //   A: 影を落とす。プロキシ 2 つ（原点の近くの (0.5, 0, 0.5) と (-0.5, 0, 0.5)）
    //   B: 影を落とす。ライトの右向きに 5000 m（どの段の範囲にも入らない）
    //   C: 影を落とさない
    //   スキニング: 影を落とす。3 頂点・三角形 1 つ。描画の境界は原点の近く
    void BuildVsmCasterScene(VsmCasterScene& scene, bool bWithClipmap)
    {
        VsmRun& run = scene.Run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        if (bWithClipmap)
        {
            run.Context.PhysicalLighting.SunClipmap = MakeCasterClipmap();
        }
        assert(scene.Resources.Initialize(run.Device));
        scene.Resources.SkinnedMeshes().BeginFrame(0);
        run.Context.Resources.Meshes = &scene.Resources.Meshes();
        run.Context.SkinnedMeshes = &scene.Resources.SkinnedMeshes();

        const Mesh3DVertex vertices[4] = {
            {{-0.4f, 0.0f, -0.4f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.4f, 0.0f, -0.4f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
            {{0.4f, 0.0f, 0.4f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-0.4f, 0.0f, 0.4f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        };
        const uint32_t indices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
        scene.Mesh.Id = 8801;
        assert(scene.Resources.Meshes().Register(scene.Mesh, vertices, sizeof(vertices), indices, 6));

        const auto addMeshProxy = [&](float x, float y, float z, bool bCastShadow) {
            MeshProxy meshProxy;
            meshProxy.ComponentId = 100 + scene.MeshProxies.size();
            meshProxy.MeshHandle = scene.Mesh;
            meshProxy.WorldTransform = MakeMarkedMatrix(x, y, z);
            meshProxy.bCastShadow = bCastShadow;
            scene.MeshProxies.push_back(meshProxy);
        };
        const NorvesLib::Math::Vector3 right = run.Context.PhysicalLighting.SunClipmap.LightRight;
        addMeshProxy(0.5f, 0.0f, 0.5f, true);
        addMeshProxy(-0.5f, 0.0f, 0.5f, true);
        addMeshProxy(right.x * 5000.0f, right.y * 5000.0f, right.z * 5000.0f, true);
        addMeshProxy(0.0f, 0.5f, 0.0f, false);

        // スキニングの描画 1 件（SkinningComputePass が頂点を変形する）。持ち主のプロキシ（ComponentId 77）は影を落とし、境界は原点の近く
        Container::VariableArray<SkinnedMeshVertex> skinVertices;
        skinVertices.resize(3);
        for (SkinnedMeshVertex& vertex : skinVertices)
        {
            vertex.BoneWeights[0] = 1.0f;
        }
        Container::VariableArray<uint32_t> skinIndices;
        skinIndices.push_back(0u);
        skinIndices.push_back(1u);
        skinIndices.push_back(2u);
        auto assetLease = Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{5, 1}, std::move(skinVertices), std::move(skinIndices));
        scene.SkinnedLeases.push_back(Container::MakeShared<SkinnedMeshFrameLease>(assetLease));
        DrawCommand skinned;
        skinned.Draw.PayloadKind = DrawPayloadKind::Skinned;
        skinned.Draw.ObjectId = 15;
        skinned.Draw.SourceMeshComponentId = 77;
        skinned.Draw.WorldMatrix = MakeMarkedMatrix(-0.5f, 0.0f, 0.25f);
        skinned.Skinned.FrameLeaseIndex = 0;
        skinned.Skinned.BonePalette.push_back(MakeMarkedMatrix(0.0f, 0.0f, 0.0f));
        scene.AllCommands.push_back(skinned);
        SkinnedMeshProxy proxy;
        proxy.ComponentId = 77;
        proxy.bCastShadow = true;
        proxy.bHasAnimatedBounds = true;
        proxy.AnimatedBounds.Min = NorvesLib::Math::Vector3(-0.5f, 0.0f, -0.5f);
        proxy.AnimatedBounds.Max = NorvesLib::Math::Vector3(0.5f, 1.0f, 0.5f);
        proxy.WorldTransform = MakeMarkedMatrix(-0.5f, 0.0f, 0.25f);
        scene.SkinnedProxies.push_back(proxy);

        ViewRenderContext& context = run.Context;
        context.SnapshotDrawCommands = DrawCommandView::FromArray(scene.AllCommands);
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(scene.AllCommands);
        context.SnapshotMeshProxies = &scene.MeshProxies;
        context.SnapshotSkinnedMeshFrameLeases = &scene.SkinnedLeases;
        context.SnapshotSkinnedMeshProxies = &scene.SkinnedProxies;

        scene.Skinning.SetEnabled(true);
        scene.Skinning.SetShadowCasterOutput(true);
        assert(scene.Skinning.Initialize(context));
        scene.Pass.SetSkinningComputePass(&scene.Skinning);
        assert(scene.Pass.Initialize(context));
        assert(scene.Pass.IsActive());
    }

    // 1 つのビューポートの Execute を回す（スキニングの計算 → 深度の入力 → VSM の順）。フレームとグラフの世代は別に数える
    void RunVsmCasterViewport(VsmCasterScene& scene, uint64_t frameIndex, uint64_t graphGeneration)
    {
        VsmRun& run = scene.Run;
        SetVsmFrame(run, frameIndex);
        run.Pool.EndFrame();
        run.Pool.BeginFrame(graphGeneration);
        run.Graph.BeginFrame(graphGeneration);
        run.Graph.AddPass(&scene.Skinning);
        run.Graph.AddPass(&run.Inputs);
        run.Graph.AddPass(&scene.Pass);
        assert(run.Graph.Compile(run.Context));
        const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
        assert(result.bSuccess);
    }

    void ShutdownVsmCasterScene(VsmCasterScene& scene)
    {
        scene.Pass.Shutdown();
        scene.Skinning.Shutdown();
        scene.Resources.Shutdown();
        scene.Run.ShaderMgr.Shutdown();
        scene.Run.Graph.Shutdown();
        scene.Run.Pool.EndFrame();
        scene.Run.Pool.Shutdown();
    }

    // vsm の構成の 1 フレーム: スキニングの計算（変形の dispatch 1 回）→ 印付け・割り当て・消去（4 回の dispatch と間接 dispatch）→
    // 投影物の展開（dispatch。グループ数 = 塊の数）→ 描画（render pass の中で塊ごとに 1 回の間接描画）の順に並ぶ。
    //  - 塊は、影を落とす手続きメッシュ（A の 2 インスタンス。範囲の外の B と、影を落とさない C は含まない）と、スキニング 1 つ。
    //  - 記録の作成（CPU）は、塊のバッファへ 3 件を書く（手続き 2・スキニング 1）。展開の前に塊・インスタンス・引数が Common → UnorderedAccess、
    //    展開の後に UnorderedAccess → GenericRead（物理ページは PixelShaderWrite）、描画の後に元へ戻り、最後は Common。
    //  - スキニングの頂点は、変形の後（VSM の前）から描画の最後まで GenericRead のまま読まれる。
    //  - 印付けをしない構成（クリップマップが無い）では、展開・描画は記録されない。
    void TestVirtualShadowMapPassRecordsCasterRasterAfterSkinning()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);
#endif

        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            RunVsmCasterViewport(scene, 0, 0);

            const FakeCommandList& commandList = scene.Run.CommandList;
            assert(scene.Pass.WasMarked() && scene.Pass.WasRasterRecorded() && scene.Pass.GetLastCasterChunkCount() == 3u);
            assert(scene.Skinning.GetInstances().size() == 1u);
            assert(scene.Skinning.GetInstances()[0].bOpaque && scene.Skinning.GetInstances()[0].bCastShadow);

            const char* expectedSequence = "DDDDDDDDDDDDJDBIIIE";
            assert(commandList.CallSequence.size() == std::strlen(expectedSequence));
            for (size_t index = 0; index < commandList.CallSequence.size(); ++index)
            {
                assert(commandList.CallSequence[index] == expectedSequence[index]);
            }
            // dispatch: 変形（頂点 3 つ = 1 グループ）・印付け・戻す・割り当て・締める・展開（塊 3 つ = 3 グループ）
            assert(commandList.DispatchGroups.size() == 13u);
            assert(commandList.DispatchGroups[0].X == 1u && commandList.DispatchGroups[1].X == 16u && commandList.DispatchGroups[1].Y == 8u);
            assert(commandList.DispatchGroups[12].X == 3u && commandList.DispatchGroups[12].Y == 1u && commandList.DispatchGroups[12].Z == 1u);
            assert(commandList.BeginRenderPassCount == 1u && commandList.EndRenderPassCount == 1u);
            // 間接描画: 塊ごとに 1 回（引数の先頭 = 頭の 4 語の後、塊ごとに 20 バイト、描画の数 1）
            assert(commandList.IndirectDraws.size() == 3u);
            for (uint32_t chunk = 0; chunk < 3u; ++chunk)
            {
                assert(commandList.IndirectDraws[chunk].OffsetBytes == 16u + chunk * 20u && commandList.IndirectDraws[chunk].MaxDrawCount == 1u);
            }

            // バリア（SequencePosition は、直前までに記録した B・E・D・I・J の数）
            const RHI::ResourceState common = RHI::ResourceState::Common;
            const RHI::ResourceState uav = RHI::ResourceState::UnorderedAccess;
            const RHI::ResourceState read = RHI::ResourceState::GenericRead;
            const size_t beforeExpand = 13u;
            const size_t afterExpand = 14u;
            const size_t afterDraw = 19u;
            for (const char* name : {"VsmRaster_Chunks", "VsmRaster_Instances", "VsmRaster_Draws"})
            {
                assert(HasBufferBarrierAt(commandList, name, common, uav, beforeExpand));
                assert(HasBufferBarrierAt(commandList, name, uav, read, afterExpand));
                assert(HasBufferBarrierAt(commandList, name, read, uav, afterDraw));
                assert(HasBufferBarrierAt(commandList, name, uav, common, afterDraw));
            }
            assert(HasBufferBarrierAt(commandList, "VSM_PhysicalPool", uav, RHI::ResourceState::PixelShaderWrite, afterExpand));
            assert(HasBufferBarrierAt(commandList, "VSM_PhysicalPool", RHI::ResourceState::PixelShaderWrite, uav, afterDraw));
            // スキニングの頂点は、変形の dispatch の後（位置 1）に GenericRead へ進み、VSM の記録の間は離れない
            const Container::VariableArray<BarrierEvent> skinnedBarriers = CollectBufferBarriers(commandList, "Skinning_CurrentVertices");
            bool bReadable = false;
            for (const BarrierEvent& barrier : skinnedBarriers)
            {
                bReadable = bReadable || (barrier.AfterState == read && barrier.SequencePosition <= 1u);
                assert(barrier.BeforeState != read);
            }
            assert(bReadable);
            // 変形した頂点の使用は、VSM のパス（3 番目）まで延びる（変形の直後に解放されて、別の資源に使い回されない）
            bool bLifetimeFound = false;
            for (const RGCompiledResourceLifetime& lifetime : scene.Run.Graph.GetCompiledResourceLifetimes())
            {
                if (lifetime.DebugName != nullptr && IsDebugName(lifetime.DebugName, "Skinning_CurrentVertices"))
                {
                    bLifetimeFound = true;
                    assert(lifetime.FirstUsePassIndex == 0u && lifetime.LastUsePassIndex == 2u);
                }
            }
            assert(bLifetimeFound);

            // 記録の作成: 塊のバッファへ 3 件（手続き 2・スキニング 1）を書く
            const Container::VariableArray<BarrierEvent> chunkBarriers = CollectBufferBarriers(commandList, "VsmRaster_Chunks");
            assert(!chunkBarriers.empty());
            const Container::VariableArray<VsmShadowChunk> uploaded = ReadUploadedChunks(chunkBarriers[0].Buffer);
            assert(uploaded.size() == 3u);
            const uint64_t meshVertexAddress = uploaded[0].Record.VertexAddress;
            const uint64_t meshIndexAddress = uploaded[0].Record.IndexAddress;
            assert(meshVertexAddress != 0u && meshIndexAddress != 0u);
            // 手続き: メッシュのバッファのアドレスと変換。インスタンス 0（+0.5, 0, +0.5）と 1（-0.5, 0, +0.5）
            for (uint32_t instance = 0; instance < 2u; ++instance)
            {
                const VsmShadowChunk& chunk = uploaded[instance];
                const float x = instance == 0u ? 0.5f : -0.5f;
                assert(chunk.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk));
                assert(chunk.Record.TriangleCount == 2u && chunk.Record.FirstIndex == 0u && chunk.Record.VertexBase == 0u);
                assert(chunk.Record.VertexAddress == meshVertexAddress && chunk.Record.IndexAddress == meshIndexAddress);
                // 原点のそばの境界はすべての段にかかる
                assert(chunk.LevelMask == (1u << VirtualShadowMap::LEVEL_COUNT) - 1u);
                assert(chunk.World[3] == x && chunk.World[7] == 0.0f && chunk.World[11] == 0.5f);
                assert(chunk.World[0] == 1.0f && chunk.World[5] == 1.0f && chunk.World[10] == 1.0f);
                assert(std::abs(chunk.BoundsMin[0] - (x - 0.4f)) < 1.0e-6f && std::abs(chunk.BoundsMax[0] - (x + 0.4f)) < 1.0e-6f);
                assert(chunk.BoundsMin[1] == 0.0f && chunk.BoundsMax[1] == 0.0f);
                assert(std::abs(chunk.BoundsMin[2] - 0.1f) < 1.0e-6f && std::abs(chunk.BoundsMax[2] - 0.9f) < 1.0e-6f);
            }
            // スキニング: 変形した頂点（インスタンスの先頭）・インデックス・描画の境界・単位行列
            const VsmShadowChunk& skinnedChunk = uploaded[2];
            assert(skinnedChunk.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::SkinnedChunk));
            assert(skinnedChunk.Record.TriangleCount == 1u && skinnedChunk.Record.FirstIndex == 0u && skinnedChunk.Record.VertexBase == 0u);
            assert(skinnedChunk.Record.VertexAddress == SkinningCurrentVerticesAddress && skinnedChunk.Record.IndexAddress != 0u);
            assert(std::abs(skinnedChunk.BoundsMin[0] - (-1.0f)) < 1.0e-6f && std::abs(skinnedChunk.BoundsMax[0] - 0.0f) < 1.0e-6f);
            assert(std::abs(skinnedChunk.BoundsMin[1] - 0.0f) < 1.0e-6f && std::abs(skinnedChunk.BoundsMax[1] - 1.0f) < 1.0e-6f);
            assert(std::abs(skinnedChunk.BoundsMin[2] - (-0.25f)) < 1.0e-6f && std::abs(skinnedChunk.BoundsMax[2] - 0.75f) < 1.0e-6f);
            assert(skinnedChunk.World[0] == 1.0f && skinnedChunk.World[3] == 0.0f && skinnedChunk.World[11] == 0.0f);

#if NORVES_ENABLE_LOGGING
            // 内訳は変わったときだけ 1 回出る
            assert(logs.Count("VSM_CASTERS") == 1);
            assert(logs.Count("VSM_CASTERS procedural_chunks=2 skinned_chunks=1 culled=1 dropped=0 skipped=0") == 1);
#endif
            ShutdownVsmCasterScene(scene);
        }

        // 主カメラの錐台で省かれた後の描画コマンドの一覧（現在のビューポートの一覧）が空でも、影を落とす手続きメッシュは
        // カリング前のプロキシから集める（錐台の外でも VSM の段の範囲に入る投影物の影を落とす）。スキニングの描画は一覧が空なので無い
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            ViewportRenderPlan viewportPlan;
            scene.Run.Context.CurrentViewport = &viewportPlan;
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Pass.WasRasterRecorded() && scene.Pass.GetLastCasterChunkCount() == 2u);
            assert(scene.Skinning.GetInstances().empty());
            ShutdownVsmCasterScene(scene);
        }

        // サブメッシュがあっても IndexCount が 0 なら、CSM と同じくメッシュ全体（先頭 0・頂点の基点 0・6 インデックス）として塊に分ける。
        // 影を落とすプロキシ A の 2 つを、IndexCount 0 のサブメッシュ 1 つに替える（頂点の基点・先頭は 0 以外にしても無視される）
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            for (uint32_t index = 0; index < 2u; ++index)
            {
                scene.MeshProxies[index].SubMeshCount = 1u;
                scene.MeshProxies[index].SubMeshes[0] = SubMeshRange{3u, 0u, 7u, 0u};
            }
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Pass.WasRasterRecorded() && scene.Pass.GetLastCasterChunkCount() == 3u);
            const Container::VariableArray<BarrierEvent> chunkBarriers = CollectBufferBarriers(scene.Run.CommandList, "VsmRaster_Chunks");
            assert(!chunkBarriers.empty());
            const Container::VariableArray<VsmShadowChunk> uploaded = ReadUploadedChunks(chunkBarriers[0].Buffer);
            assert(uploaded.size() == 3u);
            for (uint32_t instance = 0; instance < 2u; ++instance)
            {
                assert(uploaded[instance].Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk));
                assert(uploaded[instance].Record.TriangleCount == 2u && uploaded[instance].Record.FirstIndex == 0u && uploaded[instance].Record.VertexBase == 0u);
            }
            ShutdownVsmCasterScene(scene);
        }

        // 解決が使えず予備の GBuffer の描画へ戻るフレームでも、影を落とすスキニングは変形して影に描く。
        // 影を落とさない描画は変形しない。影の出力を切ると何も宣言しない（今までの動き）
        {
            VisibilityResolvePass unusableResolve;
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            scene.Skinning.SetResolvePass(&unusableResolve);
            assert(!unusableResolve.CanResolve(scene.Run.Device.get()));
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Skinning.GetInstances().size() == 1u && scene.Pass.GetLastCasterChunkCount() == 3u);
            ShutdownVsmCasterScene(scene);
        }
        {
            VisibilityResolvePass unusableResolve;
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            scene.Skinning.SetResolvePass(&unusableResolve);
            scene.AllCommands[0].Draw.bCastShadow = false;
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Skinning.GetInstances().empty() && scene.Pass.GetLastCasterChunkCount() == 2u);
            ShutdownVsmCasterScene(scene);
        }
        {
            VisibilityResolvePass unusableResolve;
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            scene.Skinning.SetResolvePass(&unusableResolve);
            scene.Skinning.SetShadowCasterOutput(false);
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Skinning.GetInstances().empty() && scene.Pass.GetLastCasterChunkCount() == 2u);
            ShutdownVsmCasterScene(scene);
        }

        // 半透明の一覧にあるスキニングの描画も、影の描画と同じ全描画の一覧から変形する（ID のラスタが描かないよう bOpaque は false）
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            scene.Run.Context.SnapshotOpaqueCommands = DrawCommandView{};
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Skinning.GetInstances().size() == 1u && !scene.Skinning.GetInstances()[0].bOpaque);
            assert(scene.Pass.GetLastCasterChunkCount() == 3u);
            ShutdownVsmCasterScene(scene);
        }

        // 同じフレームの 2 つ目のビューポートは別の塊のバッファへ書き（提出前の記録を上書きしない）、
        // 次のフレーム（別の飛行中の番号）は別の枠、同じ番号へ戻ったフレームは作り直さない
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            uint64_t generation = 0;
            RunVsmCasterViewport(scene, 0, generation++);
            RunVsmCasterViewport(scene, 0, generation++);
            assert(CountBufferCreations(*scene.Run.Device, "VsmRaster_Chunks") == 2);
            RunVsmCasterViewport(scene, 1, generation++);
            assert(CountBufferCreations(*scene.Run.Device, "VsmRaster_Chunks") == 3);
            RunVsmCasterViewport(scene, 2, generation++);
            RunVsmCasterViewport(scene, 3, generation++);
            assert(CountBufferCreations(*scene.Run.Device, "VsmRaster_Chunks") == 3);
            ShutdownVsmCasterScene(scene);
        }

        // クリップマップが無く印付けをしない構成: 割り当て済みのページが無いので、投影物を集めず展開・描画も記録しない
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, false);
            RunVsmCasterViewport(scene, 0, 0);
            const FakeCommandList& commandList = scene.Run.CommandList;
            assert(!scene.Pass.WasMarked() && !scene.Pass.WasRasterRecorded() && scene.Pass.GetLastCasterChunkCount() == 0u);
            // 変形（D）の後に、割り当て・消去だけ（戻す・割り当て・締める・間接）
            const char* expectedSequence = "DDDDDDDDDDDJ";
            assert(commandList.CallSequence.size() == std::strlen(expectedSequence));
            for (size_t index = 0; index < commandList.CallSequence.size(); ++index)
            {
                assert(commandList.CallSequence[index] == expectedSequence[index]);
            }
            assert(commandList.BeginRenderPassCount == 0u && commandList.IndirectDraws.empty());
            assert(CountBufferCreations(*scene.Run.Device, "VsmRaster_Chunks") == 0);
            ShutdownVsmCasterScene(scene);
        }

#if NORVES_ENABLE_LOGGING
        logger.RemoveSink(&logs);
        logger.Shutdown();
#endif
    }

    // ========================================
    // MegaGeometry の投影物のカリング（VirtualShadowMapMegaCull。vsm の構成だけ）
    // ========================================

    // 記述子セットの binding の番号に束縛されたバッファの名前（束縛が無ければ nullptr）
    const char* BoundBufferNameAt(const Container::VariableArray<BoundBufferName>& bindings, uint32_t binding)
    {
        for (const BoundBufferName& entry : bindings)
        {
            if (entry.Binding == binding)
            {
                return entry.Name;
            }
        }
        return nullptr;
    }

    // 主の経路（MegaGeometryPass）の記録を済ませた、vsm の構成のシーン。VsmCasterScene（手続きメッシュ・スキニングの投影物）に、
    // MegaGeometry の 3 インスタンスを足し、2 パスの遮蔽の主のカリング（1 回目 → 描画 → HZB 7 段 → 2 回目 → 描画）を同じコマンドリストへ先に記録する
    //   A: 影を落とす。境界（WorldBounds）は (1, 2, 3)・半径 4
    //   B: 影を落とさない
    //   C: 影を落とす。境界が無い（半径 0）ので、メッシュ全体のローカルの境界（中心 (0.5, 0.5, 0)・半径 1.25）をワールドの変換 (5, 0, 0) で移した球が使われる
    struct VsmMegaScene
    {
        VsmCasterScene Base;
        SharedResourceRegistry Shared;
        MegaGeometryPass Mega;
        Container::VariableArray<MegaGeometryProxy> Proxies;
        RHI::TexturePtr Textures[6];
        size_t MainSequenceLength = 0;
        size_t MainDispatchCount = 0;
        size_t MainBarrierCount = 0;
        size_t MainIndirectDrawCount = 0;
    };

    // extraCasters: 影を落とすインスタンスを、A と同じメッシュで何個足すか（CSM のインスタンスごとの定数バッファの数を超える規模を作る）
    void BuildVsmMegaScene(VsmMegaScene& scene, bool bCasters = true, uint32_t extraCasters = 0)
    {
        VsmRun& run = scene.Base.Run;
        run.Device->EnableMegaGeometryBatchCapabilities();
        run.Device->EnableDrawIndirectCount();
        BuildVsmCasterScene(scene.Base, true);
        RenderResources& resources = scene.Base.Resources;
        resources.MegaGeometry().SetOcclusionCullingEnabled(true);
        ViewRenderContext& context = run.Context;

        const RHI::Format formats[6] = {RHI::Format::R8G8B8A8_UNORM, RHI::Format::R16G16B16A16_FLOAT, RHI::Format::R8G8B8A8_UNORM,
                                        RHI::Format::R16G16B16A16_FLOAT, RHI::Format::D32_FLOAT, RHI::Format::R16G16_FLOAT};
        const char* names[6] = {"GBuffer_Albedo", "GBuffer_Normal", "GBuffer_Material", "GBuffer_Emissive", "GBuffer_Depth", "GBuffer_Velocity"};
        for (uint32_t index = 0; index < 6u; ++index)
        {
            scene.Textures[index] = index == 4u ? run.Device->CreateTexture(RHI::TextureDesc::DepthStencil(128, 64, formats[index], names[index]))
                                                : run.Device->CreateTexture(RHI::TextureDesc::RenderTarget(128, 64, formats[index], names[index]));
            scene.Shared.RegisterTexturePtr(names[index], scene.Textures[index]);
        }
        context.SharedResources = &scene.Shared;

        float vertices[12] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};
        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;
        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "VsmMegaMeshA";
        const auto meshA = resources.MegaGeometry().CreateMegaMesh(createInfo);
        createInfo.DebugName = "VsmMegaMeshC";
        const auto meshC = resources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(meshA.IsValid() && meshC.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(resources));

        MegaGeometryProxy proxyA;
        proxyA.ObjectId = 1;
        proxyA.ComponentId = 10;
        proxyA.MegaMeshHandle = meshA;
        proxyA.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxyA.WorldBounds.CenterX = 1.0f;
        proxyA.WorldBounds.CenterY = 2.0f;
        proxyA.WorldBounds.CenterZ = 3.0f;
        proxyA.WorldBounds.Radius = 4.0f;
        proxyA.bCastShadow = bCasters;
        scene.Proxies.push_back(proxyA);
        MegaGeometryProxy proxyB = proxyA;
        proxyB.ObjectId = 2;
        proxyB.ComponentId = 20;
        proxyB.bCastShadow = false;
        scene.Proxies.push_back(proxyB);
        MegaGeometryProxy proxyC = proxyA;
        proxyC.ObjectId = 3;
        proxyC.ComponentId = 30;
        proxyC.MegaMeshHandle = meshC;
        proxyC.WorldTransform = MakeMarkedMatrix(5.0f, 0.0f, 0.0f);
        proxyC.PreviousWorldTransform = proxyC.WorldTransform;
        proxyC.WorldBounds = BoundingSphere{};
        proxyC.bCastShadow = bCasters;
        scene.Proxies.push_back(proxyC);
        for (uint32_t extra = 0; extra < extraCasters; ++extra)
        {
            MegaGeometryProxy proxyExtra = proxyA;
            proxyExtra.ObjectId = 100 + extra;
            proxyExtra.ComponentId = 1000 + extra;
            scene.Proxies.push_back(proxyExtra);
        }
        context.SnapshotMegaGeometryProxies = &scene.Proxies;

        assert(scene.Mega.Initialize(context));
        scene.Mega.Setup(context);
        CameraProxy camera;
        camera.Viewport.Width = 128.0f;
        camera.Viewport.Height = 64.0f;
        RHI::Viewport viewport;
        viewport.width = 128.0f;
        viewport.height = 64.0f;
        viewport.maxDepth = 1.0f;
        RHI::ScissorRect scissor;
        scissor.right = 128;
        scissor.bottom = 64;
        FrameCommand frameCommand = FrameCommand::CreateMegaGeometryPass(
            &scene.Mega, &resources.MegaGeometry(), camera, true, viewport, scissor, DebugViewMode::Normal);
        GMegaCullUniformUpdates.clear();
        scene.Mega.RecordFrameCommand(frameCommand.MegaGeometry, &run.CommandList);
        scene.Base.Pass.SetMegaGeometryPass(&scene.Mega);
        scene.MainSequenceLength = run.CommandList.CallSequence.size();
        scene.MainDispatchCount = run.CommandList.DispatchGroups.size();
        scene.MainBarrierCount = run.CommandList.Barriers.size();
        scene.MainIndirectDrawCount = run.CommandList.IndirectDraws.size();
    }

    void ShutdownVsmMegaScene(VsmMegaScene& scene)
    {
        scene.Mega.Shutdown();
        ShutdownVsmCasterScene(scene.Base);
    }

    // 統計の読み戻しの枠へ、GPU が書き終えた体で MegaGeometry の投影物のカリングの統計（語 8〜10）を書く
    void WriteVsmMegaReadbackStats(const VirtualShadowMapPass& pass, uint32_t instances, uint32_t clusters, uint32_t overflow)
    {
        for (uint32_t slotIndex = 0; slotIndex < VirtualShadowMapPass::StatsReadbackSlotCount; ++slotIndex)
        {
            const RHI::BufferPtr& buffer = pass.GetStatsReadbackBuffer(slotIndex);
            assert(buffer);
            uint32_t* words = reinterpret_cast<uint32_t*>(static_cast<FakeBuffer*>(buffer.get())->MappedBytes.data());
            words[VirtualShadowMap::StatMegaInstances] = instances;
            words[VirtualShadowMap::StatMegaClusters] = clusters;
            words[VirtualShadowMap::StatMegaOverflow] = overflow;
        }
    }

    // 主の経路で書かない・読まないバッファの名前か（MegaGeometryPass が間接描画・見えた印・ページの要求・統計・区間などに使う）
    bool IsMainPathOutputBufferName(const char* name)
    {
        if (name == nullptr)
        {
            return false;
        }
        for (const char* main : {"MegaGeometry_IndirectDraw", "MegaGeometry_DrawCount", "MegaGeometry_DrawInfo", "MegaGeometry_VisibleLastFrame",
                                 "MegaGeometry_DummyVisibility", "MegaGeometry_DummyStats", "MegaGeometry_SwRaster", "MegaGeometry_SectionTable",
                                 "MegaGeometry_BvhQueue", "MegaGeometry_BvhCounters", "GeometryPageRequest"})
        {
            if (std::strcmp(name, main) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // VSM のパスを実際の GBuffer と照明のパスの間に置いて 2 フレーム回し、照明が太陽の影をどちらで描くかを確かめる。
    // 照明は VSM のページの表・プールが公開されたフレームだけ VSM を読み、公開されなければ CSM のまま描く。確かめる内容:
    //  - 照明が書く VSM の読み出しのパラメータの control.x（bExpectVsm なら 1、そうでなければ 0）
    //  - 照明の束縛 22・23 が VSM のページの表・プールか（bExpectVsm のときだけ VSM のバッファ）
    //  - CSM のテクスチャ配列が束縛 6 に束縛される（VSM の有無に依らず CSM は読まれ続ける）
    //  - CSM が有効なまま描かれる: 影を落とす方向光 1 灯と有効な CSM の公開（影の地図・サンプラー・4 カスケードの行列と分割距離）を
    //    置き、照明のパラメータが bShadowEnabled = 1・cascadeCount = 4 で、方向光に影の印（attenuation[2] = 1）が付くこと
    //  - 渡した VSM のパスが、2 フレームとも何も記録しないか（bExpectVsm でなければ）
    void RunVsmPassThroughLighting(VsmRun& run, VirtualShadowMapPass& pass, bool bExpectVsm)
    {
        run.Context.PhysicalLighting.SunClipmap = BuildVirtualShadowMapClipmap(
            NorvesLib::Math::Vector3(0.35f, -0.8f, 0.45f), 1u, NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), VirtualShadowMapClipmapSettings{});
        assert(run.Context.PhysicalLighting.SunClipmap.bEnabled);
        RHI::TextureDesc csmDesc = RHI::TextureDesc::DepthStencil(64, 64, RHI::Format::D32_FLOAT, "TestCsmShadowMap");
        csmDesc.ArraySize = PhysicalLightingShadowCascadeCount;
        const RHI::TexturePtr csmTexture = run.Device->CreateTexture(csmDesc);
        assert(csmTexture && csmTexture->GetArraySize() == PhysicalLightingShadowCascadeCount);

        // 有効な CSM の公開（ShadowMapPass が公開する内容と同じ形）。4 カスケードの有限の行列と、増える分割距離
        PhysicalLightingResources& physicalLighting = run.Context.PhysicalLighting;
        physicalLighting.bShadowPublished = true;
        physicalLighting.ShadowMapTexture = csmTexture;
        physicalLighting.ShadowSampler = run.Device->CreateSampler(RHI::SamplerDesc{});
        assert(physicalLighting.ShadowSampler);
        CascadedDirectionalShadowShaderValues& cascaded = physicalLighting.CascadedShadow;
        cascaded.bEnabled = true;
        cascaded.CascadeCount = PhysicalLightingShadowCascadeCount;
        for (uint32_t cascade = 0; cascade < PhysicalLightingShadowCascadeCount; ++cascade)
        {
            for (uint32_t element = 0; element < 16u; ++element)
            {
                cascaded.View[cascade][element] = element % 5 == 0 ? 1.0f : 0.0f;
                cascaded.Projection[cascade][element] = element % 5 == 0 ? 1.0f : 0.0f;
            }
        }
        const float splitDistances[PhysicalLightingShadowSplitCount] = {0.1f, 10.0f, 20.0f, 40.0f, 80.0f};
        std::memcpy(cascaded.SplitDistances, splitDistances, sizeof(splitDistances));

        // 影を落とす方向光 1 灯（CSM を掛ける灯に選ばれる条件: 表示される方向光がちょうど 1 つで、影を落とす）
        Container::VariableArray<LightProxy> lightProxies;
        LightProxy sunLight;
        sunLight.LightId = 1;
        sunLight.Type = LightType::Directional;
        sunLight.DirectionX = 0.35f;
        sunLight.DirectionY = -0.8f;
        sunLight.DirectionZ = 0.45f;
        sunLight.ColorR = 1.0f;
        sunLight.ColorG = 1.0f;
        sunLight.ColorB = 1.0f;
        sunLight.bCastShadows = true;
        sunLight.bVisible = true;
        lightProxies.push_back(sunLight);
        run.Context.SnapshotLightProxies = &lightProxies;

        run.Pool.BeginFrame(0);
        RenderResources renderResources;
        assert(renderResources.Initialize(run.Device));
        SceneRenderer renderer;
        assert(renderer.Initialize(run.Device.get(), nullptr, &run.Pool));
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        run.Context.Renderer = &renderer;
        run.Context.PendingFrameCommands = &pendingFrameCommands;
        run.Context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        run.Context.Resources.Textures = &renderResources.Textures();
        run.Context.Resources.Materials = &renderResources.Materials();
        run.Context.Resources.Meshes = &renderResources.Meshes();

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        LightingPass lightingPass;
        RGResourceHandle publishedShadowMap;
        NamedShadowMapProducerPass shadowProducer(csmTexture, &publishedShadowMap);

        for (uint64_t frame = 0; frame < 2u; ++frame)
        {
            SetVsmFrame(run, frame);
            pendingFrameCommands.clear();
            run.Pool.EndFrame();
            run.Pool.BeginFrame(frame);
            run.Graph.BeginFrame(frame);
            run.Graph.AddPass(&gbufferPass);
            run.Graph.AddPass(&pass);
            // CSM の影の地図をグラフへ公開するパス（本物の ShadowMapPass の代わり）。照明より前に置く
            const uint32_t shadowPassIndex = run.Graph.AddPass(&shadowProducer);
            const uint32_t lightingPassIndex = run.Graph.AddPass(&lightingPass);
            assert(run.Graph.AddDependency(shadowPassIndex, lightingPassIndex));
            assert(run.Graph.Compile(run.Context));
            assert(publishedShadowMap.IsValid());
            ResetLightingDescriptorCapture();
            GLastDescriptorBinding6Texture = nullptr;
            const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
            assert(result.bSuccess);

            // VSM のパスが宣言したものの有無（0 番が GBuffer、1 番が VSM、2 番が照明）
            assert((run.Graph.GetDeclaredPassAccessCount(1) != 0) == bExpectVsm);

            // 照明が書いた VSM の読み出しのパラメータ
            const FakeBuffer* sampleBuffer = static_cast<const FakeBuffer*>(run.Device->LightingVsmSampleBuffer.get());
            assert(sampleBuffer != nullptr && sampleBuffer->LastUpdateBytes.size() == sizeof(GPUVsmSampleParams));
            GPUVsmSampleParams params = {};
            std::memcpy(&params, sampleBuffer->LastUpdateBytes.data(), sizeof(params));
            assert(params.control[0] == (bExpectVsm ? 1u : 0u));

            // CSM が有効なまま描かれる: 照明のパラメータが CSM の影を有効にし、方向光に影の印が付く
            // （VSM が公開されないフレームは、照明がこの CSM を読んで太陽の影を描く）
            assert(GLastDescriptorBinding4UpdateBytes.size() == sizeof(GPULightingParams));
            GPULightingParams lightingParams = {};
            std::memcpy(&lightingParams, GLastDescriptorBinding4UpdateBytes.data(), sizeof(lightingParams));
            assert(lightingParams.bShadowEnabled == 1u);
            assert(lightingParams.cascadeCount == PhysicalLightingShadowCascadeCount);
            assert(lightingParams.lightCount == 1u);
            assert(lightingParams.shadowSplitDistances[1] == 10.0f && lightingParams.shadowSplitDistances[4] == 80.0f);
            assert(GLastDescriptorBinding5UpdateBytes.size() >= sizeof(GPULightData));
            GPULightData sunPacked = {};
            std::memcpy(&sunPacked, GLastDescriptorBinding5UpdateBytes.data(), sizeof(sunPacked));
            assert(sunPacked.position[3] == static_cast<float>(static_cast<int>(LightType::Directional)));
            assert(sunPacked.attenuation[2] == 1.0f);

            // 照明の記述子セットの束縛 22・23: VSM のページの表・プールか、既定のバッファか
            bool bFoundLightingSet = false;
            bool bBoundVsmBuffers = false;
            bool bBoundAnyVsmBuffer = false;
            for (const DescriptorBindingRecord& record : GDescriptorBindingRecords)
            {
                const char* sampleName = BoundBufferNameAt(record.Buffers, 21);
                if (sampleName == nullptr || std::strcmp(sampleName, "LightingVsmSampleParams") != 0)
                {
                    continue;
                }
                bFoundLightingSet = true;
                const char* pageTableName = BoundBufferNameAt(record.Buffers, 22);
                const char* poolName = BoundBufferNameAt(record.Buffers, 23);
                const bool bPageTable = pageTableName != nullptr && std::strcmp(pageTableName, "VSM_PageTable") == 0;
                const bool bPool = poolName != nullptr && std::strcmp(poolName, "VSM_PhysicalPool") == 0;
                bBoundVsmBuffers = bBoundVsmBuffers || (bPageTable && bPool);
                bBoundAnyVsmBuffer = bBoundAnyVsmBuffer || bPageTable || bPool;
            }
            assert(bFoundLightingSet);
            assert(bBoundVsmBuffers == bExpectVsm && bBoundAnyVsmBuffer == bExpectVsm);

            // CSM のテクスチャ配列は、VSM があってもなくても束縛 6 に束縛される
            assert(GLastDescriptorBinding6Texture == csmTexture.get());
            assert(run.CommandList.DrawCallCount > 0);
        }

        lightingPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        run.Context.Renderer = nullptr;
        run.Context.PendingFrameCommands = nullptr;
        run.Context.SnapshotOpaqueCommands = DrawCommandView{};
        run.Context.SnapshotLightProxies = nullptr;
        run.Context.Resources.Textures = nullptr;
        run.Context.Resources.Materials = nullptr;
        run.Context.Resources.Meshes = nullptr;
    }

    // 太陽の VSM の照明の統計（逃げた標本の数）の読み戻し:
    //  - 照明の描画の後（最後の EndRenderPass の後）に、統計のバッファへ PixelShaderWrite → HostRead のバリアを 1 回だけ記録する。
    //  - 書いたフレームの提出の完了が確かめられた枠（通し番号が CompletedRenderFrameSerial 以下）だけを読んで空ける。
    //    完了が未確認の枠は読まず、上書きもしない（GPU が書いているかもしれない）。
    //  - どの枠も未確認で空きが無いフレームは、統計のバリアを記録せず、読まない置き場へ束ねる（他の資源へ書かない）。
    void TestLightingVsmStatsAreMadeHostVisibleAndReadAfterCompletion()
    {
        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        run.Context.PhysicalLighting.SunClipmap = BuildVirtualShadowMapClipmap(
            NorvesLib::Math::Vector3(0.35f, -0.8f, 0.45f), 1u, NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), VirtualShadowMapClipmapSettings{});
        assert(run.Context.PhysicalLighting.SunClipmap.bEnabled);

        run.Pool.BeginFrame(0);
        RenderResources renderResources;
        assert(renderResources.Initialize(run.Device));
        SceneRenderer renderer;
        assert(renderer.Initialize(run.Device.get(), nullptr, &run.Pool));
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        run.Context.Renderer = &renderer;
        run.Context.PendingFrameCommands = &pendingFrameCommands;
        run.Context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        run.Context.Resources.Textures = &renderResources.Textures();
        run.Context.Resources.Materials = &renderResources.Materials();
        run.Context.Resources.Meshes = &renderResources.Meshes();

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        VirtualShadowMapPass vsmPass;
        assert(vsmPass.Initialize(run.Context));
        LightingPass lightingPass;

        // 1 フレーム回す。frame は 0 から、通し番号は frame + 1。completed は提出の完了が確かめられた最大の通し番号
        const auto runFrame = [&](uint64_t frame, uint64_t completed)
        {
            run.Context.FrameIndex = static_cast<uint32_t>(frame % 2u);
            run.Context.RenderFrameSerial = frame + 1u;
            run.Context.CompletedRenderFrameSerial = completed;
            pendingFrameCommands.clear();
            run.CommandList.Barriers.clear();
            run.CommandList.CallSequence.clear();
            run.Pool.EndFrame();
            run.Pool.BeginFrame(frame);
            run.Graph.BeginFrame(frame);
            run.Graph.AddPass(&gbufferPass);
            run.Graph.AddPass(&vsmPass);
            run.Graph.AddPass(&lightingPass);
            assert(run.Graph.Compile(run.Context));
            const RenderGraphExecutionResult result = run.Graph.ExecuteWithResult(run.Context);
            assert(result.bSuccess);
            assert(pendingFrameCommands.empty());
        };
        // そのフレームの統計のバリアの数と、バリアが指すバッファ。バリアは最後の EndRenderPass（照明の描画）より後
        const auto statsBarrier = [&](RHI::IBuffer*& outBuffer) -> size_t
        {
            const Container::VariableArray<BarrierEvent> barriers = CollectBufferBarriers(run.CommandList, "LightingVsmStats");
            outBuffer = barriers.empty() ? nullptr : barriers[0].Buffer;
            for (const BarrierEvent& barrier : barriers)
            {
                assert(barrier.BeforeState == RHI::ResourceState::PixelShaderWrite);
                assert(barrier.AfterState == RHI::ResourceState::HostRead);
                size_t lastEnd = run.CommandList.CallSequence.size();
                for (size_t index = 0; index < run.CommandList.CallSequence.size(); ++index)
                {
                    lastEnd = run.CommandList.CallSequence[index] == 'E' ? index : lastEnd;
                }
                assert(lastEnd < run.CommandList.CallSequence.size() && barrier.SequencePosition > lastEnd);
            }
            return barriers.size();
        };
        // GPU が書き終えた体の値を、枠の先頭の語へ書く（偽の装置の描画は中身を書かない）
        const auto word0 = [](RHI::IBuffer* buffer) -> uint32_t&
        {
            return *reinterpret_cast<uint32_t*>(static_cast<FakeBuffer*>(buffer)->MappedBytes.data());
        };

        RHI::IBuffer* slotA = nullptr;
        RHI::IBuffer* slotB = nullptr;
        RHI::IBuffer* buffer = nullptr;

        // フレーム 0（通し番号 1・完了なし）: 統計の枠 A を使い、描画の後に PixelShaderWrite → HostRead のバリアを 1 回記録する
        runFrame(0, 0);
        assert(statsBarrier(slotA) == 1 && slotA != nullptr);
        word0(slotA) = 5u;
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 0 && lightingPass.GetVsmFallbackSampleCount() == 0);

        // フレーム 1（通し番号 2・完了なし）: 枠 A はまだ GPU の完了が未確認なので読まず、別の枠 B を使う
        runFrame(1, 0);
        assert(statsBarrier(slotB) == 1 && slotB != nullptr && slotB != slotA);
        word0(slotB) = 7u;
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 0 && lightingPass.GetVsmFallbackSampleCount() == 0);
        assert(word0(slotA) == 5u);

        // フレーム 2（通し番号 3・通し番号 1 まで完了）: 枠 A を読んで空け、0 に戻して再び使う。枠 B は未確認のまま
        runFrame(2, 1);
        assert(statsBarrier(buffer) == 1 && buffer == slotA);
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 1 && lightingPass.GetVsmFallbackSampleCount() == 5u);
        assert(word0(slotA) == 0u && word0(slotB) == 7u);
        word0(slotA) = 11u;

        // フレーム 3（通し番号 4・通し番号 2 まで完了）: 枠 B を読む。枠 A（通し番号 3）は未確認のまま
        runFrame(3, 2);
        assert(statsBarrier(buffer) == 1 && buffer == slotB);
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 2 && lightingPass.GetVsmFallbackSampleCount() == 12u);
        assert(word0(slotA) == 11u);

        // GPU が止まったまま（完了が 2 のまま）フレームが進むと、空いている枠を順に使い、全 16 枠が埋まる（フレーム 4〜17）
        for (uint64_t frame = 4; frame <= 17; ++frame)
        {
            runFrame(frame, 2);
            assert(statsBarrier(buffer) == 1 && buffer != nullptr);
        }
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 2);

        // 空きが無いフレーム 18: 統計のバリアは記録せず、どの枠も上書きしない。読まない置き場（LightingVsmStatsSink）へ束ねる
        const BufferCreationRecord* sinkBefore = FindBufferCreation(*run.Device, "LightingVsmStatsSink");
        assert(sinkBefore == nullptr);
        runFrame(18, 2);
        assert(statsBarrier(buffer) == 0);
        assert(FindBufferCreation(*run.Device, "LightingVsmStatsSink") != nullptr);
        assert(word0(slotA) == 11u);
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 2);

        // 完了が 3 へ進むと、通し番号 3 の枠 A を読んで空け、また使える
        runFrame(19, 3);
        assert(statsBarrier(buffer) == 1 && buffer == slotA);
        assert(lightingPass.GetVsmStatsHarvestedExecuteCount() == 3 && lightingPass.GetVsmFallbackSampleCount() == 23u);

        lightingPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        ShutdownVsmRun(run, vsmPass);
    }

    // 対照: VSM が使える装置では、同じ構成で照明が VSM のページの表・プールを読む（control.x = 1・束縛 22・23 が VSM のバッファ）。
    // フォールバックのテストが「VSM を読まない」ことを確かめられる構成であることの裏づけ
    void TestLightingReadsVsmWhenPublished()
    {
        VsmRun run;
        run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
        InitializeVsmRun(run);
        VirtualShadowMapPass pass;
        assert(pass.Initialize(run.Context) && pass.IsActive());
        RunVsmPassThroughLighting(run, pass, true);
        ShutdownVsmRun(run, pass);
    }

    // include を引用符つきの相対パスで展開する（ShaderManager と同じ規則のテスト用の最小版）
    bool ExpandTestShaderIncludes(const char* relativePath, Container::String& expanded, uint32_t depth)
    {
        if (depth > 16)
        {
            return false;
        }
        Container::String fullPath = TestShaderDirectory;
        fullPath += "/";
        fullPath += relativePath;
        auto file = NorvesLib::FileStream::FileStream::CreateUnique(fullPath,
                                                                   NorvesLib::FileStream::FileMode::Read,
                                                                   NorvesLib::FileStream::FileAccess::Read,
                                                                   NorvesLib::FileStream::FileShare::Read);
        if (!file || !file->IsOpen())
        {
            return false;
        }
        const int64_t fileSize = file->GetSize();
        if (fileSize < 0)
        {
            return false;
        }
        const size_t size = static_cast<size_t>(fileSize);
        Container::VariableArray<char> text;
        text.resize(size + 1);
        if (size > 0 && file->Read(text.data(), size) != size)
        {
            return false;
        }
        file->Close();
        text[size] = '\0';

        size_t cursor = (size >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
                         static_cast<unsigned char>(text[2]) == 0xBF)
                            ? 3u
                            : 0u;
        while (cursor < size)
        {
            size_t lineEnd = cursor;
            while (lineEnd < size && text[lineEnd] != '\n')
            {
                ++lineEnd;
            }
            size_t contentEnd = lineEnd;
            if (contentEnd > cursor && text[contentEnd - 1] == '\r')
            {
                --contentEnd;
            }
            size_t head = cursor;
            while (head < contentEnd && (text[head] == ' ' || text[head] == '\t'))
            {
                ++head;
            }
            if (contentEnd - head >= 8 && std::strncmp(&text[head], "#include", 8) == 0)
            {
                size_t open = head + 8;
                while (open < contentEnd && text[open] != '"')
                {
                    ++open;
                }
                size_t close = open + 1;
                while (close < contentEnd && text[close] != '"')
                {
                    ++close;
                }
                if (close >= contentEnd || close - open - 1 >= 256)
                {
                    return false;
                }
                char includePath[256];
                std::memcpy(includePath, &text[open + 1], close - open - 1);
                includePath[close - open - 1] = '\0';
                if (!ExpandTestShaderIncludes(includePath, expanded, depth + 1))
                {
                    return false;
                }
                expanded += "\n";
            }
            else
            {
                const char saved = text[contentEnd];
                text[contentEnd] = '\0';
                expanded += &text[cursor];
                text[contentEnd] = saved;
                expanded += "\n";
            }
            cursor = lineEnd + 1;
        }
        return true;
    }

    // SPIR-V の命令（語数 << 16 | opcode）を先頭から数え、opcode の命令の数を返す
    uint32_t CountSpirvInstructions(const RHI::ShaderCompileResult& result, uint32_t opcode)
    {
        assert(result.bSuccess && result.ByteCode.size() >= 20 && result.ByteCode.size() % 4 == 0);
        const size_t wordCount = result.ByteCode.size() / 4;
        uint32_t count = 0;
        size_t index = 5;
        while (index < wordCount)
        {
            uint32_t word = 0;
            std::memcpy(&word, result.ByteCode.data() + index * 4, sizeof(word));
            const uint32_t length = word >> 16;
            assert(length != 0);
            count += (word & 0xFFFFu) == opcode ? 1u : 0u;
            index += length;
        }
        return count;
    }

    // 太陽の VSM の照明の逃げた標本の数え上げは、VT のフィードバック（sparse が要る）から独立に有効になる:
    //  - 能力: fragmentStoresAndAtomics と bufferDeviceAddress と十分な storage の範囲があれば、sparse が無くても VSM は使え、数え上げも有効。
    //  - シェーダー: NORVES_VSM_STATS だけを定義して lighting.frag をコンパイルすると、アトミックの加算（OpAtomicIAdd）が入る。
    //    NORVES_VT_FEEDBACK だけでは入らない（VT のフィードバックとは別のマクロ）。
    void TestLightingShaderCountsVsmFallbackIndependentlyOfVtFeedback()
    {
        RHI::DeviceCapabilities noSparse;
        noSparse.bFragmentStoresAndAtomics = true;
        noSparse.bBufferDeviceAddress = true;
        noSparse.MaxStorageBufferRange = 256ull * 1024ull * 1024ull;
        assert(VirtualShadowMap::PlanPool(noSparse, 0).Pages != 0);
        assert(!noSparse.SupportsVirtualTextureFeedback());
        assert(noSparse.SupportsVsmLightingStats());
        RHI::DeviceCapabilities noAtomics = noSparse;
        noAtomics.bFragmentStoresAndAtomics = false;
        assert(VirtualShadowMap::PlanPool(noAtomics, 0).Pages == 0);
        assert(!noAtomics.SupportsVsmLightingStats());

        constexpr uint32_t OpAtomicIAdd = 234;
        Container::String source;
        assert(ExpandTestShaderIncludes("lighting.frag", source, 0));
        const auto compile = [&](bool bVsmStats, bool bVtFeedback)
        {
            RHI::Vulkan::VulkanShaderCompiler compiler;
            compiler.SetVsmLightingStatsEnabled(bVsmStats);
            compiler.SetVirtualTextureFeedbackEnabled(bVtFeedback);
            return compiler.CompileFromSource(source, RHI::ShaderStage::Pixel, "lighting.frag", "main");
        };
        const RHI::ShaderCompileResult statsOnly = compile(true, false);
        assert(statsOnly.bSuccess);
        assert(CountSpirvInstructions(statsOnly, OpAtomicIAdd) > 0);
        const RHI::ShaderCompileResult feedbackOnly = compile(false, true);
        assert(feedbackOnly.bSuccess);
        assert(CountSpirvInstructions(feedbackOnly, OpAtomicIAdd) == 0);
        const RHI::ShaderCompileResult neither = compile(false, false);
        assert(neither.bSuccess);
        assert(CountSpirvInstructions(neither, OpAtomicIAdd) == 0);
    }

    // 影を落とす MegaGeometry のインスタンスが、CSM のインスタンスごとの定数バッファ（DynamicUniformAllocator のスロット。1 カスケードあたり 256）を
    // 超える規模でも、VSM の MegaGeometry の影の描画は定数バッファのスロットを使わず、描画の数が変わらない。
    //  - VSM の記録（印付け以降）の間接描画は、ホストが書いた塊ごとの 3 回 + MegaGeometry のクラスタの記録の DrawIndexedIndirectCount 1 回のまま。
    //  - VSM の記録の間に作るバッファの数（定数バッファを含む）は、インスタンスの数に依らない（インスタンスごとに 1 つ作る形ではない）。
    //  - 一覧・クラスタの記録の容量は一定で、カリングの dispatch の数も変わらない（ワークグループの数だけがインスタンスの数で増える）。
    void TestVirtualShadowMapMegaDrawDoesNotUseCsmUniformSlots()
    {
        constexpr uint32_t CsmUniformSlotsPerCascade = 256;
        struct Measurement
        {
            size_t IndirectDraws = 0;
            size_t CreatedBuffers = 0;
            size_t CreatedDescriptorSets = 0;
            size_t Dispatches = 0;
            uint32_t CasterCount = 0;
            uint32_t MaxDrawCount = 0;
            bool bMegaDraw = false;
        };
        const auto measure = [](uint32_t extraCasters) {
            VsmMegaScene scene;
            BuildVsmMegaScene(scene, true, extraCasters);
            FakeCommandList& commandList = scene.Base.Run.CommandList;
            const size_t buffersBefore = scene.Base.Run.Device->CreatedBuffers.size();
            const size_t setsBefore = scene.Base.Run.Device->DescriptorSetCreations;
            RunVsmCasterViewport(scene.Base, 0, 0);
            Measurement result;
            result.IndirectDraws = commandList.IndirectDraws.size() - scene.MainIndirectDrawCount;
            result.CreatedBuffers = scene.Base.Run.Device->CreatedBuffers.size() - buffersBefore;
            result.CreatedDescriptorSets = scene.Base.Run.Device->DescriptorSetCreations - setsBefore;
            result.Dispatches = commandList.DispatchGroups.size() - scene.MainDispatchCount;
            result.CasterCount = scene.Mega.GetShadowCasterInputs().CasterCount;
            result.MaxDrawCount = commandList.IndirectDraws.empty() ? 0u : commandList.IndirectDraws.back().MaxDrawCount;
            result.bMegaDraw = scene.Base.Pass.WasMegaDrawRecorded();
            ShutdownVsmMegaScene(scene);
            return result;
        };

        const Measurement few = measure(0);
        const Measurement many = measure(CsmUniformSlotsPerCascade + 47u);
        assert(few.CasterCount == 2u);
        assert(many.CasterCount == 2u + CsmUniformSlotsPerCascade + 47u);
        assert(few.bMegaDraw && many.bMegaDraw);
        // 間接描画: 塊ごとの 3 回 + クラスタの記録の 1 回。インスタンスの数に依らない
        assert(few.IndirectDraws == 4u && many.IndirectDraws == few.IndirectDraws);
        assert(few.MaxDrawCount == VirtualShadowMap::MEGA_CULL_LIST_CAPACITY && many.MaxDrawCount == few.MaxDrawCount);
        // 作るバッファ・記述子セット・dispatch の数は、インスタンスの数に依らない
        assert(many.CreatedBuffers == few.CreatedBuffers);
        assert(many.CreatedDescriptorSets == few.CreatedDescriptorSets);
        assert(many.Dispatches == few.Dispatches);
    }

    // vsm の構成の 1 フレーム: 主の経路（2 パスの遮蔽。DBIE + HZB の D 7 つ + DBIE）の後に、スキニングの変形 → 印付け・割り当て・消去 →
    // MegaGeometry の投影物のカリング（dirty の階層 = D・クラスタの選択 = D）→ 展開 → 描画（DDDDDJ + DD + D + BIIIE）が並ぶ。
    //  - カリングは、主のカリング（2 回目）より後・展開より前。dispatch は dirty の階層 (16, 16, 段の数)・選択 (影を落とすインスタンスのワークグループ数 2, 1, 段の数)・
    //    クラスタの記録 (一覧の容量 ÷ 64, 1, 1。一覧の 1 件 = 1 スレッド)。続く展開は、ホストが書いた塊 3 つの後ろに一覧の容量ぶんのワークグループを足す。
    //  - 描画は、ホストが書いた塊ごとの間接描画 3 回の後に、MegaGeometry のクラスタの記録を描く DrawIndexedIndirectCount 1 回（一覧の語 0 が数。
    //    インスタンスごとの定数バッファは使わない）。
    //  - 読む資源: 主の経路のインスタンスの表・影の表・ジオメトリのページの表（読み取りだけ）と VSM のページの表。書く資源: 自分の dirty の階層・出力の一覧・VSM の統計。
    //    主の経路の間接描画・カウンタ・描画情報・見えた印・ページの要求・統計・区間の表は束縛せず、記録の間に主のバッファへのバリアも無い。
    //  - 出力の一覧と dirty の階層は、記録の前に Common → UnorderedAccess、後に UnorderedAccess → Common。それぞれの頭（階層は全体・一覧は先頭 4 語）を 0 で埋めてから dispatch する。
    //  - 影の表はインスタンスの表と同じ並びで、影を落とさないインスタンスはワークグループを持たず（FirstGroup が次と同じ）、境界が無いものはメッシュの境界から作る。
    //  - 定数: LOD の許容は 1 texel・正射影の印（OrthoLod）が立ち、ページの要求の容量は 0。主の経路の定数の正射影の印は 0 のまま。
    //  - 作れなかった構成（影を落とすインスタンスが無い・パイプラインを作れない）では記録せず、VSM は動く。MegaGeometryPass を渡さない構成も同じ。
    void TestVirtualShadowMapPassRecordsMegaCullBetweenMainCullAndExpand()
    {
        {
            GVsmMegaCullUniformUpdates.clear();
            GVsmMegaCullParamsUpdates.clear();
            VsmMegaScene scene;
            BuildVsmMegaScene(scene);
            FakeCommandList& commandList = scene.Base.Run.CommandList;

            // 主の経路の記録
            const char* mainSequence = "DBIEDDDDDDDDBIE";
            assert(scene.MainSequenceLength == std::strlen(mainSequence));
            for (size_t index = 0; index < scene.MainSequenceLength; ++index)
            {
                assert(commandList.CallSequence[index] == mainSequence[index]);
            }
            assert(scene.MainDispatchCount == 9u);

            // 影の入力: 3 インスタンス・影を落とす 2 つ・ワークグループ 2（1 クラスタ = 1 ワークグループ）
            const MegaGeometryShadowCasterInputs& inputs = scene.Mega.GetShadowCasterInputs();
            assert(inputs.bValid && inputs.InstanceCount == 3u && inputs.CasterCount == 2u && inputs.TotalGroups == 2u);
            assert(inputs.InstanceBuffer && inputs.ShadowInstanceBuffer && inputs.PageTableBuffer);
            assert(IsDebugName(static_cast<const FakeBuffer*>(inputs.InstanceBuffer.get())->GetDesc().DebugName, "MegaGeometry_InstanceTable"));
            assert(IsDebugName(static_cast<const FakeBuffer*>(inputs.ShadowInstanceBuffer.get())->GetDesc().DebugName, "MegaGeometry_ShadowInstanceTable"));
            assert(IsDebugName(static_cast<const FakeBuffer*>(inputs.PageTableBuffer.get())->GetDesc().DebugName, "MegaGeometry_PageTable"));
            {
                const FakeBuffer* shadowBuffer = static_cast<const FakeBuffer*>(inputs.ShadowInstanceBuffer.get());
                assert(shadowBuffer->LastUpdateBytes.size() == 3u * sizeof(MegaGeometryShadowInstance));
                MegaGeometryShadowInstance table[3] = {};
                std::memcpy(table, shadowBuffer->LastUpdateBytes.data(), sizeof(table));
                const uint32_t casterWithBounds = MegaGeometryShadowFlagCaster | MegaGeometryShadowFlagBounds;
                // A: プロキシの境界（中心 (1, 2, 3)・半径 4）と、メッシュ全体の境界（中心 (0.5, 0.5, 0)・半径 1.25）の両方を含む最小の球。
                //    プロキシの境界だけを信じると、読み込みが済む前の小さな球のまま影のページの無効化・カリングが抜ける。ワークグループは 0 番から
                assert(table[0].Flags == casterWithBounds && table[0].FirstGroup == 0u);
                {
                    const float sources[2][4] = {{1.0f, 2.0f, 3.0f, 4.0f}, {0.5f, 0.5f, 0.0f, 1.25f}};
                    const float radius = table[0].BoundsSphere[3];
                    for (const auto& source : sources)
                    {
                        const float dx = table[0].BoundsSphere[0] - source[0];
                        const float dy = table[0].BoundsSphere[1] - source[1];
                        const float dz = table[0].BoundsSphere[2] - source[2];
                        assert(std::sqrt(dx * dx + dy * dy + dz * dz) + source[3] <= radius + 1.0e-4f);
                    }
                    // 2 つの球の中心の距離 3.391 を使った最小の半径 (3.391 + 4 + 1.25) / 2 = 4.3207
                    assert(std::abs(radius - 4.3207f) < 1.0e-3f);
                }
                // B: 影を落とさない。ワークグループを持たず、FirstGroup は次のインスタンスと同じ
                assert(table[1].Flags == 0u && table[1].FirstGroup == 1u);
                // C: 境界が無いのでメッシュの境界（中心 (0.5, 0.5, 0)・半径 1.25）をワールド (5, 0, 0) へ移した球
                assert(table[2].Flags == casterWithBounds && table[2].FirstGroup == 1u);
                assert(std::abs(table[2].BoundsSphere[0] - 5.5f) < 1.0e-5f && std::abs(table[2].BoundsSphere[1] - 0.5f) < 1.0e-5f &&
                       std::abs(table[2].BoundsSphere[2] - 0.0f) < 1.0e-5f && std::abs(table[2].BoundsSphere[3] - 1.25f) < 1.0e-5f);
            }
            // 主の経路の定数の正射影の印は 0（主の結果を変えない）
            assert(!GMegaCullUniformUpdates.empty());
            for (const Container::VariableArray<uint8_t>& bytes : GMegaCullUniformUpdates)
            {
                assert(bytes.size() == sizeof(MegaGeometry::CullUniformData));
                MegaGeometry::CullUniformData uniform;
                std::memcpy(&uniform, bytes.data(), sizeof(uniform));
                assert(uniform.OrthoLod == 0u);
            }

            RunVsmCasterViewport(scene.Base, 0, 0);
            assert(scene.Base.Pass.WasMarked() && scene.Base.Pass.WasRasterRecorded() && scene.Base.Pass.WasMegaCullRecorded());
            assert(scene.Base.Pass.WasMegaDrawRecorded());
            assert(scene.Base.Pass.GetMegaCullList() && scene.Base.Pass.GetMegaDirtyBits() && scene.Base.Pass.GetMegaChunks());

            // 並び: 主の経路の後に、変形・印付け・割り当て 3 段・消去（J）・階層・選択・クラスタの記録・展開の引数・展開（J。間接）・描画
            const char* vsmSequence = "DDDDDDDDDDDDJDDDDJBIIIIE";
            assert(commandList.CallSequence.size() == scene.MainSequenceLength + std::strlen(vsmSequence));
            for (size_t index = 0; index < std::strlen(vsmSequence); ++index)
            {
                assert(commandList.CallSequence[scene.MainSequenceLength + index] == vsmSequence[index]);
            }
            // GPU の区間: VsmCullMega は消去の後・展開の前に 1 回だけ開く（階層の作成と選択の両方を含む）
            {
                size_t clearScope = commandList.GpuScopes.size();
                size_t cullScope = commandList.GpuScopes.size();
                size_t expandScope = commandList.GpuScopes.size();
                uint32_t cullScopeCount = 0;
                for (size_t index = 0; index < commandList.GpuScopes.size(); ++index)
                {
                    const Container::String& name = commandList.GpuScopes[index].Name;
                    clearScope = name == "VsmClear" ? index : clearScope;
                    expandScope = name == "VsmExpand" ? index : expandScope;
                    if (name == "VsmCullMega")
                    {
                        cullScope = index;
                        ++cullScopeCount;
                    }
                }
                assert(cullScopeCount == 1u);
                assert(clearScope < cullScope && cullScope < expandScope && expandScope < commandList.GpuScopes.size());
                // 開いた時点は、消去（J）の後・階層の dispatch の前（直前までに記録した B・E・D・I・J は 6 個）
                assert(commandList.GpuScopes[cullScope].SequencePosition == scene.MainSequenceLength + 13u);
            }
            const size_t dirtyDispatch = scene.MainDispatchCount + 12u;
            const size_t cullDispatch = scene.MainDispatchCount + 13u;
            const size_t chunkDispatch = scene.MainDispatchCount + 14u;
            const size_t expandArgsDispatch = scene.MainDispatchCount + 15u;
            assert(commandList.DispatchGroups.size() == expandArgsDispatch + 1u);
            const uint32_t levelCount = VirtualShadowMap::LEVEL_COUNT;
            assert(commandList.DispatchGroups[dirtyDispatch].X == 16u && commandList.DispatchGroups[dirtyDispatch].Y == 16u &&
                   commandList.DispatchGroups[dirtyDispatch].Z == levelCount);
            assert(commandList.DispatchGroups[cullDispatch].X == 2u && commandList.DispatchGroups[cullDispatch].Y == 1u &&
                   commandList.DispatchGroups[cullDispatch].Z == levelCount);
            // クラスタの記録: 一覧の容量ぶんのスレッドを 64 ずつ x 方向に並べる
            assert(commandList.DispatchGroups[chunkDispatch].X == VirtualShadowMap::MEGA_CULL_LIST_CAPACITY / 64u &&
                   commandList.DispatchGroups[chunkDispatch].Y == 1u && commandList.DispatchGroups[chunkDispatch].Z == 1u);
            // 展開の引数: 1 スレッドの計算が、一覧の件数から間接 dispatch の引数（間接描画の引数の頭の語 1〜3）を書く
            assert(commandList.DispatchGroups[expandArgsDispatch].X == 1u && commandList.DispatchGroups[expandArgsDispatch].Y == 1u &&
                   commandList.DispatchGroups[expandArgsDispatch].Z == 1u);
            // 展開: 容量ぶんの直接 dispatch でなく、その引数の間接 dispatch 1 回（ホストが書いた塊 + 件数。x の上限を超える分の折り返しは引数の計算が行う）
            const FakeCommandList::IndirectDispatchRecord& expandIndirect = commandList.IndirectDispatches.back();
            assert(IsDebugName(expandIndirect.BufferName, "VsmRaster_Draws") && expandIndirect.OffsetBytes == sizeof(uint32_t));

            // 束縛: 階層を作る dispatch は VSM のページの表を読み、階層へ書く。選択の dispatch は主の経路の表を読み、自分の一覧・階層・統計へ書く
            const Container::VariableArray<BoundBufferName>& dirtyBindings = commandList.DispatchBindings[dirtyDispatch];
            assert(dirtyBindings.size() == 3u);
            assert(IsDebugName(BoundBufferNameAt(dirtyBindings, 14u), "VsmMegaCullParams"));
            assert(IsDebugName(BoundBufferNameAt(dirtyBindings, 15u), "VSM_PageTable"));
            assert(IsDebugName(BoundBufferNameAt(dirtyBindings, 16u), "VsmMega_DirtyBits"));
            const Container::VariableArray<BoundBufferName>& cullBindings = commandList.DispatchBindings[cullDispatch];
            assert(cullBindings.size() == 9u);
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 0u), "VsmMegaCullUniform"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 1u), "MegaGeometry_InstanceTable"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 11u), "MegaGeometry_PageTable"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 14u), "VsmMegaCullParams"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 15u), "VsmMega_List"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 16u), "VsmMega_DirtyBits"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 17u), "VSM_Stats"));
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 18u), "MegaGeometry_ShadowInstanceTable"));
            // 溢れて落としたクラスタの範囲のページへ再描画の印を書くため、VSM のページの表も束縛する
            assert(IsDebugName(BoundBufferNameAt(cullBindings, 19u), "VSM_PageTable"));
            // クラスタの記録: 主の経路のインスタンスの表・影の表と自分の一覧を読み、自分の記録の出力へ書く
            const Container::VariableArray<BoundBufferName>& chunkBindings = commandList.DispatchBindings[chunkDispatch];
            assert(chunkBindings.size() == 5u);
            assert(IsDebugName(BoundBufferNameAt(chunkBindings, 1u), "MegaGeometry_InstanceTable"));
            assert(IsDebugName(BoundBufferNameAt(chunkBindings, 14u), "VsmMegaCullParams"));
            assert(IsDebugName(BoundBufferNameAt(chunkBindings, 15u), "VsmMega_List"));
            assert(IsDebugName(BoundBufferNameAt(chunkBindings, 18u), "MegaGeometry_ShadowInstanceTable"));
            assert(IsDebugName(BoundBufferNameAt(chunkBindings, 19u), "VsmMega_Chunks"));
            // 展開は、ホストが書いた塊（束縛 1）に続けて、クラスタの記録（束縛 6）と一覧（束縛 7）を読む
            const Container::VariableArray<BoundBufferName>& expandArgsBindings = commandList.DispatchBindings[expandArgsDispatch];
            const Container::VariableArray<BoundBufferName>& expandBindings = expandIndirect.Bindings;
            assert(expandArgsBindings.size() == 8u && expandBindings.size() == 8u);
            assert(IsDebugName(BoundBufferNameAt(expandArgsBindings, 3u), "VsmRaster_Draws"));
            assert(IsDebugName(BoundBufferNameAt(expandArgsBindings, 7u), "VsmMega_List"));
            assert(IsDebugName(BoundBufferNameAt(expandBindings, 1u), "VsmRaster_Chunks"));
            assert(IsDebugName(BoundBufferNameAt(expandBindings, 6u), "VsmMega_Chunks"));
            assert(IsDebugName(BoundBufferNameAt(expandBindings, 7u), "VsmMega_List"));
            // 主の経路の出力・見えた印・ページの要求は、どの dispatch にも束縛されない
            for (const Container::VariableArray<BoundBufferName>* bindings : {&dirtyBindings, &cullBindings, &chunkBindings, &expandArgsBindings, &expandBindings})
            {
                for (const BoundBufferName& entry : *bindings)
                {
                    assert(!IsMainPathOutputBufferName(entry.Name));
                }
            }

            // バリア: 記録の間（主の経路の記録の後）に主の経路のバッファへのバリアは無い
            for (size_t index = scene.MainBarrierCount; index < commandList.Barriers.size(); ++index)
            {
                const BarrierEvent& barrier = commandList.Barriers[index];
                assert(barrier.Kind != RGBarrierKind::Buffer || barrier.Buffer == nullptr ||
                       !IsMainPathOutputBufferName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName));
            }
            // 自分の一覧・階層は、カリングの前に Common → UnorderedAccess（dirty の階層の dispatch の前。位置 = 直前までの B・E・D・I・J の数）、後に UnorderedAccess → Common
            const RHI::ResourceState common = RHI::ResourceState::Common;
            const RHI::ResourceState uav = RHI::ResourceState::UnorderedAccess;
            const size_t beforeDirty = scene.MainSequenceLength + 13u;
            const size_t afterCull = scene.MainSequenceLength + 16u;
            for (const char* name : {"VsmMega_List", "VsmMega_DirtyBits", "VsmMega_Chunks"})
            {
                assert(HasBufferBarrierAt(commandList, name, common, uav, beforeDirty));
                assert(HasBufferBarrierAt(commandList, name, uav, common, afterCull));
            }
            // クラスタの記録を作った後は、記録の書き込みを後の読み取りへ見せる（UnorderedAccess → UnorderedAccess）
            assert(HasBufferBarrierAt(commandList, "VsmMega_Chunks", uav, uav, afterCull));
            // 展開・描画は、一覧とクラスタの記録を UnorderedAccess へ遷移し、展開の後に GenericRead（頂点シェーダーと間接描画が読む）、描画の後に戻す
            {
                const size_t afterExpand = scene.MainSequenceLength + 18u;
                const size_t afterDraw = scene.MainSequenceLength + 24u;
                for (const char* name : {"VsmMega_List", "VsmMega_Chunks"})
                {
                    assert(HasBufferBarrierAt(commandList, name, common, uav, afterCull));
                    assert(HasBufferBarrierAt(commandList, name, uav, RHI::ResourceState::GenericRead, afterExpand));
                    assert(HasBufferBarrierAt(commandList, name, RHI::ResourceState::GenericRead, uav, afterDraw));
                    assert(HasBufferBarrierAt(commandList, name, uav, common, afterDraw));
                }
            }
            // 間接描画: ホストが書いた塊ごとに 1 回ずつ（引数の頭 16 バイトの後ろ、1 回 20 バイト）の後に、一覧の件数を数とする 1 回（塊の番号 3 から）
            {
                const FakeCommandList::IndirectDrawRecord* draws = commandList.IndirectDraws.data() + scene.MainIndirectDrawCount;
                assert(commandList.IndirectDraws.size() == scene.MainIndirectDrawCount + 4u);
                for (uint32_t chunk = 0; chunk < 3u; ++chunk)
                {
                    assert(draws[chunk].OffsetBytes == 16u + chunk * 20u && draws[chunk].MaxDrawCount == 1u);
                }
                assert(draws[3].OffsetBytes == 16u + 3u * 20u && draws[3].MaxDrawCount == VirtualShadowMap::MEGA_CULL_LIST_CAPACITY);
            }
            // 頭を 0 で埋める: 階層は全体、一覧は先頭 4 語。どちらも階層の dispatch の前
            assert(commandList.VsmMegaFills.size() == 2u);
            assert(IsDebugName(commandList.VsmMegaFills[0].BufferName, "VsmMega_DirtyBits") && commandList.VsmMegaFills[0].Value == 0u &&
                   commandList.VsmMegaFills[0].SizeBytes == VirtualShadowMap::MegaDirtyBitsBytes());
            assert(IsDebugName(commandList.VsmMegaFills[1].BufferName, "VsmMega_List") && commandList.VsmMegaFills[1].Value == 0u &&
                   commandList.VsmMegaFills[1].SizeBytes == VirtualShadowMap::MEGA_CULL_LIST_HEADER_WORDS * sizeof(uint32_t));
            assert(commandList.VsmMegaFills[0].SequencePosition == beforeDirty && commandList.VsmMegaFills[1].SequencePosition == beforeDirty);

            // 定数: カリングの定数（LOD の許容 1 texel・正射影・要求なし）と、段の定数
            assert(GVsmMegaCullUniformUpdates.size() == 1u && GVsmMegaCullParamsUpdates.size() == 1u);
            {
                assert(GVsmMegaCullUniformUpdates[0].size() == sizeof(MegaGeometry::CullUniformData));
                MegaGeometry::CullUniformData uniform;
                std::memcpy(&uniform, GVsmMegaCullUniformUpdates[0].data(), sizeof(uniform));
                assert(uniform.OrthoLod == 1u && uniform.LODBias == 1.0f && uniform.PageRequestCapacity == 0u);
                assert(uniform.InstanceCount == 3u && uniform.TotalGroupCount == 2u);
                assert(uniform.CullPass == 0u && uniform.bHiZEnabled == 0u && uniform.bSwRasterEnabled == 0u);

                const Container::VariableArray<uint8_t>& params = GVsmMegaCullParamsUpdates[0];
                assert(params.size() == 592u);
                uint32_t counts[4] = {};
                std::memcpy(counts, params.data() + 64, sizeof(counts));
                assert(counts[0] == levelCount && counts[1] == VirtualShadowMap::MEGA_CULL_LIST_CAPACITY && counts[2] == 2u);
                const VirtualShadowMapClipmap& clipmap = scene.Base.Run.Context.PhysicalLighting.SunClipmap;
                for (uint32_t level = 0; level < levelCount; ++level)
                {
                    float info[4] = {};
                    int32_t origin[4] = {};
                    std::memcpy(info, params.data() + 80 + level * 16u, sizeof(info));
                    std::memcpy(origin, params.data() + 336 + level * 16u, sizeof(origin));
                    assert(info[0] == clipmap.Levels[level].PageMeters && info[1] == clipmap.Levels[level].TexelMeters);
                    assert(origin[0] == static_cast<int32_t>(clipmap.Levels[level].OriginPageX) &&
                           origin[1] == static_cast<int32_t>(clipmap.Levels[level].OriginPageY));
                }
            }
            ShutdownVsmMegaScene(scene);
        }

        // 影を落とすインスタンスが無い（全部 bCastShadow = false）: 主の経路は記録するが、影の入力は無効で、カリングを記録しない（VSM は展開・描画まで動く）
        {
            VsmMegaScene scene;
            BuildVsmMegaScene(scene, false);
            assert(!scene.Mega.GetShadowCasterInputs().bValid);
            RunVsmCasterViewport(scene.Base, 0, 0);
            assert(scene.Base.Pass.WasMarked() && scene.Base.Pass.WasRasterRecorded() && !scene.Base.Pass.WasMegaCullRecorded());
            const char* vsmSequence = "DDDDDDDDDDDDJDBIIIE";
            const FakeCommandList& commandList = scene.Base.Run.CommandList;
            assert(commandList.CallSequence.size() == scene.MainSequenceLength + std::strlen(vsmSequence));
            for (size_t index = 0; index < std::strlen(vsmSequence); ++index)
            {
                assert(commandList.CallSequence[scene.MainSequenceLength + index] == vsmSequence[index]);
            }
            assert(commandList.VsmMegaFills.empty());
            ShutdownVsmMegaScene(scene);
        }

        // MegaGeometryPass を渡さない構成: カリングは記録しない
        {
            VsmCasterScene scene;
            BuildVsmCasterScene(scene, true);
            RunVsmCasterViewport(scene, 0, 0);
            assert(scene.Pass.GetMegaGeometryPass() == nullptr && !scene.Pass.WasMegaCullRecorded());
            const char* expectedSequence = "DDDDDDDDDDDDJDBIIIE";
            assert(scene.Run.CommandList.CallSequence.size() == std::strlen(expectedSequence));
            ShutdownVsmCasterScene(scene);
        }

        // 印付けをしない構成（クリップマップが無い）: カリングは記録しない
        {
            VsmMegaScene scene;
            BuildVsmMegaScene(scene);
            // 影の入力は有効なまま、クリップマップだけを無効にして回す
            scene.Base.Run.Context.PhysicalLighting.SunClipmap = VirtualShadowMapClipmap{};
            RunVsmCasterViewport(scene.Base, 0, 0);
            assert(!scene.Base.Pass.WasMarked() && !scene.Base.Pass.WasMegaCullRecorded());
            assert(scene.Base.Run.CommandList.VsmMegaFills.empty());
            ShutdownVsmMegaScene(scene);
        }

        // MegaGeometry の影の経路を用意できない装置は、MegaGeometry の影だけを欠いた VSM を公開せず、VSM の資源を何も公開しないで CSM へ戻る。
        // 戻りは VSM_FALLBACK reason=mega_geometry を 1 回だけ出し、パスは何も宣言せず、何も描かない（照明は公開された資源が無いので CSM を読む。実際の照明のパスで確かめる）
        const auto expectMegaFallback = [](VsmRun& run, VirtualShadowMapPass& pass) {
#if NORVES_ENABLE_LOGGING
            Logging::LogConfig logConfig;
            logConfig.minLevel = Logging::LogLevel::Trace;
            logConfig.outputType = Logging::LogOutput::None;
            logConfig.bAsyncLogging = false;
            logConfig.bAutoFlush = false;
            Logging::Logger& logger = Logging::Logger::GetInstance();
            logger.Shutdown();
            assert(logger.Initialize(logConfig));
            VsmLogCollector logs;
            logger.AddSink(&logs);
#endif
            assert(pass.Initialize(run.Context));
            assert(!pass.IsActive() && pass.GetFallbackReason() == VirtualShadowMap::FallbackReason::MegaGeometry);
            assert(pass.GetPoolPages() == 0);
            assert(!pass.GetPool() && !pass.GetPageTable() && !pass.GetRequestBits() && !pass.GetFreeList() && !pass.GetStats() &&
                   !pass.GetDirtyList());
            assert(!pass.GetMegaCullList() && !pass.GetMegaDirtyBits() && !pass.GetMegaChunks());
            // 実際の照明のパスを後ろに置き、VSM の資源が公開されないので照明が CSM のまま描くことまで確かめる
            RunVsmPassThroughLighting(run, pass, false);
            assert(run.CommandList.VsmFills.empty() && run.CommandList.DispatchGroups.empty());
#if NORVES_ENABLE_LOGGING
            assert(logs.Count("VSM_FALLBACK reason=mega_geometry") == 1 && logs.Count("VSM_FALLBACK") == 1);
            assert(logs.Count("VRAM_LEDGER vsm_pool") == 0 && logs.Count("VRAM_LEDGER vsm_page_table") == 0);
            logger.RemoveSink(&logs);
            logger.Shutdown();
#endif
        };
        assert(std::strcmp(VirtualShadowMap::FallbackReasonName(VirtualShadowMap::FallbackReason::MegaGeometry), "mega_geometry") == 0);

        // DrawIndexedIndirectCount を使えない装置: MegaGeometry のクラスタの記録を描けないので、VSM を使わず CSM へ戻る
        {
            VsmRun run;
            run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
            run.Device->DisableDrawIndirectCount();
            InitializeVsmRun(run);
            run.Device->FailComputePipelineCreationNumber = 0xFFFFFFFFu;
            VirtualShadowMapPass pass;
            expectMegaFallback(run, pass);
            // 印付け・割り当て・消去・展開・展開の引数の 5 つの後は、カリングのパイプラインも資源も作らない
            assert(run.Device->ComputePipelineCreations == 5u);
            assert(CountBufferCreations(*run.Device, "VsmMega_List") == 0 && CountBufferCreations(*run.Device, "VsmMega_Chunks") == 0);
            ShutdownVsmRun(run, pass);
        }

        // カリングのパイプラインを作れない装置: VSM を使わず CSM へ戻る（カリングの資源は作らない）
        {
            uint32_t baseline = 0;
            {
                VsmRun run;
                run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
                InitializeVsmRun(run);
                // 失敗の番号が 0 でないときだけ作成を数える（届かない番号にして数だけ取る）
                run.Device->FailComputePipelineCreationNumber = 0xFFFFFFFFu;
                VirtualShadowMapPass pass;
                assert(pass.Initialize(run.Context) && pass.IsActive());
                assert(pass.GetMegaCullList() && pass.GetMegaDirtyBits() && pass.GetMegaChunks());
                baseline = run.Device->ComputePipelineCreations;
                // 印付け・割り当て・消去・展開・展開の引数の 5 つの後に、階層・選択・クラスタの記録の 3 つ
                assert(baseline == 8u);
                ShutdownVsmRun(run, pass);
            }
            for (const uint32_t failNumber : {baseline - 2u, baseline - 1u, baseline})
            {
                VsmRun run;
                run.Device->SetVirtualShadowMapCapabilities(true, true, 0xFFFFFFFFull);
                InitializeVsmRun(run);
                run.Device->FailComputePipelineCreationNumber = failNumber;
                VirtualShadowMapPass pass;
                expectMegaFallback(run, pass);
                assert(CountBufferCreations(*run.Device, "VsmMega_List") == 0 && CountBufferCreations(*run.Device, "VsmMega_DirtyBits") == 0 &&
                       CountBufferCreations(*run.Device, "VsmMega_Chunks") == 0);
                ShutdownVsmRun(run, pass);
            }
        }

#if NORVES_ENABLE_LOGGING
        // 統計: 記録したフレームの語 8〜10 を、最初の読み戻しと、以後 60 回読むごとに VSM_MEGA_CULL として出す（値が変わっても間は出さない）。
        // 記録しないフレームの統計は読まない
        {
            Logging::LogConfig logConfig;
            logConfig.minLevel = Logging::LogLevel::Trace;
            logConfig.outputType = Logging::LogOutput::None;
            logConfig.bAsyncLogging = false;
            logConfig.bAutoFlush = false;
            Logging::Logger& logger = Logging::Logger::GetInstance();
            logger.Shutdown();
            assert(logger.Initialize(logConfig));
            VsmLogCollector logs;
            logger.AddSink(&logs);

            VsmMegaScene scene;
            BuildVsmMegaScene(scene);
            assert(CountBufferCreations(*scene.Base.Run.Device, "VsmMega_List") == 1);
            const char* const expectedLog = "VSM_MEGA_CULL instances=3 clusters=5 overflow=1";
            // 枠の読み戻しは同じ番号の次のフレームの最初の Execute（フレーム 2 が最初）。60 回目の次（61 回目の読み戻し = フレーム 62）で 2 回目
            for (uint64_t frame = 0; frame <= 61u; ++frame)
            {
                WriteVsmMegaReadbackStats(scene.Base.Pass, 3, 5, 1);
                RunVsmCasterViewport(scene.Base, frame, frame);
                assert(logs.Count(expectedLog) == (frame >= 2u ? 1u : 0u));
            }
            WriteVsmMegaReadbackStats(scene.Base.Pass, 3, 5, 1);
            RunVsmCasterViewport(scene.Base, 62, 62);
            assert(logs.Count(expectedLog) == 2u && logs.Count("VSM_MEGA_CULL") == 2u);
            ShutdownVsmMegaScene(scene);

            // 記録しないフレーム（影を落とすインスタンスが無い）の統計は読まない
            VsmMegaScene noCasters;
            BuildVsmMegaScene(noCasters, false);
            for (uint64_t frame = 0; frame < 6u; ++frame)
            {
                WriteVsmMegaReadbackStats(noCasters.Base.Pass, 3, 5, 1);
                RunVsmCasterViewport(noCasters.Base, frame, frame);
            }
            assert(logs.Count("VSM_MEGA_CULL") == 2u);
            ShutdownVsmMegaScene(noCasters);

            logger.RemoveSink(&logs);
            logger.Shutdown();
        }
#endif
    }

    // 報告の間隔: 最初の報告と、以後 60 回報告するごとに出す。値が変わっても間は出さない
    void TestVirtualShadowMapMegaCullStatsReporterLogsEvery60Reports()
    {
#if NORVES_ENABLE_LOGGING
        Logging::LogConfig logConfig;
        logConfig.minLevel = Logging::LogLevel::Trace;
        logConfig.outputType = Logging::LogOutput::None;
        logConfig.bAsyncLogging = false;
        logConfig.bAutoFlush = false;
        Logging::Logger& logger = Logging::Logger::GetInstance();
        logger.Shutdown();
        assert(logger.Initialize(logConfig));
        VsmLogCollector logs;
        logger.AddSink(&logs);
#endif
        VirtualShadowMapMegaCullStatsReporter reporter;
        assert(reporter.Report(1, 2, 3));
        for (uint32_t report = 1; report < VirtualShadowMapMegaCullStatsReporter::LogIntervalReports; ++report)
        {
            assert(!reporter.Report(report, report * 2u, 0));
        }
        assert(reporter.Report(7, 8, 9));
        assert(!reporter.Report(7, 8, 9));
#if NORVES_ENABLE_LOGGING
        assert(logs.Count("VSM_MEGA_CULL instances=1 clusters=2 overflow=3") == 1);
        assert(logs.Count("VSM_MEGA_CULL instances=7 clusters=8 overflow=9") == 1);
        assert(logs.Count("VSM_MEGA_CULL") == 2);
        logger.RemoveSink(&logs);
        logger.Shutdown();
#endif
    }

    // 64bit のバッファは画面の画素数 × 8 バイトの 1 つで、同じ大きさの間は作り直さない。大きさが変わると新しく作り、
    // 古いバッファは GPU が前のフレームで使っているかもしれないので、飛行中のフレームの数を超えるまで持つ。
    // 埋めは render pass の外（転送の書き込み）で、状態を Common（初回）か GenericRead（2 回目以降）から CopyDest へ進め、埋めた後は GenericRead
    void TestVisibilityMergeKeyBufferFollowsResolutionAndRetiresOldBuffers()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableVisibilityResolveCapabilities();
        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));
        const RHI::RenderPassPtr renderPass = device->CreateRenderPass(VisibilityMerge::MakeLoadRenderPassDesc());
        assert(renderPass);

        // 合流の render pass は ID・深度とも Load で、ShaderResource から ShaderResource（直前・直後のパスの状態と同じ）
        const RHI::RenderPassDesc loadDesc = VisibilityMerge::MakeLoadRenderPassDesc();
        assert(loadDesc.colorAttachments.size() == 1 && loadDesc.hasDepthStencil);
        assert(loadDesc.colorAttachments[0].format == RHI::Format::R32_UINT);
        assert(loadDesc.colorAttachments[0].loadOp == RHI::AttachmentLoadOp::Load);
        assert(loadDesc.colorAttachments[0].initialState == RHI::ResourceState::ShaderResource);
        assert(loadDesc.colorAttachments[0].finalState == RHI::ResourceState::ShaderResource);
        assert(loadDesc.depthStencilAttachment.format == RHI::Format::D32_FLOAT);
        assert(loadDesc.depthStencilAttachment.loadOp == RHI::AttachmentLoadOp::Load);
        assert(loadDesc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource);
        assert(loadDesc.depthStencilAttachment.finalState == RHI::ResourceState::ShaderResource);

        VisibilityMerge merge;
        assert(VisibilityMerge::IsSupported(device->GetCapabilities()));
        assert(merge.Initialize(device.get(), &shaderManager, renderPass));
        assert(merge.IsReady());
        assert(!merge.GetKeyBuffer());

        merge.BeginFrame(1, 1);
        assert(merge.EnsureKeyBuffer(64, 32));
        assert(merge.GetKeyBuffer() && merge.GetKeyWidth() == 64 && merge.GetKeyHeight() == 32);
        assert(merge.GetKeyBufferBytes() == 64u * 32u * VisibilityBuffer::KEY_BYTES);
        assert(CountBufferCreations(*device, "VisBuffer_Key64") == 1);
        const RHI::BufferPtr firstBuffer = merge.GetKeyBuffer();

        // 同じ大きさなら同じバッファのまま
        assert(merge.EnsureKeyBuffer(64, 32));
        assert(merge.GetKeyBuffer() == firstBuffer);
        assert(CountBufferCreations(*device, "VisBuffer_Key64") == 1);

        // 埋め: すべてのビットが 1（0xFFFFFFFF の語）、バッファ全体
        FakeCommandList commandList;
        assert(merge.RecordClear(&commandList));
        assert(commandList.Key64Fills.size() == 1);
        assert(commandList.Key64Fills[0].SizeBytes == 64u * 32u * 8u);
        assert(commandList.Key64Fills[0].Value == VisibilityBuffer::KEY_EMPTY_WORD);
        assert(commandList.Barriers.size() == 2);
        assert(commandList.Barriers[0].BeforeState == RHI::ResourceState::Common &&
               commandList.Barriers[0].AfterState == RHI::ResourceState::CopyDest);
        assert(commandList.Barriers[1].BeforeState == RHI::ResourceState::CopyDest &&
               commandList.Barriers[1].AfterState == RHI::ResourceState::GenericRead);
        // 2 回目の埋めは、前のフレームの読み取り（GenericRead）の後に並ぶ
        assert(merge.RecordClear(&commandList));
        assert(commandList.Barriers.size() == 4);
        assert(commandList.Barriers[2].BeforeState == RHI::ResourceState::GenericRead &&
               commandList.Barriers[2].AfterState == RHI::ResourceState::CopyDest);

        // 大きさが変わると新しく作り、古いバッファは持っておく。新しいバッファの最初の埋めは Common から
        merge.BeginFrame(2, 2);
        assert(merge.EnsureKeyBuffer(128, 64));
        assert(merge.GetKeyBuffer() != firstBuffer);
        assert(merge.GetKeyBufferBytes() == 128u * 64u * 8u);
        assert(merge.GetRetiredBufferCount() == 1);
        assert(CountBufferCreations(*device, "VisBuffer_Key64") == 2);
        FakeCommandList resizedCommandList;
        assert(merge.RecordClear(&resizedCommandList));
        assert(resizedCommandList.Key64Fills.size() == 1 && resizedCommandList.Key64Fills[0].SizeBytes == 128u * 64u * 8u);
        assert(resizedCommandList.Barriers[0].BeforeState == RHI::ResourceState::Common);

        // 飛行中のフレームの数を超えるまで、古いバッファを手放さない
        const uint64_t retiredFrame = 2;
        for (uint64_t serial = retiredFrame + 1; serial <= retiredFrame + FrameUseRingMaxInFlightSlots + 1; ++serial)
        {
            merge.BeginFrame(static_cast<uint32_t>(serial % FrameUseRingMaxInFlightSlots), serial);
            assert(merge.GetRetiredBufferCount() == 1);
        }
        const uint64_t releaseFrame = retiredFrame + FrameUseRingMaxInFlightSlots + 2;
        merge.BeginFrame(static_cast<uint32_t>(releaseFrame % FrameUseRingMaxInFlightSlots), releaseFrame);
        assert(merge.GetRetiredBufferCount() == 0);

        merge.Shutdown();
        assert(!merge.IsReady() && !merge.GetKeyBuffer());
        shaderManager.Shutdown();
    }

    // --visibility-buffer=off: 描画の写しを作らず、MegaGeometry のバッファは今まで通りその場で Common へ戻る
    void TestVisibilityRasterOffKeepsExistingMegaGeometryRecording()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, false, true);
        FakeCommandList& commandList = scene.CommandList;

        assert(!scene.Mega.IsVisibilityDrawPlanEnabled());
        // ID のラスタを足さない予備の経路では、64bit のバッファも合流のパスも作らない
        assert(CountBufferCreations(*scene.Device, "VisBuffer_Key64") == 0);
        assert(CountBufferCreations(*scene.Device, "VisBuffer_MergeParams") == 0);
        assert(commandList.Key64Fills.empty());
        assert(commandList.BeginRenderPassCount == 3); // GBuffer（空）・MegaGeometry 2 パス
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.DispatchCount == 9);        // カリング 2 回 + HZB 7 段
        MegaGeometryPass::VisibilityDrawPlan plan;
        assert(!scene.Mega.TakeVisibilityDrawPlan(plan));

        ShutdownVisibilityRasterScene(scene);
    }

    // GBuffer の深度が無い構成（RenderGraph に深度が無い）では、VisibilityRasterPass は何も宣言せず何も描かない
    void TestVisibilityRasterWithoutGBufferDepthDoesNothing()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableVisibilityBufferCapabilities();
        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        VisibilityRasterPass pass;
        assert(pass.Initialize(context));
        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);
        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);
        assert(!pass.GetLastFrameStats().bRendered);
        uint32_t version = 0;
        assert(!graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferId, version));

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestShadowMapNativeDeclareImportsDepthOutput()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetShadowMapHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
        uint32_t shadowMapVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::ShadowMap, shadowMapVersion));
        assert(shadowMapVersion == 0);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetShadowMapHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::DepthWrite);
        assert(finalState == RHI::ResourceState::ShaderResource);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestShadowMapNativeExecuteRegistersBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.SharedResources = &sharedResources;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        RHI::TexturePtr exportedShadowMap;
        assert(result.TryGetTexture(RenderGraphResourceNames::ShadowMap, exportedShadowMap));
        assert(exportedShadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ShadowMap"));
        assert(sharedResources.GetTexturePtr("ShadowMap").get() == pass.GetShadowMapTexture());
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestNeuralDecodeNativeDeclareWritesLogicalCompletion()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        NeuralMaterialDecodePass pass;
        const uint32_t passIndex = graph.AddPass(&pass);

        ViewRenderContext context;
        assert(graph.Compile(context));
        assert(pass.GetDecodeCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetDecodeCompleteHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::Common);
        assert(finalState == RHI::ResourceState::Common);
        assert(graph.GetCompiledBarriers().empty());
    }

    void TestNeuralDecodeNativeExecuteSkipsUnsupportedPath()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        RHI::DeviceCapabilities capabilities;
        capabilities.NeuralShaders.bSupported = false;

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.Capabilities = &capabilities;

        NeuralMaterialDecodePass pass;
        assert(pass.Initialize(context));
        assert(!pass.IsCooperativeVectorSupported());

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
    }

    void TestMegaGeometryNativeDeclareImportsPersistentBuffers()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetIndirectDrawBufferHandle().IsValid());
        assert(pass.GetDrawCountBufferHandle().IsValid());
        assert(pass.GetMegaGeometryCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 3);

        bool bHasIndirectBufferWrite = false;
        bool bHasDrawCountBufferWrite = false;
        bool bHasCompleteWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(passIndex, accessIndex, resource, mode, state, finalState));
            assert(mode == RGAccessMode::Write);
            assert(state == RHI::ResourceState::Common);
            assert(finalState == RHI::ResourceState::Common);

            if (resource == pass.GetIndirectDrawBufferHandle())
            {
                bHasIndirectBufferWrite = true;
            }
            else if (resource == pass.GetDrawCountBufferHandle())
            {
                bHasDrawCountBufferWrite = true;
            }
            else if (resource == pass.GetMegaGeometryCompleteHandle())
            {
                bHasCompleteWrite = true;
            }
        }

        RenderGraphResources graphResources(&graph);
        assert(graphResources.GetBuffer(pass.GetIndirectDrawBufferHandle()));
        assert(graphResources.GetBuffer(pass.GetDrawCountBufferHandle()));
        assert(bHasIndirectBufferWrite);
        assert(bHasDrawCountBufferWrite);
        assert(bHasCompleteWrite);
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeDeclareUsesNamedGBufferAttachments()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        GBufferPass gbufferPass;
        MegaGeometryPass megaGeometryPass;
        assert(megaGeometryPass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(megaGeometryPassIndex) == 9);

        auto hasAttachmentAccess = [&graph](uint32_t passIndex,
                                            RGResourceHandle expected,
                                            RGAttachmentKind expectedKind,
                                            RHI::ResourceState expectedState) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Read;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                bool bColorAttachmentLoadStore = false;
                RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
                RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
                RGAttachmentKind kind = RGAttachmentKind::Color;
                RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState,
                                                      &bColorAttachmentLoadStore,
                                                      &loadOp,
                                                      &storeOp,
                                                      &kind,
                                                      &mutability));

                if (resource == expected)
                {
                    const bool bExpectedColorLoadStore = expectedKind == RGAttachmentKind::Color;
                    return mode == RGAccessMode::Write &&
                           state == expectedState &&
                           finalState == RHI::ResourceState::ShaderResource &&
                           bColorAttachmentLoadStore == bExpectedColorLoadStore &&
                           loadOp == RHI::AttachmentLoadOp::Load &&
                           storeOp == RHI::AttachmentStoreOp::Store &&
                           kind == expectedKind &&
                           mutability == RGAttachmentMutability::Write;
                }
            }

            return false;
        };

        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetAlbedoHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetNormalHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetMaterialHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetEmissiveHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetVelocityHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetDepthHandle(),
                                   RGAttachmentKind::DepthStencil,
                                   RHI::ResourceState::DepthWrite));

        uint32_t gbufferVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferAlbedo, gbufferVersion));
        assert(gbufferVersion == 1);
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferDepth, gbufferVersion));
        assert(gbufferVersion == 1);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == megaGeometryPassIndex);

        megaGeometryPass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeExecuteEnqueuesEmptyAttachmentPass()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        MegaGeometryPass megaGeometryPass;

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(megaGeometryPass.Initialize(context));

        graph.AddPass(&gbufferPass);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(megaGeometryPassIndex) == 9);
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        assert(graph.GetLastExecutedPassCount() == 2);
        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());

        bool bFoundMegaGeometryRenderPassDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bColorAttachmentStatesMatch = true;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bColorAttachmentStatesMatch =
                    bColorAttachmentStatesMatch &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.storeOp == RHI::AttachmentStoreOp::Store &&
                    attachment.initialState == RHI::ResourceState::RenderTarget &&
                    attachment.finalState == RHI::ResourceState::ShaderResource;
            }

            if (!bColorAttachmentStatesMatch)
            {
                continue;
            }

            assert(desc.depthStencilAttachment.loadOp == RHI::AttachmentLoadOp::Load);
            assert(desc.depthStencilAttachment.storeOp == RHI::AttachmentStoreOp::Store);
            assert(desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite);
            assert(desc.depthStencilAttachment.finalState == RHI::ResourceState::ShaderResource);
            bFoundMegaGeometryRenderPassDesc = true;
        }
        assert(bFoundMegaGeometryRenderPassDesc);

        megaGeometryPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeExecuteRecreatesRenderPassWhenAttachmentStateModeChanges()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        SharedResourceRegistry sharedResources;
        auto legacyAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "LegacyCacheAlbedo"));
        auto legacyNormalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "LegacyCacheNormal"));
        auto legacyMaterialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "LegacyCacheMaterial"));
        auto legacyEmissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "LegacyCacheEmissive"));
        auto legacyDepthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "LegacyCacheDepth"));
        auto legacyVelocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "LegacyCacheVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", legacyAlbedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", legacyNormalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", legacyMaterialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", legacyEmissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", legacyDepthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", legacyVelocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "CacheModeMega";
        const auto megaMesh = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMesh.IsValid());
        // 区画への書き込みが GPU で完了したメッシュだけが描かれる
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxy;
        proxy.MegaMeshHandle = megaMesh;
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxy.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxy);

        MegaGeometryPass megaGeometryPass;
        ViewRenderContext context;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;
        context.Resources.MegaGeometry = &renderResources.MegaGeometry();
        assert(megaGeometryPass.Initialize(context));

        megaGeometryPass.Setup(context);
        assert(!device->CreatedRenderPassDescs.empty());

        bool bFoundLegacyMegaGeometryRenderPassDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bLegacyStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bLegacyStates =
                    bLegacyStates &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.initialState == RHI::ResourceState::ShaderResource;
            }
            bFoundLegacyMegaGeometryRenderPassDesc = bFoundLegacyMegaGeometryRenderPassDesc || bLegacyStates;
        }
        assert(bFoundLegacyMegaGeometryRenderPassDesc);

        const size_t legacyRenderPassDescCount = device->CreatedRenderPassDescs.size();

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&megaGeometryPass);

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        context.CommandList = &commandList;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);
        assert(device->CreatedRenderPassDescs.size() > legacyRenderPassDescCount);

        bool bFoundRecreatedRenderGraphRenderPassDesc = false;
        for (size_t descIndex = legacyRenderPassDescCount;
             descIndex < device->CreatedRenderPassDescs.size();
             ++descIndex)
        {
            const RHI::RenderPassDesc& desc = device->CreatedRenderPassDescs[descIndex];
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bRenderGraphStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bRenderGraphStates =
                    bRenderGraphStates &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.initialState == RHI::ResourceState::RenderTarget &&
                    attachment.finalState == RHI::ResourceState::ShaderResource;
            }
            bFoundRecreatedRenderGraphRenderPassDesc =
                bFoundRecreatedRenderGraphRenderPassDesc || bRenderGraphStates;
        }
        assert(bFoundRecreatedRenderGraphRenderPassDesc);

        megaGeometryPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryPartialNamedGBufferFallsBackToLegacyAttachmentStates()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SharedResourceRegistry sharedResources;
        auto namedAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialNamedAlbedo"));
        auto fallbackAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialFallbackAlbedo"));
        auto fallbackNormalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "PartialFallbackNormal"));
        auto fallbackMaterialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialFallbackMaterial"));
        auto fallbackEmissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "PartialFallbackEmissive"));
        auto fallbackDepthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "PartialFallbackDepth"));
        auto fallbackVelocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "PartialFallbackVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", fallbackAlbedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", fallbackNormalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", fallbackMaterialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", fallbackEmissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", fallbackDepthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", fallbackVelocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "PartialFallbackMega";
        const auto megaMesh = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMesh.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxy;
        proxy.MegaMeshHandle = megaMesh;
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxy.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxy);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        NamedGBufferAlbedoProducerPass albedoProducer(namedAlbedoTexture);
        MegaGeometryPass megaGeometryPass;
        graph.AddPass(&albedoProducer);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;
        context.Resources.MegaGeometry = &renderResources.MegaGeometry();
        assert(megaGeometryPass.Initialize(context));

        assert(graph.Compile(context));
        uint32_t gbufferAlbedoVersion = 99;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferAlbedo,
                                                gbufferAlbedoVersion));
        assert(gbufferAlbedoVersion == 0);

        bool bHasGBufferAttachmentAccess = false;
        for (uint32_t accessIndex = 0;
             accessIndex < graph.GetDeclaredPassAccessCount(megaGeometryPassIndex);
             ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            bool bAttachment = false;
            RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
            RGAttachmentKind kind = RGAttachmentKind::Color;
            RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;
            assert(graph.TryGetDeclaredPassAccess(megaGeometryPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState,
                                                  &bAttachment,
                                                  &loadOp,
                                                  &storeOp,
                                                  &kind,
                                                  &mutability));
            bHasGBufferAttachmentAccess = bHasGBufferAttachmentAccess || bAttachment;
        }
        assert(!bHasGBufferAttachmentAccess);

        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        bool bSawNamedAlbedoBarrier = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Texture != namedAlbedoTexture.get())
            {
                continue;
            }

            bSawNamedAlbedoBarrier = true;
        }
        assert(!bSawNamedAlbedoBarrier);

        bool bFoundLegacyAttachmentStateDesc = false;
        bool bFoundRenderGraphAttachmentStateDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bLegacyStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource;
            bool bRenderGraphStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                if (attachment.loadOp != RHI::AttachmentLoadOp::Load)
                {
                    bLegacyStates = false;
                    bRenderGraphStates = false;
                    continue;
                }

                bLegacyStates =
                    bLegacyStates &&
                    attachment.initialState == RHI::ResourceState::ShaderResource;
                bRenderGraphStates =
                    bRenderGraphStates &&
                    attachment.initialState == RHI::ResourceState::RenderTarget;
            }

            bFoundLegacyAttachmentStateDesc = bFoundLegacyAttachmentStateDesc || bLegacyStates;
            bFoundRenderGraphAttachmentStateDesc =
                bFoundRenderGraphAttachmentStateDesc || bRenderGraphStates;
        }
        assert(bFoundLegacyAttachmentStateDesc);
        assert(!bFoundRenderGraphAttachmentStateDesc);

        megaGeometryPass.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    // フレームごとに、描くインスタンスの並び（プロキシ）を入れ替える関数。a・b は最初のフレームのプロキシ
    using MegaProxyScript = void (*)(uint32_t frameIndex,
                                     const MegaGeometryProxy &a,
                                     const MegaGeometryProxy &b,
                                     Container::VariableArray<MegaGeometryProxy> &outProxies);

    // 1フレームの記録が「前のフレームで見えた」ビットのバッファへ行った0埋め（範囲）とコピー（古いバッファからの引き継ぎ）
    struct MegaVisibilityFrameRecord
    {
        Container::VariableArray<FakeCommandList::VisibilityFill> Fills;
        Container::VariableArray<FakeCommandList::VisibilityCopy> Copies;
    };

    // 記録を終えたときの MegaGeometryPass のフレームごとの資源の状態（通し番号の扱いの検査用）
    struct MegaFrameResourceProbe
    {
        uint64_t RenderFrameCount = 0;
        uint32_t FrameSlotCapacity = 0;
        size_t RetiredBufferCount = 0;
    };

    // 2つのMegaMeshインスタンスを持つパスのフレームコマンドを記録する（bOcclusionCulling=false は --mega-occlusion=off）。
    // frameCount 回記録し、script があればフレームごとにプロキシを入れ替える。outVisibilityFrames にはフレームごとの
    // 見えたビットの0埋めとコピーを入れる。bSeparateMaterials なら2つ目のメッシュだけ別の材質（ベースカラー）にする。
    void RecordMegaGeometryTwoInstances(bool bOcclusionCulling,
                                        FakeCommandList &commandList,
                                        float maxDepth = 1.0f,
                                        uint32_t frameCount = 1,
                                        MegaProxyScript script = nullptr,
                                        Container::VariableArray<MegaVisibilityFrameRecord> *outVisibilityFrames = nullptr,
                                        bool bSeparateMaterials = false,
                                        bool bSkipGBufferDraw = false,
                                        MegaFrameResourceProbe *outProbe = nullptr)
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();
        if (bSkipGBufferDraw)
        {
            // 幾何の解決に対応する装置（頂点のデバイスアドレス・拡張形式の storage image）
            device->EnableVisibilityResolveCapabilities();
        }

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        RenderResources renderResources;
        assert(renderResources.Initialize(device));
        renderResources.MegaGeometry().SetOcclusionCullingEnabled(bOcclusionCulling);

        SharedResourceRegistry sharedResources;
        auto albedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "RecordAlbedo"));
        auto normalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RecordNormal"));
        auto materialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "RecordMaterial"));
        auto emissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RecordEmissive"));
        auto depthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "RecordDepth"));
        // MegaGeometryPass は velocity も GBuffer へ書くため、velocity が無いとフレームバッファを作らず記録しない
        auto velocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "RecordVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", materialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", emissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", velocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "RecordMegaA";
        const auto megaMeshA = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshA.IsValid());

        createInfo.DebugName = "RecordMegaB";
        if (bSeparateMaterials)
        {
            createInfo.Material.BaseColor[0] = 0.25f;
        }
        const auto megaMeshB = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshB.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxyA;
        proxyA.ObjectId = 1;
        proxyA.ComponentId = 10;
        proxyA.MegaMeshHandle = megaMeshA;
        proxyA.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxyA.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxyA);

        MegaGeometryProxy proxyB = proxyA;
        proxyB.ObjectId = 2;
        proxyB.ComponentId = 20;
        proxyB.MegaMeshHandle = megaMeshB;
        proxies.push_back(proxyB);

        ViewRenderContext context;
        context.Device = device.get();
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;

        MegaGeometryPass megaGeometryPass;
        megaGeometryPass.SetSkipGBufferDraw(bSkipGBufferDraw);
        assert(megaGeometryPass.Initialize(context));
        megaGeometryPass.Setup(context);

        CameraProxy camera;
        camera.Viewport.Width = 128.0f;
        camera.Viewport.Height = 64.0f;
        RHI::Viewport viewport;
        viewport.width = 128.0f;
        viewport.height = 64.0f;
        viewport.maxDepth = maxDepth;
        RHI::ScissorRect scissor;
        scissor.right = 128;
        scissor.bottom = 64;

        FrameCommand frameCommand = FrameCommand::CreateMegaGeometryPass(&megaGeometryPass,
                                                                         &renderResources.MegaGeometry(),
                                                                         camera,
                                                                         true,
                                                                         viewport,
                                                                         scissor,
                                                                         DebugViewMode::Normal);
        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            if (script)
            {
                script(frameIndex, proxyA, proxyB, proxies);
                megaGeometryPass.Setup(context);
            }

            const size_t fillsBefore = commandList.VisibilityFills.size();
            const size_t copiesBefore = commandList.VisibilityCopies.size();
            megaGeometryPass.RecordFrameCommand(frameCommand.MegaGeometry, &commandList);
            if (outVisibilityFrames)
            {
                MegaVisibilityFrameRecord frameRecord;
                for (size_t index = fillsBefore; index < commandList.VisibilityFills.size(); ++index)
                {
                    frameRecord.Fills.push_back(commandList.VisibilityFills[index]);
                }
                for (size_t index = copiesBefore; index < commandList.VisibilityCopies.size(); ++index)
                {
                    frameRecord.Copies.push_back(commandList.VisibilityCopies[index]);
                }
                outVisibilityFrames->push_back(frameRecord);
            }
        }

        if (outProbe)
        {
            outProbe->RenderFrameCount = megaGeometryPass.GetRenderFrameCount();
            outProbe->FrameSlotCapacity = megaGeometryPass.GetFrameSlotCapacity();
            outProbe->RetiredBufferCount = megaGeometryPass.GetRetiredBufferCount();
        }
        megaGeometryPass.Shutdown();
        renderResources.Shutdown();
        shaderManager.Shutdown();
    }

    // 遮蔽カリングを使わない（--mega-occlusion=off）と、全インスタンスを1回のカリングで選び、1回のrender passで描く。
    // 同じ材質・同じプールの塊のインスタンスは1つの区間にまとまり、区間ごとに1回の間接描画になる
    void TestMegaGeometryRecordFrameCommandBatchesInstancesInSingleRenderPass()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(false, commandList);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 1);

        // 並び: 全インスタンスのカリング（1回）→ render pass 1つの中で区間ごとに1回描く
        const char expected[] = {'D', 'B', 'I', 'E'};
        assert(commandList.CallSequence.size() == sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(commandList.CallSequence[i] == expected[i]);
        }

        // 区間のコマンドの最大数は、区間のインスタンスのクラスタ数の合計（1 + 1）
        assert(commandList.IndirectDraws.size() == 1);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 2);
    }

    // 材質が違うインスタンスは別の区間になり、区間ごとに1回の間接描画を発行する。カリングは材質によらず全インスタンスで1回
    void TestMegaGeometryRecordFrameCommandSplitsSectionsByMaterial()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(false, commandList, 1.0f, 1, nullptr, nullptr, true);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 2);

        const char expected[] = {'D', 'B', 'I', 'I', 'E'};
        assert(commandList.CallSequence.size() == sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(commandList.CallSequence[i] == expected[i]);
        }

        // 区間のコマンドは連続した範囲（1つ目の区間のクラスタ数 1 の次から2つ目）。コマンドは20バイト
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 1);
        assert(commandList.IndirectDraws[1].OffsetBytes == 20);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 1);
    }

    // インスタンスの表（1つの storage buffer）: 並びはインスタンスの並び。ワークグループ・区間・見えたビットの区画・
    // 頂点とインデックスの基点（プールの塊の先頭から）が入る
    void TestMegaGeometryRecordFrameCommandWritesInstanceTable()
    {
        GMegaInstanceTableUpdates.clear();
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 1, nullptr, nullptr, true);

        assert(GMegaInstanceTableUpdates.size() == 1);
        const Container::VariableArray<uint8_t> &bytes = GMegaInstanceTableUpdates[0];
        // MegaGeometryPass::GPUMegaInstance は 192 バイト（world 0・previousWorld 64・LODSphere 128・
        // クラスタ配列のアドレス下位/上位・クラスタ数・最初のワークグループ 144〜156・区間・頂点の基点・インデックスの基点・見えたビットの先頭 160〜172・
        // グループの BVH の節の配列のアドレス下位/上位・節の数・予約 176〜188）
        constexpr size_t InstanceBytes = 192;
        assert(bytes.size() == 2 * InstanceBytes);
        auto readUint = [&bytes](size_t instanceIndex, size_t byteOffset) -> uint32_t
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes.data() + instanceIndex * InstanceBytes + byteOffset, sizeof(value));
            return value;
        };

        for (size_t instanceIndex = 0; instanceIndex < 2; ++instanceIndex)
        {
            assert((readUint(instanceIndex, 144) | readUint(instanceIndex, 148)) != 0); // クラスタ配列のアドレス
            assert(readUint(instanceIndex, 152) == 1);                                  // クラスタ数
            // 1クラスタ = 1ワークグループなので、最初のワークグループの通し番号はインスタンスの番号
            assert(readUint(instanceIndex, 156) == instanceIndex);
            // 材質が違うので区間も別
            assert(readUint(instanceIndex, 160) == instanceIndex);
            // 見えたビットは、全インスタンスで1本の配列にクラスタ数ずつ詰める
            assert(readUint(instanceIndex, 172) == instanceIndex);
        }
        // 2つのメッシュは同じプールの塊の中にあり、2つ目は1つ目の後ろ
        assert(readUint(0, 164) < readUint(1, 164));
        assert(readUint(0, 168) < readUint(1, 168));
    }

    // 遮蔽カリング（既定）は2パス: 1パス目のカリングと描画 → HZBの生成 → 2パス目のカリングと描画。
    // どちらのパスも、カリングは全インスタンスで1回、描画は区間ごとに1回
    void TestMegaGeometryRecordFrameCommandRecordsTwoPassOcclusion()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList);

        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        // 同じ材質の2インスタンスは1つの区間なので、1パス目と2パス目で1回ずつ描く
        assert(commandList.DrawCallCount == 2);

        // 並び: [1パス目のカリング] [render pass: 描画] [HZBの各ミップ（Dispatchだけ）] [2パス目のカリング] [render pass: 描画]
        const auto &sequence = commandList.CallSequence;
        const char head[] = {'D', 'B', 'I', 'E'};
        const char tail[] = {'D', 'B', 'I', 'E'};
        assert(sequence.size() > sizeof(head) + sizeof(tail));
        for (size_t i = 0; i < sizeof(head); ++i)
        {
            assert(sequence[i] == head[i]);
        }
        for (size_t i = 0; i < sizeof(tail); ++i)
        {
            assert(sequence[sequence.size() - sizeof(tail) + i] == tail[i]);
        }
        // 間は HZB の生成で、描画なしのディスパッチだけ（128x64 の深度は、ミップ0 が 64x32 の7段）
        const size_t middleBegin = sizeof(head);
        const size_t middleEnd = sequence.size() - sizeof(tail);
        assert(middleEnd - middleBegin == 7);
        for (size_t i = middleBegin; i < middleEnd; ++i)
        {
            assert(sequence[i] == 'D');
        }

        // コマンドの範囲はパスごとに別（2パス目は1パス目の範囲の後ろ。1パスのコマンド数 2、1コマンド20バイト）
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 2);
        assert(commandList.IndirectDraws[1].OffsetBytes == 2 * 20);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 2);
    }

    // 幾何の解決が GBuffer を書くとき（SetSkipGBufferDraw）で、記録を移す相手（ID のラスタ）を渡していない構成。
    // MegaGeometryPass は描画の呼び出しだけを省き、2 パスの並び（1 パス目のカリング → render pass → HZB → 2 パス目のカリング →
    // render pass）は保つ。1 パス目の深度は GBuffer に入らないので HZB は空の深度（1.0 で消した値のまま）から作られ、2 パス目の
    // 遮蔽の判定は何も隠さない（隠れていない側＝描く側に倒れる）。見える物は欠けず、判定が甘くなるだけ。
    // ID のラスタを渡した構成（SceneView の On）は、HZB を ID の深度から作る（TestVisibilityRasterOnBuildsHiZFromIdDepthBetweenTwoPasses）
    void TestMegaGeometrySkipGBufferDrawKeepsTwoPassOrderWithoutDraws()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 1, nullptr, nullptr, false, true);

        // render pass は 2 回開き閉じるが、描画の呼び出し（間接描画・直接描画）は 0 回
        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        assert(commandList.IndirectDraws.empty());
        assert(commandList.DrawCallCount == 0);

        // 並び: [1パス目のカリング] [render pass: 空] [HZBの各ミップ（Dispatchだけ 7 段）] [2パス目のカリング] [render pass: 空]
        const auto &sequence = commandList.CallSequence;
        const char expected[] = {'D', 'B', 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D', 'B', 'E'};
        assert(sequence.size() == sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(sequence[i] == expected[i]);
        }
    }

    // 深度の範囲が 0〜1 でないと、保存される深度は NDC の深度と一致せず HZB の判定が成り立たないので、従来の経路で描く
    void TestMegaGeometryTwoPassFallsBackWhenDepthRangeIsNotUnit()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList, 0.5f);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 1);
    }

    // 並びは A（ObjectId 1）と B（ObjectId 2）。見えたビットは、直前のフレームにも描かれた同じコンポーネントのインスタンスだけが引き継ぐ
    void MegaVisibilityReaddScript(uint32_t frameIndex,
                                   const MegaGeometryProxy &a,
                                   const MegaGeometryProxy &b,
                                   Container::VariableArray<MegaGeometryProxy> &outProxies)
    {
        outProxies.clear();
        MegaGeometryProxy readded = a;
        // 3・4: 同じ ObjectId・メッシュで新しいコンポーネント（ComponentId 11）。5: 描かれ続けたまま ComponentId だけ変わる。
        // 7: 描かれなかった後の再追加（ComponentId は5と同じ）
        if (frameIndex == 3 || frameIndex == 4)
        {
            readded.ComponentId = 11;
        }
        else if (frameIndex >= 5)
        {
            readded.ComponentId = 12;
        }
        const bool bHasA = frameIndex != 2 && frameIndex != 6;
        if (bHasA)
        {
            outProxies.push_back(readded);
        }
        outProxies.push_back(b);
    }

    // 追加されたインスタンス・再追加・コンポーネントの作り直しでは、前のフレームで見えたビットを捨てる（0で埋め直す）。
    // 配置（インスタンスの並びとクラスタ数）が変わると、ぴったりの大きさで作り直して0で埋め、描かれ続けたインスタンスの
    // 区画だけを古いバッファから写す。1インスタンスの区画は 1 クラスタぶん（4バイト）
    void TestMegaGeometryTwoPassDiscardsVisibilityOnReaddAndComponentChange()
    {
        FakeCommandList commandList;
        Container::VariableArray<MegaVisibilityFrameRecord> frames;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 8, &MegaVisibilityReaddScript, &frames);

        assert(frames.size() == 8);

        auto expectWholeFill = [](const MegaVisibilityFrameRecord &frame, uint64_t sizeBytes) -> void
        {
            assert(frame.Fills.size() == 1);
            assert(frame.Fills[0].OffsetBytes == 0);
            assert(frame.Fills[0].SizeBytes == sizeBytes);
        };
        auto expectCarriedOver = [](const MegaVisibilityFrameRecord &frame, uint64_t sourceOffset, uint64_t destinationOffset) -> void
        {
            assert(frame.Copies.size() == 1);
            assert(frame.Copies[0].SourceOffsetBytes == sourceOffset);
            assert(frame.Copies[0].DestinationOffsetBytes == destinationOffset);
            assert(frame.Copies[0].SizeBytes == sizeof(uint32_t));
        };

        // 0: A・B が新規 → 作って0で埋める（引き継ぎ無し）
        expectWholeFill(frames[0], 2 * sizeof(uint32_t));
        assert(frames[0].Copies.empty());
        // 1: 配置が同じで、2つとも引き継ぐ → 何もしない
        assert(frames[1].Fills.empty());
        assert(frames[1].Copies.empty());
        // 2: A が外れる → [B] へ作り直し、B の区画（古い位置 4）を先頭へ写す
        expectWholeFill(frames[2], sizeof(uint32_t));
        expectCarriedOver(frames[2], 1 * sizeof(uint32_t), 0);
        // 3: A を新しいコンポーネントで再追加 → [A, B] へ作り直し、B（古い位置 0）を2番目へ写す。A は0から
        expectWholeFill(frames[3], 2 * sizeof(uint32_t));
        expectCarriedOver(frames[3], 0, 1 * sizeof(uint32_t));
        // 4: 引き継ぐ → 何もしない
        assert(frames[4].Fills.empty());
        assert(frames[4].Copies.empty());
        // 5: 描かれ続けたまま ComponentId だけ変わる → 配置は同じで、A の区画（先頭の4バイト）だけ0に戻す。B は触らない
        assert(frames[5].Fills.size() == 1);
        assert(frames[5].Fills[0].OffsetBytes == 0);
        assert(frames[5].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[5].Copies.empty());
        // 6: A が外れる → [B] へ作り直し、B を写す
        expectWholeFill(frames[6], sizeof(uint32_t));
        expectCarriedOver(frames[6], 1 * sizeof(uint32_t), 0);
        // 7: 描かれなかった後の再追加 → [A, B] へ作り直し、B だけ写す（A は同じ ComponentId でも0から）
        expectWholeFill(frames[7], 2 * sizeof(uint32_t));
        expectCarriedOver(frames[7], 0, 1 * sizeof(uint32_t));
    }

    // 全インスタンスが一度消える（A → 空 → A）。同じ ObjectId・ComponentId・メッシュでも、再追加でビットを捨てる
    void MegaVisibilityEmptyGapScript(uint32_t frameIndex,
                                      const MegaGeometryProxy &a,
                                      const MegaGeometryProxy &,
                                      Container::VariableArray<MegaGeometryProxy> &outProxies)
    {
        outProxies.clear();
        if (frameIndex != 1)
        {
            outProxies.push_back(a);
        }
    }

    // 空のフレームは記録を省くので、記録の中だけで連続性を見ると再追加が引き継ぎに見える。Setup で脱落を検出して捨てる
    void TestMegaGeometryTwoPassDiscardsVisibilityWhenAllInstancesVanish()
    {
        FakeCommandList commandList;
        Container::VariableArray<MegaVisibilityFrameRecord> frames;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 3, &MegaVisibilityEmptyGapScript, &frames);

        assert(frames.size() == 3);
        // 0: A が新規 → 作って0で埋める
        assert(frames[0].Fills.size() == 1);
        assert(frames[0].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[0].Copies.empty());
        // 1: 空（記録なし）
        assert(frames[1].Fills.empty());
        assert(frames[1].Copies.empty());
        // 2: A を同じ ObjectId・ComponentId・メッシュで再追加 → 配置は同じでも引き継がず、A の区画を0に戻す
        assert(frames[2].Fills.size() == 1);
        assert(frames[2].Fills[0].OffsetBytes == 0);
        assert(frames[2].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[2].Copies.empty());
    }

    // FrameCommand::CreateMegaGeometryPass で直接組んだコマンドは通し番号 0 のまま。0 を「同じフレーム」の印として扱うと、
    // フレームごとの資源の組が記録ごとに増え続け、退避したバッファの寿命（記録したフレームの数）も進まず解放されない。
    // 記録ごとに別のフレームとして扱うので、組は 1 つを使い回し、退避したバッファは一定のフレーム後に解放される
    void TestMegaGeometryRecordWithoutFrameSerialTreatsEachRecordAsAFrame()
    {
        constexpr uint32_t RecordCount = 16;
        FakeCommandList commandList;
        MegaFrameResourceProbe probe;
        RecordMegaGeometryTwoInstances(
            true, commandList, 1.0f, RecordCount, &MegaVisibilityReaddScript, nullptr, false, false, &probe);

        assert(probe.RenderFrameCount == RecordCount);
        assert(probe.FrameSlotCapacity == 1);
        // 配置が変わるフレーム（2・3・6・7）で 4 本が退避する。8 フレームより前に退避したものは、もう解放されている
        assert(probe.RetiredBufferCount <= 2);
    }

    void TestMegaGeometryNativeExecuteSkipsWhenNoInstances()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeDeclareCreatesTransientOutputs()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(pass.GetAlbedoHandle().IsValid());
        assert(pass.GetNormalHandle().IsValid());
        assert(pass.GetMaterialHandle().IsValid());
        assert(pass.GetEmissiveHandle().IsValid());
        assert(pass.GetVelocityHandle().IsValid());
        assert(pass.GetDepthHandle().IsValid());

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 6);
        for (uint32_t i = 0; i < 5; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == 0);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[5].Kind == RGBarrierKind::Texture);
        assert(barriers[5].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[5].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[5].PassIndex == 0);
        assert(barriers[5].CompiledOrderIndex == 0);
    }

    void TestGBufferSSAONativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAORawHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);

        bool bHasDepthRead = false;
        bool bHasNormalRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssaoPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Write;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssaoPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
        }
        assert(bHasDepthRead);
        assert(bHasNormalRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 8);
        for (uint32_t i = 0; i < 5; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == gbufferPassIndex);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[5].Kind == RGBarrierKind::Texture);
        assert(barriers[5].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[5].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[5].PassIndex == gbufferPassIndex);
        assert(barriers[5].CompiledOrderIndex == 0);

        assert(barriers[6].Kind == RGBarrierKind::Texture);
        assert(barriers[6].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[6].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[6].PassIndex == ssaoPassIndex);
        assert(barriers[6].CompiledOrderIndex == 1);
        assert(barriers[7].Kind == RGBarrierKind::Texture);
        assert(barriers[7].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[7].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[7].PassIndex == ssaoPassIndex);
        assert(barriers[7].CompiledOrderIndex == 1);
    }

    void TestGBufferSSAOLightingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(lightingPass.GetIndirectSpecularHandle().IsValid());
        assert(lightingPass.GetSpecularReflectanceHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 11);

        bool bHasAlbedoRead = false;
        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasEmissiveRead = false;
        bool bHasSSAORead = false;
        bool bHasSceneColorWrite = false;
        bool bHasIndirectSpecularWrite = false;
        bool bHasSpecularReflectanceWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(lightingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(lightingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetAlbedoHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasAlbedoRead = true;
            }
            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == gbufferPass.GetEmissiveHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasEmissiveRead = true;
            }
            if (resource == ssaoPass.GetSSAOBlurredHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSSAORead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorWrite = true;
            }
            // SSRPassが読む環境光の鏡面反射とその反射率も、描画先として書いて読める状態で終える
            if (resource == lightingPass.GetIndirectSpecularHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasIndirectSpecularWrite = true;
            }
            if (resource == lightingPass.GetSpecularReflectanceHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSpecularReflectanceWrite = true;
            }
        }

        assert(bHasAlbedoRead);
        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasEmissiveRead);
        assert(bHasSSAORead);
        assert(bHasSceneColorWrite);
        assert(bHasIndirectSpecularWrite);
        assert(bHasSpecularReflectanceWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 12);
        assert(barriers[8].Kind == RGBarrierKind::Texture);
        assert(barriers[8].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[8].AfterState == RHI::ResourceState::UnorderedAccess);
        assert(barriers[8].PassIndex == lightingPassIndex);
        assert(barriers[8].CompiledOrderIndex == 2);
        assert(barriers[9].Kind == RGBarrierKind::Texture);
        assert(barriers[9].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[9].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[9].PassIndex == lightingPassIndex);
        assert(barriers[9].CompiledOrderIndex == 2);
        for (size_t i = 10; i < 12; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == lightingPassIndex);
            assert(barriers[i].CompiledOrderIndex == 2);
        }
    }

    void TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 11);

        auto hasShaderReadAccess = [&graph](uint32_t passIndex, RGResourceHandle expected) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Write;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == RGAccessMode::Read &&
                           state == RHI::ResourceState::ShaderResource &&
                           finalState == RHI::ResourceState::ShaderResource;
                }
            }

            return false;
        };

        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetAlbedoHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetMaterialHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetEmissiveHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, ssaoPass.GetSSAOBlurredHandle()));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
    }

    void TestLightingNativeDeclareReadsNamedShadowMap()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        ShadowMapPass shadowMapPass;
        assert(shadowMapPass.Initialize(context));
        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t shadowMapPassIndex = graph.AddPass(&shadowMapPass);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 12);

        bool bHasShadowMapRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(lightingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Write;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(lightingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));
            if (resource == shadowMapPass.GetShadowMapHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasShadowMapRead = true;
            }
        }

        assert(bHasShadowMapRead);

        const auto& order = graph.GetCompiledPassOrder();
        uint32_t shadowMapOrder = RGInvalidPassIndex;
        uint32_t lightingOrder = RGInvalidPassIndex;
        for (uint32_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
        {
            if (order[orderIndex] == shadowMapPassIndex)
            {
                shadowMapOrder = orderIndex;
            }
            if (order[orderIndex] == lightingPassIndex)
            {
                lightingOrder = orderIndex;
            }
        }

        assert(shadowMapOrder != RGInvalidPassIndex);
        assert(lightingOrder != RGInvalidPassIndex);
        assert(shadowMapOrder < lightingOrder);

        shadowMapPass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingSceneDepthAliasDuplicateFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        RGResourceHandle prepublishedSceneDepth;
        PublishSceneDepthAliasPass prepublishPass(&prepublishedSceneDepth);
        LightingPass lightingPass;

        graph.AddPass(&gbufferPass);
        graph.AddPass(&prepublishPass);
        graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(!graph.Compile(context));
        assert(prepublishedSceneDepth.IsValid());
    }

    void TestForwardTransparentNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);

        bool bHasSceneColorLoadStore = false;
        bool bHasDepthRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(forwardPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            bool bColorAttachmentLoadStore = false;
            RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
            assert(graph.TryGetDeclaredPassAccess(forwardPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState,
                                                  &bColorAttachmentLoadStore,
                                                  &loadOp,
                                                  &storeOp));

            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                assert(bColorAttachmentLoadStore);
                assert(loadOp == RHI::AttachmentLoadOp::Load);
                assert(storeOp == RHI::AttachmentStoreOp::Store);
                bHasSceneColorLoadStore = true;
            }

            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::DepthRead);
                assert(finalState == RHI::ResourceState::DepthRead);
                assert(!bColorAttachmentLoadStore);
                bHasDepthRead = true;
            }
        }

        assert(bHasSceneColorLoadStore);
        assert(bHasDepthRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 14);
        assert(barriers[12].Kind == RGBarrierKind::Texture);
        assert(barriers[12].Resource == lightingPass.GetSceneColorHandle());
        assert(barriers[12].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[12].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[12].PassIndex == forwardPassIndex);
        assert(barriers[12].CompiledOrderIndex == 3);
        assert(barriers[13].Kind == RGBarrierKind::Texture);
        assert(barriers[13].Resource == gbufferPass.GetDepthHandle());
        assert(barriers[13].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[13].AfterState == RHI::ResourceState::DepthRead);
        assert(barriers[13].PassIndex == forwardPassIndex);
        assert(barriers[13].CompiledOrderIndex == 3);
    }

    void TestSSRNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(ssrPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 7);

        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        bool bHasIndirectSpecularRead = false;
        bool bHasSpecularReflectanceRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssrPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssrPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle() && mode == RGAccessMode::Read)
            {
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }
            // 環境光の鏡面反射とその反射率（LightingPassの出力）をシェーダーから読む
            if (resource == lightingPass.GetIndirectSpecularHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasIndirectSpecularRead = true;
            }
            if (resource == lightingPass.GetSpecularReflectanceHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSpecularReflectanceRead = true;
            }
            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);
        assert(bHasIndirectSpecularRead);
        assert(bHasSpecularReflectanceRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 5);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);

        bool bHasDepthToShaderResource = false;
        bool bHasSSROutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != ssrPassIndex)
            {
                continue;
            }

            if (barrier.Resource == gbufferPass.GetDepthHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::DepthRead);
                assert(barrier.AfterState == RHI::ResourceState::ShaderResource);
                bHasDepthToShaderResource = true;
            }

            if (barrier.Resource == ssrPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasSSROutputWrite = true;
            }
        }

        assert(bHasDepthToShaderResource);
        assert(bHasSSROutputWrite);
    }

    void TestBloomNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(bloomPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(bloomPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(bloomPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 6);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);

        bool bHasBloomOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != bloomPassIndex)
            {
                continue;
            }

            if (barrier.Resource == bloomPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasBloomOutputWrite = true;
            }
        }

        assert(bHasBloomOutputWrite);
    }

    void TestToneMappingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(toneMappingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(toneMappingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);

        bool bHasToneMappedOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != toneMappingPassIndex)
            {
                continue;
            }

            if (barrier.Resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasToneMappedOutputWrite = true;
            }
        }

        assert(bHasToneMappedOutputWrite);
    }

    void TestVignetteNativeDeclareDependencies()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(vignettePass.GetToneMappedColorHandle() != toneMappingPass.GetToneMappedColorHandle());
        assert(graph.GetDeclaredPassAccessCount(vignettePassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(vignettePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(vignettePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);

        RGCompiledResourceLifetime vignetteLifetime;
        assert(graph.TryGetCompiledResourceLifetime(vignettePass.GetToneMappedColorHandle(),
                                                    vignetteLifetime));
        assert(vignetteLifetime.bExported);
        assert(vignetteLifetime.bPinnedUntilGraphEnd);
        assert(vignetteLifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());

        bool bHasVignetteOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != vignettePassIndex)
            {
                continue;
            }

            if (barrier.Resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasVignetteOutputWrite = true;
            }
        }

        assert(bHasVignetteOutputWrite);
#endif
    }

    void TestToneMappedVersionChainFeedsFXAAAndUpscale()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;
        FXAAPass fxaaPass;
        UpscalePass upscalePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(fxaaPass.GetToneMappedColorHandle().IsValid());
        assert(upscalePass.GetPresentationColorHandle().IsValid());

        bool bFXAAReadsVignette = false;
        bool bFXAAReadsToneMapping = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(fxaaPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(fxaaPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bFXAAReadsVignette = true;
            }

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                bFXAAReadsToneMapping = true;
            }
        }

        assert(bFXAAReadsVignette);
        assert(!bFXAAReadsToneMapping);

        bool bUpscaleReadsFXAA = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bUpscaleReadsFXAA = true;
            }
        }

        assert(bUpscaleReadsFXAA);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);
        assert(order[2] == fxaaPassIndex);
        assert(order[3] == upscalePassIndex);
#endif
    }

    void TestToneMappedVersionChainFeedsUpscaleWithoutFXAA()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;
        UpscalePass upscalePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(upscalePass.GetPresentationColorHandle().IsValid());

        bool bUpscaleReadsVignette = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bUpscaleReadsVignette = true;
            }
        }

        assert(bUpscaleReadsVignette);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);
        assert(order[2] == upscalePassIndex);
#endif
    }

    void TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        SSRPass ssrPass;
        BloomPass bloomPass;
        ToneMappingPass toneMappingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 7);
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        auto hasAccess = [&graph](uint32_t passIndex,
                                  RGResourceHandle expected,
                                  RGAccessMode expectedMode,
                                  RHI::ResourceState expectedState,
                                  RHI::ResourceState expectedFinalState) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Read;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == expectedMode &&
                           state == expectedState &&
                           finalState == expectedFinalState;
                }
            }

            return false;
        };

        assert(hasAccess(forwardPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Write,
                         RHI::ResourceState::RenderTarget,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(ssrPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(bloomPassIndex,
                         ssrPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(toneMappingPassIndex,
                         bloomPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
    }

    void TestPostProcessMissingNamedInputsCompileWithoutErrors()
    {
        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ForwardPass forwardPass(nullptr, nullptr);
            forwardPass.SetTransparentOnly(true);
            const uint32_t passIndex = graph.AddPass(&forwardPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 0);
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            SSRPass ssrPass;
            const uint32_t passIndex = graph.AddPass(&ssrPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(ssrPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BloomPass bloomPass;
            const uint32_t passIndex = graph.AddPass(&bloomPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(bloomPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ToneMappingPass toneMappingPass;
            const uint32_t passIndex = graph.AddPass(&toneMappingPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        }
    }

    void TestFXAANativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(fxaaPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(fxaaPassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(fxaaPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(fxaaPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 8);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);

        bool bHasFXAAOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != fxaaPassIndex)
            {
                continue;
            }

            if (barrier.Resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasFXAAOutputWrite = true;
            }
        }

        assert(bHasFXAAOutputWrite);
    }

    void TestUpscaleNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(upscalePass.GetPresentationColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(upscalePassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasPresentationWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == upscalePass.GetPresentationColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasPresentationWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasPresentationWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 9);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);
        assert(order[8] == upscalePassIndex);

        RGCompiledResourceLifetime presentationLifetime;
        assert(graph.TryGetCompiledResourceLifetime(upscalePass.GetPresentationColorHandle(),
                                                    presentationLifetime));
        assert(presentationLifetime.bExported);
        assert(presentationLifetime.bPinnedUntilGraphEnd);
        assert(presentationLifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());

        bool bHasPresentationOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != upscalePassIndex)
            {
                continue;
            }

            if (barrier.Resource == upscalePass.GetPresentationColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasPresentationOutputWrite = true;
            }
        }

        assert(bHasPresentationOutputWrite);
    }

    void TestGBufferSSAOLightingNativeExecuteWithoutSharedResources()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr sceneColorTexture = graphResources.GetTexture(lightingPass.GetSceneColorHandle());
        assert(sceneColorTexture);
        assert(graph.GetLastExecutedPassCount() == 3);
        assert(result.TextureOutputs.size() == 1);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::SceneColor, exportedTexture));
        assert(exportedTexture.get() == sceneColorTexture.get());
        exportedTexture.reset();
        assert(!result.TryGetTexture(RenderGraphResourceNames::SceneDepth, exportedTexture));
        assert(exportedTexture == nullptr);
        // 環境光の鏡面反射とその反射率は、SceneColorと同じ描画で書く（書き出さず、同じRenderGraphの中で読む）
        assert(graphResources.GetTexture(lightingPass.GetIndirectSpecularHandle()));
        assert(graphResources.GetTexture(lightingPass.GetSpecularReflectanceHandle()));
        assert(commandList.Barriers.size() == 12);
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    LightProxy MakeLightingBufferPointLight(uint32_t index)
    {
        LightProxy proxy;
        proxy.LightId = index + 1;
        proxy.Type = LightType::Point;
        proxy.PositionX = 1.0f + static_cast<float>(index);
        proxy.PositionY = 2.0f + static_cast<float>(index);
        proxy.PositionZ = 3.0f + static_cast<float>(index);
        proxy.DirectionX = -0.1f;
        proxy.DirectionY = -0.2f;
        proxy.DirectionZ = -0.3f;
        proxy.InnerConeAngle = 0.81f;
        proxy.OuterConeAngle = 0.62f;
        proxy.ColorR = 1.0f;
        proxy.ColorG = 1.0f;
        proxy.ColorB = 1.0f;
        proxy.Intensity = 2.0f + static_cast<float>(index);
        proxy.Range = 25.0f + static_cast<float>(index);
        proxy.bVisible = true;
        return proxy;
    }

    RHI::ResourceBindType FindDescriptorBindingType(const RHI::DescriptorSetDesc& desc,
                                                    uint32_t binding)
    {
        for (const RHI::DescriptorBinding& descriptorBinding : desc.bindings)
        {
            if (descriptorBinding.binding == binding)
            {
                return descriptorBinding.type;
            }
        }

        assert(false);
        return RHI::ResourceBindType::ConstantBuffer;
    }

    void AssertBinding5IsStructuredBuffer(const FakeDevice& device)
    {
        assert(FindDescriptorBindingType(device.LastDescriptorSetDesc, 5) ==
               RHI::ResourceBindType::StructuredBuffer);
        assert(!device.LastGraphicsPipelineDescriptorSetLayouts.empty());
        assert(FindDescriptorBindingType(device.LastGraphicsPipelineDescriptorSetLayouts[0], 5) ==
               RHI::ResourceBindType::StructuredBuffer);
    }

    const BufferCreationRecord& FindLastLightArraySSBOCreation(const FakeDevice& device)
    {
        for (size_t i = device.CreatedBuffers.size(); i > 0; --i)
        {
            const BufferCreationRecord& record = device.CreatedBuffers[i - 1];
            if (IsDebugName(record.Desc.DebugName, "LightArraySSBO"))
            {
                return record;
            }
        }

        assert(false);
        return device.CreatedBuffers[0];
    }

    uint32_t CountLightArraySSBOCreations(const FakeDevice& device)
    {
        uint32_t count = 0;
        for (const BufferCreationRecord& record : device.CreatedBuffers)
        {
            if (IsDebugName(record.Desc.DebugName, "LightArraySSBO"))
            {
                ++count;
            }
        }

        return count;
    }

    bool HasRenderEvent(FakeRenderEvent event)
    {
        for (FakeRenderEvent recordedEvent : GRenderEvents)
        {
            if (recordedEvent == event)
            {
                return true;
            }
        }

        return false;
    }

    void DecodeLightingParams(GPULightingParams& outParams)
    {
        assert(GLastDescriptorBinding4UpdateBytes.size() == sizeof(GPULightingParams));
        std::memcpy(&outParams, GLastDescriptorBinding4UpdateBytes.data(), sizeof(GPULightingParams));
    }

    float ReadPackedLightFloat(const GPULightData& light, size_t byteOffset)
    {
        float value = 0.0f;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&light);
        std::memcpy(&value, bytes + byteOffset, sizeof(value));
        return value;
    }

    FakeBuffer& GetBoundLightArrayBuffer()
    {
        assert(GLastDescriptorBinding5Buffer != nullptr);
        return *static_cast<FakeBuffer*>(GLastDescriptorBinding5Buffer);
    }

    GPULightData DecodeLightDataAt(uint32_t index)
    {
        const size_t offset = static_cast<size_t>(index) * sizeof(GPULightData);
        assert(GLastDescriptorBinding5UpdateBytes.size() >= offset + sizeof(GPULightData));

        GPULightData light = {};
        std::memcpy(&light, GLastDescriptorBinding5UpdateBytes.data() + offset, sizeof(GPULightData));
        return light;
    }

    void ExecuteLightingGraphWithLights(FakeDevice& device,
                                        ShaderManager& shaderManager,
                                        RHI::TransientResourcePool& pool,
                                        RenderResources& renderResources,
                                        SceneRenderer& renderer,
                                        LightingPass& lightingPass,
                                        Container::VariableArray<LightProxy>& lightProxies,
                                        FakeCommandList& commandList,
                                        uint64_t frameIndex)
    {
        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(frameIndex);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&lightingPass);

        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = &device;
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotLightProxies = &lightProxies;
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        ResetLightingDescriptorCapture();
        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(pendingFrameCommands.empty());

        gbufferPass.Shutdown();
        graph.Shutdown();
    }

    void TestLightingNativeExecuteBindsExpandedLightStorageBuffer()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;
        Container::VariableArray<LightProxy> lightProxies;
        for (uint32_t i = 0; i < 20; ++i)
        {
            lightProxies.push_back(MakeLightingBufferPointLight(i));
        }

        FakeCommandList commandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       lightProxies,
                                       commandList,
                                       0);

        const uint32_t expectedBytes = 20u * static_cast<uint32_t>(sizeof(GPULightData));
        const BufferCreationRecord& lightBufferRecord = FindLastLightArraySSBOCreation(*device);
        assert(lightBufferRecord.Desc.Size >= expectedBytes);
        assert(HasUsage(lightBufferRecord.Desc.Usage, RHI::ResourceUsage::StorageBuffer));
        assert(HasUsage(lightBufferRecord.Desc.Usage, RHI::ResourceUsage::ShaderRead));
        assert(lightBufferRecord.Desc.CPUAccessible);
        AssertBinding5IsStructuredBuffer(*device);

        assert(GLastDescriptorBinding5Buffer != nullptr);
        assert(GLastDescriptorBinding5Offset == 0);
        assert(GLastDescriptorBinding5Size >= expectedBytes);
        assert(GLastDescriptorBinding5UpdateBytes.size() == expectedBytes);

        GPULightingParams params = {};
        DecodeLightingParams(params);
        assert(params.lightCount == 20);
        assert(params.ddgi.info[0] == 0u);
        assert(params.ddgi.probeCounts[3] == 0u);

        const GPULightData first = DecodeLightDataAt(0);
        const GPULightData last = DecodeLightDataAt(19);
        assert(first.position[0] == lightProxies[0].PositionX);
        assert(first.position[1] == lightProxies[0].PositionY);
        assert(first.position[2] == lightProxies[0].PositionZ);
        assert(first.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(first.direction[3] == lightProxies[0].InnerConeAngle);
        assert(ReadPackedLightFloat(first, 44) == lightProxies[0].Intensity);
        assert(ReadPackedLightFloat(first, 48) == lightProxies[0].Range);
        assert(ReadPackedLightFloat(first, 52) == lightProxies[0].OuterConeAngle);
        assert(last.position[0] == lightProxies[19].PositionX);
        assert(ReadPackedLightFloat(last, 32) == 1.0f);
        assert(ReadPackedLightFloat(last, 44) == lightProxies[19].Intensity);
        assert(ReadPackedLightFloat(last, 48) == lightProxies[19].Range);

        const uint32_t ssboUpdateIndex = FindRenderEventIndex(FakeRenderEvent::LightSsboUpdate);
        const uint32_t bindStorageIndex = FindRenderEventIndex(FakeRenderEvent::BindStorageBuffer5);
        const uint32_t descriptorUpdateIndex =
            FindRenderEventIndexAfter(FakeRenderEvent::DescriptorSetUpdate, bindStorageIndex);
        const uint32_t commandSetDescriptorIndex =
            FindRenderEventIndexAfter(FakeRenderEvent::CommandSetDescriptorSet, descriptorUpdateIndex);
        assert(ssboUpdateIndex < bindStorageIndex);
        assert(bindStorageIndex < descriptorUpdateIndex);
        assert(descriptorUpdateIndex < commandSetDescriptorIndex);

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteZeroLightsKeepsLogicalCountZeroAndSkipsSsboUpdate()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;
        Container::VariableArray<LightProxy> lightProxies;
        FakeCommandList commandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       lightProxies,
                                       commandList,
                                       0);

        GPULightingParams params = {};
        DecodeLightingParams(params);
        assert(params.lightCount == 0);

        assert(GLastDescriptorBinding5Buffer != nullptr);
        assert(GLastDescriptorBinding5Size >= sizeof(GPULightData));
        const FakeBuffer& lightBuffer = GetBoundLightArrayBuffer();
        assert(lightBuffer.GetSize() >= sizeof(GPULightData));
        assert(lightBuffer.UpdateCallCount == 0);
        assert(GLastDescriptorBinding5UpdateBytes.empty());
        assert(!HasRenderEvent(FakeRenderEvent::LightSsboUpdate));
        assert(commandList.DrawCallCount > 0);
        assert(HasRenderEvent(FakeRenderEvent::Draw));

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingLightBufferGrowthRetainsRetiredBuffersAndReusesCapacity()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;

        Container::VariableArray<LightProxy> oneLight;
        oneLight.push_back(MakeLightingBufferPointLight(0));
        FakeCommandList firstCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       oneLight,
                                       firstCommandList,
                                       0);
        const uint32_t firstSsboCount = CountLightArraySSBOCreations(*device);
        assert(firstSsboCount >= 1);
        RHI::TSharedPtr<FakeBufferLifetimeTracker> firstTracker =
            FindLastLightArraySSBOCreation(*device).Tracker;
        assert(firstTracker);
        assert(!firstTracker->bDestroyed);

        Container::VariableArray<LightProxy> twentyLights;
        for (uint32_t i = 0; i < 20; ++i)
        {
            twentyLights.push_back(MakeLightingBufferPointLight(i));
        }
        FakeCommandList secondCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twentyLights,
                                       secondCommandList,
                                       1);
        const uint32_t grownSsboCount = CountLightArraySSBOCreations(*device);
        assert(grownSsboCount > firstSsboCount);
        RHI::TSharedPtr<FakeBufferLifetimeTracker> grownTracker =
            FindLastLightArraySSBOCreation(*device).Tracker;
        assert(grownTracker);
        assert(!firstTracker->bDestroyed);
        assert(!grownTracker->bDestroyed);

        Container::VariableArray<LightProxy> twelveLights;
        for (uint32_t i = 0; i < 12; ++i)
        {
            twelveLights.push_back(MakeLightingBufferPointLight(i));
        }
        FakeCommandList thirdCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twelveLights,
                                       thirdCommandList,
                                       2);
        assert(CountLightArraySSBOCreations(*device) == grownSsboCount);

        lightingPass.Shutdown();
        assert(firstTracker->bDestroyed);
        assert(grownTracker->bDestroyed);

        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingLightBufferGrowthFailureSkipsDescriptorUpdateAndDraw()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;

        Container::VariableArray<LightProxy> oneLight;
        oneLight.push_back(MakeLightingBufferPointLight(0));
        FakeCommandList firstCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       oneLight,
                                       firstCommandList,
                                       0);

        device->FailLightArraySSBOCreateIndex = device->LightArraySSBOCreateCount + 1;

        Container::VariableArray<LightProxy> twentyLights;
        for (uint32_t i = 0; i < 20; ++i)
        {
            twentyLights.push_back(MakeLightingBufferPointLight(i));
        }

        FakeCommandList secondCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twentyLights,
                                       secondCommandList,
                                       1);

        assert(GLastDescriptorBinding5Buffer == nullptr);
        assert(!HasRenderEvent(FakeRenderEvent::BindStorageBuffer5));
        assert(!HasRenderEvent(FakeRenderEvent::DescriptorSetUpdate));
        assert(!HasRenderEvent(FakeRenderEvent::CommandSetDescriptorSet));
        assert(!HasRenderEvent(FakeRenderEvent::Draw));
        assert(secondCommandList.DrawCallCount == 0);

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestProductionDeferredNativeExecuteSkipsLegacyBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        ShadowMapPass shadowMapPass;
        shadowMapPass.SetSceneView(&sceneView);
        shadowMapPass.SetSceneRenderer(&renderer);
        shadowMapPass.SetRegisterLegacyBridge(false);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneView(&sceneView);
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);

        SSAOPass ssaoPass;

        LightingPass lightingPass;
        lightingPass.SetSceneView(&sceneView);
        lightingPass.SetRegisterLegacyBridge(false);

        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);

        SSRPass ssrPass;
        BloomPass bloomPass;
        ToneMappingPass toneMappingPass;
        FXAAPass fxaaPass;
        UpscalePass upscalePass;

        graph.AddPass(&shadowMapPass);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        transparentCommands.push_back(DrawCommand{});
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        assert(graph.GetLastExecutedPassCount() == 10);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ShadowMap"));
        assert(!sharedResources.HasTexture("GBuffer_Albedo"));
        assert(!sharedResources.HasTexture("GBuffer_Normal"));
        assert(!sharedResources.HasTexture("GBuffer_Material"));
        assert(!sharedResources.HasTexture("GBuffer_Emissive"));
        assert(!sharedResources.HasTexture("GBuffer_Depth"));
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("SceneDepth"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        shadowMapPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestForwardTransparentNativeExecuteSkipsBridgeWhenRegisterOutputsDisabled()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        lightingPass.SetRegisterLegacyBridge(false);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        transparentCommands.push_back(DrawCommand{});
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 4);
        assert(commandList.Barriers.size() == 14);
        assert(commandList.BeginRenderPassCount == 5);
        assert(commandList.EndRenderPassCount == 5);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("GBuffer_Depth"));
        assert(!sharedResources.HasTexture("SceneDepth"));

        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 5);
        assert(commandList.Barriers.size() == 16);
        assert(commandList.BeginRenderPassCount == 6);
        assert(commandList.EndRenderPassCount == 6);
        assert(commandList.DrawCallCount == 4);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);

        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 6);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));

        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == toneMappedOutput.get());

        assert(graph.GetLastExecutedPassCount() == 7);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestVignetteNativeExecuteExportsToneMappedColorWithoutBridge()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        VignettePass vignettePass;

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&vignettePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr vignetteOutput = graphResources.GetTexture(vignettePass.GetToneMappedColorHandle());
        assert(vignetteOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == vignetteOutput.get());

        assert(graph.GetLastExecutedPassCount() == 8);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        vignettePass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
#endif
    }

    void TestFXAANativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 8);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == presentationOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSRPass ssrPass;
        graph.AddPass(&ssrPass);

        RHI::TexturePtr normalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal"));
        RHI::TexturePtr materialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackMaterial"));
        RHI::TexturePtr depthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth"));
        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(normalTexture);
        assert(materialTexture);
        assert(depthTexture);
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", materialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == ssrOutput.get());

        ssrPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        BloomPass bloomPass;
        graph.AddPass(&bloomPass);

        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == bloomOutput.get());

        bloomPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestVignetteNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        VignettePass vignettePass;
        graph.AddPass(&vignettePass);

        RHI::TexturePtr toneMappedTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackToneMappedColor"));
        assert(toneMappedTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ToneMappedColor", toneMappedTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr vignetteOutput = graphResources.GetTexture(vignettePass.GetToneMappedColorHandle());
        assert(vignetteOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == vignetteOutput.get());

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == vignetteOutput.get());

        vignettePass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
#endif
    }

    void TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        FXAAPass fxaaPass;
        graph.AddPass(&fxaaPass);

        RHI::TexturePtr toneMappedTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackToneMappedColor"));
        assert(toneMappedTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ToneMappedColor", toneMappedTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == fxaaOutput.get());

        fxaaPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        lightingPass.SetRegisterLegacyBridge(false);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput.get() == fxaaOutput.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LegacyFXAAProducerPass legacyFXAAPass;
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&legacyFXAAPass);

        graph.AddPass(&legacyFXAAPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;

        assert(graph.Compile(context));
        uint32_t toneMappedVersion = 0;
        assert(!graph.TryGetNamedResourceVersion(RenderGraphResourceNames::ToneMappedColor, toneMappedVersion));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr inputTexture = graphResources.GetTexture(legacyFXAAPass.GetToneMappedColorHandle());
        assert(inputTexture);
        RHI::TexturePtr presentationTexture = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationTexture.get() == inputTexture.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == inputTexture.get());

        assert(graph.GetLastExecutedPassCount() == 2);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("PresentationColor"));
        assert(sharedResources.GetTexturePtr("PresentationColor").get() == inputTexture.get());

        upscalePass.Shutdown();
        legacyFXAAPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteAcceptsRawSceneColorBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        ToneMappingPass toneMappingPass;
        graph.AddPass(&toneMappingPass);

        RHI::TextureDesc rawSceneColorDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RawForwardSceneColor");
        RHI::TexturePtr rawSceneColor = device->CreateTexture(rawSceneColorDesc);
        assert(rawSceneColor);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexture("SceneColor", rawSceneColor.get());
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == toneMappedOutput.get());

        toneMappingPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        // SceneColorと、SSRPassへ渡す環境光の鏡面反射・反射率の3枚を消して読める状態へ移す（反射率0でSSRは何も足さない）
        assert(commandList.Barriers.size() == 4);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecutePrefersNamedShadowMapOverRegistry()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RHI::TextureDesc namedShadowDesc =
            RHI::TextureDesc::DepthStencil(128, 128, RHI::Format::D32_FLOAT, "NamedShadowMap");
        namedShadowDesc.ArraySize = PhysicalLightingShadowCascadeCount;
        RHI::TextureDesc legacyShadowDesc =
            RHI::TextureDesc::DepthStencil(128, 128, RHI::Format::D32_FLOAT, "LegacyShadowMap");
        RHI::TexturePtr namedShadowMap = device->CreateTexture(namedShadowDesc);
        RHI::TexturePtr legacyShadowMap = device->CreateTexture(legacyShadowDesc);
        assert(namedShadowMap);
        assert(legacyShadowMap);

        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ShadowMap", legacyShadowMap);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        RGResourceHandle namedShadowHandle;
        NamedShadowMapProducerPass shadowProducer(namedShadowMap, &namedShadowHandle);
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        LightingPass lightingPass;

        const uint32_t shadowPassIndex = graph.AddPass(&shadowProducer);
        graph.AddPass(&gbufferPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        assert(graph.AddDependency(shadowPassIndex, lightingPassIndex));

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        GLastDescriptorBinding6Texture = nullptr;
        assert(graph.Compile(context));
        assert(namedShadowHandle.IsValid());
        assert(graph.Execute(context));

        assert(GLastDescriptorBinding6Texture == namedShadowMap.get());
        assert(GLastDescriptorBinding6Texture != legacyShadowMap.get());
        assert(sharedResources.GetTexturePtr("ShadowMap").get() == legacyShadowMap.get());
        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        assert(commandList.DrawCallCount == 1);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc albedoDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackAlbedo");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TexturePtr albedoTexture = device->CreateTexture(albedoDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        assert(albedoTexture);
        assert(normalTexture);
        assert(depthTexture);

        sharedResources.RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 4);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("SceneDepth"));
        assert(!sharedResources.HasTexture("LightingIndirectSpecular"));
        assert(!sharedResources.HasTexture("LightingSpecularReflectance"));

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass pass;
        pass.SetSceneRenderer(&renderer);
        graph.AddPass(&pass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 6);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("GBuffer_Albedo"));
        assert(sharedResources.HasTexture("GBuffer_Normal"));
        assert(sharedResources.HasTexture("GBuffer_Material"));
        assert(sharedResources.HasTexture("GBuffer_Emissive"));
        assert(sharedResources.HasTexture("GBuffer_Velocity"));
        assert(sharedResources.HasTexture("GBuffer_Depth"));

        pass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSAOPass ssaoPass;
        graph.AddPass(&ssaoPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        assert(depthTexture);
        assert(normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr blurredOutput = graphResources.GetTexture(ssaoPass.GetSSAOBlurredHandle());
        assert(blurredOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(sharedResources.HasTexture("SSAO"));
        assert(sharedResources.GetTexturePtr("SSAO").get() == blurredOutput.get());

        ssaoPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestRecordMeshDrawCallUsesDrawCommandRangesAndFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        MeshDataHandle meshHandle;
        meshHandle.Id = 4401;
        const float vertices[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
        const uint32_t indices[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6};
        assert(renderResources.Meshes().Register(meshHandle, vertices, sizeof(vertices), indices, 12));

        SceneRenderer renderer;
        FakeCommandList commandList;

        DrawCommand firstSubMesh;
        firstSubMesh.Draw.MeshHandle = meshHandle;
        firstSubMesh.Draw.IndexOffset = 3;
        firstSubMesh.Draw.IndexCount = 6;
        firstSubMesh.Draw.VertexOffset = 2;
        assert(renderer.RecordMeshDrawCall(firstSubMesh, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 6);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 3);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 2);

        DrawCommand secondSubMesh;
        secondSubMesh.Draw.MeshHandle = meshHandle;
        secondSubMesh.Draw.IndexOffset = 9;
        secondSubMesh.Draw.IndexCount = 3;
        secondSubMesh.Draw.VertexOffset = 5;
        assert(renderer.RecordMeshDrawCall(secondSubMesh, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 3);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 9);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 5);

        DrawCommand fallback;
        fallback.Draw.MeshHandle = meshHandle;
        fallback.Draw.IndexOffset = 9;
        fallback.Draw.IndexCount = 0;
        fallback.Draw.VertexOffset = 5;
        assert(renderer.RecordMeshDrawCall(fallback, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 12);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 0);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 0);

        assert(commandList.DrawCallCount == 3);
        assert(renderer.GetStats().DrawCallCount == 3);
        assert(renderer.GetStats().TriangleCount == 7);

        renderResources.Shutdown();
    }

    // GBuffer 深度を読むだけの HZB のパス。深度の Load/Store の宣言には触れず（版が増えず）、GBuffer の後ろに並ぶ。
    void TestHiZPyramidNativeDeclareReadsGBufferDepthWithoutWritingIt()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ViewRenderContext context;
        context.Device = device.get();
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        GBufferPass gbufferPass;
        HiZPyramidPass hizPass;

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t hizPassIndex = graph.AddPass(&hizPass);

        assert(graph.Compile(context));

        // 深度の読み取りと、完了を表す論理資源の書き込みだけ
        assert(graph.GetDeclaredPassAccessCount(hizPassIndex) == 2);
        bool bReadsDepth = false;
        bool bWritesComplete = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(hizPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(hizPassIndex, accessIndex, resource, mode, state, finalState));
            if (resource == gbufferPass.GetDepthHandle())
            {
                bReadsDepth = mode == RGAccessMode::Read && state == RHI::ResourceState::ShaderResource;
            }
            else if (resource == hizPass.GetCompleteHandle())
            {
                bWritesComplete = mode == RGAccessMode::Write;
            }
        }
        assert(bReadsDepth);
        assert(bWritesComplete);

        uint32_t depthVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferDepth, depthVersion));
        // GBufferPass が作った版（0）のまま。書き込みなら版が進む
        assert(depthVersion == 0);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == hizPassIndex);
    }

    // 深度の名前付き資源が無い構成では、何も宣言せず（完了の論理資源も作らず）、ピラミッドも作らない。
    void TestHiZPyramidNativeDeclareWithoutDepthDeclaresNothing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ViewRenderContext context;
        context.Device = device.get();

        HiZPyramidPass hizPass;
        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t hizPassIndex = graph.AddPass(&hizPass);
        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(hizPassIndex) == 0);
        assert(!hizPass.GetCompleteHandle().IsValid());
        assert(!hizPass.GetPyramidTexture());
    }

    // 深度を作るだけの何もしないパス（HZB のパスの実行を確かめるための入力）
    class HiZTestDepthProducerPass final : public IRenderGraphPass
    {
    public:
        HiZTestDepthProducerPass(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

        const char* GetName() const override { return "HiZTestDepthProducerPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            builder.WriteTextureAttachment(RenderGraphResourceNames::GBufferDepth,
                                           RGTextureDesc::DepthStencil(m_Width, m_Height, RHI::Format::D32_FLOAT, "HiZTestDepth"),
                                           RGAttachmentKind::DepthStencil,
                                           RHI::AttachmentLoadOp::Clear,
                                           RHI::AttachmentStoreOp::Store,
                                           RHI::ResourceState::DepthWrite,
                                           RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        uint32_t m_Width;
        uint32_t m_Height;
    };

    // 奇数の深度（37x23）から、半分（切り上げ）の 19x12・5 段の HZB を、全ミップ UnorderedAccess のまま作って
    // 最後に ShaderResource へ遷移する。ミップごとに 1 つ前の書き込みを待つバリアを 1 つずつ置き、ディスパッチは 5 回。
    void TestHiZPyramidNativeExecuteBuildsAllMipsInOneSubmission()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        HiZTestDepthProducerPass depthPass(37, 23);
        HiZPyramidPass hizPass;
        graph.AddPass(&depthPass);
        graph.AddPass(&hizPass);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 37;
        context.RenderHeight = 23;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RHI::TexturePtr pyramid = hizPass.GetPyramidTexture();
        assert(pyramid);
        assert(pyramid->GetWidth() == 19);
        assert(pyramid->GetHeight() == 12);
        assert(pyramid->GetMipLevels() == 5);
        assert(hizPass.GetMipCount() == 5);
        assert(commandList.DispatchCount == 5);

        Container::VariableArray<BarrierEvent> pyramidBarriers;
        for (const BarrierEvent& event : commandList.Barriers)
        {
            if (event.Kind == RGBarrierKind::Texture && event.Texture == pyramid.get())
            {
                pyramidBarriers.push_back(event);
            }
        }
        assert(pyramidBarriers.size() == 6);
        assert(pyramidBarriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(pyramidBarriers[0].AfterState == RHI::ResourceState::UnorderedAccess);
        for (size_t index = 1; index < 5; ++index)
        {
            assert(pyramidBarriers[index].BeforeState == RHI::ResourceState::UnorderedAccess);
            assert(pyramidBarriers[index].AfterState == RHI::ResourceState::UnorderedAccess);
        }
        assert(pyramidBarriers[5].BeforeState == RHI::ResourceState::UnorderedAccess);
        assert(pyramidBarriers[5].AfterState == RHI::ResourceState::ShaderResource);

        hizPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "RenderGraphCompileTest start\n";

    TestLinearDependencyOrder();
    TestDiamondStableOrder();
    TestWriteAfterWriteDependency();
    TestWriteAfterReadDependency();
    TestImportedResourceBarriers();
    TestTransientResourcesResolveThroughPool();
    TestCycleDetectionDoesNotExecute();
    TestInvalidHandleRejected();
    TestCompileContextPassedToDeclare();
    TestWriteFinalStateSuppressesFollowupReadBarrier();
    TestVisibilityBufferResourcesDeclareAndRead();
    TestVisibilityRasterOnRecordsMegaDrawsAndIdPass();
    TestVisibilityRasterOnBuildsHiZFromIdDepthBetweenTwoPasses();
    TestVisibilityRasterOnSinglePassCullingDrawsEverythingInOneRenderPass();
    TestVisibilityMergeAbsentWithoutInt64Atomics();
    TestVisibilityMergeAbsentWhenSwRasterOff();
    TestSwRasterBinningWritesListAndUniforms();
    TestSwRasterBinningFallsBackWhenUnavailable();
    TestSwRasterDispatchesBetweenRecordsAndMerges();
    TestSwRasterDispatchAbsentWhenUnavailable();
    TestSwRasterAbsentWithoutBufferDeviceAddress();
    TestSwRasterMaxScanSpanFollowsThreshold();
    TestSwRasterThresholdReachesScanSpanConstant();
    TestSwRasterListCapacityExceedsOneDimension();
    TestSceneViewWiresSwRasterMode();
    TestSceneViewThresholdReachesCullUniform();
#if NORVES_ENABLE_STATS
    TestShadowProbeAbsentWithoutOptionAndAfterLightingWhenEnabled();
    TestShadowProbeReadsCsmResourcesAfterLighting();
    TestShadowProbeCapturesAfterEpochThenMeasuresAndAggregates();
    TestShadowProbeEpochRestartRecapturesAndResetsTotals();
    TestShadowProbeTotalsAggregateVsmWords();
    TestShadowProbeFallbackCapturesAfterFixedExecuteCount();
    TestShadowProbeTotalsHandleEmptyDenominators();
#endif
    TestVirtualShadowMapPassAbsentForCsmAndBeforeLightingForVsm();
    TestVirtualShadowMapPassCreatesAndPublishesResources();
    TestRenderGraphHasBufferIsQuietWithoutVsmPass();
    TestVirtualShadowMapPassFallsBackWhenUnsupported();
    TestVirtualShadowMapPassFallsBackWhenPipelineFails();
    TestVirtualShadowMapPassRecordsMarkAllocateClearInOrder();
#if NORVES_ENABLE_LOGGING
    TestVirtualShadowMapPassReadsStatsOnlyAfterFrameFence();
    TestVirtualShadowMapPassStatsReadbackAcrossFlightCounts();
    TestVirtualShadowMapPassReportsRasterStats();
#endif
    TestVirtualShadowMapRasterStatsReporterDecidesWhenToLog();
    TestVirtualShadowMapPoolPlanClampsToDeviceLimit();
    TestVirtualShadowMapCasterLevelMaskMatchesLevelRanges();
    TestVirtualShadowMapCasterPlansProceduralChunks();
    TestVirtualShadowMapCasterAppendsProceduralInstances();
    TestVirtualShadowMapCasterAppendsSkinnedInstances();
    TestVirtualShadowMapCasterRecordsLevelMask();
    TestVirtualShadowMapPassRecordsCasterRasterAfterSkinning();
    TestVirtualShadowMapPassRecordsMegaCullBetweenMainCullAndExpand();
    TestLightingVsmStatsAreMadeHostVisibleAndReadAfterCompletion();
    TestLightingReadsVsmWhenPublished();
    TestLightingShaderCountsVsmFallbackIndependentlyOfVtFeedback();
    TestVirtualShadowMapMegaDrawDoesNotUseCsmUniformSlots();
    TestVirtualShadowMapMegaCullStatsReporterLogsEvery60Reports();
    TestVisibilityMergeKeyBufferFollowsResolutionAndRetiresOldBuffers();
    TestMaterialTileListCapacityNeverOverflowsAtDefault();
    TestMaterialTileClassifyDispatchesAndPublishesArgs();
    TestMaterialTileClassifyWithoutRecordTableClearsArgs();
    TestMaterialTileClassifyAbsentWhenNotAdded();
    TestMaterialTileClassifyAddedButDisabledByDefault();
    TestSkinningComputePassTransitionsDeclaredBuffersToGenericRead();
    TestSkinningComputePassPacksInstancesAndSubstitutesMissingPrevious();
    TestSkinningComputePassCountsInstancesDroppedByVertexLimit();
    TestSkinningComputePassDropCountIsPerFrameAndLoggedOnce();
    TestSkinningComputeGroupCountsAndBindingLimit();
    TestFrameUseRingGivesDistinctUsesWithinAFrameAndReusesNextFrame();
    TestComputePassFrameResourcesAreNotReusedWithinAFrame();
    TestVisibilityDebugPassFrameResourcesAreNotReusedWithinAFrame();
    TestVisibilityRasterFrameResourcesFollowFramesAndChunkScratchIsReused();
    TestVisibilityRasterOnSinglePassMegaGeometry();
    TestVisibilityRasterRecordsFrameUniqueMaterialTableIndices();
    TestVisibilityResolveOnReplacesGBufferDrawsWithStorageImageWrites();
    TestVisibilityResolveDispatchesPerMaterialFromClassification();
    TestVisibilityResolveFallsBackToDirectDispatchWhenTilesUnavailable();
    TestVisibilityResolveFallsBackToDirectDispatchWhenIndirectDispatchRejected();
    TestVisibilityRasterRecordsFallBackToDirectDispatchWhenIndirectDispatchRejected();
    TestVisibilityRasterRecordsBarriers();
    TestVisibilityResolveUnsupportedDeviceKeepsGBufferDraws();
    TestVisibilityResolveFallsBackToGBufferDrawsWhenPipelinesAreUnavailable();
    TestVisibilityResolveFallsBackToGBufferDrawsWhenSkinningComputeUnavailable();
    TestVisibilityRasterWireframeDrawsLinesAndResolveWritesThem();
    TestVisibilityRasterWireframeFallsBackToGBufferWhenLinePipelinesUnavailable();
    TestVisibilityRasterSkinnedRecordsAddressEachBodyOnce();
    TestSceneViewWiresVisibilityResolveOnlyForOnMode();
    TestVisibilityRasterOffKeepsExistingMegaGeometryRecording();
    TestVisibilityRasterWithoutGBufferDepthDoesNothing();
    TestShadowMapNativeDeclareImportsDepthOutput();
    TestNeuralDecodeNativeDeclareWritesLogicalCompletion();
    TestMegaGeometryNativeDeclareImportsPersistentBuffers();
    TestMegaGeometryNativeDeclareUsesNamedGBufferAttachments();
    TestHiZPyramidNativeDeclareReadsGBufferDepthWithoutWritingIt();
    TestHiZPyramidNativeDeclareWithoutDepthDeclaresNothing();
    TestMegaGeometryNativeExecuteEnqueuesEmptyAttachmentPass();
    TestMegaGeometryNativeExecuteRecreatesRenderPassWhenAttachmentStateModeChanges();
    TestMegaGeometryPartialNamedGBufferFallsBackToLegacyAttachmentStates();
    TestMegaGeometryRecordFrameCommandBatchesInstancesInSingleRenderPass();
    TestMegaGeometryRecordFrameCommandSplitsSectionsByMaterial();
    TestMegaGeometryRecordFrameCommandWritesInstanceTable();
    TestMegaGeometryRecordFrameCommandRecordsTwoPassOcclusion();
    TestMegaGeometrySkipGBufferDrawKeepsTwoPassOrderWithoutDraws();
    TestMegaGeometryTwoPassFallsBackWhenDepthRangeIsNotUnit();
    TestMegaGeometryTwoPassDiscardsVisibilityOnReaddAndComponentChange();
    TestMegaGeometryTwoPassDiscardsVisibilityWhenAllInstancesVanish();
    TestMegaGeometryRecordWithoutFrameSerialTreatsEachRecordAsAFrame();
    TestGBufferNativeDeclareCreatesTransientOutputs();
    TestGBufferSSAONativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestLightingNativeDeclareReadsNamedShadowMap();
    TestLightingSceneDepthAliasDuplicateFailsCompile();
    TestForwardTransparentNativeDeclareDependencies();
    TestSSRNativeDeclareDependencies();
    TestBloomNativeDeclareDependencies();
    TestToneMappingNativeDeclareDependencies();
    TestVignetteNativeDeclareDependencies();
    TestToneMappedVersionChainFeedsFXAAAndUpscale();
    TestToneMappedVersionChainFeedsUpscaleWithoutFXAA();
    TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestPostProcessMissingNamedInputsCompileWithoutErrors();
    TestFXAANativeDeclareDependencies();
    TestUpscaleNativeDeclareDependencies();
    TestShadowMapNativeExecuteRegistersBridge();
    TestNeuralDecodeNativeExecuteSkipsUnsupportedPath();
    TestMegaGeometryNativeExecuteSkipsWhenNoInstances();
    TestHiZPyramidNativeExecuteBuildsAllMipsInOneSubmission();
    TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty();
    TestRecordMeshDrawCallUsesDrawCommandRangesAndFallback();
    TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestGBufferSSAOLightingNativeExecuteWithoutSharedResources();
    TestLightingNativeExecuteBindsExpandedLightStorageBuffer();
    TestLightingNativeExecuteZeroLightsKeepsLogicalCountZeroAndSkipsSsboUpdate();
    TestLightingLightBufferGrowthRetainsRetiredBuffersAndReusesCapacity();
    TestLightingLightBufferGrowthFailureSkipsDescriptorUpdateAndDraw();
    TestProductionDeferredNativeExecuteSkipsLegacyBridge();
    TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge();
    TestLightingNativeExecutePrefersNamedShadowMapOverRegistry();
    TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing();
    TestForwardTransparentNativeExecuteSkipsBridgeWhenRegisterOutputsDisabled();
    TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge();
    TestVignetteNativeExecuteExportsToneMappedColorWithoutBridge();
    TestFXAANativeExecuteExportsToneMappedColorWithoutBridge();
    TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge();
    TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestVignetteNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale();
    TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback();
    TestToneMappingNativeExecuteAcceptsRawSceneColorBridge();

    std::cout << "RenderGraphCompileTest passed\n";
    return 0;
}
