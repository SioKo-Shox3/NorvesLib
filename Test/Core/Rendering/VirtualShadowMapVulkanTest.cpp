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
//   ケース F（印付け → 割り当て → 消去 → 展開 → 描画）: 合成した地面の深度の上に、ページの境界をまたぐ四角形（傾いた平面）と、
//     一部が重なる遠い四角形（ローカルの頂点 + 変換）を置き、物理プールの全 texel が形の和の参照（覆われた texel は手前の深度、ほかは 1.0。
//     形の縁から半 texel 未満の texel は除く）と一致すること。展開の統計・間接描画の引数・インスタンスの整合も確かめる。
//   ケース G（ページの表を直接書く）: 段 0 の 24 ページ（うち 1 ページは dirty でなく、触られない）と段 1 の 4 ページに、縁がページの中を斜めに通る
//     大きな三角形（24 ページ以上をまたぐ）と、その縁をまたぐ遠い四角形を描き、全 texel が参照と一致すること。
//     ケース H: インスタンスの容量が 1 足りないとき、描かずに溢れとして数え（物理ページは触らない）、ちょうど足りるときは描くこと。
//   ケース J（MegaGeometry の投影物のカリング。vsm_dirty_mips.comp・vsm_mega_cull.comp）: 合成した完全二分木のクラスタを本番のカリングに通し、
//     段の texel が 2 倍になるごとに選ばれるクラスタが粗くなり（葉 8・中間 4・2・根 1）、どの葉から根への道でもちょうど 1 つが選ばれること
//     （自分の誤差 ÷ texel ≤ 1 かつ親の誤差 ÷ texel > 1）、インスタンスの判定（段の範囲・深度の範囲・dirty のページの階層）、
//     出力の一覧の溢れ・統計を確かめる。dirty の階層は CPU の参照と全語一致する。
//   ケース K（MegaGeometry のクラスタの記録の経路）: ケース F と同じ場面を、形ごとに MegaGeometry のクラスタ 1 つにして、本番の流れ
//     （印付け → 割り当て → 消去 → カリング → クラスタの記録 → 展開 → 描画）に通し、物理プールが形の和の参照とケース F の手続きの経路と全 texel で一致すること、
//     GPU が作ったクラスタの記録（種類・インデックスの先頭・頂点の基点・アドレス・段の集合・変換・境界）と展開の引数が一覧の件と整合することを確かめる。
// 参照は、段の境界・ページの境界・影の最大距離に近い曖昧な画素を深度の画像から除いて作るので、GPU の単精度との差で揺れない。
// どのケースも Vulkan の validation error が 0 件。Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Container/Containers.h"
#include "Math/Vector3.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VirtualShadowMapCasters.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VirtualShadowMapRaster.h"
#include "Rendering/VisibilityBuffer.h"

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

    // ========================================
    // 影の塊の展開・描画（VirtualShadowMapRaster）
    // ========================================

    // 影の投影物の形。ライト空間（x = 右、y = 上）の矩形か三角形と、ライト空間の深度の平面
    struct Shape
    {
        bool bRect = false;
        double MinX = 0.0;
        double MaxX = 0.0;
        double MinY = 0.0;
        double MaxY = 0.0;
        double Vertex[3][2] = {};
        // 深度の平面: ライト空間の深度 = PlaneBase + PlaneX * x + PlaneY * y
        double PlaneBase = 0.0;
        double PlaneX = 0.0;
        double PlaneY = 0.0;
        // 頂点をローカル空間で持ち、ワールドへ拡大（2 倍）・平行移動する変換を記録に持たせる
        bool bLocalTransform = false;
    };

    void SetPlane(Shape& shape, double refX, double refY, double refDepth, double slopeX, double slopeY)
    {
        shape.PlaneX = slopeX;
        shape.PlaneY = slopeY;
        shape.PlaneBase = refDepth - slopeX * refX - slopeY * refY;
    }

    Shape MakeRect(double minX, double maxX, double minY, double maxY)
    {
        Shape shape;
        shape.bRect = true;
        shape.MinX = minX;
        shape.MaxX = maxX;
        shape.MinY = minY;
        shape.MaxY = maxY;
        return shape;
    }

    Shape MakeTriangle(double x0, double y0, double x1, double y1, double x2, double y2)
    {
        Shape shape;
        shape.Vertex[0][0] = x0;
        shape.Vertex[0][1] = y0;
        shape.Vertex[1][0] = x1;
        shape.Vertex[1][1] = y1;
        shape.Vertex[2][0] = x2;
        shape.Vertex[2][1] = y2;
        return shape;
    }

    double ShapePlaneDepth(const Shape& shape, double x, double y)
    {
        return shape.PlaneBase + shape.PlaneX * x + shape.PlaneY * y;
    }

    // 点から形の縁までの符号つきの距離（内側が正）
    double ShapeSignedDistance(const Shape& shape, double x, double y)
    {
        if (shape.bRect)
        {
            return std::min(std::min(x - shape.MinX, shape.MaxX - x), std::min(y - shape.MinY, shape.MaxY - y));
        }
        const double area = (shape.Vertex[1][0] - shape.Vertex[0][0]) * (shape.Vertex[2][1] - shape.Vertex[0][1]) -
                            (shape.Vertex[1][1] - shape.Vertex[0][1]) * (shape.Vertex[2][0] - shape.Vertex[0][0]);
        const double orientation = area >= 0.0 ? 1.0 : -1.0;
        double distance = 1.0e300;
        for (uint32_t edge = 0; edge < 3u; ++edge)
        {
            const double ax = shape.Vertex[edge][0];
            const double ay = shape.Vertex[edge][1];
            const double bx = shape.Vertex[(edge + 1u) % 3u][0];
            const double by = shape.Vertex[(edge + 1u) % 3u][1];
            const double length = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            const double cross = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
            distance = std::min(distance, orientation * cross / length);
        }
        return distance;
    }

    Math::Vector3 LightToWorld(const Scene& scene, double lightX, double lightY, double lightDepth)
    {
        const VirtualShadowMapClipmap& clipmap = scene.Clipmap;
        return Math::Vector3(
            static_cast<float>(clipmap.LightRight.x * lightX + clipmap.LightUp.x * lightY + clipmap.Direction.x * lightDepth),
            static_cast<float>(clipmap.LightRight.y * lightX + clipmap.LightUp.y * lightY + clipmap.Direction.y * lightDepth),
            static_cast<float>(clipmap.LightRight.z * lightX + clipmap.LightUp.z * lightY + clipmap.Direction.z * lightDepth));
    }

    // ローカル空間の頂点にかけるワールドへの変換（2 倍して平行移動）
    constexpr float LocalScale = 2.0f;
    constexpr float LocalOffset[3] = {1.5f, -2.5f, 0.75f};

    struct ChunkGeometry
    {
        // 頂点は 1 つ 8 個の float（位置 3・法線 3・UV 2）。全部の塊を 1 本の頂点・インデックスの列へ詰める
        Container::VariableArray<float> Vertices;
        Container::VariableArray<uint32_t> Indices;
        Container::VariableArray<VsmShadowChunk> Chunks;
    };

    // 形から塊の記録（アドレス以外）と頂点・インデックスを作る。塊ごとに VertexBase・FirstIndex を進めて、
    // 頂点の読み方（インデックス + 頂点の基点、インデックスの先頭）も確かめる
    ChunkGeometry BuildChunks(const Scene& scene, const Container::VariableArray<Shape>& shapes)
    {
        ChunkGeometry geometry;
        // 先頭に 1 つ、読まれない余りのインデックスを置く（FirstIndex が 3 の倍数でない塊を作る）
        geometry.Indices.push_back(0u);
        for (const Shape& shape : shapes)
        {
            double corners[4][2] = {};
            uint32_t cornerCount = 0;
            if (shape.bRect)
            {
                const double rect[4][2] = {{shape.MinX, shape.MinY}, {shape.MaxX, shape.MinY}, {shape.MaxX, shape.MaxY}, {shape.MinX, shape.MaxY}};
                std::memcpy(corners, rect, sizeof(rect));
                cornerCount = 4u;
            }
            else
            {
                std::memcpy(corners, shape.Vertex, sizeof(shape.Vertex));
                cornerCount = 3u;
            }

            VsmShadowChunk chunk;
            chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
            chunk.Record.TriangleCount = shape.bRect ? 2u : 1u;
            chunk.Record.FirstIndex = static_cast<uint32_t>(geometry.Indices.size());
            chunk.Record.VertexBase = static_cast<uint32_t>(geometry.Vertices.size() / 8u);
            if (shape.bLocalTransform)
            {
                chunk.World[0] = LocalScale;
                chunk.World[3] = LocalOffset[0];
                chunk.World[5] = LocalScale;
                chunk.World[7] = LocalOffset[1];
                chunk.World[10] = LocalScale;
                chunk.World[11] = LocalOffset[2];
            }

            float boundsMin[3] = {1.0e30f, 1.0e30f, 1.0e30f};
            float boundsMax[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
            for (uint32_t corner = 0; corner < cornerCount; ++corner)
            {
                const Math::Vector3 world = LightToWorld(scene, corners[corner][0], corners[corner][1], ShapePlaneDepth(shape, corners[corner][0], corners[corner][1]));
                const float worldXyz[3] = {world.x, world.y, world.z};
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    boundsMin[axis] = std::min(boundsMin[axis], worldXyz[axis]);
                    boundsMax[axis] = std::max(boundsMax[axis], worldXyz[axis]);
                    const float local = shape.bLocalTransform ? (worldXyz[axis] - LocalOffset[axis]) / LocalScale : worldXyz[axis];
                    geometry.Vertices.push_back(local);
                }
                // 法線と UV は読まない
                for (uint32_t pad = 0; pad < 5u; ++pad)
                {
                    geometry.Vertices.push_back(0.0f);
                }
            }
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                chunk.BoundsMin[axis] = boundsMin[axis];
                chunk.BoundsMax[axis] = boundsMax[axis];
            }
            const uint32_t localIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
            for (uint32_t index = 0; index < chunk.Record.TriangleCount * 3u; ++index)
            {
                geometry.Indices.push_back(localIndices[index]);
            }
            geometry.Chunks.push_back(chunk);
        }
        return geometry;
    }

    struct RasterBuffers
    {
        BufferPtr Vertices;
        BufferPtr Indices;
        BufferPtr Chunks;
        BufferPtr Instances;
        BufferPtr Draws;
        uint32_t ChunkCount = 0;
        uint32_t InstanceCapacity = 0;
    };

    bool CreateRasterBuffers(const DevicePtr& device, ChunkGeometry& geometry, uint32_t instanceCapacity, RasterBuffers& out)
    {
        const uint64_t vertexBytes = geometry.Vertices.size() * sizeof(float);
        const uint64_t indexBytes = geometry.Indices.size() * sizeof(uint32_t);
        out.Vertices = device->CreateBuffer(
            BufferDesc(vertexBytes, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmTestVertices"));
        out.Indices = device->CreateBuffer(
            BufferDesc(indexBytes, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmTestIndices"));
        if (!out.Vertices || !out.Indices || out.Vertices->GetDeviceAddress() == 0u || out.Indices->GetDeviceAddress() == 0u)
        {
            return false;
        }
        out.Vertices->Update(geometry.Vertices.data(), vertexBytes);
        out.Indices->Update(geometry.Indices.data(), indexBytes);
        for (VsmShadowChunk& chunk : geometry.Chunks)
        {
            chunk.Record.VertexAddress = out.Vertices->GetDeviceAddress();
            chunk.Record.IndexAddress = out.Indices->GetDeviceAddress();
        }
        out.ChunkCount = static_cast<uint32_t>(geometry.Chunks.size());
        out.InstanceCapacity = instanceCapacity;
        const uint64_t chunkBytes = VirtualShadowMap::RasterChunkBytes(out.ChunkCount);
        out.Chunks = device->CreateBuffer(BufferDesc(chunkBytes, VirtualShadowMap::RasterChunkUsage(), true, "VsmTestChunks"));
        out.Instances = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterInstanceBytes(instanceCapacity), VirtualShadowMap::RasterInstanceUsage(), true, "VsmTestInstances"));
        out.Draws = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterDrawBytes(out.ChunkCount), VirtualShadowMap::RasterDrawUsage(), true, "VsmTestDraws"));
        if (!out.Chunks || !out.Instances || !out.Draws)
        {
            return false;
        }
        out.Chunks->Update(geometry.Chunks.data(), geometry.Chunks.size() * sizeof(VsmShadowChunk));
        // インスタンスと引数は、展開が書いた分だけが意味を持つことを確かめるため、見張りで埋める
        for (const BufferPtr& buffer : {out.Instances, out.Draws})
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

    void FillWords(const BufferPtr& buffer, uint32_t value)
    {
        uint32_t* mapped = static_cast<uint32_t*>(buffer->Map(0u, buffer->GetSize()));
        for (uint64_t word = 0; mapped != nullptr && word < buffer->GetSize() / sizeof(uint32_t); ++word)
        {
            mapped[word] = value;
        }
        if (mapped != nullptr)
        {
            buffer->Unmap();
        }
    }

    struct RasterReadback
    {
        Container::VariableArray<uint32_t> Pool;
        Container::VariableArray<uint32_t> PageTable;
        Container::VariableArray<uint32_t> Stats;
        Container::VariableArray<uint32_t> Draws;
        Container::VariableArray<uint32_t> Instances;
        bool bPagesRecorded = false;
        bool bRasterRecorded = false;
        uint32_t DrawCount = 0;
    };

    // pages が null でなければ、印付け → 割り当て → 消去（深度 depth）の後に、展開 → 描画を同じコマンドリストで記録する。
    // null なら、ホストが書いたページの表・物理ページへ展開 → 描画だけを記録する
    bool RunRaster(const DevicePtr& device,
                   VirtualShadowMapPages* pages,
                   VirtualShadowMapRaster& raster,
                   const Scene& scene,
                   const Resources& resources,
                   const RasterBuffers& rasterBuffers,
                   const TexturePtr& depth,
                   uint64_t frameSerial,
                   RasterReadback& readback)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << TestName << " コマンドリストを作れませんでした" << std::endl;
            return false;
        }
        const BufferPtr buffers[] = {resources.Pool,      resources.PageTable, resources.RequestBits, resources.FreeList,
                                     resources.Stats,     resources.DirtyList, rasterBuffers.Chunks,   rasterBuffers.Instances,
                                     rasterBuffers.Draws};
        if (pages != nullptr)
        {
            pages->BeginFrame(0, frameSerial);
        }
        raster.BeginFrame(0, frameSerial);
        commandList->Begin();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        if (pages != nullptr)
        {
            VirtualShadowMapPagesDispatch pagesDispatch;
            pagesDispatch.PoolPages = resources.PoolPages;
            pagesDispatch.Pool = resources.Pool;
            pagesDispatch.PageTable = resources.PageTable;
            pagesDispatch.RequestBits = resources.RequestBits;
            pagesDispatch.FreeList = resources.FreeList;
            pagesDispatch.Stats = resources.Stats;
            pagesDispatch.DirtyList = resources.DirtyList;
            pagesDispatch.Depth = depth;
            pagesDispatch.Clipmap = &scene.Clipmap;
            std::memcpy(pagesDispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(pagesDispatch.InverseViewProjection));
            std::memcpy(pagesDispatch.CameraPosition, scene.CameraPosition, sizeof(pagesDispatch.CameraPosition));
            pagesDispatch.FovYDegrees = scene.Camera.FieldOfView;
            readback.bPagesRecorded = pages->Record(commandList.get(), pagesDispatch);
        }
        VirtualShadowMapRasterDispatch rasterDispatch;
        rasterDispatch.Clipmap = &scene.Clipmap;
        rasterDispatch.PoolPages = resources.PoolPages;
        rasterDispatch.Pool = resources.Pool;
        rasterDispatch.PageTable = resources.PageTable;
        rasterDispatch.Stats = resources.Stats;
        rasterDispatch.Chunks = rasterBuffers.Chunks;
        rasterDispatch.ChunkCount = rasterBuffers.ChunkCount;
        rasterDispatch.Instances = rasterBuffers.Instances;
        rasterDispatch.Draws = rasterBuffers.Draws;
        readback.bRasterRecorded = raster.Record(commandList.get(), rasterDispatch);
        readback.DrawCount = raster.GetLastDrawCount();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        return ReadAll(resources.Pool, readback.Pool) && ReadAll(resources.PageTable, readback.PageTable) &&
               ReadAll(resources.Stats, readback.Stats) && ReadAll(rasterBuffers.Draws, readback.Draws) &&
               ReadAll(rasterBuffers.Instances, readback.Instances);
    }

    struct PageInfo
    {
        uint32_t Level = 0;
        int64_t AbsX = 0;
        int64_t AbsY = 0;
        uint32_t Physical = 0;
        bool bDirty = false;
    };

    // ページの表の割り当て済みの欄から、段・絶対のページ・物理ページを取り出す
    Container::VariableArray<PageInfo> DecodePages(const Scene& scene, const Container::VariableArray<uint32_t>& pageTable)
    {
        Container::VariableArray<PageInfo> pages;
        const int64_t count = static_cast<int64_t>(VirtualShadowMap::TABLE_DIMENSION);
        for (uint32_t index = 0; index < pageTable.size(); ++index)
        {
            const uint32_t entry = pageTable[index];
            if ((entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) == 0u)
            {
                continue;
            }
            PageInfo page;
            page.Level = index / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const uint32_t address = index % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const int64_t addressY = address / VirtualShadowMap::TABLE_DIMENSION;
            const int64_t addressX = address % VirtualShadowMap::TABLE_DIMENSION;
            const VirtualShadowMapClipmapLevel& levelData = scene.Clipmap.Levels[page.Level];
            page.AbsX = levelData.OriginPageX + (((addressX - levelData.OriginPageX) % count) + count) % count;
            page.AbsY = levelData.OriginPageY + (((addressY - levelData.OriginPageY) % count) + count) % count;
            page.Physical = entry & VirtualShadowMap::PAGE_INDEX_MASK;
            page.bDirty = (entry & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u;
            pages.push_back(page);
        }
        return pages;
    }

    // 展開の参照: 塊のワールドの境界のライト空間の矩形（シェーダーと同じ、中心と半幅の式）が覆うページのうち、割り当て済みで dirty のものの数
    uint32_t CountExpectedInstances(const Scene& scene, const VsmShadowChunk& chunk, const Container::VariableArray<PageInfo>& pages)
    {
        const VirtualShadowMapClipmap& clipmap = scene.Clipmap;
        const double right[3] = {clipmap.LightRight.x, clipmap.LightRight.y, clipmap.LightRight.z};
        const double up[3] = {clipmap.LightUp.x, clipmap.LightUp.y, clipmap.LightUp.z};
        double centerRight = 0.0;
        double centerUp = 0.0;
        double extentRight = 0.0;
        double extentUp = 0.0;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            const double center = 0.5 * (static_cast<double>(chunk.BoundsMin[axis]) + static_cast<double>(chunk.BoundsMax[axis]));
            const double extent = 0.5 * (static_cast<double>(chunk.BoundsMax[axis]) - static_cast<double>(chunk.BoundsMin[axis]));
            centerRight += center * right[axis];
            centerUp += center * up[axis];
            extentRight += extent * std::abs(right[axis]);
            extentUp += extent * std::abs(up[axis]);
        }
        uint32_t count = 0;
        for (const PageInfo& page : pages)
        {
            if (!page.bDirty)
            {
                continue;
            }
            const double pageMeters = static_cast<double>(clipmap.Levels[page.Level].PageMeters);
            const int64_t minX = static_cast<int64_t>(std::floor((centerRight - extentRight) / pageMeters));
            const int64_t maxX = static_cast<int64_t>(std::floor((centerRight + extentRight) / pageMeters));
            const int64_t minY = static_cast<int64_t>(std::floor((centerUp - extentUp) / pageMeters));
            const int64_t maxY = static_cast<int64_t>(std::floor((centerUp + extentUp) / pageMeters));
            if (page.AbsX >= minX && page.AbsX <= maxX && page.AbsY >= minY && page.AbsY <= maxY)
            {
                ++count;
            }
        }
        return count;
    }

    float WordToFloat(uint32_t word)
    {
        float value = 0.0f;
        std::memcpy(&value, &word, sizeof(value));
        return value;
    }

    struct PoolCheck
    {
        uint32_t Mismatches = 0;
        uint32_t Compared = 0;
        uint32_t Covered = 0;
        uint32_t Skipped = 0;
        // 割り当て済みのページごとの、形に覆われた texel の数（pages の並びと同じ）
        Container::VariableArray<uint32_t> CoveredPerPage;
    };

    // 物理ページの全 texel を、形の和から求めた参照（覆われた texel は手前の深度、ほかは 1.0。dirty でないページは初期値のまま）と比べる。
    // 形の縁から texel の中心までが半 texel 未満の texel は、丸めで結果が変わりうるので比べない
    PoolCheck CheckPool(const char* label,
                        const Scene& scene,
                        const Container::VariableArray<Shape>& shapes,
                        const Container::VariableArray<PageInfo>& pages,
                        const Container::VariableArray<uint32_t>& pool,
                        uint32_t untouchedWord)
    {
        constexpr double DepthTolerance = 1.0e-5;
        PoolCheck check;
        const double depthCenter = scene.Clipmap.DepthCenter;
        const double depthScale = 0.5 / static_cast<double>(scene.Settings.DepthRangeMeters);
        uint32_t reported = 0;
        for (const PageInfo& page : pages)
        {
            uint32_t coveredInPage = 0;
            const double texelMeters = static_cast<double>(scene.Clipmap.Levels[page.Level].TexelMeters);
            const double margin = 0.5 * texelMeters;
            for (uint32_t row = 0; row < VirtualShadowMap::PAGE_RESOLUTION; ++row)
            {
                for (uint32_t column = 0; column < VirtualShadowMap::PAGE_RESOLUTION; ++column)
                {
                    const uint32_t actualWord =
                        pool[static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS + row * VirtualShadowMap::PAGE_RESOLUTION + column];
                    const double x = (static_cast<double>(page.AbsX) * VirtualShadowMap::PAGE_RESOLUTION + column + 0.5) * texelMeters;
                    const double y = (static_cast<double>(page.AbsY) * VirtualShadowMap::PAGE_RESOLUTION + row + 0.5) * texelMeters;

                    bool bAmbiguous = false;
                    bool bCovered = false;
                    double expectedDepth = 1.0;
                    if (page.bDirty)
                    {
                        for (const Shape& shape : shapes)
                        {
                            const double distance = ShapeSignedDistance(shape, x, y);
                            if (std::abs(distance) < margin)
                            {
                                bAmbiguous = true;
                            }
                            else if (distance > 0.0)
                            {
                                const double shapeDepth = 0.5 + (ShapePlaneDepth(shape, x, y) - depthCenter) * depthScale;
                                expectedDepth = bCovered ? std::min(expectedDepth, shapeDepth) : shapeDepth;
                                bCovered = true;
                            }
                        }
                    }
                    if (bAmbiguous)
                    {
                        ++check.Skipped;
                        continue;
                    }
                    ++check.Compared;
                    bool bMatch = false;
                    if (!page.bDirty)
                    {
                        bMatch = actualWord == untouchedWord;
                    }
                    else if (bCovered)
                    {
                        ++check.Covered;
                        ++coveredInPage;
                        bMatch = std::abs(static_cast<double>(WordToFloat(actualWord)) - expectedDepth) <= DepthTolerance;
                    }
                    else
                    {
                        bMatch = actualWord == VirtualShadowMap::EMPTY_DEPTH_BITS;
                    }
                    if (!bMatch)
                    {
                        ++check.Mismatches;
                        if (reported < 6u)
                        {
                            ++reported;
                            std::cerr << TestName << " " << label << " texel が参照と違う: 段=" << page.Level << " ページ=(" << page.AbsX << ","
                                      << page.AbsY << ") texel=(" << column << "," << row << ") 期待=" << (bCovered ? expectedDepth : 1.0)
                                      << " 実際=" << WordToFloat(actualWord) << std::endl;
                        }
                    }
                }
            }
            check.CoveredPerPage.push_back(coveredInPage);
        }
        return check;
    }

    // 展開の統計・間接描画の引数・インスタンスが、割り当て済みで dirty のページと整合していること
    void CheckExpansion(const char* label,
                        const Scene& scene,
                        const ChunkGeometry& geometry,
                        const Container::VariableArray<PageInfo>& pages,
                        const RasterReadback& readback,
                        uint32_t instanceCapacity)
    {
        Expect(readback.bRasterRecorded, "展開・描画を記録できなければならない");
        Expect(readback.DrawCount == geometry.Chunks.size(), "間接描画は塊の数だけ記録しなければならない");
        uint32_t expectedChunks = 0;
        uint32_t expectedInstances = 0;
        uint32_t expectedOverflow = 0;
        Container::VariableArray<uint32_t> perChunk;
        for (const VsmShadowChunk& chunk : geometry.Chunks)
        {
            perChunk.push_back(CountExpectedInstances(scene, chunk, pages));
        }
        // 確保の順は塊の処理の順で決まるので、容量に収まる場合（溢れの無い構成）だけ確保の位置まで確かめる
        uint32_t totalDemand = 0;
        for (const uint32_t count : perChunk)
        {
            totalDemand += count;
        }
        const bool bFits = totalDemand <= instanceCapacity;
        if (bFits)
        {
            for (const uint32_t count : perChunk)
            {
                expectedChunks += count != 0u ? 1u : 0u;
                expectedInstances += count;
            }
        }
        else
        {
            // 溢れる構成では、どの塊が先に確保するかは決まらないが、塊が 1 つなら決まる
            Expect(perChunk.size() == 1u, "溢れの検査は塊が 1 つの構成で行う");
            expectedOverflow = totalDemand;
        }
        Expect(readback.Stats[VirtualShadowMap::StatRasterChunks] == expectedChunks, "統計の描く塊の数が参照と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatRasterInstances] == expectedInstances, "統計のインスタンスの数が参照と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatRasterOverflow] == expectedOverflow, "統計の溢れの数が参照と一致しなければならない");
        std::cout << TestName << " " << label << ": 塊=" << geometry.Chunks.size() << " 描く塊=" << readback.Stats[VirtualShadowMap::StatRasterChunks]
                  << " インスタンス=" << readback.Stats[VirtualShadowMap::StatRasterInstances] << "（参照 " << expectedInstances << "）溢れ="
                  << readback.Stats[VirtualShadowMap::StatRasterOverflow] << std::endl;

        // 間接描画の引数: 頭 = 確保した数。塊ごと (三角形 × 3, ページの数, 0, 0, 先頭)。範囲が重ならず、容量に収まる
        if (bFits)
        {
            Expect(readback.Draws[0] == expectedInstances, "間接描画の引数の頭（確保の位置）が書いたインスタンスの数でなければならない");
        }
        uint32_t usedEnd = 0;
        Container::VariableArray<uint32_t> covered(instanceCapacity, 0u);
        for (uint32_t chunkIndex = 0; chunkIndex < geometry.Chunks.size(); ++chunkIndex)
        {
            const uint32_t base = VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS + chunkIndex * VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS;
            const uint32_t indexCount = readback.Draws[base + 0u];
            const uint32_t instanceCount = readback.Draws[base + 1u];
            const uint32_t firstIndex = readback.Draws[base + 2u];
            const uint32_t vertexOffset = readback.Draws[base + 3u];
            const uint32_t firstInstance = readback.Draws[base + 4u];
            Expect(indexCount == geometry.Chunks[chunkIndex].Record.TriangleCount * 3u, "間接描画の頂点数は三角形の数 × 3 でなければならない");
            Expect(firstIndex == 0u && vertexOffset == 0u, "間接描画の firstIndex・vertexOffset は 0 でなければならない");
            if (!bFits)
            {
                Expect(instanceCount == 0u && firstInstance == 0u, "容量を超える塊は instanceCount 0 で描かない");
                continue;
            }
            Expect(instanceCount == perChunk[chunkIndex], "間接描画の instanceCount が塊を描くページの数でなければならない");
            Expect(static_cast<uint64_t>(firstInstance) + instanceCount <= instanceCapacity, "インスタンスの範囲が容量に収まらなければならない");
            for (uint32_t slot = 0; slot < instanceCount && firstInstance + slot < instanceCapacity; ++slot)
            {
                ++covered[firstInstance + slot];
                usedEnd = std::max(usedEnd, firstInstance + slot + 1u);
                // インスタンス: 塊の番号・段 | 物理ページ << 4・絶対のページ。ページの表の割り当て済みで dirty のページのどれかと一致する
                const uint32_t* instance = &readback.Instances[static_cast<size_t>(firstInstance + slot) * 4u];
                bool bKnownPage = instance[0] == chunkIndex;
                bool bFound = false;
                for (const PageInfo& page : pages)
                {
                    if (page.bDirty && page.Level == (instance[1] & 15u) && page.Physical == (instance[1] >> 4u) &&
                        static_cast<int32_t>(instance[2]) == page.AbsX && static_cast<int32_t>(instance[3]) == page.AbsY)
                    {
                        bFound = true;
                        break;
                    }
                }
                Expect(bKnownPage && bFound, "インスタンスが割り当て済みで dirty のページ（塊の番号・段・物理ページ・絶対のページ）を指さなければならない");
            }
        }
        if (bFits)
        {
            bool bTiled = usedEnd == expectedInstances;
            for (uint32_t slot = 0; slot < usedEnd; ++slot)
            {
                bTiled = bTiled && covered[slot] == 1u;
            }
            Expect(bTiled, "塊ごとのインスタンスの範囲が、先頭から隙間も重なりも無く並ばなければならない");
        }
    }

    // 横に隣り合う 2 ページ（同じ段・同じ行の A と、その右の B）が両方とも参照の集合にある組を探す
    bool FindAdjacentPages(const Scene& scene,
                           const Container::VariableArray<uint32_t>& keys,
                           uint32_t& outLevel,
                           int64_t& outAbsX,
                           int64_t& outAbsY)
    {
        const int64_t count = static_cast<int64_t>(VirtualShadowMap::TABLE_DIMENSION);
        for (const uint32_t key : keys)
        {
            const uint32_t level = key / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const uint32_t address = key % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const int64_t addressX = address % VirtualShadowMap::TABLE_DIMENSION;
            const int64_t addressY = address / VirtualShadowMap::TABLE_DIMENSION;
            const uint32_t rightKey = level * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL +
                                      static_cast<uint32_t>(addressY) * VirtualShadowMap::TABLE_DIMENSION +
                                      static_cast<uint32_t>((addressX + 1) % count);
            if (!std::binary_search(keys.begin(), keys.end(), rightKey))
            {
                continue;
            }
            const VirtualShadowMapClipmapLevel& levelData = scene.Clipmap.Levels[level];
            outLevel = level;
            outAbsX = levelData.OriginPageX + (((addressX - levelData.OriginPageX) % count) + count) % count;
            outAbsY = levelData.OriginPageY + (((addressY - levelData.OriginPageY) % count) + count) % count;
            return true;
        }
        return false;
    }

    // ケース F〜I。実行できなければ false
    // ケース F の形・物理プール・割り当てたページ・プールのページの数（ケース I・K が、同じ場面を別の記録の経路で描いて比べる）
    struct CaseFData
    {
        Container::VariableArray<Shape> Shapes;
        Container::VariableArray<uint32_t> Pool;
        Container::VariableArray<PageInfo> Pages;
        uint32_t PoolPages = 0;
    };

    bool RunRasterCases(const DevicePtr& device,
                        VirtualShadowMapPages& pages,
                        VirtualShadowMapRaster& raster,
                        const Scene& scene,
                        const Reference& reference,
                        const TexturePtr& depth,
                        uint64_t& frameSerial,
                        CaseFData& caseF)
    {
        const double depthCenter = scene.Clipmap.DepthCenter;

        Container::VariableArray<Shape>& caseFShapes = caseF.Shapes;
        Container::VariableArray<uint32_t>& caseFPool = caseF.Pool;
        Container::VariableArray<PageInfo>& caseFPages = caseF.Pages;
        uint32_t& caseFPoolPages = caseF.PoolPages;

        // ----- ケース F: 印付け → 割り当て → 消去 → 展開 → 描画（合成した地面の深度の上に、既知の四角形 2 枚） -----
        {
            uint32_t level = 0;
            int64_t pageX = 0;
            int64_t pageY = 0;
            if (!FindAdjacentPages(scene, reference.Keys, level, pageX, pageY))
            {
                std::cerr << TestName << " ケース F: 横に隣り合うページが参照に無い（シーンが退化している）" << std::endl;
                return false;
            }
            const double pageMeters = static_cast<double>(scene.Clipmap.Levels[level].PageMeters);
            const double boundaryX = static_cast<double>(pageX + 1) * pageMeters;
            const double bottomY = static_cast<double>(pageY) * pageMeters;

            Container::VariableArray<Shape> shapes;
            // 近い四角形: ページ A・B の境界をまたぐ。深度は傾いた平面
            Shape nearQuad = MakeRect(boundaryX - 0.3137 * pageMeters, boundaryX + 0.2713 * pageMeters, bottomY + 0.2231 * pageMeters, bottomY + 0.6619 * pageMeters);
            SetPlane(nearQuad, boundaryX, bottomY, depthCenter - 50.0, 0.5, -0.25);
            shapes.push_back(nearQuad);
            // 遠い四角形: 近い四角形と一部が重なり、後から描かれる（atomicMin でなく代入だと、重なりが遠い深度で上書きされる）。ローカルの頂点＋変換で渡す
            Shape farQuad = MakeRect(boundaryX - 0.1 * pageMeters, boundaryX + 0.6 * pageMeters, bottomY + 0.4 * pageMeters, bottomY + 0.9 * pageMeters);
            SetPlane(farQuad, boundaryX, bottomY, depthCenter - 40.0, 0.0, 0.0);
            farQuad.bLocalTransform = true;
            shapes.push_back(farQuad);

            ChunkGeometry geometry = BuildChunks(scene, shapes);
            Resources resources;
            RasterBuffers rasterBuffers;
            RasterReadback readback;
            const uint32_t poolPages = static_cast<uint32_t>(reference.Keys.size()) + 24u;
            const uint32_t instanceCapacity = 4096u;
            if (!CreateResources(device, poolPages, resources) || !CreateRasterBuffers(device, geometry, instanceCapacity, rasterBuffers) ||
                !RunRaster(device, &pages, raster, scene, resources, rasterBuffers, depth, frameSerial++, readback))
            {
                std::cerr << TestName << " ケース F を実行できませんでした" << std::endl;
                return false;
            }
            Expect(readback.bPagesRecorded, "ケース F: 印付け・割り当て・消去を記録できなければならない");
            const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, readback.PageTable);
            Expect(pageInfos.size() == reference.Keys.size(), "ケース F: 割り当てたページの数が参照と一致しなければならない");
            CheckExpansion("ケース F", scene, geometry, pageInfos, readback, instanceCapacity);
            const PoolCheck check = CheckPool("ケース F", scene, shapes, pageInfos, readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
            std::cout << TestName << " ケース F: 段=" << level << " ページ=(" << pageX << "," << pageY << ")+(1,0) 比べた texel=" << check.Compared
                      << " 形に覆われた texel=" << check.Covered << " 比べなかった縁の texel=" << check.Skipped << " 不一致=" << check.Mismatches << std::endl;
            Expect(check.Mismatches == 0u, "ケース F: 物理ページが形の和の参照と一致しなければならない（覆われた texel は手前の深度、ほかは 1.0）");
            Expect(check.Covered > 2000u, "ケース F: 形に覆われた texel が十分に無い（シーンが退化している）");
            // 境界をまたぐ四角形が、両方のページに描かれていること（切れ目の無さは、全 texel の一致で確かめている）
            uint32_t coveredPageA = 0;
            uint32_t coveredPageB = 0;
            for (uint32_t index = 0; index < pageInfos.size(); ++index)
            {
                if (pageInfos[index].Level == level && pageInfos[index].AbsY == pageY)
                {
                    coveredPageA += pageInfos[index].AbsX == pageX ? check.CoveredPerPage[index] : 0u;
                    coveredPageB += pageInfos[index].AbsX == pageX + 1 ? check.CoveredPerPage[index] : 0u;
                }
            }
            Expect(coveredPageA > 500u && coveredPageB > 500u, "ケース F: 境界をまたぐ四角形が両方のページに描かれなければならない");
            caseFShapes = shapes;
            caseFPool = readback.Pool;
            caseFPages = pageInfos;
            caseFPoolPages = poolPages;
        }

        // ----- ケース I: 手続きメッシュの記録の経路（バッファのアドレスと変換の行列）で、ケース F と同じ場面を描く -----
        // 塊の記録をテストが組み立てず、本番の関数（PlanProceduralChunks・AppendProceduralInstance）が作る。頂点・インデックスはメッシュのバッファ、
        // 変換は列優先の 16 個の float。物理プールがケース F（記録を直接組み立てたもの）と全 texel で一致しなければならない
        {
            ChunkGeometry geometry = BuildChunks(scene, caseFShapes);
            Resources resources;
            RasterBuffers rasterBuffers;
            RasterReadback readback;
            const uint32_t instanceCapacity = 4096u;
            if (caseFShapes.empty() || !CreateResources(device, caseFPoolPages, resources) ||
                !CreateRasterBuffers(device, geometry, instanceCapacity, rasterBuffers))
            {
                std::cerr << TestName << " ケース I を準備できませんでした" << std::endl;
                return false;
            }

            Container::VariableArray<VsmShadowChunk> productionChunks;
            VirtualShadowMap::CasterStats casterStats;
            VirtualShadowMap::ProceduralPlanScratch planScratch;
            Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> plan;
            for (size_t index = 0; index < caseFShapes.size(); ++index)
            {
                const VsmShadowChunk& source = geometry.Chunks[index];
                const uint32_t cornerCount = caseFShapes[index].bRect ? 4u : 3u;
                // メッシュ全体のローカルの境界は、その形の頂点から求める
                BoundingBox localBounds = BoundingBox::CreateInvalid();
                for (uint32_t corner = 0; corner < cornerCount; ++corner)
                {
                    const float* vertex = &geometry.Vertices[static_cast<size_t>(source.Record.VertexBase + corner) * 8u];
                    localBounds.Expand(vertex[0], vertex[1], vertex[2]);
                }
                VirtualShadowMap::ProceduralDrawInput input;
                input.VertexAddress = rasterBuffers.Vertices->GetDeviceAddress();
                input.IndexAddress = rasterBuffers.Indices->GetDeviceAddress();
                input.FirstIndex = source.Record.FirstIndex;
                input.IndexCount = source.Record.TriangleCount * 3u;
                input.VertexOffset = source.Record.VertexBase;
                input.MeshBounds = &localBounds;
                float world[16] = {};
                world[0] = world[5] = world[10] = world[15] = 1.0f;
                if (caseFShapes[index].bLocalTransform)
                {
                    world[0] = world[5] = world[10] = LocalScale;
                    world[12] = LocalOffset[0];
                    world[13] = LocalOffset[1];
                    world[14] = LocalOffset[2];
                }
                Expect(VirtualShadowMap::PlanProceduralChunks(input, planScratch, plan), "ケース I: 手続きメッシュの描画を塊に分けられなければならない");
                VirtualShadowMap::AppendProceduralInstance(input, plan, world, scene.Clipmap, productionChunks, casterStats);
            }
            Expect(productionChunks.size() == geometry.Chunks.size() && casterStats.CulledChunks == 0u && casterStats.DroppedChunks == 0u,
                   "ケース I: 手続きメッシュの記録がすべての塊を作らなければならない（範囲の中にある）");
            if (productionChunks.size() != geometry.Chunks.size())
            {
                return false;
            }
            // 変換と境界・アドレスが記録に入っていること（記録の経路の入力）
            for (size_t index = 0; index < productionChunks.size(); ++index)
            {
                const VsmShadowChunk& made = productionChunks[index];
                const VsmShadowChunk& source = geometry.Chunks[index];
                Expect(made.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk) &&
                           made.Record.TriangleCount == source.Record.TriangleCount && made.Record.FirstIndex == source.Record.FirstIndex &&
                           made.Record.VertexBase == source.Record.VertexBase && made.Record.VertexAddress == source.Record.VertexAddress &&
                           made.Record.IndexAddress == source.Record.IndexAddress,
                       "ケース I: 記録の頂点・インデックスの読み方がメッシュのバッファから作られなければならない");
                Expect(std::memcmp(made.World, source.World, sizeof(made.World)) == 0, "ケース I: 記録の変換が変換の行列から作られなければならない");
                // 境界は、元の（頂点から求めた）境界を含む（三角形をすべて含むこと）。同じ記録の経路なので、わずかな丸めだけ広い
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    Expect(made.BoundsMin[axis] <= source.BoundsMin[axis] + 1.0e-3f && made.BoundsMax[axis] >= source.BoundsMax[axis] - 1.0e-3f,
                           "ケース I: 記録の境界が三角形を含まなければならない");
                }
            }
            geometry.Chunks = productionChunks;
            rasterBuffers.Chunks->Update(productionChunks.data(), productionChunks.size() * sizeof(VsmShadowChunk));

            if (!RunRaster(device, &pages, raster, scene, resources, rasterBuffers, depth, frameSerial++, readback))
            {
                std::cerr << TestName << " ケース I を実行できませんでした" << std::endl;
                return false;
            }
            Expect(readback.bPagesRecorded, "ケース I: 印付け・割り当て・消去を記録できなければならない");
            const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, readback.PageTable);
            CheckExpansion("ケース I", scene, geometry, pageInfos, readback, instanceCapacity);
            const PoolCheck check = CheckPool("ケース I", scene, caseFShapes, pageInfos, readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
            // 物理ページの番号は、割り当ての並び（GPU のアトミックの順）で実行ごとに変わりうるので、同じ（段・絶対のページ）を持つ
            // ページどうしで、全 texel の語を比べる
            uint32_t differentWords = 0;
            uint32_t comparedPages = 0;
            Expect(readback.Pool.size() == caseFPool.size() && pageInfos.size() == caseFPages.size(),
                   "ケース I: 物理プールの大きさ・割り当てたページの数がケース F と同じでなければならない");
            for (const PageInfo& page : pageInfos)
            {
                const PageInfo* counterpart = nullptr;
                for (const PageInfo& candidate : caseFPages)
                {
                    if (candidate.Level == page.Level && candidate.AbsX == page.AbsX && candidate.AbsY == page.AbsY)
                    {
                        counterpart = &candidate;
                        break;
                    }
                }
                Expect(counterpart != nullptr && counterpart->bDirty == page.bDirty, "ケース I: ケース F と同じページが割り当てられなければならない");
                if (counterpart == nullptr)
                {
                    continue;
                }
                ++comparedPages;
                const size_t baseI = static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS;
                const size_t baseF = static_cast<size_t>(counterpart->Physical) * VirtualShadowMap::PAGE_WORDS;
                for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
                {
                    differentWords += readback.Pool[baseI + word] != caseFPool[baseF + word] ? 1u : 0u;
                }
            }
            Expect(comparedPages == pageInfos.size(), "ケース I: すべてのページをケース F と比べなければならない");
            std::cout << TestName << " ケース I: 比べた texel=" << check.Compared << " 形に覆われた texel=" << check.Covered << " 不一致=" << check.Mismatches
                      << " ケース F との違い（語）=" << differentWords << std::endl;
            Expect(check.Mismatches == 0u && check.Covered > 2000u, "ケース I: 手続きメッシュの記録の経路で描いた物理ページが形の和の参照と一致しなければならない");
            Expect(differentWords == 0u, "ケース I: 手続きメッシュの記録の経路で描いた texel が、ケース F の記録を直接組み立てた場面と同じでなければならない");
        }

        // ----- ケース G・H: ページの表を直接書き、段 0 の 24 ページ（うち 1 ページは dirty でない）と段 1 の 4 ページを割り当てた場面 -----
        // G: 24 ページ以上をまたぐ大きな三角形（縁が段 0 のページの中を斜めに通る）と、その縁をまたぐ遠い四角形
        // H: 容量が足りない（必要な数 − 1）ときは描かずに数え、ちょうど足りるときは描く
        const VirtualShadowMapClipmapLevel& level0 = scene.Clipmap.Levels[0];
        const VirtualShadowMapClipmapLevel& level1 = scene.Clipmap.Levels[1];
        const double pageMeters0 = static_cast<double>(level0.PageMeters);
        const double centerX = static_cast<double>(level0.CenterPageX) * pageMeters0;
        const double centerY = static_cast<double>(level0.CenterPageY) * pageMeters0;

        Container::VariableArray<Shape> bigShapes;
        {
            // 縁 V0→V2 が段 0 の 24 ページの中央付近 (centerX - 4, centerY - 3) を傾き 0.7 で通る。三角形は縁の右下側
            const double edgeX = centerX - 4.0;
            const double edgeY = centerY - 3.0;
            Shape triangle = MakeTriangle(edgeX - 500.0, edgeY - 350.0, centerX + 600.0, centerY - 500.0, edgeX + 480.0, edgeY + 336.0);
            SetPlane(triangle, centerX, centerY, depthCenter - 30.0, 0.01, 0.015);
            bigShapes.push_back(triangle);
        }
        Container::VariableArray<Shape> allShapes = bigShapes;
        {
            // 縁をまたぐ遠い四角形（三角形より後。三角形が覆う所では三角形の深度、覆わない所では自分の深度）
            Shape quad = MakeRect(centerX - 6.0, centerX - 1.5, centerY - 5.0, centerY - 0.5);
            SetPlane(quad, centerX, centerY, depthCenter - 10.0, 0.0, 0.0);
            allShapes.push_back(quad);
        }

        const uint32_t poolPages = 32u;
        auto buildSyntheticTable = [&](Resources& resources) {
            // 物理ページを 1.0 のビットで埋め、ページの表と統計・要求などを 0 にする
            FillWords(resources.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
            for (const BufferPtr& buffer : {resources.PageTable, resources.RequestBits, resources.FreeList, resources.Stats, resources.DirtyList})
            {
                FillWords(buffer, 0u);
            }
            uint32_t* table = static_cast<uint32_t*>(resources.PageTable->Map(0u, resources.PageTable->GetSize()));
            if (table == nullptr)
            {
                return false;
            }
            uint32_t physical = 0;
            for (int64_t row = -2; row < 2; ++row)
            {
                for (int64_t column = -3; column < 3; ++column)
                {
                    // 1 ページだけ dirty でない（割り当て済みだが、描かない）
                    const bool bDirty = !(column == -1 && row == 0);
                    table[PageKey(0, level0.CenterPageX + column, level0.CenterPageY + row)] =
                        VirtualShadowMap::PAGE_ENTRY_ALLOCATED | (bDirty ? VirtualShadowMap::PAGE_ENTRY_DIRTY : 0u) | physical;
                    ++physical;
                }
            }
            for (int64_t row = -1; row < 1; ++row)
            {
                for (int64_t column = -1; column < 1; ++column)
                {
                    table[PageKey(1, level1.CenterPageX + column, level1.CenterPageY + row)] =
                        VirtualShadowMap::PAGE_ENTRY_ALLOCATED | VirtualShadowMap::PAGE_ENTRY_DIRTY | physical;
                    ++physical;
                }
            }
            resources.PageTable->Unmap();
            return true;
        };

        // ----- ケース G -----
        {
            ChunkGeometry geometry = BuildChunks(scene, allShapes);
            Resources resources;
            RasterBuffers rasterBuffers;
            RasterReadback readback;
            const uint32_t instanceCapacity = 256u;
            if (!CreateResources(device, poolPages, resources) || !buildSyntheticTable(resources) ||
                !CreateRasterBuffers(device, geometry, instanceCapacity, rasterBuffers) ||
                !RunRaster(device, nullptr, raster, scene, resources, rasterBuffers, TexturePtr{}, frameSerial++, readback))
            {
                std::cerr << TestName << " ケース G を実行できませんでした" << std::endl;
                return false;
            }
            const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, readback.PageTable);
            Expect(pageInfos.size() == 28u, "ケース G: 合成したページの表の割り当て済みの数が 28 でなければならない");
            CheckExpansion("ケース G", scene, geometry, pageInfos, readback, instanceCapacity);
            // 大きな三角形が段 0 の 16 ページ以上を覆うこと（シーンの前提）
            Expect(CountExpectedInstances(scene, geometry.Chunks[0], pageInfos) >= 16u, "ケース G: 大きな三角形が 16 ページ以上を覆わなければならない");
            const PoolCheck check = CheckPool("ケース G", scene, allShapes, pageInfos, readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
            std::cout << TestName << " ケース G: 比べた texel=" << check.Compared << " 形に覆われた texel=" << check.Covered
                      << " 比べなかった縁の texel=" << check.Skipped << " 不一致=" << check.Mismatches << std::endl;
            Expect(check.Mismatches == 0u, "ケース G: 大きな三角形・四角形を描いた物理ページが参照と一致しなければならない（dirty でないページは触らない）");
            Expect(check.Covered > 20000u, "ケース G: 形に覆われた texel が十分に無い（シーンが退化している）");
        }

        // ----- ケース H: 容量 -----
        {
            ChunkGeometry geometry = BuildChunks(scene, bigShapes);
            Container::VariableArray<PageInfo> expectedPages;
            uint32_t needed = 0;
            {
                Resources probe;
                if (!CreateResources(device, poolPages, probe) || !buildSyntheticTable(probe))
                {
                    return false;
                }
                Container::VariableArray<uint32_t> table;
                if (!ReadAll(probe.PageTable, table))
                {
                    return false;
                }
                expectedPages = DecodePages(scene, table);
                needed = CountExpectedInstances(scene, geometry.Chunks[0], expectedPages);
            }
            Expect(needed >= 16u, "ケース H: 三角形の必要なインスタンスの数が 16 以上でなければならない");
            for (const uint32_t instanceCapacity : {needed - 1u, needed})
            {
                Resources resources;
                RasterBuffers rasterBuffers;
                RasterReadback readback;
                if (!CreateResources(device, poolPages, resources) || !buildSyntheticTable(resources) ||
                    !CreateRasterBuffers(device, geometry, instanceCapacity, rasterBuffers) ||
                    !RunRaster(device, nullptr, raster, scene, resources, rasterBuffers, TexturePtr{}, frameSerial++, readback))
                {
                    std::cerr << TestName << " ケース H を実行できませんでした" << std::endl;
                    return false;
                }
                const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, readback.PageTable);
                const char* label = instanceCapacity < needed ? "ケース H（容量が 1 足りない）" : "ケース H（容量がちょうど）";
                CheckExpansion(label, scene, geometry, pageInfos, readback, instanceCapacity);
                if (instanceCapacity < needed)
                {
                    // 溢れた塊は描かない: 物理ページはすべて初期値のまま
                    bool bUntouched = true;
                    for (const uint32_t word : readback.Pool)
                    {
                        bUntouched = bUntouched && word == VirtualShadowMap::EMPTY_DEPTH_BITS;
                    }
                    Expect(bUntouched, "ケース H: 容量を超えた塊は物理ページへ描いてはならない");
                }
                else
                {
                    const PoolCheck check = CheckPool(label, scene, bigShapes, pageInfos, readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
                    Expect(check.Mismatches == 0u && check.Covered > 20000u, "ケース H: ちょうどの容量では三角形が描かれなければならない");
                }
            }
        }
        return true;
    }

    // ========================================
    // ケース J: MegaGeometry の投影物のカリング（vsm_dirty_mips.comp・vsm_mega_cull.comp / VirtualShadowMapMegaCull）
    // ========================================
    //
    // 合成した完全二分木のクラスタ（根 1・中間 2 と 4・葉 8 の 15 個。誤差は高さごとに 2 倍）を、本番のカリングに通す。
    // 段 k の texel の一辺は 0.25 × 2^k（m）で、誤差は高さ h の texel（0.25 × 2^h）の 0.9 倍。
    // 選ばれるのは「自分の誤差 ÷ texel ≤ 1 かつ親の誤差 ÷ texel > 1」のクラスタなので、段 k は高さ k のクラスタ（葉 8・中間 4・2・根 1）になる。
    // 段の texel が 2 倍になるごとに選ばれる高さが 1 つ上がり（粗くなり）、どの葉から根への道でもちょうど 1 つが選ばれる（一つの切り口）。
    //
    // インスタンス（添字 0〜5）と、ページの表（段 0〜3 の割り当て済みで dirty のページ・dirty でないページ）を組み合わせて、
    // インスタンスの判定（段の範囲・深度の範囲・dirty のページ）とクラスタの判定を確かめる:
    //   0: ライト空間 (16, 16)・全段で dirty のページに入る → 段 0〜3 で選ぶ（8 + 4 + 2 + 1 クラスタ）
    //   1: (3016, 16)・段 0 の範囲（±2048 m）の外 → 段 1〜3 で選ぶ（4 + 2 + 1 クラスタ）
    //   2: (16, 16)・影を落とさない → 何も選ばない
    //   3: (716, 16)・範囲内だが、そのページは割り当て済みで dirty でない（段 0）→ 何も選ばない
    //   4: (716, 16)・境界が大きく（半径 100 m）、段 0 では矩形が dirty のページ 1 つに触れる → 段 0 のインスタンスの判定を通るが、
    //      クラスタ（そのページの外）は選ばれない（クラスタの判定でも dirty のページを見る）
    //   5: (16, 16)・深度が範囲（±1000 m）の外 → 何も選ばない
    // 判定を通った（インスタンス、段）は 4 + 3 + 1 = 8、選んだクラスタは 15 + 7 = 22。dirty の階層（全 mip のビット）は CPU で作った参照と全語一致する。
    // 出力の一覧の容量が 5 のときは、選んだ数は 22・溢れは 17 で、書かれた 5 件はどれも期待の集合に入る。
    namespace MegaCull
    {
        // Common/MegaGeometryCull.glsl の MegaInstance（192 バイト）と同じ並び
        struct TestInstance
        {
            float World[16];
            float PreviousWorld[16];
            float LODSphere[4];
            uint32_t ClusterInfo[4]; // アドレスの下位・上位、クラスタ数、最初のワークグループの番号
            uint32_t DrawInfo[4];
            uint32_t BvhInfo[4]; // BVH のアドレス（0）、節の数、ページの表の先頭は [3]
        };
        static_assert(sizeof(TestInstance) == 192, "MegaInstance と大きさが一致しません");

        constexpr uint32_t ClusterCount = 15;
        constexpr uint32_t LevelCount = 4;
        constexpr float FirstTexelMeters = 0.25f;
        constexpr uint32_t InstanceCount = 6;
        constexpr uint32_t GeometryPageNone = 0xFFFFFFFFu;

        struct InstanceSpec
        {
            double LightX;
            double LightY;
            double LightDepth;
            bool bCaster;
            float BoundsRadius;
        };

        const InstanceSpec Specs[InstanceCount] = {
            {16.0, 16.0, 0.0, true, 6.0f},
            {3016.0, 16.0, 0.0, true, 6.0f},
            {16.0, 16.0, 0.0, false, 6.0f},
            {716.0, 16.0, 0.0, true, 6.0f},
            {716.0, 16.0, 0.0, true, 100.0f},
            {16.0, 16.0, 5000.0, true, 6.0f},
        };

        // 木の高さ（葉 = 0、根 = 3）
        uint32_t HeightOf(uint32_t index)
        {
            return index == 0u ? 3u : (index <= 2u ? 2u : (index <= 6u ? 1u : 0u));
        }

        void BuildClusters(Core::Rendering::MegaGeometry::GPUClusterData (&clusters)[ClusterCount])
        {
            namespace Mega = Core::Rendering::MegaGeometry;
            for (uint32_t index = 0; index < ClusterCount; ++index)
            {
                const uint32_t height = HeightOf(index);
                const uint32_t parent = index == 0u ? 0u : (index - 1u) / 2u;
                Mega::GPUClusterData cluster{};
                // 全クラスタが 1 つのページの中の小さな領域に入る（中心のずれは 1 m 以内、半径は 0.5〜2 m）
                cluster.BoundsCenterX = (static_cast<float>(index % 3u) - 1.0f) * 0.5f;
                cluster.BoundsCenterY = (static_cast<float>((index / 3u) % 3u) - 1.0f) * 0.5f;
                cluster.BoundsCenterZ = 0.0f;
                cluster.BoundsRadius = 0.5f + 0.5f * static_cast<float>(height);
                cluster.ConeCutoff = -1.0f;
                cluster.IndexCount = 3;
                cluster.LODLevel = height;
                cluster.LODError = 0.9f * FirstTexelMeters * static_cast<float>(1u << height);
                cluster.Flags = Mega::GPU_CLUSTER_FLAG_BAKED_LOD;
                if (index != 0u)
                {
                    const uint32_t parentHeight = HeightOf(parent);
                    cluster.ParentCenterX = (static_cast<float>(parent % 3u) - 1.0f) * 0.5f;
                    cluster.ParentCenterY = (static_cast<float>((parent / 3u) % 3u) - 1.0f) * 0.5f;
                    cluster.ParentRadius = 0.5f + 0.5f * static_cast<float>(parentHeight);
                    cluster.ParentError = 0.9f * FirstTexelMeters * static_cast<float>(1u << parentHeight);
                    cluster.GroupId = parent;
                }
                else
                {
                    cluster.ParentError = 3.402823466e+38f;
                    cluster.GroupId = 0xFFFFFFFFu; // 根
                }
                // ページ: 葉はページ 1、それ以外はページ 0。高さ 1 のクラスタを作ったグループ（子 = 葉）はページ 1、それより上の子はページ 0
                cluster.PageId = height == 0u ? 1u : 0u;
                cluster.ChildPageId = height == 0u ? Mega::INVALID_PAGE_ID : (height == 1u ? 1u : 0u);
                clusters[index] = cluster;
            }
        }

        // 段 level の texel の一辺（m）
        float TexelMeters(uint32_t level)
        {
            return FirstTexelMeters * static_cast<float>(1u << level);
        }

        // 段 level で選ばれるべきクラスタの添字（高さ level のクラスタ）。葉のページが非常駐なら、段 0 は葉の代わりに高さ 1 のクラスタ
        // （子のページが無いので、自分の誤差が許容を超えても自分を描く）
        void ExpectedClusters(uint32_t level, Container::VariableArray<uint32_t>& out, bool bLeafPageResident = true)
        {
            out.clear();
            const uint32_t height = (level == 0u && !bLeafPageResident) ? 1u : level;
            for (uint32_t index = 0; index < ClusterCount; ++index)
            {
                if (HeightOf(index) == height)
                {
                    out.push_back(index);
                }
            }
        }

        struct Outcome
        {
            bool bRecorded = false;
            uint32_t GroupCount = 0;
            uint32_t Selected = 0;
            uint32_t Overflow = 0;
            uint32_t InstanceLevels = 0;
            uint32_t StatInstances = 0;
            uint32_t StatClusters = 0;
            uint32_t StatOverflow = 0;
            /** @brief 実行後のジオメトリのページの表の、ページ 0・1 の要求の印（カリングはページを要求しないので、どちらも 0 のまま） */
            uint32_t PageRequestStamps[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
            /** @brief 書かれた一覧（4 語 = インスタンスの表の番号・段・クラスタの番号・予約を、書いた件数だけ） */
            Container::VariableArray<uint32_t> Entries;
            Container::VariableArray<uint32_t> DirtyBits;
        };

        // ページ座標（絶対）
        int64_t PageOf(double light, float pageMeters)
        {
            return static_cast<int64_t>(std::floor(light / static_cast<double>(pageMeters)));
        }

        // dirty の階層の参照（段ごとに、dirty のページの相対の座標から全 mip のビットを立てる）。ビットの番号は Common/VirtualShadowMapMegaCull.glsl と同じ
        void SetExpectedBits(Container::VariableArray<uint32_t>& words, uint32_t level, int64_t relX, int64_t relY)
        {
            const uint32_t offsets[8] = {0u, 16384u, 20480u, 21504u, 21760u, 21824u, 21840u, 21844u};
            for (uint32_t mip = 0; mip < 8u; ++mip)
            {
                const uint32_t width = VirtualShadowMap::TABLE_DIMENSION >> mip;
                const uint32_t bit = level * VirtualShadowMap::MEGA_DIRTY_WORDS_PER_LEVEL * 32u + offsets[mip] +
                                     static_cast<uint32_t>(relY >> mip) * width + static_cast<uint32_t>(relX >> mip);
                words[bit >> 5u] |= 1u << (bit & 31u);
            }
        }

        // 1 回の実行。listCapacity は出力の一覧の容量（クラスタの数）
        bool Run(const DevicePtr& device,
                 VirtualShadowMapMegaCull& cull,
                 const VirtualShadowMapClipmap& clipmap,
                 uint32_t listCapacity,
                 uint64_t frameSerial,
                 Outcome& outcome,
                 Container::VariableArray<uint32_t>& expectedDirtyBits,
                 bool bLeafPageResident = true)
        {
            namespace Mega = Core::Rendering::MegaGeometry;
            outcome = Outcome{};

            const ResourceUsage storage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            const BufferPtr pageTable = device->CreateBuffer(BufferDesc(VirtualShadowMap::PageTableBytes(), storage, true, "VsmMegaTestPageTable"));
            const BufferPtr stats = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsBufferUsage() | ResourceUsage::ShaderRead, true, "VsmMegaTestStats"));
            const BufferPtr dirtyBits = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::MegaDirtyBitsBytes(), VirtualShadowMap::MegaDirtyBitsUsage() | ResourceUsage::ShaderRead, true, "VsmMegaTestDirtyBits"));
            const BufferPtr list = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::MegaCullListBytes(listCapacity), VirtualShadowMap::MegaCullListUsage() | ResourceUsage::ShaderRead, true, "VsmMegaTestList"));
            const BufferPtr clusters = device->CreateBuffer(
                BufferDesc(sizeof(Mega::GPUClusterData) * ClusterCount, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmMegaTestClusters"));
            const BufferPtr instances = device->CreateBuffer(
                BufferDesc(sizeof(TestInstance) * InstanceCount, ResourceUsage::StorageBuffer, true, "VsmMegaTestInstances"));
            const BufferPtr shadowInstances = device->CreateBuffer(
                BufferDesc(sizeof(MegaGeometryShadowInstance) * InstanceCount, ResourceUsage::StorageBuffer, true, "VsmMegaTestShadowInstances"));
            const BufferPtr geometryPages = device->CreateBuffer(
                BufferDesc(sizeof(Mega::GeometryPageTable::Entry) * 4u, ResourceUsage::StorageBuffer, true, "VsmMegaTestGeometryPages"));
            if (!pageTable || !stats || !dirtyBits || !list || !clusters || !instances || !shadowInstances || !geometryPages)
            {
                return false;
            }
            const uint64_t clusterAddress = clusters->GetDeviceAddress();
            if (clusterAddress == 0u)
            {
                return false;
            }

            // クラスタ
            Mega::GPUClusterData gpuClusters[ClusterCount];
            BuildClusters(gpuClusters);
            {
                void* mapped = clusters->Map(0u, sizeof(gpuClusters));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, gpuClusters, sizeof(gpuClusters));
                clusters->Unmap();
            }
            // ジオメトリのページの表: ページ 0 は常駐（区画 0）。ページ 1（葉）は bLeafPageResident が偽なら非常駐
            {
                Mega::GeometryPageTable::Entry entries[4] = {};
                entries[1].Region = bLeafPageResident ? 0u : Mega::PAGE_NON_RESIDENT;
                void* mapped = geometryPages->Map(0u, sizeof(entries));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, entries, sizeof(entries));
                geometryPages->Unmap();
            }

            // インスタンスの表と影の表
            TestInstance instanceTable[InstanceCount] = {};
            MegaGeometryShadowInstance shadowTable[InstanceCount] = {};
            uint32_t totalGroups = 0;
            for (uint32_t index = 0; index < InstanceCount; ++index)
            {
                const InstanceSpec& spec = Specs[index];
                const Math::Vector3 position = clipmap.LightRight * static_cast<float>(spec.LightX) +
                                               clipmap.LightUp * static_cast<float>(spec.LightY) +
                                               clipmap.Direction * static_cast<float>(spec.LightDepth);
                TestInstance& instance = instanceTable[index];
                for (uint32_t axis = 0; axis < 4u; ++axis)
                {
                    instance.World[axis * 4u + axis] = 1.0f;
                    instance.PreviousWorld[axis * 4u + axis] = 1.0f;
                }
                instance.World[12] = position.x;
                instance.World[13] = position.y;
                instance.World[14] = position.z;
                instance.PreviousWorld[12] = position.x;
                instance.PreviousWorld[13] = position.y;
                instance.PreviousWorld[14] = position.z;
                instance.ClusterInfo[0] = static_cast<uint32_t>(clusterAddress & 0xFFFFFFFFull);
                instance.ClusterInfo[1] = static_cast<uint32_t>(clusterAddress >> 32);
                instance.ClusterInfo[2] = ClusterCount;
                instance.BvhInfo[3] = 0u;

                MegaGeometryShadowInstance& shadow = shadowTable[index];
                shadow.BoundsSphere[0] = position.x;
                shadow.BoundsSphere[1] = position.y;
                shadow.BoundsSphere[2] = position.z;
                shadow.BoundsSphere[3] = spec.BoundsRadius;
                shadow.FirstGroup = totalGroups;
                if (spec.bCaster)
                {
                    shadow.Flags = MegaGeometryShadowFlagCaster | MegaGeometryShadowFlagBounds;
                    totalGroups += 1u; // 15 クラスタ = 1 ワークグループ
                }
            }
            {
                void* mapped = instances->Map(0u, sizeof(instanceTable));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, instanceTable, sizeof(instanceTable));
                instances->Unmap();
                mapped = shadowInstances->Map(0u, sizeof(shadowTable));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, shadowTable, sizeof(shadowTable));
                shadowInstances->Unmap();
            }

            // VSM のページの表: 0 で埋め、dirty のページ（割り当て済み | dirty）と、割り当て済みで dirty でないページを書く
            expectedDirtyBits.assign(VirtualShadowMap::MegaDirtyBitsBytes() / sizeof(uint32_t), 0u);
            {
                Container::VariableArray<uint32_t> table(VirtualShadowMap::PageTableBytes() / sizeof(uint32_t), 0u);
                uint32_t physical = 1u;
                const auto writePage = [&](uint32_t level, int64_t pageX, int64_t pageY, bool bDirty) {
                    const uint32_t torusX = VirtualShadowMapPageTorusAddress(pageX, VirtualShadowMap::TABLE_DIMENSION);
                    const uint32_t torusY = VirtualShadowMapPageTorusAddress(pageY, VirtualShadowMap::TABLE_DIMENSION);
                    table[level * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL + torusY * VirtualShadowMap::TABLE_DIMENSION + torusX] =
                        VirtualShadowMap::PAGE_ENTRY_ALLOCATED | (bDirty ? VirtualShadowMap::PAGE_ENTRY_DIRTY : 0u) | (physical++);
                    if (bDirty)
                    {
                        const VirtualShadowMapClipmapLevel& data = clipmap.Levels[level];
                        SetExpectedBits(expectedDirtyBits, level, pageX - data.OriginPageX, pageY - data.OriginPageY);
                    }
                };
                for (uint32_t level = 0; level < LevelCount; ++level)
                {
                    const float page = clipmap.Levels[level].PageMeters;
                    writePage(level, PageOf(16.0, page), PageOf(16.0, page), true);
                    if (level >= 1u)
                    {
                        writePage(level, PageOf(3016.0, page), PageOf(16.0, page), true);
                    }
                }
                {
                    const float page = clipmap.Levels[0].PageMeters;
                    // インスタンス 3 のページ: 割り当て済みで dirty でない。インスタンス 4 の矩形に入る 1 ページ（x = 780）: dirty
                    writePage(0u, PageOf(716.0, page), PageOf(16.0, page), false);
                    writePage(0u, PageOf(780.0, page), PageOf(16.0, page), true);
                }
                void* mapped = pageTable->Map(0u, VirtualShadowMap::PageTableBytes());
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, table.data(), VirtualShadowMap::PageTableBytes());
                pageTable->Unmap();
            }
            // 書かれたかを確かめるため、階層・統計・一覧は見張りの値で埋めておく（階層・一覧の頭・統計の語 8〜10 は記録が 0 にする）
            for (const BufferPtr& buffer : {dirtyBits, stats, list})
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
            {
                // 統計は呼ぶ前に 0（本番は割り当ての記録が 0 にする）
                uint32_t* mapped = static_cast<uint32_t*>(stats->Map(0u, stats->GetSize()));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memset(mapped, 0, static_cast<size_t>(stats->GetSize()));
                stats->Unmap();
            }

            VirtualShadowMapMegaCullDispatch dispatch;
            dispatch.Clipmap = &clipmap;
            dispatch.PageTable = pageTable;
            dispatch.Stats = stats;
            dispatch.DirtyBits = dirtyBits;
            dispatch.List = list;
            dispatch.Instances = instances;
            dispatch.ShadowInstances = shadowInstances;
            dispatch.MegaPageTable = geometryPages;
            dispatch.InstanceCount = InstanceCount;
            dispatch.TotalGroups = totalGroups;

            CommandListPtr commandList = device->CreateCommandList();
            if (!commandList)
            {
                return false;
            }
            cull.BeginFrame(0u, frameSerial);
            commandList->Begin();
            const BufferPtr owned[] = {pageTable, stats, dirtyBits, list};
            for (const BufferPtr& buffer : owned)
            {
                commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
            }
            outcome.bRecorded = cull.Record(commandList.get(), dispatch);
            for (const BufferPtr& buffer : owned)
            {
                commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
            }
            commandList->End();
            commandList->Submit(true);
            device->WaitIdle();
            outcome.GroupCount = cull.GetLastGroupCount();
            if (!outcome.bRecorded)
            {
                return true;
            }

            {
                const uint32_t* words = static_cast<const uint32_t*>(list->Map(0u, list->GetSize()));
                if (words == nullptr)
                {
                    return false;
                }
                outcome.Selected = words[0];
                outcome.Overflow = words[1];
                outcome.InstanceLevels = words[2];
                const uint32_t written = std::min(outcome.Selected, listCapacity);
                for (uint32_t index = 0; index < written; ++index)
                {
                    for (uint32_t word = 0; word < 4u; ++word)
                    {
                        outcome.Entries.push_back(words[VirtualShadowMap::MEGA_CULL_LIST_HEADER_WORDS + index * 4u + word]);
                    }
                }
                list->Unmap();
            }
            {
                const uint32_t* words = static_cast<const uint32_t*>(stats->Map(0u, stats->GetSize()));
                if (words == nullptr)
                {
                    return false;
                }
                outcome.StatInstances = words[VirtualShadowMap::StatMegaInstances];
                outcome.StatClusters = words[VirtualShadowMap::StatMegaClusters];
                outcome.StatOverflow = words[VirtualShadowMap::StatMegaOverflow];
                stats->Unmap();
            }
            {
                const Mega::GeometryPageTable::Entry* entries =
                    static_cast<const Mega::GeometryPageTable::Entry*>(geometryPages->Map(0u, sizeof(Mega::GeometryPageTable::Entry) * 4u));
                if (entries == nullptr)
                {
                    return false;
                }
                outcome.PageRequestStamps[0] = entries[0].RequestStamp;
                outcome.PageRequestStamps[1] = entries[1].RequestStamp;
                geometryPages->Unmap();
            }
            return ReadAll(dirtyBits, outcome.DirtyBits);
        }

        // 期待する（インスタンス、段、クラスタ）の一覧（昇順）。段 level のクラスタは高さ level のもの
        Container::VariableArray<uint64_t> ExpectedEntries(bool bLeafPageResident = true)
        {
            Container::VariableArray<uint64_t> expected;
            const auto add = [&expected, bLeafPageResident](uint32_t instance, uint32_t level) {
                Container::VariableArray<uint32_t> clusters;
                ExpectedClusters(level, clusters, bLeafPageResident);
                for (const uint32_t cluster : clusters)
                {
                    expected.push_back((static_cast<uint64_t>(instance) << 40) | (static_cast<uint64_t>(level) << 32) | cluster);
                }
            };
            for (uint32_t level = 0; level < LevelCount; ++level)
            {
                add(0u, level); // インスタンス 0: 全段
                if (level >= 1u)
                {
                    add(1u, level); // インスタンス 1: 段 0 の範囲の外
                }
            }
            std::sort(expected.begin(), expected.end());
            return expected;
        }

        Container::VariableArray<uint64_t> PackEntries(const Container::VariableArray<uint32_t>& words)
        {
            Container::VariableArray<uint64_t> packed;
            for (size_t index = 0; index + 3u < words.size(); index += 4u)
            {
                packed.push_back((static_cast<uint64_t>(words[index]) << 40) | (static_cast<uint64_t>(words[index + 1u]) << 32) | words[index + 2u]);
            }
            std::sort(packed.begin(), packed.end());
            return packed;
        }

        // 選ばれたクラスタが一つの切り口か: インスタンス・段ごとに、どの葉から根への道でもちょうど 1 つが選ばれる
        bool IsSingleCut(const Container::VariableArray<uint64_t>& packed, uint32_t instance, uint32_t level)
        {
            bool selected[ClusterCount] = {};
            for (const uint64_t entry : packed)
            {
                if (static_cast<uint32_t>(entry >> 40) == instance && static_cast<uint32_t>((entry >> 32) & 0xFFu) == level)
                {
                    selected[static_cast<uint32_t>(entry & 0xFFFFFFFFu)] = true;
                }
            }
            for (uint32_t leaf = 7u; leaf < ClusterCount; ++leaf)
            {
                uint32_t count = 0;
                for (uint32_t node = leaf;; node = (node - 1u) / 2u)
                {
                    count += selected[node] ? 1u : 0u;
                    if (node == 0u)
                    {
                        break;
                    }
                }
                if (count != 1u)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace MegaCull

    // ケース J を実行する。実行できなければ false
    bool RunMegaCullCases(const DevicePtr& device, ShaderManager& shaderManager, uint64_t& frameSerial)
    {
        VirtualShadowMapMegaCull cull;
        if (!cull.Initialize(device.get(), &shaderManager))
        {
            std::cerr << TestName << " MegaGeometry の投影物のカリングを初期化できませんでした" << std::endl;
            return false;
        }

        // 段 0 の幅 4096 m（texel 0.25 m・ページ 32 m）の 4 段。カメラは原点（段の範囲は ±2048 m・±4096 m・±8192 m・±16384 m）
        VirtualShadowMapClipmapSettings settings;
        settings.LevelCount = MegaCull::LevelCount;
        settings.FirstWidthMeters = 4096.0f;
        const VirtualShadowMapClipmap clipmap = BuildVirtualShadowMapClipmap(Math::Vector3(0.35f, -0.8f, 0.45f), 1u, Math::Vector3(0.0f, 0.0f, 0.0f), settings);
        Expect(clipmap.bEnabled, "ケース J のクリップマップが有効でなければならない");
        for (uint32_t level = 0; level < MegaCull::LevelCount; ++level)
        {
            Expect(clipmap.Levels[level].TexelMeters == MegaCull::TexelMeters(level), "ケース J の段の texel は 0.25 × 2^段 でなければならない");
        }

        // ----- J1: 容量が十分 -----
        {
            MegaCull::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!MegaCull::Run(device, cull, clipmap, 1024u, frameSerial++, outcome, expectedBits))
            {
                std::cerr << TestName << " ケース J1 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J1: カリングを記録しなければならない");
            Expect(outcome.GroupCount == 5u, "ケース J1: 影を落とす 5 インスタンスぶんの 5 ワークグループを出さなければならない");
            // dirty の階層: CPU の参照と全語一致（ページの表のトーラスの番地から範囲の原点からの相対の座標へ直し、全 mip のビットを立てる）
            Expect(outcome.DirtyBits.size() == expectedBits.size(), "ケース J1: dirty の階層の大きさが合わなければならない");
            uint32_t differentWords = 0;
            for (size_t word = 0; word < std::min(outcome.DirtyBits.size(), expectedBits.size()); ++word)
            {
                differentWords += outcome.DirtyBits[word] != expectedBits[word] ? 1u : 0u;
            }
            Expect(differentWords == 0u, "ケース J1: dirty の階層が CPU の参照と一致しなければならない");

            const Container::VariableArray<uint64_t> expected = MegaCull::ExpectedEntries();
            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(outcome.Selected == expected.size() && outcome.Selected == 22u, "ケース J1: 選んだクラスタは 22 件でなければならない");
            Expect(outcome.Overflow == 0u, "ケース J1: 溢れてはならない");
            Expect(outcome.InstanceLevels == 8u, "ケース J1: 判定を通った（インスタンス、段）は 8 でなければならない");
            Expect(outcome.StatInstances == 8u && outcome.StatClusters == 22u && outcome.StatOverflow == 0u,
                   "ケース J1: 統計の語 8〜10 が（8, 22, 0）でなければならない");
            Expect(actual == expected, "ケース J1: 選んだ（インスタンス、段、クラスタ）が期待の集合と一致しなければならない");
            // 段の texel が 2 倍になると選ばれるクラスタが粗く（高さが 1 つ上に）なり、どの葉から根への道でもちょうど 1 つ
            for (uint32_t level = 0; level < MegaCull::LevelCount; ++level)
            {
                Expect(MegaCull::IsSingleCut(actual, 0u, level), "ケース J1: インスタンス 0 の選択が一つの切り口でなければならない");
                if (level >= 1u)
                {
                    Expect(MegaCull::IsSingleCut(actual, 1u, level), "ケース J1: インスタンス 1 の選択が一つの切り口でなければならない");
                }
            }
            for (const uint64_t entry : actual)
            {
                const uint32_t level = static_cast<uint32_t>((entry >> 32) & 0xFFu);
                const uint32_t cluster = static_cast<uint32_t>(entry & 0xFFFFFFFFu);
                Expect(MegaCull::HeightOf(cluster) == level, "ケース J1: 段 k では高さ k のクラスタが選ばれなければならない");
            }
            Expect(outcome.PageRequestStamps[0] == 0u && outcome.PageRequestStamps[1] == 0u,
                   "ケース J1: カリングはジオメトリのページを要求してはならない（要求の印は 0 のまま）");
            std::cout << TestName << " ケース J1: 選んだクラスタ=" << outcome.Selected << " 通った（インスタンス、段）=" << outcome.InstanceLevels
                      << " 統計=(" << outcome.StatInstances << "," << outcome.StatClusters << "," << outcome.StatOverflow << ")" << std::endl;
        }

        // ----- J3: 葉のページが非常駐 -----
        // 影のためにページを読み込まず、常駐している物で描く: 段 0 は子（葉）のページが無いので、高さ 1 のクラスタが自分を描く（穴を作らない）。
        // 要求の印（ジオメトリのページの表）には触らない。ほかの段は変わらない。どの葉から根への道でも、ちょうど 1 つが選ばれる
        {
            MegaCull::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!MegaCull::Run(device, cull, clipmap, 1024u, frameSerial++, outcome, expectedBits, false))
            {
                std::cerr << TestName << " ケース J3 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J3: カリングを記録しなければならない");
            const Container::VariableArray<uint64_t> expected = MegaCull::ExpectedEntries(false);
            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(outcome.Selected == expected.size() && outcome.Selected == 18u, "ケース J3: 選んだクラスタは 18 件（段 0 は高さ 1 の 4 つ）でなければならない");
            Expect(actual == expected, "ケース J3: 葉のページが非常駐なら、段 0 は高さ 1 のクラスタが選ばれなければならない");
            Expect(outcome.PageRequestStamps[0] == 0u && outcome.PageRequestStamps[1] == 0u,
                   "ケース J3: 非常駐の子のページを要求してはならない（要求の印は 0 のまま）");
            for (uint32_t level = 0; level < MegaCull::LevelCount; ++level)
            {
                Expect(MegaCull::IsSingleCut(actual, 0u, level), "ケース J3: インスタンス 0 の選択が一つの切り口でなければならない");
            }
            std::cout << TestName << " ケース J3: 選んだクラスタ=" << outcome.Selected << " 要求の印=(" << outcome.PageRequestStamps[0] << ","
                      << outcome.PageRequestStamps[1] << ")" << std::endl;
        }

        // ----- J2: 出力の一覧の容量が 5（溢れる） -----
        {
            MegaCull::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!MegaCull::Run(device, cull, clipmap, 5u, frameSerial++, outcome, expectedBits))
            {
                std::cerr << TestName << " ケース J2 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J2: カリングを記録しなければならない");
            Expect(outcome.Selected == 22u && outcome.Overflow == 17u, "ケース J2: 選んだ数は 22・溢れは 17 でなければならない");
            Expect(outcome.StatClusters == 5u && outcome.StatOverflow == 17u, "ケース J2: 統計は書いた 5・溢れ 17 でなければならない");
            const Container::VariableArray<uint64_t> expected = MegaCull::ExpectedEntries();
            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(actual.size() == 5u, "ケース J2: 容量ぶん（5 件）だけ書かなければならない");
            for (const uint64_t entry : actual)
            {
                Expect(std::find(expected.begin(), expected.end(), entry) != expected.end(), "ケース J2: 書いた件は期待の集合に入らなければならない");
            }
            std::cout << TestName << " ケース J2: 選んだ=" << outcome.Selected << " 溢れ=" << outcome.Overflow << " 書いた=" << actual.size() << std::endl;
        }
        return true;
    }

    // ========================================
    // ケース K: MegaGeometry のクラスタの記録の経路（カリング → クラスタの記録 → 展開 → 描画）
    // ========================================

    // ケース F と同じ場面（近い四角形 + ローカルの頂点と変換の遠い四角形）を、形ごとに MegaGeometry のクラスタ 1 つ・インスタンス 1 つにして、
    // 本番の流れ（印付け → 割り当て → 消去 → カリング vsm_mega_cull.comp → クラスタの記録 vsm_mega_chunks.comp → 展開 → 描画）に通す。
    // 頂点・インデックスは 1 つの共有プールの塊のバッファに置き、クラスタの記録は GPU がカリングの一覧の 1 件から作る（頂点の基点・インデックスの
    // 先頭・アドレスはインスタンスの表と影の表から引く）。物理プールが、形の和の参照と、ケース F（記録を直接組み立てた手続きの経路）と全 texel で一致すること、
    // クラスタの記録の中身（種類・インスタンス・三角形の数・インデックスの先頭・頂点の基点・アドレス・段の集合・変換・境界）、展開の引数が一覧と整合することを確かめる。
    bool RunMegaDrawCase(const DevicePtr& device,
                         VirtualShadowMapPages& pages,
                         VirtualShadowMapRaster& raster,
                         ShaderManager& shaderManager,
                         const Scene& scene,
                         const TexturePtr& depth,
                         const CaseFData& caseF,
                         uint64_t& frameSerial)
    {
        namespace Mega = Core::Rendering::MegaGeometry;
        if (caseF.Shapes.empty() || caseF.Pool.empty())
        {
            std::cerr << TestName << " ケース K: ケース F の結果が無い" << std::endl;
            return false;
        }
        if (!raster.SupportsMegaCasters())
        {
            std::cout << TestName << " ケース K: この装置は DrawIndexedIndirectCount を使えないので省略" << std::endl;
            return true;
        }
        VirtualShadowMapMegaCull cull;
        if (!cull.Initialize(device.get(), &shaderManager))
        {
            std::cerr << TestName << " ケース K: MegaGeometry の投影物のカリングを初期化できませんでした" << std::endl;
            return false;
        }

        ChunkGeometry geometry = BuildChunks(scene, caseF.Shapes);
        const uint32_t shapeCount = static_cast<uint32_t>(geometry.Chunks.size());
        constexpr uint32_t ListCapacity = 4096;
        constexpr uint32_t InstanceCapacity = 16384;

        // 共有プールの塊のバッファ: 頂点（1 つ 32 バイト）の後ろにインデックスを並べる。メッシュの頂点は塊の先頭から VertexPadding 個目から始まり
        // （インスタンスの頂点の基点 = VertexPadding。クラスタの頂点の位置はそこからの相対）、インデックスの基点はインデックスの語の位置
        constexpr uint32_t VertexPadding = 5;
        const uint64_t vertexBytes = (static_cast<uint64_t>(VertexPadding) * 8u + geometry.Vertices.size()) * sizeof(float);
        const uint64_t indexBytes = geometry.Indices.size() * sizeof(uint32_t);
        const BufferPtr poolChunk = device->CreateBuffer(
            BufferDesc(vertexBytes + indexBytes, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmMegaDrawTestPoolChunk"));
        const BufferPtr clusterBuffer = device->CreateBuffer(BufferDesc(
            sizeof(Mega::GPUClusterData) * shapeCount, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmMegaDrawTestClusters"));
        const BufferPtr instanceBuffer = device->CreateBuffer(
            BufferDesc(sizeof(MegaCull::TestInstance) * shapeCount, ResourceUsage::StorageBuffer, true, "VsmMegaDrawTestInstances"));
        const BufferPtr shadowBuffer = device->CreateBuffer(
            BufferDesc(sizeof(MegaGeometryShadowInstance) * shapeCount, ResourceUsage::StorageBuffer, true, "VsmMegaDrawTestShadowInstances"));
        const BufferPtr geometryPages = device->CreateBuffer(
            BufferDesc(sizeof(Mega::GeometryPageTable::Entry) * 4u, ResourceUsage::StorageBuffer, true, "VsmMegaDrawTestGeometryPages"));
        const ResourceUsage readable = ResourceUsage::ShaderRead;
        const BufferPtr dirtyBits = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::MegaDirtyBitsBytes(), VirtualShadowMap::MegaDirtyBitsUsage() | readable, true, "VsmMegaDrawTestDirtyBits"));
        const BufferPtr list = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::MegaCullListBytes(ListCapacity), VirtualShadowMap::MegaCullListUsage() | readable, true, "VsmMegaDrawTestList"));
        const BufferPtr megaChunks = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterChunkBytes(ListCapacity), VirtualShadowMap::MegaChunkUsage() | readable, true, "VsmMegaDrawTestMegaChunks"));
        const BufferPtr hostChunks = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterChunkBytes(0u), VirtualShadowMap::RasterChunkUsage(), true, "VsmMegaDrawTestHostChunks"));
        const BufferPtr instances = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterInstanceBytes(InstanceCapacity), VirtualShadowMap::RasterInstanceUsage() | readable, true, "VsmMegaDrawTestRasterInstances"));
        const BufferPtr draws = device->CreateBuffer(
            BufferDesc(VirtualShadowMap::RasterDrawBytes(ListCapacity), VirtualShadowMap::RasterDrawUsage() | readable, true, "VsmMegaDrawTestDraws"));
        Resources resources;
        if (!poolChunk || !clusterBuffer || !instanceBuffer || !shadowBuffer || !geometryPages || !dirtyBits || !list || !megaChunks || !hostChunks ||
            !instances || !draws || !CreateResources(device, caseF.PoolPages, resources))
        {
            std::cerr << TestName << " ケース K: バッファを作れませんでした" << std::endl;
            return false;
        }
        const uint64_t poolAddress = poolChunk->GetDeviceAddress();
        const uint64_t clusterAddress = clusterBuffer->GetDeviceAddress();
        if (poolAddress == 0u || clusterAddress == 0u)
        {
            std::cerr << TestName << " ケース K: バッファのアドレスを取れませんでした" << std::endl;
            return false;
        }
        const uint32_t indexBase = static_cast<uint32_t>(vertexBytes / sizeof(uint32_t));
        {
            uint8_t* mapped = static_cast<uint8_t*>(poolChunk->Map(0u, vertexBytes + indexBytes));
            if (mapped == nullptr)
            {
                return false;
            }
            // 先頭の余りの頂点は、基点を足し忘れたときに別の頂点を引くよう、大きな値で埋める
            float* padding = reinterpret_cast<float*>(mapped);
            for (uint32_t word = 0; word < VertexPadding * 8u; ++word)
            {
                padding[word] = 1.0e6f;
            }
            std::memcpy(mapped + static_cast<size_t>(VertexPadding) * 8u * sizeof(float), geometry.Vertices.data(), geometry.Vertices.size() * sizeof(float));
            std::memcpy(mapped + vertexBytes, geometry.Indices.data(), static_cast<size_t>(indexBytes));
            poolChunk->Unmap();
        }

        // クラスタ・インスタンスの表・影の表（形ごとに 1 つ）。クラスタは根（親の誤差が無限大・自分の誤差 0）なので、どの段でも選ばれる
        Container::VariableArray<Mega::GPUClusterData> clusters;
        Container::VariableArray<MegaCull::TestInstance> instanceTable;
        Container::VariableArray<MegaGeometryShadowInstance> shadowTable;
        clusters.resize(shapeCount);
        instanceTable.resize(shapeCount);
        shadowTable.resize(shapeCount);
        std::memset(clusters.data(), 0, clusters.size() * sizeof(Mega::GPUClusterData));
        std::memset(instanceTable.data(), 0, instanceTable.size() * sizeof(MegaCull::TestInstance));
        std::memset(shadowTable.data(), 0, shadowTable.size() * sizeof(MegaGeometryShadowInstance));
        for (uint32_t index = 0; index < shapeCount; ++index)
        {
            const VsmShadowChunk& chunk = geometry.Chunks[index];
            const uint32_t cornerCount = caseF.Shapes[index].bRect ? 4u : 3u;
            // ローカルの頂点から球を作る（中心 = 頂点の平均、半径 = 最も遠い頂点までの距離）
            double center[3] = {};
            for (uint32_t corner = 0; corner < cornerCount; ++corner)
            {
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    center[axis] += static_cast<double>(geometry.Vertices[static_cast<size_t>(chunk.Record.VertexBase + corner) * 8u + axis]) / cornerCount;
                }
            }
            double radius = 0.0;
            for (uint32_t corner = 0; corner < cornerCount; ++corner)
            {
                double distanceSquared = 0.0;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    const double delta = static_cast<double>(geometry.Vertices[static_cast<size_t>(chunk.Record.VertexBase + corner) * 8u + axis]) - center[axis];
                    distanceSquared += delta * delta;
                }
                radius = std::max(radius, std::sqrt(distanceSquared));
            }
            radius += 1.0e-3;

            Mega::GPUClusterData& cluster = clusters[index];
            cluster.BoundsCenterX = static_cast<float>(center[0]);
            cluster.BoundsCenterY = static_cast<float>(center[1]);
            cluster.BoundsCenterZ = static_cast<float>(center[2]);
            cluster.BoundsRadius = static_cast<float>(radius);
            cluster.ConeCutoff = -1.0f;
            cluster.IndexOffset = chunk.Record.FirstIndex;
            cluster.IndexCount = chunk.Record.TriangleCount * 3u;
            cluster.VertexOffset = static_cast<int32_t>(chunk.Record.VertexBase);
            cluster.LODLevel = 0u;
            cluster.LODError = 0.0f;
            cluster.Flags = Mega::GPU_CLUSTER_FLAG_BAKED_LOD;
            cluster.ParentError = 3.402823466e+38f;
            cluster.GroupId = 0xFFFFFFFFu; // 根
            cluster.PageId = 0u;
            cluster.ChildPageId = Mega::INVALID_PAGE_ID;

            // ワールドの行列は列優先: 列 column・行 row の要素は World[row * 4 + column]（3 行。4 行目は (0, 0, 0, 1)）
            MegaCull::TestInstance& instance = instanceTable[index];
            for (uint32_t column = 0; column < 4u; ++column)
            {
                for (uint32_t row = 0; row < 3u; ++row)
                {
                    instance.World[column * 4u + row] = chunk.World[row * 4u + column];
                    instance.PreviousWorld[column * 4u + row] = chunk.World[row * 4u + column];
                }
                instance.World[column * 4u + 3u] = column == 3u ? 1.0f : 0.0f;
                instance.PreviousWorld[column * 4u + 3u] = column == 3u ? 1.0f : 0.0f;
            }
            const uint64_t address = clusterAddress + static_cast<uint64_t>(index) * sizeof(Mega::GPUClusterData);
            instance.ClusterInfo[0] = static_cast<uint32_t>(address & 0xFFFFFFFFull);
            instance.ClusterInfo[1] = static_cast<uint32_t>(address >> 32);
            instance.ClusterInfo[2] = 1u;
            instance.DrawInfo[1] = VertexPadding; // 頂点の基点（塊の先頭から）
            instance.DrawInfo[2] = indexBase;  // インデックスの基点（塊の先頭から）
            instance.BvhInfo[3] = 0u;

            // 影の表: 境界はワールドの球（中心 = 変換した中心、半径 = ローカルの半径 × 拡大）
            const float scale = caseF.Shapes[index].bLocalTransform ? LocalScale : 1.0f;
            MegaGeometryShadowInstance& shadow = shadowTable[index];
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                shadow.BoundsSphere[axis] = chunk.World[axis * 4u + 0u] * static_cast<float>(center[0]) +
                                            chunk.World[axis * 4u + 1u] * static_cast<float>(center[1]) +
                                            chunk.World[axis * 4u + 2u] * static_cast<float>(center[2]) + chunk.World[axis * 4u + 3u];
            }
            shadow.BoundsSphere[3] = static_cast<float>(radius) * scale;
            shadow.FirstGroup = index; // 1 クラスタ = 1 ワークグループ
            shadow.Flags = MegaGeometryShadowFlagCaster | MegaGeometryShadowFlagBounds;
            shadow.VertexAddress[0] = static_cast<uint32_t>(poolAddress & 0xFFFFFFFFull);
            shadow.VertexAddress[1] = static_cast<uint32_t>(poolAddress >> 32);
            shadow.IndexAddress[0] = shadow.VertexAddress[0];
            shadow.IndexAddress[1] = shadow.VertexAddress[1];
        }
        clusterBuffer->Update(clusters.data(), clusters.size() * sizeof(Mega::GPUClusterData));
        instanceBuffer->Update(instanceTable.data(), instanceTable.size() * sizeof(MegaCull::TestInstance));
        shadowBuffer->Update(shadowTable.data(), shadowTable.size() * sizeof(MegaGeometryShadowInstance));
        {
            Mega::GeometryPageTable::Entry entries[4] = {};
            entries[0].Region = 0u; // ページ 0 は常駐
            geometryPages->Update(entries, sizeof(entries));
        }
        // 書かれたかを確かめるため、階層・一覧・インスタンス・引数は見張りで埋める
        for (const BufferPtr& buffer : {dirtyBits, list, instances, draws})
        {
            FillWords(buffer, GarbageWord);
        }
        // クラスタの記録は、全件を「展開されれば物理ページを書き換える」身代わりの記録で埋める（形 0 と同じ頂点を深度だけ光源側へ 7 m ずらして全段へ）。
        // 一覧の件数より後ろの記録を展開が読むと、描くはずのない身代わりが物理ページと統計に現れる
        {
            VsmShadowChunk decoy = geometry.Chunks[0];
            decoy.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::MegaGeometryCluster);
            decoy.Record.FirstIndex = indexBase + geometry.Chunks[0].Record.FirstIndex;
            decoy.Record.VertexBase = VertexPadding + geometry.Chunks[0].Record.VertexBase;
            decoy.Record.VertexAddress = poolAddress;
            decoy.Record.IndexAddress = poolAddress;
            decoy.LevelMask = 0xFFFFFFFFu;
            const float shift[3] = {scene.Clipmap.Direction.x * -7.0f, scene.Clipmap.Direction.y * -7.0f, scene.Clipmap.Direction.z * -7.0f};
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                decoy.World[axis * 4u + 3u] += shift[axis];
                decoy.BoundsMin[axis] += shift[axis];
                decoy.BoundsMax[axis] += shift[axis];
            }
            Container::VariableArray<VsmShadowChunk> decoys(ListCapacity, decoy);
            megaChunks->Update(decoys.data(), decoys.size() * sizeof(VsmShadowChunk));
        }

        const uint64_t serial = frameSerial++;
        pages.BeginFrame(0, serial);
        cull.BeginFrame(0, serial);
        raster.BeginFrame(0, serial);
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << TestName << " ケース K: コマンドリストを作れませんでした" << std::endl;
            return false;
        }
        const BufferPtr buffers[] = {resources.Pool,      resources.PageTable, resources.RequestBits, resources.FreeList, resources.Stats, resources.DirtyList,
                                     dirtyBits,           list,                megaChunks,            hostChunks,         instances,       draws};
        commandList->Begin();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        VirtualShadowMapPagesDispatch pagesDispatch;
        pagesDispatch.PoolPages = resources.PoolPages;
        pagesDispatch.Pool = resources.Pool;
        pagesDispatch.PageTable = resources.PageTable;
        pagesDispatch.RequestBits = resources.RequestBits;
        pagesDispatch.FreeList = resources.FreeList;
        pagesDispatch.Stats = resources.Stats;
        pagesDispatch.DirtyList = resources.DirtyList;
        pagesDispatch.Depth = depth;
        pagesDispatch.Clipmap = &scene.Clipmap;
        std::memcpy(pagesDispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(pagesDispatch.InverseViewProjection));
        std::memcpy(pagesDispatch.CameraPosition, scene.CameraPosition, sizeof(pagesDispatch.CameraPosition));
        pagesDispatch.FovYDegrees = scene.Camera.FieldOfView;
        const bool bPages = pages.Record(commandList.get(), pagesDispatch);

        VirtualShadowMapMegaCullDispatch cullDispatch;
        cullDispatch.Clipmap = &scene.Clipmap;
        cullDispatch.PageTable = resources.PageTable;
        cullDispatch.Stats = resources.Stats;
        cullDispatch.DirtyBits = dirtyBits;
        cullDispatch.List = list;
        cullDispatch.Chunks = megaChunks;
        cullDispatch.Instances = instanceBuffer;
        cullDispatch.ShadowInstances = shadowBuffer;
        cullDispatch.MegaPageTable = geometryPages;
        cullDispatch.InstanceCount = shapeCount;
        cullDispatch.TotalGroups = shapeCount;
        const bool bCull = cull.Record(commandList.get(), cullDispatch);

        VirtualShadowMapRasterDispatch rasterDispatch;
        rasterDispatch.Clipmap = &scene.Clipmap;
        rasterDispatch.PoolPages = resources.PoolPages;
        rasterDispatch.Pool = resources.Pool;
        rasterDispatch.PageTable = resources.PageTable;
        rasterDispatch.Stats = resources.Stats;
        rasterDispatch.Chunks = hostChunks;
        rasterDispatch.ChunkCount = 0u;
        rasterDispatch.Instances = instances;
        rasterDispatch.Draws = draws;
        rasterDispatch.MegaChunks = megaChunks;
        rasterDispatch.MegaList = list;
        rasterDispatch.MegaCapacity = ListCapacity;
        const bool bRaster = raster.Record(commandList.get(), rasterDispatch);
        const bool bMegaDraw = raster.WasMegaDrawRecorded();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        Expect(bPages && bCull && bRaster, "ケース K: 印付け・カリング・展開・描画を記録できなければならない");
        Expect(cull.WasChunkBuilt(), "ケース K: カリングがクラスタの記録を作らなければならない");
        Expect(bMegaDraw, "ケース K: MegaGeometry のクラスタの間接描画（DrawIndexedIndirectCount）を記録しなければならない");
        Expect(raster.GetLastDrawCount() == 0u, "ケース K: ホストが書いた塊が無いので、塊ごとの間接描画は無い");

        Container::VariableArray<uint32_t> poolWords;
        Container::VariableArray<uint32_t> pageTableWords;
        Container::VariableArray<uint32_t> statsWords;
        Container::VariableArray<uint32_t> listWords;
        Container::VariableArray<uint32_t> chunkWords;
        Container::VariableArray<uint32_t> drawWords;
        if (!ReadAll(resources.Pool, poolWords) || !ReadAll(resources.PageTable, pageTableWords) || !ReadAll(resources.Stats, statsWords) ||
            !ReadAll(list, listWords) || !ReadAll(megaChunks, chunkWords) || !ReadAll(draws, drawWords))
        {
            std::cerr << TestName << " ケース K: 読み戻せませんでした" << std::endl;
            return false;
        }
        const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, pageTableWords);
        Expect(pageInfos.size() == caseF.Pages.size(), "ケース K: 割り当てたページの数がケース F と同じでなければならない");

        // 一覧: 選んだクラスタの件数。どの件も（形のインスタンス、段、クラスタ 0）で、形ごとに 1 件以上ある
        const uint32_t selected = listWords[0];
        const uint32_t overflow = listWords[1];
        Expect(selected > 0u && selected <= ListCapacity && overflow == 0u, "ケース K: カリングが 1 件以上を選び、溢れてはならない");
        Expect(statsWords[VirtualShadowMap::StatMegaClusters] == selected && statsWords[VirtualShadowMap::StatMegaOverflow] == 0u,
               "ケース K: カリングの統計が一覧の件数と一致しなければならない");
        Container::VariableArray<uint32_t> selectedPerShape(shapeCount, 0u);
        uint32_t chunkErrors = 0;
        uint32_t expectedInstanceTotal = 0;
        const uint32_t live = std::min(selected, ListCapacity);
        for (uint32_t entryIndex = 0; entryIndex < live; ++entryIndex)
        {
            const uint32_t* entry = &listWords[VirtualShadowMap::MEGA_CULL_LIST_HEADER_WORDS + entryIndex * 4u];
            const uint32_t shapeIndex = entry[0];
            const uint32_t level = entry[1];
            if (shapeIndex >= shapeCount || level >= scene.Clipmap.LevelCount || entry[2] != 0u)
            {
                ++chunkErrors;
                continue;
            }
            ++selectedPerShape[shapeIndex];

            // クラスタの記録の中身（GPU が一覧の 1 件から作ったもの）
            VsmShadowChunk made;
            std::memcpy(&made, &chunkWords[static_cast<size_t>(entryIndex) * sizeof(VsmShadowChunk) / sizeof(uint32_t)], sizeof(made));
            const VsmShadowChunk& source = geometry.Chunks[shapeIndex];
            bool bOk = made.Record.Kind == static_cast<uint32_t>(VisibilityBuffer::RecordKind::MegaGeometryCluster) && made.Record.InstanceIndex == shapeIndex &&
                       made.Record.TriangleCount == source.Record.TriangleCount && made.Record.FirstIndex == indexBase + source.Record.FirstIndex &&
                       made.Record.VertexBase == VertexPadding + source.Record.VertexBase && made.Record.VertexAddress == poolAddress && made.Record.IndexAddress == poolAddress &&
                       made.Record.PreviousTransformIndex == VisibilityBuffer::NO_PREVIOUS_TRANSFORM && made.Record.PreviousVertexAddress == 0u &&
                       made.LevelMask == (1u << level) && made.Reserved == 0u;
            bOk = bOk && std::memcmp(made.World, source.World, sizeof(made.World)) == 0;
            // 境界: 形の頂点（ワールド）を含む
            const uint32_t cornerCount = caseF.Shapes[shapeIndex].bRect ? 4u : 3u;
            for (uint32_t corner = 0; corner < cornerCount && bOk; ++corner)
            {
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    float world = source.World[axis * 4u + 3u];
                    for (uint32_t inner = 0; inner < 3u; ++inner)
                    {
                        world += source.World[axis * 4u + inner] *
                                 geometry.Vertices[static_cast<size_t>(source.Record.VertexBase + corner) * 8u + inner];
                    }
                    bOk = bOk && made.BoundsMin[axis] <= world + 1.0e-3f && made.BoundsMax[axis] >= world - 1.0e-3f;
                }
            }
            if (!bOk)
            {
                ++chunkErrors;
            }

            // 展開の引数: この件（段 level のページだけ）の dirty のページの数が instanceCount
            Container::VariableArray<PageInfo> levelPages;
            for (const PageInfo& page : pageInfos)
            {
                if (page.Level == level)
                {
                    levelPages.push_back(page);
                }
            }
            const uint32_t expectedInstances = CountExpectedInstances(scene, made, levelPages);
            expectedInstanceTotal += expectedInstances;
            const uint32_t* command = &drawWords[VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS + entryIndex * VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS];
            if (command[0] != source.Record.TriangleCount * 3u || command[1] != expectedInstances)
            {
                ++chunkErrors;
            }
        }
        Expect(chunkErrors == 0u, "ケース K: クラスタの記録・展開の引数が、一覧の件と整合しなければならない");
        for (uint32_t index = 0; index < shapeCount; ++index)
        {
            Expect(selectedPerShape[index] > 0u, "ケース K: どの形のクラスタも 1 つ以上の段で選ばれなければならない");
        }
        Expect(statsWords[VirtualShadowMap::StatRasterInstances] == expectedInstanceTotal && statsWords[VirtualShadowMap::StatRasterOverflow] == 0u,
               "ケース K: 展開の統計（書いたインスタンス）が引数の合計と一致し、溢れてはならない");

        // 物理プール: 形の和の参照と、ケース F（記録を直接組み立てた手続きの経路）の全 texel と一致
        const PoolCheck check = CheckPool("ケース K", scene, caseF.Shapes, pageInfos, poolWords, VirtualShadowMap::EMPTY_DEPTH_BITS);
        uint32_t differentWords = 0;
        uint32_t comparedPages = 0;
        Expect(poolWords.size() == caseF.Pool.size(), "ケース K: 物理プールの大きさがケース F と同じでなければならない");
        for (const PageInfo& page : pageInfos)
        {
            const PageInfo* counterpart = nullptr;
            for (const PageInfo& candidate : caseF.Pages)
            {
                if (candidate.Level == page.Level && candidate.AbsX == page.AbsX && candidate.AbsY == page.AbsY)
                {
                    counterpart = &candidate;
                    break;
                }
            }
            Expect(counterpart != nullptr && counterpart->bDirty == page.bDirty, "ケース K: ケース F と同じページが割り当てられなければならない");
            if (counterpart == nullptr)
            {
                continue;
            }
            ++comparedPages;
            const size_t baseK = static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS;
            const size_t baseF = static_cast<size_t>(counterpart->Physical) * VirtualShadowMap::PAGE_WORDS;
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                differentWords += poolWords[baseK + word] != caseF.Pool[baseF + word] ? 1u : 0u;
            }
        }
        Expect(comparedPages == pageInfos.size(), "ケース K: すべてのページをケース F と比べなければならない");
        std::cout << TestName << " ケース K: 選んだクラスタ=" << selected << " 書いたインスタンス=" << statsWords[VirtualShadowMap::StatRasterInstances]
                  << " 比べた texel=" << check.Compared << " 形に覆われた texel=" << check.Covered << " 不一致=" << check.Mismatches
                  << " ケース F との違い（語）=" << differentWords << std::endl;
        Expect(check.Mismatches == 0u && check.Covered > 2000u, "ケース K: MegaGeometry のクラスタの経路で描いた物理ページが形の和の参照と一致しなければならない");
        Expect(differentWords == 0u, "ケース K: MegaGeometry のクラスタの経路で描いた texel が、ケース F の手続きの経路と同じでなければならない");
        return true;
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

            // ----- ケース F〜H: 影の塊の展開・描画 -----
            {
                VirtualShadowMapRaster raster;
                if (!raster.Initialize(device.get(), &shaderManager))
                {
                    std::cerr << TestName << " VSM の展開・描画のパイプラインを初期化できませんでした" << std::endl;
                    return 1;
                }
                CaseFData caseF;
                if (!RunRasterCases(device, pages, raster, scene, reference, depth, frameSerial, caseF))
                {
                    return 1;
                }
                // ----- ケース K: MegaGeometry のクラスタの記録の経路（ケース F と同じ場面） -----
                if (!RunMegaDrawCase(device, pages, raster, shaderManager, scene, depth, caseF, frameSerial))
                {
                    return 1;
                }
            }

            // ----- ケース J: MegaGeometry の投影物のカリング -----
            if (!RunMegaCullCases(device, shaderManager, frameSerial))
            {
                return 1;
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
