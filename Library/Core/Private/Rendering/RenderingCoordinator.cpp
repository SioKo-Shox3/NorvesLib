#include "Rendering/RenderingCoordinator.h"
#include "Debug/DebugConfig.h"
#include "Debug/EngineChecks.h"
#include "Debug/Stats.h"
#include "Ecs/EcsGpuSceneSlots.h"
#include "Ecs/EcsWorld.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/ISwapChain.h"
#include "RHI/ITexture.h"
#include "Rendering/CanvasView.h"
#include "Rendering/CompositePass.h"
#include "Rendering/DepthOfFieldPass.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FrameCaptureAssignmentGuard.h"
#include "Rendering/FrameCaptureReadbackHelper.h"
#include "Rendering/FramePacket.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/InstanceBufferRing.h"
#include "Rendering/MotionBlurPass.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/PresentationComposer.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RayTracingSceneSubsystem.h"
#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderResources.h"
#include "RHI/FrameResourceWriteTracker.h"
#include "Rendering/RenderingCoordinatorDiagnostics.h"
#include "Rendering/SceneView.h"
#include "Rendering/Screen.h"
#include "Rendering/SequentialStatScope.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SkinnedDrawCommands.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/SkySunLight.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/Viewport.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint64_t RevisionHashOffset = 1469598103934665603ull;
        constexpr uint64_t RevisionHashPrime = 1099511628211ull;
        constexpr uint64_t MeshProxyRevisionTag = 0x4D45534850524F58ull;
        constexpr uint64_t SkinnedMeshProxyRevisionTag = 0x534B494E50524F58ull;

        uint64_t HashRevisionBytes(uint64_t hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= RevisionHashPrime;
            }
            return hash;
        }

        template<typename T>
        uint64_t HashRevisionValue(uint64_t hash, const T& value)
        {
            return HashRevisionBytes(hash, &value, sizeof(value));
        }

        uint64_t HashRevisionFloatArray(uint64_t hash,
                                        const float* values,
                                        uint32_t count)
        {
            for (uint32_t index = 0; index < count; ++index)
            {
                hash = HashRevisionValue(hash, values[index]);
            }
            return hash;
        }

        uint64_t FinishRevisionSet(uint64_t hash, uint64_t count, uint64_t xorValue, uint64_t sum)
        {
            hash = HashRevisionValue(hash, count);
            hash = HashRevisionValue(hash, xorValue);
            hash = HashRevisionValue(hash, sum);
            return hash;
        }

        uint64_t MixRevisionValue(uint64_t value)
        {
            value += 0x9E3779B97F4A7C15ull;
            value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
            value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
            return value ^ (value >> 31u);
        }

        struct RevisionSetAccumulator
        {
            uint64_t Count = 0u;
            uint64_t Xor = 0u;
            uint64_t Sum = 0u;

            void Add(uint64_t value)
            {
                const uint64_t mixed = MixRevisionValue(value);
                ++Count;
                Xor ^= mixed;
                Sum += mixed;
            }

            uint64_t Finish(uint64_t hash) const
            {
                return FinishRevisionSet(hash, Count, Xor, Sum);
            }
        };

        /** @brief 空の太陽の光源が同じ値か（LightProxy の全部の値を比べる。詰め物の bit は比べない） */
        bool SameLightValues(const LightProxy& lhs, const LightProxy& rhs)
        {
            return lhs.LightId == rhs.LightId && lhs.Type == rhs.Type &&
                   lhs.PositionX == rhs.PositionX && lhs.PositionY == rhs.PositionY && lhs.PositionZ == rhs.PositionZ &&
                   lhs.DirectionX == rhs.DirectionX && lhs.DirectionY == rhs.DirectionY &&
                   lhs.DirectionZ == rhs.DirectionZ && lhs.ColorR == rhs.ColorR && lhs.ColorG == rhs.ColorG &&
                   lhs.ColorB == rhs.ColorB && lhs.CanonicalIntensity == rhs.CanonicalIntensity &&
                   lhs.Range == rhs.Range && lhs.AttenuationConstant == rhs.AttenuationConstant &&
                   lhs.AttenuationLinear == rhs.AttenuationLinear &&
                   lhs.AttenuationQuadratic == rhs.AttenuationQuadratic &&
                   lhs.InnerConeAngle == rhs.InnerConeAngle && lhs.OuterConeAngle == rhs.OuterConeAngle &&
                   lhs.bCastShadows == rhs.bCastShadows && lhs.ShadowBias == rhs.ShadowBias &&
                   lhs.ShadowMapResolution == rhs.ShadowMapResolution && lhs.bVisible == rhs.bVisible &&
                   lhs.AffectedLayers == rhs.AffectedLayers;
        }

        uint64_t HashMeshProxyRevision(const MeshProxy& proxy)
        {
            uint64_t hash = RevisionHashOffset ^ MeshProxyRevisionTag;
            hash = HashRevisionValue(hash, proxy.ObjectId);
            hash = HashRevisionValue(hash, proxy.ComponentId);
            hash = HashRevisionValue(hash, proxy.MeshHandle.Id);
            hash = HashRevisionValue(hash, proxy.LODLevel);
            hash = HashRevisionValue(hash, proxy.SubMeshCount);
            const uint32_t subMeshCount =
                proxy.SubMeshCount < MAX_MATERIAL_SLOTS ? proxy.SubMeshCount : MAX_MATERIAL_SLOTS;
            for (uint32_t index = 0u; index < subMeshCount; ++index)
            {
                const SubMeshRange& subMesh = proxy.SubMeshes[index];
                hash = HashRevisionValue(hash, subMesh.IndexStart);
                hash = HashRevisionValue(hash, subMesh.IndexCount);
                hash = HashRevisionValue(hash, subMesh.VertexStart);
                hash = HashRevisionValue(hash, subMesh.MaterialIndex);
            }

            hash = HashRevisionValue(hash, proxy.MaterialCount);
            const uint32_t materialCount =
                proxy.MaterialCount < MAX_MATERIAL_SLOTS ? proxy.MaterialCount : MAX_MATERIAL_SLOTS;
            for (uint32_t index = 0u; index < materialCount; ++index)
            {
                hash = HashRevisionValue(hash, proxy.Materials[index].Id);
                hash = HashRevisionValue(hash,
                                        static_cast<uint8_t>(proxy.MaterialBlendModes[index]));
            }
            hash = HashRevisionValue(hash, proxy.bHasMaterialOverrides);
            hash = HashRevisionValue(hash, proxy.bVisible);
            hash = HashRevisionValue(hash, proxy.bCastShadow);
            hash = HashRevisionValue(hash, proxy.bReceiveShadow);
            hash = HashRevisionValue(hash, proxy.bAffectDynamicIndirectLighting);
            hash = HashRevisionValue(hash, proxy.bAffectDistanceFieldLighting);
            hash = HashRevisionValue(hash, static_cast<uint32_t>(proxy.LayerMask));
            return HashRevisionFloatArray(hash, proxy.CustomData, 4u);
        }

        uint64_t HashSkinnedMeshProxyRevision(const SkinnedMeshProxy& proxy)
        {
            uint64_t hash = RevisionHashOffset ^ SkinnedMeshProxyRevisionTag;
            hash = HashRevisionValue(hash, proxy.MeshHandle.Id);
            hash = HashRevisionValue(hash, proxy.MeshHandle.Generation);
            hash = HashRevisionValue(hash, proxy.Material.Id);
            hash = HashRevisionValue(hash, proxy.MaterialCount);
            for (uint32_t slot = 0; slot < proxy.MaterialCount && slot < MAX_MATERIAL_SLOTS; ++slot)
            {
                hash = HashRevisionValue(hash, proxy.Materials[slot].Id);
            }
            hash = HashRevisionValue(hash, proxy.ObjectId);
            hash = HashRevisionValue(hash, proxy.ComponentId);
            hash = HashRevisionValue(hash, proxy.bCastShadow);
            hash = HashRevisionValue(hash, proxy.bHasAnimatedBounds);
            return HashRevisionValue(hash, proxy.bVisible);
        }

        /** @brief 構成revisionのhashのうち、proxy集合の後に続く環境の値（環境光・空・DDGI・フォグ） */
        uint64_t HashSceneEnvironmentRevision(uint64_t hash, const FramePacket& packet)
        {
            hash = HashRevisionValue(hash, packet.Scene.AmbientColorR);
            hash = HashRevisionValue(hash, packet.Scene.AmbientColorG);
            hash = HashRevisionValue(hash, packet.Scene.AmbientColorB);
            hash = HashRevisionValue(hash, packet.Scene.AmbientIntensity);
            hash = HashRevisionValue(hash, packet.Scene.StaticEnvironmentIntensityScale);
            hash = HashRevisionValue(hash, packet.Scene.StaticEnvironmentMaxRadiance);
            hash = HashRevisionValue(hash, packet.Scene.bStaticEnvironmentUniform);
            hash = HashRevisionValue(hash, packet.Scene.StaticEnvironmentUniformRadiance.x);
            hash = HashRevisionValue(hash, packet.Scene.StaticEnvironmentUniformRadiance.y);
            hash = HashRevisionValue(hash, packet.Scene.StaticEnvironmentUniformRadiance.z);
            hash = HashRevisionValue(hash, packet.Scene.SunShadowMaxDistance);
            hash = HashRevisionValue(hash, packet.Scene.AerialPerspective.bEnabled);
            hash = HashRevisionValue(hash, packet.Scene.AerialPerspective.DistanceScale);
            hash = HashRevisionValue(hash, packet.Scene.AerialPerspective.StartDistanceMeters);
            hash = HashRevisionValue(hash, packet.Scene.AerialPerspective.OriginAltitudeMeters);
            hash = HashRevisionValue(hash, packet.Scene.AerialPerspective.MieDensityScale);
            const CloudLayerParameters& cloudLayer = packet.Scene.CloudLayer;
            hash = HashRevisionValue(hash, cloudLayer.bEnabled);
            hash = HashRevisionValue(hash, cloudLayer.BottomAltitudeMeters);
            hash = HashRevisionValue(hash, cloudLayer.TopAltitudeMeters);
            hash = HashRevisionValue(hash, cloudLayer.Coverage);
            hash = HashRevisionValue(hash, cloudLayer.ExtinctionPerMeter);
            hash = HashRevisionValue(hash, cloudLayer.ShapeScaleMeters);
            hash = HashRevisionValue(hash, cloudLayer.WindVelocityX);
            hash = HashRevisionValue(hash, cloudLayer.WindVelocityZ);

            const SkyAtmosphereParameters& sky = packet.Scene.SkyAtmosphere;
            hash = HashRevisionValue(hash, sky.bEnabled);
            hash = HashRevisionValue(hash, sky.SunAltitudeDegrees);
            hash = HashRevisionValue(hash, sky.SunAzimuthDegrees);
            hash = HashRevisionValue(hash, sky.SunLuminanceNits);
            hash = HashRevisionValue(hash, sky.PlanetRadiusMeters);
            hash = HashRevisionValue(hash, sky.AtmosphereHeightMeters);
            hash = HashRevisionValue(hash, sky.RayleighScaleHeightMeters);
            hash = HashRevisionValue(hash, sky.MieScaleHeightMeters);
            hash = HashRevisionValue(hash, sky.MieAnisotropy);
            hash = HashRevisionValue(hash, sky.GroundAlbedo.x);
            hash = HashRevisionValue(hash, sky.GroundAlbedo.y);
            hash = HashRevisionValue(hash, sky.GroundAlbedo.z);

            const DDGIVolumeParameters& ddgi = packet.Scene.DDGIVolume;
            hash = HashRevisionValue(hash, ddgi.bEnabled);
            hash = HashRevisionValue(hash, ddgi.Origin.x);
            hash = HashRevisionValue(hash, ddgi.Origin.y);
            hash = HashRevisionValue(hash, ddgi.Origin.z);
            hash = HashRevisionValue(hash, ddgi.ProbeSpacing.x);
            hash = HashRevisionValue(hash, ddgi.ProbeSpacing.y);
            hash = HashRevisionValue(hash, ddgi.ProbeSpacing.z);
            hash = HashRevisionValue(hash, ddgi.ProbeCountX);
            hash = HashRevisionValue(hash, ddgi.ProbeCountY);
            hash = HashRevisionValue(hash, ddgi.ProbeCountZ);

            const VolumetricFogParameters& fog = packet.Scene.VolumetricFog;
            hash = HashRevisionValue(hash, fog.bEnabled);
            hash = HashRevisionValue(hash, fog.DensityAtBaseHeight);
            hash = HashRevisionValue(hash, fog.BaseHeight);
            hash = HashRevisionValue(hash, fog.HeightFalloffPerUnit);
            hash = HashRevisionValue(hash, packet.Scene.bFogEnabled);
            hash = HashRevisionValue(hash, packet.Scene.FogColorR);
            hash = HashRevisionValue(hash, packet.Scene.FogColorG);
            hash = HashRevisionValue(hash, packet.Scene.FogColorB);
            hash = HashRevisionValue(hash, packet.Scene.FogDensity);
            hash = HashRevisionValue(hash, packet.Scene.FogStart);
            hash = HashRevisionValue(hash, packet.Scene.FogEnd);
            return hash;
        }

        /**
         * @brief シーン構成revisionを計算する
         *
         * 物体とUIの変換はR6-aのvelocityと深度・法線棄却で扱うため、全画面履歴を
         * 無効化する構成revisionへ含めません。proxy集合のメッシュ・材質・環境だけを
         * 追跡し、構成変更時の履歴不採用を判定できる値にします。
         */
        uint64_t HashSceneRevisionInternal(const FramePacket& packet)
        {
            uint64_t hash = RevisionHashOffset;
            RevisionSetAccumulator meshProxies;
            for (const MeshProxy& proxy : packet.Scene.MeshProxies)
            {
                meshProxies.Add(HashMeshProxyRevision(proxy));
            }
            hash = meshProxies.Finish(hash);

            RevisionSetAccumulator skinnedMeshProxies;
            for (const SkinnedMeshProxy& proxy : packet.Scene.SkinnedMeshProxies)
            {
                skinnedMeshProxies.Add(HashSkinnedMeshProxyRevision(proxy));
            }
            hash = skinnedMeshProxies.Finish(hash);
            return HashSceneEnvironmentRevision(hash, packet);
        }

        /**
         * @brief 差分の同期のシーン構成revision（HashSceneRevisionInternal と同じ値）
         *
         * Mesh・Skinned の集合は、変わった位置だけで更新した数・XOR・和を使い、全 proxy を回さない。
         */
        uint64_t HashSceneRevisionIncremental(const FramePacket& packet,
                                              const ProxyRevisionSet& meshProxies,
                                              const ProxyRevisionSet& skinnedMeshProxies)
        {
            uint64_t hash = RevisionHashOffset;
            hash = FinishRevisionSet(hash, meshProxies.GetCount(), meshProxies.GetXor(), meshProxies.GetSum());
            hash = FinishRevisionSet(hash, skinnedMeshProxies.GetCount(), skinnedMeshProxies.GetXor(),
                                     skinnedMeshProxies.GetSum());
            return HashSceneEnvironmentRevision(hash, packet);
        }

        /** @brief 検査 PACKET_DELTA: 変わった位置だけで更新した集合が、配列の全部を数え直した値と一致するか */
        template<typename TProxy, typename THashProxy>
        bool MatchesRevisionSet(const ProxyRevisionSet& set,
                                const Container::VariableArray<TProxy>& proxies,
                                THashProxy&& hashProxy)
        {
            RevisionSetAccumulator full;
            for (const TProxy& proxy : proxies)
            {
                full.Add(hashProxy(proxy));
            }
            return full.Count == set.GetCount() && full.Xor == set.GetXor() && full.Sum == set.GetSum();
        }

        uint64_t HashLightRevision(const Container::VariableArray<LightProxy>& lights)
        {
            uint64_t hash = RevisionHashOffset;
            hash = HashRevisionValue(hash, lights.size());
            for (const LightProxy& light : lights)
            {
                hash = HashRevisionValue(hash, light.LightId);
                hash = HashRevisionValue(hash, static_cast<uint8_t>(light.Type));
                hash = HashRevisionValue(hash, light.PositionX);
                hash = HashRevisionValue(hash, light.PositionY);
                hash = HashRevisionValue(hash, light.PositionZ);
                hash = HashRevisionValue(hash, light.DirectionX);
                hash = HashRevisionValue(hash, light.DirectionY);
                hash = HashRevisionValue(hash, light.DirectionZ);
                hash = HashRevisionValue(hash, light.ColorR);
                hash = HashRevisionValue(hash, light.ColorG);
                hash = HashRevisionValue(hash, light.ColorB);
                hash = HashRevisionValue(hash, light.CanonicalIntensity);
                hash = HashRevisionValue(hash, light.Range);
                hash = HashRevisionValue(hash, light.AttenuationConstant);
                hash = HashRevisionValue(hash, light.AttenuationLinear);
                hash = HashRevisionValue(hash, light.AttenuationQuadratic);
                hash = HashRevisionValue(hash, light.InnerConeAngle);
                hash = HashRevisionValue(hash, light.OuterConeAngle);
                hash = HashRevisionValue(hash, light.bCastShadows);
                hash = HashRevisionValue(hash, light.ShadowBias);
                hash = HashRevisionValue(hash, light.ShadowMapResolution);
                hash = HashRevisionValue(hash, light.bVisible);
                hash = HashRevisionValue(hash, static_cast<uint32_t>(light.AffectedLayers));
            }
            return hash;
        }

        uint64_t AdvanceRevision(uint64_t revision)
        {
            ++revision;
            return revision == 0u ? 1u : revision;
        }

        [[noreturn]] void ThrowSwapChainBeginFrameError(RHI::SwapChainBeginFrameStatus status)
        {
            if (status == RHI::SwapChainBeginFrameStatus::Fatal)
            {
                throw std::runtime_error("SwapChain image acquisition failed fatally");
            }
            throw std::runtime_error("SwapChain BeginFrame failed with an invalid status");
        }

        [[noreturn]] void ThrowSwapChainEndFrameError(RHI::SwapChainEndFrameStatus status)
        {
            switch (status)
            {
            case RHI::SwapChainEndFrameStatus::InvalidCommandList:
                throw std::runtime_error("SwapChain rejected the command list after image acquisition");
            case RHI::SwapChainEndFrameStatus::SubmissionSerialExhausted:
                throw std::runtime_error("SwapChain submission serial exhausted before fence reset");
            case RHI::SwapChainEndFrameStatus::FenceResetFailed:
                throw std::runtime_error("SwapChain frame fence reset failed");
            case RHI::SwapChainEndFrameStatus::SubmitFailed:
                throw std::runtime_error("SwapChain queue submission failed after fence reset");
            case RHI::SwapChainEndFrameStatus::PresentationFailed:
                throw std::runtime_error("SwapChain presentation failed after queue submission");
            case RHI::SwapChainEndFrameStatus::Success:
                break;
            }
            throw std::runtime_error("SwapChain EndFrame failed with an unknown status");
        }

        // フレーム全体の GPU の区間の名前。リストごとに開いた同名の区間を、先頭の開始から末尾の終了までに合わせる
        constexpr const char* FrameGPUScopeName = "FrameGPU";

        class ScopedGPUTimestampFrameRecording
        {
        public:
            ScopedGPUTimestampFrameRecording(RHI::ICommandList* commandList,
                                             uint32_t frameSlotIndex)
                : m_CommandList(commandList), m_FrameSlotIndex(frameSlotIndex)
            {
            }

            ~ScopedGPUTimestampFrameRecording()
            {
                if (m_CommandList)
                {
                    m_CommandList->AbortGPUTimestampFrame(m_FrameSlotIndex);
                }
            }

            ScopedGPUTimestampFrameRecording(const ScopedGPUTimestampFrameRecording&) = delete;
            ScopedGPUTimestampFrameRecording& operator=(const ScopedGPUTimestampFrameRecording&) = delete;

        private:
            RHI::ICommandList* m_CommandList = nullptr;
            uint32_t m_FrameSlotIndex = 0u;
        };

        using JobCommandLists = Container::VariableArray<Container::TSharedPtr<RHI::ICommandList>>;

        // 提出まで届かなかったフレーム（例外など）の、仕事の番号ごとのリストの中身を捨てる。
        // 提出したリスト（Pending）には何もしないので、提出の後に抜けたときは何も変わらない。
        class ScopedJobCommandListsAbort
        {
        public:
            explicit ScopedJobCommandListsAbort(const JobCommandLists& commandLists)
                : m_CommandLists(commandLists)
            {
            }

            ~ScopedJobCommandListsAbort()
            {
                for (const Container::TSharedPtr<RHI::ICommandList>& commandList : m_CommandLists)
                {
                    if (commandList)
                    {
                        commandList->AbortSubmission();
                    }
                }
            }

            ScopedJobCommandListsAbort(const ScopedJobCommandListsAbort&) = delete;
            ScopedJobCommandListsAbort& operator=(const ScopedJobCommandListsAbort&) = delete;

        private:
            const JobCommandLists& m_CommandLists;
        };

        // 仕事の番号 0〜jobCount-1 のリストを用意し（足りない分を作る）、全リストを今のフレームの slot に合わせる。
        // 作るのは RenderThread の記録の前だけなので、番号ごとのリストは別々のスレッドが同時に記録できる。
        void PrepareJobCommandLists(RHI::IDevice& device,
                                    JobCommandLists& commandLists,
                                    uint32_t jobCount,
                                    uint32_t frameIndex)
        {
            while (commandLists.size() < jobCount)
            {
                RHI::CommandListPtr commandList = device.CreateCommandList();
                if (!commandList)
                {
                    throw std::runtime_error("仕事の番号の記録のリストを作れませんでした");
                }
                commandLists.push_back(commandList);
            }
            for (const Container::TSharedPtr<RHI::ICommandList>& commandList : commandLists)
            {
                commandList->SetFrameIndex(frameIndex);
            }
        }

        // このフレームで記録を終えたリストを、仕事の番号の順に 1 回で提出する（同じ提出番号・完了の通知・中止を全リストへ配る）。
        // 0 番（m_CommandList）は必ず含める。
        RHI::SwapChainEndFrameResult SubmitJobCommandLists(RHI::ISwapChain& swapChain,
                                                           const JobCommandLists& commandLists)
        {
            Container::VariableArray<RHI::CommandListPtr> frameCommandLists;
            frameCommandLists.reserve(commandLists.size());
            for (size_t jobIndex = 0; jobIndex < commandLists.size(); ++jobIndex)
            {
                if (jobIndex == 0u ||
                    commandLists[jobIndex]->GetSubmissionState() == RHI::CommandListSubmissionState::Recorded)
                {
                    frameCommandLists.push_back(commandLists[jobIndex]);
                }
            }
            return swapChain.EndFrame(frameCommandLists.data(), static_cast<uint32_t>(frameCommandLists.size()));
        }

        // 1 フレームで区間ごとの記録に渡すリストの上限（0 番を除く）。超えた分のパスは最後のリストへ Serial で記録される
        constexpr uint32_t MaxRecordJobCommandLists = 32u;

        // RenderGraph の Parallel のパスへ、仕事の番号 1 以降のリストを提出の順に渡す（RenderThread だけで使う）。
        // 渡すリストは記録を始め、GPU の時間を取るフレームでは FrameGPU の区間を開いておく。
        // フレームの終わりに CloseAcquiredLists で区間を閉じて記録を終える（提出は SubmitJobCommandLists が番号の順に行う）。
        class JobRecordListSource final : public IRenderGraphRecordListSource
        {
        public:
            JobRecordListSource(RHI::IDevice& device,
                                JobCommandLists& commandLists,
                                uint32_t frameIndex,
                                uint64_t frameNumber,
                                bool bGPUTimestamps)
                : m_Device(device),
                  m_CommandLists(commandLists),
                  m_FrameIndex(frameIndex),
                  m_FrameNumber(frameNumber),
                  m_bGPUTimestamps(bGPUTimestamps)
            {
            }

            RHI::ICommandList* AcquireRecordList() override
            {
                if (m_AcquiredCount >= MaxRecordJobCommandLists)
                {
                    return nullptr;
                }

                const size_t jobIndex = static_cast<size_t>(m_AcquiredCount) + 1u;
                while (m_CommandLists.size() <= jobIndex)
                {
                    RHI::CommandListPtr commandList = m_Device.CreateCommandList();
                    if (!commandList)
                    {
                        return nullptr;
                    }
                    m_CommandLists.push_back(commandList);
                }

                RHI::ICommandList* commandList = m_CommandLists[jobIndex].get();
                commandList->SetFrameIndex(m_FrameIndex);
                commandList->BeginRecording();
                ++m_AcquiredCount;
                if (m_bGPUTimestamps && commandList->SupportsGPUTimestamps())
                {
                    commandList->BeginGPUTimestampFrame(m_FrameNumber);
                    commandList->BeginGPUTimestamp(FrameGPUScopeName);
                }
                return commandList;
            }

            void CloseAcquiredLists()
            {
                for (uint32_t index = 0; index < m_AcquiredCount; ++index)
                {
                    RHI::ICommandList* commandList = m_CommandLists[static_cast<size_t>(index) + 1u].get();
                    if (m_bGPUTimestamps && commandList->SupportsGPUTimestamps())
                    {
                        commandList->EndGPUTimestamp();
                        commandList->EndGPUTimestampFrame();
                    }
                    commandList->End();
                }
            }

            uint32_t GetAcquiredCount() const
            {
                return m_AcquiredCount;
            }

        private:
            RHI::IDevice& m_Device;
            JobCommandLists& m_CommandLists;
            uint32_t m_FrameIndex = 0u;
            uint64_t m_FrameNumber = 0u;
            bool m_bGPUTimestamps = false;
            uint32_t m_AcquiredCount = 0u;
        };

        // --render-parallel-record（Game が NORVES_RENDER_PARALLEL_RECORD へ渡す）。既定（未設定・1）は Parallel のパスを区間ごとの
        // リストへ同時に記録し、0 のときだけ全パスを 1 本へ記録する
        bool ResolveParallelRecordEnabled()
        {
            char* value = nullptr;
            size_t length = 0;
            if (_dupenv_s(&value, &length, "NORVES_RENDER_PARALLEL_RECORD") != 0)
            {
                value = nullptr;
            }
            const bool bEnabled = value == nullptr || std::strcmp(value, "0") != 0;
            std::free(value);
            return bEnabled;
        }

        // フレームの持ち主の CPU から書ける領域の 1 ページの大きさ。開発ビルドだけ --frame-arena-initial-kb
        // （Game が NORVES_FRAME_ARENA_INITIAL_KB へ渡す）で小さくでき、ページを足す道を通せる
        uint64_t ResolveFrameUploadPageBytes()
        {
            uint64_t pageBytes = FrameUploadArena::DefaultInitialPageBytes;
#if NORVES_BUILD_DEVELOPMENT
            char* value = nullptr;
            size_t length = 0;
            if (_dupenv_s(&value, &length, "NORVES_FRAME_ARENA_INITIAL_KB") == 0 && value != nullptr)
            {
                char* end = nullptr;
                const unsigned long long kilobytes = std::strtoull(value, &end, 10);
                if (end != value && end != nullptr && *end == 0 && kilobytes != 0 && kilobytes <= 1024ull * 1024ull)
                {
                    pageBytes = static_cast<uint64_t>(kilobytes) * 1024ull;
                }
                else
                {
                    NORVES_LOG_WARNING("FrameResource",
                                       "NORVES_FRAME_ARENA_INITIAL_KB=%s は 1 から 1048576 までの整数でないので既定の大きさを使います",
                                       value);
                }
            }
            std::free(value);
#endif
            return pageBytes;
        }

        // RenderGraph の一時の資源のスロットの数の下限。開発ビルドだけ --rg-transient-slots（Game が
        // NORVES_RG_TRANSIENT_SLOTS へ渡す）で飛行中のフレームの数より多くでき、描画先がフレームごとに替わる形を作れる
        uint32_t ResolveTransientResourceSlotCount()
        {
            uint32_t slotCount = 0;
#if NORVES_BUILD_DEVELOPMENT
            char* value = nullptr;
            size_t length = 0;
            if (_dupenv_s(&value, &length, "NORVES_RG_TRANSIENT_SLOTS") == 0 && value != nullptr)
            {
                char* end = nullptr;
                const unsigned long slots = std::strtoul(value, &end, 10);
                if (end != value && end != nullptr && *end == 0 && slots >= 1 && slots <= 4)
                {
                    slotCount = static_cast<uint32_t>(slots);
                }
                else
                {
                    NORVES_LOG_WARNING("RenderGraph",
                                       "NORVES_RG_TRANSIENT_SLOTS=%s は 1 から 4 までの整数でないので飛行中のフレームの数を使います",
                                       value);
                }
            }
            std::free(value);
#endif
            return slotCount;
        }

        template<typename CameraResolver>
        ViewportRenderPlan BuildViewportRenderPlan(const Viewport &viewport,
                                                   uint32_t viewId,
                                                   uint32_t viewportId,
                                                   uint32_t renderWidth,
                                                   uint32_t renderHeight,
                                                   CameraResolver resolveCamera,
                                                   const CameraProxy *fallbackCamera)
        {
            ViewportRenderPlan plan;
            plan.ViewId = viewId;
            plan.ViewportId = viewportId;
            plan.bEnabled = viewport.IsEnabled();
            plan.RenderWidth = renderWidth;
            plan.RenderHeight = renderHeight;
            plan.DebugMode = viewport.GetDebugViewMode();

            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            viewport.GetRect(x, y, width, height);

            float minDepth = 0.0f;
            float maxDepth = 1.0f;
            viewport.GetDepthRange(minDepth, maxDepth);

            plan.NormalizedRect.X = x;
            plan.NormalizedRect.Y = y;
            plan.NormalizedRect.Width = width;
            plan.NormalizedRect.Height = height;
            plan.NormalizedRect.MinDepth = minDepth;
            plan.NormalizedRect.MaxDepth = maxDepth;

            uint32_t pixelX = 0;
            uint32_t pixelY = 0;
            uint32_t pixelWidth = 0;
            uint32_t pixelHeight = 0;
            viewport.GetPixelRect(renderWidth, renderHeight, pixelX, pixelY, pixelWidth, pixelHeight);

            plan.PixelRect.X = static_cast<float>(pixelX);
            plan.PixelRect.Y = static_cast<float>(pixelY);
            plan.PixelRect.Width = static_cast<float>(pixelWidth);
            plan.PixelRect.Height = static_cast<float>(pixelHeight);
            plan.PixelRect.MinDepth = minDepth;
            plan.PixelRect.MaxDepth = maxDepth;

            plan.Scissor.Left = static_cast<int32_t>(pixelX);
            plan.Scissor.Top = static_cast<int32_t>(pixelY);
            plan.Scissor.Right = static_cast<int32_t>(pixelX + pixelWidth);
            plan.Scissor.Bottom = static_cast<int32_t>(pixelY + pixelHeight);

            CameraProxy camera;
            const uint64_t cameraId = viewport.GetCameraId();
            const CameraProxy *resolvedCamera = cameraId != 0 ? resolveCamera(cameraId) : nullptr;
            if (resolvedCamera)
            {
                camera = *resolvedCamera;
            }
            else
            {
                camera = viewport.GetCamera();
            }

            if (!camera.IsValid() && fallbackCamera)
            {
                camera = *fallbackCamera;
            }

            if (!camera.IsValid() && pixelWidth > 0 && pixelHeight > 0)
            {
                camera.Viewport.X = static_cast<float>(pixelX);
                camera.Viewport.Y = static_cast<float>(pixelY);
                camera.Viewport.Width = static_cast<float>(pixelWidth);
                camera.Viewport.Height = static_cast<float>(pixelHeight);
                camera.Viewport.MinDepth = minDepth;
                camera.Viewport.MaxDepth = maxDepth;
            }

            if (pixelHeight > 0)
            {
                camera.AspectRatio = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
            }

            plan.Camera = camera;
            plan.bHasCamera = camera.IsValid();

            return plan;
        }

        uint32_t AppendInstanceDataToPacket(FramePacket *packet,
                                            const Container::VariableArray<GPUSceneInstanceData> &instanceData)
        {
            if (!packet)
            {
                return 0;
            }

            const uint32_t baseInstance = static_cast<uint32_t>(packet->InstanceData.size());
            if (!instanceData.empty())
            {
                packet->InstanceData.insert(packet->InstanceData.end(),
                                            instanceData.begin(),
                                            instanceData.end());
            }
            return baseInstance;
        }

