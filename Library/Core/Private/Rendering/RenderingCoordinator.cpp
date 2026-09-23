#include "Rendering/RenderingCoordinator.h"
#include "Rendering/CanvasView.h"
#include "Rendering/RenderingCoordinatorDiagnostics.h"
#include "Rendering/CompositePass.h"
#include "Rendering/Screen.h"
#include "Rendering/SceneView.h"
#include "Rendering/View.h"
#include "Rendering/Viewport.h"
#include "Rendering/InstanceBufferRing.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/FramePacket.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/PresentationComposer.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/FrameCaptureReadbackHelper.h"
#include "Rendering/FrameCaptureAssignmentGuard.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RayTracingSceneSubsystem.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "RHI/ISampler.h"
#include "RHI/IDevice.h"
#include "RHI/ISwapChain.h"
#include "RHI/ICommandList.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <cassert>
#include <chrono>
#include <algorithm>
#include <cmath>
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
                hash = HashRevisionValue(hash, Count);
                hash = HashRevisionValue(hash, Xor);
                hash = HashRevisionValue(hash, Sum);
                return hash;
            }
        };

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
            hash = HashRevisionValue(hash, proxy.ObjectId);
            hash = HashRevisionValue(hash, proxy.ComponentId);
            hash = HashRevisionValue(hash, proxy.bCastShadow);
            hash = HashRevisionValue(hash, proxy.bHasAnimatedBounds);
            return HashRevisionValue(hash, proxy.bVisible);
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

            hash = HashRevisionValue(hash, packet.Scene.AmbientColorR);
            hash = HashRevisionValue(hash, packet.Scene.AmbientColorG);
            hash = HashRevisionValue(hash, packet.Scene.AmbientColorB);
            hash = HashRevisionValue(hash, packet.Scene.AmbientIntensity);

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

        uint64_t HashLightRevision(const FramePacket& packet)
        {
            uint64_t hash = RevisionHashOffset;
            hash = HashRevisionValue(hash, packet.Scene.LightProxies.size());
            for (const LightProxy& light : packet.Scene.LightProxies)
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

        CommandRange AppendSkinnedDrawCommands(
            FramePacket* packet,
            const Container::VariableArray<SkinnedMeshProxy>& proxies)
        {
            CommandRange range;
            if (!packet || proxies.empty())
            {
                return range;
            }

            range.First = static_cast<uint32_t>(packet->DrawCommands.size());
            for (const SkinnedMeshProxy& proxy : proxies)
            {
                if (!proxy.IsValid())
                {
                    continue;
                }

                Container::TSharedPtr<const SkinnedMeshAssetLease> assetLease = proxy.AssetLease.lock();
                if (!assetLease)
                {
                    continue;
                }
                auto frameLease = Container::MakeShared<SkinnedMeshFrameLease>(assetLease);
                if (!frameLease || !frameLease->IsValid())
                {
                    continue;
                }

                const uint32_t frameLeaseIndex =
                    static_cast<uint32_t>(packet->SkinnedMeshFrameLeases.size());
                packet->SkinnedMeshFrameLeases.push_back(frameLease);

                DrawCommand command = DrawCommand::CreateDrawIndexed();
                command.Draw.PayloadKind = DrawPayloadKind::Skinned;
                command.Draw.MaterialHandle = proxy.Material;
                command.Draw.MaterialBlendMode = BlendMode::Opaque;
                command.Draw.ObjectId = proxy.ObjectId;
                command.Draw.SourceMeshComponentId = proxy.ComponentId;
                command.Draw.WorldMatrix = proxy.WorldTransform;
                command.Draw.InstanceCount = 1;
                command.Draw.FirstInstance = 0;
                command.Draw.bInstanced = false;
                command.Draw.bCastShadow = proxy.bCastShadow;
                command.Skinned.FrameLeaseIndex = frameLeaseIndex;
                command.Skinned.BonePalette = proxy.BonePalette;
                packet->DrawCommands.push_back(command);
                ++range.Count;
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
            return frameIndex;
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

    } // namespace

    uint64_t ComputeSceneRevisionHash(const FramePacket& packet)
    {
        return HashSceneRevisionInternal(packet);
    }

    RayTracingSceneSubsystem::~RayTracingSceneSubsystem()
    {
        Shutdown();
    }

    void RayTracingSceneSubsystem::Shutdown()
    {
        m_BottomLevelCache.clear();
        for (TopLevelCacheEntry& topLevel : m_TopLevelCache)
        {
            topLevel = TopLevelCacheEntry{};
        }
    }

    bool RayTracingSceneSubsystem::BuildFrameSnapshot(const MeshResources* meshResources,
                                                       FramePacket& packet,
                                                       const MaterialResources* materialResources)
    {
        packet.RayTracingScene.Clear();
        if (!meshResources)
        {
            return true;
        }

        const DrawCommandView opaqueCommands =
            DrawCommandView::FromRange(packet.DrawCommands, packet.OpaqueCommandRange);
        for (const DrawCommand& command : opaqueCommands)
        {
            const DrawParams& draw = command.Draw;
            if ((command.Type != DrawCommandType::DrawIndexed &&
                 command.Type != DrawCommandType::DrawIndexedInstanced) ||
                draw.PayloadKind != DrawPayloadKind::Mesh ||
                !draw.MeshHandle.IsValid() ||
                !draw.bCastShadow ||
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

        return true;
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
                geometry.triangles.vertexOffset =
                    static_cast<uint64_t>(instance.VertexOffset) * instance.VertexStride;
                geometry.triangles.vertexCount = instance.VertexCount;
                geometry.triangles.vertexStride = instance.VertexStride;
                geometry.triangles.vertexFormat = RHI::Format::R32G32B32_FLOAT;
                geometry.triangles.indexBuffer = entry.IndexBuffer;
                geometry.triangles.indexOffset =
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
        RHI::AccelerationStructurePtr topLevel;
        if (cachedTopLevel.Structure && cachedTopLevel.InstanceCount == instanceCount)
        {
            RHI::AccelerationStructureBuildDesc tlasUpdate;
            tlasUpdate.type = RHI::AccelerationStructureType::TopLevel;
            tlasUpdate.mode = RHI::AccelerationStructureBuildMode::Update;
            tlasUpdate.destination = cachedTopLevel.Structure;
            tlasUpdate.source = cachedTopLevel.Structure;
            tlasUpdate.instances = tlasInstances;
            if (commandList.UpdateAccelerationStructure(tlasUpdate))
            {
                topLevel = cachedTopLevel.Structure;
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
                clearPacketAccelerationStructures();
                return false;
            }

            cachedTopLevel.Structure = topLevel;
            cachedTopLevel.InstanceCount = instanceCount;
        }

        packet.RayTracingScene.TopLevel = topLevel;
        return true;
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

        Container::VariableArray<RHI::GPUTimestampResult> results;
        m_CommandList->ConsumeCompletedGPUTimestampResults(results);
        if (results.empty())
        {
            return;
        }

        Container::VariableArray<RenderPassGPUTiming> timings;
        timings.reserve(results.size());
        for (const RHI::GPUTimestampResult& result : results)
        {
            RenderPassGPUTiming timing;
            timing.FrameNumber = result.FrameNumber;
            timing.PassName = result.ScopeName;
            timing.DurationMs = result.DurationMs;
            timing.bValid = result.bValid;
            timings.push_back(timing);
        }
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

            m_BlitDescriptorSet = m_Device->CreateDescriptorSet(blitDsDesc);
            if (!m_BlitDescriptorSet)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Blit descriptor set");
                return false;
            }

            // サンプラーを事前バインド（CombinedImageSamplerに必要）
            m_BlitDescriptorSet->BindSampler(0, m_BlitSampler);

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
            m_BlitDescriptorSet->BindConstantBuffer(1,
                                                     presentationParamsBuffer,
                                                     0,
                                                     sizeof(presentationParams));

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
        if (!m_TransientPool.Initialize(m_Device->GetResourceAllocator(), swapChain->GetMaxFramesInFlight()))
        {
            NORVES_LOG_ERROR("RenderingCoordinator", "Failed to initialize TransientResourcePool");
            ReleaseInitializedResources();
            return false;
        }

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
        // 明示選択し、RT pipeline・BDA・非一様texture添字に対応するデバイスでだけ有効にする。
        m_MainViewRenderer = RenderingMainViewRenderer::Raster;
        if (settings.MainViewRenderer == RenderingMainViewRenderer::PathTracing)
        {
            const RHI::DeviceCapabilities& capabilities = m_Device->GetCapabilities();
            if (capabilities.RayTracing.bAccelerationStructure &&
                capabilities.RayTracing.bRayTracingPipeline &&
                capabilities.bBufferDeviceAddress &&
                capabilities.bSampledImageArrayNonUniformIndexing)
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
            m_MainSceneView->SetupPathTracingPipeline(settings.PathTracingSamplesPerFrame);
            NORVES_LOG_INFO("RenderingCoordinator", "Path tracing pipeline configured on MainSceneView");
        }
        else
        {
            m_MainSceneView->SetupDeferredPipeline(&m_SceneRenderer);
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

    void RenderingCoordinator::Shutdown()
    {
        m_PreviousCompletedTotalFrameTimeMs = 0.0f;
        m_LatestCompletedGPUTimeMs = 0.0f;
        m_bLatestCompletedGPUTimeValid = false;
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
        }

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
        m_BlitDescriptorSet.reset();
        m_CompositeAlphaOverDescriptorSet.reset();
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
        if (m_CompositeAlphaOverFragmentShader && m_CompositeAlphaOverDescriptorSet)
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

        if (!m_CompositeAlphaOverDescriptorSet)
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

            m_CompositeAlphaOverDescriptorSet = m_Device->CreateDescriptorSet(compositeDsDesc);
            if (!m_CompositeAlphaOverDescriptorSet)
            {
                NORVES_LOG_ERROR("RenderingCoordinator", "Failed to create Composite alpha-over descriptor set");
                return false;
            }

            m_CompositeAlphaOverDescriptorSet->BindSampler(0, m_BlitSampler);
            m_CompositeAlphaOverDescriptorSet->BindSampler(1, m_BlitSampler);
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
        packet.Scene.SkyAtmosphere = m_SkyAtmosphere;
        packet.Scene.SetDDGIVolumeParameters(
            SanitizeDDGIVolumeParametersForRHI(
                m_DDGIVolume,
                capabilities.RayTracing.bAccelerationStructure,
                capabilities.RayTracing.bRayQuery));
        packet.Scene.SetVolumetricFogParameters(m_VolumetricFog);
        packet.bRTGIEnabled = m_bRTGIEnabled;
    }

    void RenderingCoordinator::UpdateFrameRevisions(FramePacket& packet)
    {
        const uint64_t sceneHash = ComputeSceneRevisionHash(packet);
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

        const uint64_t lightHash = HashLightRevision(packet);
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

        NORVES_STAT_TIME_START(cmdGen);

        if (m_CurrentPacket)
        {
            m_CurrentPacket->bHasMainCamera = false;
            m_CurrentPacket->bHasPreviousMainCamera = m_bPreviousMainCameraValid;
            if (m_bPreviousMainCameraValid)
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

            if (m_MainSceneView)
            {
                m_CurrentPacket->Scene.MeshProxies = m_MainSceneView->GetMeshProxies();
                m_CurrentPacket->Scene.SkinnedMeshProxies = m_MainSceneView->GetSkinnedMeshProxies();
                m_CurrentPacket->Scene.LightProxies = m_MainSceneView->GetLightProxies();
                m_CurrentPacket->Scene.MegaGeometryProxies = m_MainSceneView->GetMegaGeometryProxies();
            }
            SnapshotSceneParameters(*m_CurrentPacket, m_Device->GetCapabilities());

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
                ViewportRenderPlan viewportPlan = BuildViewportRenderPlan(*viewport,
                                                                          viewIndex,
                                                                          viewportIndex,
                                                                          m_RenderWidth,
                                                                          m_RenderHeight,
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

            const MeshResources* meshResources =
                m_RenderResources ? &m_RenderResources->Meshes() : nullptr;
            const MaterialResources* materialResources =
                m_RenderResources ? &m_RenderResources->Materials() : nullptr;
            if (!NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem().BuildFrameSnapshot(
                    meshResources,
                    *m_CurrentPacket,
                    materialResources))
            {
                NORVES_LOG_WARNING("RayTracingSceneSubsystem",
                                   "FramePacketのレイトレーシングscene snapshotを構築できませんでした");
            }
            UpdateFrameRevisions(*m_CurrentPacket);
        }

        NORVES_STAT_TIME_END(cmdGen, m_GameThreadStats.CommandGenerationTimeMs);

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
        const bool bFinishedCameraValid = m_CurrentPacket && m_CurrentPacket->bHasMainCamera;
        if (bFinishedCameraValid)
        {
            finishedCamera = m_CurrentPacket->Scene.MainCamera;
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
        }

        if (bFinishedCameraValid)
        {
            m_PreviousMainCamera = finishedCamera;
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

#if NORVES_ENABLE_STATS
        auto &statsManager = NorvesLib::Debug::StatsManager::Get();
        const bool bTraceActive = statsManager.IsTraceActive();
        std::chrono::high_resolution_clock::time_point renderFrameStartTime;
        if (bTraceActive)
        {
            renderFrameStartTime = std::chrono::high_resolution_clock::now();
        }
#endif

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
        const RHI::SwapChainBeginFrameStatus beginFrameStatus = m_Screen.BeginFrame();
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
        m_CommandList->NotifyGPUTimestampFrameSlotCompleted(
            frameIndex,
            swapChain->GetCompletedSubmissionSerial());
        if (m_RenderResources)
        {
            m_RenderResources->SkinnedMeshes().BeginFrame(
                swapChain->GetCompletedSubmissionSerial());
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
            m_CommandList->SetFrameIndex(frameIndex);
            m_CommandList->BeginRecording();
            PublishCompletedGPUTimestampResults();
            m_CommandList->End();
            const RHI::SwapChainEndFrameResult endFrameResult = m_Screen.EndFrame(m_CommandList);
            if (m_RenderResources)
            {
                if (endFrameResult.SubmissionSerial != 0)
                {
                    m_RenderResources->SkinnedMeshes().CommitSubmittedFrame(
                        endFrameResult.SubmissionSerial);
                }
                else
                {
                    m_RenderResources->SkinnedMeshes().AbortFrame();
                }
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
        RHI::BufferPtr instanceDataBuffer = m_InstanceBufferRing.Upload(frameIndex, packet->InstanceData);

        // フレーム別コマンドバッファを選択（ダブルバッファリングでの同期問題を回避）
        m_CommandList->SetFrameIndex(frameIndex);

        // SceneRendererフレーム開始
        m_SceneRenderer.BeginFrame();

        // コマンド録画開始
        m_CommandList->BeginRecording();
        PublishCompletedGPUTimestampResults();
        ScopedGPUTimestampFrameRecording gpuTimestampFrameGuard(
            m_CommandList.get(),
            frameIndex);

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
        if (bTraceActive)
        {
            const float latestGPUTimeMs = m_CommandList->GetLastGPUTimestampDurationMs();
            if (latestGPUTimeMs > 0.0f)
            {
                m_LatestCompletedGPUTimeMs = latestGPUTimeMs;
                m_bLatestCompletedGPUTimeValid = true;
                renderStats.GPUTimeMs = latestGPUTimeMs;
                statsManager.SetGPUFrameTimeMs(latestGPUTimeMs);
            }

            if (m_bLatestCompletedGPUTimeValid)
            {
                renderStats.GPUTimeMs = m_LatestCompletedGPUTimeMs;
            }

            if (m_CommandList->SupportsGPUTimestamps())
            {
                m_CommandList->BeginGPUTimestampFrame(packet->FrameNumber);
                m_CommandList->BeginGPUTimestamp("FrameGPU");
            }
        }
#endif

        // ========================================
        // Deferredパスチェーン描画（スワップチェーンレンダーパスの外で実行）
        // 各パスが独自のレンダーパスを開閉する
        // ========================================

        // ViewRenderContextを構築（パスチェーン対応の新フロー）
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        ViewRenderContext viewContext;
        viewContext.CommandList = m_CommandList.get();
        viewContext.InstanceDataBuffer = instanceDataBuffer;
        viewContext.Device = m_Device.get();
        viewContext.TransientPool = &m_TransientPool;
        viewContext.SharedResources = &m_Screen.GetSharedResourceRegistry();
        viewContext.CurrentRenderPass = m_RenderPass.get();
        viewContext.CurrentFramebuffer = m_SwapChainFramebuffers[imageIndex].get();
        viewContext.bRenderPassActive = false; // Deferredパスは独自のレンダーパスを使用
        viewContext.FrameIndex = frameIndex;
        viewContext.FrameNumber = packet->FrameNumber;
        viewContext.ScreenWidth = swapChain->GetWidth();
        viewContext.ScreenHeight = swapChain->GetHeight();
        viewContext.RenderWidth = m_RenderWidth;
        viewContext.RenderHeight = m_RenderHeight;
        viewContext.DeltaTime = m_PreviousCompletedTotalFrameTimeMs * 0.001f;
        viewContext.TotalTime = packet->TotalTime;
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
        viewContext.bRTGITLASAvailable = packet->HasCompleteRayTracingScene();
        viewContext.RTGICapability = MakeRTGIRayQueryCapability(m_Device->GetCapabilities());

        // フレームパケットからスナップショットを設定（RenderThread読み取り専用）
        viewContext.MainCamera = packet->bHasMainCamera ? &packet->Scene.MainCamera : nullptr;
        viewContext.PreviousMainCamera = packet->bHasPreviousMainCamera
                                             ? &packet->PreviousMainCamera
                                             : nullptr;
        viewContext.SnapshotScene = &packet->Scene;
        viewContext.SnapshotRayTracingScene = &packet->RayTracingScene;
        viewContext.SnapshotDeltaTime = packet->DeltaTime;
        viewContext.SkyAtmosphereSnapshot = packet->Scene.SkyAtmosphere;
        viewContext.SnapshotDrawCommandSource = &packet->DrawCommands;
        viewContext.SnapshotDrawCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                      packet->DrawCommandRange);
        viewContext.SnapshotDebugLineVertices = &packet->DebugLineVertices;
        viewContext.SnapshotOpaqueCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                        packet->OpaqueCommandRange);
        viewContext.SnapshotTransparentCommands = DrawCommandView::FromRange(packet->DrawCommands,
                                                                             packet->TransparentCommandRange);
        viewContext.SnapshotSkinnedMeshFrameLeases = &packet->SkinnedMeshFrameLeases;
        viewContext.SnapshotMeshProxies = &packet->Scene.MeshProxies;
        viewContext.SnapshotSkinnedMeshProxies = &packet->Scene.SkinnedMeshProxies;
        viewContext.SnapshotLightProxies = &packet->Scene.LightProxies;
        viewContext.SnapshotMegaGeometryProxies = &packet->Scene.MegaGeometryProxies;

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
        presentationRequest.BlitDescriptorSet = m_BlitDescriptorSet;
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
        graphPresentationRequest.BlitDescriptorSet = m_BlitDescriptorSet;
        graphPresentationRequest.BlitSampler = m_BlitSampler;

        CompositePassRequest graphCompositeRequest;
        graphCompositeRequest.VertexShader = m_BlitVertexShader;
        graphCompositeRequest.PixelShader = m_CompositeAlphaOverFragmentShader;
        graphCompositeRequest.DescriptorSet = m_CompositeAlphaOverDescriptorSet;
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

                    overlayPass->Setup(viewContext);
                    overlayPass->Execute(viewContext);
                }
            }
        }

        bool bPresentationBlitRecorded = m_PresentationPass.RecordDeferredBlit(viewContext);
        if (!bPresentationBlitRecorded &&
            finalPresentationTexture &&
            presentationRequest.ClearRenderPass &&
            presentationRequest.ClearFramebuffer &&
            m_BlitPipeline &&
            m_BlitDescriptorSet &&
            m_BlitSampler)
        {
            m_BlitDescriptorSet->BindTexture(0, finalPresentationTexture);
            m_BlitDescriptorSet->BindSampler(0, m_BlitSampler);
            m_BlitDescriptorSet->Update();
            viewContext.EnqueueFullscreenPass(presentationRequest.ClearRenderPass,
                                              presentationRequest.ClearFramebuffer,
                                              viewContext.GetActiveOutputViewport(),
                                              viewContext.GetActiveOutputScissor(),
                                              m_BlitPipeline,
                                              m_BlitDescriptorSet);
            bPresentationBlitRecorded = true;
        }
        m_PresentationPass.SetDeferBlit(false);

        if (bPresentationBlitRecorded)
        {
            executionResult.PresentationBlitCount = 1;
        }

        if (!pendingFrameCommands.empty())
        {
            m_SceneRenderer.ExecuteFrameCommands(pendingFrameCommands, m_CommandList.get());
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
                m_CommandList.get(),
                claimedCaptureRequest,
                executionResult.CaptureSources);
            if (captureRecordStatus == FrameCaptureRecordStatus::PublishedFailure ||
                captureRecordStatus == FrameCaptureRecordStatus::Deferred)
            {
                captureAssignment.MarkResolved();
            }
        }
        executionResult.CaptureSources.Reset();

        // コマンド録画終了
#if NORVES_ENABLE_STATS
        if (bTraceActive && m_CommandList->SupportsGPUTimestamps())
        {
            m_CommandList->EndGPUTimestamp();
            m_CommandList->EndGPUTimestampFrame();
        }
#endif
        m_CommandList->End();

        // SceneRendererフレーム終了
        m_SceneRenderer.EndFrame();
        m_TransientPool.EndFrame();

        // 統計更新
        const auto &rendererStats = m_SceneRenderer.GetStats();
        packet->Stats.SkinnedGBufferRecordedDraws = rendererStats.SkinnedGBufferDrawCallCount;
        packet->Stats.SkinnedShadowRecordedDraws = rendererStats.SkinnedShadowDrawCallCount;
        renderStats.DrawCalls = rendererStats.DrawCallCount;
        renderStats.TrianglesRendered = rendererStats.TriangleCount;

        // コマンドリストをサブミット＆Present（旧EndFrame経路から移動）
        const RHI::SwapChainEndFrameResult endFrameResult = m_Screen.EndFrame(m_CommandList);
        if (captureRecordStatus == FrameCaptureRecordStatus::Recorded &&
            endFrameResult.SubmissionSerial != 0 &&
            m_FrameCaptureReadbackHelper &&
            m_FrameCaptureReadbackHelper->CommitRecordedCopy(claimedCaptureRequest))
        {
            captureAssignment.MarkResolved();
        }
        if (m_RenderResources)
        {
            if (endFrameResult.SubmissionSerial != 0)
            {
                m_RenderResources->SkinnedMeshes().CommitSubmittedFrame(
                    endFrameResult.SubmissionSerial);
            }
            else
            {
                m_RenderResources->SkinnedMeshes().AbortFrame();
            }
        }
        if (endFrameResult.HasError())
        {
            ThrowSwapChainEndFrameError(endFrameResult.Status);
        }

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

        if (m_Diagnostics)
        {
            RenderingCoordinatorStatsSnapshot statsSnapshot;
            statsSnapshot.SkinnedGBufferRecordedDraws = packet->Stats.SkinnedGBufferRecordedDraws;
            statsSnapshot.SkinnedShadowRecordedDraws = packet->Stats.SkinnedShadowRecordedDraws;
            statsSnapshot.Stats = renderStats;
            statsSnapshot.GeneratedDrawCommandCount = packet->GeneratedDrawCommandCount;
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
        settings.Width = m_RenderWidth;
        settings.Height = m_RenderHeight;
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
        canvasCamera.OrthoWidth = static_cast<float>(m_RenderWidth);
        canvasCamera.OrthoHeight = static_cast<float>(m_RenderHeight);
        canvasCamera.Viewport.X = 0.0f;
        canvasCamera.Viewport.Y = 0.0f;
        canvasCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
        canvasCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
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

    uint64_t RenderingCoordinator::RegisterCamera(const CameraProxy &camera)
    {
        const uint64_t cameraId = m_NextCameraId++;
        CameraProxy storedCamera = camera;
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
        canvasCamera.OrthoWidth = static_cast<float>(m_RenderWidth);
        canvasCamera.OrthoHeight = static_cast<float>(m_RenderHeight);
        canvasCamera.Viewport.X = 0.0f;
        canvasCamera.Viewport.Y = 0.0f;
        canvasCamera.Viewport.Width = static_cast<float>(m_RenderWidth);
        canvasCamera.Viewport.Height = static_cast<float>(m_RenderHeight);
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

        if (m_BlitVertexShader && m_BlitFragmentShader && m_BlitDescriptorSet)
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
            m_BlitDescriptorSet->BindConstantBuffer(1,
                                                     presentationParamsBuffer,
                                                     0,
                                                     sizeof(presentationParams));

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
