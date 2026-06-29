#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/DrawCommand.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class MegaGeometryPass;

    enum class FrameCommandType : uint8_t
    {
        GeometryPass,
        FullscreenPass,
        DebugDrawLineList,
        TextureBarrier,
        MegaGeometryPass,
    };

    struct GeometryPassCommand
    {
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        Container::TSharedPtr<Container::VariableArray<DrawCommand>> DrawCommands;
        MeshResources* Meshes = nullptr;
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

    struct DebugDrawLineListPassCommand
    {
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::PipelinePtr Pipeline;
        RHI::DescriptorSetPtr DescriptorSet;
        RHI::BufferPtr VertexBuffer;
        uint32_t VertexCount = 0;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
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
        MegaGeometryResources* MegaGeometry = nullptr;
        CameraProxy MainCamera;
        bool bHasMainCamera = false;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
        DebugViewMode DebugMode = DebugViewMode::Normal;
    };

    struct FrameCommand
    {
        FrameCommandType Type = FrameCommandType::GeometryPass;
        GeometryPassCommand GeometryPass;
        FullscreenPassCommand FullscreenPass;
        DebugDrawLineListPassCommand DebugDrawLineList;
        TextureBarrierCommand TextureBarrier;
        MegaGeometryPassCommand MegaGeometry;

        static FrameCommand CreateGeometryPass(RHI::RenderPassPtr renderPass,
                                               RHI::FramebufferPtr framebuffer,
                                               Container::TSharedPtr<Container::VariableArray<DrawCommand>> drawCommands,
                                               const RHI::Viewport& viewport,
                                               const RHI::ScissorRect& scissor,
                                               MeshResources* meshes = nullptr)
        {
            FrameCommand command;
            command.Type = FrameCommandType::GeometryPass;
            command.GeometryPass.RenderPass = renderPass;
            command.GeometryPass.Framebuffer = framebuffer;
            command.GeometryPass.DrawCommands = drawCommands;
            command.GeometryPass.Meshes = meshes;
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

        static FrameCommand CreateDebugDrawLineList(RHI::RenderPassPtr renderPass,
                                                    RHI::FramebufferPtr framebuffer,
                                                    const RHI::Viewport& viewport,
                                                    const RHI::ScissorRect& scissor,
                                                    RHI::PipelinePtr pipeline,
                                                    RHI::DescriptorSetPtr descriptorSet,
                                                    RHI::BufferPtr vertexBuffer,
                                                    uint32_t vertexCount)
        {
            FrameCommand command;
            command.Type = FrameCommandType::DebugDrawLineList;
            command.DebugDrawLineList.RenderPass = renderPass;
            command.DebugDrawLineList.Framebuffer = framebuffer;
            command.DebugDrawLineList.Viewport = viewport;
            command.DebugDrawLineList.Scissor = scissor;
            command.DebugDrawLineList.Pipeline = pipeline;
            command.DebugDrawLineList.DescriptorSet = descriptorSet;
            command.DebugDrawLineList.VertexBuffer = vertexBuffer;
            command.DebugDrawLineList.VertexCount = vertexCount;
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
                                                   MegaGeometryResources* megaGeometry,
                                                   const CameraProxy& mainCamera,
                                                   bool bHasMainCamera,
                                                   const RHI::Viewport& viewport,
                                                   const RHI::ScissorRect& scissor,
                                                   DebugViewMode debugMode)
        {
            FrameCommand command;
            command.Type = FrameCommandType::MegaGeometryPass;
            command.MegaGeometry.Pass = pass;
            command.MegaGeometry.MegaGeometry = megaGeometry;
            command.MegaGeometry.MainCamera = mainCamera;
            command.MegaGeometry.bHasMainCamera = bHasMainCamera;
            command.MegaGeometry.Viewport = viewport;
            command.MegaGeometry.Scissor = scissor;
            command.MegaGeometry.DebugMode = debugMode;
            return command;
        }
    };

} // namespace NorvesLib::Core::Rendering