#include "Rendering/RenderingCoordinatorStatsPropagation.inl"

        CommandRange AppendRebasedDrawCommands(const Container::VariableArray<DrawCommand> &source,
                                               uint32_t baseInstance,
                                               Container::VariableArray<DrawCommand> &destination)
        {
            CommandRange range;
            if (source.empty())
            {
                return range;
            }

            range.First = static_cast<uint32_t>(destination.size());
            range.Count = static_cast<uint32_t>(source.size());
            destination.insert(destination.end(), source.begin(), source.end());

            if (baseInstance == 0)
            {
                return range;
            }

            const uint32_t rangeEnd = range.End();
            for (uint32_t index = range.First; index < rangeEnd; ++index)
            {
                DrawCommand &command = destination[index];
                if (!command.IsGraphicsCommand())
                {
                    continue;
                }

                command.Draw.FirstInstance += baseInstance;
                command.Draw.InstanceDataOffset += baseInstance;
            }

            return range;
        }

        CommandRange CombineCommandRanges(const CommandRange &opaqueRange,
                                          const CommandRange &transparentRange)
        {
            if (opaqueRange.IsEmpty())
            {
                return transparentRange;
            }

            if (transparentRange.IsEmpty())
            {
                return opaqueRange;
            }

            return {opaqueRange.First, opaqueRange.Count + transparentRange.Count};
        }

        uint32_t ResolveFrameIndex(const RHI::ISwapChain &swapChain)
        {
            const uint32_t frameIndex = swapChain.GetCurrentFrameIndex();
            [[maybe_unused]] const uint32_t maxFramesInFlight = swapChain.GetMaxFramesInFlight();
            assert(maxFramesInFlight > 0);
            assert(frameIndex < maxFramesInFlight);
            // フレームごとの資源の枠（FrameUseRing）は、これ以上の番号を区別できない
            assert(maxFramesInFlight <= FrameUseRingMaxInFlightSlots);
            return frameIndex;
        }

        // 飛行中のスロットごとの記述子の組から、このフレームの組を引く
        RHI::DescriptorSetPtr SelectFrameDescriptorSet(const Container::VariableArray<RHI::DescriptorSetPtr>& sets,
                                                       uint32_t frameIndex)
        {
            if (sets.empty())
            {
                return nullptr;
            }
            assert(frameIndex < sets.size());
            return sets[frameIndex % sets.size()];
        }

        RHI::BufferPtr CreateAddressableMeshBuffer(RHI::IDevice& device,
                                                   const RHI::BufferPtr& source,
                                                   RHI::ResourceUsage usage,
                                                   const char* debugName)
        {
            if (!source || source->GetSize() == 0)
            {
                return {};
            }

            const RHI::ResourceUsage sourceUsage = source->GetUsage();
            if ((sourceUsage & RHI::ResourceUsage::BufferDeviceAddress) ==
                    RHI::ResourceUsage::BufferDeviceAddress &&
                source->GetDeviceAddress() != 0)
            {
                return source;
            }

            RHI::BufferDesc desc;
            desc.Size = source->GetSize();
            desc.Usage = usage | RHI::ResourceUsage::BufferDeviceAddress;
            desc.CPUAccessible = true;
            desc.DebugName = debugName;
            RHI::BufferPtr addressable = device.CreateBuffer(desc);
            if (!addressable || addressable->GetDeviceAddress() == 0)
            {
                return {};
            }

            void* sourceData = source->Map(0, source->GetSize());
            if (!sourceData)
            {
                return {};
            }

            addressable->Update(sourceData, source->GetSize());
            source->Unmap();
            return addressable;
        }

        bool IsFiniteRayTracingTransform(const Math::Matrix4x4& transform)
        {
            for (float value : transform.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        void CopyRayTracingInstanceTransform(const Math::Matrix4x4& worldTransform,
                                             float outTransform[12])
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                for (uint32_t column = 0; column < 4; ++column)
                {
                    outTransform[row * 4 + column] = worldTransform.m[column][row];
                }
            }
        }

        /**
         * @brief MegaMesh 全体の境界の球（ローカル）を引く。同じメッシュが続く間は直前の値を使い回す
         *
         * Find はメッシュが無い・境界が有限でなければ null を返す。
         */
        class RayTracingMegaMeshBoundsLookup
        {
        public:
            explicit RayTracingMegaMeshBoundsLookup(const MegaGeometryResources* resources)
                : m_Resources(resources)
            {
            }

            const BoundingSphere* Find(MegaGeometry::MegaMeshHandle handle)
            {
                if (!m_bHasLast || handle.Id != m_LastId)
                {
                    m_LastId = handle.Id;
                    m_bHasLast = true;
                    const MegaGeometry::MegaMeshGPUData* gpuData =
                        m_Resources ? m_Resources->GetMegaMeshGPUData(handle) : nullptr;
                    m_bLastValid = false;
                    if (gpuData)
                    {
                        m_LastBounds = gpuData->TotalBounds;
                        m_bLastValid = m_LastBounds.Radius > 0.0f && std::isfinite(m_LastBounds.Radius) &&
                                       std::isfinite(m_LastBounds.CenterX) && std::isfinite(m_LastBounds.CenterY) &&
                                       std::isfinite(m_LastBounds.CenterZ);
                    }
                }
                return m_bLastValid ? &m_LastBounds : nullptr;
            }

        private:
            const MegaGeometryResources* m_Resources;
            uint64_t m_LastId = 0;
            bool m_bHasLast = false;
            bool m_bLastValid = false;
            BoundingSphere m_LastBounds;
        };

        /**
         * @brief TLAS に入れるかを決める MegaGeometry の境界の球（ワールド）
         * @param localBounds メッシュ全体の境界の球（ローカル）。null なら proxy の WorldBounds
         *
         * メッシュの境界があれば、それを proxy の変換で写した球（半径は変換の軸の拡大率の最大を掛ける）にする。
         * proxy の WorldBounds は MegaGeometryComponent では MeshComponent の既定のローカルの箱（±1 m）を変換の原点に
         * 置いた球で、頂点をワールドの座標で焼いて原点に置く地形の区画では、遠くの区画もすべて原点の球になる。
         * どちらも無ければ変換の並進と半径 0
         */
        void MakeRayTracingSelectBounds(const MegaGeometryProxy& proxy,
                                        const BoundingSphere* localBounds,
                                        float outSphere[4])
        {
            if (localBounds)
            {
                float transform[12] = {};
                CopyRayTracingInstanceTransform(proxy.WorldTransform, transform);
                const float center[3] = {localBounds->CenterX, localBounds->CenterY, localBounds->CenterZ};
                float maxScaleSquared = 0.0f;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const float* rowValues = transform + axis * 4;
                    outSphere[axis] = rowValues[0] * center[0] + rowValues[1] * center[1] +
                                      rowValues[2] * center[2] + rowValues[3];
                    // 列 axis（ローカルの軸 axis の写る先）の長さ
                    const float scaleSquared = transform[axis] * transform[axis] +
                                               transform[4 + axis] * transform[4 + axis] +
                                               transform[8 + axis] * transform[8 + axis];
                    maxScaleSquared = scaleSquared > maxScaleSquared ? scaleSquared : maxScaleSquared;
                }
                outSphere[3] = localBounds->Radius * std::sqrt(maxScaleSquared);
                if (std::isfinite(outSphere[0]) && std::isfinite(outSphere[1]) && std::isfinite(outSphere[2]) &&
                    std::isfinite(outSphere[3]))
                {
                    return;
                }
            }
            if (proxy.WorldBounds.IsValid())
            {
                outSphere[0] = proxy.WorldBounds.CenterX;
                outSphere[1] = proxy.WorldBounds.CenterY;
                outSphere[2] = proxy.WorldBounds.CenterZ;
                outSphere[3] = proxy.WorldBounds.Radius;
                return;
            }
            const Math::Vector3 translation = proxy.WorldTransform.GetTranslationRow();
            outSphere[0] = translation.x;
            outSphere[1] = translation.y;
            outSphere[2] = translation.z;
            outSphere[3] = 0.0f;
        }

        /** @brief 選ぶ規則の原点（メインカメラの位置）。カメラが無ければ false で、距離では外さない（上限だけで選ぶ） */
        bool GetRayTracingSelectOrigin(const FramePacket& packet, float outOrigin[3])
        {
            if (!packet.bHasMainCamera)
            {
                return false;
            }
            outOrigin[0] = packet.Scene.MainCamera.PositionX;
            outOrigin[1] = packet.Scene.MainCamera.PositionY;
            outOrigin[2] = packet.Scene.MainCamera.PositionZ;
            return std::isfinite(outOrigin[0]) && std::isfinite(outOrigin[1]) && std::isfinite(outOrigin[2]);
        }

        /** @brief 原点から境界の球の表面までの距離（球の中なら 0）。値が有限でなければ無限大（外す） */
        float DistanceToRayTracingSelectBounds(const float origin[3], const float sphere[4])
        {
            const float dx = sphere[0] - origin[0];
            const float dy = sphere[1] - origin[1];
            const float dz = sphere[2] - origin[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz) - sphere[3];
            if (!std::isfinite(distance))
            {
                return std::numeric_limits<float>::infinity();
            }
            return distance > 0.0f ? distance : 0.0f;
        }

        /**
         * @brief BLAS の入力に使える MegaGeometry のメッシュの GPU データを、1 フレームの中で同じメッシュは 1 回だけ引く
         *
         * 区画へのコピーが GPU で完了し、影に使う段の範囲が頂点・インデックスの中に収まるメッシュだけを返す（BLAS は同期で構築する）。
         */
        class RayTracingMegaMeshLookup
        {
        public:
            explicit RayTracingMegaMeshLookup(const MegaGeometryResources& resources)
                : m_Resources(resources)
            {
            }

            const MegaGeometry::MegaMeshGPUData* Find(MegaGeometry::MegaMeshHandle handle)
            {
                const auto found = m_Cache.find(handle.Id);
                if (found != m_Cache.end())
                {
                    return found->second;
                }
                const MegaGeometry::MegaMeshGPUData* gpuData = Resolve(handle);
                m_Cache.emplace(handle.Id, gpuData);
                return gpuData;
            }

        private:
            const MegaGeometry::MegaMeshGPUData* Resolve(MegaGeometry::MegaMeshHandle handle) const
            {
                constexpr uint32_t vertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
                const MegaGeometry::MegaMeshGPUData* gpuData = m_Resources.GetReadyMegaMeshGPUData(handle);
                if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer ||
                    gpuData->ShadowIndexCount < 3u || gpuData->ShadowIndexCount % 3u != 0u ||
                    gpuData->ShadowFirstIndex > gpuData->IndexCount ||
                    gpuData->ShadowIndexCount > gpuData->IndexCount - gpuData->ShadowFirstIndex ||
                    gpuData->VertexCount < 3u ||
                    static_cast<uint64_t>(gpuData->VertexCount) * vertexStride >
                        gpuData->VertexBufferBytes ||
                    (static_cast<uint64_t>(gpuData->ShadowFirstIndex) + gpuData->ShadowIndexCount) *
                            sizeof(uint32_t) >
                        gpuData->IndexBufferBytes)
                {
                    return nullptr;
                }
                return gpuData;
            }

            const MegaGeometryResources& m_Resources;
            Container::UnorderedMap<uint64_t, const MegaGeometry::MegaMeshGPUData*> m_Cache;
        };

        /**
         * @brief TLAS に入れるかを決める、描画コマンドから作ったメッシュのインスタンスの境界の球（ワールド）
         * @param transform インスタンスの変換（3x4。列 3 が並進）
         * @param localBounds メッシュのローカルの AABB。null なら変換の並進と半径 0
         *
         * ローカルの AABB を変換で写した AABB（各軸の広がりは変換の成分の絶対値で足す）を囲む球にする。
         */
        void MakeRayTracingMeshSelectBounds(const float transform[12],
                                            const BoundingBox* localBounds,
                                            float outSphere[4])
        {
            if (!localBounds)
            {
                outSphere[0] = transform[3];
                outSphere[1] = transform[7];
                outSphere[2] = transform[11];
                outSphere[3] = 0.0f;
                return;
            }
            const float center[3] = {(localBounds->MinX + localBounds->MaxX) * 0.5f,
                                     (localBounds->MinY + localBounds->MaxY) * 0.5f,
                                     (localBounds->MinZ + localBounds->MaxZ) * 0.5f};
            const float half[3] = {(localBounds->MaxX - localBounds->MinX) * 0.5f,
                                   (localBounds->MaxY - localBounds->MinY) * 0.5f,
                                   (localBounds->MaxZ - localBounds->MinZ) * 0.5f};
            float radiusSquared = 0.0f;
            for (uint32_t row = 0; row < 3; ++row)
            {
                const float* rowValues = transform + row * 4;
                outSphere[row] = rowValues[0] * center[0] + rowValues[1] * center[1] +
                                 rowValues[2] * center[2] + rowValues[3];
                const float extent = std::fabs(rowValues[0]) * half[0] + std::fabs(rowValues[1]) * half[1] +
                                     std::fabs(rowValues[2]) * half[2];
                radiusSquared += extent * extent;
            }
            outSphere[3] = std::sqrt(radiusSquared);
        }

        /**
         * @brief 登録するカメラの GameThread 側の識別子を決める
         *
         * SourceCameraId があればそれ（GetMainCamera
         * の値を渡し直したときも保たれる）、無ければ渡された
         * CameraId（CameraComponent の ID など）を使う。
         */
        uint64_t ResolveSourceCameraId(const CameraProxy& camera)
        {
            return camera.SourceCameraId != 0 ? camera.SourceCameraId : camera.CameraId;
        }

    } // namespace

    uint64_t ComputeSceneRevisionHash(const FramePacket& packet)
    {
        return HashSceneRevisionInternal(packet);
    }

    RayTracingSceneSubsystem::~RayTracingSceneSubsystem()
    {
        // 破棄（静的な終了を含む）ではログを出さない
        m_TopLevelUpdateStats = TopLevelUpdateStats{};
        Shutdown();
    }

    void RayTracingSceneSubsystem::Shutdown()
    {
        LogTopLevelUpdates(true);
        m_BottomLevelCache.clear();
        for (TopLevelCacheEntry& topLevel : m_TopLevelCache)
        {
            topLevel = TopLevelCacheEntry{};
        }
        m_MegaSlots.clear();
        m_UnslottedMegaEntries.clear();
        m_MegaSelectCandidates.clear();
        m_MegaSlotSource = nullptr;
        m_MegaSlotRevision = 0;
        m_MegaSlotGeneration = 0;
        m_bMegaSlotsValid = false;
        m_RenderThreadSequenceCarry.Reset();
    }

    bool RayTracingSceneSubsystem::BuildFrameSnapshot(const MeshResources* meshResources,
                                                       FramePacket& packet,
                                                       const MaterialResources* materialResources,
                                                       const MegaGeometryResources* megaGeometryResources,
                                                       const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies,
                                                       const Container::VariableArray<MeshProxy>* meshProxies)
    {
        packet.RayTracingScene.Clear();
        if (!meshResources && !megaGeometryResources)
        {
            return true;
        }

        const DrawCommandView opaqueCommands =
            DrawCommandView::FromRange(packet.DrawCommands, packet.OpaqueCommandRange);
        const bool bResolveInstanceObjectIds =
            packet.bHasMainCamera && packet.Scene.MainCamera.SequenceFrame != 0;
        for (const DrawCommand& command : opaqueCommands)
        {
            const DrawParams& draw = command.Draw;
            // 影を落とさない物体もパストレーサーの主光線・散乱光線には見えるため含める。
            // 影・DDGI・RTGIはinstance maskで影を落とす物体だけを調べる。
            if (!meshResources ||
                (command.Type != DrawCommandType::DrawIndexed &&
                 command.Type != DrawCommandType::DrawIndexedInstanced) ||
                draw.PayloadKind != DrawPayloadKind::Mesh ||
                !draw.MeshHandle.IsValid() ||
                (draw.MaterialBlendMode != BlendMode::Opaque &&
                 draw.MaterialBlendMode != BlendMode::Masked))
            {
                continue;
            }

            const MeshResources::MeshGPUData* meshData = meshResources->GetGPUData(draw.MeshHandle);
            if (!meshData || !meshData->VertexBuffer || !meshData->IndexBuffer)
            {
                continue;
            }

            const uint32_t indexOffset = draw.IndexCount > 0 ? draw.IndexOffset : 0u;
            const uint32_t indexCount = draw.IndexCount > 0 ? draw.IndexCount : meshData->IndexCount;
            if (indexCount < 3 || indexCount % 3 != 0 ||
                indexOffset > meshData->IndexCount ||
                indexCount > meshData->IndexCount - indexOffset)
            {
                continue;
            }

            constexpr uint32_t vertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
            const uint64_t vertexOffset = static_cast<uint64_t>(draw.VertexOffset) * vertexStride;
            if (vertexOffset >= meshData->VertexBuffer->GetSize() ||
                (meshData->VertexBuffer->GetSize() - vertexOffset) % vertexStride != 0)
            {
                continue;
            }
            const uint64_t vertexCount64 =
                (meshData->VertexBuffer->GetSize() - vertexOffset) / vertexStride;
            if (vertexCount64 < 3 || vertexCount64 > std::numeric_limits<uint32_t>::max())
            {
                continue;
            }

            const uint32_t instanceCount = draw.bInstanced ? draw.InstanceCount : 1u;
            if (instanceCount == 0 ||
                (draw.bInstanced &&
                 static_cast<uint64_t>(draw.InstanceDataOffset) + instanceCount > packet.InstanceData.size()))
            {
                continue;
            }

            const MaterialResourceData* materialData = materialResources
                                                           ? materialResources->GetData(draw.MaterialHandle)
                                                           : nullptr;
            const RayTracingHitMaterialSnapshot materialSnapshot =
                MakeRayTracingHitMaterialSnapshot(materialData);
            // 連番の1フレームでは前の値を物体ごとに固定するため、インスタンシング描画の各instanceの
            // 元の物体IDをMeshProxyとの照合で求める。連番でなければ従来どおり描画の値を使う。
            Container::VariableArray<uint64_t> instanceObjectIds;
            if (bResolveInstanceObjectIds && draw.bInstanced)
            {
                ResolveInstancedDrawObjectIds(meshProxies ? *meshProxies : packet.Scene.MeshProxies,
                                              packet, draw, instanceObjectIds);
            }

            for (uint32_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
            {
                if (packet.RayTracingScene.Instances.size() > 0x00FFFFFFu)
                {
                    break;
                }

                Math::Matrix4x4 worldTransform;
                Math::Matrix4x4 previousWorldTransform;
                const uint64_t dataIndex =
                    static_cast<uint64_t>(draw.InstanceDataOffset) + instanceIndex;
                const bool bHasInstanceData = dataIndex < packet.InstanceData.size();
                if (draw.bInstanced)
                {
                    const GPUSceneInstanceData& instanceData =
                        packet.InstanceData[dataIndex];
                    std::memcpy(worldTransform.values, instanceData.World, sizeof(instanceData.World));
                    std::memcpy(previousWorldTransform.values, instanceData.PreviousWorld,
                                sizeof(instanceData.PreviousWorld));
                }
                else
                {
                    worldTransform = draw.WorldMatrix;
                    if (bHasInstanceData &&
                        std::memcmp(packet.InstanceData[dataIndex].World,
                                    worldTransform.values,
                                    sizeof(packet.InstanceData[dataIndex].World)) == 0)
                    {
                        const GPUSceneInstanceData& instanceData = packet.InstanceData[dataIndex];
                        std::memcpy(previousWorldTransform.values, instanceData.PreviousWorld,
                                    sizeof(instanceData.PreviousWorld));
                    }
                    else
                    {
                        previousWorldTransform = worldTransform;
                    }
                }

                if (!IsFiniteRayTracingTransform(worldTransform))
                {
                    continue;
                }

                RayTracingSceneInstanceSnapshot instance;
                instance.MeshHandle = draw.MeshHandle;
                instance.ObjectId = draw.ObjectId;
                instance.ObjectInstanceIndex = instanceIndex;
                if (instanceIndex < instanceObjectIds.size() && instanceObjectIds[instanceIndex] != 0)
                {
                    instance.ObjectId = instanceObjectIds[instanceIndex];
                    instance.ObjectInstanceIndex = 0;
                }
                instance.SourceVertexBuffer = meshData->VertexBuffer;
                instance.SourceIndexBuffer = meshData->IndexBuffer;
                instance.IndexOffset = indexOffset;
                instance.IndexCount = indexCount;
                instance.VertexOffset = draw.VertexOffset;
                instance.VertexCount = static_cast<uint32_t>(vertexCount64);
                instance.VertexStride = vertexStride;
                instance.bGeometryOpaque = draw.MaterialBlendMode == BlendMode::Opaque;
                instance.Material = materialSnapshot;
                // GBufferと同じく、instance dataのObjectColorを表面色の係数として渡す。
                if (bHasInstanceData)
                {
                    std::memcpy(instance.Material.ObjectColor,
                                packet.InstanceData[dataIndex].ObjectColor,
                                sizeof(instance.Material.ObjectColor));
                }
                instance.Instance.customIndex =
                    static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
                instance.Instance.mask = draw.bCastShadow ? RayTracingInstanceMaskShadowCaster
                                                          : RayTracingInstanceMaskNonShadowCaster;
                CopyRayTracingInstanceTransform(worldTransform, instance.Instance.transform);
                if (IsFiniteRayTracingTransform(previousWorldTransform))
                {
                    CopyRayTracingInstanceTransform(previousWorldTransform,
                                                    instance.PreviousTransform);
                    instance.bHasPreviousTransform = true;
                }
                packet.RayTracingScene.Instances.push_back(std::move(instance));
            }
        }

        if (megaGeometryResources)
        {
            AppendMegaGeometryInstances(meshResources,
                                        megaGeometryResources,
                                        megaGeometryProxies ? *megaGeometryProxies : packet.Scene.MegaGeometryProxies,
                                        packet);
        }

        return true;
    }

    void RayTracingSceneSubsystem::AppendMegaGeometryInstances(
        const MeshResources* meshResources,
        const MegaGeometryResources* megaGeometryResources,
        const Container::VariableArray<MegaGeometryProxy>& megaProxies,
        FramePacket& packet)
    {
        if (!megaGeometryResources)
        {
            return;
        }
        SelectionStats stats;
        stats.bRebuilt = true;
        stats.PatchedSlots = static_cast<uint32_t>(megaProxies.size());
        const uint32_t meshCount = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        float origin[3] = {};
        const bool bHasOrigin = GetRayTracingSelectOrigin(packet, origin);
        Container::VariableArray<SelectCandidate> candidates;
        candidates.reserve(static_cast<size_t>(meshCount) + megaProxies.size());
        // 番号はメッシュのインスタンスがパケットの並び、MegaGeometry が meshCount + 配列の並び
        CollectMeshCandidates(meshResources, packet, meshCount, origin, bHasOrigin, candidates, stats);
        RayTracingMegaMeshBoundsLookup meshBounds(megaGeometryResources);
        for (uint32_t index = 0; index < static_cast<uint32_t>(megaProxies.size()); ++index)
        {
            const MegaGeometryProxy& proxy = megaProxies[index];
            if (!proxy.IsValid() || !IsFiniteRayTracingTransform(proxy.WorldTransform))
            {
                continue;
            }
            float sphere[4] = {};
            MakeRayTracingSelectBounds(proxy, meshBounds.Find(proxy.MegaMeshHandle), sphere);
            ConsiderCandidate(origin, bHasOrigin, sphere, meshCount + index, candidates, stats);
        }
        stats.OverLimit = SelectNearest(candidates, RayTracingSelectMaxInstances);
        KeepSelectedMeshInstances(packet, candidates, meshCount);
        stats.MeshInstances = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());

        // 全量の経路は席の配列を持たないので、選んだ proxy からその場で入力を作る
        RayTracingMegaMeshLookup meshes(*megaGeometryResources);
        MegaSlotEntry entry;
        for (const SelectCandidate& candidate : candidates)
        {
            if (candidate.Index < meshCount)
            {
                continue;
            }
            const MegaGeometryProxy& proxy = megaProxies[candidate.Index - meshCount];
            const MegaGeometry::MegaMeshGPUData* gpuData = meshes.Find(proxy.MegaMeshHandle);
            if (!gpuData)
            {
                ++stats.NotReady;
                continue;
            }
            MakeMegaSlotEntry(&proxy, meshBounds.Find(proxy.MegaMeshHandle), entry);
            AppendMegaInstance(*gpuData, entry, packet);
        }
        stats.Instances = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        stats.MegaInstances = stats.Instances - stats.MeshInstances;
        LogSelection("full", stats, m_FullSelectionLogCounter);
    }

    void RayTracingSceneSubsystem::AppendMegaGeometryInstancesFromMirror(
        const MeshResources* meshResources,
        const MegaGeometryResources* megaGeometryResources,
        const MegaGeometryMirror& mirror,
        FramePacket& packet)
    {
        SelectionStats stats;
        stats.PatchedSlots = SyncMegaSlots(mirror, megaGeometryResources, stats.bRebuilt);
        const uint32_t meshCount = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        float origin[3] = {};
        const bool bHasOrigin = GetRayTracingSelectOrigin(packet, origin);
        const uint32_t slotCount = static_cast<uint32_t>(m_MegaSlots.size());
        Container::VariableArray<SelectCandidate>& candidates = m_MegaSelectCandidates;
        candidates.clear();
        // 番号はメッシュのインスタンスがパケットの並び、席が meshCount + 席の番号、
        // 席を持たない proxy が meshCount + slotCount + 並び
        CollectMeshCandidates(meshResources, packet, meshCount, origin, bHasOrigin, candidates, stats);
        if (megaGeometryResources)
        {
            for (uint32_t slot = 1; slot < slotCount; ++slot)
            {
                const MegaSlotEntry& entry = m_MegaSlots[slot];
                if (entry.bUsable)
                {
                    ConsiderCandidate(origin, bHasOrigin, entry.BoundsSphere, meshCount + slot, candidates, stats);
                }
            }
            for (uint32_t index = 0; index < static_cast<uint32_t>(m_UnslottedMegaEntries.size()); ++index)
            {
                const MegaSlotEntry& entry = m_UnslottedMegaEntries[index];
                if (entry.bUsable)
                {
                    ConsiderCandidate(origin, bHasOrigin, entry.BoundsSphere, meshCount + slotCount + index,
                                      candidates, stats);
                }
            }
        }
        stats.OverLimit = SelectNearest(candidates, RayTracingSelectMaxInstances);
        KeepSelectedMeshInstances(packet, candidates, meshCount);
        stats.MeshInstances = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());

        if (megaGeometryResources)
        {
            // 選んだ席だけ、席の入力（変わった席だけ直した変換・前の変換・マスク）とメッシュの GPU データからインスタンスを足す。
            // 変わらない席は写しを読まない
            RayTracingMegaMeshLookup meshes(*megaGeometryResources);
            for (const SelectCandidate& candidate : candidates)
            {
                if (candidate.Index < meshCount)
                {
                    continue;
                }
                const uint32_t index = candidate.Index - meshCount;
                const MegaSlotEntry& entry =
                    index < slotCount ? m_MegaSlots[index] : m_UnslottedMegaEntries[index - slotCount];
                const MegaGeometry::MegaMeshGPUData* gpuData = meshes.Find(entry.MegaMeshHandle);
                if (!gpuData)
                {
                    ++stats.NotReady;
                    continue;
                }
                AppendMegaInstance(*gpuData, entry, packet);
            }
        }
        stats.Instances = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        stats.MegaInstances = stats.Instances - stats.MeshInstances;
        LogSelection("mirror", stats, m_MirrorSelectionLogCounter);

        // 連番のパストレーシング: 選んだ後のインスタンスで前の値を持ち越す（描くパケットごと）。
        // 連番の1フレームの中ではどのパケットも同じ状態なので、描いたパケットの状態を前の値にしてよい。連番でなければ状態を捨てる
        ApplyPathTracingSequenceCarry(packet, m_RenderThreadSequenceCarry);
    }

    uint32_t RayTracingSceneSubsystem::SyncMegaSlots(const MegaGeometryMirror& mirror,
                                                     const MegaGeometryResources* megaGeometryResources,
                                                     bool& outRebuilt)
    {
        outRebuilt = false;
        const bool bSameSource = m_bMegaSlotsValid && m_MegaSlotSource == &mirror &&
                                 m_MegaSlotGeneration == mirror.GetGeneration();
        if (bSameSource && m_MegaSlotRevision == mirror.GetRevision())
        {
            return 0u;
        }

        // 前に見た版から数えた変わった席だけを直す（外れた席は写しに無いので使わない印にする）
        if (bSameSource && mirror.CanPatchFrom(m_MegaSlotRevision))
        {
            const Container::VariableArray<uint32_t>& changedSlots = mirror.GetChangedSlots();
            RayTracingMegaMeshBoundsLookup meshBounds(megaGeometryResources);
            for (uint32_t slot : changedSlots)
            {
                if (slot == 0)
                {
                    continue;
                }
                if (slot >= m_MegaSlots.size())
                {
                    m_MegaSlots.resize(static_cast<size_t>(slot) + 1u);
                }
                const MegaGeometryProxy* proxy = mirror.FindBySlot(slot);
                MakeMegaSlotEntry(proxy, proxy ? meshBounds.Find(proxy->MegaMeshHandle) : nullptr, m_MegaSlots[slot]);
            }
            m_MegaSlotRevision = mirror.GetRevision();
            return static_cast<uint32_t>(changedSlots.size());
        }

        // 直せない（全部が変わった・前に見た版から数えていない・別の写し）ので作り直す
        m_MegaSlots.clear();
        m_UnslottedMegaEntries.clear();
        RayTracingMegaMeshBoundsLookup meshBounds(megaGeometryResources);
        for (const MegaGeometryProxy& proxy : mirror.GetProxies())
        {
            if (proxy.SceneSlot == 0)
            {
                m_UnslottedMegaEntries.emplace_back();
                MakeMegaSlotEntry(&proxy, meshBounds.Find(proxy.MegaMeshHandle), m_UnslottedMegaEntries.back());
                continue;
            }
            if (proxy.SceneSlot >= m_MegaSlots.size())
            {
                m_MegaSlots.resize(static_cast<size_t>(proxy.SceneSlot) + 1u);
            }
            MakeMegaSlotEntry(&proxy, meshBounds.Find(proxy.MegaMeshHandle), m_MegaSlots[proxy.SceneSlot]);
        }
        m_MegaSlotSource = &mirror;
        m_MegaSlotGeneration = mirror.GetGeneration();
        m_MegaSlotRevision = mirror.GetRevision();
        m_bMegaSlotsValid = true;
        outRebuilt = true;
        return static_cast<uint32_t>(mirror.GetProxies().size());
    }

    void RayTracingSceneSubsystem::MakeMegaSlotEntry(const MegaGeometryProxy* proxy,
                                                     const BoundingSphere* localBounds,
                                                     MegaSlotEntry& entry)
    {
        entry = MegaSlotEntry{};
        if (!proxy || !proxy->IsValid() || !IsFiniteRayTracingTransform(proxy->WorldTransform))
        {
            return;
        }
        MakeRayTracingSelectBounds(*proxy, localBounds, entry.BoundsSphere);
        CopyRayTracingInstanceTransform(proxy->WorldTransform, entry.Transform);
        // 前の変換は写しの NoteDrawn が最後に描いたフレームから決めた値（変わると席が変わったものとして知らされる）
        if (IsFiniteRayTracingTransform(proxy->PreviousWorldTransform))
        {
            CopyRayTracingInstanceTransform(proxy->PreviousWorldTransform, entry.PreviousTransform);
            entry.bHasPreviousTransform = true;
        }
        entry.ObjectId = proxy->ObjectId;
        entry.MegaMeshHandle = proxy->MegaMeshHandle;
        entry.Mask = proxy->bCastShadow ? RayTracingInstanceMaskShadowCaster : RayTracingInstanceMaskNonShadowCaster;
        entry.bUsable = true;
    }

    void RayTracingSceneSubsystem::AppendMegaInstance(const MegaGeometry::MegaMeshGPUData& gpuData,
                                                      const MegaSlotEntry& entry,
                                                      FramePacket& packet)
    {
        // GBuffer ではクラスタ単位で GPU が選ぶが、光線には影に使う段（既定は LOD0）をそのまま見せる（点光源の影と同じ範囲）。
        // 表面色は MegaGeometryPass の GBuffer と同じく材質の BaseColor を instance 色とし、texture は白とみなす。
        constexpr uint32_t vertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
        RayTracingSceneInstanceSnapshot instance;
        instance.ObjectId = entry.ObjectId;
        instance.ObjectInstanceIndex = 0;
        instance.SourceVertexBuffer = gpuData.VertexBuffer;
        instance.SourceIndexBuffer = gpuData.IndexBuffer;
        instance.VertexBufferOffsetBytes = gpuData.VertexBufferOffsetBytes;
        instance.IndexBufferOffsetBytes = gpuData.IndexBufferOffsetBytes;
        instance.MegaMeshId = entry.MegaMeshHandle.Id;
        instance.GeometryRegionOwner = gpuData.RegionOwner;
        instance.IndexOffset = gpuData.ShadowFirstIndex;
        instance.IndexCount = gpuData.ShadowIndexCount;
        instance.VertexOffset = 0;
        instance.VertexCount = gpuData.VertexCount;
        instance.VertexStride = vertexStride;
        instance.bGeometryOpaque = true;
        const MegaGeometry::MegaMeshMaterial& material = gpuData.Material;
        for (uint32_t channel = 0u; channel < 4u; ++channel)
        {
            instance.Material.BaseColor[channel] = material.BaseColor[channel];
            instance.Material.ObjectColor[channel] = material.BaseColor[channel];
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            instance.Material.EmissiveColor[channel] = material.EmissiveColor[channel];
        }
        instance.Material.EmissiveLuminanceNits = material.EmissiveLuminanceNits;
        instance.Instance.customIndex = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        instance.Instance.mask = entry.Mask;
        std::memcpy(instance.Instance.transform, entry.Transform, sizeof(entry.Transform));
        if (entry.bHasPreviousTransform)
        {
            std::memcpy(instance.PreviousTransform, entry.PreviousTransform, sizeof(entry.PreviousTransform));
            instance.bHasPreviousTransform = true;
        }
        packet.RayTracingScene.Instances.push_back(std::move(instance));
    }

    void RayTracingSceneSubsystem::ConsiderCandidate(const float origin[3],
                                                     bool bHasOrigin,
                                                     const float sphere[4],
                                                     uint32_t index,
                                                     Container::VariableArray<SelectCandidate>& candidates,
                                                     SelectionStats& stats)
    {
        ++stats.Candidates;
        const float distance = bHasOrigin ? DistanceToRayTracingSelectBounds(origin, sphere) : 0.0f;
        if (distance > RayTracingSelectRadiusMeters)
        {
            ++stats.OutOfRange;
            return;
        }
        candidates.push_back(SelectCandidate{distance, index});
    }

    void RayTracingSceneSubsystem::CollectMeshCandidates(const MeshResources* meshResources,
                                                         const FramePacket& packet,
                                                         uint32_t meshCount,
                                                         const float origin[3],
                                                         bool bHasOrigin,
                                                         Container::VariableArray<SelectCandidate>& candidates,
                                                         SelectionStats& stats)
    {
        // 同じ描画のインスタンスは並んでいるので、直前のメッシュのローカルの AABB を使い回す
        uint64_t lastMeshId = 0;
        bool bHasLast = false;
        bool bLastHasBounds = false;
        BoundingBox localBounds;
        for (uint32_t index = 0; index < meshCount; ++index)
        {
            const RayTracingSceneInstanceSnapshot& instance = packet.RayTracingScene.Instances[index];
            if (!bHasLast || instance.MeshHandle.Id != lastMeshId)
            {
                lastMeshId = instance.MeshHandle.Id;
                bHasLast = true;
                bLastHasBounds = meshResources && meshResources->TryGetLocalBounds(instance.MeshHandle, localBounds) &&
                                 localBounds.MinX <= localBounds.MaxX && localBounds.MinY <= localBounds.MaxY &&
                                 localBounds.MinZ <= localBounds.MaxZ;
            }
            float sphere[4] = {};
            MakeRayTracingMeshSelectBounds(instance.Instance.transform, bLastHasBounds ? &localBounds : nullptr, sphere);
            ConsiderCandidate(origin, bHasOrigin, sphere, index, candidates, stats);
        }
    }

    void RayTracingSceneSubsystem::KeepSelectedMeshInstances(FramePacket& packet,
                                                             const Container::VariableArray<SelectCandidate>& candidates,
                                                             uint32_t meshCount)
    {
        Container::VariableArray<RayTracingSceneInstanceSnapshot>& instances = packet.RayTracingScene.Instances;
        uint32_t kept = 0;
        for (const SelectCandidate& candidate : candidates)
        {
            if (candidate.Index >= meshCount)
            {
                break;
            }
            if (candidate.Index != kept)
            {
                instances[kept] = std::move(instances[candidate.Index]);
            }
            instances[kept].Instance.customIndex = kept;
            ++kept;
        }
        if (kept < instances.size())
        {
            instances.resize(kept);
        }
    }

    uint32_t RayTracingSceneSubsystem::SelectNearest(Container::VariableArray<SelectCandidate>& candidates,
                                                     uint32_t budget)
    {
        if (candidates.size() <= budget)
        {
            return 0u;
        }
        const uint32_t overLimit = static_cast<uint32_t>(candidates.size() - budget);
        if (budget > 0u)
        {
            // 近い順（同じ距離は番号の小さい方）に budget 個を前へ集め、入れる順は番号の昇順に戻す（並びを決定的に保つ）
            const auto nearer = [](const SelectCandidate& lhs, const SelectCandidate& rhs)
            {
                return lhs.Distance != rhs.Distance ? lhs.Distance < rhs.Distance : lhs.Index < rhs.Index;
            };
            std::nth_element(candidates.begin(), candidates.begin() + (budget - 1u), candidates.end(), nearer);
        }
        candidates.resize(budget);
        std::sort(candidates.begin(), candidates.end(),
                  [](const SelectCandidate& lhs, const SelectCandidate& rhs) { return lhs.Index < rhs.Index; });
        return overLimit;
    }

    void RayTracingSceneSubsystem::LogSelection(const char* path, const SelectionStats& stats, uint32_t& counter)
    {
        // 最初と、その後は 120 回ごとに出す。excluded は規則（距離・上限）で外したインスタンス（メッシュと MegaGeometry）の数
        if ((counter++ % 120u) != 0u)
        {
            return;
        }
        NORVES_LOG_INFO("RayTracingSceneSubsystem",
                        "RT_SCENE instances=%u excluded=%u path=%s mesh=%u mega=%u candidates=%u out_of_range=%u "
                        "over_limit=%u not_ready=%u radius_m=%.0f limit=%u patched_slots=%u rebuilt=%u",
                        stats.Instances,
                        stats.OutOfRange + stats.OverLimit,
                        path,
                        stats.MeshInstances,
                        stats.MegaInstances,
                        stats.Candidates,
                        stats.OutOfRange,
                        stats.OverLimit,
                        stats.NotReady,
                        static_cast<double>(RayTracingSelectRadiusMeters),
                        RayTracingSelectMaxInstances,
                        stats.PatchedSlots,
                        stats.bRebuilt ? 1u : 0u);
    }

    bool RayTracingSceneSubsystem::BuildAccelerationStructures(RHI::DevicePtr device,
                                                                RHI::ICommandList& commandList,
                                                                uint32_t frameSlot,
                                                                FramePacket& packet)
    {
        auto clearPacketAccelerationStructures = [&packet]()
        {
            packet.RayTracingScene.TopLevel.reset();
            for (RayTracingSceneInstanceSnapshot& instance : packet.RayTracingScene.Instances)
            {
                instance.BottomLevel.reset();
                instance.AccelerationStructureVertexBuffer.reset();
                instance.AccelerationStructureIndexBuffer.reset();
            }
        };

        if (!device || !device->GetCapabilities().RayTracing.bAccelerationStructure)
        {
            clearPacketAccelerationStructures();
            m_BottomLevelCache.clear();
            for (TopLevelCacheEntry& topLevel : m_TopLevelCache)
            {
                topLevel = TopLevelCacheEntry{};
            }
            return true;
        }

        if (frameSlot >= FRAME_PACKET_BUFFER_COUNT)
        {
            clearPacketAccelerationStructures();
            return false;
        }

        if (packet.RayTracingScene.Instances.empty())
        {
            clearPacketAccelerationStructures();
            m_BottomLevelCache.clear();
            for (TopLevelCacheEntry& topLevel : m_TopLevelCache)
            {
                topLevel = TopLevelCacheEntry{};
            }
            return true;
        }

        Container::VariableArray<BottomLevelCacheEntry> activeBottomLevels;
        Container::VariableArray<RHI::AccelerationStructureInstanceDesc> tlasInstances;
        for (RayTracingSceneInstanceSnapshot& instance : packet.RayTracingScene.Instances)
        {
            if (!instance.SourceVertexBuffer || !instance.SourceIndexBuffer ||
                instance.IndexCount < 3 || instance.IndexCount % 3 != 0 ||
                instance.VertexCount < 3 || instance.VertexStride < sizeof(float) * 3u)
            {
                clearPacketAccelerationStructures();
                return false;
            }

            auto matchesGeometry = [&instance](const BottomLevelCacheEntry& candidate)
            {
                return candidate.MeshHandle == instance.MeshHandle &&
                       candidate.SourceVertexBuffer == instance.SourceVertexBuffer &&
                       candidate.SourceIndexBuffer == instance.SourceIndexBuffer &&
                       candidate.VertexBufferOffsetBytes == instance.VertexBufferOffsetBytes &&
                       candidate.IndexBufferOffsetBytes == instance.IndexBufferOffsetBytes &&
                       candidate.MegaMeshId == instance.MegaMeshId &&
                       candidate.IndexOffset == instance.IndexOffset &&
                       candidate.IndexCount == instance.IndexCount &&
                       candidate.VertexOffset == instance.VertexOffset &&
                       candidate.VertexCount == instance.VertexCount &&
                       candidate.VertexStride == instance.VertexStride &&
                       candidate.bGeometryOpaque == instance.bGeometryOpaque;
            };

            BottomLevelCacheEntry* bottomLevel = nullptr;
            for (BottomLevelCacheEntry& candidate : activeBottomLevels)
            {
                if (matchesGeometry(candidate))
                {
                    bottomLevel = &candidate;
                    break;
                }
            }
            if (!bottomLevel)
            {
                for (const BottomLevelCacheEntry& candidate : m_BottomLevelCache)
                {
                    if (matchesGeometry(candidate))
                    {
                        activeBottomLevels.push_back(candidate);
                        bottomLevel = &activeBottomLevels.back();
                        break;
                    }
                }
            }

            if (!bottomLevel)
            {
                BottomLevelCacheEntry entry;
                entry.MeshHandle = instance.MeshHandle;
                entry.SourceVertexBuffer = instance.SourceVertexBuffer;
                entry.SourceIndexBuffer = instance.SourceIndexBuffer;
                entry.VertexBufferOffsetBytes = instance.VertexBufferOffsetBytes;
                entry.IndexBufferOffsetBytes = instance.IndexBufferOffsetBytes;
                entry.MegaMeshId = instance.MegaMeshId;
                entry.IndexOffset = instance.IndexOffset;
                entry.IndexCount = instance.IndexCount;
                entry.VertexOffset = instance.VertexOffset;
                entry.VertexCount = instance.VertexCount;
                entry.VertexStride = instance.VertexStride;
                entry.bGeometryOpaque = instance.bGeometryOpaque;
                entry.VertexBuffer = CreateAddressableMeshBuffer(
                    *device,
                    instance.SourceVertexBuffer,
                    RHI::ResourceUsage::VertexBuffer,
                    "RayTracingScene.VertexInput");
                entry.IndexBuffer = CreateAddressableMeshBuffer(
                    *device,
                    instance.SourceIndexBuffer,
                    RHI::ResourceUsage::IndexBuffer,
                    "RayTracingScene.IndexInput");
                if (!entry.VertexBuffer || !entry.IndexBuffer)
                {
                    clearPacketAccelerationStructures();
                    return false;
                }

                const uint32_t primitiveCount = instance.IndexCount / 3u;
                RHI::AccelerationStructureDesc blasDesc;
                blasDesc.type = RHI::AccelerationStructureType::BottomLevel;
                blasDesc.geometryCapacities.push_back(
                    {RHI::AccelerationStructureGeometryType::Triangles,
                     primitiveCount,
                     instance.bGeometryOpaque});
                entry.Structure = device->CreateAccelerationStructure(blasDesc);
                if (!entry.Structure)
                {
                    clearPacketAccelerationStructures();
                    return false;
                }

                RHI::AccelerationStructureGeometryDesc geometry;
                geometry.type = RHI::AccelerationStructureGeometryType::Triangles;
                geometry.opaque = instance.bGeometryOpaque;
                geometry.triangles.vertexBuffer = entry.VertexBuffer;
                geometry.triangles.vertexOffset = instance.VertexBufferOffsetBytes +
                    static_cast<uint64_t>(instance.VertexOffset) * instance.VertexStride;
                geometry.triangles.vertexCount = instance.VertexCount;
                geometry.triangles.vertexStride = instance.VertexStride;
                geometry.triangles.vertexFormat = RHI::Format::R32G32B32_FLOAT;
                geometry.triangles.indexBuffer = entry.IndexBuffer;
                geometry.triangles.indexOffset = instance.IndexBufferOffsetBytes +
                    static_cast<uint64_t>(instance.IndexOffset) * sizeof(uint32_t);
                geometry.triangles.indexCount = instance.IndexCount;
                geometry.triangles.indexFormat = RHI::IndexType::Uint32;

                RHI::AccelerationStructureBuildDesc blasBuild;
                blasBuild.type = RHI::AccelerationStructureType::BottomLevel;
                blasBuild.destination = entry.Structure;
                blasBuild.geometries.push_back(geometry);
                // ICommandListのBuildはTLAS用。BLASは加速構造リソースから同期構築する。
                if (!entry.Structure->Build(blasBuild))
                {
                    clearPacketAccelerationStructures();
                    return false;
                }

                activeBottomLevels.push_back(std::move(entry));
                bottomLevel = &activeBottomLevels.back();
            }

            instance.BottomLevel = bottomLevel->Structure;
            instance.AccelerationStructureVertexBuffer = bottomLevel->VertexBuffer;
            instance.AccelerationStructureIndexBuffer = bottomLevel->IndexBuffer;
            RHI::AccelerationStructureInstanceDesc tlasInstance = instance.Instance;
            tlasInstance.bottomLevel = bottomLevel->Structure;
            tlasInstances.push_back(std::move(tlasInstance));
        }

        m_BottomLevelCache = std::move(activeBottomLevels);
        if (tlasInstances.empty())
        {
            m_TopLevelCache[frameSlot] = TopLevelCacheEntry{};
            return true;
        }

        TopLevelCacheEntry& cachedTopLevel = m_TopLevelCache[frameSlot];
        const uint32_t instanceCount = static_cast<uint32_t>(tlasInstances.size());
        ++m_TopLevelUpdateStats.Frames;
        m_TopLevelUpdateStats.LastInstances = instanceCount;

        // この slot で前回記録した入力（集合と並び・変換・マスク・BLAS）と全部一致すれば、slot の TLAS はすでにこの内容なので
        // Update を記録しない。パケットのスロットは GPU の完了を保証しない（RenderThread は提出の直後にパケットを Empty へ戻し、
        // 同じスロットを続けて描くことがある）。前の利用の GPU の読み書きとの順序はフレームの頭の全体のバリアで守り、
        // インスタンスの表と scratch は呼び出しごとの lease
        if (cachedTopLevel.Structure && SameTopLevelInstances(cachedTopLevel.Instances, tlasInstances))
        {
            ++m_TopLevelUpdateStats.Skipped;
            LogTopLevelUpdates(false);
            packet.RayTracingScene.TopLevel = cachedTopLevel.Structure;
            return true;
        }

        RHI::AccelerationStructurePtr topLevel;
        if (cachedTopLevel.Structure && cachedTopLevel.InstanceCount == instanceCount)
        {
            RHI::AccelerationStructureBuildDesc tlasUpdate;
            tlasUpdate.type = RHI::AccelerationStructureType::TopLevel;
            tlasUpdate.mode = RHI::AccelerationStructureBuildMode::Update;
            tlasUpdate.destination = cachedTopLevel.Structure;
            tlasUpdate.source = cachedTopLevel.Structure;
            tlasUpdate.instances = std::move(tlasInstances);
            if (commandList.UpdateAccelerationStructure(tlasUpdate))
            {
                topLevel = cachedTopLevel.Structure;
                cachedTopLevel.Instances = std::move(tlasUpdate.instances);
                ++m_TopLevelUpdateStats.Updated;
            }
            else
            {
                tlasInstances = std::move(tlasUpdate.instances);
            }
        }

        if (!topLevel)
        {
            RHI::AccelerationStructureDesc tlasDesc;
            tlasDesc.type = RHI::AccelerationStructureType::TopLevel;
            tlasDesc.maxInstanceCount = instanceCount;
            tlasDesc.allowUpdate = true;
            topLevel = device->CreateAccelerationStructure(tlasDesc);
            if (!topLevel)
            {
                clearPacketAccelerationStructures();
                return false;
            }

            RHI::AccelerationStructureBuildDesc tlasBuild;
            tlasBuild.type = RHI::AccelerationStructureType::TopLevel;
            tlasBuild.destination = topLevel;
            tlasBuild.instances = std::move(tlasInstances);
            if (!commandList.BuildAccelerationStructure(tlasBuild))
            {
                // slot の TLAS を作り直せなかったので、前回の入力は覚えない（次は一致させずに記録し直す）
                cachedTopLevel = TopLevelCacheEntry{};
                clearPacketAccelerationStructures();
                return false;
            }

            cachedTopLevel.Structure = topLevel;
            cachedTopLevel.InstanceCount = instanceCount;
            cachedTopLevel.Instances = std::move(tlasBuild.instances);
            ++m_TopLevelUpdateStats.Built;
        }

        LogTopLevelUpdates(false);
        packet.RayTracingScene.TopLevel = topLevel;
        return true;
    }

    bool RayTracingSceneSubsystem::SameTopLevelInstances(
        const Container::VariableArray<RHI::AccelerationStructureInstanceDesc>& lhs,
        const Container::VariableArray<RHI::AccelerationStructureInstanceDesc>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (size_t index = 0; index < lhs.size(); ++index)
        {
            const RHI::AccelerationStructureInstanceDesc& a = lhs[index];
            const RHI::AccelerationStructureInstanceDesc& b = rhs[index];
            if (a.bottomLevel.get() != b.bottomLevel.get() || a.customIndex != b.customIndex || a.mask != b.mask ||
                a.shaderBindingTableRecordOffset != b.shaderBindingTableRecordOffset ||
                a.disableTriangleFacingCull != b.disableTriangleFacingCull ||
                std::memcmp(a.transform, b.transform, sizeof(a.transform)) != 0)
            {
                return false;
            }
        }
        return true;
    }

    void RayTracingSceneSubsystem::LogTopLevelUpdates(bool bFlush)
    {
        // 最初と、その後は 120 フレームごとに、前に出してからの内訳を出す。bFlush は出していない残りを出す（Shutdown）
        if (bFlush ? m_TopLevelUpdateStats.Frames == 0u : (m_TopLevelLogCounter++ % 120u) != 0u)
        {
            return;
        }
        NORVES_LOG_INFO("RayTracingSceneSubsystem",
                        "RT_SCENE update_skipped=%u updated=%u built=%u frames=%u instances=%u",
                        m_TopLevelUpdateStats.Skipped,
                        m_TopLevelUpdateStats.Updated,
                        m_TopLevelUpdateStats.Built,
                        m_TopLevelUpdateStats.Frames,
                        m_TopLevelUpdateStats.LastInstances);
        m_TopLevelUpdateStats = TopLevelUpdateStats{};
    }

    // ========================================
    // RenderingCoordinator
    // ========================================

    RenderingCoordinator::RenderingCoordinator()
        : m_Diagnostics(Container::MakeUnique<RenderingCoordinatorDiagnostics>())
    {
    }

    RenderingCoordinator::~RenderingCoordinator() = default;

    RenderingCoordinatorStatsSnapshot RenderingCoordinator::GetStatsSnapshot() const
    {
        return m_Diagnostics ? m_Diagnostics->GetStatsSnapshot() : RenderingCoordinatorStatsSnapshot{};
    }

    Debug::RenderingStats RenderingCoordinator::GetStats() const
    {
        return GetStatsSnapshot().Stats;
    }

    void RenderingCoordinator::RequestRenderGraphDebugDump()
    {
        if (m_Diagnostics)
        {
            m_Diagnostics->RequestRenderGraphDebugDump();
        }
    }

    bool RenderingCoordinator::TryGetRenderGraphDebugDumpSnapshot(
        uint64_t knownPublicationSequence,
        RenderGraphDebugDumpSnapshot &outSnapshot) const
    {
        return m_Diagnostics &&
               m_Diagnostics->TryGetRenderGraphDebugDumpSnapshot(knownPublicationSequence, outSnapshot);
    }

    bool RenderingCoordinator::TryConsumeCompletedGPUTimings(
        Container::VariableArray<RenderPassGPUTiming>& outTimings,
        uint64_t& outDroppedFrameCount)
    {
        return m_GPUTimingMailbox.Consume(outTimings, outDroppedFrameCount);
    }

    bool RenderingCoordinator::SupportsGPUTimings() const
    {
        return m_CommandList && m_CommandList->SupportsGPUTimestamps();
    }

    void RenderingCoordinator::PublishCompletedGPUTimestampResults()
    {
        if (!m_CommandList)
        {
            return;
        }

        // 仕事の番号の順（提出の順）にリストごとの結果を集め、同じフレームの同じ区間の名前で合わせる。
        // FrameGPU は先頭のリストの開始から末尾のリストの終了まで、パスの区間は記録したリストの値になる
        const uint32_t listCount = m_JobCommandLists.empty()
                                       ? 1u
                                       : static_cast<uint32_t>(m_JobCommandLists.size());
        Container::VariableArray<Container::VariableArray<RHI::GPUTimestampResult>> perListResults;
        perListResults.resize(listCount);
        bool bAnyResult = false;
        for (uint32_t listIndex = 0u; listIndex < listCount; ++listIndex)
        {
            RHI::ICommandList* commandList = m_JobCommandLists.empty()
                                                 ? m_CommandList.get()
                                                 : m_JobCommandLists[listIndex].get();
            if (commandList)
            {
                commandList->ConsumeCompletedGPUTimestampResults(perListResults[listIndex]);
                bAnyResult = bAnyResult || !perListResults[listIndex].empty();
            }
        }
        if (!bAnyResult)
        {
            return;
        }
        Container::VariableArray<RHI::GPUTimestampResult> results;
        RHI::MergeGPUTimestampResultsAcrossCommandLists(perListResults.data(), listCount, results);

        Container::VariableArray<RenderPassGPUTiming> timings;
        timings.reserve(results.size());
#if NORVES_ENABLE_STATS
        NorvesLib::Debug::StatsManager& statsManager = NorvesLib::Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        uint64_t latestFrameGPUFrameNumber = 0u;
        bool bLatestFrameGPUFound = false;
#endif
        for (const RHI::GPUTimestampResult& result : results)
        {
#if NORVES_ENABLE_STATS
            // 完了した最も新しいフレームの FrameGPU（全リストに合わせた値）を、フレームの GPU 時間の統計に使う
            if (bTraceActive && result.bValid && result.DurationMs > 0.0f &&
                result.ScopeName == FrameGPUScopeName &&
                (!bLatestFrameGPUFound || result.FrameNumber >= latestFrameGPUFrameNumber))
            {
                latestFrameGPUFrameNumber = result.FrameNumber;
                bLatestFrameGPUFound = true;
                m_LatestCompletedGPUTimeMs = result.DurationMs;
                m_bLatestCompletedGPUTimeValid = true;
            }
            // 完了したフレームの区間（FrameGPU・加速構造の更新・RenderGraph のパス）をトレースの行へ書く。
            if (bTraceActive && result.bValid)
            {
                statsManager.RecordGPUScope(result.FrameNumber, result.ScopeName.c_str(), result.DurationMs);
            }
#endif
            RenderPassGPUTiming timing;
            timing.FrameNumber = result.FrameNumber;
            timing.PassName = result.ScopeName;
            timing.DurationMs = result.DurationMs;
            timing.bValid = result.bValid;
            timings.push_back(timing);
        }
#if NORVES_ENABLE_STATS
        if (bLatestFrameGPUFound)
        {
            statsManager.SetGPUFrameTimeMs(m_LatestCompletedGPUTimeMs);
        }
#endif
        m_GPUTimingMailbox.Append(timings);
    }

    bool RenderingCoordinator::Initialize(const RenderingCoordinatorSettings &settings)
    {
        if (m_bInitialized)
        {
            return true;
        }

        LOG_INFO("RenderingCoordinator::Initialize() - Starting initialization");

        m_PreviousCompletedTotalFrameTimeMs = 0.0f;
        m_LatestCompletedGPUTimeMs = 0.0f;
        m_bLatestCompletedGPUTimeValid = false;
        m_LatestAutoExposure = AutoExposureMeasurement{};
        m_SceneRevision = 1u;
        m_LightRevision = 1u;
        m_LastSceneRevisionHash = 0u;
        m_LastLightRevisionHash = 0u;
        m_bSceneRevisionHashValid = false;
        m_bLightRevisionHashValid = false;
        m_GPUTimingMailbox.Clear();

        if (m_Diagnostics)
        {
            m_Diagnostics->Reset();
        }

        m_Width = settings.Width;
        m_Height = settings.Height;
        m_RenderScale = std::clamp(settings.RenderScale, 0.5f, 1.0f);
        UpdateRenderResolution(m_Width, m_Height);
        m_bVSyncEnabled = settings.bVSync;
        m_bMultiThreadedRendering = settings.bEnableMultiThreadedRendering;
        m_MaxDrawCallsPerFrame = settings.MaxDrawCallsPerFrame;
        m_RenderGraph.SetDebugDumpOptions(settings.RenderGraphDumpOptions);
        m_bFrameSubmissionStarted = false;

        // ========================================
        // 1. RHIデバイス（RenderWorldから渡される）
        // ========================================
        m_Device = settings.Device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "RHI Device is null");
            return false;
        }

        const RHI::RayTracingCapabilities& rayTracingCapabilities =
            m_Device->GetCapabilities().RayTracing;
        m_DDGIVolume = SanitizeDDGIVolumeParametersForRHI(
            m_DDGIVolume,
            rayTracingCapabilities.bAccelerationStructure,
            rayTracingCapabilities.bRayQuery);

        // ========================================
        // 2. Screenの初期化（SwapChain作成を含む）
        // ========================================
        ScreenSettings screenSettings;
        screenSettings.Width = settings.Width;
        screenSettings.Height = settings.Height;
        screenSettings.WindowHandle = settings.WindowHandle;
        screenSettings.bVSync = settings.bVSync;
        screenSettings.BackBufferCount = settings.BackBufferCount;

        if (!m_Screen.Initialize(m_Device, screenSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize Screen");
            return false;
        }

        // ========================================
        // 3. CommandList作成
        // ========================================
        m_CommandList = m_Device->CreateCommandList();
        if (!m_CommandList)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create command list");
            m_Screen.Shutdown();
            return false;
        }
        m_JobCommandLists.clear();
        m_JobCommandLists.push_back(m_CommandList);

        // ========================================
        // 4. RenderPass作成（Screen SwapChain用）
        // ========================================
        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Screen SwapChain is null");
            return false;
        }

        const RHI::PresentationSurfaceDesc presentationSurface =
            swapChain->GetPresentationSurfaceDesc();
        const RHI::PresentationEncodePath presentationEncodePath =
            RHI::GetPresentationEncodePath(swapChain->GetFormat());
        if (presentationSurface.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
            presentationSurface.Transfer != RHI::PresentationTransfer::SRGB ||
            presentationEncodePath == RHI::PresentationEncodePath::Unsupported)
        {
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "Unsupported presentation surface: format=%u colorSpace=%u transfer=%u",
                             static_cast<unsigned int>(swapChain->GetFormat()),
                             static_cast<unsigned int>(presentationSurface.ColorSpace),
                             static_cast<unsigned int>(presentationSurface.Transfer));
            return false;
        }

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = swapChain->GetFormat();
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = true;
        // Cornflower blue clear color
        colorAttachment.clearColor[0] = 0.392f;
        colorAttachment.clearColor[1] = 0.584f;
        colorAttachment.clearColor[2] = 0.929f;
        colorAttachment.clearColor[3] = 1.0f;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::Present;

        RHI::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);

        // デプスステンシルアタッチメント
        RHI::AttachmentDesc depthAttachment;
        depthAttachment.format = RHI::Format::D32_FLOAT;
        depthAttachment.isDepthStencil = true;
        depthAttachment.clear = true;
        depthAttachment.clearDepth = 1.0f;
        depthAttachment.clearStencil = 0;
        depthAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        depthAttachment.storeOp = RHI::AttachmentStoreOp::DontCare;
        depthAttachment.initialState = RHI::ResourceState::Undefined;
        depthAttachment.finalState = RHI::ResourceState::DepthWrite;

        renderPassDesc.depthStencilAttachment = depthAttachment;
        renderPassDesc.hasDepthStencil = true;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create render pass");
            return false;
        }

        RHI::AttachmentDesc loadColorAttachment = colorAttachment;
        loadColorAttachment.clear = false;
        loadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadColorAttachment.initialState = RHI::ResourceState::Present;

        RHI::AttachmentDesc loadDepthAttachment = depthAttachment;
        loadDepthAttachment.clear = false;
        loadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc loadRenderPassDesc;
        loadRenderPassDesc.colorAttachments.push_back(loadColorAttachment);
        loadRenderPassDesc.depthStencilAttachment = loadDepthAttachment;
        loadRenderPassDesc.hasDepthStencil = true;

        m_PresentationLoadRenderPass = m_Device->CreateRenderPass(loadRenderPassDesc);
        if (!m_PresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create presentation load render pass");
            return false;
        }

        RHI::AttachmentDesc graphClearColorAttachment = colorAttachment;
        graphClearColorAttachment.initialState = RHI::ResourceState::RenderTarget;

        RHI::AttachmentDesc graphLoadColorAttachment = graphClearColorAttachment;
        graphLoadColorAttachment.clear = false;
        graphLoadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;

        RHI::AttachmentDesc graphClearDepthAttachment = depthAttachment;
        graphClearDepthAttachment.initialState = RHI::ResourceState::Undefined;

        RHI::AttachmentDesc graphLoadDepthAttachment = depthAttachment;
        graphLoadDepthAttachment.clear = false;
        graphLoadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        graphLoadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc graphClearRenderPassDesc;
        graphClearRenderPassDesc.colorAttachments.push_back(graphClearColorAttachment);
        graphClearRenderPassDesc.depthStencilAttachment = graphClearDepthAttachment;
        graphClearRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationClearRenderPass = m_Device->CreateRenderPass(graphClearRenderPassDesc);
        if (!m_GraphPresentationClearRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation clear render pass");
            return false;
        }

        RHI::RenderPassDesc graphLoadRenderPassDesc;
        graphLoadRenderPassDesc.colorAttachments.push_back(graphLoadColorAttachment);
        graphLoadRenderPassDesc.depthStencilAttachment = graphLoadDepthAttachment;
        graphLoadRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationLoadRenderPass = m_Device->CreateRenderPass(graphLoadRenderPassDesc);
        if (!m_GraphPresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation load render pass");
            return false;
        }

        m_SwapChainFormat = swapChain->GetFormat();

        // ========================================
        // 5. Framebuffers（スワップチェーンイメージごと）
        // ========================================
        if (!CreateSwapChainFramebuffers())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffers");
            return false;
        }

        // ========================================
        // 6. ShaderManagerの初期化
        // ========================================
        if (!m_ShaderManager.Initialize(m_Device.get(), NORVES_SHADER_DIR))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize ShaderManager");
            return false;
        }

        // Slangコンパイラの設定（Neural Shaders対応GPUの場合）
        if (m_Device->GetCapabilities().NeuralShaders.bSupported)
        {
            auto slangCompiler = m_Device->CreateSlangShaderCompiler();
            if (slangCompiler)
            {
                m_ShaderManager.SetSlangCompiler(slangCompiler);
            }
            else
            {
                NORVES_LOG_WARNING("RenderingCoordinator",
                                   "Device reports Neural Shaders support but no Slang compiler is available");
            }
        }

        // ========================================
        // 7. Blitシェーダーの作成（ToneMappedColor → SwapChain合成用）
        // ========================================
        {
            // Blit用頂点シェーダー（フルスクリーン三角形）
            m_BlitVertexShader = m_ShaderManager.LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
            if (!m_BlitVertexShader)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit vertex shader");
                return false;
            }

            // Blit用フラグメントシェーダー
            m_BlitFragmentShader = m_ShaderManager.LoadShader("blit.frag", RHI::ShaderStage::Pixel);
            if (!m_BlitFragmentShader)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit fragment shader");
                return false;
            }

            // Blitサンプラー
            RHI::SamplerDesc samplerDesc;
            samplerDesc.filterMin = RHI::FilterMode::Linear;
            samplerDesc.filterMag = RHI::FilterMode::Linear;
            samplerDesc.filterMip = RHI::FilterMode::Linear;
            samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
            samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_BlitSampler = m_Device->CreateSampler(samplerDesc);
            if (!m_BlitSampler)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit sampler");
                return false;
            }

            // Blitディスクリプタセット（binding 0: CombinedImageSampler、binding 1: encode params）
            RHI::DescriptorSetDesc blitDsDesc;
            RHI::DescriptorBinding texBinding;
            texBinding.binding = 0;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(texBinding);

            RHI::DescriptorBinding encodeBinding;
            encodeBinding.binding = 1;
            encodeBinding.type = RHI::ResourceBindType::ConstantBuffer;
            encodeBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(encodeBinding);

            // 毎フレーム束ね直すので、飛行中のスロットごとに 1 組作る
            const uint32_t blitDescriptorSetCount = std::max<uint32_t>(swapChain->GetMaxFramesInFlight(), 1u);
            m_BlitDescriptorSets.clear();
            for (uint32_t slot = 0; slot < blitDescriptorSetCount; ++slot)
            {
                // 飛行中のスロットで引く組なので、守られていない上書きの見張りへ「守られている」と登録する
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                RHI::DescriptorSetPtr blitDescriptorSet = m_Device->CreateDescriptorSet(blitDsDesc);
                if (!blitDescriptorSet)
                {
                    NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit descriptor set");
                    return false;
                }

                // サンプラーを事前バインド（CombinedImageSamplerに必要）
                blitDescriptorSet->BindSampler(0, m_BlitSampler);
                m_BlitDescriptorSets.push_back(std::move(blitDescriptorSet));
            }

            RHI::PresentationEncodeParams presentationParams;
            presentationParams.EncodePath =
                presentationEncodePath == RHI::PresentationEncodePath::ShaderOETF ? 1u : 0u;
            RHI::BufferDesc presentationParamsDesc(sizeof(RHI::PresentationEncodeParams),
                                                    RHI::ResourceUsage::ConstantBuffer,
                                                    true,
                                                    "PresentationEncodeParams");
            RHI::BufferPtr presentationParamsBuffer = m_Device->CreateBuffer(presentationParamsDesc);
            if (!presentationParamsBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create presentation encode buffer");
                return false;
            }
            presentationParamsBuffer->Update(&presentationParams, sizeof(presentationParams));
            for (const RHI::DescriptorSetPtr& blitDescriptorSet : m_BlitDescriptorSets)
            {
                blitDescriptorSet->BindConstantBuffer(1,
                                                      presentationParamsBuffer,
                                                      0,
                                                      sizeof(presentationParams));
            }

            // Blitパイプライン
            RHI::GraphicsPipelineDesc blitPipelineDesc;
            blitPipelineDesc.vertexShader = m_BlitVertexShader;
            blitPipelineDesc.pixelShader = m_BlitFragmentShader;
            blitPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            blitPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            blitPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            blitPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            blitPipelineDesc.rasterState.lineWidth = 1.0f;
            blitPipelineDesc.depthStencilState.depthTestEnable = false;
            blitPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blitBlend;
            blitBlend.blendEnable = false;
            blitBlend.colorWriteMask = RHI::ColorWriteMask::All;
            blitPipelineDesc.blendState.attachments.push_back(blitBlend);
            blitPipelineDesc.renderPass = m_RenderPass;
            blitPipelineDesc.descriptorSetLayouts.push_back(blitDsDesc);

            m_BlitPipeline = m_Device->CreateGraphicsPipeline(blitPipelineDesc);
            if (!m_BlitPipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit pipeline");
                return false;
            }

            NORVES_LOG_INFO("RenderingCoordinator", "Blit compositing resources created");
        }

        // 旧三角形シェーダー・パイプラインも残す（フォールバック用）
        {
            m_TriangleVertexShader = m_ShaderManager.LoadShader("triangle.vert", RHI::ShaderStage::Vertex);
            m_TriangleFragmentShader = m_ShaderManager.LoadShader("triangle.frag", RHI::ShaderStage::Pixel);

            RHI::GraphicsPipelineDesc triPipelineDesc;
            triPipelineDesc.vertexShader = m_TriangleVertexShader;
            triPipelineDesc.pixelShader = m_TriangleFragmentShader;
            triPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            triPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            triPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            triPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            triPipelineDesc.rasterState.lineWidth = 1.0f;
            triPipelineDesc.depthStencilState.depthTestEnable = false;
            triPipelineDesc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc triBlend;
            triBlend.blendEnable = false;
            triBlend.colorWriteMask = RHI::ColorWriteMask::All;
            triPipelineDesc.blendState.attachments.push_back(triBlend);
            triPipelineDesc.renderPass = m_RenderPass;
            m_TrianglePipeline = m_Device->CreateGraphicsPipeline(triPipelineDesc);
        }

        // ========================================
        // 11. メインSceneViewの作成
        // ========================================
        SceneViewSettings sceneViewSettings;
        sceneViewSettings.Width = settings.Width;
        sceneViewSettings.Height = settings.Height;

        m_MainSceneView = Container::MakeShared<SceneView>();
        if (!m_MainSceneView->Initialize(sceneViewSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize main SceneView");
            m_Screen.Shutdown();
            return false;
        }

        auto mainViewport = Container::MakeShared<Viewport>();
        ViewportSettings viewportSettings;
        viewportSettings.X = 0.0f;
        viewportSettings.Y = 0.0f;
        viewportSettings.Width = 1.0f;
        viewportSettings.Height = 1.0f;
        if (!mainViewport->Initialize(viewportSettings))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize main Viewport");
            m_MainSceneView->Shutdown();
            m_MainSceneView.reset();
            m_Screen.Shutdown();
            return false;
        }
        m_MainSceneView->AddViewport(mainViewport);

        m_Screen.AddView(m_MainSceneView, 0);
        m_Views.push_back(m_MainSceneView);

        // ========================================
        // 12. SceneRendererの初期化
        // ========================================
        if (swapChain->GetMaxFramesInFlight() > FrameUseRingMaxInFlightSlots)
        {
            // 飛行中のフレームの番号が FrameUseRing の枠より多いと、別のフレームの資源を上書きする
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "飛行中のフレーム数 %u "
                             "がフレームごとの資源の枠の上限 %u を超えています",
                             swapChain->GetMaxFramesInFlight(), FrameUseRingMaxInFlightSlots);
            ReleaseInitializedResources();
            return false;
        }

        if (!m_TransientPool.Initialize(m_Device->GetResourceAllocator(),
                                        swapChain->GetMaxFramesInFlight(),
                                        ResolveTransientResourceSlotCount()))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize TransientResourcePool");
            ReleaseInitializedResources();
            return false;
        }
        NORVES_LOG_INFO("RenderingCoordinator",
                        "RG_TRANSIENT_SLOTS slots=%u frames_in_flight=%u",
                        m_TransientPool.GetSlotCount(),
                        swapChain->GetMaxFramesInFlight());

        if (!m_RenderGraph.Initialize(&m_TransientPool))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize RenderGraph");
            ReleaseInitializedResources();
            return false;
        }

        if (!m_InstanceBufferRing.Initialize(m_Device.get(), swapChain->GetMaxFramesInFlight(), 1024))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize InstanceBufferRing");
            ReleaseInitializedResources();
            return false;
        }

        if (!m_SceneRenderer.Initialize(m_Device.get(), nullptr, &m_TransientPool))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize SceneRenderer");
            ReleaseInitializedResources();
            return false;
        }

        // デフォルトパイプラインは設定しない（球体描画で代替）
        // m_SceneRenderer.SetDefaultPipeline(m_TrianglePipeline);

        // ========================================
        // 12.5. メインSceneViewのパイプライン構築
        // ========================================
        // 既定はDeferred描画パス（GBuffer→Lighting→ToneMapping）。パストレーサーは起動時の設定で
        // 明示選択し、PathTracingPassに必要な機能がそろうデバイスでだけ有効にする。
        m_MainViewRenderer = RenderingMainViewRenderer::Raster;
        if (settings.MainViewRenderer == RenderingMainViewRenderer::PathTracing)
        {
            if (PathTracingPass::IsSupported(m_Device->GetCapabilities()))
            {
                m_MainViewRenderer = RenderingMainViewRenderer::PathTracing;
            }
            else
            {
                NORVES_LOG_WARNING("RenderingCoordinator",
                                   "Path tracing was requested but the device lacks ray tracing support; using the deferred pipeline");
            }
        }
        if (m_MainViewRenderer == RenderingMainViewRenderer::PathTracing)
        {
            m_MainSceneView->SetupPathTracingPipeline(settings.PathTracingSamplesPerFrame,
                                                      settings.PathTracingTransport,
                                                      settings.PathTracingPixelSamplingMode,
                                                      settings.PathTracingDebug,
                                                      settings.PathTracingSampleBatch);
            NORVES_LOG_INFO("RenderingCoordinator", "Path tracing pipeline configured on MainSceneView");
        }
        else
        {
            m_MainSceneView->SetShadowProbeEnabled(settings.bShadowProbe);
            m_MainSceneView->SetShadowProbePointOnly(settings.bShadowProbePointOnly);
            m_MainSceneView->SetCaptureAovEnabled(settings.bCaptureAov);
            m_MainSceneView->SetShadowMethod(settings.SunShadowMethod);
            m_MainSceneView->SetPointShadowMethod(settings.PointLightShadowMethod);
            m_MainSceneView->SetVsmPoolPages(settings.VsmPoolPages);
            m_MainSceneView->SetupDeferredPipeline(&m_SceneRenderer, settings.RasterDirectBrdfMode, settings.VisibilityBuffer,
                                                      settings.SwRaster, settings.SwRasterMaxPixels);
            // 被写界深度は半透明を合成した後のSceneColorへ掛ける。カメラのピント距離が0なら働かない。
            m_MainSceneView->AddPass(Container::MakeUnique<DepthOfFieldPass>());
            // 動きぼけは被写界深度の後のSceneColorへ掛ける。シャッター時間が0（既定）なら働かない。
            m_MainSceneView->AddPass(Container::MakeUnique<MotionBlurPass>());
            NORVES_LOG_INFO("RenderingCoordinator", "Deferred pipeline configured on MainSceneView");
        }

        // ========================================
        // 13. MeshProxyはWorldから自動登録される
        // ========================================

        // ========================================
        // 14. FramePacketManagerの初期化
        // ========================================
        m_PacketManager.Initialize();

        m_FrameCaptureReadbackHelper = Container::MakeUnique<FrameCaptureReadbackHelper>();
        if (!m_FrameCaptureReadbackHelper ||
            !m_FrameCaptureReadbackHelper->Initialize(m_Device.get(), swapChain->GetMaxFramesInFlight()))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize FrameCaptureReadbackHelper");
            ReleaseInitializedResources();
            return false;
        }

        m_bInitialized = true;
        LOG_INFO("RenderingCoordinator::Initialize() - Initialization completed successfully");
        return true;
    }

    namespace
    {
        bool ContainsKey(const Container::VariableArray<uint64_t>& keys, uint64_t key)
        {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        }

        // 検査 MEGA_SCENE_CONFIRM の決まった場面: 確認・台帳の約束を、本物の型で確かめる。
        // 破れたときは detail に理由を書いて false を返す。examined に確かめた項目数を足す
        bool VerifyPacketDeltaProtocol(char* detail, size_t detailSize, uint32_t& examined)
        {
            auto fail = [&](const char* reason)
            {
                std::snprintf(detail, detailSize, "%s", reason);
                return false;
            };

            // 場面 A: RenderThread に飛ばされたパケットの差分は、確認より後の分として次のパケットに積み直される
            PacketConfirmation confirmation;
            PacketDeltaLedger ledger;
            {
                Container::VariableArray<uint64_t> applied; // RenderThread の写しに当たった鍵
                Container::VariableArray<uint64_t> carried;
                constexpr uint64_t kGeneration = 1;
                // 番号 1..6 のうち、RenderThread が適用するもの（2・3・6 は飛ばされる）。GameThread が確認を取り込むのは 3 と 6 の送信のあと
                const bool applyPattern[6] = {true, false, false, true, true, false};
                for (uint64_t serial = 1; serial <= 6; ++serial)
                {
                    ledger.CollectPending(kGeneration, carried);
                    const uint64_t newKey = serial * 10;
                    ledger.NoteSent(serial, kGeneration, &newKey, 1);
                    carried.push_back(newKey);
                    if (applyPattern[serial - 1])
                    {
                        if (confirmation.TryApply(serial, kGeneration) != PacketApplyResult::Applied)
                        {
                            return fail("適用できるはずのパケットが適用されなかった");
                        }
                        for (uint64_t key : carried)
                        {
                            if (!ContainsKey(applied, key))
                            {
                                applied.push_back(key);
                            }
                        }
                    }
                    if (serial == 3 || serial == 6)
                    {
                        ledger.NoteConfirmed(confirmation.GetConfirmedSerial(), confirmation.GetConfirmedGeneration());
                    }
                    ++examined;
                }
                if (confirmation.GetConfirmedSerial() != 5)
                {
                    return fail("確認の番号が最後に適用した番号にならない");
                }
                for (uint64_t serial = 1; serial <= 5; ++serial)
                {
                    if (!ContainsKey(applied, serial * 10))
                    {
                        return fail("確認より前の送信の変化が、写しに届いていない（飛んだパケットの差分が積み直されていない）");
                    }
                    ++examined;
                }
                ledger.CollectPending(kGeneration, carried);
                if (carried.size() != 1 || carried[0] != 60 || ledger.GetUnconfirmedSendCount() != 1)
                {
                    return fail("確認より後の送信の変化だけが、積み直しの対象に残るはず");
                }
                ++examined;
            }

            // 場面 B: シーンの世代が替わったら、古い世代の差分は拒む
            {
                const uint64_t newKey = 70;
                ledger.NoteSent(7, 2, &newKey, 1);
                Container::VariableArray<uint64_t> pendingNew;
                Container::VariableArray<uint64_t> pendingOld;
                ledger.CollectPending(2, pendingNew);
                ledger.CollectPending(1, pendingOld);
                if (pendingNew.size() != 1 || pendingNew[0] != 70 || !pendingOld.empty())
                {
                    return fail("世代が替わったあとに、古い世代の差分が台帳に残った");
                }
                if (!confirmation.CanApplyDelta(1) || confirmation.CanApplyDelta(2))
                {
                    return fail("適用前は、適用済みの世代の差分だけが当たるはず");
                }
                if (confirmation.TryApply(7, 2) != PacketApplyResult::Applied)
                {
                    return fail("新しい世代のパケットが適用されなかった");
                }
                if (confirmation.CanApplyDelta(1) || !confirmation.CanApplyDelta(2))
                {
                    return fail("世代が替わったあとに、古い世代の差分が拒まれない");
                }
                if (confirmation.TryApply(8, 1) != PacketApplyResult::StaleGeneration ||
                    confirmation.TryApply(3, 2) != PacketApplyResult::StaleSerial)
                {
                    return fail("適用済みより古い世代・番号のパケットが捨てられない");
                }
                if (confirmation.GetConfirmedSerial() != 7 || confirmation.GetConfirmedGeneration() != 2 ||
                    confirmation.GetStaleDropCount() != 2)
                {
                    return fail("捨てたパケットが確認を動かした");
                }
                ledger.NoteConfirmed(confirmation.GetConfirmedSerial(), confirmation.GetConfirmedGeneration());
                if (ledger.GetUnconfirmedSendCount() != 0)
                {
                    return fail("確認した送信が台帳に残った");
                }
                examined += 5;
            }

            // 場面 C: 適用した番号と GPU の提出番号の対応。提出が完了するまで、完了の番号は進まない
            {
                PacketConfirmation mapping;
                if (mapping.TryApply(0, 1) != PacketApplyResult::Unnumbered || mapping.GetConfirmedSerial() != 0)
                {
                    return fail("番号の無いパケットが確認を動かした");
                }
                mapping.TryApply(1, 1);
                mapping.NoteSubmitted(1, 100);
                mapping.NoteFinished(1);
                mapping.TryApply(2, 1); // 描画を見送った（提出が無い）が、適用したので確認する
                mapping.NoteFinished(2);
                if (mapping.GetConfirmedSerial() != 2 || mapping.GetCompletedSerial() != 0)
                {
                    return fail("提出が完了する前に、完了の番号が進んだ");
                }
                mapping.Retire(99, false);
                if (mapping.GetCompletedSerial() != 0)
                {
                    return fail("提出番号に届かないのに、完了の番号が進んだ");
                }
                if (mapping.GetCompletedSubmissionSerial() != 99)
                {
                    return fail("GPU の完了した提出番号が公開されない");
                }
                mapping.Retire(100, false);
                if (mapping.GetCompletedSerial() != 2)
                {
                    return fail("提出が完了したのに、提出の無いパケットまで完了の番号が進まない");
                }
                if (mapping.GetCompletedSubmissionSerial() != 100)
                {
                    return fail("完了した提出番号 100 が公開されない");
                }
                mapping.TryApply(3, 1);
                mapping.NoteSubmitted(3, 5);
                mapping.NoteFinished(3);
                mapping.Retire(4, true); // スワップチェーンの作り直しで提出番号が戻った
                if (mapping.GetCompletedSerial() != 3)
                {
                    return fail("提出番号が戻ったとき、持っていた提出が完了済みにならない");
                }
                if (mapping.GetCompletedSubmissionSerial() != 4)
                {
                    return fail("提出番号が戻ったとき、公開する完了の提出番号が GPU の値に揃わない");
                }
                examined += 8;
            }
            return true;
        }

        // 席の再利用の条件に使う描画側の進み具合（送った番号・確認・完了）を、SceneView へ写す。GameThread から呼ぶ
        void PublishSceneSlotGate(SceneView* sceneView, const FramePacketManager& manager)
        {
            if (sceneView == nullptr)
            {
                return;
            }
            const PacketConfirmation& confirmation = manager.GetConfirmation();
            Ecs::EcsSlotGate gate;
            gate.bValid = true;
            gate.SceneGeneration = manager.GetSceneGeneration();
            gate.LastSentSerial = manager.GetLastSentSerial();
            // 世代を先に、完了を最後に読む（公開は逆の順で書かれる）
            gate.ConfirmedGeneration = confirmation.GetConfirmedGeneration();
            gate.ConfirmedSerial = confirmation.GetConfirmedSerial();
            gate.CompletedSerial = confirmation.GetCompletedSerial();
            sceneView->SetSlotGate(gate);
        }

        // 検査 MEGA_SLOTS の決まった場面: 席の寿命の規則を、本物の型（EcsWorld・席の表）で確かめる。
        // 破れたときは detail に理由を書いて false を返す。examined に確かめた項目数を足す
        bool VerifyGpuSceneSlotRules(char* detail, size_t detailSize, uint32_t& examined)
        {
            auto fail = [&](const char* reason)
            {
                std::snprintf(detail, detailSize, "%s", reason);
                return false;
            };
            auto makeGate = [](uint64_t generation, uint64_t sent, uint64_t confirmedGeneration, uint64_t confirmed,
                               uint64_t completed)
            {
                Ecs::EcsSlotGate gate;
                gate.bValid = true;
                gate.SceneGeneration = generation;
                gate.LastSentSerial = sent;
                gate.ConfirmedGeneration = confirmedGeneration;
                gate.ConfirmedSerial = confirmed;
                gate.CompletedSerial = completed;
                return gate;
            };
            auto slotOf = [](Ecs::EcsWorld& world, Ecs::EcsEntity entity) -> Ecs::EcsGpuSceneSlot
            {
                const Ecs::EcsGpuSceneSlot* slot = world.GetColumnValue<Ecs::EcsGpuSceneSlot>(entity);
                return slot != nullptr ? *slot : Ecs::EcsGpuSceneSlot{0, 0};
            };

            Ecs::EcsColumnInfo lightColumns[4];
            Ecs::EcsMegaInstanceColumns(lightColumns);
            const Ecs::EcsColumnInfo objectColumns[3] = {
                Ecs::EcsRegisterColumn<Ecs::EcsObjectRef>(),
                Ecs::EcsRegisterColumn<Ecs::EcsHierarchy>(),
                Ecs::EcsRegisterColumn<Ecs::EcsGpuSceneSlot>(),
            };

            // 場面 A: 解放は理由が Destroy の削除だけ。入れ替え・チャンクの返却・列の組の移動では席が変わらず、解放されない
            {
                Ecs::EcsWorld world;
                Ecs::EcsGpuSceneSlotTable& table = world.GetGpuSceneSlots();
                table.SetAuditEnabled(true);
                table.Advance(makeGate(1, 10, 1, 10, 10));
                const Ecs::EcsArchetypeId lightArchetype =
                    world.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(lightColumns, 4));
                const Ecs::EcsArchetypeId objectArchetype =
                    world.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(objectColumns, 3));
                if (lightArchetype == Ecs::kInvalidEcsArchetype || objectArchetype == Ecs::kInvalidEcsArchetype)
                {
                    return fail("席の列を持つアーキタイプを作れなかった");
                }
                Container::VariableArray<Ecs::EcsEntity> lights;
                Container::VariableArray<Ecs::EcsEntity> objects;
                world.CreateEntities(lightArchetype, 450, &lights);
                world.CreateEntities(objectArchetype, 80, &objects);
                if (lights.size() != 450 || objects.size() != 80 || table.GetLiveCount() != 530 ||
                    world.GetArchetypeChunks(lightArchetype).size() < 2)
                {
                    return fail("行の作成で席が割り当てられない、またはチャンクをまたぐ数にならない");
                }
                Container::VariableArray<uint32_t> expected;
                for (const Ecs::EcsEntity entity : lights)
                {
                    expected.push_back(slotOf(world, entity).Value);
                }
                for (const Ecs::EcsEntity entity : objects)
                {
                    expected.push_back(slotOf(world, entity).Value);
                }
                const Ecs::EcsGpuSceneSlotAudit created = world.AuditGpuSceneSlots();
                if (created.Violations() != 0 || created.Rows != 530)
                {
                    return fail("作った直後に席の重複・食い違いがある");
                }
                examined += 2;

                // 先頭のチャンクの行を削除する: 末尾の行が入れ替わって動くが、動いた行の席は変わらない
                world.DestroyEntity(lights[10]);
                // 末尾のチャンクを丸ごと返す
                for (size_t index = 200; index < lights.size(); ++index)
                {
                    world.DestroyEntity(lights[index]);
                }
                // 列の組を変える移動: 席は変わらず、解放も割り当てもされない
                const uint64_t allocatedBefore = table.GetAllocatedCount();
                const uint64_t releasedBefore = table.GetReleasedCount();
                for (size_t index = 0; index < 10; ++index)
                {
                    world.AddColumnOf<Ecs::EcsScatterOwner>(objects[index]);
                    world.RemoveColumnOf<Ecs::EcsScatterOwner>(objects[index]);
                }
                if (table.GetAllocatedCount() != allocatedBefore || table.GetReleasedCount() != releasedBefore)
                {
                    return fail("列の組を変える移動で席が割り当てられた、または解放された");
                }
                if (table.GetReleasedCount() != 1 + (lights.size() - 200))
                {
                    return fail("解放の数が、Destroy の削除の数と一致しない");
                }
                bool bUnchanged = true;
                for (size_t index = 0; index < lights.size(); ++index)
                {
                    if (index == 10 || index >= 200)
                    {
                        continue;
                    }
                    bUnchanged = bUnchanged && slotOf(world, lights[index]).Value == expected[index];
                }
                for (size_t index = 0; index < objects.size(); ++index)
                {
                    bUnchanged = bUnchanged && slotOf(world, objects[index]).Value == expected[lights.size() + index];
                }
                if (!bUnchanged)
                {
                    return fail("入れ替え・チャンクの返却・列の組の移動で、生きている行の席が変わった");
                }
                const Ecs::EcsGpuSceneSlotAudit afterDestroy = world.AuditGpuSceneSlots();
                if (afterDestroy.Violations() != 0 || afterDestroy.Rows != 530 - 1 - (lights.size() - 200))
                {
                    return fail("削除のあとに、席の重複・解放漏れ・食い違いがある");
                }
                examined += 5;
            }

            // 場面 B: 解放した席は、削除の適用の確認と提出の完了の両方がそろうまで再利用しない。
            // 実際の呼び出しの順（同期の先頭で Advance → パケットを送る → 次のフレームの Tick で Destroy → 次の同期で Advance）で確かめる。
            // 表のゲートは前の同期の値で、その後に送ったパケットを知らないまま Destroy が来る
            {
                Ecs::EcsWorld world;
                Ecs::EcsGpuSceneSlotTable& table = world.GetGpuSceneSlots();
                table.SetAuditEnabled(true);
                const Ecs::EcsArchetypeId lightArchetype =
                    world.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(lightColumns, 4));
                Container::VariableArray<Ecs::EcsEntity> rows;
                world.CreateEntities(lightArchetype, 4, &rows);
                const uint32_t slot0 = slotOf(world, rows[0]).Value;
                const uint32_t slot1 = slotOf(world, rows[1]).Value;
                const uint32_t slot2 = slotOf(world, rows[2]).Value;
                const uint32_t slot3 = slotOf(world, rows[3]).Value;
                auto createdSlot = [&]()
                {
                    Container::VariableArray<Ecs::EcsEntity> created;
                    world.CreateEntities(lightArchetype, 1, &created);
                    rows.push_back(created[0]);
                    return slotOf(world, created[0]).Value;
                };

                // 最初のフレーム: 同期ではまだゲートが無効（パケットを送っていない）。席を含むパケット 1 を送った後、
                // 表が進み具合を取り込む前に Destroy する。運ぶパケットは 2、最後に使いうる提出は 1
                table.Advance(Ecs::EcsSlotGate{});
                world.DestroyEntity(rows[0]);
                if (table.GetPendingCount() != 1 || table.GetReadyCount() != 0 || createdSlot() == slot0)
                {
                    return fail("描画側の進み具合を取り込む前の解放で、席を直ちに再利用した");
                }
                table.Advance(makeGate(1, 1, 0, 0, 0));
                if (table.GetPendingCount() != 1 || createdSlot() == slot0)
                {
                    return fail("パケット 1 が適用・完了する前に、解放した席を再利用した");
                }
                table.Advance(makeGate(1, 2, 1, 2, 1));
                if (table.GetPendingCount() != 0 || createdSlot() != slot0)
                {
                    return fail("両方の条件がそろったのに、最初のフレームに解放した席を再利用しない");
                }
                examined += 3;

                // 表のゲートは送り終えた番号 2 のまま、席を含むパケット 3 を送ってから Destroy する。
                // 運ぶパケットは 4、最後に使いうる提出は 3（前の同期の値 2 で記録すると、下の 1 つ目の Advance で再利用してしまう）
                world.DestroyEntity(rows[1]);
                if (table.GetPendingCount() != 1 || table.GetReadyCount() != 0 || createdSlot() == slot1)
                {
                    return fail("解放した席が、条件を待つ一覧に入らない");
                }
                table.Advance(makeGate(1, 3, 1, 2, 2)); // 解放を運ぶパケットの番号がここで決まる
                table.Advance(makeGate(1, 4, 1, 3, 2)); // パケット 3 は適用されたが、解放を運ぶ 4 は未適用で、3 の提出も未完了
                if (table.GetPendingCount() != 1 || createdSlot() == slot1)
                {
                    return fail("解放を運ぶパケットの適用と、席を含むパケットの完了を待たずに再利用した");
                }
                table.Advance(makeGate(1, 4, 1, 4, 2)); // 適用は済んだが、最後に使った提出（3）が完了していない
                if (table.GetPendingCount() != 1 || createdSlot() == slot1)
                {
                    return fail("提出が完了する前に、解放した席を再利用した");
                }
                table.Advance(makeGate(1, 4, 1, 3, 3)); // 完了は済んだが、解放を運ぶパケット（4）が適用されていない
                if (table.GetPendingCount() != 1 || createdSlot() == slot1)
                {
                    return fail("削除の適用の確認の前に、解放した席を再利用した");
                }
                table.Advance(makeGate(1, 4, 1, 4, 3));
                if (table.GetPendingCount() != 0 || table.GetReadyCount() != 1 || createdSlot() != slot1)
                {
                    return fail("両方の条件がそろったのに、解放した席を再利用しない");
                }
                // シーンの世代が進めば、古い世代の確認の番号に届かなくても写しごと入れ替わっている
                world.DestroyEntity(rows[2]);
                table.Advance(makeGate(1, 4, 1, 4, 4)); // 運ぶパケットは 5、最後に使いうる提出は 4
                table.Advance(makeGate(2, 6, 2, 4, 4));
                if (table.GetPendingCount() != 0 || createdSlot() != slot2)
                {
                    return fail("世代が進んだのに、解放した席を再利用しない");
                }
                // 消費者がいない間は、解放した席を次の Advance で直ちに再利用する
                world.DestroyEntity(rows[3]);
                table.Advance(Ecs::EcsSlotGate{});
                if (table.GetPendingCount() != 0 || createdSlot() != slot3)
                {
                    return fail("消費者がいないのに、解放した席を再利用しない");
                }
                const Ecs::EcsGpuSceneSlotAudit audit = world.AuditGpuSceneSlots();
                if (audit.Violations() != 0 || table.GetPrematureReuseCount() != 0)
                {
                    return fail("再利用のあとに、席の重複・食い違い・早すぎる再利用がある");
                }
                examined += 7;
            }

            // 場面 C: 範囲の AABB は所属する全席を包み、範囲が空になったら捨てる
            {
                Ecs::EcsWorld world;
                Ecs::EcsGpuSceneSlotTable& table = world.GetGpuSceneSlots();
                table.SetAuditEnabled(true);
                table.Advance(makeGate(1, 10, 1, 10, 10));
                const Ecs::EcsArchetypeId lightArchetype =
                    world.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(lightColumns, 4));
                Container::VariableArray<Ecs::EcsEntity> rows;
                world.CreateEntities(lightArchetype, 130, &rows);
                for (size_t index = 0; index < rows.size(); ++index)
                {
                    const float x = static_cast<float>(index);
                    table.NoteBounds(slotOf(world, rows[index]), Math::Vector3(x - 1.0f, -2.0f, 0.0f),
                                     Math::Vector3(x + 1.0f, 2.0f, 3.0f));
                }
                Ecs::EcsGpuSceneSlotAudit audit;
                if (table.AuditContainment(audit) != 0 || table.GetRangeCount() != 3)
                {
                    return fail("範囲の AABB が所属する席の境界を包まない");
                }
                Math::Vector3 rangeMin;
                Math::Vector3 rangeMax;
                if (!table.GetRangeBounds(0, rangeMin, rangeMax) || rangeMin.x != -1.0f || rangeMax.x != 64.0f)
                {
                    return fail("範囲の AABB が、所属する席の境界の和にならない");
                }
                // 最初の範囲の全席を解放する。条件がそろって範囲が空になれば AABB を捨てる
                for (size_t index = 0; index < 64; ++index)
                {
                    world.DestroyEntity(rows[index]);
                }
                table.Advance(makeGate(1, 10, 1, 10, 10)); // 運ぶパケットは 11、最後に使いうる提出は 10
                table.Advance(makeGate(1, 11, 1, 11, 11));
                if (table.GetRangeBounds(0, rangeMin, rangeMax))
                {
                    return fail("空になった範囲の AABB が残っている");
                }
                Container::VariableArray<Ecs::EcsEntity> created;
                world.CreateEntities(lightArchetype, 1, &created);
                const Ecs::EcsGpuSceneSlot reused = slotOf(world, created[0]);
                table.NoteBounds(reused, Math::Vector3(999.0f, 0.0f, 0.0f), Math::Vector3(1001.0f, 1.0f, 1.0f));
                if (reused.Range != 0 || !table.GetRangeBounds(0, rangeMin, rangeMax) || rangeMin.x != 999.0f ||
                    table.AuditContainment(audit) != 0)
                {
                    return fail("空になった範囲を再利用したとき、古い境界が残った、または新しい席を包まない");
                }
                examined += 4;
            }

            // 場面 D: 突き合わせが、規則の破れを見つける（重複・未割り当て・表との食い違い・Destroy 以外での列の除去）
            {
                Ecs::EcsWorld world;
                Ecs::EcsGpuSceneSlotTable& table = world.GetGpuSceneSlots();
                table.Advance(makeGate(1, 10, 1, 10, 10));
                const Ecs::EcsArchetypeId lightArchetype =
                    world.GetOrCreateArchetype(Container::Span<const Ecs::EcsColumnInfo>(lightColumns, 4));
                Container::VariableArray<Ecs::EcsEntity> rows;
                world.CreateEntities(lightArchetype, 3, &rows);
                if (world.AuditGpuSceneSlots().Violations() != 0)
                {
                    return fail("正しい状態なのに、突き合わせが破れを報告した");
                }
                const Ecs::EcsGpuSceneSlot original1 = slotOf(world, rows[1]);
                const Ecs::EcsGpuSceneSlot original2 = slotOf(world, rows[2]);
                world.WriteColumnValueOf(rows[1], slotOf(world, rows[0]));
                const Ecs::EcsGpuSceneSlotAudit duplicate = world.AuditGpuSceneSlots();
                if (duplicate.DuplicateSlots != 1 || duplicate.OrphanLiveSlots != 1)
                {
                    return fail("席の重複を見つけられない");
                }
                world.WriteColumnValueOf(rows[1], original1);
                world.WriteColumnValueOf(rows[2], Ecs::EcsGpuSceneSlot{0, 0});
                if (world.AuditGpuSceneSlots().UnassignedRows != 1)
                {
                    return fail("席が未割り当ての行を見つけられない");
                }
                world.WriteColumnValueOf(rows[2], Ecs::EcsGpuSceneSlot{9999, 0});
                if (world.AuditGpuSceneSlots().UnknownSlots != 1)
                {
                    return fail("表に無い席を持つ行を見つけられない");
                }
                world.WriteColumnValueOf(rows[2], original2);
                if (world.AuditGpuSceneSlots().Violations() != 0)
                {
                    return fail("値を戻したのに、突き合わせが破れを報告し続けた");
                }
                world.RemoveColumnOf<Ecs::EcsGpuSceneSlot>(rows[0]);
                const Ecs::EcsGpuSceneSlotAudit removed = world.AuditGpuSceneSlots();
                if (removed.RuleViolations != 1 || removed.OrphanLiveSlots != 1)
                {
                    return fail("Destroy 以外の理由で席の列を外したことを見つけられない");
                }
                examined += 9;
            }
            return true;
        }

        // 検査 MEGA_SLOTS: 席の寿命の規則（最初のパケットと 256 パケットごと）。実際の World の席は World が毎フレーム突き合わせる。
        // GameThread から呼ぶ
        void RunGpuSceneSlotRulesCheck(uint64_t sendSerial)
        {
            if (Engine::GEngine == nullptr || (sendSerial != 1 && sendSerial % 256 != 0))
            {
                return;
            }
            NorvesLib::Debug::EngineChecks& checks = Engine::GEngine->GetEngineChecks();
            if (!checks.IsEnabled("MEGA_SLOTS"))
            {
                return;
            }
            char detail[240] = {};
            uint32_t examined = 0;
            const bool bPass = VerifyGpuSceneSlotRules(detail, sizeof(detail), examined);
            if (!bPass && checks.GetFailCount() <= 8)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "MEGA_SLOTS: 席の規則が破れた: %s", detail);
            }
            checks.Report("MEGA_SLOTS", bPass, examined);
        }

        // 検査 MEGA_SCENE_CONFIRM: 確認の公開値の単調さ（毎回）と、確認・台帳の約束（最初の回と 256 回ごと）を確かめる。
        // GameThread から呼ぶ
        void RunPacketConfirmCheck(FramePacketManager& manager)
        {
            if (Engine::GEngine == nullptr)
            {
                return;
            }
            NorvesLib::Debug::EngineChecks& checks = Engine::GEngine->GetEngineChecks();
            if (!checks.IsEnabled("MEGA_SCENE_CONFIRM"))
            {
                return;
            }

            char detail[240] = {};
            PacketConfirmationMonitor& monitor = manager.GetConfirmationMonitor();
            bool bPass = monitor.Observe(manager.GetConfirmation(), manager.GetLastSentSerial(),
                                         manager.GetSceneGeneration(), detail, sizeof(detail));
            uint32_t examined = 1;
            const uint64_t calls = monitor.GetObserveCount();
            if (bPass && (calls == 1 || calls % 256 == 0))
            {
                bPass = VerifyPacketDeltaProtocol(detail, sizeof(detail), examined);
            }
            if (!bPass && checks.GetFailCount() <= 8)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "MEGA_SCENE_CONFIRM: 約束が破れた: %s", detail);
            }
            if (calls % 120 == 0)
            {
                const PacketConfirmation& confirmation = manager.GetConfirmation();
                NORVES_LOG_INFO("RenderingCoordinator",
                                "MEGA_SCENE_CONFIRM sent=%llu confirmed=%llu completed=%llu generation=%llu stale=%llu "
                                "submission=%llu",
                                static_cast<unsigned long long>(manager.GetLastSentSerial()),
                                static_cast<unsigned long long>(confirmation.GetConfirmedSerial()),
                                static_cast<unsigned long long>(confirmation.GetCompletedSerial()),
                                static_cast<unsigned long long>(manager.GetSceneGeneration()),
                                static_cast<unsigned long long>(confirmation.GetStaleDropCount()),
                                static_cast<unsigned long long>(confirmation.GetCompletedSubmissionSerial()));
            }
            // fail は毎回、pass は最初の回と 64 回ごとに出す
            if (!bPass || calls == 1 || calls % 64 == 0)
            {
                checks.Report("MEGA_SCENE_CONFIRM", bPass, examined);
            }
        }

        // 検査 MEGA_SCENE_DELTA の決まった場面: 送り手と写しを、本物の型で確かめる。
        // GameThread の配列（SceneView と同じ追加・更新・外して末尾を入れる並び）の変化を、飛ばされるパケット・世代の替わりを
        // 挟みながら運び、適用するたびに写しが配列と一致することを見る。破れたときは detail に理由を書いて false を返す
        bool VerifyMegaDeltaProtocol(char* detail, size_t detailSize, uint32_t& examined)
        {
            auto fail = [&](const char* reason)
            {
                std::snprintf(detail, detailSize, "%s", reason);
                return false;
            };

            Container::VariableArray<MegaGeometryProxy> source;
            Container::VariableArray<uint32_t> changedRaw;
            uint64_t seed = 0x9E3779B97F4A7C15ull;
            const auto nextRandom = [&seed]()
            {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<uint32_t>(seed >> 33);
            };
            uint64_t nextObject = 1;
            const auto makeProxy = [&](uint64_t objectId)
            {
                MegaGeometryProxy proxy;
                proxy.ObjectId = objectId;
                proxy.ComponentId = objectId * 10u;
                proxy.MegaMeshHandle.Id = 1u + objectId % 5u;
                proxy.WorldTransform.values[12] = static_cast<float>(objectId);
                proxy.PreviousWorldTransform = proxy.WorldTransform;
                proxy.SceneSlot = static_cast<uint32_t>(objectId);
                return proxy;
            };
            const auto addProxy = [&]()
            {
                source.push_back(makeProxy(nextObject++));
                changedRaw.push_back(static_cast<uint32_t>(source.size() - 1));
            };
            const auto updateProxy = [&](uint32_t index)
            {
                source[index].PreviousWorldTransform = source[index].WorldTransform;
                source[index].WorldTransform.values[12] += 1.0f + static_cast<float>(nextRandom() % 7u);
                changedRaw.push_back(index);
            };
            const auto removeProxy = [&](uint32_t index)
            {
                const uint32_t last = static_cast<uint32_t>(source.size() - 1);
                if (index != last)
                {
                    source[index] = source[last];
                    changedRaw.push_back(index);
                }
                source.pop_back();
            };
            const auto sameArrays = [](const Container::VariableArray<MegaGeometryProxy>& lhs,
                                       const Container::VariableArray<MegaGeometryProxy>& rhs)
            {
                if (lhs.size() != rhs.size())
                {
                    return false;
                }
                // 前の変換は写しが描いた履歴から決めるので比べない（HashMegaGeometryContent と同じ）
                const auto contentHash = [](MegaGeometryProxy proxy)
                {
                    proxy.PreviousWorldTransform = proxy.WorldTransform;
                    return HashProxy(proxy);
                };
                for (size_t index = 0; index < lhs.size(); ++index)
                {
                    if (contentHash(lhs[index]) != contentHash(rhs[index]) || lhs[index].SceneSlot != rhs[index].SceneSlot)
                    {
                        return false;
                    }
                }
                return true;
            };

            PacketConfirmation confirmation;
            MegaGeometryDeltaSender sender;
            MegaGeometryMirror mirror;
            uint64_t generation = 1;
            uint64_t serial = 0;
            struct InFlight
            {
                uint64_t Serial = 0;
                uint64_t Generation = 0;
                MegaSceneDelta Delta;
            };
            Container::VariableArray<InFlight> inFlight;
            uint32_t baselines = 0;
            bool bBaselineSkippedOnce = false;

            // 適用するステップ（それ以外は RenderThread に飛ばされる）。世代の替わり（ステップ 30）の基準は一度飛ばす
            for (uint32_t step = 0; step < 72; ++step)
            {
                if (step == 30)
                {
                    // シーンを入れ替える: 世代が進み、配列を作り直す。この基準は RenderThread に飛ばされる
                    bBaselineSkippedOnce = true;
                    source.clear();
                    changedRaw.clear();
                    ++generation;
                    for (uint32_t add = 0; add < 12; ++add)
                    {
                        addProxy();
                    }
                }
                else
                {
                    const uint32_t adds = step < 4 ? 8u : nextRandom() % 3u;
                    for (uint32_t add = 0; add < adds; ++add)
                    {
                        addProxy();
                    }
                    if (!source.empty())
                    {
                        const uint32_t updates = nextRandom() % 4u;
                        for (uint32_t update = 0; update < updates; ++update)
                        {
                            updateProxy(nextRandom() % static_cast<uint32_t>(source.size()));
                        }
                        if (nextRandom() % 3u == 0u)
                        {
                            removeProxy(nextRandom() % static_cast<uint32_t>(source.size()));
                        }
                    }
                }

                Container::VariableArray<uint32_t> taken = changedRaw;
                changedRaw.clear();
                std::sort(taken.begin(), taken.end());
                taken.erase(std::unique(taken.begin(), taken.end()), taken.end());

                InFlight flight;
                flight.Generation = generation;
                const uint64_t confirmedSerial = confirmation.GetConfirmedSerial();
                const uint64_t confirmedGeneration = confirmation.GetConfirmedGeneration();
                sender.Build(source, taken, confirmedSerial, confirmedGeneration, generation, flight.Delta);
                ++serial;
                flight.Serial = serial;
                sender.NoteSent(serial, generation);
                if (flight.Delta.bBaseline != (confirmedGeneration != generation))
                {
                    return fail("適用された世代が現在の世代でないあいだは基準を送り、それ以外は差分を送るはず");
                }
                baselines += flight.Delta.bBaseline ? 1u : 0u;
                if (step >= 30 && step <= 31 && !flight.Delta.bBaseline)
                {
                    return fail("世代が替わった直後のパケットは基準のはず");
                }
                inFlight.push_back(std::move(flight));
                ++examined;

                // RenderThread は、適用すると決めたステップで最新のパケットだけを適用する（古いパケットは捨てられる）。
                // 3 の倍数のステップでは、適用の前に 1 つ古いパケットを遅れて適用して、重なった当たり方も確かめる
                const bool bApply = (step % 4u == 3u) || (step % 11u == 5u) || step == 31 || step == 33;
                if (bApply)
                {
                    if (step % 3u == 0u && inFlight.size() >= 2)
                    {
                        const InFlight& older = inFlight[inFlight.size() - 2];
                        if (confirmation.TryApply(older.Serial, older.Generation) == PacketApplyResult::Applied)
                        {
                            if (!mirror.Apply(older.Delta, older.Generation))
                            {
                                return fail("遅れて適用した差分を当てられなかった");
                            }
                        }
                    }
                    const InFlight& newest = inFlight.back();
                    if (confirmation.TryApply(newest.Serial, newest.Generation) != PacketApplyResult::Applied)
                    {
                        return fail("適用できるはずのパケットが適用されなかった");
                    }
                    if (!mirror.Apply(newest.Delta, newest.Generation))
                    {
                        return fail("最新のパケットの差分を当てられなかった");
                    }
                    if (!sameArrays(mirror.GetProxies(), source))
                    {
                        return fail("差分を当てた写しが、GameThread の配列と一致しない");
                    }
                    // 席から proxy を引ける（席の位置の表が、配列の全部の席を正しい位置で指す）
                    for (uint32_t index = 0; index < static_cast<uint32_t>(source.size()); ++index)
                    {
                        uint32_t position = 0;
                        if (!mirror.TryGetPositionBySlot(source[index].SceneSlot, position) || position != index)
                        {
                            return fail("席の位置の表が、配列の位置を指していない");
                        }
                    }
                    if (newest.Delta.bBaseline)
                    {
                        if (!mirror.AreAllChanged())
                        {
                            return fail("基準を当てた直後は、全部の席が変わったはず");
                        }
                    }
                    else if (!mirror.AreAllChanged())
                    {
                        // 変わった席には、このパケットが運んだ proxy の席が全部入る
                        // （全部が変わった扱いのあいだは、どの席も変わった席として扱われる）
                        for (uint32_t index : newest.Delta.Indices)
                        {
                            const uint32_t slot = source[index].SceneSlot;
                            if (!std::binary_search(mirror.GetChangedSlots().begin(), mirror.GetChangedSlots().end(), slot))
                            {
                                return fail("運んだ proxy の席が、変わった席に入っていない");
                            }
                        }
                    }
                    // 検査用の消費者が取り込んだ地点。次の適用から、変わった席を数え直す
                    mirror.NoteDrawn(false);
                    inFlight.clear();
                    ++examined;
                }
            }
            if (!bBaselineSkippedOnce || baselines < 2)
            {
                return fail("基準を飛ばされても、次のパケットが基準を送り直す場面が通らなかった");
            }

            // 世代が替わったあとに、古い世代の差分は写しへ当たらない
            {
                MegaSceneDelta staleDelta;
                staleDelta.bActive = true;
                staleDelta.Size = 1;
                staleDelta.Indices.push_back(0);
                staleDelta.Proxies.push_back(makeProxy(9999));
                const size_t sizeBefore = mirror.GetProxies().size();
                if (mirror.Apply(staleDelta, generation - 1) || mirror.GetProxies().size() != sizeBefore ||
                    mirror.GetRejectedCount() != 1)
                {
                    return fail("古い世代の差分が写しへ当たった");
                }
                ++examined;
            }

            // 描画を見送った差分を挟んでも、前の変換は最後に描いたフレームの変換へ付け替わる（TAA）。
            // 当てただけで描かなかった差分は、描いた変換の履歴を動かさない
            {
                constexpr uint64_t taaGeneration = 7;
                const auto makeDelta = [](bool bBaseline, uint32_t size)
                {
                    MegaSceneDelta delta;
                    delta.bActive = true;
                    delta.bBaseline = bBaseline;
                    delta.Size = size;
                    return delta;
                };
                const auto pushEntry = [&](MegaSceneDelta& delta, uint32_t index, uint64_t objectId, float x, float previousX)
                {
                    MegaGeometryProxy proxy = makeProxy(objectId);
                    proxy.WorldTransform.values[12] = x;
                    proxy.PreviousWorldTransform.values[12] = previousX;
                    delta.Indices.push_back(index);
                    delta.Proxies.push_back(proxy);
                };
                const auto previousX = [](const MegaGeometryMirror& target, uint32_t index)
                {
                    return target.GetProxies()[index].PreviousWorldTransform.values[12];
                };
                MegaGeometryMirror taa;
                constexpr uint64_t objectA = 100, objectB = 101, objectC = 102;

                // フレーム 10: 基準（A=0, B=50）を当てて描く（最初に描くので付け替えない）
                MegaSceneDelta delta = makeDelta(true, 2);
                pushEntry(delta, 0, objectA, 0.0f, 0.0f);
                pushEntry(delta, 1, objectB, 50.0f, 50.0f);
                if (!taa.Apply(delta, taaGeneration))
                {
                    return fail("TAA の場面: 基準を当てられなかった");
                }
                taa.NoteDrawn(false);

                // フレーム 11: A=10 を当てるが、描かない
                delta = makeDelta(false, 2);
                pushEntry(delta, 0, objectA, 10.0f, 0.0f);
                taa.Apply(delta, taaGeneration);

                // フレーム 12: A=20 と新しい C=7 を当てて描く（フレームが飛んだので付け替える）
                delta = makeDelta(false, 3);
                pushEntry(delta, 0, objectA, 20.0f, 10.0f);
                pushEntry(delta, 2, objectC, 7.0f, 7.0f);
                taa.Apply(delta, taaGeneration);
                taa.NoteDrawn(true);
                if (previousX(taa, 0) != 0.0f)
                {
                    return fail("TAA の場面: 描かなかった差分を挟むと、前の変換が最後に描いたフレームのものにならない");
                }
                if (previousX(taa, 1) != 50.0f || previousX(taa, 2) != 7.0f)
                {
                    return fail("TAA の場面: 変わっていない物体・新しい物体の前の変換を動かした");
                }

                // フレーム 13: 続けて描く（飛ばない）。前の変換は 12 で描いた 20（GameThread の値と同じ）
                delta = makeDelta(false, 3);
                pushEntry(delta, 0, objectA, 30.0f, 20.0f);
                taa.Apply(delta, taaGeneration);
                taa.NoteDrawn(false);
                if (previousX(taa, 0) != 20.0f)
                {
                    return fail("TAA の場面: 続けて描くフレームの前の変換を動かした");
                }

                // フレーム 14: A を外して末尾の C を A の位置へ入れる。描かない
                delta = makeDelta(false, 2);
                pushEntry(delta, 0, objectC, 9.0f, 7.0f);
                taa.Apply(delta, taaGeneration);

                // フレーム 15: C=12 を当てて描く（飛んだので、C の前の変換はフレーム 12 で描いた 7）
                delta = makeDelta(false, 2);
                pushEntry(delta, 0, objectC, 12.0f, 9.0f);
                taa.Apply(delta, taaGeneration);
                taa.NoteDrawn(true);
                if (previousX(taa, 0) != 7.0f)
                {
                    return fail("TAA の場面: 位置を移った物体の前の変換が、最後に描いたフレームのものにならない");
                }
                uint32_t position = 0;
                if (taa.FindBySlot(static_cast<uint32_t>(objectA)) != nullptr ||
                    !taa.TryGetPositionBySlot(static_cast<uint32_t>(objectC), position) || position != 0)
                {
                    return fail("TAA の場面: 外れた物体の席が残っている、または移った物体の席を引けない");
                }

                // フレーム 17: A を戻して描く（フレーム 15 の時点で外れているので、最後に描いたフレームに無い物体）
                delta = makeDelta(false, 3);
                pushEntry(delta, 2, objectA, 100.0f, 100.0f);
                taa.Apply(delta, taaGeneration);
                taa.NoteDrawn(true);
                if (previousX(taa, 2) != 100.0f)
                {
                    return fail("TAA の場面: 最後に描いたフレームに無かった物体の前の変換を付け替えた");
                }
                // フレーム 18: C=20 を当てて続けて描く（GameThread の前の変換は 12）。前の変換は 15 で描いた 12
                delta = makeDelta(false, 3);
                pushEntry(delta, 0, objectC, 20.0f, 12.0f);
                taa.Apply(delta, taaGeneration);
                taa.NoteDrawn(false);
                if (previousX(taa, 0) != 12.0f)
                {
                    return fail("履歴の場面: 続けて描くフレームの前の変換が、最後に描いたフレームのものにならない");
                }
                // フレーム 20: C は止まったが、止まった印（前の変換を揃える差分）は破棄されたパケットにあり、空の差分で描く。
                // 前の変換は 18 で描いた 20 に揃い（速度 0）、その席を消費者へ知らせる
                const uint64_t revisionBeforeSettle = taa.GetRevision();
                taa.Apply(makeDelta(false, 3), taaGeneration);
                taa.NoteDrawn(true);
                if (previousX(taa, 0) != 20.0f || taa.GetRevision() == revisionBeforeSettle ||
                    !std::binary_search(taa.GetChangedSlots().begin(), taa.GetChangedSlots().end(),
                                        static_cast<uint32_t>(objectC)))
                {
                    return fail("履歴の場面: 動いて止まった物体の前の変換が、描いた基準で 1 回だけ揃わない");
                }
                // フレーム 21: 空の差分で描く。揃った物体はもう変わった席にならない
                taa.Apply(makeDelta(false, 3), taaGeneration);
                taa.NoteDrawn(false);
                if (previousX(taa, 0) != 20.0f || !taa.GetChangedSlots().empty() || taa.GetUnsettledCount() != 0)
                {
                    return fail("履歴の場面: 揃った物体の前の変換を、続くフレームでまた動かした");
                }
                examined += 6;
            }
            return true;
        }

        // 検査 MEGA_SCENE_DELTA: 本物の写しの hash の照合結果（RenderThread が公開）と、決まった場面（最初の回と 256 回ごと）を確かめる。
        // GameThread から呼ぶ
        void RunMegaDeltaCheck(const MegaGeometryMirror& mirror,
                               uint64_t sentCount,
                               uint64_t& reportedChecked,
                               uint64_t& reportedMismatched)
        {
            if (Engine::GEngine == nullptr)
            {
                return;
            }
            NorvesLib::Debug::EngineChecks& checks = Engine::GEngine->GetEngineChecks();
            if (!checks.IsEnabled("MEGA_SCENE_DELTA"))
            {
                return;
            }
            char detail[240] = {};
            bool bPass = true;
            uint32_t examined = 0;
            if (sentCount == 1 || sentCount % 256 == 0)
            {
                bPass = VerifyMegaDeltaProtocol(detail, sizeof(detail), examined);
            }
            const uint64_t checked = mirror.GetVerifyCheckedCount();
            const uint64_t mismatched = mirror.GetVerifyMismatchedCount();
            if (mismatched > reportedMismatched)
            {
                bPass = false;
                std::snprintf(detail, sizeof(detail), "差分を当てた写し・差分で直した消費者の結果が、全量と一致しない: mismatched=%llu checked=%llu",
                              static_cast<unsigned long long>(mismatched), static_cast<unsigned long long>(checked));
            }
            if (!bPass && checks.GetFailCount() <= 8)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "MEGA_SCENE_DELTA: 約束が破れた: %s", detail);
            }
            // fail は毎回、pass は照合が進んだときと決まった場面を確かめたときに出す
            if (!bPass || checked > reportedChecked || examined > 0)
            {
                checks.Report("MEGA_SCENE_DELTA", bPass,
                              std::max<uint32_t>(examined, static_cast<uint32_t>(checked - reportedChecked)));
            }
            reportedChecked = checked;
            reportedMismatched = mismatched;
        }

        /**
         * @brief 検査 PACKET_DELTA の決まった場面: proxy 配列の差分の送り手と写し、構成の hash の集合
         *
         * 追加・更新・外し（末尾の入れ替え）を重ね、RenderThread が飛ばす・遅れて古いパケットを当てる・世代が替わった基準を
         * 飛ばす、の順を作る。当てるたびに写しが GameThread の配列と一致し、変わった位置だけで更新した集合の hash が
         * 全部を数え直した値と一致すること、古い世代の差分が写しへ当たらないことを確かめる。
         */
        bool VerifyProxyDeltaProtocol(char* detail, size_t detailSize, uint32_t& examined)
        {
            auto fail = [&](const char* reason)
            {
                std::snprintf(detail, detailSize, "%s", reason);
                return false;
            };

            Container::VariableArray<LightProxy> source;
            Container::VariableArray<uint32_t> changedRaw;
            uint64_t seed = 0xD1B54A32D192ED03ull;
            const auto nextRandom = [&seed]()
            {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<uint32_t>(seed >> 33);
            };
            uint64_t nextLight = 1;
            const auto addProxy = [&]()
            {
                LightProxy light;
                light.LightId = nextLight;
                light.PositionX = static_cast<float>(nextLight);
                ++nextLight;
                source.push_back(light);
                changedRaw.push_back(static_cast<uint32_t>(source.size() - 1));
            };
            const auto updateProxy = [&](uint32_t index)
            {
                source[index].PositionX += 1.0f + static_cast<float>(nextRandom() % 7u);
                changedRaw.push_back(index);
            };
            // SceneView と同じ外し方: 末尾を外した位置へ入れ、両方の位置を書いた位置にする
            const auto removeProxy = [&](uint32_t index)
            {
                const uint32_t last = static_cast<uint32_t>(source.size() - 1);
                if (index != last)
                {
                    source[index] = source[last];
                    changedRaw.push_back(index);
                }
                changedRaw.push_back(last);
                source.pop_back();
            };
            const auto mixedAt = [&](size_t index) { return MixRevisionValue(HashProxy(source[index])); };

            PacketConfirmation confirmation;
            ProxyArrayDeltaSender<LightProxy> sender;
            ProxyArrayMirror<LightProxy> mirror;
            ProxyRevisionSet revision;
            uint64_t generation = 1;
            uint64_t revisionGeneration = 0;
            uint64_t serial = 0;
            struct InFlight
            {
                uint64_t Serial = 0;
                uint64_t Generation = 0;
                ProxyArrayDelta<LightProxy> Delta;
                Container::VariableArray<LightProxy> Baseline;
            };
            Container::VariableArray<InFlight> inFlight;
            uint32_t baselines = 0;

            for (uint32_t step = 0; step < 72; ++step)
            {
                if (step == 30)
                {
                    // シーンを入れ替える: 世代が進み、配列を作り直す。この基準は RenderThread に飛ばされる
                    source.clear();
                    changedRaw.clear();
                    ++generation;
                    for (uint32_t add = 0; add < 12; ++add)
                    {
                        addProxy();
                    }
                }
                else
                {
                    const uint32_t adds = step < 4 ? 8u : nextRandom() % 3u;
                    for (uint32_t add = 0; add < adds; ++add)
                    {
                        addProxy();
                    }
                    if (!source.empty())
                    {
                        const uint32_t updates = nextRandom() % 4u;
                        for (uint32_t update = 0; update < updates; ++update)
                        {
                            updateProxy(nextRandom() % static_cast<uint32_t>(source.size()));
                        }
                        if (nextRandom() % 3u == 0u)
                        {
                            removeProxy(nextRandom() % static_cast<uint32_t>(source.size()));
                        }
                    }
                }

                Container::VariableArray<uint32_t> taken = changedRaw;
                changedRaw.clear();
                std::sort(taken.begin(), taken.end());
                taken.erase(std::unique(taken.begin(), taken.end()), taken.end());

                // 構成の hash の集合: 世代が替わったら作り直し、それ以外は書いた位置だけで更新する
                revision.Update(source.size(), taken, revisionGeneration != generation, mixedAt);
                revisionGeneration = generation;
                if (!MatchesRevisionSet(revision, source, [](const LightProxy& light) { return HashProxy(light); }))
                {
                    return fail("書いた位置だけで更新した構成の hash の集合が、全部を数え直した値と一致しない");
                }

                InFlight flight;
                flight.Generation = generation;
                const uint64_t confirmedSerial = confirmation.GetConfirmedSerial();
                const uint64_t confirmedGeneration = confirmation.GetConfirmedGeneration();
                sender.Build(source, taken, confirmedSerial, confirmedGeneration, generation, flight.Delta, flight.Baseline);
                ++serial;
                flight.Serial = serial;
                sender.NoteSent(serial, generation);
                if (flight.Delta.bBaseline != (confirmedGeneration != generation))
                {
                    return fail("適用された世代が現在の世代でないあいだは基準を送り、それ以外は差分を送るはず");
                }
                if (!flight.Delta.bBaseline && !flight.Baseline.empty())
                {
                    return fail("基準でない差分のパケットが全量の配列を持った");
                }
                baselines += flight.Delta.bBaseline ? 1u : 0u;
                inFlight.push_back(std::move(flight));
                ++examined;

                // RenderThread は、適用すると決めたステップで最新のパケットだけを適用する（ステップ 30 の基準は飛ばす）。
                // 3 の倍数のステップでは、適用の前に 1 つ古いパケットを遅れて適用して、重なった当たり方も確かめる
                const bool bApply = (step % 4u == 3u) || (step % 11u == 5u) || step == 31 || step == 33;
                if (bApply)
                {
                    if (step % 3u == 0u && inFlight.size() >= 2)
                    {
                        const InFlight& older = inFlight[inFlight.size() - 2];
                        if (confirmation.TryApply(older.Serial, older.Generation) == PacketApplyResult::Applied &&
                            !mirror.Apply(older.Delta, older.Generation, older.Baseline))
                        {
                            return fail("遅れて適用した差分を当てられなかった");
                        }
                    }
                    const InFlight& newest = inFlight.back();
                    if (confirmation.TryApply(newest.Serial, newest.Generation) != PacketApplyResult::Applied)
                    {
                        return fail("適用できるはずのパケットが適用されなかった");
                    }
                    if (!mirror.Apply(newest.Delta, newest.Generation, newest.Baseline))
                    {
                        return fail("最新のパケットの差分を当てられなかった");
                    }
                    if (mirror.GetProxies().size() != source.size() ||
                        HashProxyArrayContent(mirror.GetProxies()) != HashProxyArrayContent(source))
                    {
                        return fail("差分を当てた写しが、GameThread の配列と一致しない");
                    }
                    inFlight.clear();
                    ++examined;
                }
            }
            if (baselines < 2)
            {
                return fail("世代が替わったあとに基準を送り直す場面が通らなかった");
            }

            // 世代が替わったあとに、古い世代の差分は写しへ当たらない
            ProxyArrayDelta<LightProxy> staleDelta;
            staleDelta.bActive = true;
            staleDelta.Size = 1;
            staleDelta.Indices.push_back(0);
            staleDelta.Proxies.push_back(LightProxy{});
            const Container::VariableArray<LightProxy> noBaseline;
            const size_t sizeBefore = mirror.GetProxies().size();
            if (mirror.Apply(staleDelta, generation - 1, noBaseline) || mirror.GetProxies().size() != sizeBefore ||
                mirror.GetRejectedCount() != 1)
            {
                return fail("古い世代の差分が写しへ当たった");
            }
            ++examined;
            return true;
        }

        // 検査 PACKET_DELTA: Mesh・Skinned・Light の写しの照合（RenderThread が公開）と、構成の hash の集合の照合
        // （GameThread）と、決まった場面（最初の回と 256 回ごと）を確かめる。GameThread から呼ぶ
        void RunPacketDeltaCheck(uint64_t sentCount,
                                 uint64_t checked,
                                 uint64_t mismatched,
                                 uint64_t& reportedChecked,
                                 uint64_t& reportedMismatched)
        {
            if (Engine::GEngine == nullptr)
            {
                return;
            }
            NorvesLib::Debug::EngineChecks& checks = Engine::GEngine->GetEngineChecks();
            if (!checks.IsEnabled("PACKET_DELTA"))
            {
                return;
            }
            char detail[240] = {};
            bool bPass = true;
            uint32_t examined = 0;
            if (sentCount == 1 || sentCount % 256 == 0)
            {
                bPass = VerifyProxyDeltaProtocol(detail, sizeof(detail), examined);
            }
            if (mismatched > reportedMismatched)
            {
                bPass = false;
                std::snprintf(detail, sizeof(detail),
                              "差分を当てた写し・増分で更新した構成の hash が、全量と一致しない: mismatched=%llu checked=%llu",
                              static_cast<unsigned long long>(mismatched), static_cast<unsigned long long>(checked));
            }
            if (!bPass && checks.GetFailCount() <= 8)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "PACKET_DELTA: 約束が破れた: %s", detail);
            }
            // fail は毎回、pass は照合が進んだときと決まった場面を確かめたときに出す
            if (!bPass || checked > reportedChecked || examined > 0)
            {
                checks.Report("PACKET_DELTA", bPass,
                              std::max<uint32_t>(examined, static_cast<uint32_t>(checked - reportedChecked)));
            }
            reportedChecked = checked;
            reportedMismatched = mismatched;
        }

        /** @brief Mesh・Skinned・Light の差分を写しへ当て、検査のときは送った時点の配列と照合する（RenderThread） */
        template<typename TProxy>
        void ApplyProxyArrayDelta(ProxyArrayMirror<TProxy>& mirror,
                                  const ProxyArrayDelta<TProxy>& delta,
                                  const FramePacket& packet,
                                  const Container::VariableArray<TProxy>& baseline,
                                  const char* kind)
        {
            if (!delta.bActive)
            {
                return;
            }
            if (!mirror.Apply(delta, packet.SceneGeneration, baseline))
            {
                NORVES_LOG_ERROR("RenderingCoordinator",
                                 "%s の差分を写しへ当てられません: serial=%llu generation=%llu mirror_generation=%llu",
                                 kind,
                                 static_cast<unsigned long long>(packet.SendSerial),
                                 static_cast<unsigned long long>(packet.SceneGeneration),
                                 static_cast<unsigned long long>(mirror.GetGeneration()));
            }
            mirror.Verify(delta);
        }
    } // namespace

    void RenderingCoordinator::ConfigureFrameUploadArena()
    {
        if (!m_Device)
        {
            return;
        }
        // ページはデバイスで作り、整列と範囲の上限はデバイスの能力から決める
        m_FrameUploadPageSource.SetDevice(m_Device);
        const uint64_t pageBytes = ResolveFrameUploadPageBytes();
        m_FrameResourceOwners.ConfigureUpload(&m_FrameUploadPageSource, pageBytes);
        const FrameUploadLimits limits = m_FrameUploadPageSource.GetLimits();
        NORVES_LOG_INFO("FrameResource",
                        "FRAME_RESOURCE_UPLOAD_CONFIG page_bytes=%llu constants_alignment=%llu storage_alignment=%llu "
                        "max_constants=%llu max_storage=%llu",
                        static_cast<unsigned long long>(pageBytes),
                        static_cast<unsigned long long>(limits.ConstantsAlignment),
                        static_cast<unsigned long long>(limits.StorageAlignment),
                        static_cast<unsigned long long>(limits.MaxConstantsSize),
                        static_cast<unsigned long long>(limits.MaxStorageSize));

        // 記述子の pool も飛行中のスロットごとに作る（借りるのは共有のレイアウトから）
        const RHI::FrameDescriptorPoolCapacity descriptorCapacity;
        const bool bDescriptorsConfigured = m_FrameResourceOwners.ConfigureDescriptors(m_Device.get(), descriptorCapacity);
        // 読み戻し先（ホストが読めるバッファ）もデバイスで作る
        m_FrameResourceOwners.ConfigureReadback(m_Device.get());
        NORVES_LOG_INFO("FrameResource",
                        "FRAME_RESOURCE_DESCRIPTOR_CONFIG configured=%d max_sets=%u uniform=%u sampled=%u sampler=%u "
                        "storage_buffer=%u storage_image=%u combined=%u acceleration=%u",
                        bDescriptorsConfigured ? 1 : 0,
                        descriptorCapacity.MaxSets,
                        descriptorCapacity.UniformBuffers,
                        descriptorCapacity.SampledImages,
                        descriptorCapacity.Samplers,
                        descriptorCapacity.StorageBuffers,
                        descriptorCapacity.StorageImages,
                        descriptorCapacity.CombinedImageSamplers,
                        descriptorCapacity.AccelerationStructures);
    }

    void RenderingCoordinator::AdvanceCompletedRenderFrameSerial(uint64_t completedSubmissionSerial)
    {
        // スワップチェーンを作り直すと提出 serial の数え直しで値が戻りうる（作り直す前に GPU は待ち終えている）。
        // 戻ったときは、持っていた提出がすべて完了済みとして扱う
        const bool bRestarted = completedSubmissionSerial < m_LastCompletedSubmissionSerial;
        m_LastCompletedSubmissionSerial = completedSubmissionSerial;
        // 適用の確認にも、完了した提出を渡す（適用した番号と提出番号の対応から、完了した番号を公開する）
        m_PacketManager.GetConfirmation().Retire(completedSubmissionSerial, bRestarted);

        size_t completedCount = 0;
        for (const SubmittedRenderFrame& frame : m_SubmittedRenderFrames)
        {
            if (!bRestarted && frame.SubmissionSerial > completedSubmissionSerial)
            {
                break;
            }
            m_CompletedRenderFrameSerial = std::max(m_CompletedRenderFrameSerial, frame.RenderFrameSerial);
            ++completedCount;
        }
        if (completedCount > 0)
        {
            m_SubmittedRenderFrames.erase(m_SubmittedRenderFrames.begin(),
                                          m_SubmittedRenderFrames.begin() + static_cast<std::ptrdiff_t>(completedCount));
        }
    }

    void RenderingCoordinator::Shutdown()
    {
        m_PreviousCompletedTotalFrameTimeMs = 0.0f;
        m_LatestCompletedGPUTimeMs = 0.0f;
        m_bLatestCompletedGPUTimeValid = false;
        m_LatestAutoExposure = AutoExposureMeasurement{};
        m_GPUTimingMailbox.Clear();

        if (m_Diagnostics)
        {
            m_Diagnostics->Reset();
        }

        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderingCoordinator::Shutdown() - Starting shutdown");

        ReleaseInitializedResources();

        m_bInitialized = false;
        LOG_INFO("RenderingCoordinator::Shutdown() - Shutdown completed");
    }

    void RenderingCoordinator::ReleaseInitializedResources()
    {
        // GPU処理の完了を待機
        if (m_Device)
        {
            m_Device->WaitIdle();
            // 守られていない上書きの見張りの一覧と集計を出す（終わりに 1 回）
            if (RHI::FrameResourceWriteTracker* const writeTracker = m_Device->GetFrameResourceWriteTracker())
            {
                writeTracker->EmitSummary("release");
            }
#if NORVES_BUILD_DEVELOPMENT
            // 鍵で持ち続けたレンダーパス・フレームバッファ・パイプラインの作成数と使い回した数（累計）
            const RHI::ObjectCacheStats objectCacheStats = m_Device->GetObjectCacheStats();
            NORVES_LOG_INFO("RenderingCoordinator",
                            "RHI_CREATE_SUMMARY RHI.Create.Framebuffer=%llu RHI.Create.RenderPass=%llu RHI.Create.Pipeline=%llu "
                            "reused_framebuffer=%llu reused_render_pass=%llu reused_pipeline=%llu evicted=%llu entries=%llu",
                            static_cast<unsigned long long>(objectCacheStats.CreatedFramebuffers),
                            static_cast<unsigned long long>(objectCacheStats.CreatedRenderPasses),
                            static_cast<unsigned long long>(objectCacheStats.CreatedPipelines),
                            static_cast<unsigned long long>(objectCacheStats.ReusedFramebuffers),
                            static_cast<unsigned long long>(objectCacheStats.ReusedRenderPasses),
                            static_cast<unsigned long long>(objectCacheStats.ReusedPipelines),
                            static_cast<unsigned long long>(objectCacheStats.Evicted),
                            static_cast<unsigned long long>(objectCacheStats.Entries));
#endif
            // GPU を待ち終えたので、鍵で持ち続けた物を手放す（持ち手の残る物はその持ち手が手放すまで生きる）
            m_Device->ReleaseObjectCache();
        }
        // GPU を待ち終えたので、フレームの持ち主の分を全部戻す。提出済みの読み戻しはここで受け取り手（ビューの
        // パス）へ渡るので、下のビューの Shutdown が最後のフレームまでの統計を出せる
        m_FrameResourceOwners.Reset();
        m_FrameResourceOwners.ReleaseReadback();
        // 領域のページ（マップしたままのバッファ）をデバイスより先に手放す
        m_FrameResourceOwners.ReleaseUploadPages();
        m_FrameResourceOwners.ConfigureUpload(nullptr, FrameUploadArena::DefaultInitialPageBytes);
        // 記述子の pool もデバイスより先に手放す
        m_FrameResourceOwners.ReleaseDescriptorPools();
        m_FrameUploadPageSource.SetDevice(nullptr);

        // デバイス所有参照を解放する前に、レイトレーシングsceneの資源を破棄する
        NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem().Shutdown();

        if (m_FrameCaptureReadbackHelper)
        {
            m_FrameCaptureReadbackHelper->Shutdown();
            m_FrameCaptureReadbackHelper.reset();
        }

        // Writing中のパケットを安全にキャンセル（シャットダウン前のGT書き込み中断）
        m_PacketManager.CancelInflightWrites();

        // Readyな未消費パケットも全て解放（シャットダウン後は消費されないため）
        m_PacketManager.DrainUnconsumedPackets();

        // PresentationPass は直近の backbuffer/renderpass/pipeline 参照を保持するため、
        // RHI shutdown 前に request/result を明示クリアする。
        m_CompositePass.SetRequest(CompositePassRequest{});
        m_CompositePass.ReleaseRetainedResources();
        m_PresentationPass.SetRequest(PresentationPassRequest{});
        m_PresentationPass.InvalidateOverlayResources();

        // Viewの破棄
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Shutdown();
            }
        }
        m_Views.clear();
        m_MainSceneView.reset();
        if (m_CanvasView && Engine::GEngine)
        {
            Engine::GEngine->GetWorld().SetScreenSpaceBoardSink(nullptr);
        }
        m_CanvasView.reset();
        m_Cameras.clear();
        m_MainCameraId = 0;
        m_CanvasCameraId = 0;
        m_NextCameraId = 1;
        m_bCanvasCameraSyncPending.Store(false);
        m_PreviousMainCamera = CameraProxy{};
        m_bPreviousMainCameraValid = false;
        m_PreviousMegaGeometryWorlds.clear();
        m_PreviousSkinnedStates.Reset();
        m_bPreviousObjectStateValid = false;
        if (m_RenderedObjectRebasedFrameCount > 0u)
        {
            NORVES_LOG_INFO("RenderingCoordinator",
                            "stage=rendered_object_history rebased_frames=%llu incomplete_frames=%llu",
                            static_cast<unsigned long long>(m_RenderedObjectRebasedFrameCount),
                            static_cast<unsigned long long>(m_RenderedObjectIncompleteFrameCount));
        }
        m_RenderedObjectHistory.Reset();
        m_MegaSender.Reset();
        m_MegaMirror.Reset();
        m_MegaHistoryReportedMismatched = 0;
        m_bMegaDeltaPending = false;
        m_MegaDeltaSentCount = 0;
        m_MegaDeltaEntryTotal = 0;
        m_MegaVerifyReportedChecked = 0;
        m_MegaVerifyReportedMismatched = 0;
        m_MeshSender.Reset();
        m_SkinnedMeshSender.Reset();
        m_LightSender.Reset();
        m_MeshMirror.Reset();
        m_SkinnedMeshMirror.Reset();
        m_LightMirror.Reset();
        m_PacketLights.clear();
        m_PacketLightChanged.clear();
        m_PacketSceneLightCount = 0;
        m_bPacketHasSkySun = false;
        m_MeshRevisionSet.Reset();
        m_SkinnedMeshRevisionSet.Reset();
        m_bPacketLightRevisionDirty = true;
        m_ProxySyncSource = nullptr;
        m_ProxySyncGeneration = 0;
        m_bProxySyncValid = false;
        m_bProxyDeltaPending = false;
        m_ProxyDeltaSentCount = 0;
        m_MeshDeltaEntryTotal = 0;
        m_SkinnedMeshDeltaEntryTotal = 0;
        m_LightDeltaEntryTotal = 0;
        m_SceneRevisionSetChecked = 0;
        m_SceneRevisionSetMismatched = 0;
        m_ProxyDeltaReportedChecked = 0;
        m_ProxyDeltaReportedMismatched = 0;
        m_RenderedObjectRebasedFrameCount = 0u;
        m_RenderedObjectIncompleteFrameCount = 0u;

        // SceneRendererの終了
        m_SceneRenderer.Shutdown();

        // インスタンスデータSSBOリングの終了
        m_InstanceBufferRing.Shutdown();

        // RenderGraphの終了（一時リソースプールより先に破棄）
        m_RenderGraph.Shutdown();

        // 一時リソースプールの終了
        m_TransientPool.Shutdown();

        // シェーダーマネージャーの終了
        m_ShaderManager.Shutdown();

        // テスト三角形リソースの解放
        m_TrianglePipeline.reset();
        m_TriangleFragmentShader.reset();
        m_TriangleVertexShader.reset();

        // 3Dメッシュリソースの解放 → Blitリソースの解放
        m_BlitPipeline.reset();
        m_BlitDescriptorSets.clear();
        m_CompositeAlphaOverDescriptorSets.clear();
        m_BlitSampler.reset();
        m_CompositeAlphaOverFragmentShader.reset();
        m_BlitFragmentShader.reset();
        m_BlitVertexShader.reset();
        m_DepthTexture.reset();

        // フレームバッファ・レンダーパスの解放
        m_SwapChainFramebuffers.clear();
        m_PresentationLoadFramebuffers.clear();
        m_GraphPresentationClearFramebuffers.clear();
        m_GraphPresentationLoadFramebuffers.clear();
        m_bSwapChainFramebuffersReady = false;
        m_RenderPass.reset();
        m_PresentationLoadRenderPass.reset();
        m_GraphPresentationClearRenderPass.reset();
        m_GraphPresentationLoadRenderPass.reset();

        // コマンドリストの解放
        m_JobCommandLists.clear();
        m_CommandList.reset();

        // 共有リソースレジストリを明示的にクリア（テクスチャ参照をデバイス破棄前に解放）
        m_Screen.GetSharedResourceRegistry().Clear();

        // Screenの破棄（SwapChain解放を含む）
        m_Screen.Shutdown();

        // FramePacketManagerの終了
        m_PacketManager.Shutdown();

        // デバイス参照の解放（Engine層がRHIの終了を管理）
        m_Device.reset();
        m_bFrameSubmissionStarted = false;
    }

    bool RenderingCoordinator::EnsureCompositeAlphaOverResources()
    {
        if (m_CompositeAlphaOverFragmentShader && !m_CompositeAlphaOverDescriptorSets.empty())
        {
            return true;
        }

        if (!m_Device)
        {
            return true;
        }

        if (!m_BlitSampler)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Cannot create Composite alpha-over resources without sampler");
            return false;
        }

        if (!m_CompositeAlphaOverFragmentShader)
        {
            m_CompositeAlphaOverFragmentShader =
                m_ShaderManager.LoadShader("composite_alpha_over.frag", RHI::ShaderStage::Pixel);
            if (!m_CompositeAlphaOverFragmentShader)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Composite alpha-over fragment shader");
                return false;
            }
        }

        if (m_CompositeAlphaOverDescriptorSets.empty())
        {
            RHI::DescriptorSetDesc compositeDsDesc;
            RHI::DescriptorBinding sceneBinding;
            sceneBinding.binding = 0;
            sceneBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneBinding.stages = RHI::ShaderStage::Pixel;
            compositeDsDesc.bindings.push_back(sceneBinding);

            RHI::DescriptorBinding canvasBinding;
            canvasBinding.binding = 1;
            canvasBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            canvasBinding.stages = RHI::ShaderStage::Pixel;
            compositeDsDesc.bindings.push_back(canvasBinding);

            // CompositePass が毎フレーム束ね直して Update するので、Blit と同じく飛行中のスロットごとに 1 組作る
            const uint32_t compositeDescriptorSetCount =
                std::max<uint32_t>(static_cast<uint32_t>(m_BlitDescriptorSets.size()), 1u);
            Container::VariableArray<RHI::DescriptorSetPtr> compositeDescriptorSets;
            for (uint32_t slot = 0; slot < compositeDescriptorSetCount; ++slot)
            {
                // 飛行中のスロットで引く組なので、守られていない上書きの見張りへ「守られている」と登録する
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                RHI::DescriptorSetPtr compositeDescriptorSet = m_Device->CreateDescriptorSet(compositeDsDesc);
                if (!compositeDescriptorSet)
                {
                    NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Composite alpha-over descriptor set");
                    return false;
                }

                compositeDescriptorSet->BindSampler(0, m_BlitSampler);
                compositeDescriptorSet->BindSampler(1, m_BlitSampler);
                compositeDescriptorSets.push_back(std::move(compositeDescriptorSet));
            }
            m_CompositeAlphaOverDescriptorSets = std::move(compositeDescriptorSets);
        }

        return true;
    }

    void RenderingCoordinator::BeginFrame()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ConsumePendingCanvasCameraSync();
        m_bFrameSubmissionStarted = true;

        // 現在時刻を取得
        auto now = std::chrono::high_resolution_clock::now();
        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

        // デルタタイム計算
        float deltaTime = static_cast<float>(currentTime - m_LastFrameTime);
        m_LastFrameTime = currentTime;

        // 決定的な撮影: 壁時計を使わず 1/60 秒の固定刻みにし、エポックの 0 番のフレームから数え直す。
        bool bTemporalEpochStart = false;
        if (m_bDeterministicCapture)
        {
            deltaTime = 1.0f / 60.0f;
            if (m_bDeterministicEpochPending)
            {
                m_bDeterministicEpochPending = false;
                m_bDeterministicEpochActive = true;
                m_DeterministicEpochFrames = 0u;
                m_TotalTime = 0.0;
                bTemporalEpochStart = true;
            }
            else if (m_bDeterministicEpochActive)
            {
                ++m_DeterministicEpochFrames;
            }
        }
        m_TotalTime += deltaTime;

        // ST経路ではReadyパケットがRTに消費されないため、フレーム開始時に再利用する。
        // MT経路ではReady→QueuedのハンドオフをRT側が行うため、ここでは触らない。
        if (!m_bMultiThreadedRendering)
        {
            m_PacketManager.DrainUnconsumedPackets();
        }

        // 書き込み用パケットを取得
        m_CurrentPacket = m_PacketManager.AcquireForWrite();
        if (!m_CurrentPacket)
        {
            NORVES_LOG_WARNING("RenderingCoordinator",
                               "BeginFrame: all FramePacket slots occupied, skipping packet acquisition this frame");
        }
        if (m_CurrentPacket)
        {
            m_CurrentPacket->FrameNumber = m_GameThreadStats.FrameNumber;
            m_CurrentPacket->DeltaTime = deltaTime;
            m_CurrentPacket->TotalTime = m_TotalTime;
            m_CurrentPacket->bDeterministicCapture = m_bDeterministicCapture;
            m_CurrentPacket->bTemporalEpochStart = bTemporalEpochStart;
            m_CurrentPacket->EpochFrameIndex = m_bDeterministicEpochActive ? m_DeterministicEpochFrames : 0u;
        }

        m_GameThreadStats.DeltaTime = deltaTime;
        if (deltaTime > 0.0f)
        {
            m_GameThreadStats.FPS = 1.0f / deltaTime;
        }

        // Screen.BeginFrame（swapchain acquire）はRenderFrame内に移動。
        // GT側ではパケット取得のみ行い、swapchainへの触れは行わない。
    }

    void RenderingCoordinator::CollectScene()
    {
        if (!m_bInitialized || !m_CurrentPacket)
        {
            return;
        }

        NORVES_STAT_TIME_START(collection);

        // 各SceneViewでシーン収集
        // 注: ProxyはWorldからSceneViewに直接渡されているため、
        //     ここでは統計情報の更新のみ行う
        for (auto &view : m_Views)
        {
            if (!view)
            {
                continue;
            }

            // SceneViewのProxy情報は既にWorldから設定済み
        }

        NORVES_STAT_TIME_END(collection, m_GameThreadStats.CollectionTimeMs);
    }

    void RenderingCoordinator::SnapshotSceneParameters(
        FramePacket& packet,
        const RHI::DeviceCapabilities& capabilities) const
    {
        // 空の太陽は、パケットへ運ぶ光源表（UpdatePacketLights）が SceneView の光源の後に加える。
        packet.Scene.SkyAtmosphere = m_SkyAtmosphere;
        packet.Scene.SetDDGIVolumeParameters(
            SanitizeDDGIVolumeParametersForRHI(
                m_DDGIVolume,
                capabilities.RayTracing.bAccelerationStructure,
                capabilities.RayTracing.bRayQuery));
        packet.Scene.SetVolumetricFogParameters(m_VolumetricFog);
        packet.Scene.StaticEnvironmentIntensityScale = m_StaticEnvironmentIntensityScale;
        packet.Scene.StaticEnvironmentMaxRadiance = m_StaticEnvironmentMaxRadiance;
        packet.Scene.bStaticEnvironmentUniform = m_bStaticEnvironmentUniform;
        packet.Scene.StaticEnvironmentUniformRadiance = m_StaticEnvironmentUniformRadiance;
        packet.Scene.SunShadowMaxDistance = m_SunShadowMaxDistance;
        packet.Scene.AerialPerspective = m_AerialPerspective;
        packet.Scene.CloudLayer = m_CloudLayer;
        packet.Scene.Foliage = m_Foliage;
        packet.bRTGIEnabled = m_bRTGIEnabled;
    }

    void RenderingCoordinator::BuildSceneProxyDeltas(FramePacket& packet)
    {
        SceneView* sceneView = m_MainSceneView.get();
        const Container::VariableArray<MeshProxy> noMeshProxies;
        const Container::VariableArray<SkinnedMeshProxy> noSkinnedMeshProxies;
        const Container::VariableArray<LightProxy> noLightProxies;
        const Container::VariableArray<MeshProxy>& meshProxies = sceneView ? sceneView->GetMeshProxies() : noMeshProxies;
        const Container::VariableArray<SkinnedMeshProxy>& skinnedMeshProxies =
            sceneView ? sceneView->GetSkinnedMeshProxies() : noSkinnedMeshProxies;
        const Container::VariableArray<LightProxy>& sceneLights = sceneView ? sceneView->GetLightProxies() : noLightProxies;
        if (sceneView)
        {
            sceneView->TakeMeshProxyChangedIndices(m_MeshChangedScratch);
            sceneView->TakeSkinnedMeshProxyChangedIndices(m_SkinnedMeshChangedScratch);
            sceneView->TakeLightProxyChangedIndices(m_LightChangedScratch);
        }
        else
        {
            m_MeshChangedScratch.clear();
            m_SkinnedMeshChangedScratch.clear();
            m_LightChangedScratch.clear();
        }

        // SceneView かシーンの世代が替わったら、書いた位置の記録は前の配列のものなので、保持を全部作り直す
        const bool bRebuild = !m_bProxySyncValid || m_ProxySyncSource != sceneView ||
                              m_ProxySyncGeneration != packet.SceneGeneration;
        m_ProxySyncSource = sceneView;
        m_ProxySyncGeneration = packet.SceneGeneration;
        m_bProxySyncValid = true;

        // 構成の revision の hash の集合を、書いた位置だけで更新する
        m_MeshRevisionSet.Update(meshProxies.size(), m_MeshChangedScratch, bRebuild, [&](size_t index)
        {
            return MixRevisionValue(HashMeshProxyRevision(meshProxies[index]));
        });
        m_SkinnedMeshRevisionSet.Update(skinnedMeshProxies.size(), m_SkinnedMeshChangedScratch, bRebuild, [&](size_t index)
        {
            return MixRevisionValue(HashSkinnedMeshProxyRevision(skinnedMeshProxies[index]));
        });
        UpdatePacketLights(sceneLights, bRebuild);

        // 書く側は 世代 → 番号 の順に公開するので、読む側は逆の順に読む
        const uint64_t confirmedSerial = m_PacketManager.GetConfirmation().GetConfirmedSerial();
        const uint64_t confirmedGeneration = m_PacketManager.GetConfirmation().GetConfirmedGeneration();
        m_MeshSender.Build(meshProxies, m_MeshChangedScratch, confirmedSerial, confirmedGeneration,
                           packet.SceneGeneration, packet.MeshDelta, packet.Scene.MeshProxies);
        m_SkinnedMeshSender.Build(skinnedMeshProxies, m_SkinnedMeshChangedScratch, confirmedSerial, confirmedGeneration,
                                  packet.SceneGeneration, packet.SkinnedMeshDelta, packet.Scene.SkinnedMeshProxies);
        m_LightSender.Build(m_PacketLights, m_PacketLightChanged, confirmedSerial, confirmedGeneration,
                            packet.SceneGeneration, packet.LightDelta, packet.Scene.LightProxies);
        m_bProxyDeltaPending = true;
        m_ProxyDeltaPendingGeneration = packet.SceneGeneration;
        m_MeshDeltaEntryTotal += packet.MeshDelta.bBaseline ? meshProxies.size() : packet.MeshDelta.Indices.size();
        m_SkinnedMeshDeltaEntryTotal +=
            packet.SkinnedMeshDelta.bBaseline ? skinnedMeshProxies.size() : packet.SkinnedMeshDelta.Indices.size();
        m_LightDeltaEntryTotal += packet.LightDelta.bBaseline ? m_PacketLights.size() : packet.LightDelta.Indices.size();

        if (Engine::GEngine != nullptr && Engine::GEngine->GetEngineChecks().IsEnabled("PACKET_DELTA"))
        {
            // RenderThread が差分を当てた写しと照合する、送った時点の全量の hash
            packet.MeshDelta.bVerify = true;
            packet.MeshDelta.VerifyHash = HashProxyArrayContent(meshProxies);
            packet.SkinnedMeshDelta.bVerify = true;
            packet.SkinnedMeshDelta.VerifyHash = HashProxyArrayContent(skinnedMeshProxies);
            packet.LightDelta.bVerify = true;
            packet.LightDelta.VerifyHash = HashProxyArrayContent(m_PacketLights);
            // 書いた位置だけで更新した構成の hash の集合が、全部を数え直した値と一致すること
            const bool bMeshSame = MatchesRevisionSet(m_MeshRevisionSet, meshProxies,
                                                      [](const MeshProxy& proxy) { return HashMeshProxyRevision(proxy); });
            const bool bSkinnedSame =
                MatchesRevisionSet(m_SkinnedMeshRevisionSet, skinnedMeshProxies,
                                   [](const SkinnedMeshProxy& proxy) { return HashSkinnedMeshProxyRevision(proxy); });
            m_SceneRevisionSetChecked += 2u;
            m_SceneRevisionSetMismatched += (bMeshSame ? 0u : 1u) + (bSkinnedSame ? 0u : 1u);
        }
    }

    void RenderingCoordinator::UpdatePacketLights(const Container::VariableArray<LightProxy>& sceneLights, bool bRebuild)
    {
        // パケットの光源表は SceneView の光源と同じ位置に同じ光源を置き、その後に空の太陽を 1 つ置く
        // （空の太陽の LightId は Component の通し番号と重ならないので、SceneView の光源には含まれない）
        m_PacketLightChanged.clear();
        LightProxy sun;
        const bool bHasSun = MakeSkySunLightProxy(m_SkyAtmosphere, sun);
        const size_t oldSize = m_PacketLights.size();
        const size_t oldSceneCount = m_PacketSceneLightCount;
        const size_t sceneCount = sceneLights.size();
        const size_t newSize = sceneCount + (bHasSun ? 1u : 0u);
        if (bRebuild)
        {
            m_PacketLights = sceneLights;
            if (bHasSun)
            {
                m_PacketLights.push_back(sun);
            }
            for (size_t index = 0; index < std::max(oldSize, newSize); ++index)
            {
                m_PacketLightChanged.push_back(static_cast<uint32_t>(index));
            }
        }
        else
        {
            m_PacketLights.resize(newSize);
            for (uint32_t index : m_LightChangedScratch)
            {
                if (index < sceneCount)
                {
                    m_PacketLights[index] = sceneLights[index];
                    m_PacketLightChanged.push_back(index);
                }
            }
            // SceneView の光源が増えた位置（前は空の太陽が居た位置を含む）
            for (size_t index = oldSceneCount; index < sceneCount; ++index)
            {
                m_PacketLights[index] = sceneLights[index];
                m_PacketLightChanged.push_back(static_cast<uint32_t>(index));
            }
            const bool bSunChanged = bHasSun != m_bPacketHasSkySun || sceneCount != oldSceneCount ||
                                     (bHasSun && !SameLightValues(sun, m_PacketSkySun));
            if (bHasSun && bSunChanged)
            {
                m_PacketLights[sceneCount] = sun;
                m_PacketLightChanged.push_back(static_cast<uint32_t>(sceneCount));
            }
            std::sort(m_PacketLightChanged.begin(), m_PacketLightChanged.end());
            m_PacketLightChanged.erase(std::unique(m_PacketLightChanged.begin(), m_PacketLightChanged.end()),
                                       m_PacketLightChanged.end());
        }
        m_PacketSceneLightCount = sceneCount;
        m_bPacketHasSkySun = bHasSun;
        if (bHasSun)
        {
            m_PacketSkySun = sun;
        }
        if (!m_PacketLightChanged.empty() || oldSize != newSize)
        {
            m_bPacketLightRevisionDirty = true;
        }
    }

    void RenderingCoordinator::UpdateFrameRevisions(FramePacket& packet)
    {
        // 差分の同期では、Mesh・Skinned の集合は変わった位置だけで更新した値を使う（パケットは全量を持たない）
        const uint64_t sceneHash = packet.MeshDelta.bActive
                                       ? HashSceneRevisionIncremental(packet, m_MeshRevisionSet, m_SkinnedMeshRevisionSet)
                                       : ComputeSceneRevisionHash(packet);
        if (!m_bSceneRevisionHashValid)
        {
            m_LastSceneRevisionHash = sceneHash;
            m_bSceneRevisionHashValid = true;
        }
        else if (m_LastSceneRevisionHash != sceneHash)
        {
            m_LastSceneRevisionHash = sceneHash;
            m_SceneRevision = AdvanceRevision(m_SceneRevision);
        }

        // 光源表の hash は、光源表が変わったときだけ計算し直す
        if (!packet.LightDelta.bActive)
        {
            m_PacketLightRevisionHash = HashLightRevision(packet.Scene.LightProxies);
            m_bPacketLightRevisionDirty = true;
        }
        else if (m_bPacketLightRevisionDirty)
        {
            m_PacketLightRevisionHash = HashLightRevision(m_PacketLights);
            m_bPacketLightRevisionDirty = false;
        }
        const uint64_t lightHash = m_PacketLightRevisionHash;
        if (!m_bLightRevisionHashValid)
        {
            m_LastLightRevisionHash = lightHash;
            m_bLightRevisionHashValid = true;
        }
        else if (m_LastLightRevisionHash != lightHash)
        {
            m_LastLightRevisionHash = lightHash;
            m_LightRevision = AdvanceRevision(m_LightRevision);
        }

        packet.SceneRevision = m_SceneRevision;
        packet.LightRevision = m_LightRevision;
    }

    void RenderingCoordinator::GenerateDrawCommands()
    {
        if (!m_bInitialized)
        {
            return;
        }
        const RHI::DeviceCapabilities noDeviceCapabilities{};
        GenerateDrawCommands(m_Device ? m_Device->GetCapabilities() : noDeviceCapabilities);
    }

    void RenderingCoordinator::GenerateDrawCommands(const RHI::DeviceCapabilities& capabilities)
    {
        if (!m_bInitialized)
        {
            return;
        }

        NORVES_STAT_TIME_START(cmdGen);

        if (m_CurrentPacket)
        {
            m_CurrentPacket->bHasMainCamera = false;
            // 前のカメラは直前のゲームのフレームのパケットのものだけを渡す（velocity と TAA の履歴の基準を
            // 「このパケットの1つ前のフレーム」にそろえる。前の変換も同じ規約）。
            const bool bPreviousMainCameraContinuous =
                m_bPreviousMainCameraValid &&
                m_PreviousMainCameraFrameNumber + 1u == m_CurrentPacket->FrameNumber;
            m_CurrentPacket->bHasPreviousMainCamera = bPreviousMainCameraContinuous;
            if (bPreviousMainCameraContinuous)
            {
                m_CurrentPacket->PreviousMainCamera = m_PreviousMainCamera;
            }
            if (m_bCameraSet)
            {
                auto mainCamera = m_MainCamera;
                if (!mainCamera.IsValid())
                {
                    mainCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
                    mainCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
                }

                m_CurrentPacket->Scene.MainCamera = mainCamera;
                m_CurrentPacket->bHasMainCamera = true;
            }

            m_CurrentPacket->SceneGeneration = m_PacketManager.ResolveSceneGeneration(
                m_MainSceneView.get(), m_MainSceneView ? m_MainSceneView->GetSceneGeneration() : 0u);
            m_CurrentPacket->MegaDelta.Clear();
            m_bMegaDeltaPending = false;
            // Mesh・Skinned・Light は差分で運ぶ（基準のパケットだけが Scene の配列に全量を持つ）。消費者は RenderThread の写しを読む
            BuildSceneProxyDeltas(*m_CurrentPacket);
            if (m_MainSceneView)
            {
                if (m_MainSceneView->IsMegaFullSync())
                {
                    // 全量の同期（--mega-scene-full-sync）: 配列を毎フレーム全量で写す
                    m_CurrentPacket->Scene.MegaGeometryProxies = m_MainSceneView->GetMegaGeometryProxies();
                }
                else
                {
                    // 差分の同期: 変わった位置と、確認されていない送信の変化だけをパケットへ積む。
                    // 消費者は RenderThread の写し（MegaGeometryMirror）を読む
                    m_CurrentPacket->Scene.MegaGeometryProxies.clear();
                    m_MainSceneView->TakeMegaGeometryChangedIndices(m_MegaChangedScratch);
                    // 書く側は 世代 → 番号 の順に公開するので、読む側は逆の順に読む
                    const uint64_t confirmedSerial = m_PacketManager.GetConfirmation().GetConfirmedSerial();
                    const uint64_t confirmedGeneration = m_PacketManager.GetConfirmation().GetConfirmedGeneration();
                    MegaSceneDelta& megaDelta = m_CurrentPacket->MegaDelta;
                    m_MegaSender.Build(m_MainSceneView->GetMegaGeometryProxies(), m_MegaChangedScratch, confirmedSerial,
                                       confirmedGeneration, m_CurrentPacket->SceneGeneration, megaDelta);
                    m_bMegaDeltaPending = true;
                    m_MegaDeltaPendingGeneration = m_CurrentPacket->SceneGeneration;
                    m_MegaDeltaEntryTotal += megaDelta.Indices.size();
                    if (Engine::GEngine != nullptr && Engine::GEngine->GetEngineChecks().IsEnabled("MEGA_SCENE_DELTA"))
                    {
                        megaDelta.bVerify = true;
                        megaDelta.VerifyHash = HashMegaGeometryContent(m_MainSceneView->GetMegaGeometryProxies());
                    }
                }
            }
            SnapshotSceneParameters(*m_CurrentPacket, capabilities);
            // 影を落とす点光源の選択と6面の行列は、空の太陽を加えた後の光源表とメインカメラから作る。
            BuildPointShadowSnapshot(m_PacketLights,
                                     m_CurrentPacket->bHasMainCamera
                                         ? &m_CurrentPacket->Scene.MainCamera
                                         : nullptr,
                                     m_CurrentPacket->PointShadows);

            m_CurrentPacket->DrawCommands.clear();
            m_CurrentPacket->DrawCommands.reserve(m_MaxDrawCallsPerFrame);
            m_CurrentPacket->DrawCommandRange = CommandRange{};
            m_CurrentPacket->OpaqueCommandRange = CommandRange{};
            m_CurrentPacket->SkinnedMeshFrameLeases.clear();
            m_CurrentPacket->TransparentCommandRange = CommandRange{};
            m_CurrentPacket->InstanceData.clear();
            m_CurrentPacket->Views.clear();
            m_CurrentPacket->Stats = FrameStatsSnapshot{};
            m_CurrentPacket->GeneratedDrawCommandCount = 0;

            auto& debugDraw = NorvesLib::Core::GEngine.GetDebugDraw();
            m_CurrentPacket->DebugLineVertices.clear();
            const auto &debugLineVertices = debugDraw.GetVertices();
            if (!debugLineVertices.empty())
            {
                m_CurrentPacket->DebugLineVertices.insert(m_CurrentPacket->DebugLineVertices.end(),
                                                          debugLineVertices.begin(),
                                                          debugLineVertices.end());
            }
            debugDraw.Clear();
        }

        bool bLegacyCommandsSet = false;
        const auto &screenViews = m_Screen.GetViews();
        for (uint32_t viewIndex = 0; viewIndex < screenViews.size(); ++viewIndex)
        {
            const auto &view = screenViews[viewIndex];
            if (!view)
            {
                continue;
            }

            ViewRenderPlan viewPlan;
            viewPlan.ViewId = viewIndex;
            viewPlan.ViewType = static_cast<uint8_t>(view->GetViewType());
            viewPlan.Priority = static_cast<int32_t>(viewIndex);
            viewPlan.bEnabled = view->IsEnabled();

            auto sceneView = Container::DynamicPointerCast<SceneView>(view);
            auto canvasView = Container::DynamicPointerCast<CanvasView>(view);
            const bool bIsMainSceneView = sceneView && sceneView == m_MainSceneView;
            const uint32_t viewportCount = view->GetViewportCount();

            if (sceneView && viewportCount == 0 && view->IsEnabled())
            {
                // Viewport未作成時の互換フォールバック。
                sceneView->PrepareDrawCommands();

                const uint32_t instanceBase =
                    AppendInstanceDataToPacket(m_CurrentPacket, sceneView->GetInstanceData());

                CommandRange opaqueCommandRange;
                CommandRange transparentCommandRange;
                CommandRange drawCommandRange;
                if (m_CurrentPacket)
                {
                    opaqueCommandRange =
                        AppendRebasedDrawCommands(sceneView->GetOpaqueCommands(),
                                                  instanceBase,
                                                  m_CurrentPacket->DrawCommands);
                    const CommandRange skinnedCommandRange =
                        AppendSkinnedDrawCommands(m_CurrentPacket, sceneView->GetSkinnedMeshProxies());
                    opaqueCommandRange = CombineCommandRanges(opaqueCommandRange, skinnedCommandRange);
                    transparentCommandRange =
                        AppendRebasedDrawCommands(sceneView->GetTransparentCommands(),
                                                  instanceBase,
                                                  m_CurrentPacket->DrawCommands);
                    drawCommandRange = CombineCommandRanges(opaqueCommandRange, transparentCommandRange);
                    AccumulateViewStats(m_CurrentPacket, sceneView->GetStats());
                }

                if (m_CurrentPacket && bIsMainSceneView && !bLegacyCommandsSet)
                {
                    m_CurrentPacket->DrawCommandRange = drawCommandRange;
                    m_CurrentPacket->OpaqueCommandRange = opaqueCommandRange;
                    m_CurrentPacket->TransparentCommandRange = transparentCommandRange;
                    bLegacyCommandsSet = true;
                }
            }

            for (uint32_t viewportIndex = 0; viewportIndex < viewportCount; ++viewportIndex)
            {
                auto viewport = view->GetViewport(viewportIndex);
                if (!viewport)
                {
                    continue;
                }

                const CameraProxy *fallbackCamera =
                    (bIsMainSceneView && m_bCameraSet) ? &m_MainCamera : nullptr;
                auto resolveCamera = [this](uint64_t cameraId) -> const CameraProxy*
                {
                    return FindCamera(cameraId);
                };
                // キャンバス（UI）は内部解像度で描かず、画面解像度でそのまま描く。
                ViewportRenderPlan viewportPlan = BuildViewportRenderPlan(*viewport,
                                                                          viewIndex,
                                                                          viewportIndex,
                                                                          canvasView ? GetCanvasWidth() : m_RenderWidth,
                                                                          canvasView ? GetCanvasHeight() : m_RenderHeight,
                                                                          resolveCamera,
                                                                          fallbackCamera);

                if (sceneView && view->IsEnabled() && viewportPlan.HasDrawableExtent())
                {
                    sceneView->PrepareDrawCommandsForViewport(viewportPlan);

                    const uint32_t instanceBase =
                        AppendInstanceDataToPacket(m_CurrentPacket, sceneView->GetInstanceData());
                    if (m_CurrentPacket)
                    {
                        viewportPlan.OpaqueCommandRange =
                            AppendRebasedDrawCommands(sceneView->GetOpaqueCommands(),
                                                      instanceBase,
                                                      m_CurrentPacket->DrawCommands);
                        const CommandRange skinnedCommandRange =
                            AppendSkinnedDrawCommands(m_CurrentPacket, sceneView->GetSkinnedMeshProxies());
                        viewportPlan.OpaqueCommandRange =
                            CombineCommandRanges(viewportPlan.OpaqueCommandRange, skinnedCommandRange);
                        viewportPlan.TransparentCommandRange =
                            AppendRebasedDrawCommands(sceneView->GetTransparentCommands(),
                                                      instanceBase,
                                                      m_CurrentPacket->DrawCommands);
                        viewportPlan.DrawCommandRange =
                            CombineCommandRanges(viewportPlan.OpaqueCommandRange,
                                                 viewportPlan.TransparentCommandRange);
                        AccumulateViewStats(m_CurrentPacket, sceneView->GetStats());
                    }

                    if (m_CurrentPacket && bIsMainSceneView && !bLegacyCommandsSet)
                    {
                        m_CurrentPacket->DrawCommandRange = viewportPlan.DrawCommandRange;
                        m_CurrentPacket->OpaqueCommandRange = viewportPlan.OpaqueCommandRange;
                        m_CurrentPacket->TransparentCommandRange = viewportPlan.TransparentCommandRange;
                        bLegacyCommandsSet = true;
                    }
                }

                if (canvasView && view->IsEnabled() && viewportPlan.HasDrawableExtent())
                {
                    const uint32_t packetCommandBase =
                        m_CurrentPacket ? static_cast<uint32_t>(m_CurrentPacket->DrawCommands.size()) : 0u;
                    canvasView->PrepareBoardDrawCommands(viewportPlan, packetCommandBase);

                    const uint32_t instanceBase =
                        AppendInstanceDataToPacket(m_CurrentPacket, canvasView->GetBoardInstanceData());
                    if (m_CurrentPacket)
                    {
                        viewportPlan.TransparentCommandRange =
                            AppendRebasedDrawCommands(canvasView->GetBoardDrawCommands(),
                                                      instanceBase,
                                                      m_CurrentPacket->DrawCommands);
                        viewportPlan.DrawCommandRange = viewportPlan.TransparentCommandRange;
                    }
                }

                viewPlan.Viewports.push_back(viewportPlan);
            }

            if (m_CurrentPacket)
            {
                m_CurrentPacket->Views.push_back(viewPlan);
            }
        }

        if (m_CurrentPacket)
        {
            m_CurrentPacket->GeneratedDrawCommandCount =
                static_cast<uint32_t>(m_CurrentPacket->DrawCommands.size());
            ApplyPreviousObjectStates(*m_CurrentPacket);

            const MeshResources* meshResources =
                m_RenderResources ? &m_RenderResources->Meshes() : nullptr;
            const MaterialResources* materialResources =
                m_RenderResources ? &m_RenderResources->Materials() : nullptr;
            const MegaGeometryResources* megaGeometryResources =
                m_RenderResources ? &m_RenderResources->MegaGeometry() : nullptr;
            // 差分の同期では、MegaGeometry のレイトレーシングのインスタンスは RenderThread が写しの席ごとの配列から選んで足す
            // （連番のパストレーシングも同じ。前の値の持ち越しは RenderThread が MegaGeometry を足した後に当てる）
            const bool bDeferMegaRayTracing = m_CurrentPacket->MegaDelta.bActive;
            if (!NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem().BuildFrameSnapshot(
                    meshResources,
                    *m_CurrentPacket,
                    materialResources,
                    bDeferMegaRayTracing ? nullptr : megaGeometryResources,
                    nullptr,
                    m_CurrentPacket->MeshDelta.bActive && m_MainSceneView
                        ? &m_MainSceneView->GetMeshProxies()
                        : nullptr))
            {
                NORVES_LOG_WARNING("RayTracingSceneSubsystem", "FramePacketのレイトレーシングscene "
                                                               "snapshotを構築できませんでした");
            }
            // 連番の1フレームの間は、どのパケットにも同じ前のカメラ・instance変換を書く。
            ApplyPathTracingSequenceCarry(*m_CurrentPacket, m_PacketManager.GetSequenceCarry());
            UpdateFrameRevisions(*m_CurrentPacket);
        }

        NORVES_STAT_TIME_END(cmdGen, m_GameThreadStats.CommandGenerationTimeMs);

    }

    void RenderingCoordinator::ApplyPreviousObjectStates(FramePacket& packet)
    {
        // 前の値は直前のゲームのフレームのパケットのものだけを使う（前のカメラと同じ規約）。
        const bool bHasPrevious = m_bPreviousObjectStateValid &&
                                  m_PreviousObjectStateFrameNumber + 1u == packet.FrameNumber;
        // 差分の同期では、MegaGeometry の前の変換は RenderThread の写しが描いた履歴から決める（パケットは全量を持たない）。
        // 全量の同期（--mega-scene-full-sync）だけ、毎フレーム全量で前の変換を付け直して覚え直す
        const bool bMegaFull = !packet.MegaDelta.bActive;
        if (bMegaFull)
        {
            for (MegaGeometryProxy& proxy : packet.Scene.MegaGeometryProxies)
            {
                proxy.PreviousWorldTransform = proxy.WorldTransform;
                if (bHasPrevious)
                {
                    const auto found = m_PreviousMegaGeometryWorlds.find(proxy.ObjectId);
                    if (found != m_PreviousMegaGeometryWorlds.end())
                    {
                        proxy.PreviousWorldTransform = found->second;
                    }
                }
            }
        }
        m_PreviousSkinnedStates.Apply(packet,bHasPrevious);

        if (bMegaFull)
        {
            m_PreviousMegaGeometryWorlds.clear();
            for (const MegaGeometryProxy& proxy : packet.Scene.MegaGeometryProxies)
            {
                // 物体（ObjectId）ごとに覚える（散布の部品は 1 つの ComponentId が多数の物体を持つ）
                if (proxy.ObjectId != 0)
                {
                    m_PreviousMegaGeometryWorlds[proxy.ObjectId] = proxy.WorldTransform;
                }
            }
        }
        m_PreviousSkinnedStates.Record(packet);
        m_PreviousObjectStateFrameNumber = packet.FrameNumber;
        m_bPreviousObjectStateValid = true;
    }

    FramePacket* RenderingCoordinator::EndFrame()
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        // 書き込み完了をマーク（Writing→Ready）
        // Screen.EndFrame（submit/present）はRenderFrame内で実行するため、ここでは行わない。
        FramePacket* finishedPacket = m_CurrentPacket;
        CameraProxy finishedCamera;
        uint64_t finishedFrameNumber = 0u;
        const bool bFinishedCameraValid = m_CurrentPacket && m_CurrentPacket->bHasMainCamera;
        if (bFinishedCameraValid)
        {
            finishedCamera = m_CurrentPacket->Scene.MainCamera;
            finishedFrameNumber = m_CurrentPacket->FrameNumber;
        }
        if (m_CurrentPacket)
        {
            m_CurrentPacket->Stats.GameThreadStats = m_GameThreadStats;
#if NORVES_ENABLE_STATS
            m_CurrentPacket->Stats.bGameThreadTimingsAvailable =
                NorvesLib::Debug::StatsManager::Get().IsTraceActive();
#endif
            if (m_FrameCaptureReadbackHelper)
            {
                m_FrameCaptureReadbackHelper->TrySnapshotPendingRequest(
                    m_CurrentPacket->CaptureRequest);
            }
            m_PacketManager.FinishWrite(m_CurrentPacket);
            m_CurrentPacket = nullptr;
            if (m_bMegaDeltaPending)
            {
                // 送り終えた番号で、運んだ変化を台帳へ記録する（確認されるまで次のパケットが積み直す）
                m_MegaSender.NoteSent(m_PacketManager.GetLastSentSerial(), m_MegaDeltaPendingGeneration);
                m_bMegaDeltaPending = false;
                ++m_MegaDeltaSentCount;
                if (m_MegaDeltaSentCount % 120 == 0)
                {
                    NORVES_LOG_INFO("RenderingCoordinator",
                                    "MEGA_SCENE_DELTA sent=%llu entries=%llu baselines=%llu unconfirmed=%zu consumer_patches=%llu",
                                    static_cast<unsigned long long>(m_MegaDeltaSentCount),
                                    static_cast<unsigned long long>(m_MegaDeltaEntryTotal),
                                    static_cast<unsigned long long>(m_MegaSender.GetBaselineCount()),
                                    m_MegaSender.GetUnconfirmedSendCount(),
                                    static_cast<unsigned long long>(m_MegaMirror.GetConsumerCheckedCount()));
                }
                RunMegaDeltaCheck(m_MegaMirror, m_MegaDeltaSentCount, m_MegaVerifyReportedChecked,
                                  m_MegaVerifyReportedMismatched);
            }
            if (m_bProxyDeltaPending)
            {
                // 送り終えた番号で、運んだ変化を台帳へ記録する（確認されるまで次のパケットが積み直す）
                const uint64_t sentSerial = m_PacketManager.GetLastSentSerial();
                m_MeshSender.NoteSent(sentSerial, m_ProxyDeltaPendingGeneration);
                m_SkinnedMeshSender.NoteSent(sentSerial, m_ProxyDeltaPendingGeneration);
                m_LightSender.NoteSent(sentSerial, m_ProxyDeltaPendingGeneration);
                m_bProxyDeltaPending = false;
                ++m_ProxyDeltaSentCount;
                if (m_ProxyDeltaSentCount % 120 == 0)
                {
                    NORVES_LOG_INFO("RenderingCoordinator",
                                    "PACKET_DELTA sent=%llu mesh_entries=%llu skinned_entries=%llu light_entries=%llu "
                                    "mesh_baselines=%llu unconfirmed=%zu",
                                    static_cast<unsigned long long>(m_ProxyDeltaSentCount),
                                    static_cast<unsigned long long>(m_MeshDeltaEntryTotal),
                                    static_cast<unsigned long long>(m_SkinnedMeshDeltaEntryTotal),
                                    static_cast<unsigned long long>(m_LightDeltaEntryTotal),
                                    static_cast<unsigned long long>(m_MeshSender.GetBaselineCount()),
                                    m_MeshSender.GetUnconfirmedSendCount());
                }
                const uint64_t checked = m_MeshMirror.GetVerifyCheckedCount() +
                                         m_SkinnedMeshMirror.GetVerifyCheckedCount() +
                                         m_LightMirror.GetVerifyCheckedCount() + m_SceneRevisionSetChecked;
                const uint64_t mismatched = m_MeshMirror.GetVerifyMismatchedCount() +
                                            m_SkinnedMeshMirror.GetVerifyMismatchedCount() +
                                            m_LightMirror.GetVerifyMismatchedCount() + m_SceneRevisionSetMismatched;
                RunPacketDeltaCheck(m_ProxyDeltaSentCount, checked, mismatched, m_ProxyDeltaReportedChecked,
                                    m_ProxyDeltaReportedMismatched);
            }
            RunPacketConfirmCheck(m_PacketManager);
            PublishSceneSlotGate(m_MainSceneView.get(), m_PacketManager);
            RunGpuSceneSlotRulesCheck(m_PacketManager.GetLastSentSerial());
        }

        if (bFinishedCameraValid)
        {
            m_PreviousMainCamera = finishedCamera;
            m_PreviousMainCameraFrameNumber = finishedFrameNumber;
            m_bPreviousMainCameraValid = true;
        }
        else
        {
            // カメラを公開しなかったフレームをまたいで履歴を再利用しない。
            m_PreviousMainCamera = CameraProxy{};
            m_bPreviousMainCameraValid = false;
        }

        m_GameThreadStats.FrameNumber++;
        return finishedPacket;
    }

    void RenderingCoordinator::SetOverlayPassesForNextFrame(Container::Span<IViewPass *> passes)
    {
        // 書き込み中パケットへ overlay 借用ポインタをコピーする。BeginFrame で確保した
        // m_CurrentPacket が無い場合(スロット枯渇等)は何もしない。空 passes のときは
        // OverlayPasses を空に保ち、描画シームを完全 no-op にする。
        if (!m_CurrentPacket)
        {
            return;
        }

        // 書き込み中パケットが占有する FramePacket スロット index。overlay パスが
        // per-slot リソースを書き込み中スロットへ束ねるために通知する。RenderThread の
        // 読取スロットとはプール排他で重ならないため、この index で per-slot 書込みしても
        // RT の per-slot 読取と競合しない（FramePacket スロット寿命連動の証明根拠）。
        const uint32_t writeSlotIndex = m_PacketManager.GetSlotIndex(m_CurrentPacket);

        m_CurrentPacket->OverlayPasses.clear();
        for (IViewPass *pass : passes)
        {
            if (pass)
            {
                // 当該パケットが Writing 状態である間（=RT が同スロットを読まない間）に
                // GameThread 上で発火する per-slot 束ね通知。既定 no-op。
                pass->OnAssignedToPacket(writeSlotIndex);
                m_CurrentPacket->OverlayPasses.push_back(pass);
            }
        }
    }

    FrameCaptureRequestResult RenderingCoordinator::RequestFrameCapture()
    {
        return RequestFrameCapture(FrameCaptureRequest{});
    }

    FrameCaptureRequestResult RenderingCoordinator::RequestFrameCapture(
        const FrameCaptureRequest& request)
    {
        if (!m_bInitialized || !m_FrameCaptureReadbackHelper)
        {
            return {};
        }

        return m_FrameCaptureReadbackHelper->RequestFrameCapture(request);
    }

    bool RenderingCoordinator::TryConsumeCapturedFrame(CapturedFrame& outFrame)
    {
        if (!m_bInitialized || !m_FrameCaptureReadbackHelper)
        {
            outFrame = CapturedFrame{};
            return false;
        }

        return m_FrameCaptureReadbackHelper->TryConsumeCapturedFrame(outFrame);
    }

    void RenderingCoordinator::RenderFrame(FramePacket *packet)
    {
        if (!m_bInitialized || !packet)
        {
            return;
        }
        // パスの外の書き込みは呼び出し元の名前で見張りへ報告する（パスの中はパスの名前の区間が上書きする）
        RHI::FrameResourceWriteScope frameWriteScope("RenderingCoordinator");

#if NORVES_ENABLE_STATS
        auto &statsManager = NorvesLib::Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point renderFrameStartTime;
        if (bTraceActive)
        {
            renderFrameStartTime = std::chrono::high_resolution_clock::now();
        }
#endif
        // 直下の区間: RT.ApplyPacket → （RT.WaitFrameFence・RT.AcquireImage はスワップチェーンが取る）→ RT.PrepareFrame
        // → RT.Streaming → RT.BeginRecording → RT.BuildAccelerationStructures → RT.RenderViews（RenderGraph.* を含む）
        // → RT.RecordTail → （RT.Submit・RT.Present はスワップチェーンが取る）→ RT.FinishFrame
        SequentialStatScope frameScopes("RenderThread");
        frameScopes.Begin("RT.ApplyPacket");

        bool bCanReadPacket = packet->GetState() == FramePacketState::Reading;
        if (!bCanReadPacket)
        {
            bCanReadPacket = packet->CompareExchangeState(FramePacketState::Queued, FramePacketState::Reading) ||
                             packet->CompareExchangeState(FramePacketState::Ready, FramePacketState::Reading);
        }
        if (!bCanReadPacket)
        {
            return;
        }

        // 適用の確認。マルチ・シングルのどちらの経路もここを通る共通の場所で、適用済みより古いパケットは描かずに捨て、
        // 適用したパケットは、描画を見送っても（スワップチェーンが使えないなど）確認する
        PacketConfirmation& packetConfirmation = m_PacketManager.GetConfirmation();
        const PacketApplyResult applyResult =
            packetConfirmation.TryApply(packet->SendSerial, packet->SceneGeneration);
        if (applyResult == PacketApplyResult::StaleSerial || applyResult == PacketApplyResult::StaleGeneration)
        {
            NORVES_LOG_WARNING("RenderingCoordinator",
                               "適用済みより古いパケットを捨てた: serial=%llu generation=%llu confirmed=%llu",
                               static_cast<unsigned long long>(packet->SendSerial),
                               static_cast<unsigned long long>(packet->SceneGeneration),
                               static_cast<unsigned long long>(packetConfirmation.GetConfirmedSerial()));
            return;
        }
        struct AppliedPacketScope
        {
            PacketConfirmation& Confirmation;
            uint64_t SendSerial;
            bool bApplied;
            ~AppliedPacketScope()
            {
                if (bApplied)
                {
                    Confirmation.NoteFinished(SendSerial);
                }
            }
        } appliedPacketScope{packetConfirmation, packet->SendSerial, applyResult == PacketApplyResult::Applied};

        // MegaGeometry の差分を写しへ当てる。描画を見送っても、適用したパケットの差分は当てる（次の差分の土台になる）。
        // 前の変換（最後に描いたフレームの変換）は、描く場所の NoteDrawn が決める
        if (applyResult == PacketApplyResult::Applied && packet->MegaDelta.bActive)
        {
            if (!m_MegaMirror.Apply(packet->MegaDelta, packet->SceneGeneration))
            {
                NORVES_LOG_ERROR("RenderingCoordinator",
                                 "MegaGeometry の差分を写しへ当てられません: serial=%llu generation=%llu mirror_generation=%llu",
                                 static_cast<unsigned long long>(packet->SendSerial),
                                 static_cast<unsigned long long>(packet->SceneGeneration),
                                 static_cast<unsigned long long>(m_MegaMirror.GetGeneration()));
            }
            m_MegaMirror.Verify(packet->MegaDelta);
        }
        // Mesh・Skinned・Light の差分も同じ場所で写しへ当てる（描画を見送っても当てる。次の差分の土台になる）
        if (applyResult == PacketApplyResult::Applied)
        {
            ApplyProxyArrayDelta(m_MeshMirror, packet->MeshDelta, *packet, packet->Scene.MeshProxies, "Mesh");
            ApplyProxyArrayDelta(m_SkinnedMeshMirror, packet->SkinnedMeshDelta, *packet,
                                 packet->Scene.SkinnedMeshProxies, "SkinnedMesh");
            ApplyProxyArrayDelta(m_LightMirror, packet->LightDelta, *packet, packet->Scene.LightProxies, "Light");
        }

        FrameCaptureRequestSnapshot claimedCaptureRequest;
        if (m_FrameCaptureReadbackHelper)
        {
            m_FrameCaptureReadbackHelper->TryClaimPendingRequest(
                packet->CaptureRequest,
                claimedCaptureRequest);
        }
        FrameCaptureAssignmentGuard captureAssignment(
            m_FrameCaptureReadbackHelper.get(),
            claimedCaptureRequest,
            packet->FrameNumber);
        FrameCaptureRecordStatus captureRecordStatus = FrameCaptureRecordStatus::NoRequest;

        Debug::RenderingStats renderStats = packet->Stats.GameThreadStats;
        renderStats.VisibleObjects = packet->Stats.VisibleObjects;
        renderStats.BatchCount = packet->Stats.BatchCount;
        renderStats.InstancedDrawCalls = packet->Stats.InstancedDrawCalls;
        renderStats.SavedDrawCalls = packet->Stats.SavedDrawCalls;
        renderStats.CullingTimeMs = packet->Stats.CullingTimeMs;
        renderStats.BatchingTimeMs = packet->Stats.BatchingTimeMs;
        const bool bGameThreadTimingsAvailable = packet->Stats.bGameThreadTimingsAvailable;

        RenderGraphDebugDumpRequestClaim debugDumpClaim;
        RenderGraphDebugCapture debugDumpCapture;
        if (m_Diagnostics)
        {
            debugDumpClaim = m_Diagnostics->TryClaimRenderGraphDebugDumpRequest();
            const ViewportRenderPlan *primarySceneViewport =
                RenderFrameExecutor::FindPrimarySceneViewportRenderPlan(*packet);
            if (debugDumpClaim.IsClaimed() && primarySceneViewport)
            {
                debugDumpCapture.TargetSceneViewId = primarySceneViewport->ViewId;
                debugDumpCapture.Options.bEnabled = true;
                debugDumpCapture.Options.bWriteFiles = false;
                debugDumpCapture.Options.bText = true;
                debugDumpCapture.Options.bDot = false;
                debugDumpCapture.Options.bJson = false;
                debugDumpCapture.Options.bDebugMarkers = false;
            }
        }

        auto publishIncompleteStats = [this, packet, &renderStats, bGameThreadTimingsAvailable]()
        {
            if (!m_Diagnostics)
            {
                return;
            }

            RenderingCoordinatorStatsSnapshot statsSnapshot;
            statsSnapshot.SkinnedGBufferRecordedDraws = packet->Stats.SkinnedGBufferRecordedDraws;
            statsSnapshot.SkinnedShadowRecordedDraws = packet->Stats.SkinnedShadowRecordedDraws;
            statsSnapshot.Stats = renderStats;
            statsSnapshot.GeneratedDrawCommandCount = packet->GeneratedDrawCommandCount;
            statsSnapshot.bGameThreadTimingsAvailable = bGameThreadTimingsAvailable;
            statsSnapshot.AutoExposure = m_LatestAutoExposure;
            m_Diagnostics->PublishStatsSnapshot(statsSnapshot);
        };

        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain)
        {
            publishIncompleteStats();
            return;
        }

        const uint32_t presentationBufferCount = swapChain->GetBufferCount();
        const bool bPresentationResourcesMatchSwapChain =
            m_bSwapChainFramebuffersReady &&
            m_SwapChainFramebuffers.size() == presentationBufferCount &&
            m_PresentationLoadFramebuffers.size() == presentationBufferCount &&
            m_GraphPresentationClearFramebuffers.size() == presentationBufferCount &&
            m_GraphPresentationLoadFramebuffers.size() == presentationBufferCount;
        if (!bPresentationResourcesMatchSwapChain && !RecreateSwapChainPresentationResources())
        {
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "Swapchain presentation resources are not ready before acquire");
            publishIncompleteStats();
            return;
        }

        // swapchain acquire（旧BeginFrame経路から移動）
        // RenderThreadまたはSTインライン経路でここを呼ぶ。
        // 前のフレームの完了待ちと画像の取得は、スワップチェーンが RT.WaitFrameFence・RT.AcquireImage として取る。
        frameScopes.End();
        const RHI::SwapChainBeginFrameStatus beginFrameStatus = m_Screen.BeginFrame();
        frameScopes.Begin("RT.PrepareFrame");
        if (beginFrameStatus == RHI::SwapChainBeginFrameStatus::Fatal)
        {
            ThrowSwapChainBeginFrameError(beginFrameStatus);
        }
        if (beginFrameStatus == RHI::SwapChainBeginFrameStatus::NotReady)
        {
            publishIncompleteStats();
            return;
        }
        if (beginFrameStatus == RHI::SwapChainBeginFrameStatus::OutOfDate)
        {
            const uint32_t swapChainWidth = swapChain->GetWidth();
            const uint32_t swapChainHeight = swapChain->GetHeight();

            m_Width = swapChainWidth;
            m_Height = swapChainHeight;
            UpdateRenderResolution(swapChainWidth, swapChainHeight);
            RequestCanvasCameraSync();

            if (!RecreateSwapChainPresentationResources())
            {
                NORVES_LOG_ERROR("RenderingCoordinator",
                                 "Failed to recreate swapchain presentation resources after acquire failure");
            }

            for (auto &view : m_Views)
            {
                if (view)
                {
                    view->Resize(swapChainWidth, swapChainHeight);
                }
            }

            publishIncompleteStats();
            return;
        }
        const uint32_t frameIndex = ResolveFrameIndex(*swapChain);
        AdvanceCompletedRenderFrameSerial(swapChain->GetCompletedSubmissionSerial());
        if (!m_FrameResourceOwners.IsUploadConfigured())
        {
            ConfigureFrameUploadArena();
        }
        // フレームの持ち主の開始。完了した提出の分と、提出しなかったフレームの分を先に回収する。
        // このフレームの全ビュー・ビューポート・overlay がこの持ち主を使う（パスの Execute では位置を戻さない）
        FrameResourceOwner* const frameResources =
            m_FrameResourceOwners.BeginFrame(frameIndex, swapChain->GetCompletedSubmissionSerial());
        // 守られていない上書きの見張りのフレームを進める（飛行中のフレームを 2 と仮定して前のフレームの参照と比べる）
        if (RHI::FrameResourceWriteTracker* const writeTracker = m_Device->GetFrameResourceWriteTracker())
        {
            writeTracker->BeginFrame();
        }
        for (const Container::TSharedPtr<RHI::ICommandList>& jobCommandList : m_JobCommandLists)
        {
            jobCommandList->NotifyGPUTimestampFrameSlotCompleted(
                frameIndex,
                swapChain->GetCompletedSubmissionSerial());
        }
        if (m_RenderResources)
        {
            m_RenderResources->SkinnedMeshes().BeginFrame(
                swapChain->GetCompletedSubmissionSerial());
            m_RenderResources->BeginRetireFrame(swapChain->GetCompletedSubmissionSerial());
            m_SceneRenderer.SetSkinnedMeshResources(&m_RenderResources->SkinnedMeshes());
        }
        if (m_FrameCaptureReadbackHelper)
        {
            m_FrameCaptureReadbackHelper->PublishCompletedFrameSlot(frameIndex);
        }
        uint32_t imageIndex = swapChain->GetCurrentBackBufferIndex();

        if (imageIndex >= m_SwapChainFramebuffers.size() ||
            imageIndex >= m_PresentationLoadFramebuffers.size() ||
            imageIndex >= m_GraphPresentationClearFramebuffers.size() ||
            imageIndex >= m_GraphPresentationLoadFramebuffers.size())
        {
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "Swapchain framebuffer index out of range: image=%u framebufferCount=%zu loadFramebufferCount=%zu graphClearFramebufferCount=%zu graphLoadFramebufferCount=%zu",
                             imageIndex,
                             m_SwapChainFramebuffers.size(),
                             m_PresentationLoadFramebuffers.size(),
                              m_GraphPresentationClearFramebuffers.size(),
                              m_GraphPresentationLoadFramebuffers.size());
            PrepareJobCommandLists(*m_Device, m_JobCommandLists, 1u, frameIndex);
            m_CommandList->BeginRecording();
            ScopedJobCommandListsAbort jobCommandListsAbort(m_JobCommandLists);
            PublishCompletedGPUTimestampResults();
            m_CommandList->End();
            const RHI::SwapChainEndFrameResult endFrameResult = SubmitJobCommandLists(*swapChain, m_JobCommandLists);
            // 提出できたら通し番号を持ち主に結び付け、できなければ中止として次の開始で回収する
            m_FrameResourceOwners.CommitSubmitted(endFrameResult.SubmissionSerial);
            if (m_RenderResources)
            {
                if (endFrameResult.SubmissionSerial != 0)
                {
                    m_RenderResources->SkinnedMeshes().CommitSubmittedFrame(
                        endFrameResult.SubmissionSerial);
                    m_RenderResources->CommitRetireFrame(endFrameResult.SubmissionSerial);
                }
                else
                {
                    m_RenderResources->SkinnedMeshes().AbortFrame();
                    m_RenderResources->AbortRetireFrame();
                }
            }
            if (endFrameResult.SubmissionSerial != 0)
            {
                packetConfirmation.NoteSubmitted(packet->SendSerial, endFrameResult.SubmissionSerial);
            }
            if (endFrameResult.HasError())
            {
                ThrowSwapChainEndFrameError(endFrameResult.Status);
            }
            publishIncompleteStats();
            return;
        }

        m_TransientPool.BeginFrame(frameIndex);
        m_RenderGraph.BeginFrame(frameIndex);
        // TAA の履歴は最後に描いたフレームで書くので、描画がゲームのフレームを飛ばしたときは、velocity の基準
        // （物体の前の変換）もそのフレームのものへ付け替える。TAA を選んだカメラのときだけで、それ以外の
        // 描画は変えない。
        const bool bTemporalAARequested =
            (m_MainSceneView && m_MainSceneView->IsTemporalAAForced()) ||
            (packet->bHasMainCamera &&
             packet->Scene.MainCamera.AntiAliasing == CameraAntiAliasingMode::TemporalAA);
        // 最後に描いたフレームの直後でないパケット（パケットの破棄・描画の見送りを挟んだ）か
        const bool bRenderedFrameGap = m_RenderedObjectHistory.IsValid() &&
                                       packet->FrameNumber > m_RenderedObjectHistory.GetFrameNumber() + 1u;
        const RenderedObjectHistoryResult renderedObjectHistory = m_RenderedObjectHistory.Apply(
            *packet,
            packet->MeshDelta.bActive ? m_MeshMirror.GetProxies() : packet->Scene.MeshProxies,
            bTemporalAARequested);
        if (renderedObjectHistory.bRebased)
        {
            ++m_RenderedObjectRebasedFrameCount;
            m_RenderedObjectIncompleteFrameCount += renderedObjectHistory.bComplete ? 0u : 1u;
        }
        // MegaGeometry の写しも同じ規約: 前の変換は RenderThread が覚えた最後に描いたフレームの変換にする
        // （TAA の有無に関わらない。動いて止まった物体の前の変換も、描いた基準で 1 回だけ揃える）
        if (packet->MegaDelta.bActive)
        {
#if NORVES_BUILD_DEVELOPMENT
            // 検査 MEGA_HISTORY: 差分で保った前の変換を、全量で数え直した値と照合する。全量の写しの費用を抑えるため、
            // 16384 本以下は 4 回描くごと、それより多いと 60 回描くごと（262144 本を超える回は省く）
            m_MegaMirror.SetHistoryCheck(m_MegaMirror.GetProxies().size() <= 16384u ? 4u : 60u, 262144u);
#endif
            m_MegaMirror.NoteDrawn(bRenderedFrameGap);
#if NORVES_BUILD_DEVELOPMENT
            if (m_MegaMirror.GetHistoryMismatched() > m_MegaHistoryReportedMismatched)
            {
                NORVES_LOG_ERROR("RenderingCoordinator",
                                 "MEGA_HISTORY 前の変換が全量の経路（最後に描いたフレームの変換）と一致しません: mismatch=%llu",
                                 static_cast<unsigned long long>(m_MegaMirror.GetHistoryMismatched()));
                m_MegaHistoryReportedMismatched = m_MegaMirror.GetHistoryMismatched();
            }
            if (m_MegaMirror.GetDrawCount() % 60u == 0u)
            {
                NORVES_LOG_INFO("RenderingCoordinator",
                                "MEGA_HISTORY draws=%llu gap_draws=%llu checked=%llu instances=%llu skipped=%llu "
                                "unsettled=%zu mismatch=%llu",
                                static_cast<unsigned long long>(m_MegaMirror.GetDrawCount()),
                                static_cast<unsigned long long>(m_MegaMirror.GetGapDrawCount()),
                                static_cast<unsigned long long>(m_MegaMirror.GetHistoryCheckRounds()),
                                static_cast<unsigned long long>(m_MegaMirror.GetHistoryCheckedInstances()),
                                static_cast<unsigned long long>(m_MegaMirror.GetHistorySkippedRounds()),
                                m_MegaMirror.GetUnsettledCount(),
                                static_cast<unsigned long long>(m_MegaMirror.GetHistoryMismatched()));
            }
#endif
        }
        RHI::BufferPtr instanceDataBuffer = m_InstanceBufferRing.Upload(frameIndex, packet->InstanceData);

        // フレーム別コマンドバッファを選択（ダブルバッファリングでの同期問題を回避）
        // 仕事の番号ごとのリストを用意する。描画の記録はまだ 0 番の 1 本だけ
        PrepareJobCommandLists(*m_Device, m_JobCommandLists, 1u, frameIndex);

        // SceneRendererフレーム開始
        m_SceneRenderer.BeginFrame();

        frameScopes.Begin("RT.Streaming");
        // VT のストリーマを進める（タイルの読み込み完了の取り込み・ページの結び付け・コピーの積み込み）。
        // BindSparse は提出の直列化の下で呼ぶ必要があるので、RenderThread のこの位置（BeginRetireFrame の後）で行う。
        if (m_RenderResources)
        {
            m_RenderResources->UpdateVirtualTextureStreaming();
        }
        // ジオメトリのページのストリーマを進める（カリングの要求の取り込み・ページの読み込み・区画への書き込みと公開・追い出し）。
        // 区画へのコピーはステージングのリングへ積むだけで、コマンドの先頭（RecordTileUploads）で記録される。
        if (m_RenderResources)
        {
            m_RenderResources->UpdateGeometryPageStreaming();
        }

        // コマンド録画開始
        frameScopes.Begin("RT.BeginRecording");
        m_CommandList->BeginRecording();
        ScopedJobCommandListsAbort jobCommandListsAbort(m_JobCommandLists);
        // 仕事の番号 0 番はこのフレームで最初に提出されるので、その頭の全体のバリア 1 本で、同じキューの前の提出の
        // GPU の書き込み（TAA などの履歴・VSM のページの状態・HZB・TLAS の Update など、GPU だけが書いてフレームを
        // またいで 1 つで持つ資源）を、このフレームの全コマンドが待つ。以下の記録（タイルのコピー・TLAS の Update を含む）より前に張る
        m_CommandList->GlobalMemoryBarrier();
        // ステージングのリングに置いたタイル・ミップテイルのデータを、描画のコマンドの先頭でテクスチャの領域へコピーする
        // （GPU を待たない。render pass の外で、他の記録より前）。
        if (m_RenderResources)
        {
            m_RenderResources->RecordTileUploads(*m_CommandList);
        }
        PublishCompletedGPUTimestampResults();
        ScopedGPUTimestampFrameRecording gpuTimestampFrameGuard(
            m_CommandList.get(),
            frameIndex);

        // フレームのGPUの区間（FrameGPU）は加速構造の更新の記録より前に開き、更新の費用も含める。
        // タイムスタンプの書き込みは同じコマンドバッファへ足すだけで、記録の順序や同期は変えない。
