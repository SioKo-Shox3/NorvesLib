#include "Rendering/FrameCommand.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;

namespace
{
    bool ContainsText(const std::string& source, const std::string& expected)
    {
        return source.find(expected) != std::string::npos;
    }

    std::string ReadShaderSource(const char* fileName)
    {
#ifndef NORVES_SHADER_DIR
#error NORVES_SHADER_DIR must be defined for MegaGeometryFrameCommandDebugModeTest.
#endif
        const std::string shaderPath = std::string(NORVES_SHADER_DIR) + "/" + fileName;
        std::ifstream shaderFile(shaderPath, std::ios::binary);
        assert(shaderFile.is_open());
        return std::string((std::istreambuf_iterator<char>(shaderFile)),
                           std::istreambuf_iterator<char>());
    }
} // namespace

int main()
{
    std::cout << "MegaGeometryFrameCommandDebugModeTest start\n";

    assert(sizeof(MegaGeometry::DrawIndexedIndirectCommand) == 20);
    assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, IndexCount) == 0);
    assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, InstanceCount) == 4);
    assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstIndex) == 8);
    assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, VertexOffset) == 12);
    assert(offsetof(MegaGeometry::DrawIndexedIndirectCommand, FirstInstance) == 16);

    CameraProxy camera;
    NorvesLib::RHI::Viewport viewport;
    viewport.width = 128.0f;
    viewport.height = 64.0f;

    NorvesLib::RHI::ScissorRect scissor;
    scissor.right = 128;
    scissor.bottom = 64;

    FrameCommand command = FrameCommand::CreateMegaGeometryPass(nullptr,
                                                                nullptr,
                                                                camera,
                                                                true,
                                                                viewport,
                                                                scissor,
                                                                DebugViewMode::Wireframe);
    assert(command.Type == FrameCommandType::MegaGeometryPass);
    assert(command.MegaGeometry.DebugMode == DebugViewMode::Wireframe);

    FrameCommand lodCommand = FrameCommand::CreateMegaGeometryPass(nullptr,
                                                                   nullptr,
                                                                   camera,
                                                                   true,
                                                                   viewport,
                                                                   scissor,
                                                                   DebugViewMode::LODLevel);
    assert(lodCommand.Type == FrameCommandType::MegaGeometryPass);
    assert(lodCommand.MegaGeometry.DebugMode == DebugViewMode::LODLevel);

    ViewportRenderPlan viewportPlan;
    viewportPlan.DebugMode = DebugViewMode::LODLevel;
    viewportPlan.PixelRect.Width = 320.0f;
    viewportPlan.PixelRect.Height = 180.0f;
    viewportPlan.Scissor.Right = 320;
    viewportPlan.Scissor.Bottom = 180;

    NorvesLib::Core::Container::VariableArray<FrameCommand> pendingCommands;
    ViewRenderContext context;
    context.CurrentViewport = &viewportPlan;
    context.PendingFrameCommands = &pendingCommands;

    context.EnqueueMegaGeometryPass(nullptr);

    assert(pendingCommands.size() == 1);
    assert(pendingCommands[0].Type == FrameCommandType::MegaGeometryPass);
    assert(pendingCommands[0].MegaGeometry.DebugMode == DebugViewMode::LODLevel);

    const std::string cullSource = ReadShaderSource("cluster_cull.comp");
    assert(ContainsText(cullSource, "uint debugPayloadMode;"));
    assert(ContainsText(cullSource, "const uint DEBUG_PAYLOAD_MODE_CLUSTER_INDEX = 1u;"));
    assert(ContainsText(cullSource, "const uint DEBUG_PAYLOAD_MODE_LOD_LEVEL = 2u;"));
    assert(ContainsText(cullSource, "uint ComputeDebugPayload(uint clusterIndex, GPUClusterData cluster)"));
    assert(ContainsText(cullSource, "return cluster.lodInfo.x;"));
    assert(ContainsText(cullSource, "return 0u;"));
    assert(ContainsText(cullSource,
                        "drawCommands[drawIndex].firstInstance = ComputeDebugPayload(clusterIndex, cluster);"));

    const std::string vertexSource = ReadShaderSource("megageometry.vert");
    assert(ContainsText(vertexSource, "layout(location = 6) flat out uint fragDebugPayload;"));
    assert(ContainsText(vertexSource, "fragDebugPayload = gl_InstanceIndex;"));
    assert(ContainsText(vertexSource, "z=debugMode, w=debugPayloadSupported"));

    const std::string fragmentSource = ReadShaderSource("megageometry.frag");
    assert(ContainsText(fragmentSource, "layout(location = 6) flat in uint fragDebugPayload;"));
    assert(ContainsText(fragmentSource, "const float DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3.0;"));
    assert(ContainsText(fragmentSource, "const float DEBUG_VIEW_MODE_LOD_LEVEL = 8.0;"));
    assert(ContainsText(fragmentSource, "HashClusterId"));
    assert(ContainsText(fragmentSource, "LODLevelDebugColor"));
    assert(ContainsText(fragmentSource, "const vec3 palette[8] = vec3[8]"));
    assert(ContainsText(fragmentSource, "return palette[min(lodLevel, 7u)];"));
    assert(ContainsText(fragmentSource, "float debugPayloadSupported = mvp.pomParams.w;"));
    assert(ContainsText(fragmentSource, "if (debugPayloadSupported > 0.5)"));
    assert(ContainsText(fragmentSource, "debugMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS"));
    assert(ContainsText(fragmentSource, "WriteDebugGBuffer(ClusterDebugColor(fragDebugPayload));"));
    assert(ContainsText(fragmentSource, "debugMode == DEBUG_VIEW_MODE_LOD_LEVEL"));
    assert(ContainsText(fragmentSource, "WriteDebugGBuffer(LODLevelDebugColor(fragDebugPayload));"));
    assert(ContainsText(fragmentSource, "outMaterial = vec4(0.0, 1.0, 1.0, 0.0);"));

    std::cout << "MegaGeometryFrameCommandDebugModeTest passed\n";
    return 0;
}
