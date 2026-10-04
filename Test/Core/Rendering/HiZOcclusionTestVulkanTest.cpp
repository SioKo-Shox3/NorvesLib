// Hi-Z による遮蔽の判定（Common/HiZOcclusion.glsl の HiZIsSphereOccluded）を GPU で確かめる。
// 合成した深度から本物の HiZPyramid で HZB を作り、確認用のコンピュートシェーダー（hiz_occlusion_probe.comp）で
// 球ごとの判定を読み戻す。契約は「見えているものを隠れていると判定しない」（誤りが 0 件）で、
// 1) 名前付きの場面（完全に隠れる・一部見える・近平面をまたぐ・カメラの後ろ・画面の端・小さくて 1 texel に収まる・
//    半分だけ覆う壁・手前の柱の後ろ）は、期待の判定と完全に一致すること、
// 2) 乱数の場面（重なる矩形の深度と乱数の球）は、球の内側を標本した点のどれかが描かれた面より手前なら
//    「隠れていない」と判定されること（実装を写さず、球の標本点を CPU で投影して描かれた面と比べる）、
// を確かめる。偶数・奇数の深度の解像度の両方で行う。
// クラスタのカリング（cluster_cull.comp）が同じ関数を取り込んでコンパイルできることも確かめる。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/HiZPyramidPass.h"
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

    constexpr const char* TestName = "HiZOcclusionTestVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t MaxSpheres = 256;

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

    // hiz_occlusion_probe.comp の ProbeParams（std140）と同じ並び
    struct ProbeParams
    {
        float ViewProjection[16];
        int32_t Info[4]; // 深度の幅・高さ、HZB のミップ数、球の数
        float Spheres[MaxSpheres][4];
    };
    static_assert(sizeof(ProbeParams) == 64u + 16u + 16u * MaxSpheres, "ProbeParams は std140 の並びと一致しなければならない");

    struct Sphere
    {
        float X;
        float Y;
        float Z;
        float Radius;
        bool bExpectOccluded; // 名前付きの場面だけで使う
        const char* Name;
    };

    // 決定的な擬似乱数
    class Rng
    {
    public:
        explicit Rng(uint32_t seed) : m_State(seed * 2654435761u + 12345u) {}
        uint32_t NextU32()
        {
            m_State ^= m_State << 13u;
            m_State ^= m_State >> 17u;
            m_State ^= m_State << 5u;
            return m_State;
        }
        float Next01() { return static_cast<float>(NextU32() >> 8u) / 16777216.0f; }
        float Range(float low, float high) { return low + (high - low) * Next01(); }

    private:
        uint32_t m_State;
    };

    // 列優先の行列（シェーダーへ渡す配列。[列 * 4 + 行]）で同次座標を変換する
    void TransformPoint(const float matrix[16], float x, float y, float z, float outClip[4])
    {
        const float input[4] = {x, y, z, 1.0f};
        for (uint32_t row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (uint32_t column = 0; column < 4; ++column)
            {
                sum += matrix[column * 4 + row] * input[column];
            }
            outClip[row] = sum;
        }
    }

    // 画面の中心の軸上で、ビュー空間の奥行き z にある点の深度（[0,1]）
    float DepthAtViewZ(const float viewProjection[16], float viewZ)
    {
        float clip[4] = {};
        TransformPoint(viewProjection, 0.0f, 0.0f, viewZ, clip);
        return clip[2] / clip[3];
    }

    struct DepthRect
    {
        float U0; // 画面の割合 [0,1]
        float V0;
        float U1;
        float V1;
        float ViewZ;
    };

    // クリア値 1.0 の深度へ、画面に平行な面（奥行き一定）を重ねる。重なりは手前（小さい値）が勝つ。
    VariableArray<float> MakeDepth(const float viewProjection[16], uint32_t width, uint32_t height, const DepthRect* rects, size_t rectCount)
    {
        VariableArray<float> depth;
        depth.resize(static_cast<size_t>(width) * height);
        for (float& value : depth)
        {
            value = 1.0f;
        }
        for (size_t rectIndex = 0; rectIndex < rectCount; ++rectIndex)
        {
            const DepthRect& rect = rects[rectIndex];
            const float rectDepth = DepthAtViewZ(viewProjection, rect.ViewZ);
            const uint32_t x0 = static_cast<uint32_t>(std::floor(rect.U0 * static_cast<float>(width)));
            const uint32_t x1 = std::min(width, static_cast<uint32_t>(std::ceil(rect.U1 * static_cast<float>(width))));
            const uint32_t y0 = static_cast<uint32_t>(std::floor(rect.V0 * static_cast<float>(height)));
            const uint32_t y1 = std::min(height, static_cast<uint32_t>(std::ceil(rect.V1 * static_cast<float>(height))));
            for (uint32_t y = y0; y < y1; ++y)
            {
                for (uint32_t x = x0; x < x1; ++x)
                {
                    float& value = depth[static_cast<size_t>(y) * width + x];
                    value = std::min(value, rectDepth);
                }
            }
        }
        return depth;
    }

    TexturePtr CreateDepthSource(const DevicePtr& device, uint32_t width, uint32_t height, const VariableArray<float>& depth)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.TextureFormat = Format::R32_FLOAT;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = "HiZOcclusionTestDepth";
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        const uint32_t rowPitch = width * static_cast<uint32_t>(sizeof(float));
        texture->Update(depth.data(), rowPitch, rowPitch * height, 0, 0);
        return texture;
    }

    DescriptorSetDesc MakeProbeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {ResourceBindType::CombinedImageSampler, ResourceBindType::ConstantBuffer, ResourceBindType::RWBuffer};
        for (uint32_t bindingIndex = 0; bindingIndex < 3u; ++bindingIndex)
        {
            DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    struct Fixture
    {
        DevicePtr Device;
        HiZPyramid Pyramid;
        PipelinePtr Pipeline;
        SamplerPtr Sampler;
        BufferPtr ParamBuffer;
        BufferPtr ResultBuffer;
        BufferPtr ReadbackBuffer;
        float ViewProjection[16] = {};
        float TanHalfX = 0.0f;
        float TanHalfY = 0.0f;
    };

    constexpr uint64_t ResultBytes = static_cast<uint64_t>(MaxSpheres) * sizeof(uint32_t);

    // 深度から HZB を作り、球ごとの判定（1=隠れている）を読み戻す。失敗したら false
    bool RunProbe(Fixture& fixture,
                  const VariableArray<float>& depth,
                  uint32_t width,
                  uint32_t height,
                  const Sphere* spheres,
                  size_t sphereCount,
                  uint32_t outOccluded[MaxSpheres])
    {
        TexturePtr source = CreateDepthSource(fixture.Device, width, height, depth);
        if (!source || sphereCount > MaxSpheres)
        {
            std::cerr << "深度のテクスチャを作れませんでした\n";
            return false;
        }

        CommandListPtr commandList = fixture.Device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "コマンドリストを作れませんでした\n";
            return false;
        }
        commandList->Begin();
        if (!fixture.Pyramid.Build(commandList.get(), source))
        {
            std::cerr << "HZB の生成を記録できませんでした\n";
            return false;
        }

        ProbeParams params{};
        std::memcpy(params.ViewProjection, fixture.ViewProjection, sizeof(params.ViewProjection));
        params.Info[0] = static_cast<int32_t>(width);
        params.Info[1] = static_cast<int32_t>(height);
        params.Info[2] = static_cast<int32_t>(fixture.Pyramid.GetMipCount());
        params.Info[3] = static_cast<int32_t>(sphereCount);
        for (size_t index = 0; index < sphereCount; ++index)
        {
            params.Spheres[index][0] = spheres[index].X;
            params.Spheres[index][1] = spheres[index].Y;
            params.Spheres[index][2] = spheres[index].Z;
            params.Spheres[index][3] = spheres[index].Radius;
        }
        fixture.ParamBuffer->Update(&params, sizeof(ProbeParams));

        DescriptorSetPtr descriptorSet = fixture.Device->CreateDescriptorSet(MakeProbeDescriptorSetDesc());
        if (!descriptorSet)
        {
            std::cerr << "ディスクリプタセットを作れませんでした\n";
            return false;
        }
        descriptorSet->BindTexture(0u, fixture.Pyramid.GetTexture());
        descriptorSet->BindSampler(0u, fixture.Sampler);
        descriptorSet->BindConstantBuffer(1u, fixture.ParamBuffer, 0u, static_cast<uint32_t>(sizeof(ProbeParams)));
        descriptorSet->BindStorageBuffer(2u, fixture.ResultBuffer, 0u, static_cast<uint32_t>(ResultBytes));
        descriptorSet->Update();

        commandList->BufferBarrier(fixture.ResultBuffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, ResultBytes);
        commandList->SetPipeline(fixture.Pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Dispatch(static_cast<uint32_t>((sphereCount + 63u) / 64u), 1u, 1u);
        commandList->BufferBarrier(fixture.ResultBuffer, ResourceState::UnorderedAccess, ResourceState::CopySource, 0u, ResultBytes);
        commandList->CopyBuffer(fixture.ResultBuffer, fixture.ReadbackBuffer, ResultBytes);
        commandList->BufferBarrier(fixture.ReadbackBuffer, ResourceState::CopyDest, ResourceState::HostRead, 0u, ResultBytes);
        commandList->End();
        commandList->Submit(true);
        fixture.Device->WaitIdle();

        const void* mapped = fixture.ReadbackBuffer->Map(0u, ResultBytes);
        if (mapped == nullptr)
        {
            std::cerr << "読み戻しのバッファを写像できませんでした\n";
            return false;
        }
        std::memcpy(outOccluded, mapped, ResultBytes);
        fixture.ReadbackBuffer->Unmap();
        return true;
    }

    // ---- 名前付きの場面 ----

    // 期待と違う球の数を返す（-1 は実行の失敗）。見えているのに隠れていると判定した数は falseHidden へ足す。
    int RunNamedScene(Fixture& fixture,
                      uint32_t width,
                      uint32_t height,
                      const char* sceneName,
                      const DepthRect* rects,
                      size_t rectCount,
                      const Sphere* spheres,
                      size_t sphereCount,
                      uint32_t& falseHidden)
    {
        const VariableArray<float> depth = MakeDepth(fixture.ViewProjection, width, height, rects, rectCount);
        uint32_t occluded[MaxSpheres] = {};
        if (!RunProbe(fixture, depth, width, height, spheres, sphereCount, occluded))
        {
            return -1;
        }

        int mismatchCount = 0;
        for (size_t index = 0; index < sphereCount; ++index)
        {
            const bool bActual = occluded[index] != 0u;
            if (bActual == spheres[index].bExpectOccluded)
            {
                continue;
            }
            ++mismatchCount;
            if (bActual && !spheres[index].bExpectOccluded)
            {
                ++falseHidden;
            }
            std::cerr << sceneName << " " << width << "x" << height << " [" << spheres[index].Name << "]: 判定=" << (bActual ? "隠れている" : "隠れていない")
                      << " 期待=" << (spheres[index].bExpectOccluded ? "隠れている" : "隠れていない") << '\n';
        }
        std::cout << sceneName << " " << width << "x" << height << " 球=" << sphereCount << " 不一致=" << mismatchCount << '\n';
        return mismatchCount;
    }

    int RunNamedScenes(Fixture& fixture, uint32_t width, uint32_t height, uint32_t& falseHidden)
    {
        const float tanX = fixture.TanHalfX;
        const float tanY = fixture.TanHalfY;
        int mismatchTotal = 0;
        auto accumulate = [&mismatchTotal](int mismatch) { mismatchTotal = (mismatch < 0 || mismatchTotal < 0) ? -1 : mismatchTotal + mismatch; };

        // 場面 A: 画面全体を覆う壁（奥行き 20）
        {
            const DepthRect wall[] = {{0.0f, 0.0f, 1.0f, 1.0f, 20.0f}};
            const float edgeX = tanX * 40.0f; // 奥行き 40 で画面の右端のワールド x
            const float edgeY = tanY * 40.0f;
            const Sphere spheres[] = {
                {0.0f, 0.0f, 40.0f, 2.0f, true, "壁の後ろ（完全に隠れる）"},
                {8.0f, 3.0f, 45.0f, 1.5f, true, "壁の後ろ（中心から外れる）"},
                {0.0f, 0.0f, 21.0f, 2.0f, false, "壁をまたぐ（一部見える）"},
                {0.0f, 0.0f, 10.0f, 1.0f, false, "壁の手前"},
                {0.0f, 0.0f, 0.5f, 1.0f, false, "近平面をまたぐ"},
                {0.0f, 0.0f, -5.0f, 1.0f, false, "カメラの後ろ"},
                {0.0f, 0.0f, -0.5f, 1.0f, false, "カメラの後ろにかかる"},
                {0.0f, 0.0f, 0.0f, 50.0f, false, "カメラを含む大きな球"},
                {edgeX, 0.0f, 40.0f, 3.0f, true, "右端にかかり壁の後ろ"},
                {edgeX, 0.0f, 15.0f, 3.0f, false, "右端にかかり壁の手前"},
                {edgeX, edgeY * 0.98f, 40.0f, 3.0f, true, "右上の角にかかり壁の後ろ"},
                {-edgeX, -edgeY, 40.0f, 3.0f, true, "左下の角にかかり壁の後ろ"},
                {200.0f, 0.0f, 40.0f, 1.0f, false, "画面の外"},
                {0.0f, 0.0f, 60.0f, 30.0f, true, "画面全体を覆う大きな球が壁の後ろ"},
                {0.3f, 0.2f, 40.0f, 0.01f, true, "小さくて1 texel 以内で壁の後ろ"},
                {0.3f, 0.2f, 10.0f, 0.01f, false, "小さくて1 texel 以内で壁の手前"},
            };
            accumulate(RunNamedScene(fixture, width, height, "壁", wall, 1, spheres, sizeof(spheres) / sizeof(spheres[0]), falseHidden));
        }

        // 場面 B: 画面の左半分だけ壁（奥行き 20）、右半分は何も描かれていない（クリア値）。
        // ワールドの x と画面の左右の向きの対応は、射影から求める（カメラの規約に依らないようにする）。
        {
            float probeClip[4] = {};
            TransformPoint(fixture.ViewProjection, 1.0f, 0.0f, 10.0f, probeClip);
            const float toLeft = probeClip[0] / probeClip[3] < 0.0f ? 1.0f : -1.0f; // 画面の左へ向かうワールド x の符号
            const DepthRect leftWall[] = {{0.0f, 0.0f, 0.5f, 1.0f, 20.0f}};
            const Sphere spheres[] = {
                {20.0f * toLeft, 0.0f, 40.0f, 3.0f, true, "左半分の壁の後ろ"},
                {0.0f, 0.0f, 40.0f, 3.0f, false, "壁の境目をまたぎ右半分が見える"},
                {-20.0f * toLeft, 0.0f, 40.0f, 3.0f, false, "右半分（何も無い）"},
                {5.0f * toLeft, 0.0f, 80.0f, 3.0f, true, "左半分の壁の後ろ（遠い）"},
                {-5.0f * toLeft, 0.0f, 80.0f, 3.0f, false, "右半分の遠い球"},
            };
            accumulate(RunNamedScene(fixture, width, height, "左半分の壁", leftWall, 1, spheres, sizeof(spheres) / sizeof(spheres[0]), falseHidden));
        }

        // 場面 C: 奥の壁（奥行き 40）の手前、中央に柱（奥行き 10。画面の中央 40%）。
        // 保守的な判定は texel の粒度で範囲を広げるので、柱は球の矩形より十分大きくしておく。
        {
            const DepthRect scene[] = {{0.0f, 0.0f, 1.0f, 1.0f, 40.0f}, {0.3f, 0.3f, 0.7f, 0.7f, 10.0f}};
            const Sphere spheres[] = {
                {0.0f, 0.0f, 25.0f, 1.0f, true, "柱の後ろに収まる"},
                {0.0f, 0.0f, 25.0f, 8.0f, false, "柱より大きく壁の手前にはみ出す"},
                {0.0f, 0.0f, 50.0f, 8.0f, true, "壁の後ろの大きな球"},
                {0.0f, 0.0f, 5.0f, 1.0f, false, "柱の手前"},
            };
            accumulate(RunNamedScene(fixture, width, height, "柱", scene, 2, spheres, sizeof(spheres) / sizeof(spheres[0]), falseHidden));
        }
        return mismatchTotal;
    }

    // ---- 乱数の場面 ----

    // 球の内側を標本した点のどれかが描かれた面より手前なら、その球は確かに見えている
    bool IsSphereTrulyVisible(const Fixture& fixture, const VariableArray<float>& depth, uint32_t width, uint32_t height, const Sphere& sphere, Rng& rng)
    {
        constexpr uint32_t SampleCount = 400;
        for (uint32_t sampleIndex = 0; sampleIndex <= SampleCount; ++sampleIndex)
        {
            float offset[3] = {0.0f, 0.0f, 0.0f};
            if (sampleIndex > 0)
            {
                do
                {
                    offset[0] = rng.Range(-1.0f, 1.0f);
                    offset[1] = rng.Range(-1.0f, 1.0f);
                    offset[2] = rng.Range(-1.0f, 1.0f);
                } while (offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] > 1.0f);
            }
            float clip[4] = {};
            TransformPoint(fixture.ViewProjection,
                           sphere.X + offset[0] * sphere.Radius,
                           sphere.Y + offset[1] * sphere.Radius,
                           sphere.Z + offset[2] * sphere.Radius,
                           clip);
            if (!(clip[3] > 1.0e-6f) || clip[2] < 0.0f)
            {
                continue;
            }
            const float ndcX = clip[0] / clip[3];
            const float ndcY = clip[1] / clip[3];
            const float ndcZ = clip[2] / clip[3];
            if (ndcX < -1.0f || ndcX >= 1.0f || ndcY < -1.0f || ndcY >= 1.0f || ndcZ > 1.0f)
            {
                continue;
            }
            const uint32_t pixelX = std::min(width - 1u, static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(width)));
            const uint32_t pixelY = std::min(height - 1u, static_cast<uint32_t>((ndcY * 0.5f + 0.5f) * static_cast<float>(height)));
            if (ndcZ < depth[static_cast<size_t>(pixelY) * width + pixelX])
            {
                return true;
            }
        }
        return false;
    }

    // 誤り（見えているのに隠れていると判定）の数を返す（-1 は実行の失敗）
    int RunRandomScenes(Fixture& fixture, uint32_t width, uint32_t height, uint32_t sceneCount, uint32_t seedBase, uint32_t& hiddenCount, uint32_t& visibleCount)
    {
        int falseHiddenTotal = 0;
        for (uint32_t scene = 0; scene < sceneCount; ++scene)
        {
            Rng rng(seedBase + scene * 7919u + width * 31u + height);

            DepthRect rects[10] = {};
            const size_t rectCount = 3u + rng.NextU32() % 8u;
            for (size_t rectIndex = 0; rectIndex < rectCount; ++rectIndex)
            {
                const float u0 = rng.Range(0.0f, 0.9f);
                const float v0 = rng.Range(0.0f, 0.9f);
                rects[rectIndex] = {u0, v0, std::min(1.0f, u0 + rng.Range(0.02f, 1.0f)), std::min(1.0f, v0 + rng.Range(0.02f, 1.0f)), rng.Range(2.0f, 70.0f)};
            }
            const VariableArray<float> depth = MakeDepth(fixture.ViewProjection, width, height, rects, rectCount);

            Sphere spheres[MaxSpheres] = {};
            for (uint32_t index = 0; index < MaxSpheres; ++index)
            {
                const float z = rng.Range(0.05f, 90.0f);
                spheres[index] = {rng.Range(-1.3f, 1.3f) * fixture.TanHalfX * z,
                                  rng.Range(-1.3f, 1.3f) * fixture.TanHalfY * z,
                                  z,
                                  std::exp(rng.Range(std::log(0.01f), std::log(8.0f))),
                                  false,
                                  "乱数"};
            }

            uint32_t occluded[MaxSpheres] = {};
            if (!RunProbe(fixture, depth, width, height, spheres, MaxSpheres, occluded))
            {
                return -1;
            }

            int sceneFalseHidden = 0;
            for (uint32_t index = 0; index < MaxSpheres; ++index)
            {
                const bool bTrulyVisible = IsSphereTrulyVisible(fixture, depth, width, height, spheres[index], rng);
                if (occluded[index] != 0u)
                {
                    ++hiddenCount;
                    if (bTrulyVisible)
                    {
                        ++sceneFalseHidden;
                        if (falseHiddenTotal + sceneFalseHidden <= 8)
                        {
                            std::cerr << "乱数の場面 " << width << "x" << height << " #" << scene << ": 見えているのに隠れていると判定 中心=("
                                      << spheres[index].X << "," << spheres[index].Y << "," << spheres[index].Z << ") 半径=" << spheres[index].Radius << '\n';
                        }
                    }
                }
                else
                {
                    ++visibleCount;
                }
            }
            falseHiddenTotal += sceneFalseHidden;
        }
        std::cout << "乱数の場面 " << width << "x" << height << " 場面=" << sceneCount << " 球=" << sceneCount * MaxSpheres << " 誤り(見えているのに隠れている)=" << falseHiddenTotal << '\n';
        return falseHiddenTotal;
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
            // クラスタのカリングが共通の関数を取り込んでコンパイルできる
            if (!shaderManager.LoadShader("cluster_cull.comp", RHI::ShaderStage::Compute))
            {
                std::cerr << "cluster_cull.comp をコンパイルできませんでした\n";
                bPassed = false;
            }

            Fixture fixture;
            fixture.Device = device;
            if (!fixture.Pyramid.Initialize(device.get(), &shaderManager))
            {
                std::cerr << "HiZPyramid を初期化できませんでした\n";
                return 1;
            }

            ShaderPtr shader = shaderManager.LoadShader("hiz_occlusion_probe.comp", RHI::ShaderStage::Compute);
            if (!shader)
            {
                std::cerr << "hiz_occlusion_probe.comp をコンパイルできませんでした\n";
                return 1;
            }
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = shader;
            pipelineDesc.descriptorSetLayouts.push_back(MakeProbeDescriptorSetDesc());
            fixture.Pipeline = device->CreateComputePipeline(pipelineDesc);

            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            fixture.Sampler = device->CreateSampler(samplerDesc);

            fixture.ParamBuffer = device->CreateBuffer(BufferDesc(sizeof(ProbeParams), ResourceUsage::ConstantBuffer, true, "HiZOcclusionProbeParams"));
            BufferDesc resultDesc;
            resultDesc.Size = ResultBytes;
            resultDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
            resultDesc.DebugName = "HiZOcclusionProbeResults";
            fixture.ResultBuffer = device->CreateBuffer(resultDesc);
            fixture.ReadbackBuffer = device->CreateBuffer(BufferDesc(ResultBytes, ResourceUsage::TransferDst, true, "HiZOcclusionProbeReadback"));
            if (!fixture.Pipeline || !fixture.Sampler || !fixture.ParamBuffer || !fixture.ResultBuffer || !fixture.ReadbackBuffer)
            {
                std::cerr << "確認用の資源を作れませんでした\n";
                return 1;
            }

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
            constants.CopyShaderViewProjection(fixture.ViewProjection);
            fixture.TanHalfY = std::tan(constants.FieldOfViewRadians * 0.5f);
            fixture.TanHalfX = fixture.TanHalfY * aspect;

            // 偶数と奇数の深度の解像度（奇数は HZB の最後の列・行がはみ出す）
            struct SizeCase
            {
                uint32_t Width;
                uint32_t Height;
            };
            const SizeCase sizes[] = {{160, 90}, {161, 91}, {97, 33}};
            uint32_t falseHidden = 0;
            uint32_t hiddenCount = 0;
            uint32_t visibleCount = 0;
            for (const SizeCase& size : sizes)
            {
                const int namedMismatch = RunNamedScenes(fixture, size.Width, size.Height, falseHidden);
                bPassed = bPassed && namedMismatch == 0;
                const int randomFalseHidden = RunRandomScenes(fixture, size.Width, size.Height, 12u, 1u, hiddenCount, visibleCount);
                bPassed = bPassed && randomFalseHidden == 0;
                if (randomFalseHidden > 0)
                {
                    falseHidden += static_cast<uint32_t>(randomFalseHidden);
                }
            }

            // 判定が自明（全部「隠れていない」）でないこと
            std::cout << "乱数の場面の合計 隠れている=" << hiddenCount << " 隠れていない=" << visibleCount << '\n';
            if (hiddenCount < 100u || visibleCount < 100u)
            {
                std::cerr << "乱数の場面の判定が偏っていて検査になっていません\n";
                bPassed = false;
            }
            std::cout << "FALSE_HIDDEN=" << falseHidden << '\n';
            if (falseHidden != 0u)
            {
                bPassed = false;
            }

            device->WaitIdle();
            fixture.Pyramid.Shutdown();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        if (validationErrorCount != 0u)
        {
            std::cerr << "Vulkan validation errorを検出しました: " << validationErrorCount << '\n';
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