#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            // 最新の FrameGPU は PublishCompletedGPUTimestampResults が全リストの結果を合わせて求める
            if (m_bLatestCompletedGPUTimeValid)
            {
                renderStats.GPUTimeMs = m_LatestCompletedGPUTimeMs;
            }

            if (m_CommandList->SupportsGPUTimestamps())
            {
                m_CommandList->BeginGPUTimestampFrame(packet->FrameNumber);
                m_CommandList->BeginGPUTimestamp(FrameGPUScopeName);
            }
        }
#endif

        frameScopes.Begin("RT.BuildAccelerationStructures");
        {
#if NORVES_ENABLE_STATS
            // 加速構造の更新は別の区間としても取り、トレースのGPUの行に出す。
            RHI::GPUTimestampScopeHandle accelerationStructureTimestamp;
            if (bTraceActive && m_CommandList->SupportsGPUTimestamps())
            {
                accelerationStructureTimestamp =
                    m_CommandList->BeginGPUTimestampScope("AccelerationStructureBuild");
            }
#endif
            // 差分の同期の MegaGeometry は、RenderThread の写し（NoteDrawn の後）の席ごとの配列から、
            // GameThread が足したメッシュのインスタンスと合わせて規則で選んだレイトレーシングのインスタンスを足す
            if (packet->MegaDelta.bActive && m_RenderResources && m_Device &&
                m_Device->GetCapabilities().RayTracing.bAccelerationStructure)
            {
                NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem().AppendMegaGeometryInstancesFromMirror(
                    &m_RenderResources->Meshes(),
                    &m_RenderResources->MegaGeometry(),
                    m_MegaMirror,
                    *packet);
            }
            if (!NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem().BuildAccelerationStructures(
                    m_Device,
                    *m_CommandList,
                    m_PacketManager.GetSlotIndex(packet),
                    *packet))
            {
                NORVES_LOG_WARNING("RayTracingSceneSubsystem",
                                   "FramePacketのレイトレーシング加速構造を構築できませんでした");
            }
#if NORVES_ENABLE_STATS
            if (accelerationStructureTimestamp.IsValid())
            {
                m_CommandList->EndGPUTimestampScope(accelerationStructureTimestamp);
            }
#endif
        }

        // ビューの描画（グラフの構築・RenderGraph.Compile・RenderGraph.Execute）
        frameScopes.Begin("RT.RenderViews");

        // ========================================
        // Deferredパスチェーン描画（スワップチェーンレンダーパスの外で実行）
        // 各パスが独自のレンダーパスを開閉する
        // ========================================

        // ViewRenderContextを構築（パスチェーン対応の新フロー）
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        ViewRenderContext viewContext;
        viewContext.CommandList = m_CommandList.get();
        // Parallel のパスは仕事の番号 1 以降のリストへ同時に記録する（--render-parallel-record=off なら全パスを 0 番へ）。
        // グラフは実行の後に viewContext.CommandList を最後に受け取ったリストへ差し替えるので、以後の記録はそこへ続ける。
        if (!m_bParallelRecordResolved)
        {
            m_bParallelRecordResolved = true;
            m_bParallelRecordEnabled = ResolveParallelRecordEnabled();
            NORVES_LOG_INFO("RenderingCoordinator",
                            "RENDER_PARALLEL_RECORD enabled=%d",
                            m_bParallelRecordEnabled ? 1 : 0);
        }
        bool bRecordListTimestamps = false;
