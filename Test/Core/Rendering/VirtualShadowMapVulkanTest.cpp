// 太陽の VSM の印付け・物理ページの割り当て・消去（vsm_mark.comp・vsm_allocate.comp・vsm_clear.comp / VirtualShadowMapPages）の GPU テスト。
// 合成した深度（既知の 2 枚の平面と空）とカメラで動かし、読み戻した結果を CPU（倍精度）の参照と照合する。
//   ケース A（プールが十分）: 印が付いたページの集合が CPU で求めた集合と一致すること（PCF の核が境界をまたぐときの隣のページを含む）。
//     割り当てたページの表の欄（割り当て済み・dirty・物理ページの番号）・統計・空きページの一覧・消去の一覧・物理ページの中身
//     （割り当てたページは 1.0 のビット、それ以外は触らない）。物理ページの番号が重ならないこと。
//   ケース B（プールが足りない）: 溢れの数が（要求 − プール）に一致し、割り当てた物理ページの番号が重ならず、
//     割り当てないページの欄は 0 のまま。
//   ケース C（核が境界をまたぐ画素だけ）: 隣のページへの印が、画素の位置のページだけの集合より多いこと。
//   ケース D（深度なし）: 印付けをせず、要求も割り当ても 0 で、物理ページに触らない。
//   ケース E（同じ資源での 2 フレーム目）: 前フレームの割り当てを引き継がず、2 フレーム目の集合だけが残ること（毎フレームすべて作り直す）。
// 参照は、段の境界・ページの境界・影の最大距離に近い曖昧な画素を深度の画像から除いて作るので、GPU の単精度との差で揺れない。
// どのケースも Vulkan の validation error が 0 件。Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Container/Containers.h"
#include "Math/Vector3.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapPass.h"

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
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "VirtualShadowMapVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ImageWidth = 128;
    constexpr uint32_t ImageHeight = 72;
    // 物理ページの中身の初期値（消去されたページと見分ける）
    constexpr uint32_t GarbageWord = 0xDEADBEEFu;
    // 曖昧な画素を除く許容（m）。単精度の GPU との差（1e-3 m 程度）より十分大きく、PCF の核の幅（段 0 で 0.125 m）より小さい
    constexpr double AmbiguityToleranceMeters = 0.03;

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

    // ========================================
    // 合成したシーンと CPU の参照
    // ========================================

    struct Scene
    {
        CameraProxy Camera;
        float InverseViewProjection[16] = {};
        float ViewProjection[16] = {};
        float CameraPosition[3] = {};
        VirtualShadowMapClipmapSettings Settings;
        VirtualShadowMapClipmap Clipmap;
    };

    // 列優先の行列（シェーダー向け）と同次座標の積
    void Multiply(const float* matrix, const double* vector, double* out)
    {
        for (uint32_t row = 0; row < 4u; ++row)
        {
            double sum = 0.0;
            for (uint32_t column = 0; column < 4u; ++column)
            {
                sum += static_cast<double>(matrix[column * 4u + row]) * vector[column];
            }
            out[row] = sum;
        }
    }

    // 画素の中心のクリップ座標（深度 depth）から、ワールドの位置（倍精度）を戻す。シェーダーの式と同じ
    void Unproject(const Scene& scene, uint32_t pixelX, uint32_t pixelY, double depth, double* outWorld)
    {
        const double clip[4] = {(static_cast<double>(pixelX) + 0.5) / ImageWidth * 2.0 - 1.0,
                                (static_cast<double>(pixelY) + 0.5) / ImageHeight * 2.0 - 1.0,
                                depth,
                                1.0};
        double world[4] = {};
        Multiply(scene.InverseViewProjection, clip, world);
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            outWorld[axis] = world[axis] / world[3];
        }
    }

    Scene BuildScene(const DevicePtr& device)
    {
        Scene scene;
        scene.Camera.PositionX = 0.0f;
        scene.Camera.PositionY = 3.0f;
        scene.Camera.PositionZ = 6.0f;
        // 前方は (0, -0.25, -1) を正規化。上は右 × 前方
        const float forwardLength = std::sqrt(0.25f * 0.25f + 1.0f);
        scene.Camera.ForwardX = 0.0f;
        scene.Camera.ForwardY = -0.25f / forwardLength;
        scene.Camera.ForwardZ = -1.0f / forwardLength;
        scene.Camera.RightX = 1.0f;
        scene.Camera.RightY = 0.0f;
        scene.Camera.RightZ = 0.0f;
        scene.Camera.UpX = 0.0f;
        scene.Camera.UpY = 1.0f / forwardLength;
        scene.Camera.UpZ = -0.25f / forwardLength;
        scene.Camera.Projection = ProjectionType::Perspective;
        scene.Camera.FieldOfView = 60.0f;
        scene.Camera.NearPlane = 0.1f;
        scene.Camera.FarPlane = 200.0f;
        scene.Camera.AspectRatio = static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight);

        const CameraViewConstants constants =
            CameraViewConstants::BuildForDevice(scene.Camera, scene.Camera.AspectRatio, device.get());
        constants.CopyShaderInverseViewProjection(scene.InverseViewProjection);
        constants.CopyShaderViewProjection(scene.ViewProjection);
        scene.CameraPosition[0] = scene.Camera.PositionX;
        scene.CameraPosition[1] = scene.Camera.PositionY;
        scene.CameraPosition[2] = scene.Camera.PositionZ;

        // 段 0 の幅 1024 m（ページ 8 m・texel 6.25 cm）の 5 段。画素の大きさ × 2^-0.5 に合わせた段が 0〜3 に散る。
        // 核の幅（2 texel）は段 0 で 0.125 m で、曖昧さの許容（0.03 m）より十分大きい
        scene.Settings.LevelCount = 5u;
        scene.Settings.FirstWidthMeters = 1024.0f;
        scene.Settings.MaxShadowDistance = 40.0f;
        const Math::Vector3 sunDirection = Math::Vector3(0.35f, -0.8f, 0.45f);
        scene.Clipmap = BuildVirtualShadowMapClipmap(sunDirection,
                                                     1u,
                                                     Math::Vector3(scene.CameraPosition[0], scene.CameraPosition[1], scene.CameraPosition[2]),
                                                     scene.Settings);
        return scene;
    }

    // 2 枚の平面（地面 y = 0・奥の壁 z = -25）の深度。どちらにも当たらない・上の帯・縦の帯は空（1.0）
    Container::VariableArray<float> BuildDepthImage(const Scene& scene)
    {
        Container::VariableArray<float> image(ImageWidth * ImageHeight, 1.0f);
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                if (pixelY < 8u || (pixelX >= 60u && pixelX < 68u))
                {
                    continue;
                }
                double nearPoint[3] = {};
                double farPoint[3] = {};
                Unproject(scene, pixelX, pixelY, 0.0, nearPoint);
                Unproject(scene, pixelX, pixelY, 1.0, farPoint);
                const double direction[3] = {farPoint[0] - nearPoint[0], farPoint[1] - nearPoint[1], farPoint[2] - nearPoint[2]};
                double bestT = 2.0;
                if (std::abs(direction[1]) > 1.0e-12)
                {
                    const double t = (0.0 - nearPoint[1]) / direction[1];
                    if (t > 0.0 && t < bestT)
                    {
                        bestT = t;
                    }
                }
                if (std::abs(direction[2]) > 1.0e-12)
                {
                    const double t = (-25.0 - nearPoint[2]) / direction[2];
                    if (t > 0.0 && t < bestT)
                    {
                        bestT = t;
                    }
                }
                if (bestT > 1.0)
                {
                    continue;
                }
                const double hit[4] = {nearPoint[0] + direction[0] * bestT,
                                       nearPoint[1] + direction[1] * bestT,
                                       nearPoint[2] + direction[2] * bestT,
                                       1.0};
                double clip[4] = {};
                Multiply(scene.ViewProjection, hit, clip);
                image[pixelY * ImageWidth + pixelX] = static_cast<float>(clip[2] / clip[3]);
            }
        }
        return image;
    }

    enum class PixelKind
    {
        Sky,       // 空（深度 1.0）
        Outside,   // 影の最大の距離の外
        Ambiguous, // 段・ページの境界に近く、単精度の差で結果が変わりうる
        Stable,
    };

    uint32_t PageKey(uint32_t level, int64_t pageX, int64_t pageY)
    {
        return level * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL +
               VirtualShadowMapPageTorusAddress(pageY, VirtualShadowMap::TABLE_DIMENSION) * VirtualShadowMap::TABLE_DIMENSION +
               VirtualShadowMapPageTorusAddress(pageX, VirtualShadowMap::TABLE_DIMENSION);
    }

    // 画素を分類し、安定した画素ではそのページの集合（核の隣を含む / 画素の位置のページだけ）を返す
    PixelKind ClassifyPixel(const Scene& scene,
                            uint32_t pixelX,
                            uint32_t pixelY,
                            float depth,
                            Container::VariableArray<uint32_t>* outKeys,
                            Container::VariableArray<uint32_t>* outPointKeys,
                            uint32_t* outLevel)
    {
        if (!(depth < 1.0f))
        {
            return PixelKind::Sky;
        }
        double world[3] = {};
        Unproject(scene, pixelX, pixelY, static_cast<double>(depth), world);
        const double offset[3] = {world[0] - scene.CameraPosition[0], world[1] - scene.CameraPosition[1], world[2] - scene.CameraPosition[2]};
        const double distance = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);

        const float fovY = scene.Camera.FieldOfView;
        const float height = static_cast<float>(ImageHeight);
        const int32_t level = SelectVirtualShadowMapLevel(scene.Settings, static_cast<float>(distance), fovY, height);
        const int32_t levelNear = SelectVirtualShadowMapLevel(scene.Settings, static_cast<float>(distance - AmbiguityToleranceMeters), fovY, height);
        const int32_t levelFar = SelectVirtualShadowMapLevel(scene.Settings, static_cast<float>(distance + AmbiguityToleranceMeters), fovY, height);
        if (level != levelNear || level != levelFar)
        {
            return PixelKind::Ambiguous;
        }
        if (level < 0)
        {
            return PixelKind::Outside;
        }

        const Math::Vector3 position(static_cast<float>(world[0]), static_cast<float>(world[1]), static_cast<float>(world[2]));
        double lightX = 0.0;
        double lightY = 0.0;
        double lightDepth = 0.0;
        VirtualShadowMapWorldToLightSpace(scene.Clipmap, position, lightX, lightY, lightDepth);
        const VirtualShadowMapClipmapLevel& levelData = scene.Clipmap.Levels[level];
        const double pageMeters = static_cast<double>(levelData.PageMeters);
        const double margin = static_cast<double>(VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS) * static_cast<double>(levelData.TexelMeters);

        // 点とその核の 4 本の境界が、ページの境界から許容以上離れていること
        const double probes[6] = {lightX, lightX - margin, lightX + margin, lightY, lightY - margin, lightY + margin};
        for (const double value : probes)
        {
            const double scaled = value / pageMeters;
            const double fraction = scaled - std::floor(scaled);
            if (std::min(fraction, 1.0 - fraction) * pageMeters < AmbiguityToleranceMeters)
            {
                return PixelKind::Ambiguous;
            }
        }

        const int64_t pageMinX = static_cast<int64_t>(std::floor((lightX - margin) / pageMeters));
        const int64_t pageMaxX = static_cast<int64_t>(std::floor((lightX + margin) / pageMeters));
        const int64_t pageMinY = static_cast<int64_t>(std::floor((lightY - margin) / pageMeters));
        const int64_t pageMaxY = static_cast<int64_t>(std::floor((lightY + margin) / pageMeters));
        const int64_t count = static_cast<int64_t>(scene.Clipmap.PagesPerAxis);
        for (int64_t pageY = pageMinY; pageY <= pageMaxY; ++pageY)
        {
            for (int64_t pageX = pageMinX; pageX <= pageMaxX; ++pageX)
            {
                if (pageX < levelData.OriginPageX || pageX >= levelData.OriginPageX + count || pageY < levelData.OriginPageY ||
                    pageY >= levelData.OriginPageY + count)
                {
                    continue;
                }
                if (outKeys)
                {
                    outKeys->push_back(PageKey(static_cast<uint32_t>(level), pageX, pageY));
                }
            }
        }
        if (outPointKeys)
        {
            outPointKeys->push_back(PageKey(static_cast<uint32_t>(level),
                                            static_cast<int64_t>(std::floor(lightX / pageMeters)),
                                            static_cast<int64_t>(std::floor(lightY / pageMeters))));
        }
        if (outLevel)
        {
            *outLevel = static_cast<uint32_t>(level);
        }
        return PixelKind::Stable;
    }

    void SortUnique(Container::VariableArray<uint32_t>& keys)
    {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }

    struct Reference
    {
        // 核の隣を含む要求の集合（昇順・重複なし）と、画素の位置のページだけの集合
        Container::VariableArray<uint32_t> Keys;
        Container::VariableArray<uint32_t> PointKeys;
        uint32_t LevelMask = 0;
        uint32_t StablePixels = 0;
    };

    Reference BuildReference(const Scene& scene, const Container::VariableArray<float>& image)
    {
        Reference reference;
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                uint32_t level = 0;
                if (ClassifyPixel(scene, pixelX, pixelY, image[pixelY * ImageWidth + pixelX], &reference.Keys, &reference.PointKeys, &level) ==
                    PixelKind::Stable)
                {
                    reference.LevelMask |= 1u << level;
                    ++reference.StablePixels;
                }
            }
        }
        SortUnique(reference.Keys);
        SortUnique(reference.PointKeys);
        return reference;
    }

    // 曖昧な画素を空（1.0）にして、GPU と CPU が同じ結果になる深度の画像にする
    uint32_t RemoveAmbiguousPixels(const Scene& scene, Container::VariableArray<float>& image)
    {
        uint32_t removed = 0;
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                float& depth = image[pixelY * ImageWidth + pixelX];
                if (ClassifyPixel(scene, pixelX, pixelY, depth, nullptr, nullptr, nullptr) == PixelKind::Ambiguous)
                {
                    depth = 1.0f;
                    ++removed;
                }
            }
        }
        return removed;
    }

    // ========================================
    // GPU の実行と読み戻し
    // ========================================

    struct Resources
    {
        uint32_t PoolPages = 0;
        BufferPtr Pool;
        BufferPtr PageTable;
        BufferPtr RequestBits;
        BufferPtr FreeList;
        BufferPtr Stats;
        BufferPtr DirtyList;
        // 本番のパスと同じ用途の読み戻し先（統計のコピー経路の検証用）
        BufferPtr StatsReadback;
    };

    struct Readback
    {
        Container::VariableArray<uint32_t> Pool;
        Container::VariableArray<uint32_t> PageTable;
        Container::VariableArray<uint32_t> RequestBits;
        Container::VariableArray<uint32_t> FreeList;
        Container::VariableArray<uint32_t> Stats;
        Container::VariableArray<uint32_t> StatsCopied;
        Container::VariableArray<uint32_t> DirtyList;
        bool bRecorded = false;
        bool bMarked = false;
    };

    bool CreateResources(const DevicePtr& device, uint32_t poolPages, Resources& resources)
    {
        resources.PoolPages = poolPages;
        const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        resources.Pool = device->CreateBuffer(BufferDesc(VirtualShadowMap::PoolBytes(poolPages), usage, true, "VsmTestPool"));
        resources.PageTable = device->CreateBuffer(BufferDesc(VirtualShadowMap::PageTableBytes(), usage, true, "VsmTestPageTable"));
        resources.RequestBits = device->CreateBuffer(BufferDesc(VirtualShadowMap::RequestBitsBytes(), usage, true, "VsmTestRequestBits"));
        resources.FreeList = device->CreateBuffer(BufferDesc(VirtualShadowMap::FreeListBytes(poolPages), usage, true, "VsmTestFreeList"));
        // 統計は本番と同じ用途（読み戻しのコピー元の TransferSrc を含む）で作る
        resources.Stats = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsBufferUsage() | ResourceUsage::ShaderRead, true, "VsmTestStats"));
        resources.StatsReadback = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsReadbackUsage(), true, "VsmTestStatsReadback"));
        resources.DirtyList = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::DirtyListBytes(poolPages), usage | ResourceUsage::IndirectBuffer, true, "VsmTestDirtyList"));
        if (!resources.Pool || !resources.PageTable || !resources.RequestBits || !resources.FreeList || !resources.Stats ||
            !resources.DirtyList || !resources.StatsReadback)
        {
            return false;
        }
        // 物理ページを見張りの値で埋める（消去されたページと、触られなかったページを見分ける）。ほかは見張りで埋めて、書かれたかを確かめる
        for (const BufferPtr& buffer : {resources.Pool, resources.PageTable, resources.RequestBits, resources.FreeList, resources.Stats,
                                         resources.DirtyList, resources.StatsReadback})
        {
            uint32_t* mapped = static_cast<uint32_t*>(buffer->Map(0u, buffer->GetSize()));
            if (mapped == nullptr)
            {
                return false;
            }
            for (uint64_t word = 0; word < buffer->GetSize() / sizeof(uint32_t); ++word)
            {
                mapped[word] = GarbageWord;
            }
            buffer->Unmap();
        }
        return true;
    }

    TexturePtr CreateDepthTexture(const DevicePtr& device, const Container::VariableArray<float>& image)
    {
        TextureDesc desc;
        desc.Width = ImageWidth;
        desc.Height = ImageHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = Format::R32_FLOAT;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = ResourceUsage::ShaderResource;
        desc.DebugName = "VsmTestDepth";
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        texture->Update(image.data(), ImageWidth * sizeof(float), ImageWidth * ImageHeight * sizeof(float));
        return texture;
    }

    bool ReadAll(const BufferPtr& buffer, Container::VariableArray<uint32_t>& out)
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

    // 記録して読み戻す。depth が null なら深度なし。frameSerial は 0 以外で呼び出しごとに増やす
    bool RunPages(const DevicePtr& device,
                  VirtualShadowMapPages& pages,
                  const Scene& scene,
                  const Resources& resources,
                  const TexturePtr& depth,
                  bool bPassClipmap,
                  uint64_t frameSerial,
                  bool bFirstUse,
                  Readback& readback)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << TestName << " コマンドリストを作れませんでした" << std::endl;
            return false;
        }
        VirtualShadowMapPagesDispatch dispatch;
        dispatch.PoolPages = resources.PoolPages;
        dispatch.Pool = resources.Pool;
        dispatch.PageTable = resources.PageTable;
        dispatch.RequestBits = resources.RequestBits;
        dispatch.FreeList = resources.FreeList;
        dispatch.Stats = resources.Stats;
        dispatch.DirtyList = resources.DirtyList;
        dispatch.Depth = depth;
        dispatch.Clipmap = bPassClipmap ? &scene.Clipmap : nullptr;
        std::memcpy(dispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(dispatch.InverseViewProjection));
        std::memcpy(dispatch.CameraPosition, scene.CameraPosition, sizeof(dispatch.CameraPosition));
        dispatch.FovYDegrees = scene.Camera.FieldOfView;

        const BufferPtr buffers[] = {resources.Pool, resources.PageTable, resources.RequestBits,
                                     resources.FreeList, resources.Stats, resources.DirtyList};
        pages.BeginFrame(0, frameSerial);
        commandList->Begin();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, bFirstUse ? ResourceState::Undefined : ResourceState::HostRead, ResourceState::UnorderedAccess,
                                       0u, buffer->GetSize());
        }
        readback.bRecorded = pages.Record(commandList.get(), dispatch);
        readback.bMarked = pages.WasMarked();
        // 本番のパスと同じ読み戻しのコピー（統計 → 読み戻し先）。検証レイヤーが用途のフラグの不足を検出する
        VirtualShadowMap::RecordStatsReadback(*commandList, resources.Stats, resources.StatsReadback);
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        return ReadAll(resources.Pool, readback.Pool) && ReadAll(resources.PageTable, readback.PageTable) &&
               ReadAll(resources.RequestBits, readback.RequestBits) && ReadAll(resources.FreeList, readback.FreeList) &&
               ReadAll(resources.Stats, readback.Stats) && ReadAll(resources.DirtyList, readback.DirtyList) &&
               ReadAll(resources.StatsReadback, readback.StatsCopied);
    }

    // 要求のビット列から、立っているビットの番号（昇順）
    Container::VariableArray<uint32_t> DecodeRequestKeys(const Container::VariableArray<uint32_t>& words)
    {
        Container::VariableArray<uint32_t> keys;
        for (uint32_t wordIndex = 0; wordIndex < words.size(); ++wordIndex)
        {
            for (uint32_t bit = 0; bit < 32u; ++bit)
            {
                if ((words[wordIndex] >> bit) & 1u)
                {
                    keys.push_back(wordIndex * 32u + bit);
                }
            }
        }
        return keys;
    }

    void PrintKeys(const char* label, const Container::VariableArray<uint32_t>& keys)
    {
        std::cerr << TestName << " " << label << " (" << keys.size() << "):";
        for (const uint32_t key : keys)
        {
            std::cerr << " " << key / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL << ":"
                      << (key % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL) % VirtualShadowMap::TABLE_DIMENSION << ","
                      << (key % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL) / VirtualShadowMap::TABLE_DIMENSION;
        }
        std::cerr << std::endl;
    }

    // 結果が参照の集合と整合していること。expectedKeys は要求のあるページ（昇順・重複なし）、
    // untouchedPhysicalGarbage は割り当てなかった物理ページが見張りのままであることを確かめるか
    void CheckAllocation(const char* label,
                         const Readback& readback,
                         const Container::VariableArray<uint32_t>& expectedKeys,
                         uint32_t poolPages,
                         uint32_t expectedLevelMask,
                         bool bExpectPoolGarbageElsewhere)
    {
        Expect(readback.bRecorded, "印付け・割り当て・消去を記録できなければならない");
        const uint32_t requested = static_cast<uint32_t>(expectedKeys.size());
        const uint32_t expectedAllocated = std::min(requested, poolPages);

        const Container::VariableArray<uint32_t> markedKeys = DecodeRequestKeys(readback.RequestBits);
        const bool bKeysMatch = markedKeys == expectedKeys;
        if (!bKeysMatch)
        {
            std::cerr << TestName << " " << label << " 印が付いたページの集合が参照と違う" << std::endl;
            PrintKeys("GPU", markedKeys);
            PrintKeys("参照", expectedKeys);
        }
        Expect(bKeysMatch, "印が付いたページの集合が CPU の参照と一致しなければならない");

        // 統計
        Expect(readback.Stats[VirtualShadowMap::StatRequested] == requested, "統計の要求の数が参照と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatAllocated] == expectedAllocated, "統計の割り当ての数が min(要求, プール) でなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatOverflow] == requested - expectedAllocated, "統計の溢れの数が 要求 − 割り当て でなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatDrawn] == 0u, "描いたページの数はこのパスでは 0 のはず");
        // 読み戻し先へのコピーは統計の全語と一致しなければならない（本番の読み戻しの経路）
        Expect(readback.StatsCopied.size() == readback.Stats.size() &&
                   std::equal(readback.Stats.begin(), readback.Stats.end(), readback.StatsCopied.begin()),
               "読み戻し先へコピーした統計がコピー元と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatLevelsUsed] == expectedLevelMask, "要求のあった段の集合が参照と一致しなければならない");

        // ページの表: 要求のあるページは割り当て済み・dirty・物理ページの番号（重ならない）、割り当てなかったページと要求の無いページは 0
        Container::VariableArray<uint32_t> physicalPages;
        uint32_t allocatedEntries = 0;
        for (uint32_t index = 0; index < readback.PageTable.size(); ++index)
        {
            const uint32_t entry = readback.PageTable[index];
            const bool bRequested = std::binary_search(expectedKeys.begin(), expectedKeys.end(), index);
            if (entry == 0u)
            {
                continue;
            }
            Expect(bRequested, "要求の無いページに割り当ててはならない");
            Expect((entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) != 0u && (entry & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u,
                   "割り当てたページの表の欄に、割り当て済みと dirty の印が要る");
            Expect((entry & ~(VirtualShadowMap::PAGE_ENTRY_ALLOCATED | VirtualShadowMap::PAGE_ENTRY_DIRTY | VirtualShadowMap::PAGE_INDEX_MASK)) == 0u,
                   "ページの表の欄に余計なビットが立ってはならない");
            const uint32_t physical = entry & VirtualShadowMap::PAGE_INDEX_MASK;
            Expect(physical < poolPages, "物理ページの番号がプールの中でなければならない");
            physicalPages.push_back(physical);
            ++allocatedEntries;
        }
        Expect(allocatedEntries == expectedAllocated, "ページの表で割り当て済みの欄の数が min(要求, プール) でなければならない");
        Container::VariableArray<uint32_t> sortedPhysical = physicalPages;
        std::sort(sortedPhysical.begin(), sortedPhysical.end());
        Expect(std::adjacent_find(sortedPhysical.begin(), sortedPhysical.end()) == sortedPhysical.end(),
               "割り当てた物理ページの番号が重なってはならない");

        // 空きページの一覧: 数 = プール − 割り当て、残りの空き + 割り当てた物理ページ = 全ページが 1 回ずつ
        Expect(readback.FreeList[0] == poolPages - expectedAllocated, "空きページの数が プール − 割り当て でなければならない");
        Container::VariableArray<uint32_t> everyPage = sortedPhysical;
        for (uint32_t slot = 0; slot < readback.FreeList[0] && slot + 1u < readback.FreeList.size(); ++slot)
        {
            everyPage.push_back(readback.FreeList[1u + slot]);
        }
        std::sort(everyPage.begin(), everyPage.end());
        bool bPermutation = everyPage.size() == poolPages;
        for (uint32_t page = 0; bPermutation && page < poolPages; ++page)
        {
            bPermutation = everyPage[page] == page;
        }
        Expect(bPermutation, "空きの一覧と割り当てた物理ページで、全ページがちょうど 1 回ずつ現れなければならない");

        // 消去する一覧: 引数 (min(数, 65535), 1, 1)・数・物理ページの番号の集合
        Expect(readback.DirtyList[VirtualShadowMap::DIRTY_LIST_COUNT_WORD] == expectedAllocated, "消去する一覧の数が割り当てた数でなければならない");
        Expect(readback.DirtyList[0] == std::min(expectedAllocated, VirtualShadowMap::GROUP_COUNT_X_LIMIT) && readback.DirtyList[1] == 1u &&
                   readback.DirtyList[2] == 1u,
               "消去する一覧の間接 dispatch の引数が (min(数, 上限), 1, 1) でなければならない");
        Container::VariableArray<uint32_t> dirtyPages;
        for (uint32_t index = 0; index < expectedAllocated; ++index)
        {
            dirtyPages.push_back(readback.DirtyList[VirtualShadowMap::DIRTY_LIST_HEADER_WORDS + index]);
        }
        std::sort(dirtyPages.begin(), dirtyPages.end());
        Expect(dirtyPages == sortedPhysical, "消去する一覧の物理ページの集合が、ページの表へ割り当てた集合と一致しなければならない");

        // 物理ページの中身: 割り当てたページはすべて 1.0 のビット、ほかは触られていない（見張りのまま）
        uint32_t badWords = 0;
        for (uint32_t page = 0; page < poolPages; ++page)
        {
            const bool bAllocated = std::binary_search(sortedPhysical.begin(), sortedPhysical.end(), page);
            const uint32_t expectedWord = bAllocated ? VirtualShadowMap::EMPTY_DEPTH_BITS : GarbageWord;
            if (!bAllocated && !bExpectPoolGarbageElsewhere)
            {
                continue;
            }
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                badWords += readback.Pool[static_cast<size_t>(page) * VirtualShadowMap::PAGE_WORDS + word] != expectedWord ? 1u : 0u;
            }
        }
        if (badWords != 0)
        {
            std::cerr << TestName << " " << label << " 物理ページの中身が期待と違う語の数: " << badWords << std::endl;
        }
        Expect(badWords == 0u, "割り当てたページは 1.0 のビットで埋まり、割り当てなかったページは触られていてはならない");
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
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
            std::cerr << TestName << " ShaderManagerを初期化できませんでした" << std::endl;
            return 1;
        }

        {
            VirtualShadowMapPages pages;
            if (!pages.Initialize(device.get(), &shaderManager))
            {
                std::cerr << TestName << " VSM の計算パイプラインを初期化できませんでした" << std::endl;
                return 1;
            }

            const Scene scene = BuildScene(device);
            Expect(scene.Clipmap.bEnabled, "テストのクリップマップが有効でなければならない");
            Container::VariableArray<float> image = BuildDepthImage(scene);
            const uint32_t removedPixels = RemoveAmbiguousPixels(scene, image);
            const Reference reference = BuildReference(scene, image);
            std::cout << TestName << " シーン: 安定した画素=" << reference.StablePixels << " 除いた曖昧な画素=" << removedPixels
                      << " ページ=" << reference.Keys.size() << "（画素の位置だけなら " << reference.PointKeys.size() << "）段の集合=0x"
                      << std::hex << reference.LevelMask << std::dec << std::endl;
            // 参照が空・単一の段・隣のページが無いだけの退化したシーンでは、何も確かめられない
            Expect(reference.Keys.size() >= 6, "参照のページが少なすぎる（シーンが退化している）");
            Expect(reference.LevelMask != 0u && (reference.LevelMask & (reference.LevelMask - 1u)) != 0u, "複数の段が使われるシーンでなければならない");
            const uint32_t requested = static_cast<uint32_t>(reference.Keys.size());

            const TexturePtr depth = CreateDepthTexture(device, image);
            if (!depth)
            {
                std::cerr << TestName << " 深度のテクスチャを作れませんでした" << std::endl;
                return 1;
            }

            uint64_t frameSerial = 1;

            // ----- ケース A: プールが十分 -----
            {
                Resources resources;
                Readback readback;
                const uint32_t poolPages = requested + 24u;
                if (!CreateResources(device, poolPages, resources) ||
                    !RunPages(device, pages, scene, resources, depth, true, frameSerial++, true, readback))
                {
                    std::cerr << TestName << " ケース A を実行できませんでした" << std::endl;
                    return 1;
                }
                Expect(readback.bMarked, "印付けを記録しなければならない");
                CheckAllocation("A", readback, reference.Keys, poolPages, reference.LevelMask, true);
                std::cout << TestName << " ケース A: 要求=" << readback.Stats[0] << " 割り当て=" << readback.Stats[1] << " 溢れ=" << readback.Stats[2]
                          << " 段の集合=0x" << std::hex << readback.Stats[4] << std::dec << std::endl;
            }

            // ----- ケース B: プールが足りない（要求の約半分。1 つの溢れも出ない大きさも確かめる） -----
            for (const uint32_t poolPages : {requested / 2u, requested - 1u, requested, 1u})
            {
                Resources resources;
                Readback readback;
                if (poolPages == 0u || !CreateResources(device, poolPages, resources) ||
                    !RunPages(device, pages, scene, resources, depth, true, frameSerial++, true, readback))
                {
                    std::cerr << TestName << " ケース B を実行できませんでした（プール " << poolPages << "）" << std::endl;
                    return 1;
                }
                CheckAllocation("B", readback, reference.Keys, poolPages, reference.LevelMask, true);
                std::cout << TestName << " ケース B: プール=" << poolPages << " 要求=" << readback.Stats[0] << " 割り当て=" << readback.Stats[1]
                          << " 溢れ=" << readback.Stats[2] << std::endl;
            }

            // ----- ケース C: 核が境界をまたぐ画素だけ（隣のページへの印） -----
            Container::VariableArray<float> sparseImage(ImageWidth * ImageHeight, 1.0f);
            Reference sparse;
            {
                // 安定した画素のうち、核が隣のページへかかるもの（近傍込みの集合が画素の位置のページだけより多い）を、段ごとに最大 2 つ選ぶ
                uint32_t pickedPerLevel[VirtualShadowMapMaxLevels] = {};
                uint32_t pickedPixels = 0;
                for (uint32_t pixelY = 0; pixelY < ImageHeight && pickedPixels < 8u; pixelY += 3u)
                {
                    for (uint32_t pixelX = 0; pixelX < ImageWidth && pickedPixels < 8u; pixelX += 5u)
                    {
                        Container::VariableArray<uint32_t> keys;
                        Container::VariableArray<uint32_t> pointKeys;
                        uint32_t level = 0;
                        const float depthValue = image[pixelY * ImageWidth + pixelX];
                        if (ClassifyPixel(scene, pixelX, pixelY, depthValue, &keys, &pointKeys, &level) != PixelKind::Stable ||
                            keys.size() <= 1u || pickedPerLevel[level] >= 2u)
                        {
                            continue;
                        }
                        ++pickedPerLevel[level];
                        ++pickedPixels;
                        sparseImage[pixelY * ImageWidth + pixelX] = depthValue;
                    }
                }
                // 近傍のページを持たない画素も、境界から離れた画素を 3 つ足す（位置のページだけが立つこと）
                uint32_t interior = 0;
                for (uint32_t pixelY = 10; pixelY < ImageHeight && interior < 3u; pixelY += 7u)
                {
                    for (uint32_t pixelX = 3; pixelX < ImageWidth && interior < 3u; pixelX += 11u)
                    {
                        Container::VariableArray<uint32_t> keys;
                        const float depthValue = image[pixelY * ImageWidth + pixelX];
                        if (ClassifyPixel(scene, pixelX, pixelY, depthValue, &keys, nullptr, nullptr) == PixelKind::Stable && keys.size() == 1u)
                        {
                            sparseImage[pixelY * ImageWidth + pixelX] = depthValue;
                            ++interior;
                        }
                    }
                }
                sparse = BuildReference(scene, sparseImage);
                std::cout << TestName << " ケース C: 選んだ画素=" << sparse.StablePixels << " 近傍込みのページ=" << sparse.Keys.size()
                          << " 画素の位置のページだけ=" << sparse.PointKeys.size() << std::endl;
                Expect(pickedPixels >= 3u, "核が境界をまたぐ画素が見つからない（シーンが退化している）");
                Expect(sparse.Keys.size() > sparse.PointKeys.size(),
                       "核が境界をまたぐ画素では、近傍込みのページが画素の位置のページだけより多くなければならない（隣のページへの印の検査）");

                const TexturePtr sparseDepth = CreateDepthTexture(device, sparseImage);
                Resources resources;
                Readback readback;
                const uint32_t poolPages = static_cast<uint32_t>(sparse.Keys.size()) + 8u;
                if (!sparseDepth || !CreateResources(device, poolPages, resources) ||
                    !RunPages(device, pages, scene, resources, sparseDepth, true, frameSerial++, true, readback))
                {
                    std::cerr << TestName << " ケース C を実行できませんでした" << std::endl;
                    return 1;
                }
                CheckAllocation("C", readback, sparse.Keys, poolPages, sparse.LevelMask, true);

                // ----- ケース E: 同じ資源での 2 フレーム目（前フレームの割り当てを引き継がない） -----
                // 1 フレーム目は全シーン（プールをこのケースの大きさに合わせるため、ケース C の資源を使い回すので、まず C の疎な画像の結果が残っている）。
                // 続けて全シーンの深度で実行し、疎な画像の割り当てが残らないこと、次に疎な画像へ戻して全シーンの割り当てが残らないことを確かめる
                Resources shared;
                Readback first;
                Readback second;
                const uint32_t sharedPages = requested + 8u;
                if (!CreateResources(device, sharedPages, shared) ||
                    !RunPages(device, pages, scene, shared, sparseDepth, true, frameSerial++, true, first) ||
                    !RunPages(device, pages, scene, shared, depth, true, frameSerial++, false, second))
                {
                    std::cerr << TestName << " ケース E を実行できませんでした" << std::endl;
                    return 1;
                }
                CheckAllocation("E1", first, sparse.Keys, sharedPages, sparse.LevelMask, true);
                // 2 フレーム目の物理ページの中身は、1 フレーム目に消去したページが残るので、見張りの検査はしない
                CheckAllocation("E2", second, reference.Keys, sharedPages, reference.LevelMask, false);
                Readback third;
                if (!RunPages(device, pages, scene, shared, sparseDepth, true, frameSerial++, false, third))
                {
                    std::cerr << TestName << " ケース E の 3 フレーム目を実行できませんでした" << std::endl;
                    return 1;
                }
                CheckAllocation("E3", third, sparse.Keys, sharedPages, sparse.LevelMask, false);
            }

            // ----- ケース D: 深度なし・クリップマップなし -----
            for (const bool bWithDepth : {false, true})
            {
                Resources resources;
                Readback readback;
                const uint32_t poolPages = 16;
                if (!CreateResources(device, poolPages, resources) ||
                    !RunPages(device, pages, scene, resources, bWithDepth ? depth : TexturePtr{}, !bWithDepth, frameSerial++, true, readback))
                {
                    std::cerr << TestName << " ケース D を実行できませんでした" << std::endl;
                    return 1;
                }
                Expect(!readback.bMarked, "深度かクリップマップが無いときは印付けを記録してはならない");
                CheckAllocation("D", readback, Container::VariableArray<uint32_t>{}, poolPages, 0u, true);
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
