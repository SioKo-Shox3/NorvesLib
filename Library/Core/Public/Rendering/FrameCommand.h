#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/SceneProxy.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderResourceManager;
    class MegaGeometryPass;

    enum class FrameCommandType : uint8_t
    {
        GeometryPass,
        FullscreenPass,
        TextureBarrier,
        MegaGeometryPass,
    };

    struct GeometryPassCommand
    {
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        Container::TSharedPtr<Container::VariableArray<DrawCommand>> DrawCommands;
        RenderResourceManager* ResourceManager = nullptr;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
    };

    struct FullscreenPassCommand
    {
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
        RHI::PipelinePtr Pipeline;
        RHI::DescriptorSetPtr DescriptorSet;
        uint32_t DescriptorSetSlot = 0;
        uint32_t VertexCount = 3;
    };

    struct TextureBarrierCommand
    {
        RHI::TexturePtr Texture;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint32_t MipLevel = 0;
        uint32_t ArrayIndex = 0;
        uint32_t MipCount = 0;
        uint32_t ArrayCount = 0;
    };

    struct MegaGeometryPassCommand
    {
        MegaGeometryPass* Pass = nullptr;
        RenderResourceManager* ResourceManager = nullptr;
        CameraProxy MainCamera;
        bool bHasMainCamera = false;
    };

    struct FrameCommand
    {
        FrameCommandType Type = FrameCommandType::GeometryPass;
        GeometryPassCommand GeometryPass;
        FullscreenPassCommand FullscreenPass;
        TextureBarrierCommand TextureBarrier;
        MegaGeometryPassCommand MegaGeometry;

        static FrameCommand CreateGeometryPass(RHI::RenderPassPtr renderPass,
                                               RHI::FramebufferPtr framebuffer,
                                               Container::TSharedPtr<Container::VariableArray<DrawCommand>> drawCommands,
                                               const RHI::Viewport& viewport,
                                               const RHI::ScissorRect& scissor,
                                               RenderResourceManager* resourceManager = nullptr)
        {
            FrameCommand command;
            command.Type = FrameCommandType::GeometryPass;
            command.GeometryPass.RenderPass = renderPass;
            command.GeometryPass.Framebuffer = framebuffer;
            command.GeometryPass.DrawCommands = drawCommands;
            command.GeometryPass.ResourceManager = resourceManager;
            command.GeometryPass.Viewport = viewport;
            command.GeometryPass.Scissor = scissor;
            return command;
        }

        static FrameCommand CreateFullscreenPass(RHI::RenderPassPtr renderPass,
                                                 RHI::FramebufferPtr framebuffer,
                                                 const RHI::Viewport& viewport,
                                                 const RHI::ScissorRect& scissor,
                                                 RHI::PipelinePtr pipeline,
                                                 RHI::DescriptorSetPtr descriptorSet,
                                                 uint32_t descriptorSetSlot = 0,
                                                 uint32_t vertexCount = 3)
        {
            FrameCommand command;
            command.Type = FrameCommandType::FullscreenPass;
            command.FullscreenPass.RenderPass = renderPass;
            command.FullscreenPass.Framebuffer = framebuffer;
            command.FullscreenPass.Viewport = viewport;
            command.FullscreenPass.Scissor = scissor;
            command.FullscreenPass.Pipeline = pipeline;
            command.FullscreenPass.DescriptorSet = descriptorSet;
            command.FullscreenPass.DescriptorSetSlot = descriptorSetSlot;
            command.FullscreenPass.VertexCount = vertexCount;
            return command;
        }

        static FrameCommand CreateTextureBarrier(RHI::TexturePtr texture,
                                                 RHI::ResourceState beforeState,
                                                 RHI::ResourceState afterState,
                                                 uint32_t mipLevel = 0,
                                                 uint32_t arrayIndex = 0,
                                                 uint32_t mipCount = 0,
                                                 uint32_t arrayCount = 0)
        {
            FrameCommand command;
            command.Type = FrameCommandType::TextureBarrier;
            command.TextureBarrier.Texture = texture;
            command.TextureBarrier.BeforeState = beforeState;
            command.TextureBarrier.AfterState = afterState;
            command.TextureBarrier.MipLevel = mipLevel;
            command.TextureBarrier.ArrayIndex = arrayIndex;
            command.TextureBarrier.MipCount = mipCount;
            command.TextureBarrier.ArrayCount = arrayCount;
            return command;
        }

        static FrameCommand CreateMegaGeometryPass(MegaGeometryPass* pass,
                                                   RenderResourceManager* resourceManager,
                                                   const CameraProxy& mainCamera,
                                                   bool bHasMainCamera)
        {
            FrameCommand command;
            command.Type = FrameCommandType::MegaGeometryPass;
            command.MegaGeometry.Pass = pass;
            command.MegaGeometry.ResourceManager = resourceManager;
            command.MegaGeometry.MainCamera = mainCamera;
            command.MegaGeometry.bHasMainCamera = bHasMainCamera;
            return command;
        }
    };

} // namespace NorvesLib::Core::Rendering