#if NORVES_ENABLE_STATS
        bRecordListTimestamps = bTraceActive;
#endif
        JobRecordListSource recordListSource(*m_Device,
                                             m_JobCommandLists,
                                             frameIndex,
                                             packet->FrameNumber,
                                             bRecordListTimestamps);
        viewContext.RecordListSource = m_bParallelRecordEnabled ? &recordListSource : nullptr;
        viewContext.InstanceDataBuffer = instanceDataBuffer;
        viewContext.Device = m_Device.get();
        viewContext.TransientPool = &m_TransientPool;
        viewContext.SharedResources = &m_Screen.GetSharedResourceRegistry();
        viewContext.CurrentRenderPass = m_RenderPass.get();
        viewContext.CurrentFramebuffer = m_SwapChainFramebuffers[imageIndex].get();
        viewContext.bRenderPassActive = false; // Deferredパスは独自のレンダーパスを使用
        viewContext.FrameIndex = frameIndex;
        viewContext.FrameNumber = packet->FrameNumber;
        viewContext.RenderFrameSerial = ++m_RenderFrameSerial;
        viewContext.CompletedRenderFrameSerial = m_CompletedRenderFrameSerial;
        viewContext.FrameResources = frameResources;
        viewContext.ScreenWidth = swapChain->GetWidth();
        viewContext.ScreenHeight = swapChain->GetHeight();
        viewContext.RenderWidth = m_RenderWidth;
        viewContext.RenderHeight = m_RenderHeight;
        viewContext.DeltaTime = m_PreviousCompletedTotalFrameTimeMs * 0.001f;
        viewContext.TotalTime = packet->TotalTime;
        viewContext.bDeterministicCapture = packet->bDeterministicCapture;
        viewContext.bTemporalEpochStart = packet->bTemporalEpochStart;
        viewContext.TemporalFrameIndex = packet->bDeterministicCapture ? packet->EpochFrameIndex : packet->FrameNumber;
        if (packet->bDeterministicCapture)
        {
            viewContext.DeltaTime = packet->DeltaTime;
        }
        if (m_RenderResources)
        {
            viewContext.Resources.Gpu = &m_RenderResources->Gpu();
            viewContext.Resources.Textures = &m_RenderResources->Textures();
            viewContext.Resources.Materials = &m_RenderResources->Materials();
            viewContext.SkinnedMeshes = &m_RenderResources->SkinnedMeshes();
            viewContext.Resources.Meshes = &m_RenderResources->Meshes();
            viewContext.Resources.MegaGeometry = &m_RenderResources->MegaGeometry();
        }
        viewContext.ShaderMgr = &m_ShaderManager;
        viewContext.Capabilities = &m_Device->GetCapabilities();
        viewContext.Renderer = &m_SceneRenderer;
        viewContext.PendingFrameCommands = &pendingFrameCommands;
        viewContext.Graph = &m_RenderGraph;
        viewContext.SceneRevision = packet->SceneRevision;
        viewContext.LightRevision = packet->LightRevision;
        viewContext.bRTGIEnabled = packet->bRTGIEnabled;
        // RTGIの光線は影を落とす物体だけを調べる。影を落とす物体がなければ従来どおりfallbackする。
        viewContext.bRTGITLASAvailable = packet->HasCompleteRayTracingScene() &&
                                         packet->RayTracingScene.HasShadowCasters();
        viewContext.RTGICapability = MakeRTGIRayQueryCapability(m_Device->GetCapabilities());

        // フレームパケットからスナップショットを設定（RenderThread読み取り専用）
        viewContext.MainCamera = packet->bHasMainCamera ? &packet->Scene.MainCamera : nullptr;
        viewContext.PreviousMainCamera = packet->bHasPreviousMainCamera
                                             ? &packet->PreviousMainCamera
                                             : nullptr;
        viewContext.SnapshotScene = &packet->Scene;
        viewContext.SnapshotRayTracingScene = &packet->RayTracingScene;
        viewContext.SnapshotDeltaTime = packet->DeltaTime;
        viewContext.PreviousObjectStateFrameNumber = renderedObjectHistory.PreviousFrameNumber;
        viewContext.bPreviousObjectStateComplete = renderedObjectHistory.bComplete;
        viewContext.PreviousCameraFrameNumber = packet->bHasPreviousMainCamera && packet->FrameNumber > 0u
                                                    ? packet->FrameNumber - 1u
                                                    : UINT64_MAX;
        viewContext.SkyAtmosphereSnapshot = packet->Scene.SkyAtmosphere;
        viewContext.SnapshotDrawCommandSource = &packet->DrawCommands;
        viewContext.SnapshotDrawCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                      packet->DrawCommandRange);
        viewContext.SnapshotDebugLineVertices = &packet->DebugLineVertices;
        viewContext.SnapshotFoliage = &packet->Scene.Foliage;
        viewContext.SnapshotOpaqueCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                        packet->OpaqueCommandRange);
        viewContext.SnapshotTransparentCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                             packet->TransparentCommandRange);
        viewContext.SnapshotSkinnedMeshFrameLeases = &packet->SkinnedMeshFrameLeases;
        // 差分を持つパケットでは、Mesh・Skinned・Light の全量は RenderThread の写しにある
        viewContext.SnapshotMeshProxies =
            packet->MeshDelta.bActive ? &m_MeshMirror.GetProxies() : &packet->Scene.MeshProxies;
        viewContext.SnapshotInstanceData = &packet->InstanceData;
        viewContext.SnapshotSkinnedMeshProxies = packet->SkinnedMeshDelta.bActive
                                                     ? &m_SkinnedMeshMirror.GetProxies()
                                                     : &packet->Scene.SkinnedMeshProxies;
        viewContext.SnapshotLightProxies =
            packet->LightDelta.bActive ? &m_LightMirror.GetProxies() : &packet->Scene.LightProxies;
        viewContext.SnapshotPointShadows = &packet->PointShadows;
        if (packet->MegaDelta.bActive)
        {
            viewContext.SnapshotMegaGeometryProxies = &m_MegaMirror.GetProxies();
            viewContext.SnapshotMegaChangedSlots = &m_MegaMirror.GetChangedSlots();
            viewContext.bSnapshotMegaAllChanged = m_MegaMirror.AreAllChanged();
            viewContext.SnapshotMegaRevision = m_MegaMirror.GetRevision();
            viewContext.SnapshotMegaMirror = &m_MegaMirror;
            viewContext.bSnapshotMegaVerify = packet->MegaDelta.bVerify;
        }
        else
        {
            viewContext.SnapshotMegaGeometryProxies = &packet->Scene.MegaGeometryProxies;
        }

        PresentationComposer presentationComposer;
        const auto &screenViews = m_Screen.GetViews();
        PresentationComposeRequest presentationRequest;
        presentationRequest.Context = &viewContext;
        presentationRequest.Renderer = &m_SceneRenderer;
        presentationRequest.CommandList = m_CommandList.get();
        presentationRequest.ClearRenderPass = m_RenderPass;
        presentationRequest.LoadRenderPass = m_PresentationLoadRenderPass;
        presentationRequest.ClearFramebuffer = m_SwapChainFramebuffers[imageIndex];
        presentationRequest.LoadFramebuffer = m_PresentationLoadFramebuffers[imageIndex];
        presentationRequest.BlitPipeline = m_BlitPipeline;
        presentationRequest.BlitDescriptorSet = SelectFrameDescriptorSet(m_BlitDescriptorSets, frameIndex);
        presentationRequest.BlitSampler = m_BlitSampler;

        PresentationComposeRequest deferredLegacyRequest = presentationRequest;
        deferredLegacyRequest.ClearRenderPass.reset();
        deferredLegacyRequest.LoadRenderPass.reset();
        deferredLegacyRequest.ClearFramebuffer.reset();
        deferredLegacyRequest.LoadFramebuffer.reset();
        deferredLegacyRequest.BlitPipeline.reset();
        deferredLegacyRequest.BlitDescriptorSet.reset();
        deferredLegacyRequest.BlitSampler.reset();

        PresentationPassRequest graphPresentationRequest;
        graphPresentationRequest.BackBufferTexture = swapChain->GetBackBuffer(imageIndex);
        graphPresentationRequest.ClearRenderPass = m_GraphPresentationClearRenderPass;
        graphPresentationRequest.LoadRenderPass = m_GraphPresentationLoadRenderPass;
        graphPresentationRequest.ClearFramebuffer = m_GraphPresentationClearFramebuffers[imageIndex];
        graphPresentationRequest.LoadFramebuffer = m_GraphPresentationLoadFramebuffers[imageIndex];
        graphPresentationRequest.BlitPipeline = m_BlitPipeline;
        graphPresentationRequest.BlitDescriptorSet = presentationRequest.BlitDescriptorSet;
        graphPresentationRequest.BlitSampler = m_BlitSampler;

        CompositePassRequest graphCompositeRequest;
        graphCompositeRequest.VertexShader = m_BlitVertexShader;
        graphCompositeRequest.PixelShader = m_CompositeAlphaOverFragmentShader;
        graphCompositeRequest.DescriptorSet = SelectFrameDescriptorSet(m_CompositeAlphaOverDescriptorSets, frameIndex);
        graphCompositeRequest.Sampler = m_BlitSampler;

        RenderFrameExecutionRequest executionRequest;
        executionRequest.Packet = packet;
        executionRequest.Views = &screenViews;
        executionRequest.FallbackView = m_MainSceneView.get();
        executionRequest.Context = &viewContext;
        executionRequest.Renderer = &m_SceneRenderer;
        executionRequest.CommandList = m_CommandList.get();
        executionRequest.PendingFrameCommands = &pendingFrameCommands;
        executionRequest.Presentation = &presentationComposer;
        executionRequest.PresentationRequest = deferredLegacyRequest;
        executionRequest.PresentationGraphPass = &m_PresentationPass;
        executionRequest.GraphPresentationRequest = graphPresentationRequest;
        executionRequest.CompositeGraphPass = &m_CompositePass;
        executionRequest.GraphCompositeRequest = graphCompositeRequest;
        executionRequest.DebugDumpCapture = debugDumpClaim.IsClaimed() &&
                                                  debugDumpCapture.TargetSceneViewId != UINT32_MAX
                                              ? &debugDumpCapture
                                              : nullptr;

        m_PresentationPass.SetDeferBlit(true);
        RenderFrameExecutor frameExecutor;
        RenderFrameExecutionResult executionResult = frameExecutor.Execute(executionRequest);
        // グラフの後の記録（overlay・表示のコピー・保留のコマンド・撮影のコピー）とリストの終了
        frameScopes.Begin("RT.RecordTail");

        if (debugDumpClaim.IsClaimed() && m_Diagnostics)
        {
            if (!m_RenderGraph.IsDebugDumpSupported())
            {
                m_Diagnostics->PublishRenderGraphDebugDump(
                    Container::String{},
                    packet->FrameNumber,
                    false,
                    "RenderGraph debug dump is unavailable in this build");
                debugDumpClaim.MarkProcessed();
            }
            else if (debugDumpCapture.bCaptured)
            {
                m_Diagnostics->PublishRenderGraphDebugDump(
                    debugDumpCapture.Text,
                    packet->FrameNumber,
                    true,
                    Container::String{});
                debugDumpClaim.MarkProcessed();
            }
        }
        renderStats.RenderGraphBarrierCount = m_RenderGraph.GetLastCompiledBarrierCount();
        renderStats.RenderGraphTransientAcquireCount = m_RenderGraph.GetLastTransientAcquireCount();
        // 計算スキニングから外したインスタンスの数（フレームごと。このフレームに Declare された SceneView のパスの数を足す）
        AssignSkinningComputeStats(renderStats, m_Views, viewContext.ResolveRenderFrameSerial());

        RHI::TexturePtr finalPresentationTexture;
        if (executionResult.bComposite)
        {
            m_RenderGraph.TryGetLastOutputTexture(RenderGraphResourceNames::CompositeColor,
                                                  finalPresentationTexture);
        }
        if (!finalPresentationTexture)
        {
            finalPresentationTexture = m_PresentationPass.GetLastResult().InputTexture;
        }
        if (!finalPresentationTexture && viewContext.SharedResources)
        {
            finalPresentationTexture = viewContext.SharedResources->GetTexturePtr(
                RenderGraphResourceNames::PresentationColor);
            if (!finalPresentationTexture)
            {
                finalPresentationTexture = viewContext.SharedResources->GetTexturePtr(
                    RenderGraphResourceNames::ToneMappedColor);
            }
        }

        // ========================================
        // overlay seam（OETF前のRGBA16F PresentationColorへ描画）
        // ========================================
        RHI::RenderPassPtr overlayRenderPass;
        RHI::FramebufferPtr overlayFramebuffer;
        if (packet && !packet->OverlayPasses.empty() &&
            finalPresentationTexture &&
            finalPresentationTexture->GetFormat() == RHI::Format::R16G16B16A16_FLOAT)
        {
            overlayFramebuffer = m_PresentationPass.AcquireOverlayFramebuffer(
                m_Device,
                swapChain->GetCurrentFrameIndex(),
                swapChain->GetMaxFramesInFlight(),
                finalPresentationTexture);
            overlayRenderPass = m_PresentationPass.GetOverlayRenderPass();
        }

        if (packet && !packet->OverlayPasses.empty())
        {
            if (!overlayRenderPass || !overlayFramebuffer)
            {
                LOG_WARNING("RenderingCoordinator", "Overlay skipped: RGBA16F PresentationColor target unavailable");
            }
            else
            {
                viewContext.OverlayPacketSlotIndex = m_PacketManager.GetSlotIndex(packet);
                viewContext.OverlayLoadRenderPass = overlayRenderPass.get();
                viewContext.OverlayLoadFramebuffer = overlayFramebuffer.get();

                for (IViewPass *overlayPass : packet->OverlayPasses)
                {
                    if (!overlayPass || !overlayPass->IsEnabled())
                    {
                        continue;
                    }

                    if (!overlayPass->IsInitialized())
                    {
                        if (!overlayPass->Initialize(viewContext))
                        {
                            overlayPass->SetEnabled(false);
                            continue;
                        }
                    }

                    RHI::FrameResourceWriteScope overlayWriteScope(overlayPass->GetName());
                    overlayPass->Setup(viewContext);
                    overlayPass->Execute(viewContext);
                }
            }
        }

        bool bPresentationBlitRecorded = false;
        {
            RHI::FrameResourceWriteScope presentationWriteScope("PresentationPass");
            bPresentationBlitRecorded = m_PresentationPass.RecordDeferredBlit(viewContext);
        }
        if (!bPresentationBlitRecorded &&
            finalPresentationTexture &&
            presentationRequest.ClearRenderPass &&
            presentationRequest.ClearFramebuffer &&
            m_BlitPipeline &&
            presentationRequest.BlitDescriptorSet &&
            m_BlitSampler)
        {
            // このフレームのスロットの組だけを束ね直す（前のフレームの GPU が読む組は触らない）
            const RHI::DescriptorSetPtr& blitDescriptorSet = presentationRequest.BlitDescriptorSet;
            blitDescriptorSet->BindTexture(0, finalPresentationTexture);
            blitDescriptorSet->BindSampler(0, m_BlitSampler);
            blitDescriptorSet->Update();
            viewContext.EnqueueFullscreenPass(presentationRequest.ClearRenderPass,
                                              presentationRequest.ClearFramebuffer,
                                              viewContext.GetActiveOutputViewport(),
                                              viewContext.GetActiveOutputScissor(),
                                              m_BlitPipeline,
                                              blitDescriptorSet);
            bPresentationBlitRecorded = true;
        }
        m_PresentationPass.SetDeferBlit(false);

        if (bPresentationBlitRecorded)
        {
            executionResult.PresentationBlitCount = 1;
        }

        // グラフの後の記録（保留のコマンド・撮影のコピー・VT の要求のバリア）は、最後に記録したリストへ続ける
        // （Parallel のパスが無ければ 0 番のまま）。
        RHI::ICommandList* frameTailCommandList = viewContext.CommandList ? viewContext.CommandList : m_CommandList.get();
        if (!pendingFrameCommands.empty())
        {
            m_SceneRenderer.ExecuteFrameCommands(pendingFrameCommands, frameTailCommandList);
            pendingFrameCommands.clear();
        }

        // Presentation surfaceのtransfer責務をcapture metadataへ記録する。
        const RHI::PresentationSurfaceDesc presentationSurface =
            swapChain->GetPresentationSurfaceDesc();
        const RHI::PresentationEncodePath presentationEncodePath =
            RHI::GetPresentationEncodePath(swapChain->GetFormat());
        if (bPresentationBlitRecorded &&
            claimedCaptureRequest.SourceKind == FrameCaptureSourceKind::PresentationColor)
        {
            FrameCaptureSource& presentationSource = executionResult.CaptureSources.PresentationColor;
            presentationSource.Texture = finalPresentationTexture;
            presentationSource.CurrentState = RHI::ResourceState::ShaderResource;
            presentationSource.RestoreState = RHI::ResourceState::ShaderResource;
            presentationSource.FrameNumber = packet->FrameNumber;
            presentationSource.ColorSpace = presentationSurface.ColorSpace;
            presentationSource.Transfer = presentationSurface.Transfer;
            presentationSource.bHardwareSrgbEncode =
                presentationEncodePath == RHI::PresentationEncodePath::HardwareSRGB;
            presentationSource.bShaderSrgbEncode =
                presentationEncodePath == RHI::PresentationEncodePath::ShaderOETF;
        }

        // BackBuffer は PresentationPass と全 overlay の後、command list 終了直前に取得する。
        if (bPresentationBlitRecorded &&
            claimedCaptureRequest.SourceKind == FrameCaptureSourceKind::BackBuffer)
        {
            FrameCaptureSource& backBufferSource = executionResult.CaptureSources.BackBuffer;
            backBufferSource.Texture = swapChain->GetCurrentBackBuffer();
            backBufferSource.CurrentState = RHI::ResourceState::Present;
            backBufferSource.RestoreState = RHI::ResourceState::Present;
            backBufferSource.FrameNumber = packet->FrameNumber;
            backBufferSource.ColorSpace = presentationSurface.ColorSpace;
            backBufferSource.Transfer = presentationSurface.Transfer;
            backBufferSource.bHardwareSrgbEncode =
                presentationEncodePath == RHI::PresentationEncodePath::HardwareSRGB;
            backBufferSource.bShaderSrgbEncode =
                presentationEncodePath == RHI::PresentationEncodePath::ShaderOETF;
        }

        if (m_FrameCaptureReadbackHelper)
        {
            captureRecordStatus = m_FrameCaptureReadbackHelper->TryRecordCopy(
                frameIndex,
                frameTailCommandList,
                claimedCaptureRequest,
                executionResult.CaptureSources);
            if (captureRecordStatus == FrameCaptureRecordStatus::PublishedFailure ||
                captureRecordStatus == FrameCaptureRecordStatus::Deferred)
            {
                captureAssignment.MarkResolved();
            }
        }
        executionResult.CaptureSources.Reset();

        // VT の要求のバッファへのシェーダーの書き込みを、ホストの読み取りへ見せる（最後の書き込みの後・提出の前）
        if (m_RenderResources)
        {
            m_RenderResources->RecordVirtualTextureFeedbackBarrier(*frameTailCommandList);
        }

        // コマンド録画終了（0 番と、区間ごとの記録に渡したリスト。FrameGPU は合わせる側が先頭の開始から末尾の終了にする）
