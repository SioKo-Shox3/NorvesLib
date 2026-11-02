/**
 * @file SwordTrailRibbonVulkanTest.cpp
 * @brief 既知の剣の帯を実際の描画器で描き、投影した画素の位置を確かめる。
 */

#include "Particle/SwordTrailRibbon.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RibbonTrailRenderer.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

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
    using namespace NorvesLib::Core::Particle;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "SwordTrailRibbonVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t TargetWidth = 128u;
    constexpr uint32_t TargetHeight = 128u;
    constexpr uint32_t BytesPerPixel = 4u;
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float EdgeMargin = 3.0f;

    // 列優先。平行移動・回転・異なる縦横比を入れ、行列の転置や乗算順の違いが画素の位置に表れるようにする。
    constexpr float View[16] = {
        0.9553364891f, 0.2955202067f, 0.0f, 0.0f,
        -0.2955202067f, 0.9553364891f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.16f, -0.08f, 0.4f, 1.0f
    };
    constexpr float Projection[16] = {
        0.85f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.72f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.11f, -0.12f, 0.0f, 1.0f
    };

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // 描画器が借りた頂点と記述子を GPU の完了まで保持する。
    struct TestFrameResources
    {
        explicit TestFrameResources(const DevicePtr& device)
        {
            PageSource.SetDevice(device);
        }

        ~TestFrameResources()
        {
            Owners.Reset();
            Owners.ReleaseUploadPages();
            Owners.ReleaseDescriptorPools();
        }

        bool Configure(IDevice* device)
        {
            Owners.ConfigureUpload(&PageSource, FrameUploadArena::DefaultInitialPageBytes);
            return Owners.ConfigureDescriptors(device);
        }

        DeviceFrameUploadPageSource PageSource;
        FrameResourceOwners Owners;
    };

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || value == nullptr)
        {
            return false;
        }
        const bool bSkip = std::strcmp(value, "1") == 0;
        free(value);
        return bSkip;
    }

    bool Near(float actual, float expected)
    {
        return std::isfinite(actual) && std::abs(actual - expected) <= 0.0001f;
    }

    // 原点の根元から長さ 1 の刃を 0〜90 度振り、補間点も時刻から独立に求めた位置と照合する。
    bool MakeScene(RibbonTrailScene& scene)
    {
        SwordTrailRibbonSettings settings;
        settings.Lifetime = 10.0f;
        settings.Color = Math::Vector4(0.8f, 0.4f, 0.2f, 1.0f);
        settings.Blend = RibbonTrailBlend::Additive;
        SwordTrailRibbon ribbon;
        if (!ribbon.Configure(settings))
        {
            std::cerr << TestName << " 帯の設定に失敗しました\n";
            return false;
        }
        for (uint32_t frame = 0; frame <= 4u; ++frame)
        {
            const float time = static_cast<float>(frame) / 4.0f;
            Math::Transform blade;
            blade.rotation = Math::Quaternion(Math::Vector3(0.0f, 0.0f, 1.0f), time * Pi * 0.5f);
            ribbon.AddSample(blade, time);
        }
        ribbon.AppendDraw(scene);
        const uint32_t pointCount = ribbon.GetPointCount();
        if (pointCount < 5u || scene.Draws.size() != 1u || scene.Draws[0].FirstVertex != 0u ||
            scene.Draws[0].VertexCount != pointCount * 2u || scene.Vertices.size() != pointCount * 2u ||
            scene.Draws[0].Blend != RibbonTrailBlend::Additive)
        {
            std::cerr << TestName << " 帯の点数または頂点の範囲が不正です\n";
            return false;
        }
        for (uint32_t i = 0; i < pointCount; ++i)
        {
            const auto& point = ribbon.GetPoint(i);
            const float time = static_cast<float>(point.Time);
            const float angle = time * Pi * 0.5f;
            const float expectedTip[3] = {-std::sin(angle), std::cos(angle), 0.0f};
            if (!std::isfinite(time) || time < 0.0f || time > 1.0f ||
                (i > 0u && point.Time <= ribbon.GetPoint(i - 1u).Time) ||
                (i == 0u && !Near(time, 0.0f)) || (i + 1u == pointCount && !Near(time, 1.0f)))
            {
                std::cerr << TestName << " 点の時刻が不正です 点=" << i << " 時刻=" << time << '\n';
                return false;
            }
            for (uint32_t component = 0; component < 3u; ++component)
            {
                const float base = scene.Vertices[i * 2u].Position[component];
                const float tip = scene.Vertices[i * 2u + 1u].Position[component];
                if (!Near(base, 0.0f) || !Near(tip, expectedTip[component]))
                {
                    std::cerr << TestName << " 帯の頂点が既知の軌跡と違います 点=" << i
                              << " 成分=" << component << " 根元=" << base << " 切っ先=" << tip
                              << " 期待=" << expectedTip[component] << '\n';
                    return false;
                }
            }
        }
        return true;
    }

    struct Point2
    {
        float X;
        float Y;
    };

    void Multiply(const float (&matrix)[16], const float (&input)[4], float (&output)[4])
    {
        for (uint32_t row = 0; row < 4u; ++row)
        {
            output[row] = 0.0f;
            for (uint32_t column = 0; column < 4u; ++column)
            {
                output[row] += matrix[column * 4u + row] * input[column];
            }
        }
    }

    Point2 Project(const float (&position)[3])
    {
        const float world[4] = {position[0], position[1], position[2], 1.0f};
        float camera[4];
        float clip[4];
        Multiply(View, world, camera);
        Multiply(Projection, camera, clip);
        // RHI の正の高さのビューポートは、NDC の -1 を画像の 0 行・0 列に写す。
        return {(clip[0] / clip[3] + 1.0f) * 0.5f * TargetWidth,
                (clip[1] / clip[3] + 1.0f) * 0.5f * TargetHeight};
    }

    float Cross(Point2 a, Point2 b, Point2 p)
    {
        return (b.X - a.X) * (p.Y - a.Y) - (b.Y - a.Y) * (p.X - a.X);
    }

    bool InsideTriangle(Point2 p, Point2 a, Point2 b, Point2 c)
    {
        if (std::abs(Cross(a, b, c)) < 0.0001f)
        {
            return false;
        }
        const float ab = Cross(a, b, p);
        const float bc = Cross(b, c, p);
        const float ca = Cross(c, a, p);
        return (ab >= 0.0f && bc >= 0.0f && ca >= 0.0f) ||
               (ab <= 0.0f && bc <= 0.0f && ca <= 0.0f);
    }

    bool InsideRibbon(Point2 p, const Container::VariableArray<Point2>& vertices)
    {
        for (size_t i = 2; i < vertices.size(); ++i)
        {
            if (InsideTriangle(p, vertices[i - 2u], vertices[i - 1u], vertices[i]))
            {
                return true;
            }
        }
        return false;
    }

    float DistanceSquaredToSegment(Point2 p, Point2 a, Point2 b)
    {
        const float dx = b.X - a.X;
        const float dy = b.Y - a.Y;
        const float lengthSquared = dx * dx + dy * dy;
        const float t = lengthSquared > 0.0f ?
            std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        const float x = p.X - (a.X + t * dx);
        const float y = p.Y - (a.Y + t * dy);
        return x * x + y * y;
    }

    float DistanceSquaredToBoundary(Point2 p, const Container::VariableArray<Point2>& vertices)
    {
        // 根元は全点で同じ。外周は根元から最初の切っ先、隣り合う切っ先、最後の切っ先から根元。
        float nearest = DistanceSquaredToSegment(p, vertices[0], vertices[1]);
        for (size_t i = 3; i < vertices.size(); i += 2u)
        {
            nearest = std::min(nearest, DistanceSquaredToSegment(p, vertices[i - 2u], vertices[i]));
        }
        return std::min(nearest, DistanceSquaredToSegment(p, vertices.back(), vertices[0]));
    }

    bool CheckProbe(float radius, float angleDegrees, bool bExpectedInside,
                    const Container::VariableArray<Point2>& vertices)
    {
        const float angle = angleDegrees * Pi / 180.0f;
        const float position[3] = {-radius * std::sin(angle), radius * std::cos(angle), 0.0f};
        const Point2 projected = Project(position);
        const int32_t x = static_cast<int32_t>(std::floor(projected.X));
        const int32_t y = static_cast<int32_t>(std::floor(projected.Y));
        const Point2 center = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(TargetWidth) || y >= static_cast<int32_t>(TargetHeight) ||
            DistanceSquaredToBoundary(center, vertices) < EdgeMargin * EdgeMargin ||
            InsideRibbon(center, vertices) != bExpectedInside)
        {
            std::cerr << TestName << " 標本の位置または内外の前提が不正です 半径=" << radius
                      << " 角度=" << angleDegrees << " 画素=(" << x << "," << y << ")\n";
            return false;
        }
        return true;
    }

    bool VerifyPixels(const RibbonTrailScene& scene, const uint8_t* pixels)
    {
        Container::VariableArray<Point2> vertices;
        vertices.reserve(scene.Vertices.size());
        for (const RibbonTrailVertex& vertex : scene.Vertices)
        {
            vertices.push_back(Project(vertex.Position));
        }
        // 内側と、振り始めより前・振り終わりの先・切っ先より外の各標本を保証する。
        const struct Probe { float Radius; float Angle; bool bInside; } probes[] = {
            {0.4f, 15.0f, true}, {0.72f, 30.0f, true}, {0.55f, 45.0f, true},
            {0.78f, 60.0f, true}, {0.42f, 75.0f, true},
            {0.6f, -25.0f, false}, {0.75f, -35.0f, false},
            {0.6f, 115.0f, false}, {0.75f, 130.0f, false},
            {1.25f, 20.0f, false}, {1.25f, 45.0f, false}, {1.25f, 70.0f, false}
        };
        for (const Probe& probe : probes)
        {
            if (!CheckProbe(probe.Radius, probe.Angle, probe.bInside, vertices))
            {
                return false;
            }
        }

        uint32_t insideCount = 0u;
        uint32_t outsideCount = 0u;
        uint32_t mismatchCount = 0u;
        for (uint32_t y = 0; y < TargetHeight; ++y)
        {
            for (uint32_t x = 0; x < TargetWidth; ++x)
            {
                const Point2 center = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                if (DistanceSquaredToBoundary(center, vertices) < EdgeMargin * EdgeMargin)
                {
                    continue;
                }
                const bool bInside = InsideRibbon(center, vertices);
                bInside ? ++insideCount : ++outsideCount;
                const uint8_t* pixel = pixels + (static_cast<size_t>(y) * TargetWidth + x) * BytesPerPixel;
                const bool bMatches = bInside ?
                    (pixel[0] >= 100u && pixel[1] >= 40u && pixel[2] >= 20u && pixel[3] >= 200u) :
                    (pixel[0] == 0u && pixel[1] == 0u && pixel[2] == 0u && pixel[3] == 0u);
                if (!bMatches)
                {
                    if (mismatchCount < 24u)
                    {
                        std::cerr << TestName << " 画素が違います 位置=(" << x << "," << y
                                  << ") 期待=" << (bInside ? "帯の内側" : "クリア色")
                                  << " RGBA=(" << static_cast<uint32_t>(pixel[0]) << ","
                                  << static_cast<uint32_t>(pixel[1]) << "," << static_cast<uint32_t>(pixel[2])
                                  << "," << static_cast<uint32_t>(pixel[3]) << ")\n";
                    }
                    ++mismatchCount;
                }
            }
        }
        std::cout << TestName << " 内側=" << insideCount << " 外側=" << outsideCount
                  << " 不一致=" << mismatchCount << '\n';
        return insideCount >= 500u && outsideCount >= 10000u && mismatchCount == 0u;
    }

    bool RenderAndVerify(const DevicePtr& device, ShaderManager& shaderManager, const RibbonTrailScene& scene)
    {
        TextureDesc colorDesc;
        colorDesc.Width = TargetWidth;
        colorDesc.Height = TargetHeight;
        colorDesc.TextureFormat = Format::R8G8B8A8_UNORM;
        colorDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
        colorDesc.DebugName = "SwordTrailRibbonColor";
        TexturePtr color = device->CreateTexture(colorDesc);
        TextureDesc depthDesc;
        depthDesc.Width = TargetWidth;
        depthDesc.Height = TargetHeight;
        depthDesc.TextureFormat = Format::D32_FLOAT;
        depthDesc.Usage = ResourceUsage::DepthStencil;
        depthDesc.DebugName = "SwordTrailRibbonDepth";
        TexturePtr depth = device->CreateTexture(depthDesc);

        AttachmentDesc colorAttachment;
        colorAttachment.format = Format::R8G8B8A8_UNORM;
        colorAttachment.clearColor[3] = 0.0f;
        colorAttachment.loadOp = AttachmentLoadOp::Clear;
        colorAttachment.storeOp = AttachmentStoreOp::Store;
        colorAttachment.initialState = ResourceState::Undefined;
        colorAttachment.finalState = ResourceState::RenderTarget;
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back(colorAttachment);
        passDesc.hasDepthStencil = true;
        passDesc.depthStencilAttachment.format = Format::D32_FLOAT;
        passDesc.depthStencilAttachment.isDepthStencil = true;
        passDesc.depthStencilAttachment.clearDepth = 1.0f;
        passDesc.depthStencilAttachment.loadOp = AttachmentLoadOp::Clear;
        passDesc.depthStencilAttachment.storeOp = AttachmentStoreOp::Store;
        passDesc.depthStencilAttachment.initialState = ResourceState::Undefined;
        passDesc.depthStencilAttachment.finalState = ResourceState::DepthWrite;
        RenderPassPtr renderPass = device->CreateRenderPass(passDesc);

        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = renderPass;
        framebufferDesc.colorTargets.push_back(color);
        framebufferDesc.depthStencilTarget = depth;
        framebufferDesc.width = TargetWidth;
        framebufferDesc.height = TargetHeight;
        FramebufferPtr framebuffer = color && depth && renderPass ? device->CreateFramebuffer(framebufferDesc) : nullptr;
        constexpr uint64_t readbackBytes = static_cast<uint64_t>(TargetWidth) * TargetHeight * BytesPerPixel;
        BufferPtr readback = device->CreateBuffer(BufferDesc(readbackBytes, ResourceUsage::TransferDst, true,
                                                              "SwordTrailRibbonReadback"));
        RibbonTrailRenderer renderer;
        TestFrameResources frameResources(device);
        if (!framebuffer || !readback || !renderer.Initialize(device.get(), &shaderManager, renderPass) ||
            !frameResources.Configure(device.get()))
        {
            std::cerr << TestName << " 描画の確認用の資源を作れませんでした\n";
            return false;
        }
        CommandListPtr commands = device->CreateCommandList();
        FrameResourceLane* lane = frameResources.Owners.BeginFrame(0u, 0u);
        if (!commands || !lane)
        {
            std::cerr << TestName << " コマンドリストまたはフレーム資源を作れませんでした\n";
            return false;
        }

        Viewport viewport;
        viewport.width = static_cast<float>(TargetWidth);
        viewport.height = static_cast<float>(TargetHeight);
        ScissorRect scissor;
        scissor.right = static_cast<int32_t>(TargetWidth);
        scissor.bottom = static_cast<int32_t>(TargetHeight);
        commands->Begin();
        commands->BeginRenderPass(renderPass, framebuffer);
        commands->SetViewport(viewport);
        commands->SetScissor(scissor);
        const bool bRecorded = renderer.Record(commands.get(), lane, scene, View, Projection, 1.0f);
        commands->EndRenderPass();
        if (!bRecorded)
        {
            commands->End();
            frameResources.Owners.AbortFrame();
            std::cerr << TestName << " 帯の描画を記録できませんでした\n";
            return false;
        }
        commands->TextureBarrier(color, ResourceState::RenderTarget, ResourceState::CopySource);
        commands->CopyTextureToBuffer(color, readback, TargetWidth, TargetHeight, 0u);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, readbackBytes);
        commands->End();
        commands->Submit(true);
        frameResources.Owners.CommitSubmitted(1u);
        device->WaitIdle();
        frameResources.Owners.Retire(1u);

        const uint8_t* pixels = static_cast<const uint8_t*>(readback->Map(0u, readbackBytes));
        if (!pixels)
        {
            std::cerr << TestName << " 読み戻しのバッファを写像できませんでした\n";
            return false;
        }
        const bool bPassed = VerifyPixels(scene, pixels);
        readback->Unmap();
        renderer.Shutdown();
        return bPassed;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            std::cout << TestName << " スキップ: NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定されました\n";
            return GpuTestSkipReturnCode;
        }
        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            std::cout << TestName << " スキップ: Vulkan デバイスを利用できません\n";
            return GpuTestSkipReturnCode;
        }
        ShaderManager shaderManager;
        Container::String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManager を初期化できませんでした\n";
            return 1;
        }
        RibbonTrailScene scene;
        bool bPassed = MakeScene(scene) && RenderAndVerify(device, shaderManager, scene);
        device->WaitIdle();
        shaderManager.Shutdown();
        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        if (validationErrorCount != 0u)
        {
            std::cerr << TestName << " Vulkan の検証エラー=" << validationErrorCount << '\n';
            bPassed = false;
        }
        std::cout << (bPassed ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return bPassed ? 0 : 1;
    }
} // 無名名前空間

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << " で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