#if NORVES_ENABLE_STATS
        if (bTraceActive && m_CommandList->SupportsGPUTimestamps())
        {
            m_CommandList->EndGPUTimestamp();
            m_CommandList->EndGPUTimestampFrame();
        }
#endif
        m_CommandList->End();
        recordListSource.CloseAcquiredLists();
        // 区間ごとの記録に渡したリストの数は、変わったときだけ出す（計測と撮影の確認用）
        if (recordListSource.GetAcquiredCount() != m_LastLoggedParallelRecordListCount)
        {
            m_LastLoggedParallelRecordListCount = recordListSource.GetAcquiredCount();
            NORVES_LOG_INFO("RenderingCoordinator",
                            "RENDER_PARALLEL_RECORD_LISTS count=%u frame=%llu",
                            m_LastLoggedParallelRecordListCount,
                            static_cast<unsigned long long>(packet->FrameNumber));
        }

        // SceneRendererフレーム終了
        m_SceneRenderer.EndFrame();
        m_TransientPool.EndFrame();

        // 統計更新
        const auto &rendererStats = m_SceneRenderer.GetStats();
        packet->Stats.SkinnedGBufferRecordedDraws = rendererStats.SkinnedGBufferDrawCallCount;
        packet->Stats.SkinnedShadowRecordedDraws = rendererStats.SkinnedShadowDrawCallCount;
        renderStats.DrawCalls = rendererStats.DrawCallCount;
        renderStats.TrianglesRendered = rendererStats.TriangleCount;

        // 仕事の番号ごとのリストを 1 回でサブミット＆Present（スワップチェーンが RT.Submit・RT.Present として取る）
        frameScopes.End();
        const RHI::SwapChainEndFrameResult endFrameResult = SubmitJobCommandLists(*swapChain, m_JobCommandLists);
        frameScopes.Begin("RT.FinishFrame");
        if (captureRecordStatus == FrameCaptureRecordStatus::Recorded &&
            endFrameResult.SubmissionSerial != 0 &&
            m_FrameCaptureReadbackHelper &&
            m_FrameCaptureReadbackHelper->CommitRecordedCopy(claimedCaptureRequest))
        {
            captureAssignment.MarkResolved();
        }
        // 提出できたら通し番号を持ち主に結び付け（その完了で回収する）、できなければ中止として次の開始で回収する
        m_FrameResourceOwners.CommitSubmitted(endFrameResult.SubmissionSerial);
        if (m_RenderResources)
        {
            if (endFrameResult.SubmissionSerial != 0)
            {
                m_RenderResources->SkinnedMeshes().CommitSubmittedFrame(
                    endFrameResult.SubmissionSerial);
                m_RenderResources->CommitRetireFrame(endFrameResult.SubmissionSerial);
            }
            else
            {
                m_RenderResources->SkinnedMeshes().AbortFrame();
                m_RenderResources->AbortRetireFrame();
            }
        }
        if (endFrameResult.SubmissionSerial != 0)
        {
            m_SubmittedRenderFrames.push_back({viewContext.RenderFrameSerial, endFrameResult.SubmissionSerial});
            packetConfirmation.NoteSubmitted(packet->SendSerial, endFrameResult.SubmissionSerial);
        }
        if (endFrameResult.HasError())
        {
            ThrowSwapChainEndFrameError(endFrameResult.Status);
        }

        // 鍵で持ち続けるレンダーパス・フレームバッファ・パイプラインのフレームを進め、見えなくなった鍵を手放す
        m_Device->AdvanceObjectCacheFrame();
#if NORVES_BUILD_DEVELOPMENT
        {
            // このフレームで実際に作った数（鍵に合う物が無かった数）。作ったフレームだけ出す
            const RHI::ObjectCacheStats objectCacheStats = m_Device->GetObjectCacheStats();
            const uint64_t createdFramebuffers =
                objectCacheStats.CreatedFramebuffers - m_LastLoggedObjectCacheStats.CreatedFramebuffers;
            const uint64_t createdRenderPasses =
                objectCacheStats.CreatedRenderPasses - m_LastLoggedObjectCacheStats.CreatedRenderPasses;
            const uint64_t createdPipelines =
                objectCacheStats.CreatedPipelines - m_LastLoggedObjectCacheStats.CreatedPipelines;
            if (createdFramebuffers != 0 || createdRenderPasses != 0 || createdPipelines != 0)
            {
                NORVES_LOG_INFO("RenderingCoordinator",
                                "RHI_CREATE frame=%llu RHI.Create.Framebuffer=%llu RHI.Create.RenderPass=%llu RHI.Create.Pipeline=%llu",
                                static_cast<unsigned long long>(packet->FrameNumber),
                                static_cast<unsigned long long>(createdFramebuffers),
                                static_cast<unsigned long long>(createdRenderPasses),
                                static_cast<unsigned long long>(createdPipelines));
            }
            m_LastLoggedObjectCacheStats = objectCacheStats;
        }
#endif

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            auto renderFrameEndTime = std::chrono::high_resolution_clock::now();
            renderStats.RenderFrameTimeMs =
                std::chrono::duration<float, std::milli>(renderFrameEndTime - renderFrameStartTime).count();
            renderStats.TotalFrameTimeMs = std::max(std::max(renderStats.GameThreadTimeMs, renderStats.RenderThreadTimeMs),
                                                    std::max(renderStats.RenderFrameTimeMs, renderStats.GPUTimeMs));
            statsManager.SetRenderFrameTimeMs(renderStats.RenderFrameTimeMs);
            statsManager.UpdateRenderingStats(renderStats);
        }
#endif

        const bool bPresentationDirty = swapChain->ConsumePresentationDirty();
        const uint32_t presentWidth = swapChain->GetWidth();
        const uint32_t presentHeight = swapChain->GetHeight();
        const uint32_t presentBackBufferCount = swapChain->GetBufferCount();
        const RHI::Format presentFormat = swapChain->GetFormat();
        if (bPresentationDirty ||
            presentWidth != m_Width ||
            presentHeight != m_Height ||
            presentFormat != m_SwapChainFormat ||
            presentBackBufferCount != m_SwapChainFramebuffers.size())
        {
            m_Width = presentWidth;
            m_Height = presentHeight;
            m_SwapChainFormat = presentFormat;
            m_bSwapChainFramebuffersReady = false;
            m_SwapChainFramebuffers.clear();
            m_PresentationLoadFramebuffers.clear();
            m_GraphPresentationClearFramebuffers.clear();
            m_GraphPresentationLoadFramebuffers.clear();
            m_DepthTexture.reset();

            UpdateRenderResolution(presentWidth, presentHeight);
            RequestCanvasCameraSync();
            for (auto &view : m_Views)
            {
                if (view)
                {
                    view->Resize(presentWidth, presentHeight);
                }
            }
        }

        // 自動露出の測定は最初の SceneView のものを GameThread へ渡す（View の中身は RenderThread 専用）
        for (const auto &view : m_Views)
        {
            const auto *sceneView = dynamic_cast<const SceneView *>(view.get());
            AutoExposureMeasurement measurement;
            if (sceneView && sceneView->TryGetAutoExposureMeasurement(measurement))
            {
                m_LatestAutoExposure = measurement;
                break;
            }
        }

        if (m_Diagnostics)
        {
            RenderingCoordinatorStatsSnapshot statsSnapshot;
            statsSnapshot.SkinnedGBufferRecordedDraws = packet->Stats.SkinnedGBufferRecordedDraws;
            statsSnapshot.SkinnedShadowRecordedDraws = packet->Stats.SkinnedShadowRecordedDraws;
            statsSnapshot.Stats = renderStats;
            statsSnapshot.GeneratedDrawCommandCount = packet->GeneratedDrawCommandCount;
            statsSnapshot.AutoExposure = m_LatestAutoExposure;
            statsSnapshot.bRenderFrameCompleted = true;
            statsSnapshot.bGameThreadTimingsAvailable = bGameThreadTimingsAvailable;
#if NORVES_ENABLE_STATS
            statsSnapshot.bRenderFrameTimingAvailable = bTraceActive;
            statsSnapshot.bGPUTimeAvailable = bTraceActive && m_bLatestCompletedGPUTimeValid;
            statsSnapshot.bTotalFrameTimeAvailable = bTraceActive;
#endif
            m_Diagnostics->PublishStatsSnapshot(statsSnapshot);
        }

#if NORVES_ENABLE_STATS
        if (bTraceActive)
        {
            m_PreviousCompletedTotalFrameTimeMs = renderStats.TotalFrameTimeMs;
        }
#endif
    }

    void RenderingCoordinator::ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands)
    {
        if (!m_bInitialized || commands.empty())
        {
            return;
        }

        // SceneRenderer経由でRHI描画コールを発行
        m_SceneRenderer.ExecuteDrawCommands(commands, m_CommandList.get());
    }

    void RenderingCoordinator::SubmitToGPU()
    {
        // Screen::EndFrame()内でSwapChain::EndFrame(commandList)がsubmit+presentを行うため、
        // この関数は将来的なマルチスレッドレンダリング用に予約
    }

    void RenderingCoordinator::Present()
    {
        // Screen::EndFrame()内でSwapChain::EndFrame(commandList)がsubmit+presentを行うため、
        // この関数は将来的なマルチスレッドレンダリング用に予約
    }

    void RenderingCoordinator::ReleasePacket(FramePacket *packet)
    {
        if (packet)
        {
            m_PacketManager.FinishRead(packet);
        }
    }

    Container::TSharedPtr<SceneView> RenderingCoordinator::CreateSceneView(const SceneViewSettings &settings)
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        auto sceneView = Container::MakeShared<SceneView>();
        if (!sceneView->Initialize(settings))
        {
            return nullptr;
        }

        m_Screen.AddView(sceneView, 0);
        m_Views.push_back(sceneView);
        return sceneView;
    }

    Container::TSharedPtr<CanvasView> RenderingCoordinator::CreateCanvasView()
    {
        if (!m_bInitialized)
        {
            return nullptr;
        }

        if (m_CanvasView)
        {
            return m_CanvasView;
        }

        if (m_bFrameSubmissionStarted ||
            m_CurrentPacket ||
            m_PacketManager.HasInflightPackets())
        {
            NORVES_LOG_WARNING("RenderingCoordinator",
                               "CreateCanvasView rejected because frame submission has already started");
            return nullptr;
        }

        if (!EnsureCompositeAlphaOverResources())
        {
            return nullptr;
        }

        ViewSettings settings;
        settings.Type = ViewType::UI;
        settings.Width = GetCanvasWidth();
        settings.Height = GetCanvasHeight();
        settings.bClearColor = true;
        settings.ClearColor[0] = 0.0f;
        settings.ClearColor[1] = 0.0f;
        settings.ClearColor[2] = 0.0f;
        settings.ClearColor[3] = 0.0f;
        settings.bClearDepth = false;

        auto canvasView = Container::MakeShared<CanvasView>();
        if (!canvasView->Initialize(settings))
        {
            return nullptr;
        }
        canvasView->SetBoardInstanceBatchingEnabled(m_bBoardInstanceBatchingEnabled);

        CameraProxy canvasCamera;
        canvasCamera.Projection = ProjectionType::Orthographic;
        canvasCamera.CullingMask = RenderLayer::UI;
        canvasCamera.NearPlane = 0.0f;
        canvasCamera.FarPlane = 1.0f;
        canvasCamera.OrthoWidth = static_cast<float>(GetCanvasWidth());
        canvasCamera.OrthoHeight = static_cast<float>(GetCanvasHeight());
        canvasCamera.Viewport.X = 0.0f;
        canvasCamera.Viewport.Y = 0.0f;
        canvasCamera.Viewport.Width = static_cast<float>(GetCanvasWidth());
        canvasCamera.Viewport.Height = static_cast<float>(GetCanvasHeight());
        canvasCamera.Viewport.MinDepth = 0.0f;
        canvasCamera.Viewport.MaxDepth = 1.0f;
        m_CanvasCameraId = RegisterCamera(canvasCamera);
        const CameraProxy *registeredCamera = FindCamera(m_CanvasCameraId);
        auto canvasViewport = canvasView->GetMainViewport();
        if (canvasViewport && registeredCamera)
        {
            canvasViewport->SetCameraId(m_CanvasCameraId);
            canvasViewport->SetCamera(*registeredCamera);
        }
        m_bCanvasCameraSyncPending.Store(false);

        m_Screen.AddView(canvasView, 1);
        m_Views.push_back(canvasView);
        m_CanvasView = canvasView;
        if (Engine::GEngine)
        {
            Engine::GEngine->GetWorld().SetScreenSpaceBoardSink(m_CanvasView.get());
        }
        return m_CanvasView;
    }

    void RenderingCoordinator::SetBoardInstanceBatchingEnabled(bool bEnabled)
    {
        m_bBoardInstanceBatchingEnabled = bEnabled;
        if (m_CanvasView)
        {
            m_CanvasView->SetBoardInstanceBatchingEnabled(bEnabled);
        }
    }

    void RenderingCoordinator::DestroyView(Container::TSharedPtr<View> view)
    {
        if (!m_bInitialized || !view)
        {
            return;
        }

        // Screenから削除
        m_Screen.RemoveView(view);

        // リストから削除
        auto it = std::find(m_Views.begin(), m_Views.end(), view);
        if (it != m_Views.end())
        {
            const bool bDestroyingCanvasView = *it == m_CanvasView;
            if (bDestroyingCanvasView && m_Device)
            {
                m_Device->WaitIdle();
            }

            (*it)->Shutdown();
            if (bDestroyingCanvasView)
            {
                if (Engine::GEngine)
                {
                    Engine::GEngine->GetWorld().SetScreenSpaceBoardSink(nullptr);
                }
                m_CanvasView.reset();
                m_CanvasCameraId = 0;
                m_bCanvasCameraSyncPending.Store(false);
            }
            if (*it == m_MainSceneView)
            {
                m_MainSceneView.reset();
            }
            m_Views.erase(it);
        }
    }

    void RenderingCoordinator::SetMainCamera(const CameraProxy &camera)
    {
        m_MainCamera = camera;
        m_MainCamera.SourceCameraId = ResolveSourceCameraId(camera);
        if (!m_MainCamera.IsValid())
        {
            m_MainCamera.Viewport.X = 0.0f;
            m_MainCamera.Viewport.Y = 0.0f;
            m_MainCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
            m_MainCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
            m_MainCamera.Viewport.MinDepth = 0.0f;
            m_MainCamera.Viewport.MaxDepth = 1.0f;
        }

        if (m_MainCameraId == 0)
        {
            m_MainCameraId = RegisterCamera(m_MainCamera);
        }
        else
        {
            UpdateCamera(m_MainCameraId, m_MainCamera);
        }
        m_MainCamera.CameraId = m_MainCameraId;

        if (m_MainSceneView)
        {
            auto mainViewport = m_MainSceneView->GetMainViewport();
            if (mainViewport)
            {
                mainViewport->SetCamera(m_MainCamera);
            }
        }

        m_bCameraSet = true;
    }

    void RenderingCoordinator::SetSkyAtmosphere(const SkyAtmosphereParameters& parameters)
    {
        m_SkyAtmosphere = parameters;
    }

    void RenderingCoordinator::SetDDGIVolumeParameters(
        const DDGIVolumeParameters& parameters)
    {
        m_DDGIVolume = SanitizeDDGIVolumeParameters(parameters);
        if (m_Device)
        {
            const RHI::RayTracingCapabilities& rayTracingCapabilities =
                m_Device->GetCapabilities().RayTracing;
            m_DDGIVolume = SanitizeDDGIVolumeParametersForRHI(
                m_DDGIVolume,
                rayTracingCapabilities.bAccelerationStructure,
                rayTracingCapabilities.bRayQuery);
        }
    }

    void RenderingCoordinator::SetRTGIEnabled(bool bEnabled)
    {
        m_bRTGIEnabled = bEnabled;
    }

    void RenderingCoordinator::SetVolumetricFogParameters(
        const VolumetricFogParameters& parameters)
    {
        m_VolumetricFog = SanitizeVolumetricFogParameters(parameters);
    }

    void RenderingCoordinator::SetStaticEnvironmentIntensityScale(float scale)
    {
        m_StaticEnvironmentIntensityScale = std::isfinite(scale) && scale >= 0.0f ? scale : 1.0f;
    }

    void RenderingCoordinator::SetStaticEnvironmentMaxRadiance(float maxRadiance)
    {
        m_StaticEnvironmentMaxRadiance =
            std::isfinite(maxRadiance) && maxRadiance >= 0.0f ? maxRadiance : 0.0f;
    }

    void RenderingCoordinator::SetStaticEnvironmentUniformRadiance(bool bEnabled, const Math::Vector3& radiance)
    {
        const bool bFinite = std::isfinite(radiance.x) && std::isfinite(radiance.y) && std::isfinite(radiance.z);
        m_bStaticEnvironmentUniform = bEnabled;
        m_StaticEnvironmentUniformRadiance =
            bEnabled && bFinite ? Math::Vector3(std::max(radiance.x, 0.0f), std::max(radiance.y, 0.0f),
                                                std::max(radiance.z, 0.0f))
                                : Math::Vector3::Zero;
    }

    void RenderingCoordinator::SetSunShadowMaxDistance(float distance)
    {
        m_SunShadowMaxDistance = std::isfinite(distance) && distance > 0.0f ? distance : 0.0f;
    }

    void RenderingCoordinator::SetAerialPerspective(const AerialPerspectiveParameters& parameters)
    {
        m_AerialPerspective = SanitizeAerialPerspectiveParameters(parameters);
    }

    void RenderingCoordinator::SetCloudLayer(const CloudLayerParameters& parameters)
    {
        m_CloudLayer = SanitizeCloudLayerParameters(parameters);
    }

    void RenderingCoordinator::SetFoliage(const FoliageScene& foliage)
    {
        m_Foliage = foliage;
    }

    uint64_t RenderingCoordinator::RegisterCamera(const CameraProxy &camera)
    {
        const uint64_t cameraId = m_NextCameraId++;
        CameraProxy storedCamera = camera;
        storedCamera.SourceCameraId = ResolveSourceCameraId(camera);
        storedCamera.CameraId = cameraId;
        m_Cameras[cameraId] = storedCamera;
        return cameraId;
    }

    bool RenderingCoordinator::UpdateCamera(uint64_t cameraId, const CameraProxy &camera)
    {
        if (cameraId == 0)
        {
            return false;
        }

        auto it = m_Cameras.find(cameraId);
        if (it == m_Cameras.end())
        {
            return false;
        }

        CameraProxy storedCamera = camera;
        storedCamera.SourceCameraId = ResolveSourceCameraId(camera);
        storedCamera.CameraId = cameraId;
        it->second = storedCamera;
        return true;
    }

    const CameraProxy *RenderingCoordinator::FindCamera(uint64_t cameraId) const
    {
        if (cameraId == 0)
        {
            return nullptr;
        }

        auto it = m_Cameras.find(cameraId);
        if (it == m_Cameras.end())
        {
            return nullptr;
        }

        return &it->second;
    }

    void RenderingCoordinator::Resize(uint32_t width, uint32_t height)
    {
        if (!m_bInitialized)
        {
            return;
        }

        LOG_INFO("RenderingCoordinator::Resize(%u, %u)", width, height);

        // GPU処理の完了を待機
        if (m_Device)
        {
            m_Device->WaitIdle();
        }
        m_PresentationPass.InvalidateOverlayResources();

        // リサイズ前にWriting中のパケットをキャンセル
        // （新しいフレームバッファが確保されるまでGTの書き込みを止める）
        m_PacketManager.CancelInflightWrites();

        // Readyな未消費パケットも解放（リサイズ後はフレームデータが無効になるため）
        m_PacketManager.DrainUnconsumedPackets();

        m_Width = width;
        m_Height = height;
        UpdateRenderResolution(width, height);
        m_bCanvasCameraSyncPending.Store(false);
        UpdateCanvasCameraForRenderResolution();

        // Screenのリサイズ（SwapChainリサイズを含む）
        m_Screen.Resize(width, height);

        // フレームバッファの再作成（デプスバッファも含む）
        if (!RecreateSwapChainPresentationResources())
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate swapchain presentation resources during resize");
        }

        // 各Viewのリサイズ
        for (auto &view : m_Views)
        {
            if (view)
            {
                view->Resize(width, height);
            }
        }
    }

    void RenderingCoordinator::SetRenderScale(float renderScale)
    {
        float clampedRenderScale = std::clamp(renderScale, 0.5f, 1.0f);
        if (std::abs(m_RenderScale - clampedRenderScale) < 0.0001f)
        {
            return;
        }

        m_RenderScale = clampedRenderScale;
        UpdateRenderResolution(m_Width, m_Height);

        if (m_bInitialized)
        {
            Resize(m_Width, m_Height);
        }
    }

    void RenderingCoordinator::UpdateRenderResolution(uint32_t screenWidth, uint32_t screenHeight)
    {
        m_RenderWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(screenWidth) * static_cast<double>(m_RenderScale))));
        m_RenderHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(screenHeight) * static_cast<double>(m_RenderScale))));
    }

    void RenderingCoordinator::RequestCanvasCameraSync()
    {
        m_bCanvasCameraSyncPending.Store(true);
    }

    void RenderingCoordinator::ConsumePendingCanvasCameraSync()
    {
        if (m_bCanvasCameraSyncPending.Exchange(false))
        {
            UpdateCanvasCameraForRenderResolution();
        }
    }

    void RenderingCoordinator::UpdateCanvasCameraForRenderResolution()
    {
        if (m_CanvasCameraId == 0)
        {
            return;
        }

        CameraProxy canvasCamera;
        const CameraProxy *existingCamera = FindCamera(m_CanvasCameraId);
        if (existingCamera)
        {
            canvasCamera = *existingCamera;
        }

        canvasCamera.Projection = ProjectionType::Orthographic;
        canvasCamera.CullingMask = RenderLayer::UI;
        canvasCamera.NearPlane = 0.0f;
        canvasCamera.FarPlane = 1.0f;
        canvasCamera.OrthoWidth = static_cast<float>(GetCanvasWidth());
        canvasCamera.OrthoHeight = static_cast<float>(GetCanvasHeight());
        canvasCamera.Viewport.X = 0.0f;
        canvasCamera.Viewport.Y = 0.0f;
        canvasCamera.Viewport.Width = static_cast<float>(GetCanvasWidth());
        canvasCamera.Viewport.Height = static_cast<float>(GetCanvasHeight());
        canvasCamera.Viewport.MinDepth = 0.0f;
        canvasCamera.Viewport.MaxDepth = 1.0f;

        if (UpdateCamera(m_CanvasCameraId, canvasCamera))
        {
            const CameraProxy *updatedCamera = FindCamera(m_CanvasCameraId);
            if (m_CanvasView && updatedCamera)
            {
                auto canvasViewport = m_CanvasView->GetMainViewport();
                if (canvasViewport)
                {
                    canvasViewport->SetCameraId(m_CanvasCameraId);
                    canvasViewport->SetCamera(*updatedCamera);
                }
            }
        }
    }

    bool RenderingCoordinator::CreateSwapChainFramebuffers()
    {
        m_bSwapChainFramebuffersReady = false;
        m_SwapChainFramebuffers.clear();
        m_PresentationLoadFramebuffers.clear();
        m_GraphPresentationClearFramebuffers.clear();
        m_GraphPresentationLoadFramebuffers.clear();
        m_DepthTexture.reset();

        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain ||
            !m_RenderPass ||
            !m_PresentationLoadRenderPass ||
            !m_GraphPresentationClearRenderPass ||
            !m_GraphPresentationLoadRenderPass)
        {
            return false;
        }

        // デプステクスチャの作成
        RHI::TextureDesc depthDesc = RHI::TextureDesc::DepthStencil(
            swapChain->GetWidth(), swapChain->GetHeight(),
            RHI::Format::D32_FLOAT, "DepthBuffer");
        m_DepthTexture = m_Device->CreateTexture(depthDesc);
        if (!m_DepthTexture)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create depth texture");
            return false;
        }

        uint32_t imageCount = swapChain->GetBufferCount();
        m_SwapChainFramebuffers.reserve(imageCount);
        m_PresentationLoadFramebuffers.reserve(imageCount);
        m_GraphPresentationClearFramebuffers.reserve(imageCount);
        m_GraphPresentationLoadFramebuffers.reserve(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            RHI::FramebufferDesc fbDesc;
            fbDesc.colorTargets.push_back(swapChain->GetBackBuffer(i));
            fbDesc.depthStencilTarget = m_DepthTexture;
            fbDesc.renderPass = m_RenderPass;
            fbDesc.width = swapChain->GetWidth();
            fbDesc.height = swapChain->GetHeight();

            auto framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!framebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create framebuffer for swapchain image %u", i);
                return false;
            }

            m_SwapChainFramebuffers.push_back(framebuffer);

            fbDesc.renderPass = m_PresentationLoadRenderPass;
            auto loadFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!loadFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create presentation load framebuffer for swapchain image %u", i);
                return false;
            }

            m_PresentationLoadFramebuffers.push_back(loadFramebuffer);

            fbDesc.renderPass = m_GraphPresentationClearRenderPass;
            auto graphClearFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!graphClearFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation clear framebuffer for swapchain image %u", i);
                return false;
            }

            m_GraphPresentationClearFramebuffers.push_back(graphClearFramebuffer);

            fbDesc.renderPass = m_GraphPresentationLoadRenderPass;
            auto graphLoadFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!graphLoadFramebuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create graph presentation load framebuffer for swapchain image %u", i);
                return false;
            }

            m_GraphPresentationLoadFramebuffers.push_back(graphLoadFramebuffer);
        }

        LOG_INFO("Created %u swapchain framebuffers with depth buffer", imageCount);
        m_bSwapChainFramebuffersReady = true;
        return true;
    }

    bool RenderingCoordinator::RecreateSwapChainPresentationResources()
    {
        auto swapChain = m_Screen.GetSwapChain();
        if (!swapChain || !m_Device)
        {
            return false;
        }

        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = swapChain->GetFormat();
        colorAttachment.isDepthStencil = false;
        colorAttachment.clear = true;
        colorAttachment.clearColor[0] = 0.392f;
        colorAttachment.clearColor[1] = 0.584f;
        colorAttachment.clearColor[2] = 0.929f;
        colorAttachment.clearColor[3] = 1.0f;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::Present;

        RHI::AttachmentDesc depthAttachment;
        depthAttachment.format = RHI::Format::D32_FLOAT;
        depthAttachment.isDepthStencil = true;
        depthAttachment.clear = true;
        depthAttachment.clearDepth = 1.0f;
        depthAttachment.clearStencil = 0;
        depthAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        depthAttachment.storeOp = RHI::AttachmentStoreOp::DontCare;
        depthAttachment.initialState = RHI::ResourceState::Undefined;
        depthAttachment.finalState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        renderPassDesc.depthStencilAttachment = depthAttachment;
        renderPassDesc.hasDepthStencil = true;

        m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate swapchain render pass");
            return false;
        }

        RHI::AttachmentDesc loadColorAttachment = colorAttachment;
        loadColorAttachment.clear = false;
        loadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadColorAttachment.initialState = RHI::ResourceState::Present;

        RHI::AttachmentDesc loadDepthAttachment = depthAttachment;
        loadDepthAttachment.clear = false;
        loadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        loadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc loadRenderPassDesc;
        loadRenderPassDesc.colorAttachments.push_back(loadColorAttachment);
        loadRenderPassDesc.depthStencilAttachment = loadDepthAttachment;
        loadRenderPassDesc.hasDepthStencil = true;

        m_PresentationLoadRenderPass = m_Device->CreateRenderPass(loadRenderPassDesc);
        if (!m_PresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate presentation load render pass");
            return false;
        }

        RHI::AttachmentDesc graphClearColorAttachment = colorAttachment;
        graphClearColorAttachment.initialState = RHI::ResourceState::RenderTarget;

        RHI::AttachmentDesc graphLoadColorAttachment = graphClearColorAttachment;
        graphLoadColorAttachment.clear = false;
        graphLoadColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;

        RHI::AttachmentDesc graphClearDepthAttachment = depthAttachment;
        graphClearDepthAttachment.initialState = RHI::ResourceState::Undefined;

        RHI::AttachmentDesc graphLoadDepthAttachment = depthAttachment;
        graphLoadDepthAttachment.clear = false;
        graphLoadDepthAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        graphLoadDepthAttachment.initialState = RHI::ResourceState::DepthWrite;

        RHI::RenderPassDesc graphClearRenderPassDesc;
        graphClearRenderPassDesc.colorAttachments.push_back(graphClearColorAttachment);
        graphClearRenderPassDesc.depthStencilAttachment = graphClearDepthAttachment;
        graphClearRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationClearRenderPass = m_Device->CreateRenderPass(graphClearRenderPassDesc);
        if (!m_GraphPresentationClearRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate graph presentation clear render pass");
            return false;
        }

        RHI::RenderPassDesc graphLoadRenderPassDesc;
        graphLoadRenderPassDesc.colorAttachments.push_back(graphLoadColorAttachment);
        graphLoadRenderPassDesc.depthStencilAttachment = graphLoadDepthAttachment;
        graphLoadRenderPassDesc.hasDepthStencil = true;

        m_GraphPresentationLoadRenderPass = m_Device->CreateRenderPass(graphLoadRenderPassDesc);
        if (!m_GraphPresentationLoadRenderPass)
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate graph presentation load render pass");
            return false;
        }
        m_SwapChainFormat = swapChain->GetFormat();

        const RHI::PresentationSurfaceDesc presentationSurface =
            swapChain->GetPresentationSurfaceDesc();
        const RHI::PresentationEncodePath presentationEncodePath =
            RHI::GetPresentationEncodePath(m_SwapChainFormat);
        if (presentationSurface.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
            presentationSurface.Transfer != RHI::PresentationTransfer::SRGB ||
            presentationEncodePath == RHI::PresentationEncodePath::Unsupported)
        {
            NORVES_LOG_ERROR("RenderingCoordinator",
                             "Unsupported presentation surface after recreation: format=%u colorSpace=%u transfer=%u",
                             static_cast<unsigned int>(m_SwapChainFormat),
                             static_cast<unsigned int>(presentationSurface.ColorSpace),
                             static_cast<unsigned int>(presentationSurface.Transfer));
            return false;
        }

        if (m_BlitVertexShader && m_BlitFragmentShader && !m_BlitDescriptorSets.empty())
        {
            RHI::DescriptorSetDesc blitDsDesc;
            RHI::DescriptorBinding texBinding;
            texBinding.binding = 0;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(texBinding);

            RHI::DescriptorBinding encodeBinding;
            encodeBinding.binding = 1;
            encodeBinding.type = RHI::ResourceBindType::ConstantBuffer;
            encodeBinding.stages = RHI::ShaderStage::Pixel;
            blitDsDesc.bindings.push_back(encodeBinding);

            RHI::PresentationEncodeParams presentationParams;
            presentationParams.EncodePath =
                presentationEncodePath == RHI::PresentationEncodePath::ShaderOETF ? 1u : 0u;
            RHI::BufferDesc presentationParamsDesc(sizeof(RHI::PresentationEncodeParams),
                                                    RHI::ResourceUsage::ConstantBuffer,
                                                    true,
                                                    "PresentationEncodeParams");
            RHI::BufferPtr presentationParamsBuffer = m_Device->CreateBuffer(presentationParamsDesc);
            if (!presentationParamsBuffer)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate presentation encode buffer");
                return false;
            }
            presentationParamsBuffer->Update(&presentationParams, sizeof(presentationParams));
            for (const RHI::DescriptorSetPtr& blitDescriptorSet : m_BlitDescriptorSets)
            {
                blitDescriptorSet->BindConstantBuffer(1,
                                                      presentationParamsBuffer,
                                                      0,
                                                      sizeof(presentationParams));
            }

            RHI::GraphicsPipelineDesc blitPipelineDesc;
            blitPipelineDesc.vertexShader = m_BlitVertexShader;
            blitPipelineDesc.pixelShader = m_BlitFragmentShader;
            blitPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            blitPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            blitPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            blitPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            blitPipelineDesc.rasterState.lineWidth = 1.0f;
            blitPipelineDesc.depthStencilState.depthTestEnable = false;
            blitPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blitBlend;
            blitBlend.blendEnable = false;
            blitBlend.colorWriteMask = RHI::ColorWriteMask::All;
            blitPipelineDesc.blendState.attachments.push_back(blitBlend);
            blitPipelineDesc.renderPass = m_RenderPass;
            blitPipelineDesc.descriptorSetLayouts.push_back(blitDsDesc);

            m_BlitPipeline = m_Device->CreateGraphicsPipeline(blitPipelineDesc);
            if (!m_BlitPipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate blit pipeline");
                return false;
            }
        }

        if (m_TriangleVertexShader && m_TriangleFragmentShader)
        {
            RHI::GraphicsPipelineDesc triPipelineDesc;
            triPipelineDesc.vertexShader = m_TriangleVertexShader;
            triPipelineDesc.pixelShader = m_TriangleFragmentShader;
            triPipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            triPipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            triPipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            triPipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            triPipelineDesc.rasterState.lineWidth = 1.0f;
            triPipelineDesc.depthStencilState.depthTestEnable = false;
            triPipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc triBlend;
            triBlend.blendEnable = false;
            triBlend.colorWriteMask = RHI::ColorWriteMask::All;
            triPipelineDesc.blendState.attachments.push_back(triBlend);
            triPipelineDesc.renderPass = m_RenderPass;

            m_TrianglePipeline = m_Device->CreateGraphicsPipeline(triPipelineDesc);
            if (!m_TrianglePipeline)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to recreate triangle pipeline");
                return false;
            }
        }

        const bool bCreated = CreateSwapChainFramebuffers();
        if (bCreated)
        {
            swapChain->ConsumePresentationDirty();
        }
        return bCreated;
    }

} // namespace NorvesLib::Core::Rendering
