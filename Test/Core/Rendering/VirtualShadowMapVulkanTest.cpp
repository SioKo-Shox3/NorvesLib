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
//   ケース J5（点光源の面のカリング。透視のスライス）: 1 灯 × 6 面 × 6 段のスライスの表を外から渡し（太陽なし）、同じ木のクラスタを面 0 の軸の上
//     12 m と 40 m に置いて本番のカリングに通す。texel は球の最も近い点の面の軸の距離 z での 2z ÷ 段の解像度で、光源から遠いほど・段が粗いほど粗いクラスタ
//     （葉 8・4・2・根 1）が選ばれ、どの葉から根への道でもちょうど 1 つが選ばれること、面のページの表（一辺 32〜1）の dirty の階層が参照と全語一致すること、
//     面 0 以外・Range の外・影を落とさないインスタンスは選ばれないこと、溢れたクラスタの範囲のページに再描画の印が付くことを確かめる。
//   ケース K（MegaGeometry のクラスタの記録の経路）: ケース F と同じ場面を、形ごとに MegaGeometry のクラスタ 1 つにして、本番の流れ
//     （印付け → 割り当て → 消去 → カリング → クラスタの記録 → 展開 → 描画）に通し、物理プールが形の和の参照とケース F の手続きの経路と全 texel で一致すること、
//     GPU が作ったクラスタの記録（種類・インデックスの先頭・頂点の基点・アドレス・段の集合・変換・境界）と展開の引数が一覧の件と整合することを確かめる。
//   ケース L（照明が使う VSM の読み出し。Common/VirtualShadowMap.glsl の VsmSampleSunShadow を計算シェーダー vsm_sample_probe.comp から呼ぶ）: ケース F の物理プールとページの表を、
//     受け手の点から照明と同じ関数で読む。画面の安定した画素では印を付けた段のページが逃げずに読め、使った段が CPU の選んだ段と一致すること、
//     段を固定した受け手で、影の中心で 0・影の外で 1・縁（PCF の標本が縁をまたぐ位置）で 0 と 1 の間の値（CPU の参照と一致）・ページの境界をまたぐ標本も読めること、
//     受け手のページが割り当て外なら粗い段の値になり（逃げた標本 16）、粗い段にも無ければ影なし（1）になることを確かめる。
//   ケース M（ページのキャッシュ。前フレームのページの表の引き継ぎ・無効化。従来のケース A〜L はキャッシュを使わない = --vsm-cache=off 相当）:
//     M1 最初のフレームは全ページを描く。M2 止まった場面の 2 フレーム目は描かれるページが 0・持ち越しが要求の数・物理プールの中身と物理ページが不変
//     （M2b は 30 フレームを超えて続けても、要求のあるページは空きへ戻らない）。M3 投影物を動かすと、前フレームと今フレームの境界のライト空間の矩形が覆うページだけが
//     dirty になり、物理プールが毎フレーム描き直したときと全 texel で一致する（3a 同じページの中の動き・3b 別のページへ出る動き）。
//     M4 太陽の向きを変えると（1 フレームの変化が微小でも）全ページが描き直される。M5 深度の原点（スナップ）が動くと全ページが描き直される。M6 段の中心が動くと、範囲に残ったページは
//     同じ物理ページのまま描き直されず、範囲の外へ出たページは空きへ戻る。M7 要求の無いページは 30 フレーム持ち越し、次のフレームで空きへ戻る。
//     M8 空きが足りないとき、要求の無いページを古い順（最も昔に要求されたもの）から戻す。
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
#include "Rendering/VirtualShadowMapSample.h"
#include "Test/Core/Rendering/VirtualShadowMapWideTestSupport.h"
#include "Rendering/VisibilityBuffer.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
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

    // 印付けに渡すカメラの前方（影の範囲は前方への距離で測る。範囲は設定の [0, MaxShadowDistance]）
    void SetCameraForward(VirtualShadowMapPagesDispatch& dispatch, const Scene& scene)
    {
        dispatch.CameraForward[0] = scene.Camera.ForwardX;
        dispatch.CameraForward[1] = scene.Camera.ForwardY;
        dispatch.CameraForward[2] = scene.Camera.ForwardZ;
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

        // 影の範囲はカメラの前方への距離で測る（照明と同じ。範囲は [0, MaxShadowDistance]）。端の許容の内側は曖昧
        const double forwardLength = std::sqrt(static_cast<double>(scene.Camera.ForwardX) * scene.Camera.ForwardX +
                                               static_cast<double>(scene.Camera.ForwardY) * scene.Camera.ForwardY +
                                               static_cast<double>(scene.Camera.ForwardZ) * scene.Camera.ForwardZ);
        const double viewDistance = (offset[0] * scene.Camera.ForwardX + offset[1] * scene.Camera.ForwardY + offset[2] * scene.Camera.ForwardZ) / forwardLength;
        const double shadowFar = static_cast<double>(scene.Settings.MaxShadowDistance);
        if (std::abs(viewDistance) < AmbiguityToleranceMeters || std::abs(viewDistance - shadowFar) < AmbiguityToleranceMeters)
        {
            return PixelKind::Ambiguous;
        }
        if (viewDistance < 0.0 || viewDistance > shadowFar)
        {
            return PixelKind::Outside;
        }

        // 段はカメラからの直線距離で選ぶ。視錐台の端では直線距離が MaxShadowDistance を超えるので、距離の上限を広げた設定で選ぶ
        // （段の選び方はそれ以外 MaxShadowDistance に依らない）
        VirtualShadowMapClipmapSettings selectSettings = scene.Settings;
        selectSettings.MaxShadowDistance *= VirtualShadowMapThresholdDistanceScale;
        const float fovY = scene.Camera.FieldOfView;
        const float height = static_cast<float>(ImageHeight);
        const int32_t level = SelectVirtualShadowMapLevel(selectSettings, static_cast<float>(distance), fovY, height);
        const int32_t levelNear = SelectVirtualShadowMapLevel(selectSettings, static_cast<float>(distance - AmbiguityToleranceMeters), fovY, height);
        const int32_t levelFar = SelectVirtualShadowMapLevel(selectSettings, static_cast<float>(distance + AmbiguityToleranceMeters), fovY, height);
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
        // 印付けの隣のページへの印の範囲 = texel に比例する分 + ワールドの長さの分（物理の半影の上限）
        const double margin = static_cast<double>(VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS) * static_cast<double>(levelData.TexelMeters) +
                              static_cast<double>(VirtualShadowMap::MAX_FILTER_RADIUS_METERS);

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

    // ページの表・要求のビット列は sliceCount 個のスライスぶん（既定は太陽の段の数）
    bool CreateResources(const DevicePtr& device, uint32_t poolPages, Resources& resources, uint32_t sliceCount = VirtualShadowMap::LEVEL_COUNT)
    {
        resources.PoolPages = poolPages;
        const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        resources.Pool = device->CreateBuffer(BufferDesc(VirtualShadowMap::PoolBytes(poolPages), usage, true, "VsmTestPool"));
        resources.PageTable = device->CreateBuffer(BufferDesc(VirtualShadowMap::PageTableBytes(sliceCount), usage, true, "VsmTestPageTable"));
        resources.RequestBits = device->CreateBuffer(BufferDesc(VirtualShadowMap::RequestBitsBytes(sliceCount), usage, true, "VsmTestRequestBits"));
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

    // 33 個以上のスライスの場面で、ページの記録・展開へ渡す入力（スライスの数・外から渡すスライスの表・印付けが選ぶ太陽の段の先頭）
    struct WideInput
    {
        uint32_t SliceCount = 0;
        const GPUVsmSlice* Slices = nullptr;
        uint32_t MarkFirstSlice = 0;
    };

    // 記録して読み戻す。depth が null なら深度なし。frameSerial は 0 以外で呼び出しごとに増やす
    bool RunPages(const DevicePtr& device,
                  VirtualShadowMapPages& pages,
                  const Scene& scene,
                  const Resources& resources,
                  const TexturePtr& depth,
                  bool bPassClipmap,
                  uint64_t frameSerial,
                  bool bFirstUse,
                  Readback& readback,
                  const VirtualShadowMapPointLights* pointLights = nullptr,
                  uint32_t sliceCount = 0)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << TestName << " コマンドリストを作れませんでした" << std::endl;
            return false;
        }
        VirtualShadowMapPagesDispatch dispatch;
        dispatch.SliceCount = sliceCount;
        dispatch.PointLights = pointLights;
        dispatch.PoolPages = resources.PoolPages;
        dispatch.Pool = resources.Pool;
        dispatch.PageTable = resources.PageTable;
        dispatch.RequestBits = resources.RequestBits;
        dispatch.FreeList = resources.FreeList;
        dispatch.Stats = resources.Stats;
        dispatch.DirtyList = resources.DirtyList;
        dispatch.Depth = depth;
        dispatch.Clipmap = bPassClipmap ? &scene.Clipmap : nullptr;
        // この関数を使う従来のケースは、毎フレームすべて割り当て直す動き（--vsm-cache=off 相当）を確かめる。キャッシュを使う場面は RunCachePages
        dispatch.bCacheEnabled = false;
        std::memcpy(dispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(dispatch.InverseViewProjection));
        std::memcpy(dispatch.CameraPosition, scene.CameraPosition, sizeof(dispatch.CameraPosition));
        SetCameraForward(dispatch, scene);
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

    // RunRaster がページの記録へ渡すキャッシュの入力（null なら従来どおりキャッシュを使わない）
    struct CacheInput
    {
        bool bEnabled = true;
        const float* Rects = nullptr;
        uint32_t RectCount = 0;
        bool bInvalidateAll = false;
    };

    struct RasterReadback
    {
        Container::VariableArray<uint32_t> Pool;
        Container::VariableArray<uint32_t> PageTable;
        Container::VariableArray<uint32_t> FreeList;
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
                   RasterReadback& readback,
                   const CacheInput* cache = nullptr,
                   const WideInput* wide = nullptr)
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
            SetCameraForward(pagesDispatch, scene);
            pagesDispatch.FovYDegrees = scene.Camera.FieldOfView;
            if (wide != nullptr)
            {
                pagesDispatch.SliceCount = wide->SliceCount;
                pagesDispatch.Slices = wide->Slices;
                pagesDispatch.MarkFirstSlice = wide->MarkFirstSlice;
            }
            // 従来のケースはキャッシュを使わない（毎フレームすべて割り当て直す）。キャッシュの入力があるときだけ使う
            pagesDispatch.bCacheEnabled = cache != nullptr && cache->bEnabled;
            if (cache != nullptr)
            {
                pagesDispatch.InvalidationRects = cache->Rects;
                pagesDispatch.InvalidationRectCount = cache->RectCount;
                pagesDispatch.bInvalidateAll = cache->bInvalidateAll;
            }
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
        if (wide != nullptr)
        {
            rasterDispatch.SliceCount = wide->SliceCount;
            rasterDispatch.Slices = wide->Slices;
        }
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
               ReadAll(resources.FreeList, readback.FreeList) && ReadAll(resources.Stats, readback.Stats) &&
               ReadAll(rasterBuffers.Draws, readback.Draws) && ReadAll(rasterBuffers.Instances, readback.Instances);
    }

    struct PageInfo
    {
        uint32_t Level = 0;
        int64_t AbsX = 0;
        int64_t AbsY = 0;
        uint32_t Physical = 0;
        bool bDirty = false;
        /** 展開が容量の溢れで描けなかった塊の範囲に付ける再描画の印 */
        bool bRetry = false;
    };

    // ページの表の割り当て済みの欄から、段・絶対のページ・物理ページを取り出す。
    // firstSlice は太陽の段 0 がスライスの表の何番目か（段 L はスライス firstSlice + L。それより前のスライスの欄は読まない）
    Container::VariableArray<PageInfo> DecodePages(const Scene& scene, const Container::VariableArray<uint32_t>& pageTable, uint32_t firstSlice = 0u)
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
            if (index / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL < firstSlice)
            {
                continue;
            }
            PageInfo page;
            page.Level = index / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL - firstSlice;
            const uint32_t address = index % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const int64_t addressY = address / VirtualShadowMap::TABLE_DIMENSION;
            const int64_t addressX = address % VirtualShadowMap::TABLE_DIMENSION;
            const VirtualShadowMapClipmapLevel& levelData = scene.Clipmap.Levels[page.Level];
            page.AbsX = levelData.OriginPageX + (((addressX - levelData.OriginPageX) % count) + count) % count;
            page.AbsY = levelData.OriginPageY + (((addressY - levelData.OriginPageY) % count) + count) % count;
            page.Physical = entry & VirtualShadowMap::PAGE_INDEX_MASK;
            page.bDirty = (entry & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u;
            page.bRetry = (entry & VirtualShadowMap::PAGE_ENTRY_RETRY) != 0u;
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
                    if (page.bDirty && page.Level == (instance[1] & 255u) && page.Physical == (instance[1] >> 8u) &&
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
        // ページの表の全語と、境界をまたぐ四角形のページ（段・左のページ A の絶対の座標）。ケース L が同じ場面を照明と同じ関数で読む
        Container::VariableArray<uint32_t> PageTable;
        uint32_t Level = 0;
        int64_t PageX = 0;
        int64_t PageY = 0;
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
            caseF.PageTable = readback.PageTable;
            caseF.Level = level;
            caseF.PageX = pageX;
            caseF.PageY = pageY;
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
                // 溢れた塊の範囲の dirty のページには再描画の印が付き（次フレームの引き継ぎが dirty を付け直す）、溢れなければ印は付かない
                uint32_t retryPages = 0;
                for (const PageInfo& page : pageInfos)
                {
                    retryPages += page.bRetry ? 1u : 0u;
                }
                Expect(retryPages == (instanceCapacity < needed ? needed : 0u),
                       "ケース H: 溢れた塊の範囲のページだけに再描画の印が付かなければならない（溢れなければ付かない）");
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
    //   6: 段 0 の範囲の最小のページから相対 (0..2, 0..2) のページを矩形が覆い、相対 (3, 3) のページだけが dirty（矩形のすぐ外）
    //      → 粗い mip のセルでは 1 つのセルに入るが、矩形の中に dirty のページは無いので、どの段でも何も選ばない
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
        constexpr uint32_t FixedInstanceCount = 6;
        constexpr uint32_t InstanceCount = FixedInstanceCount + 1; // 末尾の 1 つは範囲の原点からの相対の位置で決める（SpecOf）
        constexpr uint32_t GeometryPageNone = 0xFFFFFFFFu;

        struct InstanceSpec
        {
            double LightX;
            double LightY;
            double LightDepth;
            bool bCaster;
            float BoundsRadius;
        };

        const InstanceSpec Specs[FixedInstanceCount] = {
            {16.0, 16.0, 0.0, true, 6.0f},
            {3016.0, 16.0, 0.0, true, 6.0f},
            {16.0, 16.0, 0.0, false, 6.0f},
            {716.0, 16.0, 0.0, true, 6.0f},
            {716.0, 16.0, 0.0, true, 100.0f},
            {16.0, 16.0, 5000.0, true, 6.0f},
        };

        // 添字 index のインスタンスの仕様。最後のインスタンス（矩形のすぐ外にだけ dirty のページがある）は段 0 の範囲の原点のページから決める:
        // 相対のページ (0..2, 0..2) を覆う半径 40 m の球（ページ 32 m）
        InstanceSpec SpecOf(uint32_t index, const VirtualShadowMapClipmap& clipmap)
        {
            if (index < FixedInstanceCount)
            {
                return Specs[index];
            }
            const VirtualShadowMapClipmapLevel& first = clipmap.Levels[0];
            const double pageMeters = static_cast<double>(first.PageMeters);
            return {static_cast<double>(first.OriginPageX) * pageMeters + 1.5 * pageMeters, static_cast<double>(first.OriginPageY) * pageMeters + 1.5 * pageMeters,
                    0.0, true, 40.0f};
        }

        // 木の高さ（葉 = 0、根 = 3）
        uint32_t HeightOf(uint32_t index)
        {
            return index == 0u ? 3u : (index <= 2u ? 2u : (index <= 6u ? 1u : 0u));
        }

        // errorUnit は葉の誤差の 1/0.9（高さ h の誤差は 0.9 × errorUnit × 2^h）。既定は段 0 の texel の一辺
        void BuildClusters(Core::Rendering::MegaGeometry::GPUClusterData (&clusters)[ClusterCount], float errorUnit = FirstTexelMeters)
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
                cluster.LODError = 0.9f * errorUnit * static_cast<float>(1u << height);
                cluster.Flags = Mega::GPU_CLUSTER_FLAG_BAKED_LOD;
                if (index != 0u)
                {
                    const uint32_t parentHeight = HeightOf(parent);
                    cluster.ParentCenterX = (static_cast<float>(parent % 3u) - 1.0f) * 0.5f;
                    cluster.ParentCenterY = (static_cast<float>((parent / 3u) % 3u) - 1.0f) * 0.5f;
                    cluster.ParentRadius = 0.5f + 0.5f * static_cast<float>(parentHeight);
                    cluster.ParentError = 0.9f * errorUnit * static_cast<float>(1u << parentHeight);
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

        // 1 回の実行。listCapacity は出力の一覧の容量（クラスタの数）。
        // wideSliceCount が 0 でなければ、スライスを wideSliceCount 個にした合成の場面（先頭の LevelCount 段 + 先頭の段を繰り返した正射影のスライス。
        // スライス s は 段 s % LevelCount と同じ範囲・texel・dirty のページを持つ）。ページの表・dirty の階層はスライスの数から決める
        bool Run(const DevicePtr& device,
                 VirtualShadowMapMegaCull& cull,
                 const VirtualShadowMapClipmap& clipmap,
                 uint32_t listCapacity,
                 uint64_t frameSerial,
                 Outcome& outcome,
                 Container::VariableArray<uint32_t>& expectedDirtyBits,
                 bool bLeafPageResident = true,
                 uint32_t wideSliceCount = 0u)
        {
            namespace Mega = Core::Rendering::MegaGeometry;
            outcome = Outcome{};

            const bool bWide = wideSliceCount != 0u;
            // バッファが持つスライスの数と、ページの表へ書くスライスの数（広い場面はスライス全部、従来は段だけ）
            const uint32_t tableSlices = bWide ? wideSliceCount : VirtualShadowMap::LEVEL_COUNT;
            const uint32_t writtenSlices = bWide ? wideSliceCount : LevelCount;
            const ResourceUsage storage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            const BufferPtr pageTable = device->CreateBuffer(BufferDesc(VirtualShadowMap::PageTableBytes(tableSlices), storage, true, "VsmMegaTestPageTable"));
            const BufferPtr stats = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsBufferUsage() | ResourceUsage::ShaderRead, true, "VsmMegaTestStats"));
            const BufferPtr dirtyBits = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::MegaDirtyBitsBytes(tableSlices), VirtualShadowMap::MegaDirtyBitsUsage() | ResourceUsage::ShaderRead, true, "VsmMegaTestDirtyBits"));
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
                const InstanceSpec spec = SpecOf(index, clipmap);
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
            expectedDirtyBits.assign(VirtualShadowMap::MegaDirtyBitsBytes(tableSlices) / sizeof(uint32_t), 0u);
            {
                Container::VariableArray<uint32_t> table(VirtualShadowMap::PageTableBytes(tableSlices) / sizeof(uint32_t), 0u);
                uint32_t physical = 1u;
                // 段 level のページを書く。広い場面では、段 level を繰り返したスライス（level, level + LevelCount, …）すべてへ同じページを書く
                const auto writePage = [&](uint32_t level, int64_t pageX, int64_t pageY, bool bDirty) {
                    const uint32_t torusX = VirtualShadowMapPageTorusAddress(pageX, VirtualShadowMap::TABLE_DIMENSION);
                    const uint32_t torusY = VirtualShadowMapPageTorusAddress(pageY, VirtualShadowMap::TABLE_DIMENSION);
                    for (uint32_t slice = level; slice < writtenSlices; slice += LevelCount)
                    {
                        table[slice * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL + torusY * VirtualShadowMap::TABLE_DIMENSION + torusX] =
                            VirtualShadowMap::PAGE_ENTRY_ALLOCATED | (bDirty ? VirtualShadowMap::PAGE_ENTRY_DIRTY : 0u) | (physical++);
                        if (bDirty)
                        {
                            const VirtualShadowMapClipmapLevel& data = clipmap.Levels[level];
                            SetExpectedBits(expectedDirtyBits, slice, pageX - data.OriginPageX, pageY - data.OriginPageY);
                        }
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
                    // インスタンス 6 の矩形（範囲の原点から相対 (0..2, 0..2)）のすぐ外の dirty のページ（相対 (3, 3)）。粗い mip のセルは矩形と共有する
                    const VirtualShadowMapClipmapLevel& first = clipmap.Levels[0];
                    writePage(0u, static_cast<int64_t>(first.OriginPageX) + 3, static_cast<int64_t>(first.OriginPageY) + 3, true);
                }
                void* mapped = pageTable->Map(0u, VirtualShadowMap::PageTableBytes(tableSlices));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, table.data(), VirtualShadowMap::PageTableBytes(tableSlices));
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
            // 広い場面は、外から渡すスライスの表（先頭の段 + 繰り返したスライス）で動かす
            GPUVsmSlice wideSlices[VirtualShadowMapMaxSlices];
            if (bWide)
            {
                VirtualShadowMapWideTest::BuildWideSlices(clipmap, wideSliceCount, wideSlices);
                dispatch.SliceCount = wideSliceCount;
                dispatch.Slices = wideSlices;
            }

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

        // 期待する（インスタンス、段、クラスタ）の一覧（昇順）。段 level のクラスタは高さ level のもの。
        // sliceCount が 0 でなければ広い場面で、スライス s は 段 s % LevelCount と同じクラスタを選ぶ（一覧の段の欄はスライスの番号）
        Container::VariableArray<uint64_t> ExpectedEntries(bool bLeafPageResident = true, uint32_t sliceCount = 0u)
        {
            Container::VariableArray<uint64_t> expected;
            const auto add = [&expected, bLeafPageResident](uint32_t instance, uint32_t slice) {
                Container::VariableArray<uint32_t> clusters;
                ExpectedClusters(slice % LevelCount, clusters, bLeafPageResident);
                for (const uint32_t cluster : clusters)
                {
                    expected.push_back((static_cast<uint64_t>(instance) << 40) | (static_cast<uint64_t>(slice) << 32) | cluster);
                }
            };
            for (uint32_t slice = 0; slice < (sliceCount != 0u ? sliceCount : LevelCount); ++slice)
            {
                add(0u, slice); // インスタンス 0: 全段
                if (slice % LevelCount >= 1u)
                {
                    add(1u, slice); // インスタンス 1: 段 0 の範囲の外
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

    // ========================================
    // ケース J5: 点光源の面（透視のスライス）の MegaGeometry の投影物のカリング
    // ========================================
    //
    // 1 灯（位置 (3, 4, 5)・Range 50）の 6 面 × 6 段 = 36 スライスを、太陽なし（クリップマップ無効）の外から渡すスライスの表で、本番のカリングに通す。
    // ケース J と同じ完全二分木のクラスタ（誤差は高さごとに 2 倍。単位は PointMegaCull::ErrorUnit）を、面 0（+X）に置く。
    // 選ばれるのは「自分の誤差 ÷ texel ≤ 1 かつ親の誤差 ÷ texel > 1」のクラスタで、texel は球の最も近い点の面の軸の距離 z での 2z ÷ 段の解像度。
    // 光源から遠いほど、また段が粗いほど粗いクラスタ（葉 8・中間 4・2・根 1）が選ばれ、どの葉から根への道でもちょうど 1 つが選ばれる。
    //   0: 軸の上 z=12 m → 面 0 の 6 段で選ぶ（段 0 は葉 8、段 1 は高さ 1 の 4、段 2 は 2、段 3〜5 は根 1）
    //   1: 軸の上 z=40 m → 面 0 の 6 段で選ぶ（段 0 は高さ 2 の 2、段 1〜5 は根 1）
    //   2: z=12 m・面の座標で横に 7 m（NDC 0.58）→ 中央の dirty のページと重ならない細かい段では何も選ばず、ページが大きい段 3〜5 では根を選ぶ
    //   3: z=60 m・Range(50) の外 → 何も選ばない
    //   4: 影を落とさない → 何も選ばない
    // 面 0 の各段には、中央（NDC ±0.1 に触れるページ）と隅（最後のページ）に dirty のページを置く。面 1〜5 は表が空で、何も選ばない。
    // 期待は倍精度の参照（ページの範囲・LOD）で、単精度の GPU との差で変わりうる境（ページの境・LOD の比が 1 の近く）は参照が曖昧と判定し、
    // 場面を直すまでテストを失敗させる。dirty の階層は一辺が面の段のページ数（32〜1）の表を、CPU の参照と全語一致する。
    namespace PointMegaCull
    {
        namespace Mega = Core::Rendering::MegaGeometry;

        constexpr uint32_t InstanceCount = 5;
        constexpr uint32_t SliceCount = 36;
        constexpr float ErrorUnit = 0.004f;
        constexpr double LightPosition[3] = {3.0, 4.0, 5.0};
        constexpr float LightRange = 50.0f;
        constexpr double BoundsRadius = 3.0;
        // LOD の比（誤差 ÷ texel）の自然対数の絶対値がこれ未満なら、単精度の GPU が別の選び方をしうるので曖昧とする
        constexpr double LodAmbiguity = 0.1;
        // ページの範囲の判定の余裕（面の NDC）を GPU の 1e-4 から縮めた場合と広げた場合で結果が変わるなら曖昧とする
        constexpr double GpuPageMargin = 1.0e-4;
        constexpr double PageAmbiguity = 2.0e-3;

        struct Spec
        {
            double Axial;
            double Sc;
            double Tc;
            bool bCaster;
        };

        const Spec Specs[InstanceCount] = {
            {12.0, 0.0, 0.0, true},
            {40.0, 0.0, 0.0, true},
            {12.0, 7.0, 0.0, true},
            {60.0, 0.0, 0.0, true},
            {12.0, 0.0, 0.0, false},
        };

        // スライスの行（ワールドの位置 → 面の座標）と、LOD・ページの範囲に要る値（倍精度）
        struct SliceView
        {
            double X[4] = {};
            double Y[4] = {};
            double Z[4] = {};
            double TexelNdc = 0.0;
            double Range = 0.0;
            double NearPlane = 0.0;
            int32_t Pages = 0;
        };

        SliceView ViewOf(const GPUVsmSlice& slice)
        {
            SliceView view;
            for (uint32_t index = 0; index < 4u; ++index)
            {
                view.X[index] = slice.axisX[index];
                view.Y[index] = slice.axisY[index];
                view.Z[index] = slice.axisZ[index];
            }
            view.TexelNdc = slice.info[1];
            view.Range = slice.info[2];
            view.NearPlane = slice.info[3];
            view.Pages = slice.origin[3];
            return view;
        }

        void ToFace(const SliceView& view, const double (&position)[3], double (&face)[3])
        {
            face[0] = view.X[0] * position[0] + view.X[1] * position[1] + view.X[2] * position[2] + view.X[3];
            face[1] = view.Y[0] * position[0] + view.Y[1] * position[1] + view.Y[2] * position[2] + view.Y[3];
            face[2] = view.Z[0] * position[0] + view.Z[1] * position[1] + view.Z[2] * position[2] + view.Z[3];
        }

        struct Rect
        {
            bool bValid = false;
            int32_t X0 = 0;
            int32_t Y0 = 0;
            int32_t X1 = -1;
            int32_t Y1 = -1;
        };

        // vsm_expand.comp・vsm_mega_cull.comp の VsmPerspectivePageRange と同じ手順（球が覆う面のページの矩形）
        Rect PageRect(const SliceView& view, const double (&center)[3], double radius, double margin)
        {
            Rect rect;
            double c[3] = {};
            ToFace(view, center, c);
            const double reach = view.Range + radius;
            if (!(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] <= reach * reach))
            {
                return rect;
            }
            if (c[2] + radius < view.NearPlane || c[2] - radius > view.Range)
            {
                return rect;
            }
            const double side = radius * 1.4142136;
            if (c[0] - c[2] > side || -c[0] - c[2] > side || c[1] - c[2] > side || -c[1] - c[2] > side)
            {
                return rect;
            }
            const int32_t pages = view.Pages;
            if (c[2] - radius <= view.NearPlane)
            {
                rect = {true, 0, 0, pages - 1, pages - 1};
                return rect;
            }
            int32_t low[2] = {};
            int32_t high[2] = {};
            for (uint32_t axis = 0; axis < 2u; ++axis)
            {
                const double denominator = c[2] * c[2] - radius * radius;
                const double centerNdc = c[axis] * c[2];
                const double spread = radius * std::sqrt(std::max(c[axis] * c[axis] + denominator, 0.0));
                const double ndcLow = (centerNdc - spread) / denominator;
                const double ndcHigh = (centerNdc + spread) / denominator;
                low[axis] = std::max(static_cast<int32_t>(std::floor((ndcLow - margin) * 0.5 * pages + 0.5 * pages)), 0);
                high[axis] = std::min(static_cast<int32_t>(std::floor((ndcHigh + margin) * 0.5 * pages + 0.5 * pages)), pages - 1);
            }
            rect = {high[0] >= low[0] && high[1] >= low[1], low[0], low[1], high[0], high[1]};
            return rect;
        }

        // 面 0 の段 mip（一辺 pages ページ）の dirty のページ: 中央（NDC ±0.1 に触れるページ）と、最後のページ（隅）
        bool IsDirtyPage(int32_t pages, int32_t x, int32_t y)
        {
            const int32_t first = 45 * pages / 100;
            const int32_t last = 55 * pages / 100;
            return (x >= first && x <= last && y >= first && y <= last) || (x == pages - 1 && y == pages - 1);
        }

        bool RectHasDirty(const Rect& rect, uint32_t slice, int32_t pages)
        {
            if (!rect.bValid || slice >= 6u)
            {
                return false;
            }
            for (int32_t y = rect.Y0; y <= rect.Y1; ++y)
            {
                for (int32_t x = rect.X0; x <= rect.X1; ++x)
                {
                    if (IsDirtyPage(pages, x, y))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // 球が覆うページに dirty のページがあるか。余裕を縮めた場合と広げた場合で答えが変われば bAmbiguous
        bool SphereHasDirty(const SliceView& view, uint32_t slice, const double (&center)[3], double radius, bool& bAmbiguous)
        {
            const bool base = RectHasDirty(PageRect(view, center, radius, GpuPageMargin), slice, view.Pages);
            const bool shrunk = RectHasDirty(PageRect(view, center, radius, GpuPageMargin - PageAmbiguity), slice, view.Pages);
            const bool widened = RectHasDirty(PageRect(view, center, radius, GpuPageMargin + PageAmbiguity), slice, view.Pages);
            bAmbiguous = bAmbiguous || base != shrunk || base != widened;
            return base;
        }

        // 球の最も近い点の面の軸の距離での texel の一辺（m）
        double TexelMeters(const SliceView& view, const double (&center)[3], double radius)
        {
            double face[3] = {};
            ToFace(view, center, face);
            return view.TexelNdc * std::max(face[2] - radius, view.NearPlane);
        }

        // クラスタを選ぶか（Common/MegaGeometryCull.glsl の ShouldDrawBakedCluster。ページはすべて常駐）。minLogRatio に比の曖昧さを残す
        bool ClusterSelected(const SliceView& view, const double (&instance)[3], const Mega::GPUClusterData& cluster, double& minLogRatio)
        {
            const double center[3] = {instance[0] + cluster.BoundsCenterX, instance[1] + cluster.BoundsCenterY, instance[2] + cluster.BoundsCenterZ};
            const double selfRatio = static_cast<double>(cluster.LODError) / TexelMeters(view, center, cluster.BoundsRadius);
            minLogRatio = std::min(minLogRatio, std::abs(std::log(selfRatio)));
            if (selfRatio > 1.0)
            {
                return false;
            }
            if (cluster.GroupId == 0xFFFFFFFFu)
            {
                return true;
            }
            const double parent[3] = {instance[0] + cluster.ParentCenterX, instance[1] + cluster.ParentCenterY, instance[2] + cluster.ParentCenterZ};
            const double parentRatio = static_cast<double>(cluster.ParentError) / TexelMeters(view, parent, cluster.ParentRadius);
            minLogRatio = std::min(minLogRatio, std::abs(std::log(parentRatio)));
            return !(parentRatio <= 1.0);
        }

        // 期待する（インスタンス、スライス、クラスタ）の一覧（昇順）と、判定を通った（インスタンス、スライス）の数。曖昧な場面は bAmbiguous
        void BuildExpected(const GPUVsmSlice (&slices)[SliceCount],
                           const double (&positions)[InstanceCount][3],
                           Container::VariableArray<uint64_t>& expected,
                           uint32_t& instanceSlices,
                           bool& bAmbiguous)
        {
            Mega::GPUClusterData clusters[MegaCull::ClusterCount];
            MegaCull::BuildClusters(clusters, ErrorUnit);
            expected.clear();
            instanceSlices = 0;
            double minLogRatio = 1.0e9;
            for (uint32_t instance = 0; instance < InstanceCount; ++instance)
            {
                if (!Specs[instance].bCaster)
                {
                    continue;
                }
                for (uint32_t slice = 0; slice < SliceCount; ++slice)
                {
                    const SliceView view = ViewOf(slices[slice]);
                    if (!SphereHasDirty(view, slice, positions[instance], BoundsRadius, bAmbiguous))
                    {
                        continue;
                    }
                    ++instanceSlices;
                    for (uint32_t index = 0; index < MegaCull::ClusterCount; ++index)
                    {
                        const Mega::GPUClusterData& cluster = clusters[index];
                        const double center[3] = {positions[instance][0] + cluster.BoundsCenterX,
                                                  positions[instance][1] + cluster.BoundsCenterY,
                                                  positions[instance][2] + cluster.BoundsCenterZ};
                        if (SphereHasDirty(view, slice, center, cluster.BoundsRadius, bAmbiguous) &&
                            ClusterSelected(view, positions[instance], cluster, minLogRatio))
                        {
                            expected.push_back((static_cast<uint64_t>(instance) << 40) | (static_cast<uint64_t>(slice) << 32) | index);
                        }
                    }
                }
            }
            bAmbiguous = bAmbiguous || minLogRatio < LodAmbiguity;
            std::sort(expected.begin(), expected.end());
        }

        struct Outcome
        {
            bool bRecorded = false;
            uint32_t Selected = 0;
            uint32_t Overflow = 0;
            uint32_t InstanceSlices = 0;
            uint32_t StatInstances = 0;
            uint32_t StatClusters = 0;
            uint32_t StatPointInstances = 0;
            uint32_t StatPointClusters = 0;
            Container::VariableArray<uint32_t> Entries;
            Container::VariableArray<uint32_t> DirtyBits;
            Container::VariableArray<uint32_t> PageTable;
        };

        // 1 回の実行。listCapacity は出力の一覧の容量（クラスタの数）
        bool Run(const DevicePtr& device,
                 VirtualShadowMapMegaCull& cull,
                 const GPUVsmSlice (&slices)[SliceCount],
                 const double (&positions)[InstanceCount][3],
                 uint32_t listCapacity,
                 uint64_t frameSerial,
                 Outcome& outcome,
                 Container::VariableArray<uint32_t>& expectedDirtyBits)
        {
            outcome = Outcome{};
            const ResourceUsage storage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            const BufferPtr pageTable = device->CreateBuffer(BufferDesc(VirtualShadowMap::PageTableBytes(SliceCount), storage, true, "VsmPointMegaTestPageTable"));
            const BufferPtr stats = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsBufferUsage() | ResourceUsage::ShaderRead, true, "VsmPointMegaTestStats"));
            const BufferPtr dirtyBits = device->CreateBuffer(BufferDesc(
                VirtualShadowMap::MegaDirtyBitsBytes(SliceCount), VirtualShadowMap::MegaDirtyBitsUsage() | ResourceUsage::ShaderRead, true, "VsmPointMegaTestDirtyBits"));
            const BufferPtr list = device->CreateBuffer(BufferDesc(
                VirtualShadowMap::MegaCullListBytes(listCapacity), VirtualShadowMap::MegaCullListUsage() | ResourceUsage::ShaderRead, true, "VsmPointMegaTestList"));
            const BufferPtr clusters = device->CreateBuffer(BufferDesc(
                sizeof(Mega::GPUClusterData) * MegaCull::ClusterCount, ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress, true, "VsmPointMegaTestClusters"));
            const BufferPtr instances = device->CreateBuffer(
                BufferDesc(sizeof(MegaCull::TestInstance) * InstanceCount, ResourceUsage::StorageBuffer, true, "VsmPointMegaTestInstances"));
            const BufferPtr shadowInstances = device->CreateBuffer(
                BufferDesc(sizeof(MegaGeometryShadowInstance) * InstanceCount, ResourceUsage::StorageBuffer, true, "VsmPointMegaTestShadowInstances"));
            const BufferPtr geometryPages = device->CreateBuffer(
                BufferDesc(sizeof(Mega::GeometryPageTable::Entry) * 4u, ResourceUsage::StorageBuffer, true, "VsmPointMegaTestGeometryPages"));
            if (!pageTable || !stats || !dirtyBits || !list || !clusters || !instances || !shadowInstances || !geometryPages)
            {
                return false;
            }
            const uint64_t clusterAddress = clusters->GetDeviceAddress();
            if (clusterAddress == 0u)
            {
                return false;
            }
            const auto upload = [](const BufferPtr& buffer, const void* data, uint64_t bytes) {
                void* mapped = buffer->Map(0u, bytes);
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memcpy(mapped, data, static_cast<size_t>(bytes));
                buffer->Unmap();
                return true;
            };

            Mega::GPUClusterData gpuClusters[MegaCull::ClusterCount];
            MegaCull::BuildClusters(gpuClusters, ErrorUnit);
            Mega::GeometryPageTable::Entry geometryEntries[4] = {}; // すべて常駐
            if (!upload(clusters, gpuClusters, sizeof(gpuClusters)) || !upload(geometryPages, geometryEntries, sizeof(geometryEntries)))
            {
                return false;
            }

            MegaCull::TestInstance instanceTable[InstanceCount] = {};
            MegaGeometryShadowInstance shadowTable[InstanceCount] = {};
            uint32_t totalGroups = 0;
            for (uint32_t index = 0; index < InstanceCount; ++index)
            {
                MegaCull::TestInstance& instance = instanceTable[index];
                for (uint32_t axis = 0; axis < 4u; ++axis)
                {
                    instance.World[axis * 4u + axis] = 1.0f;
                    instance.PreviousWorld[axis * 4u + axis] = 1.0f;
                }
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    instance.World[12 + axis] = static_cast<float>(positions[index][axis]);
                    instance.PreviousWorld[12 + axis] = static_cast<float>(positions[index][axis]);
                }
                instance.ClusterInfo[0] = static_cast<uint32_t>(clusterAddress & 0xFFFFFFFFull);
                instance.ClusterInfo[1] = static_cast<uint32_t>(clusterAddress >> 32);
                instance.ClusterInfo[2] = MegaCull::ClusterCount;

                MegaGeometryShadowInstance& shadow = shadowTable[index];
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    shadow.BoundsSphere[axis] = static_cast<float>(positions[index][axis]);
                }
                shadow.BoundsSphere[3] = static_cast<float>(BoundsRadius);
                shadow.FirstGroup = totalGroups;
                if (Specs[index].bCaster)
                {
                    shadow.Flags = MegaGeometryShadowFlagCaster | MegaGeometryShadowFlagBounds;
                    totalGroups += 1u; // 15 クラスタ = 1 ワークグループ
                }
            }
            if (!upload(instances, instanceTable, sizeof(instanceTable)) || !upload(shadowInstances, shadowTable, sizeof(shadowTable)))
            {
                return false;
            }

            // VSM のページの表: 面 0 の各段（スライス 0〜5。一辺 32 >> 段 ページ）の dirty のページだけを書く。ほかは 0
            expectedDirtyBits.assign(VirtualShadowMap::MegaDirtyBitsBytes(SliceCount) / sizeof(uint32_t), 0u);
            {
                Container::VariableArray<uint32_t> table(VirtualShadowMap::PageTableBytes(SliceCount) / sizeof(uint32_t), 0u);
                uint32_t physical = 1u;
                for (uint32_t slice = 0; slice < 6u; ++slice)
                {
                    const int32_t pages = static_cast<int32_t>(32u >> slice);
                    for (int32_t y = 0; y < pages; ++y)
                    {
                        for (int32_t x = 0; x < pages; ++x)
                        {
                            if (IsDirtyPage(pages, x, y))
                            {
                                table[slice * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL + y * pages + x] =
                                    VirtualShadowMap::PAGE_ENTRY_ALLOCATED | VirtualShadowMap::PAGE_ENTRY_DIRTY | (physical++);
                                MegaCull::SetExpectedBits(expectedDirtyBits, slice, x, y);
                            }
                        }
                    }
                }
                if (!upload(pageTable, table.data(), VirtualShadowMap::PageTableBytes(SliceCount)))
                {
                    return false;
                }
            }
            // 書かれたかを確かめるため、階層・一覧は見張りの値で埋める（記録が 0 にする）。統計は呼ぶ前に 0
            for (const BufferPtr& buffer : {dirtyBits, list})
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
                uint32_t* mapped = static_cast<uint32_t*>(stats->Map(0u, stats->GetSize()));
                if (mapped == nullptr)
                {
                    return false;
                }
                std::memset(mapped, 0, static_cast<size_t>(stats->GetSize()));
                stats->Unmap();
            }

            // 太陽なし（クリップマップ無効）。点光源のスライスの表だけを外から渡す
            const VirtualShadowMapClipmap noSun{};
            VirtualShadowMapMegaCullDispatch dispatch;
            dispatch.Clipmap = &noSun;
            dispatch.SliceCount = SliceCount;
            dispatch.Slices = slices;
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
                outcome.InstanceSlices = words[2];
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
                outcome.StatPointInstances = words[VirtualShadowMap::StatMegaPointInstances];
                outcome.StatPointClusters = words[VirtualShadowMap::StatMegaPointClusters];
                stats->Unmap();
            }
            return ReadAll(dirtyBits, outcome.DirtyBits) && ReadAll(pageTable, outcome.PageTable);
        }

        // 選んだクラスタの高さ（インスタンス・スライスの組で 1 つの値だけ。なければ -1、2 種類以上なら -2）
        int32_t SelectedHeight(const Container::VariableArray<uint64_t>& packed, uint32_t instance, uint32_t slice)
        {
            int32_t height = -1;
            for (const uint64_t entry : packed)
            {
                if (static_cast<uint32_t>(entry >> 40) == instance && static_cast<uint32_t>((entry >> 32) & 0xFFu) == slice)
                {
                    const int32_t value = static_cast<int32_t>(MegaCull::HeightOf(static_cast<uint32_t>(entry & 0xFFFFFFFFu)));
                    height = height == -1 || height == value ? value : -2;
                }
            }
            return height;
        }

        uint32_t SelectedCount(const Container::VariableArray<uint64_t>& packed, uint32_t instance, uint32_t slice)
        {
            uint32_t count = 0;
            for (const uint64_t entry : packed)
            {
                count += static_cast<uint32_t>(entry >> 40) == instance && static_cast<uint32_t>((entry >> 32) & 0xFFu) == slice ? 1u : 0u;
            }
            return count;
        }
    } // namespace PointMegaCull

    // ケース J5 を実行する。実行できなければ false
    bool RunPointMegaCullCases(const DevicePtr& device, VirtualShadowMapMegaCull& cull, uint64_t& frameSerial)
    {
        namespace Point = PointMegaCull;
        PointShadowSnapshot snapshot;
        snapshot.LightCount = 1u;
        snapshot.Lights[0].LightId = 1u;
        snapshot.Lights[0].Position = Math::Vector3(static_cast<float>(Point::LightPosition[0]),
                                                    static_cast<float>(Point::LightPosition[1]),
                                                    static_cast<float>(Point::LightPosition[2]));
        snapshot.Lights[0].Range = Point::LightRange;
        const VirtualShadowMapPointLights lights = BuildVirtualShadowMapPointLights(snapshot, VirtualShadowMapPointSettings{}, 0u);
        Expect(lights.LightCount == 1u && lights.SliceCount() == Point::SliceCount, "ケース J5: 1 灯 × 6 面 × 6 段 = 36 スライスでなければならない");
        if (lights.SliceCount() != Point::SliceCount)
        {
            return false;
        }
        GPUVsmSlice slices[Point::SliceCount] = {};
        BuildVirtualShadowMapPointSlices(lights, slices);

        // インスタンスの位置: 光源から、面 0 の軸の向きへ Axial、面の接線方向へ Sc・Tc
        double positions[Point::InstanceCount][3] = {};
        for (uint32_t index = 0; index < Point::InstanceCount; ++index)
        {
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                positions[index][axis] = Point::LightPosition[axis] + static_cast<double>(slices[0].axisZ[axis]) * Point::Specs[index].Axial +
                                         static_cast<double>(slices[0].axisX[axis]) * Point::Specs[index].Sc +
                                         static_cast<double>(slices[0].axisY[axis]) * Point::Specs[index].Tc;
            }
        }

        Container::VariableArray<uint64_t> expected;
        uint32_t expectedInstanceSlices = 0;
        bool bAmbiguous = false;
        Point::BuildExpected(slices, positions, expected, expectedInstanceSlices, bAmbiguous);
        Expect(!bAmbiguous, "ケース J5: 期待に曖昧な境（ページの境・LOD の比が 1 の近く）があってはならない（場面を直すこと）");

        // ----- J5-1: 容量が十分 -----
        {
            Point::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!Point::Run(device, cull, slices, positions, 4096u, ++frameSerial, outcome, expectedBits))
            {
                std::cerr << TestName << " ケース J5-1 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J5-1: 太陽が無くても、外から渡した点光源のスライスの表でカリングを記録しなければならない");
            if (!outcome.bRecorded)
            {
                return true;
            }
            Expect(outcome.DirtyBits.size() == expectedBits.size(), "ケース J5-1: dirty の階層の大きさがスライスの数で決まらなければならない");
            uint32_t differentWords = 0;
            for (size_t word = 0; word < std::min(outcome.DirtyBits.size(), expectedBits.size()); ++word)
            {
                differentWords += outcome.DirtyBits[word] != expectedBits[word] ? 1u : 0u;
            }
            Expect(differentWords == 0u, "ケース J5-1: 面のページの表（一辺 32〜1）の dirty の階層が CPU の参照と全語一致しなければならない");

            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(outcome.Selected == expected.size() && outcome.Overflow == 0u, "ケース J5-1: 選んだクラスタの数が参照と一致し、溢れてはならない");
            Expect(outcome.InstanceSlices == expectedInstanceSlices, "ケース J5-1: 判定を通った（インスタンス、スライス）の数が参照と一致しなければならない");
            Expect(actual == expected, "ケース J5-1: 選んだ（インスタンス、スライス、クラスタ）が参照の集合と一致しなければならない");
            Expect(outcome.StatInstances == expectedInstanceSlices && outcome.StatPointInstances == expectedInstanceSlices,
                   "ケース J5-1: 統計の（インスタンス、スライス）は全部が点光源の分で、合計と点光源の分が一致しなければならない");
            Expect(outcome.StatClusters == expected.size() && outcome.StatPointClusters == expected.size(),
                   "ケース J5-1: 統計のクラスタは全部が点光源の分で、合計と点光源の分が一致しなければならない");

            // 光源から遠いほど、段が粗いほど粗いクラスタ: 面 0 の段 0〜5 で、インスタンス 0（12 m）は葉 8・4・2・根 1、インスタンス 1（40 m）は 2・根 1
            const int32_t nearHeights[6] = {0, 1, 2, 3, 3, 3};
            const uint32_t nearCounts[6] = {8, 4, 2, 1, 1, 1};
            const int32_t farHeights[6] = {2, 3, 3, 3, 3, 3};
            const uint32_t farCounts[6] = {2, 1, 1, 1, 1, 1};
            for (uint32_t mip = 0; mip < 6u; ++mip)
            {
                Expect(Point::SelectedHeight(actual, 0u, mip) == nearHeights[mip] && Point::SelectedCount(actual, 0u, mip) == nearCounts[mip],
                       "ケース J5-1: インスタンス 0（光源から 12 m）の選ぶクラスタの高さと数が、段に応じて粗くならなければならない");
                Expect(Point::SelectedHeight(actual, 1u, mip) == farHeights[mip] && Point::SelectedCount(actual, 1u, mip) == farCounts[mip],
                       "ケース J5-1: インスタンス 1（光源から 40 m）の選ぶクラスタの高さと数が、同じ段でより粗くならなければならない");
                if (mip > 0u)
                {
                    Expect(Point::SelectedHeight(actual, 0u, mip) >= Point::SelectedHeight(actual, 0u, mip - 1u), "ケース J5-1: 段が粗いほど選ぶ高さが下がってはならない");
                }
                Expect(Point::SelectedHeight(actual, 1u, mip) >= Point::SelectedHeight(actual, 0u, mip), "ケース J5-1: 光源から遠いほど選ぶ高さが下がってはならない");
                Expect(MegaCull::IsSingleCut(actual, 0u, mip) && MegaCull::IsSingleCut(actual, 1u, mip),
                       "ケース J5-1: 選んだクラスタがどの葉から根への道でもちょうど 1 つ（一つの切り口）でなければならない");
            }
            // 面 0 以外・Range の外（3）・影を落とさない（4）は何も選ばない。インスタンス 2 は dirty のページと重ならない細かい段で何も選ばない
            uint32_t strayEntries = 0;
            for (const uint64_t entry : actual)
            {
                const uint32_t instance = static_cast<uint32_t>(entry >> 40);
                const uint32_t slice = static_cast<uint32_t>((entry >> 32) & 0xFFu);
                strayEntries += slice >= 6u || instance >= 3u ? 1u : 0u;
            }
            Expect(strayEntries == 0u, "ケース J5-1: 面 0 以外のスライス・Range の外・影を落とさないインスタンスは何も選んではならない");
            for (uint32_t mip = 0; mip < 3u; ++mip)
            {
                Expect(Point::SelectedCount(actual, 2u, mip) == 0u, "ケース J5-1: インスタンス 2 は中央の dirty のページと重ならない細かい段で何も選んではならない");
            }
            Expect(Point::SelectedCount(actual, 2u, 5u) == 1u, "ケース J5-1: インスタンス 2 は 1 ページの段（5）では根を選ばなければならない");
            std::cout << TestName << " ケース J5-1: 選んだクラスタ=" << outcome.Selected << " 通った（インスタンス、スライス）=" << outcome.InstanceSlices
                      << " 点光源の統計=(" << outcome.StatPointInstances << "," << outcome.StatPointClusters << ")" << std::endl;
        }

        // ----- J5-2: 出力の一覧の容量が 5（溢れる）。落としたクラスタの範囲の、割り当て済みで dirty のページに再描画の印を付ける -----
        {
            Point::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!Point::Run(device, cull, slices, positions, 5u, ++frameSerial, outcome, expectedBits))
            {
                std::cerr << TestName << " ケース J5-2 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J5-2: カリングを記録しなければならない");
            if (!outcome.bRecorded)
            {
                return true;
            }
            Expect(outcome.Selected == expected.size() && outcome.Overflow == expected.size() - 5u, "ケース J5-2: 選んだ数は参照と同じで、溢れは容量を超えた分でなければならない");
            Expect(outcome.StatClusters == 5u && outcome.StatPointClusters == 5u, "ケース J5-2: 統計のクラスタは書いた 5 件でなければならない");
            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(actual.size() == 5u, "ケース J5-2: 容量ぶん（5 件）だけ書かなければならない");
            for (const uint64_t entry : actual)
            {
                Expect(std::find(expected.begin(), expected.end(), entry) != expected.end(), "ケース J5-2: 書いた件は参照の集合に入らなければならない");
            }
            uint32_t retryPages = 0;
            uint32_t badRetryPages = 0;
            for (size_t index = 0; index < outcome.PageTable.size(); ++index)
            {
                const uint32_t entry = outcome.PageTable[index];
                if ((entry & VirtualShadowMap::PAGE_ENTRY_RETRY) != 0u)
                {
                    ++retryPages;
                    const bool bDrawable = (entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) != 0u && (entry & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u;
                    // 面 0 の段 0〜5 の表（スライス 0〜5）の内側だけ
                    badRetryPages += !bDrawable || index >= 6u * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL ? 1u : 0u;
                }
            }
            Expect(retryPages > 0u && badRetryPages == 0u, "ケース J5-2: 溢れたクラスタの範囲の、割り当て済みで dirty の面のページにだけ再描画の印が付かなければならない");
            std::cout << TestName << " ケース J5-2: 選んだ=" << outcome.Selected << " 溢れ=" << outcome.Overflow << " 再描画の印のページ=" << retryPages << std::endl;
        }
        return true;
    }

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
            Expect(outcome.GroupCount == 6u, "ケース J1: 影を落とす 6 インスタンスぶんの 6 ワークグループを出さなければならない");
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
            Expect(outcome.InstanceLevels == 8u,
                   "ケース J1: 判定を通った（インスタンス、段）は 8 でなければならない（矩形のすぐ外にだけ dirty のページがあるインスタンス 6 は通らない）");
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

        // ----- J4: スライスが 40 個 -----
        // 先頭の 4 段の後ろに、先頭の段を繰り返した正射影のスライス 36 個を置く（ページの表の先頭は連続する番地）。
        // dirty の階層の大きさ・ページの表の大きさ・カリングの dispatch がスライスの数で決まり、後ろのスライスも先頭の段と同じ結果になる:
        // 階層は CPU の参照と全語一致、判定を通った（インスタンス、スライス）は 8 × 10、選んだクラスタは 22 × 10
        {
            constexpr uint32_t wideSlices = 40u;
            MegaCull::Outcome outcome;
            Container::VariableArray<uint32_t> expectedBits;
            if (!MegaCull::Run(device, cull, clipmap, 4096u, frameSerial++, outcome, expectedBits, true, wideSlices))
            {
                std::cerr << TestName << " ケース J4 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(outcome.bRecorded, "ケース J4: カリングを記録しなければならない");
            Expect(outcome.GroupCount == 6u, "ケース J4: 影を落とす 6 インスタンスぶんの 6 ワークグループを出さなければならない");
            Expect(outcome.DirtyBits.size() == expectedBits.size() &&
                       expectedBits.size() == VirtualShadowMap::MegaDirtyBitsBytes(wideSlices) / sizeof(uint32_t),
                   "ケース J4: dirty の階層の大きさがスライスの数で決まらなければならない");
            uint32_t differentWords = 0;
            for (size_t word = 0; word < std::min(outcome.DirtyBits.size(), expectedBits.size()); ++word)
            {
                differentWords += outcome.DirtyBits[word] != expectedBits[word] ? 1u : 0u;
            }
            Expect(differentWords == 0u, "ケース J4: 後ろのスライスの dirty の階層も CPU の参照と一致しなければならない");
            const Container::VariableArray<uint64_t> expected = MegaCull::ExpectedEntries(true, wideSlices);
            const Container::VariableArray<uint64_t> actual = MegaCull::PackEntries(outcome.Entries);
            Expect(outcome.Selected == expected.size() && outcome.Selected == 220u, "ケース J4: 選んだクラスタは 22 × 10 = 220 件でなければならない");
            Expect(outcome.Overflow == 0u, "ケース J4: 溢れてはならない");
            Expect(outcome.InstanceLevels == 80u, "ケース J4: 判定を通った（インスタンス、スライス）は 8 × 10 = 80 でなければならない");
            Expect(outcome.StatInstances == 80u && outcome.StatClusters == 220u && outcome.StatOverflow == 0u,
                   "ケース J4: 統計の語 8〜10 が（80, 220, 0）でなければならない");
            Expect(actual == expected, "ケース J4: 選んだ（インスタンス、スライス、クラスタ）が期待の集合と一致しなければならない");
            // 後ろのスライスは、先頭の段と同じクラスタを選ぶ（一覧の段の欄だけがスライスの番号）
            uint32_t differentFromBase = 0;
            for (const uint64_t entry : actual)
            {
                const uint32_t slice = static_cast<uint32_t>((entry >> 32) & 0xFFu);
                const uint64_t baseEntry = (entry & ~(0xFFull << 32)) | (static_cast<uint64_t>(slice % MegaCull::LevelCount) << 32);
                differentFromBase += std::find(actual.begin(), actual.end(), baseEntry) == actual.end() ? 1u : 0u;
            }
            Expect(differentFromBase == 0u, "ケース J4: 後ろのスライスは先頭の段と同じクラスタを選ばなければならない");
            for (uint32_t slice = 0; slice < wideSlices; ++slice)
            {
                Expect(MegaCull::IsSingleCut(actual, 0u, slice), "ケース J4: インスタンス 0 の選択がどのスライスでも一つの切り口でなければならない");
                if (slice % MegaCull::LevelCount >= 1u)
                {
                    Expect(MegaCull::IsSingleCut(actual, 1u, slice), "ケース J4: インスタンス 1 の選択がどのスライスでも一つの切り口でなければならない");
                }
            }
            std::cout << TestName << " ケース J4: スライス=" << wideSlices << " 選んだクラスタ=" << outcome.Selected << " 通った（インスタンス、スライス）="
                      << outcome.InstanceLevels << " 階層の語=" << expectedBits.size() << std::endl;
        }

        // ----- J5: 点光源の面（透視のスライス） -----
        return RunPointMegaCullCases(device, cull, frameSerial);
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
        SetCameraForward(pagesDispatch, scene);
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

        // ----- ケース K2: カリングの一覧の容量を超えて落としたクラスタのページは、欠けたまま持ち越さず、次のフレームで描き直す -----
        // 一覧の容量を小さくしたフレーム（最初のフレーム。全ページが dirty）は、落としたクラスタの形が物理ページに描かれず欠ける。
        // 落としたクラスタの範囲の、割り当て済みで dirty のページには再描画の印が付き、次のフレーム（同じ場面・容量は十分）が
        // 前フレームの表を引き継いで、印のあるページだけを描き直す。結果の物理プールが、ケース F（毎フレーム描き直したときと同じ）の全 texel と一致する
        {
            constexpr uint32_t OverflowCapacity = 2;
            const BufferPtr smallList = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::MegaCullListBytes(OverflowCapacity), VirtualShadowMap::MegaCullListUsage() | readable, true, "VsmMegaRetryTestList"));
            const BufferPtr smallChunks = device->CreateBuffer(
                BufferDesc(VirtualShadowMap::RasterChunkBytes(OverflowCapacity), VirtualShadowMap::MegaChunkUsage() | readable, true, "VsmMegaRetryTestMegaChunks"));
            Resources retryResources;
            VirtualShadowMapPages retryPages;
            if (!smallList || !smallChunks || !CreateResources(device, caseF.PoolPages, retryResources) ||
                !retryPages.Initialize(device.get(), &shaderManager))
            {
                std::cerr << TestName << " ケース K2: 資源を作れませんでした" << std::endl;
                return false;
            }

            struct RetryFrame
            {
                Container::VariableArray<uint32_t> Pool;
                Container::VariableArray<uint32_t> PageTable;
                Container::VariableArray<uint32_t> Stats;
                Container::VariableArray<uint32_t> List;
                Container::VariableArray<PageInfo> Infos;
            };
            // 印付け → 割り当て（キャッシュを使う）→ 消去 → カリング → クラスタの記録 → 展開 → 描画を 1 フレーム分走らせ、結果を読み戻す
            const auto runFrame = [&](const BufferPtr& frameList, const BufferPtr& frameChunks, uint32_t capacity, RetryFrame& out) -> bool
            {
                const uint64_t frameId = frameSerial++;
                retryPages.BeginFrame(0, frameId);
                cull.BeginFrame(0, frameId);
                raster.BeginFrame(0, frameId);
                CommandListPtr retryCommands = device->CreateCommandList();
                if (!retryCommands)
                {
                    return false;
                }
                const BufferPtr frameBuffers[] = {retryResources.Pool,      retryResources.PageTable, retryResources.RequestBits, retryResources.FreeList,
                                                  retryResources.Stats,     retryResources.DirtyList, dirtyBits,                  frameList,
                                                  frameChunks,              hostChunks,               instances,                  draws};
                retryCommands->Begin();
                for (const BufferPtr& buffer : frameBuffers)
                {
                    retryCommands->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
                }
                VirtualShadowMapPagesDispatch frameDispatch;
                frameDispatch.PoolPages = retryResources.PoolPages;
                frameDispatch.Pool = retryResources.Pool;
                frameDispatch.PageTable = retryResources.PageTable;
                frameDispatch.RequestBits = retryResources.RequestBits;
                frameDispatch.FreeList = retryResources.FreeList;
                frameDispatch.Stats = retryResources.Stats;
                frameDispatch.DirtyList = retryResources.DirtyList;
                frameDispatch.Depth = depth;
                frameDispatch.Clipmap = &scene.Clipmap;
                std::memcpy(frameDispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(frameDispatch.InverseViewProjection));
                std::memcpy(frameDispatch.CameraPosition, scene.CameraPosition, sizeof(frameDispatch.CameraPosition));
                SetCameraForward(frameDispatch, scene);
                frameDispatch.FovYDegrees = scene.Camera.FieldOfView;
                frameDispatch.bCacheEnabled = true;
                const bool bFramePages = retryPages.Record(retryCommands.get(), frameDispatch);

                VirtualShadowMapMegaCullDispatch frameCull;
                frameCull.Clipmap = &scene.Clipmap;
                frameCull.PageTable = retryResources.PageTable;
                frameCull.Stats = retryResources.Stats;
                frameCull.DirtyBits = dirtyBits;
                frameCull.List = frameList;
                frameCull.Chunks = frameChunks;
                frameCull.Instances = instanceBuffer;
                frameCull.ShadowInstances = shadowBuffer;
                frameCull.MegaPageTable = geometryPages;
                frameCull.InstanceCount = shapeCount;
                frameCull.TotalGroups = shapeCount;
                const bool bFrameCull = cull.Record(retryCommands.get(), frameCull);

                VirtualShadowMapRasterDispatch frameRaster;
                frameRaster.Clipmap = &scene.Clipmap;
                frameRaster.PoolPages = retryResources.PoolPages;
                frameRaster.Pool = retryResources.Pool;
                frameRaster.PageTable = retryResources.PageTable;
                frameRaster.Stats = retryResources.Stats;
                frameRaster.Chunks = hostChunks;
                frameRaster.ChunkCount = 0u;
                frameRaster.Instances = instances;
                frameRaster.Draws = draws;
                frameRaster.MegaChunks = frameChunks;
                frameRaster.MegaList = frameList;
                frameRaster.MegaCapacity = capacity;
                const bool bFrameRaster = raster.Record(retryCommands.get(), frameRaster);
                for (const BufferPtr& buffer : frameBuffers)
                {
                    retryCommands->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
                }
                retryCommands->End();
                retryCommands->Submit(true);
                device->WaitIdle();
                if (!bFramePages || !bFrameCull || !bFrameRaster || !ReadAll(retryResources.Pool, out.Pool) ||
                    !ReadAll(retryResources.PageTable, out.PageTable) || !ReadAll(retryResources.Stats, out.Stats) || !ReadAll(frameList, out.List))
                {
                    return false;
                }
                out.Infos = DecodePages(scene, out.PageTable);
                return true;
            };
            // ケース F の同じ（段・絶対のページ）のページ。無ければ null
            const auto counterpartOf = [&](const PageInfo& page) -> const PageInfo*
            {
                for (const PageInfo& candidate : caseF.Pages)
                {
                    if (candidate.Level == page.Level && candidate.AbsX == page.AbsX && candidate.AbsY == page.AbsY)
                    {
                        return &candidate;
                    }
                }
                return nullptr;
            };
            const auto pageDiffers = [&](const RetryFrame& frame, const PageInfo& page, const PageInfo& counterpart) -> bool
            {
                const size_t baseFrame = static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS;
                const size_t baseF = static_cast<size_t>(counterpart.Physical) * VirtualShadowMap::PAGE_WORDS;
                for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
                {
                    if (frame.Pool[baseFrame + word] != caseF.Pool[baseF + word])
                    {
                        return true;
                    }
                }
                return false;
            };

            RetryFrame overflowFrame;
            if (!runFrame(smallList, smallChunks, OverflowCapacity, overflowFrame))
            {
                std::cerr << TestName << " ケース K2（溢れるフレーム）を実行できませんでした" << std::endl;
                return false;
            }
            Expect(overflowFrame.List[0] > OverflowCapacity && overflowFrame.List[1] == overflowFrame.List[0] - OverflowCapacity &&
                       overflowFrame.Stats[VirtualShadowMap::StatMegaOverflow] == overflowFrame.List[1],
                   "ケース K2: 容量が小さいカリングは、選んだ数のうち容量を超えた分を溢れとして数えなければならない");
            Expect(overflowFrame.Infos.size() == caseF.Pages.size(), "ケース K2: 溢れるフレームの割り当てたページの数がケース F と同じでなければならない");
            uint32_t incompletePages = 0;
            uint32_t retryCount = 0;
            uint32_t unmarkedIncomplete = 0;
            uint32_t retryOnClean = 0;
            for (const PageInfo& page : overflowFrame.Infos)
            {
                const PageInfo* counterpart = counterpartOf(page);
                Expect(counterpart != nullptr, "ケース K2: ケース F と同じページが割り当てられなければならない");
                if (counterpart == nullptr)
                {
                    continue;
                }
                const bool bIncomplete = pageDiffers(overflowFrame, page, *counterpart);
                incompletePages += bIncomplete ? 1u : 0u;
                retryCount += page.bRetry ? 1u : 0u;
                // 落としたクラスタの形が欠けたページには、必ず再描画の印が付く（印は dirty のページにだけ付く）
                unmarkedIncomplete += bIncomplete && !page.bRetry ? 1u : 0u;
                retryOnClean += page.bRetry && !page.bDirty ? 1u : 0u;
            }
            std::cout << TestName << " ケース K2: 選んだ=" << overflowFrame.List[0] << " 溢れ=" << overflowFrame.List[1] << " 欠けたページ=" << incompletePages
                      << " 再描画の印=" << retryCount << " 印の無い欠け=" << unmarkedIncomplete << std::endl;
            Expect(incompletePages > 0u, "ケース K2: 容量を超えて落としたクラスタの形が、物理ページから欠けた場面でなければならない");
            Expect(retryCount > 0u && unmarkedIncomplete == 0u && retryOnClean == 0u,
                   "ケース K2: 落としたクラスタの形が欠けたページ（割り当て済みで dirty）には、すべて再描画の印が付かなければならない");

            RetryFrame recoverFrame;
            if (!runFrame(list, megaChunks, ListCapacity, recoverFrame))
            {
                std::cerr << TestName << " ケース K2（描き直すフレーム）を実行できませんでした" << std::endl;
                return false;
            }
            uint32_t remainingRetry = 0;
            for (const PageInfo& page : recoverFrame.Infos)
            {
                remainingRetry += page.bRetry ? 1u : 0u;
            }
            Expect(recoverFrame.List[1] == 0u && recoverFrame.Stats[VirtualShadowMap::StatMegaOverflow] == 0u,
                   "ケース K2: 描き直すフレームは容量が十分で、溢れてはならない");
            Expect(recoverFrame.Stats[VirtualShadowMap::StatRendered] == retryCount && recoverFrame.Stats[VirtualShadowMap::StatInvalidated] == retryCount &&
                       remainingRetry == 0u,
                   "ケース K2: 再描画の印のあるページだけが dirty になって描き直され、印は外れなければならない");
            Expect(recoverFrame.Infos.size() == caseF.Pages.size(), "ケース K2: 描き直すフレームの割り当てたページの数がケース F と同じでなければならない");
            uint32_t differentPages = 0;
            for (const PageInfo& page : recoverFrame.Infos)
            {
                const PageInfo* counterpart = counterpartOf(page);
                if (counterpart == nullptr || pageDiffers(recoverFrame, page, *counterpart))
                {
                    ++differentPages;
                }
            }
            std::cout << TestName << " ケース K2: 描き直したページ=" << recoverFrame.Stats[VirtualShadowMap::StatRendered]
                      << " ケース F と違うページ=" << differentPages << std::endl;
            Expect(differentPages == 0u,
                   "ケース K2: 描き直した後の物理プールが、毎フレーム描き直したとき（ケース F）と全 texel で一致しなければならない（欠けを持ち越さない）");
        }
        return true;
    }

    // ========================================
    // ケース L: 照明が使う VSM の読み出し（Common/VirtualShadowMap.glsl の VsmSampleSunShadow。計算シェーダー vsm_sample_probe.comp から呼ぶ）
    // ========================================
    //
    // ケース F の物理プールとページの表（近い四角形 + 遠い四角形が描かれている）を、照明と同じ関数で受け手の点から読む。
    //   L1（実際のしきい値）: 画面の安定した画素のワールドの位置・法線を受け手にして、印を付けた段のページがそのまま読めること
    //       （粗い段へ逃げた標本が 0）と、使った段の texel の一辺が CPU の SelectVirtualShadowMapLevel と一致することを確かめる。
    //   L2（段を固定）: しきい値を置き換えて段をケース F の段に固定し、カメラを PCF の半径が 2 texel になる距離に置いて、四角形の後ろの受け手を読む。
    //       影の中心で 0、影の外で 1、縁（PCF の標本が形の縁をまたぐ位置）で 0 と 1 の間の値になり、
    //       値は CPU の参照（Poisson の 16 点それぞれが形の内側か）と一致する。ページの境界をまたぐ標本も読む。
    //   L3（割り当てられていないページ）: 受け手のページを割り当て外にし、1 段粗い段の別の物理ページ（全 texel が受け手より手前の深度）を割り当てると、
    //       その粗い段の値（0）になる（逃げた標本は 16）。粗い段にも無ければ影なし（1。逃げた標本は 16）。
    //       粗い段へ逃げたときに返す texel の一辺は、実際に読んだ（粗い）段の値。
    //   L4（影の距離の範囲・奥の薄め）: CSM と同じ前方への距離で、範囲の外は影なし、最後のカスケードの幅の 10% で薄める。
    //   L5（物理の半影）: 光に正対する受け手の平面をカメラが正面から見る場面で、同じ四角形（縁が中心の近くを縦に通る）を受け手から深度の差 10 m・30 m の
    //       位置に置き、本番の流れ（印付け → 割り当て → 消去 → 展開 → 描画）で物理ページへ描いて、縁を読む。縁の途中の値（0 と 1 の間）の帯の幅が、
    //       物理の半影（深度の差 × 太陽の角半径の tan。Poisson の 16 点の横の広がりを掛けた値）に ±30% で合い、2 つの帯の幅の比が深度の差の比（3）に
    //       ±30% で合うこと。遮る物に接する受け手は、深度の差 10 m の帯の半分未満の鋭い縁になること。探索・PCF の標本が粗い段へ逃げないこと
    //       （印付けが読むページまで届くこと）も確かめる。
    //   C2（印の範囲）: カメラを受け手の平面から 0.1 m に置いた 1 画素の印の範囲が、ページ何枚分にもなっても CPU の参照（半径が覆うすべてのページ）と一致すること。

    // Common/PoissonDisk16.glsl と同じ 16 点（CPU の参照のための独立した写し。シェーダーの点列が変わると一致しなくなる）
    constexpr double ReferencePoissonDisk[16][2] = {
        {-0.94201624, -0.39906216}, {0.94558609, -0.76890725}, {-0.09418410, -0.92938870}, {0.34495938, 0.29387760},
        {-0.91588581, 0.45771432},  {-0.81544232, -0.87912464}, {-0.38277543, 0.27676845}, {0.97484398, 0.75648379},
        {0.44323325, -0.97511554},  {0.53742981, -0.47373420},  {-0.26496911, -0.41893023}, {0.79197514, 0.19090188},
        {-0.24188840, 0.99706507},  {-0.81409955, 0.91437590},  {0.19984126, 0.78641367},  {0.14383161, -0.14100790}};

    struct SampleProbePoint
    {
        float Position[4];
        float Normal[4];
    };

    // vsm_sample_probe.comp の VsmSampleProbeParams（std140）と同じ並び
    struct SampleProbeParams
    {
        GPUVsmSampleParams Vsm;
        /** @brief x = 点の数、y = 1 なら点光源の VSM を読む、z = 点光源の影の灯の番号 */
        uint32_t Control[4];
        GPUVsmPointSampleParams Point;
    };
    static_assert(sizeof(SampleProbeParams) == sizeof(GPUVsmSampleParams) + 16 + sizeof(GPUVsmPointSampleParams),
                  "vsm_sample_probe.comp の VsmSampleProbeParams と同じ大きさにすること");

    struct SampleProbe
    {
        ShaderPtr Shader;
        PipelinePtr Pipeline;
        DescriptorSetDesc Layout;
    };

    struct SampleOutput
    {
        Container::VariableArray<float> Visibility;
        Container::VariableArray<float> TexelMeters;
        // 粗い段へ逃げた PCF の標本の数（統計の語 0。点光源は語 1）
        uint32_t Fallback = 0;
    };

    bool CreateSampleProbe(const DevicePtr& device, ShaderManager& shaderManager, SampleProbe& probe)
    {
        probe.Shader = shaderManager.LoadShader("vsm_sample_probe.comp", RHI::ShaderStage::Compute);
        if (!probe.Shader)
        {
            return false;
        }
        // 0 = パラメータ、1 = 点、2 = 結果、3 = 統計、4 = ページの表、5 = 物理ページのプール、6 = スライスの表
        for (uint32_t binding = 0; binding < 7u; ++binding)
        {
            DescriptorBinding entry;
            entry.binding = binding;
            entry.type = binding == 0u ? ResourceBindType::ConstantBuffer : ResourceBindType::RWBuffer;
            entry.stages = RHI::ShaderStage::Compute;
            probe.Layout.bindings.push_back(entry);
        }
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = probe.Shader;
        pipelineDesc.descriptorSetLayouts.push_back(probe.Layout);
        probe.Pipeline = device->CreateComputePipeline(pipelineDesc);
        return probe.Pipeline != nullptr;
    }

    // 受け手の点を GPU で評価する（スライスの表・パラメータを呼び出し側が与える）。pool・table はページの表・物理プールの全語。
    // params.Control[0]（点の数）はここで points.size() に置き換える。点光源（Control[1] = 1）の逃げた標本は統計の語 1
    bool RunSampleProbeWith(const DevicePtr& device,
                            const SampleProbe& probe,
                            const GPUVsmSlice* sliceData,
                            uint32_t sliceCount,
                            SampleProbeParams params,
                            const Container::VariableArray<uint32_t>& poolWords,
                            const Container::VariableArray<uint32_t>& tableWords,
                            const Container::VariableArray<SampleProbePoint>& points,
                            SampleOutput& out)
    {
        const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        const uint64_t pointBytes = static_cast<uint64_t>(points.size()) * sizeof(SampleProbePoint);
        const uint64_t resultBytes = static_cast<uint64_t>(points.size()) * 4u * sizeof(float);
        BufferPtr uniform = device->CreateBuffer(BufferDesc(sizeof(SampleProbeParams), ResourceUsage::ConstantBuffer, true, "VsmSampleProbeParams"));
        BufferPtr pointBuffer = device->CreateBuffer(BufferDesc(pointBytes, usage, true, "VsmSampleProbePoints"));
        BufferPtr results = device->CreateBuffer(BufferDesc(resultBytes, usage, true, "VsmSampleProbeResults"));
        BufferPtr stats = device->CreateBuffer(BufferDesc(4u * sizeof(uint32_t), usage, true, "VsmSampleProbeStats"));
        BufferPtr table = device->CreateBuffer(BufferDesc(static_cast<uint64_t>(tableWords.size()) * sizeof(uint32_t), usage, true, "VsmSampleProbeTable"));
        BufferPtr pool = device->CreateBuffer(BufferDesc(static_cast<uint64_t>(poolWords.size()) * sizeof(uint32_t), usage, true, "VsmSampleProbePool"));
        BufferPtr sliceBuffer = device->CreateBuffer(BufferDesc(sizeof(GPUVsmSlice) * sliceCount, usage, true, "VsmSampleProbeSlices"));
        DescriptorSetPtr descriptorSet = device->CreateDescriptorSet(probe.Layout);
        CommandListPtr commandList = device->CreateCommandList();
        if (!uniform || !pointBuffer || !results || !stats || !table || !pool || !sliceBuffer || !descriptorSet || !commandList || points.empty())
        {
            return false;
        }

        params.Control[0] = static_cast<uint32_t>(points.size());
        uniform->Update(&params, sizeof(params));
        pointBuffer->Update(points.data(), pointBytes);
        const Container::VariableArray<uint32_t> zeroResults(resultBytes / sizeof(uint32_t), 0u);
        results->Update(zeroResults.data(), resultBytes);
        const uint32_t zeroStats[4] = {};
        stats->Update(zeroStats, sizeof(zeroStats));
        table->Update(tableWords.data(), static_cast<uint64_t>(tableWords.size()) * sizeof(uint32_t));
        pool->Update(poolWords.data(), static_cast<uint64_t>(poolWords.size()) * sizeof(uint32_t));
        sliceBuffer->Update(sliceData, sizeof(GPUVsmSlice) * sliceCount);

        descriptorSet->BindConstantBuffer(0, uniform, 0, static_cast<uint32_t>(sizeof(SampleProbeParams)));
        descriptorSet->BindStorageBuffer(1, pointBuffer, 0, static_cast<uint32_t>(pointBytes));
        descriptorSet->BindStorageBuffer(2, results, 0, static_cast<uint32_t>(resultBytes));
        descriptorSet->BindStorageBuffer(3, stats, 0, static_cast<uint32_t>(stats->GetSize()));
        descriptorSet->BindStorageBuffer(4, table, 0, static_cast<uint32_t>(table->GetSize()));
        descriptorSet->BindStorageBuffer(5, pool, 0, static_cast<uint32_t>(pool->GetSize()));
        descriptorSet->BindStorageBuffer(6, sliceBuffer, 0, static_cast<uint32_t>(sliceBuffer->GetSize()));
        descriptorSet->Update();

        const BufferPtr storage[] = {pointBuffer, results, stats, table, pool, sliceBuffer};
        commandList->Begin();
        for (const BufferPtr& buffer : storage)
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        commandList->SetPipeline(probe.Pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0);
        commandList->Dispatch((static_cast<uint32_t>(points.size()) + 63u) / 64u, 1u, 1u);
        for (const BufferPtr& buffer : {results, stats})
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        Container::VariableArray<uint32_t> resultWords;
        Container::VariableArray<uint32_t> statWords;
        if (!ReadAll(results, resultWords) || !ReadAll(stats, statWords))
        {
            return false;
        }
        out.Visibility.assign(points.size(), 0.0f);
        out.TexelMeters.assign(points.size(), 0.0f);
        for (size_t index = 0; index < points.size(); ++index)
        {
            out.Visibility[index] = WordToFloat(resultWords[index * 4u]);
            out.TexelMeters[index] = WordToFloat(resultWords[index * 4u + 1u]);
        }
        out.Fallback = params.Control[1] == 1u ? statWords[1] : statWords[0];
        return true;
    }

    // 太陽の VSM の読み出し。pool・table はケース F のページの表・物理プール（またはその変更）の全語
    bool RunSampleProbe(const DevicePtr& device,
                        const SampleProbe& probe,
                        const VirtualShadowMapClipmap& clipmap,
                        const GPUVsmSampleParams& vsm,
                        const Container::VariableArray<uint32_t>& poolWords,
                        const Container::VariableArray<uint32_t>& tableWords,
                        const Container::VariableArray<SampleProbePoint>& points,
                        SampleOutput& out)
    {
        GPUVsmSlice slices[VirtualShadowMapMaxLevels];
        BuildVirtualShadowMapSlices(&clipmap, nullptr, VirtualShadowMapMaxLevels, slices);
        SampleProbeParams params = {};
        params.Vsm = vsm;
        return RunSampleProbeWith(device, probe, slices, VirtualShadowMapMaxLevels, params, poolWords, tableWords, points, out);
    }

    // 受け手の点（ライト空間の位置と深度）のワールドの位置と、光源を向いた法線（光に正対するので法線の向きへのずらしも受け面の傾きも 0）
    SampleProbePoint MakeReceiver(const Scene& scene, double lightX, double lightY, double lightDepth)
    {
        const Math::Vector3 position = LightToWorld(scene, lightX, lightY, lightDepth);
        const Math::Vector3& direction = scene.Clipmap.Direction;
        SampleProbePoint point = {};
        point.Position[0] = position.x;
        point.Position[1] = position.y;
        point.Position[2] = position.z;
        point.Position[3] = 1.0f;
        point.Normal[0] = -direction.x;
        point.Normal[1] = -direction.y;
        point.Normal[2] = -direction.z;
        return point;
    }

    // 段を固定し（しきい値: 段より下を 0、上を 1e30）、影の距離の範囲を広げ、カメラを受け手から「PCF の半径が 2 texel になる距離」だけ離す
    // （カメラは受け手の +X 側に置き、前方は受け手を向く -X）
    GPUVsmSampleParams MakeForcedLevelParams(const GPUVsmSampleParams& real, uint32_t level, double texel, const SampleProbePoint& receiver)
    {
        GPUVsmSampleParams params = real;
        for (uint32_t index = 0; index < VirtualShadowMapMaxLevels; ++index)
        {
            params.thresholds[index] = index < level ? 0.0f : 1.0e30f;
        }
        const double distance = 2.0 * texel / static_cast<double>(real.pixel[0]);
        params.cameraPosition[0] = receiver.Position[0] + static_cast<float>(distance);
        params.cameraPosition[1] = receiver.Position[1];
        params.cameraPosition[2] = receiver.Position[2];
        params.view[0] = -1.0f;
        params.view[1] = 0.0f;
        params.view[2] = 0.0f;
        params.range[0] = 0.0f;
        params.range[1] = 1.0e6f;
        params.range[2] = 1.0f;
        return params;
    }

    // MakeForcedLevelParams の段の固定のまま、カメラを (受け手 + offset) へ置き、前方を viewDirection、影の距離の範囲を (near, far, fadeWidth) にする。
    // PCF の半径は常に 2 texel（画素の大きさを、カメラからの直線距離に合わせて決める）
    GPUVsmSampleParams MakeDistanceParams(const GPUVsmSampleParams& real,
                                          uint32_t level,
                                          double texel,
                                          const SampleProbePoint& receiver,
                                          const double offset[3],
                                          const double viewDirection[3],
                                          double nearDistance,
                                          double farDistance,
                                          double fadeWidth)
    {
        GPUVsmSampleParams params = MakeForcedLevelParams(real, level, texel, receiver);
        const double straight = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            params.cameraPosition[axis] = receiver.Position[axis] + static_cast<float>(offset[axis]);
            params.view[axis] = static_cast<float>(viewDirection[axis]);
        }
        params.pixel[0] = static_cast<float>(2.0 * texel / straight);
        params.range[0] = static_cast<float>(nearDistance);
        params.range[1] = static_cast<float>(farDistance);
        params.range[2] = static_cast<float>(fadeWidth);
        return params;
    }

    // CPU の参照: 受け手の点から半径 radius の Poisson の 16 点が、形のどれかに覆われない割合（可視度）。
    // 標本が読む texel（ページの格子は texel の整数倍）の中心が形の内側なら覆われている（ラスタライズの規則）。受け手は形より十分後ろにあるので、
    // 覆われた標本は影になる。標本が texel の境界の近く（0.01 texel 未満）・texel の中心が形の縁の近くにあるものは丸めで結果が変わりうるので、あいまいとして知らせる
    double ReferenceVisibility(const Container::VariableArray<Shape>& shapes, double lightX, double lightY, double radius, double texel, bool& outAmbiguous)
    {
        constexpr double AmbiguousTexels = 0.01;
        outAmbiguous = false;
        uint32_t lit = 0;
        for (const auto& point : ReferencePoissonDisk)
        {
            const double sampleX = lightX + point[0] * radius;
            const double sampleY = lightY + point[1] * radius;
            // 形の縁から 1 texel 以上離れた標本は、どの texel を読んでも結果が同じ（texel の境界・中心の細かい扱いに依らない）
            double bestInside = -1.0e300;
            for (const Shape& shape : shapes)
            {
                bestInside = std::max(bestInside, ShapeSignedDistance(shape, sampleX, sampleY));
            }
            if (bestInside >= texel)
            {
                continue; // 覆われている → 影
            }
            if (bestInside <= -texel)
            {
                ++lit; // どの形にも覆われない → 光
                continue;
            }
            const double gridX = sampleX / texel;
            const double gridY = sampleY / texel;
            if (std::abs(gridX - std::round(gridX)) < AmbiguousTexels || std::abs(gridY - std::round(gridY)) < AmbiguousTexels)
            {
                outAmbiguous = true;
            }
            const double centerX = (std::floor(gridX) + 0.5) * texel;
            const double centerY = (std::floor(gridY) + 0.5) * texel;
            bool covered = false;
            for (const Shape& shape : shapes)
            {
                const double distance = ShapeSignedDistance(shape, centerX, centerY);
                if (std::abs(distance) < AmbiguousTexels * texel)
                {
                    outAmbiguous = true;
                }
                covered = covered || distance >= 0.0;
            }
            lit += covered ? 0u : 1u;
        }
        return static_cast<double>(lit) / 16.0;
    }

    // ----- L5: 物理の半影 -----
    // 光に正対する平面（法線 = 光の向き。ライト空間の深度が一定）を受け手にし、カメラがその中心を正面から見る場面を作る。受け手の平面の深度の画像を
    // 本番の流れ（印付け → 割り当て → 消去 → 展開 → 描画）に通し、縁が中心の近くを縦に通る同じ四角形を、受け手から depthGap だけ光の側へ置いて描く。
    // 縁を横切る受け手を照明と同じ関数で読み、可視度が 0 と 1 の間の値になる位置の幅（帯の幅）を測る。
    // 半影の半幅は depthGap × 太陽の角半径の tan。Poisson の 16 点の横の広がりは [-0.94201624, 0.97484398] の 1.91686 倍
    struct ReceiverScene
    {
        Scene Base;
        // 受け手の平面の中心（カメラの正面）のライト空間の座標と、平面のライト空間の深度
        double CenterX = 0.0;
        double CenterY = 0.0;
        double ReceiverDepth = 0.0;
        // 深度の画像（中心の周りの窓だけが平面。ほかは空）の画素の、カメラからの直線距離の最小と最大
        double MinDistance = 0.0;
        double MaxDistance = 0.0;
        Container::VariableArray<float> Image;
    };

    // cameraDistance: カメラから受け手の平面までの距離（前方への距離）。windowHalfPixels: 窓の中心から窓の端までの画素数。
    // lateralMeters: 受け手の平面の中心がカメラの正面から右へずれる量（m）。0 でない（視錐台の端の）ときは、窓の中心をその点の画素にする
    ReceiverScene BuildReceiverScene(const DevicePtr& device, double cameraDistance, int32_t windowHalfPixels, double lateralMeters = 0.0)
    {
        ReceiverScene result;
        Scene& scene = result.Base;
        const Math::Vector3 sunDirection(0.35f, -0.8f, 0.45f);
        // ライト空間の軸は太陽の向きだけで決まる。仮のカメラのクリップマップから取り出す
        const VirtualShadowMapClipmap axes = BuildVirtualShadowMapClipmap(sunDirection, 1u, Math::Vector3(0.0f, 0.0f, 0.0f), scene.Settings);
        const double right[3] = {axes.LightRight.x, axes.LightRight.y, axes.LightRight.z};
        const double up[3] = {axes.LightUp.x, axes.LightUp.y, axes.LightUp.z};
        const double forward[3] = {axes.Direction.x, axes.Direction.y, axes.Direction.z};

        result.CenterX = 0.0137;
        result.CenterY = 0.0291;
        result.ReceiverDepth = 0.0;
        double center[3] = {};
        double cameraPosition[3] = {};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            center[axis] = right[axis] * result.CenterX + up[axis] * result.CenterY + forward[axis] * result.ReceiverDepth;
        }

        // 前方 = 光の向き、上 = ライト空間の上、右 = 前方 × 上
        const double cameraRight[3] = {forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2], forward[0] * up[1] - forward[1] * up[0]};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            cameraPosition[axis] = center[axis] - forward[axis] * cameraDistance - cameraRight[axis] * lateralMeters;
        }
        scene.Camera.PositionX = static_cast<float>(cameraPosition[0]);
        scene.Camera.PositionY = static_cast<float>(cameraPosition[1]);
        scene.Camera.PositionZ = static_cast<float>(cameraPosition[2]);
        scene.Camera.ForwardX = static_cast<float>(forward[0]);
        scene.Camera.ForwardY = static_cast<float>(forward[1]);
        scene.Camera.ForwardZ = static_cast<float>(forward[2]);
        scene.Camera.RightX = static_cast<float>(cameraRight[0]);
        scene.Camera.RightY = static_cast<float>(cameraRight[1]);
        scene.Camera.RightZ = static_cast<float>(cameraRight[2]);
        scene.Camera.UpX = static_cast<float>(up[0]);
        scene.Camera.UpY = static_cast<float>(up[1]);
        scene.Camera.UpZ = static_cast<float>(up[2]);
        scene.Camera.Projection = ProjectionType::Perspective;
        scene.Camera.FieldOfView = 60.0f;
        scene.Camera.NearPlane = 0.01f;
        scene.Camera.FarPlane = 100.0f;
        scene.Camera.AspectRatio = static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight);
        const CameraViewConstants constants = CameraViewConstants::BuildForDevice(scene.Camera, scene.Camera.AspectRatio, device.get());
        constants.CopyShaderInverseViewProjection(scene.InverseViewProjection);
        constants.CopyShaderViewProjection(scene.ViewProjection);
        scene.CameraPosition[0] = scene.Camera.PositionX;
        scene.CameraPosition[1] = scene.Camera.PositionY;
        scene.CameraPosition[2] = scene.Camera.PositionZ;
        scene.Clipmap = BuildVirtualShadowMapClipmap(sunDirection, 1u, Math::Vector3(scene.CameraPosition[0], scene.CameraPosition[1], scene.CameraPosition[2]), scene.Settings);

        // 受け手の平面（中心を通り、法線は光の向き）の深度の画像
        result.Image.assign(ImageWidth * ImageHeight, 1.0f);
        result.MinDistance = 1.0e300;
        result.MaxDistance = 0.0;
        int32_t centerPixelX = static_cast<int32_t>(ImageWidth / 2u);
        const int32_t centerPixelY = static_cast<int32_t>(ImageHeight / 2u);
        if (lateralMeters != 0.0)
        {
            // 受け手の中心の画素（クリップ座標の x → 画素）
            const double centerPoint[4] = {center[0], center[1], center[2], 1.0};
            double centerClip[4] = {};
            Multiply(scene.ViewProjection, centerPoint, centerClip);
            centerPixelX = static_cast<int32_t>(std::floor((centerClip[0] / centerClip[3] * 0.5 + 0.5) * ImageWidth));
        }
        for (int32_t offsetY = -windowHalfPixels; offsetY <= windowHalfPixels; ++offsetY)
        {
            for (int32_t offsetX = -windowHalfPixels; offsetX <= windowHalfPixels; ++offsetX)
            {
                const uint32_t pixelX = static_cast<uint32_t>(centerPixelX + offsetX);
                const uint32_t pixelY = static_cast<uint32_t>(centerPixelY + offsetY);
                double nearPoint[3] = {};
                double farPoint[3] = {};
                Unproject(scene, pixelX, pixelY, 0.0, nearPoint);
                Unproject(scene, pixelX, pixelY, 1.0, farPoint);
                const double direction[3] = {farPoint[0] - nearPoint[0], farPoint[1] - nearPoint[1], farPoint[2] - nearPoint[2]};
                double toPlane = 0.0;
                double along = 0.0;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    toPlane += (center[axis] - nearPoint[axis]) * forward[axis];
                    along += direction[axis] * forward[axis];
                }
                if (std::abs(along) < 1.0e-12)
                {
                    continue;
                }
                const double t = toPlane / along;
                const double hit[4] = {nearPoint[0] + direction[0] * t, nearPoint[1] + direction[1] * t, nearPoint[2] + direction[2] * t, 1.0};
                double clip[4] = {};
                Multiply(scene.ViewProjection, hit, clip);
                result.Image[pixelY * ImageWidth + pixelX] = static_cast<float>(clip[2] / clip[3]);
                const double distance = std::sqrt((hit[0] - cameraPosition[0]) * (hit[0] - cameraPosition[0]) + (hit[1] - cameraPosition[1]) * (hit[1] - cameraPosition[1]) +
                                                  (hit[2] - cameraPosition[2]) * (hit[2] - cameraPosition[2]));
                result.MinDistance = std::min(result.MinDistance, distance);
                result.MaxDistance = std::max(result.MaxDistance, distance);
            }
        }
        return result;
    }

    struct BandResult
    {
        bool bRan = false;
        // 可視度が 0 と 1 の間の値になる点があったか
        bool bBand = false;
        double Width = 0.0;
        // 粗い段へ逃げた標本の数
        uint32_t Fallback = 0;
    };

    struct ReceiverProbeResult
    {
        bool bRan = false;
        // 粗い段へ逃げた標本の数
        uint32_t Fallback = 0;
        // 縁からの横の位置ごとの可視度
        Container::VariableArray<float> Visibility;
    };

    // 受け手が使う段の texel の一辺（m）。段が無ければ負
    double ReceiverTexel(const ReceiverScene& receiver)
    {
        const Scene& scene = receiver.Base;
        const int32_t level = SelectVirtualShadowMapLevel(scene.Settings, static_cast<float>(receiver.MinDistance), scene.Camera.FieldOfView, static_cast<float>(ImageHeight));
        return level < 0 ? -1.0 : static_cast<double>(scene.Clipmap.Levels[static_cast<uint32_t>(level)].TexelMeters);
    }

    // 受け手の平面の深度の画像を本番の流れに通し、縁がライト空間の x = 中心 + 0.0731 の四角形（受け手から depthGap だけ光の側）を描いて、
    // 縁から offsets（m。正が光の当たる側）だけ離れた受け手を照明と同じ関数で読む
    ReceiverProbeResult ProbeReceiverEdge(const DevicePtr& device,
                                          VirtualShadowMapPages& pages,
                                          VirtualShadowMapRaster& raster,
                                          const SampleProbe& probe,
                                          const ReceiverScene& receiver,
                                          const TexturePtr& depth,
                                          double depthGap,
                                          uint64_t& frameSerial,
                                          const Container::VariableArray<double>& offsets)
    {
        ReceiverProbeResult result;
        const Scene& scene = receiver.Base;
        const double edgeX = receiver.CenterX + 0.0731;

        // 縁の左側（x < edgeX）が四角形。奥行きは画面の窓より十分に広い
        Container::VariableArray<Shape> shapes;
        Shape quad = MakeRect(edgeX - 5.0, edgeX, receiver.CenterY - 5.0, receiver.CenterY + 5.0);
        SetPlane(quad, edgeX, receiver.CenterY, receiver.ReceiverDepth - depthGap, 0.0, 0.0);
        shapes.push_back(quad);

        ChunkGeometry geometry = BuildChunks(scene, shapes);
        Resources resources;
        RasterBuffers rasterBuffers;
        RasterReadback readback;
        constexpr uint32_t PoolPages = 256u;
        constexpr uint32_t InstanceCapacity = 4096u;
        if (!CreateResources(device, PoolPages, resources) || !CreateRasterBuffers(device, geometry, InstanceCapacity, rasterBuffers) ||
            !RunRaster(device, &pages, raster, scene, resources, rasterBuffers, depth, frameSerial++, readback))
        {
            std::cerr << TestName << " ケース L5 の本番の流れを実行できませんでした" << std::endl;
            return result;
        }
        Expect(readback.bPagesRecorded && readback.bRasterRecorded, "ケース L5: 印付け・割り当て・消去・展開・描画を記録できなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatOverflow] == 0u && readback.Stats[VirtualShadowMap::StatRasterOverflow] == 0u,
               "ケース L5: プール・インスタンスの容量に収まらなければならない");

        const float cameraForward[3] = {scene.Camera.ForwardX, scene.Camera.ForwardY, scene.Camera.ForwardZ};
        GPUVsmSampleParams params;
        if (!BuildVirtualShadowMapSampleParams(&scene.Clipmap, scene.CameraPosition, cameraForward, nullptr, scene.Camera.FieldOfView,
                                               static_cast<float>(ImageHeight), PoolPages, params))
        {
            std::cerr << TestName << " ケース L5: 読み出しのパラメータを作れませんでした" << std::endl;
            return result;
        }
        Expect(params.pixel[1] == VirtualShadowMap::SUN_TAN_ANGULAR_RADIUS && params.pixel[2] == VirtualShadowMap::MAX_FILTER_RADIUS_METERS,
               "ケース L5: 読み出しのパラメータが太陽の角半径の tan と探索・PCF の半径の上限を持たなければならない");

        Container::VariableArray<SampleProbePoint> points;
        for (const double offset : offsets)
        {
            points.push_back(MakeReceiver(scene, edgeX + offset, receiver.CenterY, receiver.ReceiverDepth));
        }

        SampleOutput output;
        if (!RunSampleProbe(device, probe, scene.Clipmap, params, readback.Pool, readback.PageTable, points, output))
        {
            return result;
        }
        result.bRan = true;
        result.Fallback = output.Fallback;
        result.Visibility = output.Visibility;
        return result;
    }

    // 縁を横切る受け手を読み、可視度が 0 と 1 の間の値になる位置の幅（帯の幅）を測る
    BandResult MeasureReceiverBand(const DevicePtr& device,
                                   VirtualShadowMapPages& pages,
                                   VirtualShadowMapRaster& raster,
                                   const SampleProbe& probe,
                                   const ReceiverScene& receiver,
                                   const TexturePtr& depth,
                                   double depthGap,
                                   uint64_t& frameSerial)
    {
        BandResult result;
        const double texel = ReceiverTexel(receiver);
        if (!(texel > 0.0))
        {
            return result;
        }
        constexpr double TanRadius = static_cast<double>(VirtualShadowMap::SUN_TAN_ANGULAR_RADIUS);
        const double penumbra = depthGap * TanRadius;
        // 縁を中心に、帯（約 1.92 × 半影）が十分に入る範囲を texel の 1/4 刻みで読む
        const double span = std::max(1.5 * penumbra, 6.0 * texel);
        const double step = 0.25 * texel;
        const int32_t half = static_cast<int32_t>(std::ceil(span / step));
        Container::VariableArray<double> offsets;
        for (int32_t index = -half; index <= half; ++index)
        {
            offsets.push_back(static_cast<double>(index) * step);
        }

        const ReceiverProbeResult probed = ProbeReceiverEdge(device, pages, raster, probe, receiver, depth, depthGap, frameSerial, offsets);
        if (!probed.bRan)
        {
            return result;
        }
        result.bRan = true;
        result.Fallback = probed.Fallback;
        int32_t first = -1;
        int32_t last = -1;
        for (int32_t index = 0; index < static_cast<int32_t>(probed.Visibility.size()); ++index)
        {
            const float value = probed.Visibility[static_cast<size_t>(index)];
            if (value > 1.0e-6f && value < 1.0f - 1.0e-6f)
            {
                first = first < 0 ? index : first;
                last = index;
            }
        }
        result.bBand = first >= 0;
        result.Width = result.bBand ? static_cast<double>(last - first + 1) * step : 0.0;
        return result;
    }

    // ----- L6: 視錐台の端（前方の距離は影の範囲の内側で、直線の距離は範囲を超える）の受け手 -----
    // 受け手を前方 70 m・横 40 m（直線 80.6 m）に置く。照明は影の範囲を前方への距離で測るので、この受け手は範囲の内側で影を受ける。
    // 印付けも同じ前方への距離で測らないと、このページが要求されず、割り当てられず、描かれない（照明は粗い段へ逃げても何も読めず影なしになる）。
    // 本番の流れ（印付け → 割り当て → 消去 → 展開 → 描画）で四角形を描き、縁から離れた影の側・光の側の受け手を照明と同じ関数で読む。
    bool RunObliqueReceiverCase(const DevicePtr& device, VirtualShadowMapPages& pages, VirtualShadowMapRaster& raster, const SampleProbe& probe, uint64_t& frameSerial)
    {
        constexpr double Forward = 70.0;
        constexpr double Lateral = 40.0;
        // 窓は受け手の中心の 1 画素だけ。画素は 70 m 先で約 1.1 m なので、隣の画素まで含めると直線距離が影の範囲（80 m）の
        // 近く（深度の復元の誤差の内側）に入り、範囲の内側と判定される画素が混じる
        const ReceiverScene receiver = BuildReceiverScene(device, Forward, 0, Lateral);
        const Scene& scene = receiver.Base;
        const double straight = std::sqrt(Forward * Forward + Lateral * Lateral);
        std::cout << TestName << " ケース L6: カメラから受け手まで " << receiver.MinDistance << "〜" << receiver.MaxDistance << " m（前方 " << Forward
                  << " m・横 " << Lateral << " m = 直線 " << straight << " m） 影の範囲=" << scene.Settings.MaxShadowDistance << " m" << std::endl;
        Expect(receiver.MinDistance > static_cast<double>(scene.Settings.MaxShadowDistance),
               "ケース L6: 窓のすべての画素の直線距離が影の範囲を超える場面でなければならない（前方の距離は範囲の内側）");
        Expect(scene.Clipmap.bEnabled, "ケース L6: クリップマップが有効でなければならない");
        const TexturePtr depth = CreateDepthTexture(device, receiver.Image);
        if (!depth)
        {
            return false;
        }

        // 縁（x = 中心 + 0.0731）から 1.5 m 離れた影の側（負）と光の側（正）。半径（最小で画素の大きさ程度）より十分に離す
        Container::VariableArray<double> offsets;
        offsets.push_back(-1.5);
        offsets.push_back(1.5);
        const ReceiverProbeResult probed = ProbeReceiverEdge(device, pages, raster, probe, receiver, depth, 10.0, frameSerial, offsets);
        if (!probed.bRan || probed.Visibility.size() != 2u)
        {
            return false;
        }
        std::cout << TestName << " ケース L6: 可視度(影の側, 光の側)=" << probed.Visibility[0] << ", " << probed.Visibility[1] << " 逃げた標本=" << probed.Fallback << std::endl;
        Expect(probed.Visibility[0] == 0.0f, "ケース L6: 前方の距離が影の範囲の内側なら、直線距離が範囲を超える受け手も影にならなければならない（印付けが同じ距離で測る）");
        Expect(probed.Visibility[1] == 1.0f, "ケース L6: 縁の光の側の受け手は光が当たらなければならない");
        Expect(probed.Fallback == 0u, "ケース L6: 印付けが読む段のページまで要求し、粗い段へ逃げてはならない");
        return true;
    }

    bool RunPenumbraCase(const DevicePtr& device, VirtualShadowMapPages& pages, VirtualShadowMapRaster& raster, const SampleProbe& probe, uint64_t& frameSerial)
    {
        // カメラを受け手の平面の近くに置く。窓（中心から 30 画素）の画素と探索の標本が、すべて同じ段のページを読む。
        // 段の境目の距離は段ごとに 2 倍ずつ離れていて、窓の距離の幅（約 1.22 倍）が境目をまたぐ置き方があるので、またがない距離を選ぶ
        ReceiverScene receiver;
        int32_t nearLevel = -1;
        int32_t farLevel = -2;
        for (const double cameraDistance : {1.0, 1.3, 1.6, 2.0, 2.6, 3.2})
        {
            receiver = BuildReceiverScene(device, cameraDistance, 30);
            const Scene& candidate = receiver.Base;
            nearLevel = SelectVirtualShadowMapLevel(candidate.Settings, static_cast<float>(receiver.MinDistance - AmbiguityToleranceMeters),
                                                    candidate.Camera.FieldOfView, static_cast<float>(ImageHeight));
            farLevel = SelectVirtualShadowMapLevel(candidate.Settings, static_cast<float>(receiver.MaxDistance + AmbiguityToleranceMeters),
                                                   candidate.Camera.FieldOfView, static_cast<float>(ImageHeight));
            if (nearLevel >= 0 && nearLevel == farLevel)
            {
                break;
            }
        }
        const Scene& scene = receiver.Base;
        Expect(scene.Clipmap.bEnabled && std::abs(scene.Clipmap.DepthCenter) < 1.0, "ケース L5: クリップマップが有効で、深度の中心が受け手の平面の近くになければならない");
        std::cout << TestName << " ケース L5: カメラから受け手まで " << receiver.MinDistance << "〜" << receiver.MaxDistance << " m 段=" << nearLevel
                  << " 段の texel=" << scene.Clipmap.Levels[static_cast<uint32_t>(std::max(nearLevel, 0))].TexelMeters * 1000.0f << " mm" << std::endl;
        Expect(nearLevel >= 0 && nearLevel == farLevel, "ケース L5: 窓のすべての画素が同じ段を使わなければならない（場面が退化している）");
        const TexturePtr depth = CreateDepthTexture(device, receiver.Image);
        if (!depth || nearLevel < 0 || nearLevel != farLevel)
        {
            return false;
        }

        // Poisson の 16 点の横の広がり（Common/PoissonDisk16.glsl の x の最小と最大）
        constexpr double PoissonSpread = 0.94201624 + 0.97484398;
        constexpr double TanRadius = static_cast<double>(VirtualShadowMap::SUN_TAN_ANGULAR_RADIUS);
        constexpr double NearGap = 10.0;
        constexpr double FarGap = 30.0;
        double widths[2] = {};
        const double gaps[2] = {NearGap, FarGap};
        for (uint32_t index = 0; index < 2u; ++index)
        {
            const BandResult band = MeasureReceiverBand(device, pages, raster, probe, receiver, depth, gaps[index], frameSerial);
            if (!band.bRan)
            {
                return false;
            }
            widths[index] = band.Width;
            const double expected = PoissonSpread * gaps[index] * TanRadius;
            std::cout << TestName << " ケース L5 深度の差 " << gaps[index] << " m: 帯の幅=" << band.Width * 1000.0 << " mm 物理の半影から期待=" << expected * 1000.0
                      << " mm 逃げた標本=" << band.Fallback << std::endl;
            Expect(band.bBand, "ケース L5: 縁の途中の値（0 と 1 の間）が現れなければならない");
            Expect(band.Fallback == 0u, "ケース L5: 印付けが探索・PCF の標本の読むページまで届き、粗い段へ逃げてはならない");
            Expect(std::abs(band.Width - expected) <= 0.3 * expected,
                   "ケース L5: 縁の帯の幅が物理の半影（深度の差 × 太陽の角半径の tan × Poisson の広がり）に ±30% で合わなければならない");
        }
        const double ratio = widths[1] / std::max(widths[0], 1.0e-12);
        std::cout << TestName << " ケース L5: 高いほうの縁の帯の幅の比=" << ratio << "（高さの比 " << FarGap / NearGap << "）" << std::endl;
        Expect(std::abs(ratio - FarGap / NearGap) <= 0.3 * (FarGap / NearGap),
               "ケース L5: 2 つの高さの縁の帯の幅の比が、高さの比に ±30% で合わなければならない");

        // 遮る物に接する受け手（深度の差が比較の余裕の内側）は、探索で遮る物が見つからず、最小の半径の帯になる
        {
            const BandResult contact = MeasureReceiverBand(device, pages, raster, probe, receiver, depth, 0.01, frameSerial);
            if (!contact.bRan)
            {
                return false;
            }
            std::cout << TestName << " ケース L5 接する受け手: 帯の幅=" << contact.Width * 1000.0 << " mm" << std::endl;
            Expect(!contact.bBand || contact.Width < 0.5 * widths[0], "ケース L5: 遮る物に接する受け手の縁は、深度の差 10 m の縁より十分に鋭くなければならない");
        }

        // 深度の差が約 107 m を超えると半径は上限 R_max（探索・PCF のワールドの長さの上限）で止まる。深度の差 200 m（物理の半影の半幅 0.936 m）の四角形で、
        // 縁から離れた受け手を読む。半径が R_max のとき標本の横の広がりは [-0.942, 0.975] × R_max なので、縁から 0.6 m 離れた受け手は
        // 標本のどれも反対側へ届かず、光の側は 1、影の側は 0 になる（上限が無ければ半影の半幅 0.936 m の標本が反対側へ届き、0 と 1 の間の値になる）。
        // 縁から 0.4 m の受け手は、R_max の標本だけが反対側へ届いて 0 と 1 の間の値になる。印付けは R_max の標本の読むページまで届くので、粗い段へ逃げない
        {
            constexpr double HugeGap = 200.0;
            constexpr double RadiusLimit = static_cast<double>(VirtualShadowMap::MAX_FILTER_RADIUS_METERS);
            Expect(HugeGap * TanRadius > RadiusLimit, "ケース L5: 深度の差が物理の半影の半幅を R_max より大きくする値でなければならない");
            const double texel = ReceiverTexel(receiver);
            const double pageMeters = static_cast<double>(scene.Clipmap.Levels[static_cast<uint32_t>(nearLevel)].PageMeters);
            const double offsetValues[5] = {-0.6, -0.4, 0.0, 0.4, 0.6};
            Container::VariableArray<double> offsets;
            for (const double value : offsetValues)
            {
                offsets.push_back(value);
            }
            const ReceiverProbeResult capped = ProbeReceiverEdge(device, pages, raster, probe, receiver, depth, HugeGap, frameSerial, offsets);
            if (!capped.bRan || capped.Visibility.size() != 5u)
            {
                return false;
            }
            std::cout << TestName << " ケース L5 深度の差 " << HugeGap << " m: 半影の半幅=" << HugeGap * TanRadius << " m R_max=" << RadiusLimit << " m ページ=" << pageMeters
                      << " m texel=" << texel * 1000.0 << " mm 可視度(-0.6, -0.4, 0, 0.4, 0.6 m)=" << capped.Visibility[0] << ", " << capped.Visibility[1] << ", "
                      << capped.Visibility[2] << ", " << capped.Visibility[3] << ", " << capped.Visibility[4] << " 逃げた標本=" << capped.Fallback << std::endl;
            Expect(pageMeters <= 2.0 * RadiusLimit, "ケース L5: ページが R_max の 2 倍以下でなければならない（半径 R_max の標本が隣のページを読む場面）");
            Expect(capped.Visibility[4] == 1.0f, "ケース L5: 深度の差が 107 m を超えても半径は R_max で止まり、縁から 0.6 m の光の側は 1 でなければならない");
            Expect(capped.Visibility[0] == 0.0f, "ケース L5: 深度の差が 107 m を超えても半径は R_max で止まり、縁から 0.6 m の影の側は 0 でなければならない");
            Expect(capped.Visibility[1] > 0.0f && capped.Visibility[1] < 1.0f && capped.Visibility[3] > 0.0f && capped.Visibility[3] < 1.0f,
                   "ケース L5: 縁から 0.4 m の受け手は R_max の標本が反対側へ届き、0 と 1 の間の値でなければならない");
            Expect(capped.Visibility[2] > 0.0f && capped.Visibility[2] < 1.0f, "ケース L5: 縁の受け手は 0 と 1 の間の値でなければならない");
            Expect(capped.Fallback == 0u, "ケース L5: 印付けが半径 R_max の標本の読むページまで届き、粗い段へ逃げてはならない");
        }
        return true;
    }

    // ----- C2: 隣のページへの印の範囲がページの何枚分にもなるとき -----
    // カメラを受け手の平面の近く（0.1 m 前後）に置くと、細かい段（ページ 12.5 cm 以下）が選ばれ、探索・PCF の最大の半径（0.5 m + 5 texel）が片側 4 ページ分以上に
    // なる。1 つの画素に印を付ける範囲が、旧来の頭打ち（pageMin から 4 ページ）で切れず、CPU の参照（半径が覆うすべてのページ）と一致すること
    bool RunWideMarginMarkingCase(const DevicePtr& device, VirtualShadowMapPages& pages, uint64_t& frameSerial)
    {
        // 段の境目の距離をまたぐ置き方では、画素の段が曖昧（Stable でない）になるので、またがない距離を選ぶ
        ReceiverScene receiver;
        Container::VariableArray<float> single;
        bool bPicked = false;
        for (const double cameraDistance : {0.1, 0.14, 0.2, 0.28, 0.4})
        {
            receiver = BuildReceiverScene(device, cameraDistance, 30);
            const Scene& candidate = receiver.Base;
            single.assign(ImageWidth * ImageHeight, 1.0f);
            const int32_t centerPixelX = static_cast<int32_t>(ImageWidth / 2u);
            const int32_t centerPixelY = static_cast<int32_t>(ImageHeight / 2u);
            for (int32_t offsetY = -30; offsetY <= 30 && !bPicked; ++offsetY)
            {
                for (int32_t offsetX = -30; offsetX <= 30 && !bPicked; ++offsetX)
                {
                    const uint32_t pixelX = static_cast<uint32_t>(centerPixelX + offsetX);
                    const uint32_t pixelY = static_cast<uint32_t>(centerPixelY + offsetY);
                    Container::VariableArray<uint32_t> keys;
                    const float depthValue = receiver.Image[pixelY * ImageWidth + pixelX];
                    if (ClassifyPixel(candidate, pixelX, pixelY, depthValue, &keys, nullptr, nullptr) == PixelKind::Stable && keys.size() >= 64u)
                    {
                        single[pixelY * ImageWidth + pixelX] = depthValue;
                        bPicked = true;
                    }
                }
            }
            if (bPicked)
            {
                break;
            }
        }
        const Scene& scene = receiver.Base;
        Expect(bPicked, "ケース C2: 印の範囲が 64 ページ以上になる安定した画素が見つからない（場面が退化している）");
        if (!bPicked)
        {
            return false;
        }
        const Reference reference = BuildReference(scene, single);
        const TexturePtr depth = CreateDepthTexture(device, single);
        Resources resources;
        Readback readback;
        const uint32_t poolPages = static_cast<uint32_t>(reference.Keys.size()) + 8u;
        if (!depth || !CreateResources(device, poolPages, resources) || !RunPages(device, pages, scene, resources, depth, true, frameSerial++, true, readback))
        {
            std::cerr << TestName << " ケース C2 を実行できませんでした" << std::endl;
            return false;
        }
        CheckAllocation("C2", readback, reference.Keys, poolPages, reference.LevelMask, true);
        std::cout << TestName << " ケース C2: 1 画素の印の範囲=" << reference.Keys.size() << " ページ（段の集合 0x" << std::hex << reference.LevelMask << std::dec << "）要求="
                  << readback.Stats[VirtualShadowMap::StatRequested] << std::endl;
        return true;
    }

    bool RunSampleCases(const DevicePtr& device,
                        ShaderManager& shaderManager,
                        VirtualShadowMapPages& pages,
                        VirtualShadowMapRaster& raster,
                        const Scene& scene,
                        const Container::VariableArray<float>& image,
                        const CaseFData& caseF,
                        uint64_t& frameSerial)
    {
        SampleProbe probe;
        if (!CreateSampleProbe(device, shaderManager, probe))
        {
            std::cerr << TestName << " ケース L の計算パイプラインを作れませんでした" << std::endl;
            return false;
        }
        GPUVsmSampleParams realParams;
        const float cameraForward[3] = {scene.Camera.ForwardX, scene.Camera.ForwardY, scene.Camera.ForwardZ};
        if (!BuildVirtualShadowMapSampleParams(&scene.Clipmap, scene.CameraPosition, cameraForward, nullptr, scene.Camera.FieldOfView,
                                               static_cast<float>(ImageHeight), caseF.PoolPages, realParams))
        {
            std::cerr << TestName << " ケース L: 読み出しのパラメータを作れませんでした" << std::endl;
            return false;
        }
        Expect(realParams.control[0] == 1u && realParams.control[1] == scene.Clipmap.LevelCount && realParams.control[2] == caseF.PoolPages,
               "ケース L: 読み出しのパラメータが有効で、段の数・物理ページの数がクリップマップとプールと一致しなければならない");

        // ----- L1: 実際のしきい値。画面の安定した画素の位置・法線で読む -----
        {
            Container::VariableArray<SampleProbePoint> points;
            Container::VariableArray<uint32_t> levels;
            for (uint32_t pixelY = 0; pixelY < ImageHeight; pixelY += 2u)
            {
                for (uint32_t pixelX = 0; pixelX < ImageWidth; pixelX += 2u)
                {
                    uint32_t level = 0;
                    const float depth = image[pixelY * ImageWidth + pixelX];
                    if (ClassifyPixel(scene, pixelX, pixelY, depth, nullptr, nullptr, &level) != PixelKind::Stable)
                    {
                        continue;
                    }
                    double world[3] = {};
                    Unproject(scene, pixelX, pixelY, static_cast<double>(depth), world);
                    SampleProbePoint point = {};
                    point.Position[0] = static_cast<float>(world[0]);
                    point.Position[1] = static_cast<float>(world[1]);
                    point.Position[2] = static_cast<float>(world[2]);
                    point.Position[3] = 1.0f;
                    // 地面（y = 0）は上向き、奥の壁（z = -25）は手前向き
                    const bool bGround = std::abs(world[1]) < 1.0e-3;
                    point.Normal[1] = bGround ? 1.0f : 0.0f;
                    point.Normal[2] = bGround ? 0.0f : 1.0f;
                    points.push_back(point);
                    levels.push_back(level);
                }
            }
            SampleOutput output;
            if (points.size() < 200u || !RunSampleProbe(device, probe, scene.Clipmap, realParams, caseF.Pool, caseF.PageTable, points, output))
            {
                std::cerr << TestName << " ケース L1 を実行できませんでした（点の数 " << points.size() << "）" << std::endl;
                return false;
            }
            uint32_t wrongTexel = 0;
            uint32_t invalidValue = 0;
            uint32_t shadowed = 0;
            uint32_t lit = 0;
            for (size_t index = 0; index < points.size(); ++index)
            {
                const float expectedTexel = scene.Clipmap.Levels[levels[index]].TexelMeters;
                wrongTexel += std::abs(output.TexelMeters[index] - expectedTexel) > expectedTexel * 1.0e-6f ? 1u : 0u;
                invalidValue += !(output.Visibility[index] >= 0.0f && output.Visibility[index] <= 1.0f) ? 1u : 0u;
                shadowed += output.Visibility[index] < 0.5f ? 1u : 0u;
                lit += output.Visibility[index] >= 0.5f ? 1u : 0u;
            }
            std::cout << TestName << " ケース L1: 点=" << points.size() << " 逃げた標本=" << output.Fallback << " 段の不一致=" << wrongTexel
                      << " 影の点=" << shadowed << " 光の点=" << lit << std::endl;
            Expect(output.Fallback == 0u, "ケース L1: 印を付けた段のページがそのまま読め、粗い段へ逃げてはならない（印付けと読み出しの段・ページが一致する）");
            Expect(wrongTexel == 0u, "ケース L1: 使った段の texel の一辺が CPU の選んだ段と一致しなければならない");
            Expect(invalidValue == 0u, "ケース L1: 可視度が [0, 1] でなければならない");
            // ケース F の四角形のページの上の受け手は、四角形の後ろにあれば影になり、ほかは光が当たる（どちらも現れる場面）
            Expect(lit > 0u, "ケース L1: 光が当たる点があるはず（場面が退化している）");
        }

        // ----- L2・L3: 段をケース F の段に固定し、四角形の後ろの受け手を読む -----
        const uint32_t level = caseF.Level;
        Expect(level + 1u < scene.Clipmap.LevelCount, "ケース L: ケース F の段の 1 段粗い段がなければならない（シーンが退化している）");
        if (level + 1u >= scene.Clipmap.LevelCount || caseF.Shapes.size() < 2u)
        {
            return false;
        }
        const double pageMeters = static_cast<double>(scene.Clipmap.Levels[level].PageMeters);
        const double texel = static_cast<double>(scene.Clipmap.Levels[level].TexelMeters);
        const double boundaryX = static_cast<double>(caseF.PageX + 1) * pageMeters;
        const double bottomY = static_cast<double>(caseF.PageY) * pageMeters;
        const double depthCenter = scene.Clipmap.DepthCenter;
        // 近い四角形（中心 - 50 の周りで傾きによる ±1.2）・遠い四角形（中心 - 40）のどちらよりも後ろ。深度の差は最大でも約 21.2 m で、
        // 物理の半影（× 太陽の角半径の tan）が最小の半径（2 texel）を超えない深さにする（L2 の参照は半径 2 texel の PCF）
        const double receiverDepth = depthCenter - 30.0;
        const double radius = 2.0 * texel;
        Expect(21.2 * static_cast<double>(VirtualShadowMap::SUN_TAN_ANGULAR_RADIUS) < radius,
               "ケース L2: 物理の半影が最小の半径（2 texel）を超えない場面でなければならない（参照の半径が 2 texel のため）");

        auto evaluate = [&](double lightX, double lightY, const Container::VariableArray<uint32_t>& pool, const Container::VariableArray<uint32_t>& table,
                            float& outVisibility, uint32_t& outFallback, double expectedTexel = -1.0) -> bool
        {
            const SampleProbePoint receiver = MakeReceiver(scene, lightX, lightY, receiverDepth);
            Container::VariableArray<SampleProbePoint> points;
            points.push_back(receiver);
            SampleOutput output;
            if (!RunSampleProbe(device, probe, scene.Clipmap, MakeForcedLevelParams(realParams, level, texel, receiver), pool, table, points, output))
            {
                return false;
            }
            const double wantedTexel = expectedTexel > 0.0 ? expectedTexel : texel;
            Expect(std::abs(static_cast<double>(output.TexelMeters[0]) - wantedTexel) < wantedTexel * 1.0e-6,
                   "ケース L2・L3: 実際に読んだ段（読めた標本がなければ選んだ段）の texel の一辺を返さなければならない");
            outVisibility = output.Visibility[0];
            outFallback = output.Fallback;
            return true;
        };

        const double centerY = bottomY + 0.4425 * pageMeters;
        const Shape& nearQuad = caseF.Shapes[0];

        // L2-中心: 近い四角形だけに覆われる位置（遠い四角形の縁から 0.1 ページ、近い四角形の左の縁から 0.1 ページ以上）。PCF の標本がすべて形の内側 → 0
        {
            float visibility = -1.0f;
            uint32_t fallback = 0;
            bool bAmbiguous = false;
            const double lightX = boundaryX - 0.2068 * pageMeters;
            const double expected = ReferenceVisibility(caseF.Shapes, lightX, centerY, radius, texel, bAmbiguous);
            if (!evaluate(lightX, centerY, caseF.Pool, caseF.PageTable, visibility, fallback))
            {
                return false;
            }
            std::cout << TestName << " ケース L2 中心: 可視度=" << visibility << " 参照=" << expected << " 逃げた標本=" << fallback << std::endl;
            Expect(!bAmbiguous && expected == 0.0, "ケース L2 中心: 参照が影の中心（0）でなければならない");
            Expect(std::abs(visibility - 0.0f) < 1.0e-5f, "ケース L2 中心: 影の中心で 0 でなければならない");
            Expect(fallback == 0u, "ケース L2 中心: 割り当て済みのページを読み、逃げてはならない");
        }

        // L2-外: 近い四角形の左の縁から 0.15 ページ左（同じページ）。標本はすべて形の外側 → 1
        {
            float visibility = -1.0f;
            uint32_t fallback = 0;
            bool bAmbiguous = false;
            const double lightX = nearQuad.MinX - 0.15 * pageMeters;
            const double expected = ReferenceVisibility(caseF.Shapes, lightX, centerY, radius, texel, bAmbiguous);
            if (!evaluate(lightX, centerY, caseF.Pool, caseF.PageTable, visibility, fallback))
            {
                return false;
            }
            std::cout << TestName << " ケース L2 外: 可視度=" << visibility << " 参照=" << expected << " 逃げた標本=" << fallback << std::endl;
            Expect(!bAmbiguous && expected == 1.0, "ケース L2 外: 参照が影の外（1）でなければならない");
            Expect(std::abs(visibility - 1.0f) < 1.0e-5f, "ケース L2 外: 影の外で 1 でなければならない");
            Expect(fallback == 0u, "ケース L2 外: 割り当て済みのページを読み、逃げてはならない");
        }

        // L2-縁: 近い四角形の左の縁の上。PCF の標本が読む texel の中心が縁をまたぐ位置を、結果があいまいな標本が無いように刻んで探す。0 と 1 の間で CPU の参照と一致
        {
            bool bFound = false;
            double edgeX = 0.0;
            double expected = 0.0;
            for (const double shift : {0.0, 0.37, -0.41, 0.83, -0.77, 1.31, -1.29, 1.73, -1.67, 0.19, -0.23})
            {
                const double candidate = nearQuad.MinX + shift * texel;
                bool bAmbiguous = false;
                const double reference = ReferenceVisibility(caseF.Shapes, candidate, centerY, radius, texel, bAmbiguous);
                if (!bAmbiguous && reference > 0.0 && reference < 1.0)
                {
                    bFound = true;
                    edgeX = candidate;
                    expected = reference;
                    break;
                }
            }
            Expect(bFound, "ケース L2 縁: 標本が縁をまたぎ、texel の境界・縁に近すぎる標本が無い位置が見つからない（シーンが退化している）");
            if (bFound)
            {
                float visibility = -1.0f;
                uint32_t fallback = 0;
                if (!evaluate(edgeX, centerY, caseF.Pool, caseF.PageTable, visibility, fallback))
                {
                    return false;
                }
                std::cout << TestName << " ケース L2 縁: 可視度=" << visibility << " 参照=" << expected << " 逃げた標本=" << fallback << std::endl;
                Expect(visibility > 0.0f && visibility < 1.0f, "ケース L2 縁: 縁で 0 と 1 の間の値でなければならない");
                Expect(std::abs(static_cast<double>(visibility) - expected) < 1.0e-5, "ケース L2 縁: 値が CPU の参照（形の内側の標本の数）と一致しなければならない");
                Expect(fallback == 0u, "ケース L2 縁: 割り当て済みのページを読み、逃げてはならない");
            }
        }

        // L2-境界: ページ A・B の境界から 1 texel 手前（標本の半径は 2 texel なので、標本が隣のページ B を読む）。近い四角形が両方のページを覆うので 0
        {
            float visibility = -1.0f;
            uint32_t fallback = 0;
            bool bAmbiguous = false;
            const double lightX = boundaryX - texel;
            const double lightY = bottomY + 0.30 * pageMeters;
            const double expected = ReferenceVisibility(caseF.Shapes, lightX, lightY, radius, texel, bAmbiguous);
            if (!evaluate(lightX, lightY, caseF.Pool, caseF.PageTable, visibility, fallback))
            {
                return false;
            }
            std::cout << TestName << " ケース L2 境界: 可視度=" << visibility << " 参照=" << expected << " 逃げた標本=" << fallback << std::endl;
            Expect(!bAmbiguous && expected == 0.0, "ケース L2 境界: 参照が影の中心（0）でなければならない");
            Expect(std::abs(visibility - 0.0f) < 1.0e-5f, "ケース L2 境界: 隣のページを読む標本も含めて 0 でなければならない");
            Expect(fallback == 0u, "ケース L2 境界: 隣のページも割り当て済みなので、逃げてはならない");
        }

        // ----- L3: 割り当てられていないページ -----
        // 受け手は L2-外 の位置（本来は光が当たり、可視度 1）。そのページ（段 level）を割り当て外にする
        {
            const double lightX = nearQuad.MinX - 0.15 * pageMeters;
            const int64_t pageX = static_cast<int64_t>(std::floor(lightX / pageMeters));
            const int64_t pageY = static_cast<int64_t>(std::floor(centerY / pageMeters));
            Container::VariableArray<uint32_t> table = caseF.PageTable;
            const uint32_t fineKey = PageKey(level, pageX, pageY);
            Expect((table[fineKey] & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) != 0u, "ケース L3: 受け手のページが（ケース F で）割り当て済みでなければならない");
            table[fineKey] = 0u;

            // 粗い段（level + 1 以上）の、同じ位置のページ。どの段にも無い場面のために、すべて割り当て外にする
            Container::VariableArray<uint32_t> coarseKeys;
            for (uint32_t coarse = level + 1u; coarse < scene.Clipmap.LevelCount; ++coarse)
            {
                const double coarsePageMeters = static_cast<double>(scene.Clipmap.Levels[coarse].PageMeters);
                const uint32_t key = PageKey(coarse, static_cast<int64_t>(std::floor(lightX / coarsePageMeters)),
                                             static_cast<int64_t>(std::floor(centerY / coarsePageMeters)));
                coarseKeys.push_back(key);
                table[key] = 0u;
            }
            const uint32_t coarseKey = coarseKeys[0];

            // 割り当てに使われていない物理ページ（ケース F のプールは割り当てたページ + 24 ページ）
            Container::VariableArray<bool> used(caseF.PoolPages, false);
            for (const uint32_t entry : table)
            {
                if ((entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) != 0u && (entry & VirtualShadowMap::PAGE_INDEX_MASK) < caseF.PoolPages)
                {
                    used[entry & VirtualShadowMap::PAGE_INDEX_MASK] = true;
                }
            }
            uint32_t spare = caseF.PoolPages;
            for (uint32_t index = 0; index < caseF.PoolPages; ++index)
            {
                if (!used[index] && index != (caseF.PageTable[fineKey] & VirtualShadowMap::PAGE_INDEX_MASK))
                {
                    spare = index;
                    break;
                }
            }
            Expect(spare < caseF.PoolPages, "ケース L3: 使われていない物理ページがなければならない");
            if (spare >= caseF.PoolPages)
            {
                return false;
            }

            // L3-粗い段に割り当てあり: 物理ページの全 texel を、受け手より十分手前の深度（中心 - 50 m）にする → 粗い段の値 0
            Container::VariableArray<uint32_t> pool = caseF.Pool;
            const float blockerDepth01 = static_cast<float>(-50.0 * 0.5 / static_cast<double>(scene.Clipmap.Settings.DepthRangeMeters) + 0.5);
            uint32_t blockerWord = 0;
            std::memcpy(&blockerWord, &blockerDepth01, sizeof(blockerWord));
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                pool[static_cast<size_t>(spare) * VirtualShadowMap::PAGE_WORDS + word] = blockerWord;
            }
            Container::VariableArray<uint32_t> tableWithCoarse = table;
            tableWithCoarse[coarseKey] = VirtualShadowMap::PAGE_ENTRY_ALLOCATED | VirtualShadowMap::PAGE_ENTRY_DIRTY | spare;

            float visibility = -1.0f;
            uint32_t fallback = 0;
            const double coarseTexel = static_cast<double>(scene.Clipmap.Levels[level + 1u].TexelMeters);
            if (!evaluate(lightX, centerY, pool, tableWithCoarse, visibility, fallback, coarseTexel))
            {
                return false;
            }
            std::cout << TestName << " ケース L3 粗い段あり: 可視度=" << visibility << " 逃げた標本=" << fallback << std::endl;
            Expect(std::abs(visibility - 0.0f) < 1.0e-5f, "ケース L3: 自分の段のページが無いとき、粗い段の値（この場面では 0）を読まなければならない");
            Expect(fallback == 16u, "ケース L3: 逃げた標本の数が 16 でなければならない");

            // L3-どの段にも無い: 影なし（1）
            if (!evaluate(lightX, centerY, pool, table, visibility, fallback))
            {
                return false;
            }
            std::cout << TestName << " ケース L3 どの段にも無い: 可視度=" << visibility << " 逃げた標本=" << fallback << std::endl;
            Expect(std::abs(visibility - 1.0f) < 1.0e-5f, "ケース L3: どの段にもページが無いとき、影なし（1）でなければならない");
            Expect(fallback == 16u, "ケース L3: どの段にも無い標本も、逃げた標本として 16 と数えなければならない");
        }

        // ----- L4: 影の距離の範囲と奥の薄め（CSM と同じ前方への距離） -----
        {
            // CSM の分割（0.1, 5, 15, 40, 80）から、範囲 [0.1, 80]・薄めの幅 = 最後のカスケードの幅（40）の 10% = 4 m を作る
            const float splits[5] = {0.1f, 5.0f, 15.0f, 40.0f, 80.0f};
            GPUVsmSampleParams splitParams;
            Expect(BuildVirtualShadowMapSampleParams(&scene.Clipmap, scene.CameraPosition, cameraForward, splits, scene.Camera.FieldOfView,
                                                     static_cast<float>(ImageHeight), caseF.PoolPages, splitParams),
                   "ケース L4: 分割の距離つきで読み出しのパラメータを作れなければならない");
            Expect(std::abs(splitParams.range[0] - 0.1f) < 1.0e-6f && std::abs(splitParams.range[1] - 80.0f) < 1.0e-6f &&
                       std::abs(splitParams.range[2] - 4.0f) < 1.0e-5f,
                   "ケース L4: 影の距離の範囲・薄めの幅が CSM の分割（最初・最後・最後のカスケードの幅の 10%）から決まらなければならない");
            // 分割が使えないとき（増加しない）は、設定の最大の距離と割合から決まる
            const float brokenSplits[5] = {0.1f, 5.0f, 5.0f, 40.0f, 80.0f};
            GPUVsmSampleParams settingsParams;
            Expect(BuildVirtualShadowMapSampleParams(&scene.Clipmap, scene.CameraPosition, cameraForward, brokenSplits, scene.Camera.FieldOfView,
                                                     static_cast<float>(ImageHeight), caseF.PoolPages, settingsParams),
                   "ケース L4: 不正な分割でも設定から読み出しのパラメータを作れなければならない");
            const float maxDistance = scene.Clipmap.Settings.MaxShadowDistance;
            Expect(settingsParams.range[0] == 0.0f && std::abs(settingsParams.range[1] - maxDistance) < 1.0e-4f &&
                       std::abs(settingsParams.range[2] - maxDistance * scene.Clipmap.Settings.FadeRatio) < 1.0e-4f,
                   "ケース L4: 分割が使えないときは [0, 最大の距離]・最大の距離 × 割合で薄めなければならない");
            const float zeroForward[3] = {0.0f, 0.0f, 0.0f};
            GPUVsmSampleParams rejected;
            Expect(!BuildVirtualShadowMapSampleParams(&scene.Clipmap, scene.CameraPosition, zeroForward, splits, scene.Camera.FieldOfView,
                                                      static_cast<float>(ImageHeight), caseF.PoolPages, rejected) &&
                       rejected.control[0] == 0u,
                   "ケース L4: カメラの前方が 0 なら無効のパラメータ（control.x = 0）を返さなければならない");

            // 影の中心（L2-中心と同じ位置。距離に依らず可視度 0）を、カメラを動かして読む
            const double lightX = boundaryX - 0.2068 * pageMeters;
            const SampleProbePoint receiver = MakeReceiver(scene, lightX, centerY, receiverDepth);
            const double forwardX[3] = {-1.0, 0.0, 0.0};
            auto readAt = [&](const double offset[3], float& outVisibility) -> bool
            {
                Container::VariableArray<SampleProbePoint> points;
                points.push_back(receiver);
                SampleOutput output;
                if (!RunSampleProbe(device, probe, scene.Clipmap, MakeDistanceParams(splitParams, level, texel, receiver, offset, forwardX, splitParams.range[0], splitParams.range[1], splitParams.range[2]),
                                    caseF.Pool, caseF.PageTable, points, output))
                {
                    return false;
                }
                outVisibility = output.Visibility[0];
                return true;
            };
            struct DistanceCase
            {
                const char* Name;
                double Offset[3];
                double Expected;
            };
            // 前方への距離 74 m は薄めの手前（76 m 以降）なので影のまま（最大の距離全体の 10% = 8 m で薄めると 0.156 になる）。78 m は薄めの真ん中（0.5）。
            // 前方への距離 70 m・直線距離 80.6 m は、直線で測ると範囲の外になるが、CSM と同じ前方への距離では範囲の内側で影のまま
            const DistanceCase cases[] = {
                {"前方 74 m（薄めの手前）", {74.0, 0.0, 0.0}, 0.0},
                {"前方 78 m（薄めの真ん中）", {78.0, 0.0, 0.0}, 0.5},
                {"前方 81 m（最大の距離の外）", {81.0, 0.0, 0.0}, 1.0},
                {"カメラの後ろ（前方への距離が負）", {-5.0, 0.0, 0.0}, 1.0},
                {"最小の距離の手前（0.05 m）", {0.05, 0.0, 0.0}, 1.0},
                {"前方 70 m・直線 80.6 m（斜め）", {70.0, 40.0, 0.0}, 0.0},
            };
            for (const DistanceCase& distanceCase : cases)
            {
                float visibility = -1.0f;
                if (!readAt(distanceCase.Offset, visibility))
                {
                    return false;
                }
                std::cout << TestName << " ケース L4 " << distanceCase.Name << ": 可視度=" << visibility << " 期待=" << distanceCase.Expected << std::endl;
                Expect(std::abs(static_cast<double>(visibility) - distanceCase.Expected) < 1.0e-4,
                       "ケース L4: 影の距離の範囲・奥の薄めが CSM と同じ前方への距離で決まらなければならない");
            }
        }

        return RunPenumbraCase(device, pages, raster, probe, frameSerial) &&
               RunObliqueReceiverCase(device, pages, raster, probe, frameSerial);
    }

    // ========================================
    // ページのキャッシュ（前フレームのページの表の引き継ぎ・持ち越し・無効化）
    // ========================================

    // 1 フレーム分の結果
    struct CacheFrame
    {
        RasterReadback Readback;
        Container::VariableArray<PageInfo> Infos;
        Container::VariableArray<uint32_t> FreeList;
        bool bContinued = false;
        bool bInvalidatedAll = false;
        uint32_t RectCount = 0;

        uint32_t Stat(VirtualShadowMap::StatWord word) const { return Readback.Stats[word]; }
        uint32_t DirtyPages() const
        {
            uint32_t count = 0;
            for (const PageInfo& page : Infos)
            {
                count += page.bDirty ? 1u : 0u;
            }
            return count;
        }
    };

    // 塊の記録 1 つぶんの、フレームをまたぐ動きの入力（鍵 = 塊の番号、署名 = 境界・変換、境界 = 塊の境界）
    Container::VariableArray<VirtualShadowMap::CasterMotionEntry> BuildMotionEntries(const ChunkGeometry& geometry)
    {
        Container::VariableArray<VirtualShadowMap::CasterMotionEntry> entries;
        for (size_t index = 0; index < geometry.Chunks.size(); ++index)
        {
            const VsmShadowChunk& chunk = geometry.Chunks[index];
            VirtualShadowMap::CasterMotionEntry entry;
            entry.Key = static_cast<uint64_t>(index) + 1u;
            uint64_t signature = VirtualShadowMap::CasterHashFloats(1469598103934665603ull, chunk.BoundsMin, 3u);
            signature = VirtualShadowMap::CasterHashFloats(signature, chunk.BoundsMax, 3u);
            signature = VirtualShadowMap::CasterHashFloats(signature, chunk.World, 12u);
            entry.Signature = signature;
            entry.bHasBounds = true;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                entry.Bounds.Min[axis] = chunk.BoundsMin[axis];
                entry.Bounds.Max[axis] = chunk.BoundsMax[axis];
            }
            entries.push_back(entry);
        }
        return entries;
    }

    // キャッシュを使って、印付け → 割り当て → 消去 → 展開 → 描画を 1 フレーム分走らせる。tracker が null ならキャッシュを使わない（--vsm-cache=off 相当）。
    // useTracker が true で tracker の無効化の入力が要らないときは、無効化の矩形を渡さない（動いた物が無いフレーム）
    bool RunCacheFrame(const DevicePtr& device,
                       VirtualShadowMapPages& pages,
                       VirtualShadowMapRaster& raster,
                       const Scene& scene,
                       const Resources& resources,
                       const ChunkGeometry& geometry,
                       const RasterBuffers& rasterBuffers,
                       const TexturePtr& depth,
                       VirtualShadowMap::CasterMotionTracker* tracker,
                       uint64_t frameSerial,
                       CacheFrame& out)
    {
        CacheInput input;
        input.bEnabled = tracker != nullptr;
        Container::VariableArray<float> rects;
        if (tracker != nullptr)
        {
            Container::VariableArray<VirtualShadowMap::CasterBounds> changed;
            bool bInvalidateAll = false;
            tracker->Update(BuildMotionEntries(geometry), changed, bInvalidateAll);
            if (!bInvalidateAll && !VirtualShadowMap::BuildInvalidationRects(scene.Clipmap, changed, VirtualShadowMap::MAX_INVALIDATION_RECTS, rects))
            {
                bInvalidateAll = true;
                rects.clear();
            }
            input.Rects = rects.empty() ? nullptr : rects.data();
            input.RectCount = static_cast<uint32_t>(rects.size() / 4u);
            input.bInvalidateAll = bInvalidateAll;
            out.RectCount = input.RectCount;
        }
        if (!RunRaster(device, &pages, raster, scene, resources, rasterBuffers, depth, frameSerial, out.Readback, &input))
        {
            return false;
        }
        out.Infos = DecodePages(scene, out.Readback.PageTable);
        out.FreeList = out.Readback.FreeList;
        out.bContinued = pages.WasCacheContinued();
        out.bInvalidatedAll = pages.WasInvalidatedAll();
        return out.Readback.bPagesRecorded;
    }

    const PageInfo* FindPage(const Container::VariableArray<PageInfo>& infos, uint32_t level, int64_t absX, int64_t absY)
    {
        for (const PageInfo& page : infos)
        {
            if (page.Level == level && page.AbsX == absX && page.AbsY == absY)
            {
                return &page;
            }
        }
        return nullptr;
    }

    // 2 つの結果が、同じ（段・絶対のページ）の集合を持ち、全 texel の語が一致する（物理ページの番号は違ってよい）。違った語の数を返す（集合が違えば最大）
    uint32_t CountPoolDifferences(const CacheFrame& cached, const CacheFrame& uncached)
    {
        if (cached.Infos.size() != uncached.Infos.size())
        {
            return 0xFFFFFFFFu;
        }
        uint32_t different = 0;
        for (const PageInfo& page : cached.Infos)
        {
            const PageInfo* counterpart = FindPage(uncached.Infos, page.Level, page.AbsX, page.AbsY);
            if (counterpart == nullptr)
            {
                return 0xFFFFFFFFu;
            }
            const size_t baseCached = static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS;
            const size_t baseUncached = static_cast<size_t>(counterpart->Physical) * VirtualShadowMap::PAGE_WORDS;
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                different += cached.Readback.Pool[baseCached + word] != uncached.Readback.Pool[baseUncached + word] ? 1u : 0u;
            }
        }
        return different;
    }

    // 同じ場面を、キャッシュを使わない別の記録で描き直し、キャッシュを使った結果と全 texel で比べる。違った語の数を返す
    uint32_t CompareWithUncachedFrame(const DevicePtr& device,
                                      VirtualShadowMapPages& uncachedPages,
                                      VirtualShadowMapRaster& raster,
                                      const Scene& scene,
                                      uint32_t poolPages,
                                      const ChunkGeometry& geometry,
                                      const RasterBuffers& rasterBuffers,
                                      const TexturePtr& depth,
                                      const CacheFrame& cached,
                                      uint64_t frameSerial)
    {
        Resources resources;
        CacheFrame uncached;
        if (!CreateResources(device, poolPages, resources) ||
            !RunCacheFrame(device, uncachedPages, raster, scene, resources, geometry, rasterBuffers, depth, nullptr, frameSerial, uncached))
        {
            return 0xFFFFFFFFu;
        }
        return CountPoolDifferences(cached, uncached);
    }

    // ワールドの境界（最小・最大）のライト空間の矩形（margin だけ広げる）が覆うページに、page が入るか。展開・無効化と同じ floor の範囲
    bool PageOverlapsBounds(const Scene& scene, const PageInfo& page, const float (&boundsMin)[3], const float (&boundsMax)[3], double margin)
    {
        VirtualShadowMap::CasterBounds bounds;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            bounds.Min[axis] = boundsMin[axis];
            bounds.Max[axis] = boundsMax[axis];
        }
        double lightMin[2] = {};
        double lightMax[2] = {};
        if (!VirtualShadowMap::LightSpaceRect(scene.Clipmap, bounds, lightMin, lightMax))
        {
            return false;
        }
        const double pageMeters = static_cast<double>(scene.Clipmap.Levels[page.Level].PageMeters);
        const int64_t minX = static_cast<int64_t>(std::floor((lightMin[0] - margin) / pageMeters));
        const int64_t maxX = static_cast<int64_t>(std::floor((lightMax[0] + margin) / pageMeters));
        const int64_t minY = static_cast<int64_t>(std::floor((lightMin[1] - margin) / pageMeters));
        const int64_t maxY = static_cast<int64_t>(std::floor((lightMax[1] + margin) / pageMeters));
        return page.AbsX >= minX && page.AbsX <= maxX && page.AbsY >= minY && page.AbsY <= maxY;
    }

    // 形（ライト空間）を、ページ (level, pageX, pageY) とその右のページの境界をまたぐ近い四角形と遠い四角形にする。offsetPages だけ x へ動かす
    Container::VariableArray<Shape> MakeCacheShapes(const Scene& scene, uint32_t level, int64_t pageX, int64_t pageY, double offsetPages)
    {
        const double depthCenter = scene.Clipmap.DepthCenter;
        const double pageMeters = static_cast<double>(scene.Clipmap.Levels[level].PageMeters);
        const double boundaryX = static_cast<double>(pageX + 1) * pageMeters + offsetPages * pageMeters;
        const double bottomY = static_cast<double>(pageY) * pageMeters;
        Container::VariableArray<Shape> shapes;
        Shape nearQuad = MakeRect(boundaryX - 0.3137 * pageMeters, boundaryX + 0.2713 * pageMeters, bottomY + 0.2231 * pageMeters, bottomY + 0.6619 * pageMeters);
        SetPlane(nearQuad, boundaryX, bottomY, depthCenter - 50.0, 0.5, -0.25);
        shapes.push_back(nearQuad);
        Shape farQuad = MakeRect(boundaryX - 0.1 * pageMeters, boundaryX + 0.6 * pageMeters, bottomY + 0.4 * pageMeters, bottomY + 0.9 * pageMeters);
        SetPlane(farQuad, boundaryX, bottomY, depthCenter - 40.0, 0.0, 0.0);
        farQuad.bLocalTransform = true;
        shapes.push_back(farQuad);
        return shapes;
    }

    // 形の塊と、その頂点・インデックス・塊・展開の出力のバッファを作る
    bool PrepareCacheGeometry(const DevicePtr& device,
                              const Scene& scene,
                              const Container::VariableArray<Shape>& shapes,
                              ChunkGeometry& geometry,
                              RasterBuffers& rasterBuffers)
    {
        geometry = BuildChunks(scene, shapes);
        rasterBuffers = RasterBuffers{};
        return CreateRasterBuffers(device, geometry, 4096u, rasterBuffers);
    }

    // 段 level の安定した画素だけを残した深度の画像（ほかは空）。段ごとに要求の集合を分けるのに使う
    Container::VariableArray<float> BuildLevelImage(const Scene& scene, const Container::VariableArray<float>& image, uint32_t level)
    {
        Container::VariableArray<float> result(ImageWidth * ImageHeight, 1.0f);
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                uint32_t pixelLevel = 0;
                const float depthValue = image[pixelY * ImageWidth + pixelX];
                if (ClassifyPixel(scene, pixelX, pixelY, depthValue, nullptr, nullptr, &pixelLevel) == PixelKind::Stable && pixelLevel == level)
                {
                    result[pixelY * ImageWidth + pixelX] = depthValue;
                }
            }
        }
        return result;
    }

    // 絶対のページ (level, absX, absY) の集合の中の要素の数
    uint32_t CountAllocated(const CacheFrame& frame)
    {
        return static_cast<uint32_t>(frame.Infos.size());
    }

    bool RunCacheCases(const DevicePtr& device,
                       ShaderManager& shaderManager,
                       VirtualShadowMapRaster& raster,
                       const Scene& scene,
                       const Reference& reference,
                       const Container::VariableArray<float>& image,
                       const TexturePtr& depth,
                       uint64_t& frameSerial)
    {
        VirtualShadowMapPages cachedPages;
        VirtualShadowMapPages uncachedPages;
        if (!cachedPages.Initialize(device.get(), &shaderManager) || !uncachedPages.Initialize(device.get(), &shaderManager))
        {
            std::cerr << TestName << " ケース M: ページのパイプラインを初期化できませんでした" << std::endl;
            return false;
        }
        uint32_t level = 0;
        int64_t pageX = 0;
        int64_t pageY = 0;
        if (!FindAdjacentPages(scene, reference.Keys, level, pageX, pageY))
        {
            std::cerr << TestName << " ケース M: 横に隣り合うページが参照に無い" << std::endl;
            return false;
        }
        const uint32_t requested = static_cast<uint32_t>(reference.Keys.size());
        const uint32_t poolPages = requested + 24u;
        const double pageMeters = static_cast<double>(scene.Clipmap.Levels[level].PageMeters);
        const double margin = 1.0e-3;

        Resources resources;
        if (!CreateResources(device, poolPages, resources))
        {
            std::cerr << TestName << " ケース M: 資源を作れませんでした" << std::endl;
            return false;
        }
        VirtualShadowMap::CasterMotionTracker tracker;

        const Container::VariableArray<Shape> shapesA = MakeCacheShapes(scene, level, pageX, pageY, 0.0);
        ChunkGeometry geometryA;
        RasterBuffers buffersA;
        if (!PrepareCacheGeometry(device, scene, shapesA, geometryA, buffersA))
        {
            std::cerr << TestName << " ケース M: 形のバッファを作れませんでした" << std::endl;
            return false;
        }

        // ----- ケース M1: 最初のフレームはすべて割り当てて描く -----
        CacheFrame frame1;
        if (!RunCacheFrame(device, cachedPages, raster, scene, resources, geometryA, buffersA, depth, &tracker, frameSerial++, frame1))
        {
            std::cerr << TestName << " ケース M1 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(!frame1.bContinued, "ケース M1: 最初のフレームは前フレームの表を引き継がない");
        Expect(CountAllocated(frame1) == requested, "ケース M1: 要求のページがすべて割り当て済みでなければならない");
        Expect(frame1.Stat(VirtualShadowMap::StatRendered) == requested && frame1.Stat(VirtualShadowMap::StatCached) == 0u,
               "ケース M1: 最初のフレームは全ページを描き、持ち越しは 0 でなければならない");
        {
            const PoolCheck check = CheckPool("ケース M1", scene, shapesA, frame1.Infos, frame1.Readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
            Expect(check.Mismatches == 0u && check.Covered > 2000u, "ケース M1: 物理ページが形の和の参照と一致しなければならない");
        }

        // ----- ケース M2: 止まった場面の 2 フレーム目は、描かれるページが 0 -----
        CacheFrame frame2;
        if (!RunCacheFrame(device, cachedPages, raster, scene, resources, geometryA, buffersA, depth, &tracker, frameSerial++, frame2))
        {
            std::cerr << TestName << " ケース M2 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame2.bContinued && !frame2.bInvalidatedAll && frame2.RectCount == 0u, "ケース M2: 止まった場面は前フレームの表を引き継ぎ、無効にしない");
        Expect(frame2.Stat(VirtualShadowMap::StatRendered) == 0u && frame2.Stat(VirtualShadowMap::StatCached) == requested &&
                   frame2.Stat(VirtualShadowMap::StatInvalidated) == 0u && frame2.Stat(VirtualShadowMap::StatReleased) == 0u,
               "ケース M2: 止まった場面の 2 フレーム目に描かれるページが 0、持ち越しが要求の数でなければならない");
        Expect(frame2.Readback.Stats[VirtualShadowMap::StatRasterInstances] == 0u,
               "ケース M2: 展開が dirty のページを持たないので、描くインスタンスが 0 でなければならない");
        Expect(frame2.DirtyPages() == 0u && CountAllocated(frame2) == requested, "ケース M2: ページの表に dirty が無く、割り当ては残っていなければならない");
        Expect(frame2.Readback.Pool == frame1.Readback.Pool, "ケース M2: 持ち越したページの物理プールの中身が変わってはならない");
        for (const PageInfo& page : frame1.Infos)
        {
            const PageInfo* same = FindPage(frame2.Infos, page.Level, page.AbsX, page.AbsY);
            Expect(same != nullptr && same->Physical == page.Physical, "ケース M2: 持ち越したページが同じ物理ページを保たなければならない");
        }
        std::cout << TestName << " ケース M2: 持ち越し=" << frame2.Stat(VirtualShadowMap::StatCached) << " 描いたページ=" << frame2.Stat(VirtualShadowMap::StatRendered) << std::endl;

        // ----- ケース M2b: 止まった場面を長く続けても、要求のあるページは空きへ戻らない（持ち越しの条件が要求の印を見ること） -----
        {
            bool bStable = true;
            for (uint32_t index = 0; index < VirtualShadowMap::CACHE_CARRY_FRAMES + 5u && bStable; ++index)
            {
                CacheFrame frame;
                if (!RunCacheFrame(device, cachedPages, raster, scene, resources, geometryA, buffersA, depth, &tracker, frameSerial++, frame))
                {
                    std::cerr << TestName << " ケース M2b を実行できませんでした" << std::endl;
                    return false;
                }
                bStable = frame.bContinued && frame.Stat(VirtualShadowMap::StatRendered) == 0u && frame.Stat(VirtualShadowMap::StatReleased) == 0u &&
                          frame.Stat(VirtualShadowMap::StatCached) == requested && CountAllocated(frame) == requested;
                if (!bStable)
                {
                    std::cerr << TestName << " ケース M2b: " << index + 3u << " フレーム目: 描いた=" << frame.Stat(VirtualShadowMap::StatRendered)
                              << " 戻した=" << frame.Stat(VirtualShadowMap::StatReleased) << " 持ち越し=" << frame.Stat(VirtualShadowMap::StatCached)
                              << " 割り当て=" << CountAllocated(frame) << std::endl;
                }
            }
            Expect(bStable, "ケース M2b: 要求が続くページは、持ち越しの上限を超えても空きへ戻らず、描き直されてもならない");
        }

        // ----- ケース M3: 投影物を動かすと、その範囲（前フレームと今フレームの境界）のページだけが描き直される -----
        // 3a は同じページの中での小さな動き、3b は別のページへ出る大きな動き（元の場所のページが古い形のまま残らないこと）
        const double moves[2] = {0.4, 1.7};
        const char* const moveNames[2] = {"M3a", "M3b"};
        Container::VariableArray<Shape> previousShapes = shapesA;
        ChunkGeometry previousGeometry = geometryA;
        for (uint32_t moveIndex = 0; moveIndex < 2u; ++moveIndex)
        {
            const Container::VariableArray<Shape> movedShapes = MakeCacheShapes(scene, level, pageX, pageY, moves[moveIndex]);
            ChunkGeometry movedGeometry;
            RasterBuffers movedBuffers;
            if (!PrepareCacheGeometry(device, scene, movedShapes, movedGeometry, movedBuffers))
            {
                std::cerr << TestName << " ケース " << moveNames[moveIndex] << " の形のバッファを作れませんでした" << std::endl;
                return false;
            }
            CacheFrame frame;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, movedGeometry, movedBuffers, depth, &tracker, frameSerial++, frame))
            {
                std::cerr << TestName << " ケース " << moveNames[moveIndex] << " を実行できませんでした" << std::endl;
                return false;
            }
            Expect(frame.bContinued && !frame.bInvalidatedAll && frame.RectCount == previousGeometry.Chunks.size() * 2u,
                   "ケース M3: 動いた塊ごとに、前フレームと今フレームの境界の 2 つの矩形で無効にしなければならない");
            // 期待: 割り当て済みのページのうち、前フレームの境界か今フレームの境界のライト空間の矩形に入るもの
            uint32_t expectedDirty = 0;
            uint32_t actualDirty = 0;
            bool bSetMatches = true;
            for (const PageInfo& page : frame.Infos)
            {
                bool bExpected = false;
                for (size_t chunk = 0; chunk < movedGeometry.Chunks.size() && !bExpected; ++chunk)
                {
                    bExpected = PageOverlapsBounds(scene, page, previousGeometry.Chunks[chunk].BoundsMin, previousGeometry.Chunks[chunk].BoundsMax, margin) ||
                                PageOverlapsBounds(scene, page, movedGeometry.Chunks[chunk].BoundsMin, movedGeometry.Chunks[chunk].BoundsMax, margin);
                }
                expectedDirty += bExpected ? 1u : 0u;
                actualDirty += page.bDirty ? 1u : 0u;
                bSetMatches = bSetMatches && bExpected == page.bDirty;
            }
            Expect(bSetMatches, "ケース M3: dirty のページが、動いた塊の前後の境界が覆うページと一致しなければならない");
            Expect(actualDirty > 0u && actualDirty < CountAllocated(frame), "ケース M3: 一部のページだけが描き直されなければならない（全部でも 0 でもない）");
            Expect(frame.Stat(VirtualShadowMap::StatRendered) == actualDirty && frame.Stat(VirtualShadowMap::StatCached) == CountAllocated(frame) - actualDirty &&
                       frame.Stat(VirtualShadowMap::StatInvalidated) == actualDirty,
                   "ケース M3: 統計の描いたページ・持ち越し・無効にしたページが dirty の数と一致しなければならない");
            const uint32_t different = CompareWithUncachedFrame(device, uncachedPages, raster, scene, poolPages, movedGeometry, movedBuffers, depth, frame, frameSerial++);
            std::cout << TestName << " ケース " << moveNames[moveIndex] << ": 描き直したページ=" << actualDirty << "/" << CountAllocated(frame)
                      << " 期待=" << expectedDirty << " キャッシュなしとの違い（語）=" << different << std::endl;
            Expect(different == 0u, "ケース M3: 動いた後の物理プールが、毎フレーム描き直したとき（--vsm-cache=off 相当）と全 texel で一致しなければならない");
            previousShapes = movedShapes;
            previousGeometry = movedGeometry;
            // 次の動きは、この位置から始める（動いた物をいったん止めて、1 フレーム持ち越しを挟む）
            // 動きが止まれば、次のフレームは何も描かない
            CacheFrame settle;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, movedGeometry, movedBuffers, depth, &tracker, frameSerial++, settle))
            {
                return false;
            }
            Expect(settle.RectCount == 0u && settle.Stat(VirtualShadowMap::StatRendered) == 0u, "ケース M3: 動きが止まれば次のフレームは何も描かない");
        }

        // 以降の場面は、元の位置（M3 で動かす前）の形を、M3 の最後の位置のまま使う
        ChunkGeometry stillGeometry = previousGeometry;
        RasterBuffers stillBuffers;
        {
            const Container::VariableArray<Shape> stillShapes = previousShapes;
            if (!PrepareCacheGeometry(device, scene, stillShapes, stillGeometry, stillBuffers))
            {
                return false;
            }
        }

        // ----- ケース M3c: 展開の容量が足りず描けなかった塊の範囲のページは、欠けたまま持ち越さず、次のフレームで描き直す -----
        // 元の位置（shapesA）へ動かし、展開のインスタンスの容量が 1 の出力で記録する。動いた先のページは dirty で、2 ページ以上をまたぐ塊は
        // どれも溢れて描かれない。その範囲の dirty のページには再描画の印が付き、次のフレーム（動きの無い場面。容量は十分）が
        // 印のあるページだけを描き直して、毎フレーム描き直したとき（--vsm-cache=off 相当）と全 texel で一致する。最後に M3 の最後の位置へ戻す
        {
            ChunkGeometry overflowGeometry;
            RasterBuffers fullBuffers;
            RasterBuffers tinyBuffers;
            if (!PrepareCacheGeometry(device, scene, shapesA, overflowGeometry, fullBuffers) ||
                !CreateRasterBuffers(device, overflowGeometry, 1u, tinyBuffers))
            {
                std::cerr << TestName << " ケース M3c の形のバッファを作れませんでした" << std::endl;
                return false;
            }
            CacheFrame overflowFrame;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, overflowGeometry, tinyBuffers, depth, &tracker, frameSerial++, overflowFrame))
            {
                std::cerr << TestName << " ケース M3c（溢れるフレーム）を実行できませんでした" << std::endl;
                return false;
            }
            uint32_t retryPages = 0;
            uint32_t dirtyPages = 0;
            bool bRetrySetMatches = true;
            for (const PageInfo& page : overflowFrame.Infos)
            {
                bool bInRange = false;
                for (size_t chunk = 0; chunk < overflowGeometry.Chunks.size() && !bInRange; ++chunk)
                {
                    bInRange = PageOverlapsBounds(scene, page, overflowGeometry.Chunks[chunk].BoundsMin, overflowGeometry.Chunks[chunk].BoundsMax, 0.0);
                }
                retryPages += page.bRetry ? 1u : 0u;
                dirtyPages += page.bDirty ? 1u : 0u;
                bRetrySetMatches = bRetrySetMatches && page.bRetry == (page.bDirty && bInRange);
            }
            std::cout << TestName << " ケース M3c: 溢れ=" << overflowFrame.Stat(VirtualShadowMap::StatRasterOverflow) << " 再描画の印=" << retryPages
                      << " dirty=" << dirtyPages << std::endl;
            Expect(overflowFrame.Stat(VirtualShadowMap::StatRasterOverflow) > 0u && overflowFrame.Stat(VirtualShadowMap::StatRasterInstances) == 0u,
                   "ケース M3c: 容量が足りない展開は塊を描かず、溢れとして数えなければならない");
            Expect(retryPages > 0u && bRetrySetMatches,
                   "ケース M3c: 溢れた塊の範囲の dirty のページだけに再描画の印が付かなければならない");

            CacheFrame retryFrame;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, overflowGeometry, fullBuffers, depth, &tracker, frameSerial++, retryFrame))
            {
                std::cerr << TestName << " ケース M3c（描き直すフレーム）を実行できませんでした" << std::endl;
                return false;
            }
            uint32_t remainingRetry = 0;
            for (const PageInfo& page : retryFrame.Infos)
            {
                remainingRetry += page.bRetry ? 1u : 0u;
            }
            Expect(retryFrame.bContinued && retryFrame.RectCount == 0u && !retryFrame.bInvalidatedAll,
                   "ケース M3c: 動きの無い次のフレームは、前フレームの表を引き継ぎ、無効の矩形を持たない");
            Expect(retryFrame.Stat(VirtualShadowMap::StatRendered) == retryPages && retryFrame.Stat(VirtualShadowMap::StatInvalidated) == retryPages &&
                       retryFrame.DirtyPages() == retryPages && remainingRetry == 0u,
                   "ケース M3c: 再描画の印のあるページだけが dirty になって描き直され、印は外れなければならない");
            Expect(retryFrame.Stat(VirtualShadowMap::StatRasterOverflow) == 0u && retryFrame.Stat(VirtualShadowMap::StatRasterInstances) > 0u,
                   "ケース M3c: 描き直すフレームは溢れず、インスタンスを書かなければならない");
            const uint32_t different = CompareWithUncachedFrame(device, uncachedPages, raster, scene, poolPages, overflowGeometry, fullBuffers, depth, retryFrame, frameSerial++);
            std::cout << TestName << " ケース M3c: 描き直したページ=" << retryFrame.Stat(VirtualShadowMap::StatRendered) << " キャッシュなしとの違い（語）=" << different << std::endl;
            Expect(different == 0u, "ケース M3c: 描き直した後の物理プールが、毎フレーム描き直したとき（--vsm-cache=off 相当）と全 texel で一致しなければならない");

            // M3 の最後の位置へ戻し、動きが止まるまで進める（後のケースの前提）
            CacheFrame back;
            CacheFrame settle;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, back) ||
                !RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, settle))
            {
                std::cerr << TestName << " ケース M3c（元へ戻すフレーム）を実行できませんでした" << std::endl;
                return false;
            }
            Expect(settle.RectCount == 0u && settle.Stat(VirtualShadowMap::StatRendered) == 0u, "ケース M3c: 元へ戻して動きが止まれば、次のフレームは何も描かない");
        }

        // ----- ケース M4: 太陽の向きが変わると全ページが描き直される -----
        {
            Scene sunScene = scene;
            sunScene.Clipmap = BuildVirtualShadowMapClipmap(Math::Vector3(0.36f, -0.8f, 0.45f),
                                                            1u,
                                                            Math::Vector3(scene.CameraPosition[0], scene.CameraPosition[1], scene.CameraPosition[2]),
                                                            scene.Settings);
            Expect(sunScene.Clipmap.bEnabled, "ケース M4: 向きを変えたクリップマップが有効でなければならない");
            CacheFrame frame;
            if (!RunCacheFrame(device, cachedPages, raster, sunScene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, frame))
            {
                std::cerr << TestName << " ケース M4 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(frame.bContinued && frame.bInvalidatedAll, "ケース M4: 太陽の向きの変化で全ページを無効にしなければならない");
            Expect(CountAllocated(frame) > 0u && frame.DirtyPages() == CountAllocated(frame) && frame.Stat(VirtualShadowMap::StatRendered) == CountAllocated(frame) &&
                       frame.Stat(VirtualShadowMap::StatCached) == 0u,
                   "ケース M4: 太陽の向きを変えると、全ページが描き直されなければならない");
            const uint32_t different = CompareWithUncachedFrame(device, uncachedPages, raster, sunScene, poolPages, stillGeometry, stillBuffers, depth, frame, frameSerial++);
            std::cout << TestName << " ケース M4: 描き直したページ=" << frame.Stat(VirtualShadowMap::StatRendered) << "/" << CountAllocated(frame)
                      << " キャッシュなしとの違い（語）=" << different << std::endl;
            Expect(different == 0u, "ケース M4: 太陽の向きを変えた後の物理プールが、毎フレーム描き直したときと全 texel で一致しなければならない");
            // 元の向きへ戻す（次のケースの前提）。向きが変わるので、また全ページを描き直す
            CacheFrame back;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, back))
            {
                return false;
            }
            Expect(back.bInvalidatedAll && back.Stat(VirtualShadowMap::StatRendered) == CountAllocated(back), "ケース M4: 向きを戻したときも全ページを描き直さなければならない");
            CacheFrame stay;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, stay))
            {
                return false;
            }
            Expect(stay.Stat(VirtualShadowMap::StatRendered) == 0u, "ケース M4: 向きが落ち着けば次のフレームは何も描かない");
        }

        // ----- ケース M4b: 1 フレームごとの変化が微小でも、向きが変われば全ページが描き直される -----
        // 比較に許容を設けると、許容より小さい回転が毎フレーム続いたとき（比較元も毎フレーム更新される）に無効化されないまま累積する
        {
            Scene tinyScene = scene;
            const Math::Vector3 baseDirection = scene.Clipmap.Direction;
            const Math::Vector3 cameraPosition(scene.CameraPosition[0], scene.CameraPosition[1], scene.CameraPosition[2]);
            constexpr float TinyStep = 5.0e-7f;
            float previousX = baseDirection.x;
            for (uint32_t step = 1u; step <= 3u; ++step)
            {
                tinyScene.Clipmap = BuildVirtualShadowMapClipmap(
                    Math::Vector3(baseDirection.x + TinyStep * static_cast<float>(step), baseDirection.y, baseDirection.z), 1u, cameraPosition, scene.Settings);
                Expect(tinyScene.Clipmap.bEnabled, "ケース M4b: 微小に回したクリップマップが有効でなければならない");
                Expect(tinyScene.Clipmap.Direction.x != previousX && std::abs(tinyScene.Clipmap.Direction.x - previousX) < 1.0e-6f,
                       "ケース M4b: 微小に回した向きが、前の向きと違い、かつ 1e-6 未満の差でなければならない");
                previousX = tinyScene.Clipmap.Direction.x;
                CacheFrame frame;
                if (!RunCacheFrame(device, cachedPages, raster, tinyScene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, frame))
                {
                    std::cerr << TestName << " ケース M4b を実行できませんでした" << std::endl;
                    return false;
                }
                Expect(frame.bContinued && frame.bInvalidatedAll, "ケース M4b: 1 フレームの変化が微小でも、太陽の向きの変化で全ページを無効にしなければならない");
                Expect(CountAllocated(frame) > 0u && frame.Stat(VirtualShadowMap::StatRendered) == CountAllocated(frame) &&
                           frame.Stat(VirtualShadowMap::StatCached) == 0u,
                       "ケース M4b: 微小な回転でも、全ページが描き直されなければならない");
                const uint32_t different = CompareWithUncachedFrame(device, uncachedPages, raster, tinyScene, poolPages, stillGeometry, stillBuffers, depth, frame, frameSerial++);
                std::cout << TestName << " ケース M4b(" << step << "): 描き直したページ=" << frame.Stat(VirtualShadowMap::StatRendered) << "/" << CountAllocated(frame)
                          << " キャッシュなしとの違い（語）=" << different << std::endl;
                Expect(different == 0u, "ケース M4b: 微小な回転の後の物理プールが、毎フレーム描き直したときと全 texel で一致しなければならない");
            }
            // 元の向きへ戻して落ち着かせる（次のケースの前提）
            CacheFrame back;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, back))
            {
                return false;
            }
            Expect(back.bInvalidatedAll, "ケース M4b: 向きを戻したときも全ページを無効にしなければならない");
            CacheFrame stay;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, stay))
            {
                return false;
            }
            Expect(stay.Stat(VirtualShadowMap::StatRendered) == 0u, "ケース M4b: 向きが落ち着けば次のフレームは何も描かない");
        }

        // ----- ケース M5: 深度の原点がスナップで動くと、全ページが描き直される（段の中心は動かない） -----
        {
            Scene depthScene = scene;
            const Math::Vector3 direction = scene.Clipmap.Direction;
            depthScene.Clipmap = BuildVirtualShadowMapClipmap(Math::Vector3(0.35f, -0.8f, 0.45f),
                                                              1u,
                                                              Math::Vector3(scene.CameraPosition[0] + direction.x * 300.0f,
                                                                            scene.CameraPosition[1] + direction.y * 300.0f,
                                                                            scene.CameraPosition[2] + direction.z * 300.0f),
                                                              scene.Settings);
            Expect(depthScene.Clipmap.bEnabled && depthScene.Clipmap.DepthCenter != scene.Clipmap.DepthCenter,
                   "ケース M5: カメラを光の向きへ動かすと深度の原点が変わらなければならない");
            bool bSameOrigins = depthScene.Clipmap.LevelCount == scene.Clipmap.LevelCount;
            for (uint32_t levelIndex = 0; bSameOrigins && levelIndex < scene.Clipmap.LevelCount; ++levelIndex)
            {
                bSameOrigins = depthScene.Clipmap.Levels[levelIndex].OriginPageX == scene.Clipmap.Levels[levelIndex].OriginPageX &&
                               depthScene.Clipmap.Levels[levelIndex].OriginPageY == scene.Clipmap.Levels[levelIndex].OriginPageY;
            }
            Expect(bSameOrigins, "ケース M5: 光の向きへの移動では、段の範囲が動かない");
            CacheFrame frame;
            if (!RunCacheFrame(device, cachedPages, raster, depthScene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, frame))
            {
                std::cerr << TestName << " ケース M5 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(frame.bContinued && frame.bInvalidatedAll, "ケース M5: 深度の原点の変化で全ページを無効にしなければならない");
            Expect(frame.DirtyPages() == CountAllocated(frame) && frame.Stat(VirtualShadowMap::StatRendered) == CountAllocated(frame) &&
                       frame.Stat(VirtualShadowMap::StatCached) == 0u,
                   "ケース M5: 深度の原点が動くと、全ページが描き直されなければならない");
            const uint32_t different = CompareWithUncachedFrame(device, uncachedPages, raster, depthScene, poolPages, stillGeometry, stillBuffers, depth, frame, frameSerial++);
            Expect(different == 0u, "ケース M5: 深度の原点が動いた後の物理プールが、毎フレーム描き直したときと全 texel で一致しなければならない");
            CacheFrame back;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, back))
            {
                return false;
            }
            Expect(back.bInvalidatedAll, "ケース M5: 深度の原点を戻したときも全ページを無効にしなければならない");
        }

        // ----- ケース M6: 段の中心がページ単位で動くと、範囲に残ったページは描き直されず、範囲の外へ出たページは空きへ戻る -----
        {
            CacheFrame before;
            if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, before))
            {
                return false;
            }
            Expect(before.Stat(VirtualShadowMap::StatRendered) == 0u && CountAllocated(before) == requested, "ケース M6: 動かす前は落ち着いていなければならない");

            const Math::Vector3 right = scene.Clipmap.LightRight;
            // 2 つ目は、どの段の範囲（最も粗い段 4 の幅 16384 m の半分）よりも大きく動かす。視錐台の端の画素は粗い段のページも要求する
            const float shifts[2] = {static_cast<float>(pageMeters), 20000.0f};
            for (uint32_t shiftIndex = 0; shiftIndex < 2u; ++shiftIndex)
            {
                Scene movedScene = scene;
                movedScene.Clipmap = BuildVirtualShadowMapClipmap(Math::Vector3(0.35f, -0.8f, 0.45f),
                                                                  1u,
                                                                  Math::Vector3(scene.CameraPosition[0] + right.x * shifts[shiftIndex],
                                                                                scene.CameraPosition[1] + right.y * shifts[shiftIndex],
                                                                                scene.CameraPosition[2] + right.z * shifts[shiftIndex]),
                                                                  scene.Settings);
                CacheFrame beforeMove;
                if (!RunCacheFrame(device, cachedPages, raster, scene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, beforeMove))
                {
                    return false;
                }
                CacheFrame frame;
                if (!RunCacheFrame(device, cachedPages, raster, movedScene, resources, stillGeometry, stillBuffers, depth, &tracker, frameSerial++, frame))
                {
                    std::cerr << TestName << " ケース M6 を実行できませんでした" << std::endl;
                    return false;
                }
                // 期待: 前フレームのページのうち、新しい範囲の中のものは同じ物理ページのまま残り、外のものは戻る
                uint32_t expectedKept = 0;
                uint32_t expectedReleased = 0;
                bool bKeptSame = true;
                for (const PageInfo& page : beforeMove.Infos)
                {
                    const VirtualShadowMapClipmapLevel& levelData = movedScene.Clipmap.Levels[page.Level];
                    const int64_t count = static_cast<int64_t>(VirtualShadowMap::TABLE_DIMENSION);
                    const bool bInside = page.AbsX >= levelData.OriginPageX && page.AbsX < levelData.OriginPageX + count && page.AbsY >= levelData.OriginPageY &&
                                         page.AbsY < levelData.OriginPageY + count;
                    if (bInside)
                    {
                        ++expectedKept;
                        const PageInfo* same = FindPage(frame.Infos, page.Level, page.AbsX, page.AbsY);
                        bKeptSame = bKeptSame && same != nullptr && same->Physical == page.Physical && !same->bDirty;
                    }
                    else
                    {
                        ++expectedReleased;
                    }
                }
                const uint32_t newPages = CountAllocated(frame) - std::min(CountAllocated(frame), expectedKept);
                std::cout << TestName << " ケース M6（移動 " << shifts[shiftIndex] << " m）: 残ったページ=" << expectedKept << " 戻したページ=" << expectedReleased
                          << " 描いたページ=" << frame.Stat(VirtualShadowMap::StatRendered) << " 持ち越し=" << frame.Stat(VirtualShadowMap::StatCached) << std::endl;
                Expect(frame.bContinued && !frame.bInvalidatedAll, "ケース M6: 段の中心の移動だけでは全ページを無効にしない");
                Expect(frame.Stat(VirtualShadowMap::StatReleased) == expectedReleased, "ケース M6: 範囲の外へ出たページだけを空きへ戻さなければならない");
                Expect(bKeptSame, "ケース M6: 範囲に残ったページは同じ物理ページを保ち、dirty にしてはならない");
                Expect(frame.Stat(VirtualShadowMap::StatRendered) == newPages, "ケース M6: 描くのは、範囲に残らず新しく割り当てたページだけでなければならない");
                if (shiftIndex == 0u)
                {
                    Expect(expectedKept == requested && expectedReleased == 0u && frame.Stat(VirtualShadowMap::StatRendered) == 0u,
                           "ケース M6: 1 ページだけの移動では、要求のページがすべて残り、何も描き直さない");
                }
                else
                {
                    Expect(expectedReleased == requested && frame.Stat(VirtualShadowMap::StatCached) == 0u,
                           "ケース M6: 範囲より大きく動くと、前のページはすべて空きへ戻る");
                }
            }
        }

        // ----- ケース M7: 要求の無いページは 30 フレーム持ち越し、その後に空きへ戻す -----
        {
            VirtualShadowMapPages agePages;
            Resources ageResources;
            if (!agePages.Initialize(device.get(), &shaderManager) || !CreateResources(device, poolPages, ageResources))
            {
                return false;
            }
            const Container::VariableArray<float> emptyImage(ImageWidth * ImageHeight, 1.0f);
            const TexturePtr emptyDepth = CreateDepthTexture(device, emptyImage);
            if (!emptyDepth)
            {
                return false;
            }
            VirtualShadowMap::CasterMotionTracker ageTracker;
            CacheFrame first;
            if (!RunCacheFrame(device, agePages, raster, scene, ageResources, geometryA, buffersA, depth, &ageTracker, frameSerial++, first))
            {
                return false;
            }
            Expect(CountAllocated(first) == requested, "ケース M7: 最初のフレームで要求のページが割り当て済みでなければならない");
            bool bCarried = true;
            for (uint32_t index = 1; index <= VirtualShadowMap::CACHE_CARRY_FRAMES && bCarried; ++index)
            {
                CacheFrame frame;
                if (!RunCacheFrame(device, agePages, raster, scene, ageResources, geometryA, buffersA, emptyDepth, &ageTracker, frameSerial++, frame))
                {
                    return false;
                }
                bCarried = frame.Stat(VirtualShadowMap::StatReleased) == 0u && CountAllocated(frame) == requested && frame.Stat(VirtualShadowMap::StatRendered) == 0u &&
                           frame.Stat(VirtualShadowMap::StatRequested) == 0u;
                if (!bCarried)
                {
                    std::cerr << TestName << " ケース M7: 要求の無い " << index << " フレーム目: 戻した=" << frame.Stat(VirtualShadowMap::StatReleased)
                              << " 割り当て=" << CountAllocated(frame) << std::endl;
                }
            }
            Expect(bCarried, "ケース M7: 要求の無いページは 30 フレームの間、空きへ戻さず持ち越さなければならない");
            CacheFrame expired;
            if (!RunCacheFrame(device, agePages, raster, scene, ageResources, geometryA, buffersA, emptyDepth, &ageTracker, frameSerial++, expired))
            {
                return false;
            }
            Expect(expired.Stat(VirtualShadowMap::StatReleased) == requested && CountAllocated(expired) == 0u && expired.FreeList[0] == poolPages,
                   "ケース M7: 30 フレーム持ち越した次のフレームで、要求の無いページをすべて空きへ戻さなければならない");
            std::cout << TestName << " ケース M7: 戻したページ=" << expired.Stat(VirtualShadowMap::StatReleased) << " 空き=" << expired.FreeList[0] << "/" << poolPages << std::endl;
        }

        // ----- ケース M8: 空きが足りないとき、要求の無いページを古い順に戻す -----
        {
            Container::VariableArray<uint32_t> levelKeys[3];
            Container::VariableArray<float> levelImages[3];
            uint32_t order[3] = {0, 1, 2};
            for (uint32_t levelIndex = 0; levelIndex < 3u; ++levelIndex)
            {
                levelImages[levelIndex] = BuildLevelImage(scene, image, levelIndex);
                levelKeys[levelIndex] = BuildReference(scene, levelImages[levelIndex]).Keys;
            }
            std::sort(order, order + 3, [&](uint32_t a, uint32_t b) { return levelKeys[a].size() > levelKeys[b].size(); });
            const Container::VariableArray<uint32_t>& oldest = levelKeys[order[0]];
            const Container::VariableArray<uint32_t>& middle = levelKeys[order[1]];
            const Container::VariableArray<uint32_t>& newest = levelKeys[order[2]];
            Expect(!oldest.empty() && !middle.empty() && !newest.empty(), "ケース M8: 3 つの段それぞれに要求のページが要る（シーンが退化している）");
            if (oldest.empty() || middle.empty() || newest.empty())
            {
                return false;
            }
            const uint32_t evictPoolPages = static_cast<uint32_t>(oldest.size() + middle.size());
            VirtualShadowMapPages evictPages;
            Resources evictResources;
            if (!evictPages.Initialize(device.get(), &shaderManager) || !CreateResources(device, evictPoolPages, evictResources))
            {
                return false;
            }
            VirtualShadowMap::CasterMotionTracker evictTracker;
            CacheFrame frames[3];
            for (uint32_t index = 0; index < 3u; ++index)
            {
                const TexturePtr levelDepth = CreateDepthTexture(device, levelImages[order[index]]);
                if (!levelDepth ||
                    !RunCacheFrame(device, evictPages, raster, scene, evictResources, geometryA, buffersA, levelDepth, &evictTracker, frameSerial++, frames[index]))
                {
                    return false;
                }
            }
            // 3 フレーム目: 空きが 0 で、新しく要るページ（newest）の数だけ、いちばん古く要求された oldest から戻す
            uint32_t oldestLeft = 0;
            uint32_t middleLeft = 0;
            uint32_t newestAllocated = 0;
            for (const PageInfo& page : frames[2].Infos)
            {
                const uint32_t key = PageKey(page.Level, page.AbsX, page.AbsY);
                if (std::binary_search(oldest.begin(), oldest.end(), key))
                {
                    ++oldestLeft;
                }
                else if (std::binary_search(middle.begin(), middle.end(), key))
                {
                    ++middleLeft;
                    Expect(!page.bDirty, "ケース M8: 要求の無い新しいページは、戻さず持ち越さなければならない");
                }
                else if (std::binary_search(newest.begin(), newest.end(), key))
                {
                    ++newestAllocated;
                    Expect(page.bDirty, "ケース M8: 新しく割り当てたページは dirty でなければならない");
                }
            }
            std::cout << TestName << " ケース M8: プール=" << evictPoolPages << " 古い=" << oldest.size() << " 中=" << middle.size() << " 新しい=" << newest.size()
                      << " 戻したページ=" << frames[2].Stat(VirtualShadowMap::StatReleased) << " 古いページの残り=" << oldestLeft << std::endl;
            Expect(frames[2].Stat(VirtualShadowMap::StatOverflow) == 0u && newestAllocated == newest.size(), "ケース M8: 戻した空きへ、要求のページがすべて割り当てられなければならない（溢れ 0）");
            Expect(frames[2].Stat(VirtualShadowMap::StatReleased) == newest.size(), "ケース M8: 足りない数だけを戻さなければならない");
            Expect(oldestLeft == oldest.size() - newest.size() && middleLeft == middle.size(), "ケース M8: 戻すのは最も古く要求されたページからで、新しいページは残さなければならない");
        }
        return true;
    }

    // ========================================
    // ケース W: スライスが 40 個（太陽の 10 段をスライス 30〜39 に置く）
    // ========================================
    //
    // スライスの表の先頭 30 個は、太陽の段を繰り返した正射影のスライス（ページの表の先頭は連続する番地）。その後ろに太陽の 10 段が並ぶ
    // （段 L はスライス 30 + L。印付けは MarkFirstSlice = 30）。ケース F と同じ場面を、印付け → 割り当て → 消去 → 展開 → 描画に通して、
    // 先頭の 10 段に置いたとき（ケース F）と同じ物理ページの texel になることを確かめる:
    //   - 印・割り当て: 要求がスライス 30 + L の番地（参照のページの番号 + 30 × 128 × 128）に立ち、ページの表・要求のビット列はスライス 40 個ぶん。
    //     統計は先頭 32 スライスの集合（スライス 30・31 = 段 0・1）と、33 番目以降の使用の有無（段 2 以降）に分かれる
    //   - 塊の段の印: 境界が触れるスライスの組ごとの印（組 0 = スライス 0〜31、組 1 = 32〜39）。2 つの組にまたがる塊は同じ記録を持つ塊が組ごとに出る
    //   - 展開・描画: インスタンスのスライスの欄は 8 ビットで、32 以上のスライスと、その物理ページを正しく引く
    bool RunWideSliceCases(const DevicePtr& device,
                           VirtualShadowMapPages& pages,
                           VirtualShadowMapRaster& raster,
                           const Scene& scene,
                           const Reference& reference,
                           const TexturePtr& depth,
                           const CaseFData& caseF,
                           uint64_t& frameSerial)
    {
        constexpr uint32_t sliceCount = 40u;
        constexpr uint32_t firstSun = 30u;
        const VirtualShadowMapClipmap& clipmap = scene.Clipmap;
        Expect(clipmap.LevelCount + firstSun <= sliceCount, "ケース W: 太陽の段がスライスの表に収まらなければならない");

        GPUVsmSlice slices[VirtualShadowMapMaxSlices];
        VirtualShadowMapWideTest::BuildSunAtSlices(clipmap, sliceCount, firstSun, slices);

        // 塊の段の印は、本番の関数（SliceMasksForBounds・PushChunkPerGroup）が組ごとに作る
        ChunkGeometry geometry = BuildChunks(scene, caseF.Shapes);
        const Container::VariableArray<VsmShadowChunk> originalChunks = geometry.Chunks;
        Container::VariableArray<VsmShadowChunk> groupedChunks;
        VirtualShadowMap::CasterStats casterStats;
        uint32_t groupCounts[2] = {};
        for (const VsmShadowChunk& chunk : originalChunks)
        {
            VirtualShadowMap::CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = chunk.BoundsMin[axis];
                bounds.Max[axis] = chunk.BoundsMax[axis];
            }
            const VirtualShadowMap::SliceMasks masks = VirtualShadowMap::SliceMasksForBounds(slices, sliceCount, bounds);
            Expect(masks.GroupCount == 2u && masks.IsAny(), "ケース W: 40 スライスの印は 2 つの組で、境界が触れるスライスがなければならない");
            VirtualShadowMap::PushChunkPerGroup(chunk, masks, groupedChunks, casterStats);
        }
        for (const VsmShadowChunk& chunk : groupedChunks)
        {
            Expect(chunk.Reserved < 2u && chunk.LevelMask != 0u, "ケース W: 塊の組の番号は 0 か 1 で、印が空であってはならない");
            ++groupCounts[std::min(chunk.Reserved, 1u)];
        }
        Expect(groupCounts[0] == originalChunks.size() && groupCounts[1] == originalChunks.size() && casterStats.DroppedChunks == 0u,
               "ケース W: 各塊がスライス 0〜31 の組と 32〜39 の組の両方にまたがり、組ごとに 1 つずつ出なければならない");
        geometry.Chunks = groupedChunks;

        Resources resources;
        RasterBuffers rasterBuffers;
        RasterReadback readback;
        const uint32_t poolPages = static_cast<uint32_t>(reference.Keys.size()) + 24u;
        const uint32_t instanceCapacity = 4096u;
        const WideInput wide{sliceCount, slices, firstSun};
        if (!CreateResources(device, poolPages, resources, sliceCount) || !CreateRasterBuffers(device, geometry, instanceCapacity, rasterBuffers))
        {
            std::cerr << TestName << " ケース W の資源を作れませんでした" << std::endl;
            return false;
        }
        Expect(resources.PageTable->GetSize() == VirtualShadowMap::PageTableBytes(sliceCount) &&
                   resources.RequestBits->GetSize() == VirtualShadowMap::RequestBitsBytes(sliceCount),
               "ケース W: ページの表・要求のビット列はスライス 40 個ぶんの大きさでなければならない");
        // 展開が書く統計の語（5〜7）は、呼ぶ前に 0 にする（ページの記録の割り当てが全語を 0 にする）
        if (!RunRaster(device, &pages, raster, scene, resources, rasterBuffers, depth, frameSerial++, readback, nullptr, &wide))
        {
            std::cerr << TestName << " ケース W を実行できませんでした" << std::endl;
            return false;
        }
        Expect(readback.bPagesRecorded && readback.bRasterRecorded, "ケース W: 印付け・割り当て・消去・展開・描画を記録できなければならない");
        Expect(readback.PageTable.size() == VirtualShadowMap::PageTableBytes(sliceCount) / sizeof(uint32_t),
               "ケース W: ページの表の語の数がスライス 40 個ぶんでなければならない");

        // 割り当て済みの欄は、参照のページの番号をスライス 30 だけずらした番地にだけある
        Container::VariableArray<uint32_t> allocatedKeys;
        for (uint32_t index = 0; index < readback.PageTable.size(); ++index)
        {
            if ((readback.PageTable[index] & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) != 0u)
            {
                allocatedKeys.push_back(index);
            }
        }
        Container::VariableArray<uint32_t> expectedKeys = reference.Keys;
        for (uint32_t& key : expectedKeys)
        {
            key += firstSun * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
        }
        Expect(allocatedKeys == expectedKeys, "ケース W: 割り当て済みの欄が、参照のページをスライス 30 ずらした集合と一致しなければならない");
        // 統計: 先頭 32 スライスの集合（スライス 30・31）と、33 番目以降の有無
        const uint32_t expectedLevelsUsed = reference.LevelMask << firstSun;
        const uint32_t expectedBeyond = (reference.LevelMask >> (32u - firstSun)) != 0u ? 1u : 0u;
        Expect(readback.Stats.size() == VirtualShadowMap::STATS_WORD_COUNT, "ケース W: 統計の語の数が STATS_WORD_COUNT でなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatRequested] == reference.Keys.size() &&
                   readback.Stats[VirtualShadowMap::StatAllocated] == reference.Keys.size() && readback.Stats[VirtualShadowMap::StatOverflow] == 0u,
               "ケース W: 要求・割り当て・溢れが参照と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatLevelsUsed] == expectedLevelsUsed,
               "ケース W: 先頭 32 スライスのうち要求のあったスライスの集合が参照と一致しなければならない");
        Expect(expectedBeyond == 1u && readback.Stats[VirtualShadowMap::StatLevelsUsedBeyond] == expectedBeyond,
               "ケース W: 33 番目以降のスライスに要求があるとき、その印が立たなければならない");

        // 展開・描画: ケース F と同じ形の和の参照と全 texel で一致し、ケース F（先頭の 10 段）と同じページの texel になる
        const Container::VariableArray<PageInfo> pageInfos = DecodePages(scene, readback.PageTable, firstSun);
        Expect(pageInfos.size() == reference.Keys.size(), "ケース W: 割り当てたページの数が参照と一致しなければならない");
        const PoolCheck check = CheckPool("ケース W", scene, caseF.Shapes, pageInfos, readback.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
        Expect(check.Mismatches == 0u && check.Covered > 2000u, "ケース W: 物理ページが形の和の参照と一致しなければならない");
        uint32_t differentFromCaseF = 0;
        for (const PageInfo& page : pageInfos)
        {
            const PageInfo* base = FindPage(caseF.Pages, page.Level, page.AbsX, page.AbsY);
            if (base == nullptr)
            {
                ++differentFromCaseF;
                continue;
            }
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                differentFromCaseF += readback.Pool[static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS + word] !=
                                              caseF.Pool[static_cast<size_t>(base->Physical) * VirtualShadowMap::PAGE_WORDS + word]
                                          ? 1u
                                          : 0u;
            }
        }
        Expect(differentFromCaseF == 0u, "ケース W: 物理ページの texel が先頭の 10 段のとき（ケース F）と一致しなければならない");

        // 展開の統計・引数・インスタンス: 組ごとの塊に分かれていても、書いたインスタンスの数は元の塊ごとの参照の合計と一致する
        uint32_t expectedInstances = 0;
        for (const VsmShadowChunk& chunk : originalChunks)
        {
            expectedInstances += CountExpectedInstances(scene, chunk, pageInfos);
        }
        uint32_t drawnInstances = 0;
        for (uint32_t chunk = 0; chunk < groupedChunks.size(); ++chunk)
        {
            drawnInstances += readback.Draws[VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS + chunk * VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS + 1u];
        }
        Expect(readback.DrawCount == groupedChunks.size(), "ケース W: 間接描画は組ごとに出した塊の数だけ記録しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatRasterInstances] == expectedInstances && readback.Stats[VirtualShadowMap::StatRasterOverflow] == 0u &&
                   drawnInstances == expectedInstances && expectedInstances > 0u,
               "ケース W: 書いたインスタンスの数が参照と一致し、溢れてはならない");
        uint32_t badInstances = 0;
        for (uint32_t index = 0; index < expectedInstances && index < readback.Instances.size() / 4u; ++index)
        {
            const uint32_t slice = readback.Instances[index * 4u + 1u] & 255u;
            const uint32_t physical = readback.Instances[index * 4u + 1u] >> 8u;
            const PageInfo* page = slice >= firstSun
                                       ? FindPage(pageInfos, slice - firstSun, static_cast<int32_t>(readback.Instances[index * 4u + 2u]),
                                                  static_cast<int32_t>(readback.Instances[index * 4u + 3u]))
                                       : nullptr;
            badInstances += (page == nullptr || !page->bDirty || page->Physical != physical || slice >= sliceCount) ? 1u : 0u;
        }
        Expect(badInstances == 0u, "ケース W: インスタンスのスライス（8 ビット）と物理ページが、ページの表と一致しなければならない");
        std::cout << TestName << " ケース W: スライス=" << sliceCount << "（太陽の段はスライス " << firstSun << " から）ページ=" << pageInfos.size()
                  << " 塊=" << originalChunks.size() << "→" << groupedChunks.size() << "（組ごと）インスタンス=" << expectedInstances
                  << " 段の集合=0x" << std::hex << readback.Stats[VirtualShadowMap::StatLevelsUsed] << std::dec << " 33 番目以降="
                  << readback.Stats[VirtualShadowMap::StatLevelsUsedBeyond] << " 比べた texel=" << check.Compared << " 不一致=" << check.Mismatches
                  << std::endl;
        return true;
    }

    // ========================================
    // ケース P: 点光源の面のページへの印付けと、太陽と同じプールからの割り当て
    // ========================================
    //
    // 太陽の 5 段（スライス 0〜4）の後ろ（スライス 10 から）に、2 灯 × 6 面 × 6 段のスライスを並べる。合成の深度（床と奥の壁）の各画素について、
    // 影を持つ灯のうち Range の内側のものごとに、面（向きの主軸）・段（面の軸の距離とカメラからの距離）・ページを倍精度で求め、
    // 核（texel に比例する分）= 受け手の面の接平面上の正方形を各面の錐台で切り、残った部分が触れるページ（面の縁をまたぐ核は
    // 隣の面の同じ段のページ）を集める。単精度の GPU との差で結果が変わりうる画素（面・段・ページの境目、Range、近い面の端の近く、
    // 核の大きさを少し変えると集合が変わる画素）は空にして除く。
    constexpr double PointAmbiguityMeters = 0.01;
    constexpr double PointAmbiguityRatio = 1.0e-3;

    struct PointScene
    {
        VirtualShadowMapPointLights Lights;
        uint32_t SliceCount = 0;
        GPUVsmSlice Slices[VirtualShadowMapMaxSlices] = {};
    };

    PointScene BuildPointSceneFrom(const Scene& scene, const PointShadowSnapshot& snapshot)
    {
        PointScene result;
        result.Lights = BuildVirtualShadowMapPointLights(snapshot, VirtualShadowMapPointSettings{}, VirtualShadowMap::LEVEL_COUNT);
        result.SliceCount = VirtualShadowMap::LEVEL_COUNT + PointShadowMaxLights * PointShadowFaceCount * VirtualShadowMapPointSettings{}.MipCount;
        BuildVirtualShadowMapSlices(&scene.Clipmap, nullptr, result.SliceCount, result.Slices);
        BuildVirtualShadowMapPointSlices(result.Lights, result.Slices);
        return result;
    }

    PointScene BuildPointScene(const Scene& scene)
    {
        PointShadowSnapshot snapshot;
        snapshot.LightCount = 2u;
        // 灯 0: 奥の壁の手前の高い所（床・壁の広い範囲、複数の段）。灯 1: カメラの近くの低い所（Range が短く、床の一部だけ）
        snapshot.Lights[0].LightId = 101u;
        snapshot.Lights[0].Position = Math::Vector3(1.5f, 2.0f, -12.0f);
        snapshot.Lights[0].Range = 30.0f;
        snapshot.Lights[1].LightId = 102u;
        snapshot.Lights[1].Position = Math::Vector3(-6.0f, 1.0f, -2.0f);
        snapshot.Lights[1].Range = 6.0f;
        return BuildPointSceneFrom(scene, snapshot);
    }

    double Dot3(const double* a, const double* b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    // 向き v の主軸の面。最大の成分と次に大きい成分の差が許容以内なら false（単精度の GPU が別の面を選びうる）
    bool SelectPointFaceChecked(const double (&v)[3], uint32_t& outFace)
    {
        double magnitude[3] = {std::abs(v[0]), std::abs(v[1]), std::abs(v[2])};
        std::sort(magnitude, magnitude + 3);
        if (magnitude[2] - magnitude[1] < PointAmbiguityMeters)
        {
            return false;
        }
        outFace = SelectVirtualShadowMapPointFace(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
        return true;
    }

    // 核の大きさ・ページ番号の境を少し縮めた場合と広げた場合で集合が変わるなら、単精度の GPU と食い違いうるので曖昧な画素にする。
    // 幅は単精度の誤差（1e-6 程度）より十分大きい
    constexpr double PointFootprintRadiusSlack = 2.0e-3;
    constexpr double PointFootprintPageSlack = 2.0e-3;
    constexpr uint32_t PointClipMaxVertices = 12u;

    struct PointClipPolygon
    {
        double Vertices[PointClipMaxVertices][3] = {};
        uint32_t Count = 0;
    };

    // 面の座標（x, y, 軸の距離 z）での境界の符号つき距離。0 以上が面の錐台の内側（0: 近い面、1〜4: 面の 4 つの縁。vsm_mark.comp と同じ平面）
    double PointFaceClipDistance(const double (&p)[3], uint32_t plane)
    {
        switch (plane)
        {
        case 0u:
            return p[2] - static_cast<double>(PointShadowNearPlane);
        case 1u:
            return p[2] - p[0];
        case 2u:
            return p[2] + p[0];
        case 3u:
            return p[2] - p[1];
        default:
            return p[2] + p[1];
        }
    }

    void ClipPointPolygon(PointClipPolygon& polygon)
    {
        for (uint32_t plane = 0; plane < 5u && polygon.Count != 0u; ++plane)
        {
            PointClipPolygon next;
            for (uint32_t index = 0; index < polygon.Count; ++index)
            {
                const double(&current)[3] = polygon.Vertices[index];
                const double(&following)[3] = polygon.Vertices[(index + 1u) % polygon.Count];
                const double currentDistance = PointFaceClipDistance(current, plane);
                const double followingDistance = PointFaceClipDistance(following, plane);
                if (currentDistance >= 0.0)
                {
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        next.Vertices[next.Count][axis] = current[axis];
                    }
                    ++next.Count;
                }
                if ((currentDistance >= 0.0) != (followingDistance >= 0.0))
                {
                    const double fraction = currentDistance / (currentDistance - followingDistance);
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        next.Vertices[next.Count][axis] = current[axis] + (following[axis] - current[axis]) * fraction;
                    }
                    ++next.Count;
                }
            }
            polygon = next;
        }
    }

    // 受け手（光源からの相対位置 offset・面 receiverFace・段 mip）の核 = 受け手の面の接平面上の、半径 radius の正方形が触れる点光源のページの鍵
    // （スライスの番号 × 128 × 128 + 番地）を足す。正方形を各面の錐台で切り、残った多角形の頂点を NDC へ写した外接の範囲のページを集める。
    // pageSlack はページ番号の境の扱い（ページ単位。正なら範囲を広げ、負なら狭める）。bOwnFaceOnly は受け手の面だけを数える（隣の面へ印を付けない変異）
    void CollectPointFootprintKeys(const PointScene& pointScene,
                                   uint32_t light,
                                   uint32_t receiverFace,
                                   uint32_t mip,
                                   const double (&offset)[3],
                                   double radius,
                                   double pageSlack,
                                   bool bOwnFaceOnly,
                                   Container::VariableArray<uint32_t>& outKeys)
    {
        const VirtualShadowMapPointLights& lights = pointScene.Lights;
        const GPUVsmSlice& receiverSlice = pointScene.Slices[VirtualShadowMapPointSliceIndex(lights, light, receiverFace, mip)];
        const double tangentX[3] = {receiverSlice.axisX[0], receiverSlice.axisX[1], receiverSlice.axisX[2]};
        const double tangentY[3] = {receiverSlice.axisY[0], receiverSlice.axisY[1], receiverSlice.axisY[2]};
        double corners[4][3] = {};
        for (uint32_t corner = 0; corner < 4u; ++corner)
        {
            const double signX = (corner == 1u || corner == 2u) ? radius : -radius;
            const double signY = corner >= 2u ? radius : -radius;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                corners[corner][axis] = offset[axis] + signX * tangentX[axis] + signY * tangentY[axis];
            }
        }
        const int32_t pageCount = static_cast<int32_t>(VirtualShadowMapPointPagesPerAxis(lights.Settings, mip));
        for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
        {
            if (bOwnFaceOnly && face != receiverFace)
            {
                continue;
            }
            const uint32_t sliceIndex = VirtualShadowMapPointSliceIndex(lights, light, face, mip);
            const GPUVsmSlice& slice = pointScene.Slices[sliceIndex];
            const double faceX[3] = {slice.axisX[0], slice.axisX[1], slice.axisX[2]};
            const double faceY[3] = {slice.axisY[0], slice.axisY[1], slice.axisY[2]};
            const double faceZ[3] = {slice.axisZ[0], slice.axisZ[1], slice.axisZ[2]};
            PointClipPolygon polygon;
            polygon.Count = 4u;
            for (uint32_t corner = 0; corner < 4u; ++corner)
            {
                polygon.Vertices[corner][0] = Dot3(faceX, corners[corner]);
                polygon.Vertices[corner][1] = Dot3(faceY, corners[corner]);
                polygon.Vertices[corner][2] = Dot3(faceZ, corners[corner]);
            }
            ClipPointPolygon(polygon);
            if (polygon.Count == 0u)
            {
                continue;
            }
            double low[2] = {1.0e30, 1.0e30};
            double high[2] = {-1.0e30, -1.0e30};
            for (uint32_t index = 0; index < polygon.Count; ++index)
            {
                for (uint32_t axis = 0; axis < 2u; ++axis)
                {
                    const double ndc = std::clamp(polygon.Vertices[index][axis] / std::max(polygon.Vertices[index][2], 1.0e-6), -1.0, 1.0);
                    low[axis] = std::min(low[axis], ndc);
                    high[axis] = std::max(high[axis], ndc);
                }
            }
            int32_t pageLow[2] = {};
            int32_t pageHigh[2] = {};
            for (uint32_t axis = 0; axis < 2u; ++axis)
            {
                pageLow[axis] = std::clamp(static_cast<int32_t>(std::floor((low[axis] * 0.5 + 0.5) * pageCount - pageSlack)), 0, pageCount - 1);
                pageHigh[axis] = std::clamp(static_cast<int32_t>(std::floor((high[axis] * 0.5 + 0.5) * pageCount + pageSlack)), 0, pageCount - 1);
            }
            for (int32_t pageY = pageLow[1]; pageY <= pageHigh[1]; ++pageY)
            {
                for (int32_t pageX = pageLow[0]; pageX <= pageHigh[0]; ++pageX)
                {
                    outKeys.push_back(sliceIndex * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL + static_cast<uint32_t>(pageY) * static_cast<uint32_t>(pageCount) +
                                      static_cast<uint32_t>(pageX));
                }
            }
        }
    }

    // 核を受け手の面の座標の 3 × 3 の標本だけで表した場合のページの鍵（標本が面の縁を越えれば向きから選んだ面の同じ段）。
    // 標本だけでは隣の面のページを取りこぼす向きがあることを示すためだけに使う（印付けの参照ではない）
    void CollectPointNineSampleKeys(const PointScene& pointScene,
                                    uint32_t light,
                                    uint32_t receiverFace,
                                    uint32_t mip,
                                    const double (&offset)[3],
                                    double radius,
                                    Container::VariableArray<uint32_t>& outKeys)
    {
        const VirtualShadowMapPointLights& lights = pointScene.Lights;
        const GPUVsmSlice& receiverSlice = pointScene.Slices[VirtualShadowMapPointSliceIndex(lights, light, receiverFace, mip)];
        const double tangentX[3] = {receiverSlice.axisX[0], receiverSlice.axisX[1], receiverSlice.axisX[2]};
        const double tangentY[3] = {receiverSlice.axisY[0], receiverSlice.axisY[1], receiverSlice.axisY[2]};
        const int32_t pageCount = static_cast<int32_t>(VirtualShadowMapPointPagesPerAxis(lights.Settings, mip));
        for (int32_t row = -1; row <= 1; ++row)
        {
            for (int32_t column = -1; column <= 1; ++column)
            {
                double sample[3] = {};
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    sample[axis] = offset[axis] + column * radius * tangentX[axis] + row * radius * tangentY[axis];
                }
                const uint32_t face = SelectVirtualShadowMapPointFace(static_cast<float>(sample[0]), static_cast<float>(sample[1]), static_cast<float>(sample[2]));
                const uint32_t sliceIndex = VirtualShadowMapPointSliceIndex(lights, light, face, mip);
                const GPUVsmSlice& slice = pointScene.Slices[sliceIndex];
                const double faceX[3] = {slice.axisX[0], slice.axisX[1], slice.axisX[2]};
                const double faceY[3] = {slice.axisY[0], slice.axisY[1], slice.axisY[2]};
                const double faceZ[3] = {slice.axisZ[0], slice.axisZ[1], slice.axisZ[2]};
                const double axial = Dot3(faceZ, sample);
                if (axial < static_cast<double>(PointShadowNearPlane))
                {
                    continue;
                }
                int32_t page[2] = {};
                const double ndc[2] = {std::clamp(Dot3(faceX, sample) / axial, -1.0, 1.0), std::clamp(Dot3(faceY, sample) / axial, -1.0, 1.0)};
                for (uint32_t axis = 0; axis < 2u; ++axis)
                {
                    page[axis] = std::clamp(static_cast<int32_t>(std::floor((ndc[axis] * 0.5 + 0.5) * pageCount)), 0, pageCount - 1);
                }
                outKeys.push_back(sliceIndex * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL + static_cast<uint32_t>(page[1]) * static_cast<uint32_t>(pageCount) +
                                  static_cast<uint32_t>(page[0]));
            }
        }
    }

    // 面の軸の距離 axial の受け手の段: texel（2z ÷ 段の解像度）が目標以下の最も粗い段（どれも超えれば 0）。目標に近い段があれば false
    bool ChoosePointMip(const VirtualShadowMapPointSettings& settings, double axial, double targetTexel, uint32_t& outMip)
    {
        outMip = 0;
        bool bChosen = false;
        for (uint32_t candidate = settings.MipCount; candidate-- > 0u;)
        {
            const double texel = 2.0 * axial / static_cast<double>(VirtualShadowMapPointMipResolution(settings, candidate));
            if (std::abs(texel - targetTexel) < PointAmbiguityRatio * targetTexel)
            {
                return false;
            }
            if (!bChosen && texel <= targetTexel)
            {
                outMip = candidate;
                bChosen = true;
            }
        }
        return true;
    }

    // 核の半径（ワールドの長さ）: texel に比例する分を、その距離でのページの幅で抑える（vsm_mark.comp と同じ。ワールドの長さの分は既定で 0）
    double PointKernelRadius(const VirtualShadowMapPointSettings& settings, uint32_t mip, double axial)
    {
        const double texelMeters = 2.0 * axial / static_cast<double>(VirtualShadowMapPointMipResolution(settings, mip));
        return std::min(static_cast<double>(VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS) * texelMeters,
                        axial * 2.0 / static_cast<double>(VirtualShadowMapPointPagesPerAxis(settings, mip)));
    }

    struct PointReferenceStats
    {
        uint32_t StablePixels = 0;
        // 点光源のページに印が付く画素の数、核の標本が別の面を指した画素の数
        uint32_t MarkedPixels = 0;
        uint32_t CrossFacePixels = 0;
        uint32_t MipMask = 0;
        uint32_t FaceMask = 0;
    };

    // 画素を分類し、安定した画素では点光源のページの鍵（スライスの番号 × 128 × 128 + ページの番地）を返す。
    // outNoCrossKeys は、核の標本が面の縁を越えても隣の面を引かず、受け手の面の縁のページに収めたとき（変異した印付け）の鍵
    PixelKind ClassifyPointPixel(const Scene& scene,
                                 const PointScene& pointScene,
                                 uint32_t pixelX,
                                 uint32_t pixelY,
                                 float depth,
                                 Container::VariableArray<uint32_t>* outKeys,
                                 Container::VariableArray<uint32_t>* outNoCrossKeys,
                                 PointReferenceStats* outStats)
    {
        if (!(depth < 1.0f))
        {
            return PixelKind::Sky;
        }
        double world[3] = {};
        Unproject(scene, pixelX, pixelY, static_cast<double>(depth), world);
        const double toCamera[3] = {world[0] - scene.CameraPosition[0], world[1] - scene.CameraPosition[1], world[2] - scene.CameraPosition[2]};
        const double cameraDistance = std::sqrt(Dot3(toCamera, toCamera));
        const VirtualShadowMapPointLights& lights = pointScene.Lights;
        const VirtualShadowMapPointSettings& settings = lights.Settings;
        const double pixelPerMeter = 2.0 * std::tan(static_cast<double>(scene.Camera.FieldOfView) * 3.14159265358979323846 / 360.0) / ImageHeight;
        const double targetTexel = cameraDistance * pixelPerMeter * std::exp2(static_cast<double>(settings.BiasLevels));
        const double nearPlane = static_cast<double>(PointShadowNearPlane);

        Container::VariableArray<uint32_t> keys;
        Container::VariableArray<uint32_t> noCrossKeys;
        uint32_t mipMask = 0;
        uint32_t faceMask = 0;
        bool bCross = false;
        for (uint32_t light = 0; light < lights.LightCount; ++light)
        {
            const double offset[3] = {world[0] - lights.Position[light].x, world[1] - lights.Position[light].y, world[2] - lights.Position[light].z};
            const double range = static_cast<double>(lights.Range[light]);
            const double distance = std::sqrt(Dot3(offset, offset));
            if (std::abs(distance - range) < PointAmbiguityMeters)
            {
                return PixelKind::Ambiguous;
            }
            if (distance > range)
            {
                continue;
            }
            uint32_t face = 0;
            if (!SelectPointFaceChecked(offset, face))
            {
                return PixelKind::Ambiguous;
            }
            const GPUVsmSlice& faceSlice = pointScene.Slices[VirtualShadowMapPointSliceIndex(lights, light, face, 0u)];
            const double major[3] = {faceSlice.axisZ[0], faceSlice.axisZ[1], faceSlice.axisZ[2]};
            const double axial = Dot3(major, offset);
            if (std::abs(axial - nearPlane) < PointAmbiguityMeters)
            {
                return PixelKind::Ambiguous;
            }
            if (axial < nearPlane)
            {
                continue;
            }

            // 段: texel（2z ÷ 段の解像度）が目標以下の最も粗い段。目標に近い段があれば曖昧
            uint32_t mip = 0;
            if (!ChoosePointMip(settings, axial, targetTexel, mip))
            {
                return PixelKind::Ambiguous;
            }
            const double radius = PointKernelRadius(settings, mip, axial);
            mipMask |= 1u << mip;
            faceMask |= 1u << face;

            // 核の大きさとページ番号の境を縮めた場合と広げた場合で集合が変わるなら曖昧（縮めた側は触れる面が減る・ページが減る）
            Container::VariableArray<uint32_t> shrunk;
            Container::VariableArray<uint32_t> expanded;
            CollectPointFootprintKeys(pointScene, light, face, mip, offset, radius * (1.0 - PointFootprintRadiusSlack), -PointFootprintPageSlack, false, shrunk);
            CollectPointFootprintKeys(pointScene, light, face, mip, offset, radius * (1.0 + PointFootprintRadiusSlack), PointFootprintPageSlack, false, expanded);
            SortUnique(shrunk);
            SortUnique(expanded);
            if (shrunk != expanded)
            {
                return PixelKind::Ambiguous;
            }
            // 隣の面のページが入るか: 鍵のスライスから面を求めて、受け手の面と違えば面をまたぐ核
            for (const uint32_t key : expanded)
            {
                const uint32_t local = key / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL - lights.FirstSlice;
                bCross = bCross || (local / settings.MipCount) % PointShadowFaceCount != face;
            }
            CollectPointFootprintKeys(pointScene, light, face, mip, offset, radius, 0.0, false, keys);
            // 変異した印付け（隣の面へ印を付けない）の鍵
            CollectPointFootprintKeys(pointScene, light, face, mip, offset, radius, 0.0, true, noCrossKeys);
        }
        if (outKeys)
        {
            outKeys->insert(outKeys->end(), keys.begin(), keys.end());
        }
        if (outNoCrossKeys)
        {
            outNoCrossKeys->insert(outNoCrossKeys->end(), noCrossKeys.begin(), noCrossKeys.end());
        }
        if (outStats)
        {
            ++outStats->StablePixels;
            outStats->MarkedPixels += keys.empty() ? 0u : 1u;
            outStats->CrossFacePixels += bCross ? 1u : 0u;
            outStats->MipMask |= mipMask;
            outStats->FaceMask |= faceMask;
        }
        return PixelKind::Stable;
    }

    // 点光源の分の曖昧な画素を空にする
    uint32_t RemoveAmbiguousPointPixels(const Scene& scene, const PointScene& pointScene, Container::VariableArray<float>& image)
    {
        uint32_t removed = 0;
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                float& depth = image[pixelY * ImageWidth + pixelX];
                if (ClassifyPointPixel(scene, pointScene, pixelX, pixelY, depth, nullptr, nullptr, nullptr) == PixelKind::Ambiguous)
                {
                    depth = 1.0f;
                    ++removed;
                }
            }
        }
        return removed;
    }

    // 要求の鍵（スライスの番号 × 128 × 128 + 番地）の集合から、先頭 32 スライスの集合と、33 番目以降の有無
    uint32_t LowSliceMask(const Container::VariableArray<uint32_t>& keys, uint32_t& outBeyond)
    {
        uint32_t mask = 0;
        outBeyond = 0;
        for (const uint32_t key : keys)
        {
            const uint32_t slice = key / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            if (slice < 32u)
            {
                mask |= 1u << slice;
            }
            else
            {
                outBeyond = 1u;
            }
        }
        return mask;
    }

    // 点光源の分の鍵に太陽の分を足した、要求の集合（昇順・重複なし）
    Container::VariableArray<uint32_t> UnionKeys(const Container::VariableArray<uint32_t>& sunKeys, const Container::VariableArray<uint32_t>& pointKeys)
    {
        Container::VariableArray<uint32_t> all = sunKeys;
        all.insert(all.end(), pointKeys.begin(), pointKeys.end());
        SortUnique(all);
        return all;
    }

    // 1 回の実行: 資源を作って記録し、印・割り当て・統計・太陽と点光源の物理ページの重なりを確かめる。bOverflow なら、プールを要求の約半分にする
    bool RunPointMarkOnce(const DevicePtr& device,
                          VirtualShadowMapPages& pages,
                          const Scene& scene,
                          const PointScene& pointScene,
                          const char* label,
                          const Container::VariableArray<float>& image,
                          bool bOverflow,
                          bool bFullScene,
                          uint64_t& frameSerial,
                          bool bNoSun = false)
    {
        // 太陽が無い場面（夜）は、太陽のクリップマップを渡さず、要求は点光源の分だけ
        const Reference sunReference = bNoSun ? Reference{} : BuildReference(scene, image);
        Container::VariableArray<uint32_t> pointKeys;
        PointReferenceStats referenceStats;
        for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
            {
                ClassifyPointPixel(scene, pointScene, pixelX, pixelY, image[pixelY * ImageWidth + pixelX], &pointKeys, nullptr, &referenceStats);
            }
        }
        SortUnique(pointKeys);
        const Container::VariableArray<uint32_t> expected = UnionKeys(sunReference.Keys, pointKeys);
        uint32_t expectedBeyond = 0;
        const uint32_t expectedMask = LowSliceMask(expected, expectedBeyond);
        std::cout << TestName << " " << label << ": 安定した画素=" << referenceStats.StablePixels << " 点光源のページが付く画素=" << referenceStats.MarkedPixels
                  << " 面をまたぐ核の画素=" << referenceStats.CrossFacePixels << " 段の集合=0x" << std::hex << referenceStats.MipMask << " 面の集合=0x"
                  << referenceStats.FaceMask << std::dec << " 太陽のページ=" << sunReference.Keys.size() << " 点光源のページ=" << pointKeys.size() << std::endl;
        // 全画素の場面は太陽にも点光源にも要求があり、疎な場面（点光源の隣の面への印だけを見る）は点光源の要求があればよい
        Expect(pointKeys.size() >= 3u && (!bFullScene || ((bNoSun || !sunReference.Keys.empty()) && pointKeys.size() >= 6u)),
               "ケース P: 点光源（全画素の場面では太陽も）に要求がなければならない（場面が退化している）");

        const TexturePtr depth = CreateDepthTexture(device, image);
        Resources resources;
        Readback readback;
        const uint32_t requested = static_cast<uint32_t>(expected.size());
        const uint32_t poolPages = bOverflow ? requested / 2u : requested + 24u;
        if (!depth || poolPages == 0u || !CreateResources(device, poolPages, resources, pointScene.SliceCount) ||
            !RunPages(device, pages, scene, resources, depth, !bNoSun, frameSerial++, true, readback, &pointScene.Lights, pointScene.SliceCount))
        {
            std::cerr << TestName << " " << label << " を実行できませんでした" << std::endl;
            return false;
        }
        Expect(readback.bMarked, "ケース P: 印付けを記録しなければならない");
        Expect(readback.PageTable.size() == VirtualShadowMap::PageTableBytes(pointScene.SliceCount) / sizeof(uint32_t),
               "ケース P: ページの表はスライスの数ぶんの大きさでなければならない");
        CheckAllocation(label, readback, expected, poolPages, expectedMask, true);
        Expect(readback.Stats[VirtualShadowMap::StatLevelsUsedBeyond] == expectedBeyond, "ケース P: 33 番目以降のスライスの使用の有無が参照と一致しなければならない");

        // 太陽の段と点光源の面は同じプールの別の物理ページ。点光源の分の統計は、点光源のスライスだけを数える
        Container::VariableArray<uint32_t> sunPhysical;
        Container::VariableArray<uint32_t> pointPhysical;
        for (uint32_t index = 0; index < readback.PageTable.size(); ++index)
        {
            const uint32_t entry = readback.PageTable[index];
            if ((entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) == 0u)
            {
                continue;
            }
            const bool bPoint = index / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL >= pointScene.Lights.FirstSlice;
            (bPoint ? pointPhysical : sunPhysical).push_back(entry & VirtualShadowMap::PAGE_INDEX_MASK);
        }
        std::sort(sunPhysical.begin(), sunPhysical.end());
        std::sort(pointPhysical.begin(), pointPhysical.end());
        Container::VariableArray<uint32_t> shared;
        std::set_intersection(sunPhysical.begin(), sunPhysical.end(), pointPhysical.begin(), pointPhysical.end(), std::back_inserter(shared));
        Expect(shared.empty(), "ケース P: 太陽の段のページと点光源の面のページで、物理ページが重なってはならない");
        Expect(!bNoSun || sunPhysical.empty(), "ケース P: 太陽が無い場面では、太陽の段に割り当ててはならない");
        Expect(readback.Stats[VirtualShadowMap::StatPointRequested] == pointKeys.size(), "ケース P: 点光源の要求の数が参照と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatPointAllocated] == pointPhysical.size(),
               "ケース P: 点光源の割り当ての数が、ページの表で点光源のスライスに割り当てた欄の数と一致しなければならない");
        Expect(readback.Stats[VirtualShadowMap::StatRequested] == requested && readback.Stats[VirtualShadowMap::StatAllocated] == sunPhysical.size() + pointPhysical.size(),
               "ケース P: 要求・割り当ては太陽と点光源の合計でなければならない");
        if (bOverflow)
        {
            Expect(readback.Stats[VirtualShadowMap::StatOverflow] == requested - poolPages && readback.Stats[VirtualShadowMap::StatOverflow] > 0u,
                   "ケース P: プールが足りないとき、溢れを数えなければならない");
            Expect(readback.Stats[VirtualShadowMap::StatPointAllocated] <= readback.Stats[VirtualShadowMap::StatPointRequested],
                   "ケース P: 点光源の割り当ては要求を超えてはならない");
        }
        else
        {
            Expect(readback.Stats[VirtualShadowMap::StatPointAllocated] == readback.Stats[VirtualShadowMap::StatPointRequested],
                   "ケース P: プールが十分なとき、点光源の要求がすべて割り当てられなければならない");
        }
        if (bFullScene && !bOverflow)
        {
            Expect(referenceStats.MipMask != 0u && (referenceStats.MipMask & (referenceStats.MipMask - 1u)) != 0u &&
                       (referenceStats.MipMask & (referenceStats.MipMask - 1u) & ((referenceStats.MipMask & (referenceStats.MipMask - 1u)) - 1u)) != 0u,
                   "ケース P: 3 つ以上の段が使われる場面でなければならない");
            Expect(referenceStats.FaceMask != 0u && (referenceStats.FaceMask & (referenceStats.FaceMask - 1u)) != 0u, "ケース P: 複数の面が使われる場面でなければならない");
            Expect(referenceStats.CrossFacePixels > 0u, "ケース P: 核が面の縁をまたぐ画素がなければならない（隣の面への印の検査）");
        }
        std::cout << TestName << " " << label << ": 要求=" << readback.Stats[VirtualShadowMap::StatRequested] << " 割り当て=" << readback.Stats[VirtualShadowMap::StatAllocated]
                  << " 溢れ=" << readback.Stats[VirtualShadowMap::StatOverflow] << " 点光源の要求=" << readback.Stats[VirtualShadowMap::StatPointRequested]
                  << " 点光源の割り当て=" << readback.Stats[VirtualShadowMap::StatPointAllocated] << std::endl;
        return true;
    }

    // P3: 核を受け手の面の座標の 3 × 3 の標本で表すと隣の面のページを取りこぼす向き（面の縁の近く）で、印が付くページの集合が
    // 核の全体（正方形を各面の錐台で切った範囲）の参照と一致すること。1 画素について、標本だけでは取りこぼす向きの灯を最大 4 つ、
    // 光源からその画素への向きが縁の近くになる位置に置く
    bool RunPointFootprintGapCase(const DevicePtr& device,
                                  VirtualShadowMapPages& pages,
                                  const Scene& scene,
                                  const Container::VariableArray<float>& sunImage,
                                  uint64_t& frameSerial)
    {
        // 地面の安定した画素を 1 つ選ぶ（太陽の曖昧な画素は除いてある）
        uint32_t pixelX = ImageWidth / 2u + 8u;
        const uint32_t pixelY = ImageHeight * 3u / 4u;
        while (pixelX < ImageWidth && !(sunImage[pixelY * ImageWidth + pixelX] < 1.0f))
        {
            ++pixelX;
        }
        Expect(pixelX < ImageWidth, "ケース P3: 地面の画素が見つからない（場面が退化している）");
        if (pixelX >= ImageWidth)
        {
            return false;
        }
        const float depthValue = sunImage[pixelY * ImageWidth + pixelX];
        double world[3] = {};
        Unproject(scene, pixelX, pixelY, static_cast<double>(depthValue), world);
        const double toCamera[3] = {world[0] - scene.CameraPosition[0], world[1] - scene.CameraPosition[1], world[2] - scene.CameraPosition[2]};
        const double cameraDistance = std::sqrt(Dot3(toCamera, toCamera));

        // 向きの候補（光源から画素への相対位置 = 距離 × (縁の近くの値, y, ±1) の並び）を、光源の位置に依存しない参照の関数で選ぶ。
        // 4 種類の縁（+Z/±X・-Z/±X）ごとに、標本が取りこぼすページがあり、曖昧でない最初の向きを 1 つ採る
        const double lightDistance = 40.0;
        PointShadowSnapshot probeSnapshot;
        probeSnapshot.LightCount = 1u;
        probeSnapshot.Lights[0].LightId = 1u;
        probeSnapshot.Lights[0].Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        probeSnapshot.Lights[0].Range = 1000.0f;
        const PointScene probe = BuildPointSceneFrom(scene, probeSnapshot);
        const VirtualShadowMapPointSettings& settings = probe.Lights.Settings;
        const double pixelPerMeter = 2.0 * std::tan(static_cast<double>(scene.Camera.FieldOfView) * 3.14159265358979323846 / 360.0) / ImageHeight;
        const double targetTexel = cameraDistance * pixelPerMeter * std::exp2(static_cast<double>(settings.BiasLevels));

        double chosen[4][3] = {};
        uint32_t chosenCount = 0;
        for (uint32_t kind = 0; kind < 4u; ++kind)
        {
            const double signX = (kind & 1u) != 0u ? -1.0 : 1.0;
            const double signZ = (kind & 2u) != 0u ? -1.0 : 1.0;
            bool bFound = false;
            for (uint32_t gapIndex = 0; gapIndex < 6u && !bFound; ++gapIndex)
            {
                const double gap = 0.0006 + 0.0005 * gapIndex;
                for (uint32_t yIndex = 0; yIndex < 1800u && !bFound; ++yIndex)
                {
                    const double y = -0.9 + 0.001 * yIndex;
                    const double offset[3] = {signX * (1.0 - gap) * lightDistance, y * lightDistance, signZ * lightDistance};
                    uint32_t face = 0;
                    uint32_t mip = 0;
                    if (!SelectPointFaceChecked(offset, face) || !ChoosePointMip(settings, lightDistance, targetTexel, mip))
                    {
                        continue;
                    }
                    const double radius = PointKernelRadius(settings, mip, lightDistance);
                    Container::VariableArray<uint32_t> shrunk;
                    Container::VariableArray<uint32_t> expanded;
                    Container::VariableArray<uint32_t> nominal;
                    Container::VariableArray<uint32_t> nine;
                    CollectPointFootprintKeys(probe, 0u, face, mip, offset, radius * (1.0 - PointFootprintRadiusSlack), -PointFootprintPageSlack, false, shrunk);
                    CollectPointFootprintKeys(probe, 0u, face, mip, offset, radius * (1.0 + PointFootprintRadiusSlack), PointFootprintPageSlack, false, expanded);
                    SortUnique(shrunk);
                    SortUnique(expanded);
                    if (shrunk != expanded)
                    {
                        continue;
                    }
                    nominal = expanded;
                    CollectPointNineSampleKeys(probe, 0u, face, mip, offset, radius, nine);
                    SortUnique(nine);
                    if (std::includes(nine.begin(), nine.end(), nominal.begin(), nominal.end()))
                    {
                        continue;
                    }
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        chosen[chosenCount][axis] = offset[axis];
                    }
                    ++chosenCount;
                    bFound = true;
                }
            }
        }
        Expect(chosenCount >= 3u, "ケース P3: 標本だけでは隣の面のページを取りこぼす向きが見つからない（場面が退化している）");
        if (chosenCount < 3u)
        {
            return false;
        }

        // 画素から chosen[k] だけ戻した位置に灯を置く
        PointShadowSnapshot snapshot;
        snapshot.LightCount = chosenCount;
        for (uint32_t light = 0; light < chosenCount; ++light)
        {
            snapshot.Lights[light].LightId = 201u + light;
            snapshot.Lights[light].Position = Math::Vector3(static_cast<float>(world[0] - chosen[light][0]),
                                                            static_cast<float>(world[1] - chosen[light][1]),
                                                            static_cast<float>(world[2] - chosen[light][2]));
            snapshot.Lights[light].Range = 200.0f;
        }
        const PointScene pointScene = BuildPointSceneFrom(scene, snapshot);

        // 置いた灯でも、標本が取りこぼすページが参照に入っていること（浮動小数の位置の丸めで向きが変わっていないこと）
        uint32_t gapLights = 0;
        for (uint32_t light = 0; light < chosenCount; ++light)
        {
            const double offset[3] = {world[0] - pointScene.Lights.Position[light].x,
                                      world[1] - pointScene.Lights.Position[light].y,
                                      world[2] - pointScene.Lights.Position[light].z};
            uint32_t face = 0;
            uint32_t mip = 0;
            if (!SelectPointFaceChecked(offset, face))
            {
                continue;
            }
            const GPUVsmSlice& slice = pointScene.Slices[VirtualShadowMapPointSliceIndex(pointScene.Lights, light, face, 0u)];
            const double major[3] = {slice.axisZ[0], slice.axisZ[1], slice.axisZ[2]};
            const double axial = Dot3(major, offset);
            if (!ChoosePointMip(pointScene.Lights.Settings, axial, targetTexel, mip))
            {
                continue;
            }
            const double radius = PointKernelRadius(pointScene.Lights.Settings, mip, axial);
            Container::VariableArray<uint32_t> nominal;
            Container::VariableArray<uint32_t> nine;
            CollectPointFootprintKeys(pointScene, light, face, mip, offset, radius, 0.0, false, nominal);
            CollectPointNineSampleKeys(pointScene, light, face, mip, offset, radius, nine);
            SortUnique(nominal);
            SortUnique(nine);
            gapLights += std::includes(nine.begin(), nine.end(), nominal.begin(), nominal.end()) ? 0u : 1u;
        }
        Expect(gapLights >= 3u, "ケース P3: 標本だけでは隣の面のページを取りこぼす灯が 3 つ以上なければならない");
        std::cout << TestName << " ケース P3: 画素=(" << pixelX << "," << pixelY << ") 灯=" << chosenCount << " 標本だけでは取りこぼす灯=" << gapLights << std::endl;

        Container::VariableArray<float> single(ImageWidth * ImageHeight, 1.0f);
        single[pixelY * ImageWidth + pixelX] = depthValue;
        return RunPointMarkOnce(device, pages, scene, pointScene, "P3", single, false, false, frameSerial);
    }

    bool RunPointMarkCases(const DevicePtr& device,
                           VirtualShadowMapPages& pages,
                           const Scene& scene,
                           const Container::VariableArray<float>& sunImage,
                           uint64_t& frameSerial)
    {
        const PointScene pointScene = BuildPointScene(scene);
        Expect(pointScene.Lights.LightCount == 2u && pointScene.Lights.SliceCount() == 72u, "ケース P: 2 灯 × 6 面 × 6 段のスライスが並ばなければならない");

        Container::VariableArray<float> image = sunImage;
        const uint32_t removed = RemoveAmbiguousPointPixels(scene, pointScene, image);
        std::cout << TestName << " ケース P: 点光源の分で除いた曖昧な画素=" << removed << std::endl;

        // P1: 全画素。P1b: プールが要求の約半分（溢れ）
        if (!RunPointMarkOnce(device, pages, scene, pointScene, "P1", image, false, true, frameSerial) ||
            !RunPointMarkOnce(device, pages, scene, pointScene, "P1b", image, true, true, frameSerial))
        {
            return false;
        }

        // P2: 核が面の縁をまたぐ画素だけ（隣の面の同じ段のページへの印）。受け手の面だけに印を付ける印付け（変異）の鍵より、参照の鍵が多い画素を選ぶ
        Container::VariableArray<float> sparse(ImageWidth * ImageHeight, 1.0f);
        Container::VariableArray<uint32_t> allKeys;
        Container::VariableArray<uint32_t> allNoCrossKeys;
        uint32_t picked = 0;
        for (uint32_t pixelY = 0; pixelY < ImageHeight && picked < 6u; pixelY += 2u)
        {
            for (uint32_t pixelX = 0; pixelX < ImageWidth && picked < 6u; pixelX += 3u)
            {
                Container::VariableArray<uint32_t> keys;
                Container::VariableArray<uint32_t> noCrossKeys;
                const float depthValue = image[pixelY * ImageWidth + pixelX];
                if (ClassifyPointPixel(scene, pointScene, pixelX, pixelY, depthValue, &keys, &noCrossKeys, nullptr) != PixelKind::Stable)
                {
                    continue;
                }
                SortUnique(keys);
                SortUnique(noCrossKeys);
                if (keys == noCrossKeys)
                {
                    continue;
                }
                sparse[pixelY * ImageWidth + pixelX] = depthValue;
                allKeys.insert(allKeys.end(), keys.begin(), keys.end());
                allNoCrossKeys.insert(allNoCrossKeys.end(), noCrossKeys.begin(), noCrossKeys.end());
                ++picked;
            }
        }
        SortUnique(allKeys);
        SortUnique(allNoCrossKeys);
        Expect(picked >= 3u && allKeys != allNoCrossKeys && allKeys.size() > allNoCrossKeys.size(),
               "ケース P2: 核が面の縁をまたぎ、隣の面のページが増える画素が見つからない（場面が退化している）");
        std::cout << TestName << " ケース P2: 選んだ画素=" << picked << " 点光源のページ=" << allKeys.size() << "（隣の面へ印を付けないと " << allNoCrossKeys.size()
                  << "）" << std::endl;
        if (!RunPointMarkOnce(device, pages, scene, pointScene, "P2", sparse, false, false, frameSerial))
        {
            return false;
        }

        // P3: 標本だけでは隣の面のページを取りこぼす向き。N1: 太陽が無い場面（夜）でも点光源だけで印付け・割り当てをする
        return RunPointFootprintGapCase(device, pages, scene, image, frameSerial) &&
               RunPointMarkOnce(device, pages, scene, pointScene, "N1", image, false, true, frameSerial, true);
    }

    // ========================================
    // ケース R: 点光源の面のスライスへの展開・描画
    // ========================================
    //
    // 1 灯（光源 (3, 2, -1)・Range 20）の面 0 の段 0（4096²・32 ページ四方）と、全 6 面の段 2（1024²・8 ページ四方）に、ホストが割り当て済み・dirty
    // で書いたページを置き、ワールドの多角形（面の軸に垂直な四角形・傾いた四角形・面 0 と隣の面の境をまたぐ四角形・光源の後ろの頂点を持つ三角形・
    // 小さな四角形・Range の外の四角形）の塊を展開 → 描画する。期待値は倍精度で、texel の中心を通る光線と多角形の交点までの
    // 面の軸の向きの距離 ÷ Range を求め、交点が多角形の内側にあるかで決める（多角形の縁・近い平面・Range の近くは曖昧として比べない）。
    // 塊の段の印は本番の関数（SliceMasksForBounds・PushChunkPerGroup）が組ごとに作り、Range の外の塊は印が空で記録にならない。
    // 太陽のクリップマップを無効にして渡した場合（点光源だけのフレーム）も、同じ結果になることを確かめる。

    struct PointPolygon
    {
        uint32_t Count = 0;
        double P[4][3] = {};
        const char* Name = "";
    };

    struct PointRasterPageSpec
    {
        uint32_t Slice = 0;
        uint32_t PageX = 0;
        uint32_t PageY = 0;
    };

    struct PointRasterReference
    {
        double Light[3] = {};
        double Range = 1.0;
        double Near = 0.05;
        Container::VariableArray<PointPolygon> Polygons;
    };

    enum class PointTexelKind
    {
        Empty,
        Covered,
        Ambiguous
    };

    // 面のスライス slice のページ (pageX, pageY) の texel (texelX, texelY) の期待値。Covered のとき outDepth に 面の軸の向きの距離 ÷ Range
    PointTexelKind ClassifyPointTexel(const PointRasterReference& reference,
                                      const GPUVsmSlice& slice,
                                      uint32_t pageX,
                                      uint32_t pageY,
                                      uint32_t texelX,
                                      uint32_t texelY,
                                      double& outDepth,
                                      double& outNdcX,
                                      double& outNdcY)
    {
        const double texelNdc = static_cast<double>(slice.info[1]);
        const double ndcX = -1.0 + (static_cast<double>(pageX) * VirtualShadowMap::PAGE_RESOLUTION + texelX + 0.5) * texelNdc;
        const double ndcY = -1.0 + (static_cast<double>(pageY) * VirtualShadowMap::PAGE_RESOLUTION + texelY + 0.5) * texelNdc;
        outNdcX = ndcX;
        outNdcY = ndcY;
        double direction[3] = {};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            direction[axis] = static_cast<double>(slice.axisZ[axis]) + ndcX * static_cast<double>(slice.axisX[axis]) +
                              ndcY * static_cast<double>(slice.axisY[axis]);
        }
        const double directionLength = std::sqrt(Dot3(direction, direction));
        bool bCovered = false;
        bool bAmbiguous = false;
        double best = 1.0e30;
        for (const PointPolygon& polygon : reference.Polygons)
        {
            double edge1[3] = {};
            double edge2[3] = {};
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                edge1[axis] = polygon.P[1][axis] - polygon.P[0][axis];
                edge2[axis] = polygon.P[2][axis] - polygon.P[0][axis];
            }
            double normal[3] = {edge1[1] * edge2[2] - edge1[2] * edge2[1], edge1[2] * edge2[0] - edge1[0] * edge2[2],
                                edge1[0] * edge2[1] - edge1[1] * edge2[0]};
            const double normalLength = std::sqrt(Dot3(normal, normal));
            for (double& component : normal)
            {
                component /= normalLength;
            }
            const double denominator = Dot3(normal, direction);
            if (std::abs(denominator) < 1.0e-9)
            {
                continue;
            }
            double toPlane[3] = {polygon.P[0][0] - reference.Light[0], polygon.P[0][1] - reference.Light[1], polygon.P[0][2] - reference.Light[2]};
            const double axial = Dot3(normal, toPlane) / denominator;
            // 近い平面・Range の近くは、単精度の GPU が別の側に落としうるので曖昧にする
            if (std::abs(axial - reference.Near) < 0.03 || std::abs(axial - reference.Range) < 0.03)
            {
                bAmbiguous = true;
                continue;
            }
            if (axial < reference.Near || axial > reference.Range)
            {
                continue;
            }
            const double hit[3] = {reference.Light[0] + axial * direction[0], reference.Light[1] + axial * direction[1],
                                   reference.Light[2] + axial * direction[2]};
            double minDistance = 1.0e30;
            for (uint32_t edge = 0; edge < polygon.Count; ++edge)
            {
                const double* from = polygon.P[edge];
                const double* to = polygon.P[(edge + 1u) % polygon.Count];
                const double edgeVector[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                const double offset[3] = {hit[0] - from[0], hit[1] - from[1], hit[2] - from[2]};
                const double cross[3] = {edgeVector[1] * offset[2] - edgeVector[2] * offset[1], edgeVector[2] * offset[0] - edgeVector[0] * offset[2],
                                         edgeVector[0] * offset[1] - edgeVector[1] * offset[0]};
                minDistance = std::min(minDistance, Dot3(cross, normal) / std::sqrt(Dot3(edgeVector, edgeVector)));
            }
            // texel 2 つぶんの長さ（軸の距離 axial での 1 texel = axial × texelNdc × |direction|）
            const double tolerance = 2.0 * axial * texelNdc * directionLength;
            if (minDistance > tolerance)
            {
                bCovered = true;
                best = std::min(best, axial / reference.Range);
            }
            else if (minDistance > -tolerance)
            {
                bAmbiguous = true;
            }
        }
        if (bAmbiguous)
        {
            return PointTexelKind::Ambiguous;
        }
        if (bCovered)
        {
            outDepth = best;
            return PointTexelKind::Covered;
        }
        return PointTexelKind::Empty;
    }

    // ワールドの多角形を、物理ページの描画の塊（ワールド空間の頂点・単位行列）にする。境界は頂点から決める
    ChunkGeometry BuildPointPolygonChunks(const Container::VariableArray<PointPolygon>& polygons)
    {
        ChunkGeometry geometry;
        geometry.Indices.push_back(0u);
        for (const PointPolygon& polygon : polygons)
        {
            VsmShadowChunk chunk;
            chunk.Record.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
            chunk.Record.TriangleCount = polygon.Count == 4u ? 2u : 1u;
            chunk.Record.FirstIndex = static_cast<uint32_t>(geometry.Indices.size());
            chunk.Record.VertexBase = static_cast<uint32_t>(geometry.Vertices.size() / 8u);
            float boundsMin[3] = {1.0e30f, 1.0e30f, 1.0e30f};
            float boundsMax[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
            for (uint32_t corner = 0; corner < polygon.Count; ++corner)
            {
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    const float value = static_cast<float>(polygon.P[corner][axis]);
                    boundsMin[axis] = std::min(boundsMin[axis], value);
                    boundsMax[axis] = std::max(boundsMax[axis], value);
                    geometry.Vertices.push_back(value);
                }
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

    // ホストが書いたページの表（pageSpecs の順に物理ページ 0, 1, … を割り当て済み・dirty）と、何も無い深度で埋めた物理ページへ、展開 → 描画を記録して読み戻す
    bool RunPointRasterFrame(const DevicePtr& device,
                             VirtualShadowMapRaster& raster,
                             const VirtualShadowMapClipmap& clipmap,
                             const Resources& resources,
                             const RasterBuffers& rasterBuffers,
                             uint32_t sliceCount,
                             const GPUVsmSlice* slices,
                             const Container::VariableArray<PointRasterPageSpec>& pageSpecs,
                             uint64_t frameSerial,
                             RasterReadback& readback)
    {
        FillWords(resources.Pool, VirtualShadowMap::EMPTY_DEPTH_BITS);
        FillWords(resources.PageTable, 0u);
        FillWords(resources.Stats, 0u);
        {
            uint32_t* table = static_cast<uint32_t*>(resources.PageTable->Map(0u, resources.PageTable->GetSize()));
            if (table == nullptr)
            {
                return false;
            }
            for (uint32_t physical = 0; physical < static_cast<uint32_t>(pageSpecs.size()); ++physical)
            {
                const PointRasterPageSpec& spec = pageSpecs[physical];
                const GPUVsmSlice& slice = slices[spec.Slice];
                const uint32_t mask = static_cast<uint32_t>(slice.origin[3]) - 1u;
                const uint32_t index = static_cast<uint32_t>(slice.origin[2]) + (spec.PageY & mask) * static_cast<uint32_t>(slice.origin[3]) + (spec.PageX & mask);
                table[index] = VirtualShadowMap::PAGE_ENTRY_ALLOCATED | VirtualShadowMap::PAGE_ENTRY_DIRTY | physical;
            }
            resources.PageTable->Unmap();
        }

        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            return false;
        }
        const BufferPtr buffers[] = {resources.Pool,  resources.PageTable, resources.Stats,       rasterBuffers.Chunks,
                                     rasterBuffers.Instances, rasterBuffers.Draws};
        raster.BeginFrame(0, frameSerial);
        commandList->Begin();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        VirtualShadowMapRasterDispatch dispatch;
        dispatch.Clipmap = &clipmap;
        dispatch.SliceCount = sliceCount;
        dispatch.Slices = slices;
        dispatch.PoolPages = resources.PoolPages;
        dispatch.Pool = resources.Pool;
        dispatch.PageTable = resources.PageTable;
        dispatch.Stats = resources.Stats;
        dispatch.Chunks = rasterBuffers.Chunks;
        dispatch.ChunkCount = rasterBuffers.ChunkCount;
        dispatch.Instances = rasterBuffers.Instances;
        dispatch.Draws = rasterBuffers.Draws;
        readback.bRasterRecorded = raster.Record(commandList.get(), dispatch);
        readback.DrawCount = raster.GetLastDrawCount();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();
        return ReadAll(resources.Pool, readback.Pool) && ReadAll(resources.Stats, readback.Stats) && ReadAll(rasterBuffers.Draws, readback.Draws) &&
               ReadAll(rasterBuffers.Instances, readback.Instances);
    }

    // ========================================
    // ケース S: 点光源の VSM の読み出し（Common/VirtualShadowMapPoint.glsl の VsmSamplePointShadow。照明と同じ関数を vsm_sample_probe.comp から呼ぶ）
    // ========================================
    //
    // ケース R の物理ページ（四角形が描かれている）と、その場面の灯・スライスの表を、受け手の点から読む。受け手は光源から見て四角形 Q1（軸の距離 8）の
    // 真後ろ（軸の距離 12）に、光源を向いた面（法線は面 0 の軸の逆向き。法線の向きへのずらしも受け面の傾きも小さい）として置く。
    // 縁・影の外の受け手は、光源の後ろへ伸びる三角形 T（面 0 の NDC では (0.167, 0) から右下へ広がる）の影を避けて、NDC の v = 0.3 付近に置く。
    //   S1（影の中心）: 四角形に覆われた中心の受け手は 0。使った段の texel は受け手の段（段 0）の値。逃げた標本は 0。
    //   S2（影の外）: 四角形の隙間（Q1 と Q3 の間）の受け手は 1。
    //   S3（縁）: Q1 の縁（面の NDC u = 0.3125）をまたいで受け手を 0.00025（NDC）刻みで動かす。縁の手前で 0、向こうで 1、帯の中で途中の値になり、
    //       可視度は u について減らない。各受け手の値は、独立の参照（Poisson の 16 点それぞれの面・ページ・texel を倍精度で求め、多角形との光線の交点で影を判定する）と
    //       曖昧な標本の数 ÷ 16 の範囲で一致する。
    //   S4（割り当てのないページ）: 段 0 のページが無い位置（u = -0.45）の受け手は、粗い段（段 2）の値になる。参照も段 2 の多角形の交点で決める。逃げた標本は 16。
    //       粗い段へ逃げた標本の texel は、読んだ（粗い）段の値。
    //   S5（どの段にも無い）: 受け手の段を 3 にすると（段 3 以上のページは無い）、影の中心でもキューブの値ではなく影なし（1）。逃げた標本は 16。
    //   S6（面の境）: 面 0 と隣の面の境（NDC u = 1）をまたぐ PCF の円盤が、両方の面のページを読んで Q3 の影（0）になる。

    struct PointSampleReceiver
    {
        double Position[3] = {};
        double Normal[3] = {};
    };

    struct PointSampleReference
    {
        double Visibility = 1.0;
        /** @brief 曖昧な標本（texel の中の多角形の縁・遮る物との深度の差が小さい）の数 */
        uint32_t Ambiguous = 0;
        /** @brief 自分の段のページが無く粗い段へ進んだ標本の数 */
        uint32_t Escaped = 0;
        uint32_t Mip = 0;
        double ReceiverTexel = 0.0;
    };

    // 面のスライスのページ (sliceIndex, pageX, pageY) が割り当て済みか（ケース R のページの並び）
    bool IsPointPageAllocated(const Container::VariableArray<PointRasterPageSpec>& pageSpecs, uint32_t sliceIndex, uint32_t pageX, uint32_t pageY)
    {
        for (const PointRasterPageSpec& spec : pageSpecs)
        {
            if (spec.Slice == sliceIndex && spec.PageX == pageX && spec.PageY == pageY)
            {
                return true;
            }
        }
        return false;
    }

    // シェーダーの VsmSamplePointShadow の独立した参照。受け手の面・段・円盤・各標本の面とページを倍精度で求め、標本ごとの遮る物は
    // 多角形との光線の交点（ClassifyPointTexel。texel の中心を通る光線）で決める。深度の比較の余裕は見ない（遮る物と受け手の差が大きい受け手だけを使う）
    PointSampleReference ReferencePointVisibility(const PointRasterReference& reference,
                                                  const PointScene& pointScene,
                                                  const Container::VariableArray<PointRasterPageSpec>& pageSpecs,
                                                  const PointSampleReceiver& receiver,
                                                  double cameraDistance,
                                                  float fovYDegrees,
                                                  float screenHeightPixels)
    {
        PointSampleReference result;
        const VirtualShadowMapPointSettings& settings = pointScene.Lights.Settings;
        const auto sliceOf = [&](uint32_t face, uint32_t mip) { return VirtualShadowMapPointSliceIndex(pointScene.Lights, 0u, face, mip); };
        double offset[3];
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            offset[axis] = receiver.Position[axis] - reference.Light[axis];
        }
        const uint32_t face = SelectVirtualShadowMapPointFace(static_cast<float>(offset[0]), static_cast<float>(offset[1]), static_cast<float>(offset[2]));
        const GPUVsmSlice& faceSlice = pointScene.Slices[sliceOf(face, 0u)];
        const double faceAxis[3] = {faceSlice.axisZ[0], faceSlice.axisZ[1], faceSlice.axisZ[2]};
        const double axial = Dot3(faceAxis, offset);
        const double pixelPerMeter = static_cast<double>(VirtualShadowMapScreenPixelMeters(1.0f, fovYDegrees, screenHeightPixels));
        const double target = cameraDistance * pixelPerMeter * std::exp2(static_cast<double>(settings.BiasLevels));
        uint32_t mip = 0u;
        for (uint32_t candidate = settings.MipCount; candidate-- > 0u;)
        {
            if (2.0 * axial / static_cast<double>(settings.FaceResolution >> candidate) <= target)
            {
                mip = candidate;
                break;
            }
        }
        result.Mip = mip;
        result.ReceiverTexel = 2.0 * axial / static_cast<double>(settings.FaceResolution >> mip);

        const double distance = std::sqrt(Dot3(offset, offset));
        const double toLight[3] = {-offset[0] / distance, -offset[1] / distance, -offset[2] / distance};
        double normal[3] = {receiver.Normal[0], receiver.Normal[1], receiver.Normal[2]};
        const double normalLength = std::sqrt(Dot3(normal, normal));
        for (double& component : normal)
        {
            component /= normalLength;
        }
        double normalDotLight = Dot3(normal, toLight);
        if (normalDotLight < 0.0)
        {
            for (double& component : normal)
            {
                component = -component;
            }
            normalDotLight = -normalDotLight;
        }
        const double normalOffset = result.ReceiverTexel * (0.6 + 1.4 * (1.0 - std::clamp(normalDotLight, 0.0, 1.0)));
        double receiverPosition[3];
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            receiverPosition[axis] = receiver.Position[axis] + normal[axis] * normalOffset;
        }
        const double helper[3] = {std::abs(normal[1]) < 0.99 ? 0.0 : 1.0, std::abs(normal[1]) < 0.99 ? 1.0 : 0.0, 0.0};
        double tangent[3] = {helper[1] * normal[2] - helper[2] * normal[1], helper[2] * normal[0] - helper[0] * normal[2],
                             helper[0] * normal[1] - helper[1] * normal[0]};
        const double tangentLength = std::sqrt(Dot3(tangent, tangent));
        for (double& component : tangent)
        {
            component /= tangentLength;
        }
        const double bitangent[3] = {normal[1] * tangent[2] - normal[2] * tangent[1], normal[2] * tangent[0] - normal[0] * tangent[2],
                                     normal[0] * tangent[1] - normal[1] * tangent[0]};
        const double radius = std::max(cameraDistance * pixelPerMeter * static_cast<double>(VirtualShadowMap::PCF_MIN_RADIUS_PIXELS), result.ReceiverTexel);

        uint32_t lit = 0;
        for (uint32_t index = 0; index < 16u; ++index)
        {
            double tap[3];
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                tap[axis] = receiverPosition[axis] - reference.Light[axis] + tangent[axis] * ReferencePoissonDisk[index][0] * radius +
                            bitangent[axis] * ReferencePoissonDisk[index][1] * radius;
            }
            const uint32_t tapFace = SelectVirtualShadowMapPointFace(static_cast<float>(tap[0]), static_cast<float>(tap[1]), static_cast<float>(tap[2]));
            const GPUVsmSlice& tapSlice = pointScene.Slices[sliceOf(tapFace, mip)];
            const double axisX[3] = {tapSlice.axisX[0], tapSlice.axisX[1], tapSlice.axisX[2]};
            const double axisY[3] = {tapSlice.axisY[0], tapSlice.axisY[1], tapSlice.axisY[2]};
            const double axisZ[3] = {tapSlice.axisZ[0], tapSlice.axisZ[1], tapSlice.axisZ[2]};
            const double tapAxial = Dot3(axisZ, tap);
            bool bVisible = true;
            bool bEscaped = false;
            if (tapAxial >= reference.Near && tapAxial <= reference.Range)
            {
                const double ndcX = std::clamp(Dot3(axisX, tap) / tapAxial, -1.0, 1.0);
                const double ndcY = std::clamp(Dot3(axisY, tap) / tapAxial, -1.0, 1.0);
                for (uint32_t candidate = mip; candidate < settings.MipCount; ++candidate)
                {
                    const uint32_t sliceIndex = sliceOf(tapFace, candidate);
                    const GPUVsmSlice& candidateSlice = pointScene.Slices[sliceIndex];
                    const uint32_t pages = static_cast<uint32_t>(candidateSlice.origin[3]);
                    const double scaledX = (ndcX * 0.5 + 0.5) * pages;
                    const double scaledY = (ndcY * 0.5 + 0.5) * pages;
                    const uint32_t pageX = std::min(static_cast<uint32_t>(scaledX), pages - 1u);
                    const uint32_t pageY = std::min(static_cast<uint32_t>(scaledY), pages - 1u);
                    if (!IsPointPageAllocated(pageSpecs, sliceIndex, pageX, pageY))
                    {
                        bEscaped = true;
                        continue;
                    }
                    const uint32_t texelX = std::min(static_cast<uint32_t>((scaledX - pageX) * VirtualShadowMap::PAGE_RESOLUTION), VirtualShadowMap::PAGE_RESOLUTION - 1u);
                    const uint32_t texelY = std::min(static_cast<uint32_t>((scaledY - pageY) * VirtualShadowMap::PAGE_RESOLUTION), VirtualShadowMap::PAGE_RESOLUTION - 1u);
                    double depth = 0.0;
                    double texelNdcX = 0.0;
                    double texelNdcY = 0.0;
                    const PointTexelKind kind = ClassifyPointTexel(reference, candidateSlice, pageX, pageY, texelX, texelY, depth, texelNdcX, texelNdcY);
                    if (kind == PointTexelKind::Ambiguous)
                    {
                        ++result.Ambiguous;
                    }
                    else if (kind == PointTexelKind::Covered)
                    {
                        const double occluder = depth * reference.Range;
                        if (std::abs(tapAxial - occluder) < 0.5)
                        {
                            ++result.Ambiguous;
                        }
                        else if (tapAxial > occluder)
                        {
                            bVisible = false;
                        }
                    }
                    break;
                }
            }
            lit += bVisible ? 1u : 0u;
            result.Escaped += bEscaped ? 1u : 0u;
        }
        result.Visibility = static_cast<double>(lit) / 16.0;
        return result;
    }

    bool RunPointSampleCases(const DevicePtr& device,
                             ShaderManager& shaderManager,
                             const PointRasterReference& reference,
                             const PointScene& pointScene,
                             const Container::VariableArray<PointRasterPageSpec>& pageSpecs,
                             const RasterReadback& readback,
                             const Resources& resources)
    {
        SampleProbe probe;
        if (!CreateSampleProbe(device, shaderManager, probe))
        {
            std::cerr << TestName << " ケース S の計算シェーダーを作れませんでした" << std::endl;
            return false;
        }
        Container::VariableArray<uint32_t> tableWords;
        if (!ReadAll(resources.PageTable, tableWords))
        {
            return false;
        }
        const auto sliceOf = [&](uint32_t face, uint32_t mip) { return VirtualShadowMapPointSliceIndex(pointScene.Lights, 0u, face, mip); };
        const GPUVsmSlice& face0 = pointScene.Slices[sliceOf(0u, 0u)];
        const double sc[3] = {face0.axisX[0], face0.axisX[1], face0.axisX[2]};
        const double tc[3] = {face0.axisY[0], face0.axisY[1], face0.axisY[2]};
        const double major[3] = {face0.axisZ[0], face0.axisZ[1], face0.axisZ[2]};
        constexpr float FovYDegrees = 60.0f;
        constexpr float ScreenHeight = 720.0f;

        // 面 0 の NDC (u, v)・軸の距離 axial の受け手。法線は光源を向く（面 0 の軸の逆向き）
        const auto makeReceiver = [&](double u, double v, double axial) {
            PointSampleReceiver receiver;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                receiver.Position[axis] = reference.Light[axis] + axial * (major[axis] + u * sc[axis] + v * tc[axis]);
                receiver.Normal[axis] = -major[axis];
            }
            return receiver;
        };

        struct SampleBatch
        {
            Container::VariableArray<PointSampleReceiver> Receivers;
            double CameraDistance = 0.0;
        };
        // 1 回の評価で使う受け手の組と、カメラまでの距離（受け手の段を決める）。段 0 は受け手から 2 m、段 2 は 30 m、段 3 は 60 m
        const auto runBatch = [&](const SampleBatch& batch, SampleOutput& out) {
            Container::VariableArray<SampleProbePoint> points;
            for (const PointSampleReceiver& receiver : batch.Receivers)
            {
                SampleProbePoint point = {};
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    point.Position[axis] = static_cast<float>(receiver.Position[axis]);
                    point.Normal[axis] = static_cast<float>(receiver.Normal[axis]);
                }
                points.push_back(point);
            }
            // カメラは先頭の受け手から、世界の +Y へ CameraDistance 離す（受け手の組は互いに近いので、距離の差は段を変えない）
            const float cameraPosition[3] = {static_cast<float>(batch.Receivers[0].Position[0]),
                                             static_cast<float>(batch.Receivers[0].Position[1] + batch.CameraDistance),
                                             static_cast<float>(batch.Receivers[0].Position[2])};
            SampleProbeParams params = {};
            if (!BuildVirtualShadowMapPointSampleParams(pointScene.Lights, cameraPosition, FovYDegrees, ScreenHeight, resources.PoolPages, params.Point))
            {
                return false;
            }
            params.Control[1] = 1u;
            params.Control[2] = 0u;
            return RunSampleProbeWith(device, probe, pointScene.Slices, pointScene.SliceCount, params, readback.Pool, tableWords, points, out);
        };
        const auto referenceOf = [&](const SampleBatch& batch, size_t index) {
            return ReferencePointVisibility(reference, pointScene, pageSpecs, batch.Receivers[index], batch.CameraDistance, FovYDegrees, ScreenHeight);
        };

        // 受け手の段ごとの texel（軸の距離 12）
        const double texel0 = 2.0 * 12.0 / 4096.0;
        const double texel2 = 2.0 * 12.0 / 1024.0;
        const double texel3 = 2.0 * 12.0 / 512.0;

        // ----- S1: 影の中心 -----
        {
            SampleBatch batch;
            batch.CameraDistance = 2.0;
            const double uv[5][2] = {{-0.1, 0.2}, {0.0, 0.1}, {0.1, -0.05}, {-0.2, 0.3}, {0.2, 0.25}};
            for (const auto& entry : uv)
            {
                batch.Receivers.push_back(makeReceiver(entry[0], entry[1], 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S1 を実行できませんでした" << std::endl;
                return false;
            }
            bool bAllShadow = true;
            bool bTexel = true;
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                bAllShadow = bAllShadow && out.Visibility[index] == 0.0f;
                bTexel = bTexel && std::abs(static_cast<double>(out.TexelMeters[index]) - texel0) < 0.05 * texel0;
            }
            Expect(bAllShadow, "ケース S1: 四角形に覆われた中心の受け手は影（0）でなければならない");
            Expect(bTexel, "ケース S1: 使った段の texel の一辺は受け手の段（段 0）の値でなければならない");
            Expect(out.Fallback == 0u, "ケース S1: 段 0 のページが割り当て済みの受け手は、粗い段へ逃げてはならない");
            std::cout << TestName << " ケース S1: 影の中心 5 点 可視度=" << out.Visibility[0] << " texel=" << out.TexelMeters[0] * 1000.0 << " mm 逃げた=" << out.Fallback << std::endl;
        }

        // ----- S2: 影の外（Q1 の縁 u = 0.3125 と Q3 の縁 u = 0.375 の間） -----
        {
            SampleBatch batch;
            batch.CameraDistance = 2.0;
            const double uv[3][2] = {{0.34, 0.3}, {0.34, 0.35}, {0.345, 0.45}};
            for (const auto& entry : uv)
            {
                batch.Receivers.push_back(makeReceiver(entry[0], entry[1], 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S2 を実行できませんでした" << std::endl;
                return false;
            }
            bool bAllLit = true;
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                bAllLit = bAllLit && out.Visibility[index] == 1.0f;
            }
            Expect(bAllLit, "ケース S2: 四角形の外の受け手は光が当たらなければならない（1）");
            Expect(out.Fallback == 0u, "ケース S2: 段 0 のページが割り当て済みの受け手は、粗い段へ逃げてはならない");
        }

        // ----- S3: 縁 -----
        {
            SampleBatch batch;
            batch.CameraDistance = 2.0;
            constexpr uint32_t EdgeCount = 13;
            for (uint32_t index = 0; index < EdgeCount; ++index)
            {
                batch.Receivers.push_back(makeReceiver(0.3125 + (static_cast<double>(index) - 6.0) * 0.00025, 0.3, 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S3 を実行できませんでした" << std::endl;
                return false;
            }
            uint32_t partial = 0;
            bool bMonotonic = true;
            uint32_t referenceMismatch = 0;
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                const float value = out.Visibility[index];
                partial += (value > 0.0f && value < 1.0f) ? 1u : 0u;
                if (index > 0)
                {
                    bMonotonic = bMonotonic && value + 1.0e-6f >= out.Visibility[index - 1];
                }
                const PointSampleReference expected = referenceOf(batch, index);
                const double tolerance = static_cast<double>(expected.Ambiguous) / 16.0 + 1.0e-4;
                referenceMismatch += std::abs(static_cast<double>(value) - expected.Visibility) > tolerance ? 1u : 0u;
            }
            std::cout << TestName << " ケース S3: 縁 13 点の可視度=";
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                std::cout << out.Visibility[index] << (index + 1 < batch.Receivers.size() ? "," : "");
            }
            std::cout << " 途中の値=" << partial << " 参照との不一致=" << referenceMismatch << std::endl;
            Expect(out.Visibility[0] == 0.0f && out.Visibility[EdgeCount - 1u] == 1.0f, "ケース S3: 縁の手前は影（0）、向こうは光（1）でなければならない");
            Expect(partial >= 2u, "ケース S3: 縁の帯の中で途中の値（0 と 1 の間）が出なければならない");
            Expect(bMonotonic, "ケース S3: 縁をまたいで動かすと、可視度は減ってはならない");
            Expect(referenceMismatch == 0u, "ケース S3: 縁の値は、Poisson の 16 点の面・ページ・texel と多角形との光線の交点から求めた参照と曖昧な標本の範囲で一致しなければならない");
        }

        // ----- S4: 割り当てのないページ（段 0 のページが無い u = -0.45）は粗い段（段 2）の値 -----
        {
            SampleBatch batch;
            batch.CameraDistance = 2.0;
            const double uv[3][2] = {{-0.45, 0.3}, {-0.46, 0.35}, {-0.44, 0.4}};
            for (const auto& entry : uv)
            {
                batch.Receivers.push_back(makeReceiver(entry[0], entry[1], 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S4 を実行できませんでした" << std::endl;
                return false;
            }
            bool bMatches = true;
            bool bTexel = true;
            uint32_t escapedReference = 0;
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                const PointSampleReference expected = referenceOf(batch, index);
                const double tolerance = static_cast<double>(expected.Ambiguous) / 16.0 + 1.0e-4;
                bMatches = bMatches && std::abs(static_cast<double>(out.Visibility[index]) - expected.Visibility) <= tolerance;
                escapedReference += expected.Escaped;
                bTexel = bTexel && std::abs(static_cast<double>(out.TexelMeters[index]) - texel2) < 0.05 * texel2;
            }
            std::cout << TestName << " ケース S4: 段 0 のページ無し 可視度=" << out.Visibility[0] << " texel=" << out.TexelMeters[0] * 1000.0
                      << " mm 逃げた=" << out.Fallback << "（参照 " << escapedReference << "）" << std::endl;
            Expect(out.Visibility[0] == 0.0f, "ケース S4: 段 0 のページが無い影の中の受け手は、粗い段（段 2）の値（影 = 0）にならなければならない");
            Expect(bMatches, "ケース S4: 値は、粗い段の多角形の交点から求めた参照と一致しなければならない");
            Expect(bTexel, "ケース S4: 粗い段へ逃げた標本の texel の一辺は、読んだ（段 2 の）値でなければならない");
            Expect(out.Fallback == escapedReference && out.Fallback == 16u * static_cast<uint32_t>(batch.Receivers.size()),
                   "ケース S4: 逃げた標本の数は、受け手 × 16 点でなければならない");
        }

        // ----- S5: どの段にも無い（受け手の段 3 以上のページは無い）は影なし -----
        {
            SampleBatch batch;
            batch.CameraDistance = 60.0;
            const double uv[2][2] = {{-0.1, 0.2}, {0.0, 0.1}};
            for (const auto& entry : uv)
            {
                batch.Receivers.push_back(makeReceiver(entry[0], entry[1], 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S5 を実行できませんでした" << std::endl;
                return false;
            }
            Expect(out.Visibility[0] == 1.0f && out.Visibility[1] == 1.0f,
                   "ケース S5: どの段にもページが無い受け手は、キューブの値ではなく影なし（1）でなければならない");
            Expect(out.Fallback == 16u * static_cast<uint32_t>(batch.Receivers.size()), "ケース S5: 逃げた標本の数は、受け手 × 16 点でなければならない");
            Expect(std::abs(static_cast<double>(out.TexelMeters[0]) - texel3) < 0.05 * texel3, "ケース S5: どの標本も読めなかったときの texel の一辺は、受け手の段（段 3）の値でなければならない");
        }

        // ----- S6: 面 0 と隣の面の境（u = 1）をまたぐ円盤。Q3 の影の中（段 2。全面にページがある） -----
        {
            SampleBatch batch;
            batch.CameraDistance = 30.0;
            const double uv[3][2] = {{1.0, 0.0}, {0.9995, 0.1}, {1.0005, -0.1}};
            for (const auto& entry : uv)
            {
                batch.Receivers.push_back(makeReceiver(entry[0], entry[1], 12.0));
            }
            SampleOutput out;
            if (!runBatch(batch, out))
            {
                std::cerr << TestName << " ケース S6 を実行できませんでした" << std::endl;
                return false;
            }
            bool bMatches = true;
            for (size_t index = 0; index < batch.Receivers.size(); ++index)
            {
                const PointSampleReference expected = referenceOf(batch, index);
                const double tolerance = static_cast<double>(expected.Ambiguous) / 16.0 + 1.0e-4;
                bMatches = bMatches && std::abs(static_cast<double>(out.Visibility[index]) - expected.Visibility) <= tolerance;
            }
            std::cout << TestName << " ケース S6: 面の境 可視度=" << out.Visibility[0] << "," << out.Visibility[1] << "," << out.Visibility[2] << std::endl;
            Expect(out.Visibility[0] == 0.0f && out.Visibility[1] == 0.0f && out.Visibility[2] == 0.0f,
                   "ケース S6: 面の境をまたぐ円盤は、両方の面のページを読んで Q3 の影（0）にならなければならない");
            Expect(bMatches, "ケース S6: 値は参照と一致しなければならない");
            Expect(out.Fallback == 0u, "ケース S6: 段 2 は全面でページがあるので、粗い段へ逃げてはならない");
        }
        return true;
    }

    bool RunPointRasterCases(const DevicePtr& device, ShaderManager& shaderManager, VirtualShadowMapRaster& raster, const Scene& scene, uint64_t& frameSerial)
    {
        // ----- 灯とスライスの表（太陽の段 + 1 灯 × 6 面 × 6 段） -----
        PointShadowSnapshot snapshot;
        snapshot.LightCount = 1u;
        snapshot.Lights[0].LightId = 301u;
        snapshot.Lights[0].Position = Math::Vector3(3.0f, 2.0f, -1.0f);
        snapshot.Lights[0].Range = 20.0f;
        const PointScene pointScene = BuildPointSceneFrom(scene, snapshot);
        const uint32_t sliceCount = pointScene.SliceCount;
        const GPUVsmSlice* slices = pointScene.Slices;
        Expect(pointScene.Lights.LightCount == 1u, "ケース R: 点光源の灯が 1 つ並ばなければならない");
        const auto sliceOf = [&](uint32_t face, uint32_t mip) { return VirtualShadowMapPointSliceIndex(pointScene.Lights, 0u, face, mip); };

        PointRasterReference reference;
        reference.Light[0] = 3.0;
        reference.Light[1] = 2.0;
        reference.Light[2] = -1.0;
        reference.Range = 20.0;
        reference.Near = PointShadowNearPlane;

        // 面 0 の基底（接線 2 本と軸）。面の座標の行はスライスの行列のまま
        const GPUVsmSlice& face0 = slices[sliceOf(0u, 0u)];
        const double sc[3] = {face0.axisX[0], face0.axisX[1], face0.axisX[2]};
        const double tc[3] = {face0.axisY[0], face0.axisY[1], face0.axisY[2]};
        const double major[3] = {face0.axisZ[0], face0.axisZ[1], face0.axisZ[2]};
        Expect(face0.info[2] == 20.0f && face0.info[3] == PointShadowNearPlane && face0.extra[2] == VirtualShadowMapSliceProjectionPerspective,
               "ケース R: 面のスライスは Range・近い平面・透視の印を持たなければならない");
        // 面 0 の +接線方向（sc）の隣の面（軸が sc と同じ向きの面）
        uint32_t neighborFace = 0u;
        for (uint32_t face = 1; face < PointShadowFaceCount; ++face)
        {
            const GPUVsmSlice& candidate = slices[sliceOf(face, 0u)];
            if (candidate.axisZ[0] == face0.axisX[0] && candidate.axisZ[1] == face0.axisX[1] && candidate.axisZ[2] == face0.axisX[2])
            {
                neighborFace = face;
            }
        }
        Expect(neighborFace != 0u, "ケース R: 面 0 の接線方向の隣の面が見つからない");

        // 光源 + 軸の距離 × 軸 + a × 接線 sc + b × 接線 tc
        const auto point = [&](double axial, double a, double b, double (&out)[3]) {
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                out[axis] = reference.Light[axis] + axial * major[axis] + a * sc[axis] + b * tc[axis];
            }
        };
        const auto addQuad = [&](const char* name, double (&c0)[3], double (&c1)[3], double (&c2)[3], double (&c3)[3]) {
            PointPolygon polygon;
            polygon.Count = 4u;
            polygon.Name = name;
            std::memcpy(polygon.P[0], c0, sizeof(c0));
            std::memcpy(polygon.P[1], c1, sizeof(c1));
            std::memcpy(polygon.P[2], c2, sizeof(c2));
            std::memcpy(polygon.P[3], c3, sizeof(c3));
            reference.Polygons.push_back(polygon);
        };
        double c0[3], c1[3], c2[3], c3[3];
        // Q1: 面 0 の軸に垂直な四角形（軸の距離 8。面の NDC は 接線 a/8 ∈ [-0.5, 0.31]、b/8 ∈ [-0.25, 0.5]）
        point(8.0, -4.0, -2.0, c0);
        point(8.0, 2.5, -2.0, c1);
        point(8.0, 2.5, 4.0, c2);
        point(8.0, -4.0, 4.0, c3);
        addQuad("Q1", c0, c1, c2, c3);
        // Q2: 傾いた四角形（軸の距離が 6 から 15 まで変わる平行四辺形。透視の補間が要る）。原点 O と辺 E1 = 8 軸 + 4 sc、E2 = 軸 + 4 tc
        {
            double origin[3];
            point(6.0, -2.0, -2.0, origin);
            double e1[3], e2[3];
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                e1[axis] = 8.0 * major[axis] + 4.0 * sc[axis];
                e2[axis] = 1.0 * major[axis] + 4.0 * tc[axis];
            }
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                c0[axis] = origin[axis];
                c1[axis] = origin[axis] + e1[axis];
                c2[axis] = origin[axis] + e1[axis] + e2[axis];
                c3[axis] = origin[axis] + e2[axis];
            }
            addQuad("Q2", c0, c1, c2, c3);
        }
        // Q3: 面 0 と隣の面（sc 方向）の境をまたぐ四角形。軸の距離 8 の平面の a ∈ [3, 14]（a = 8 が面の境）
        point(8.0, 3.0, -2.0, c0);
        point(8.0, 14.0, -2.0, c1);
        point(8.0, 14.0, 3.0, c2);
        point(8.0, 3.0, 3.0, c3);
        addQuad("Q3", c0, c1, c2, c3);
        // T: 光源の後ろ（軸の距離 -5）の頂点を 2 つ持つ三角形。近い平面（軸の距離 0.05）をまたぐ
        {
            PointPolygon triangle;
            triangle.Count = 3u;
            triangle.Name = "T";
            point(6.0, 1.0, 0.0, c0);
            point(-5.0, 2.0, 3.0, c1);
            point(-5.0, -2.0, -2.0, c2);
            std::memcpy(triangle.P[0], c0, sizeof(c0));
            std::memcpy(triangle.P[1], c1, sizeof(c1));
            std::memcpy(triangle.P[2], c2, sizeof(c2));
            reference.Polygons.push_back(triangle);
        }
        // S: 面 0 の中心（NDC (0, 0)）に写る小さな四角形（軸の距離 8）。球が面 0 の中心の細い錐台に収まるので、面 0 のスライスだけに印が付く
        const uint32_t smallIndex = static_cast<uint32_t>(reference.Polygons.size());
        point(8.0, -0.2, -0.2, c0);
        point(8.0, 0.2, -0.2, c1);
        point(8.0, 0.2, 0.2, c2);
        point(8.0, -0.2, 0.2, c3);
        addQuad("S", c0, c1, c2, c3);
        // F: Range の外の四角形（軸の距離 40）。印が空で記録にならない
        point(40.0, -1.0, -1.0, c0);
        point(40.0, 1.0, -1.0, c1);
        point(40.0, 1.0, 1.0, c2);
        point(40.0, -1.0, 1.0, c3);
        addQuad("F", c0, c1, c2, c3);

        // ----- 塊（組ごとの印）。太陽の段のスライスは空にして、点光源の面の印だけを見る -----
        GPUVsmSlice pointOnlySlices[VirtualShadowMapMaxSlices];
        std::memcpy(pointOnlySlices, slices, sizeof(GPUVsmSlice) * sliceCount);
        for (uint32_t level = 0; level < VirtualShadowMap::LEVEL_COUNT; ++level)
        {
            std::memset(&pointOnlySlices[level], 0, sizeof(GPUVsmSlice));
        }
        ChunkGeometry geometry = BuildPointPolygonChunks(reference.Polygons);
        const Container::VariableArray<VsmShadowChunk> originalChunks = geometry.Chunks;
        Container::VariableArray<VsmShadowChunk> groupedChunks;
        Container::VariableArray<uint32_t> sourceOfGrouped;
        VirtualShadowMap::CasterStats casterStats;
        uint32_t culledPolygons = 0;
        for (uint32_t source = 0; source < static_cast<uint32_t>(originalChunks.size()); ++source)
        {
            VirtualShadowMap::CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = originalChunks[source].BoundsMin[axis];
                bounds.Max[axis] = originalChunks[source].BoundsMax[axis];
            }
            const VirtualShadowMap::SliceMasks masks = VirtualShadowMap::SliceMasksForBounds(pointOnlySlices, sliceCount, bounds);
            if (!masks.IsAny())
            {
                ++culledPolygons;
                Expect(std::strcmp(reference.Polygons[source].Name, "F") == 0, "ケース R: Range の内側の多角形は塊の印が空であってはならない");
                continue;
            }
            const uint32_t before = static_cast<uint32_t>(groupedChunks.size());
            VirtualShadowMap::PushChunkPerGroup(originalChunks[source], masks, groupedChunks, casterStats);
            for (uint32_t index = before; index < static_cast<uint32_t>(groupedChunks.size()); ++index)
            {
                sourceOfGrouped.push_back(source);
            }
        }
        Expect(culledPolygons == 1u && casterStats.DroppedChunks == 0u && !groupedChunks.empty(),
               "ケース R: Range の外の四角形だけが塊の印が空で、ほかは組ごとに記録にならなければならない");
        geometry.Chunks = groupedChunks;

        // ----- ホストが書くページ: 面 0 の段 0 の 16 × 12 ページ（x 10..25、y 12..23）と、全 6 面の段 2 の 8 × 8 ページ -----
        Container::VariableArray<PointRasterPageSpec> pageSpecs;
        for (uint32_t pageY = 12; pageY < 24; ++pageY)
        {
            for (uint32_t pageX = 10; pageX < 26; ++pageX)
            {
                PointRasterPageSpec spec;
                spec.Slice = sliceOf(0u, 0u);
                spec.PageX = pageX;
                spec.PageY = pageY;
                pageSpecs.push_back(spec);
            }
        }
        for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
        {
            for (uint32_t pageY = 0; pageY < 8; ++pageY)
            {
                for (uint32_t pageX = 0; pageX < 8; ++pageX)
                {
                    PointRasterPageSpec spec;
                    spec.Slice = sliceOf(face, 2u);
                    spec.PageX = pageX;
                    spec.PageY = pageY;
                    pageSpecs.push_back(spec);
                }
            }
        }

        Resources resources;
        RasterBuffers rasterBuffers;
        const uint32_t poolPages = static_cast<uint32_t>(pageSpecs.size());
        if (!CreateResources(device, poolPages, resources, sliceCount) || !CreateRasterBuffers(device, geometry, 8192u, rasterBuffers))
        {
            std::cerr << TestName << " ケース R の資源を作れませんでした" << std::endl;
            return false;
        }

        RasterReadback readback;
        if (!RunPointRasterFrame(device, raster, scene.Clipmap, resources, rasterBuffers, sliceCount, slices, pageSpecs, frameSerial++, readback))
        {
            std::cerr << TestName << " ケース R を実行できませんでした" << std::endl;
            return false;
        }
        Expect(readback.bRasterRecorded && readback.DrawCount == groupedChunks.size(), "ケース R: 展開 → 描画を、組ごとの塊の数だけ記録できなければならない");

        // ----- 物理ページと期待値の比較 -----
        constexpr double DepthTolerance = 3.0e-4;
        uint32_t compared = 0;
        uint32_t covered = 0;
        uint32_t mismatches = 0;
        uint32_t ambiguous = 0;
        uint32_t coveredFace0Mip0 = 0;
        uint32_t coveredFace0Mip2 = 0;
        uint32_t coveredNeighborMip2 = 0;
        uint32_t seamCoveredFace0 = 0;
        uint32_t seamCoveredNeighbor = 0;
        uint32_t belowNear = 0;
        double maxError = 0.0;
        for (uint32_t physical = 0; physical < static_cast<uint32_t>(pageSpecs.size()); ++physical)
        {
            const PointRasterPageSpec& spec = pageSpecs[physical];
            const GPUVsmSlice& slice = slices[spec.Slice];
            const bool bMip0 = spec.Slice == sliceOf(0u, 0u);
            for (uint32_t texelY = 0; texelY < VirtualShadowMap::PAGE_RESOLUTION; ++texelY)
            {
                for (uint32_t texelX = 0; texelX < VirtualShadowMap::PAGE_RESOLUTION; ++texelX)
                {
                    const uint32_t bits = readback.Pool[static_cast<size_t>(physical) * VirtualShadowMap::PAGE_WORDS + texelY * VirtualShadowMap::PAGE_RESOLUTION + texelX];
                    float gpuDepth = 0.0f;
                    std::memcpy(&gpuDepth, &bits, sizeof(gpuDepth));
                    double depth = 0.0;
                    double ndcX = 0.0;
                    double ndcY = 0.0;
                    const PointTexelKind kind = ClassifyPointTexel(reference, slice, spec.PageX, spec.PageY, texelX, texelY, depth, ndcX, ndcY);
                    if (kind == PointTexelKind::Ambiguous)
                    {
                        ++ambiguous;
                        continue;
                    }
                    ++compared;
                    if (bits != VirtualShadowMap::EMPTY_DEPTH_BITS && !(gpuDepth >= static_cast<float>(PointShadowNearPlane / reference.Range) - 1.0e-6f))
                    {
                        ++belowNear;
                    }
                    if (kind == PointTexelKind::Empty)
                    {
                        if (bits != VirtualShadowMap::EMPTY_DEPTH_BITS)
                        {
                            ++mismatches;
                        }
                        continue;
                    }
                    ++covered;
                    const double error = std::abs(static_cast<double>(gpuDepth) - depth);
                    maxError = std::max(maxError, error);
                    if (!(error <= DepthTolerance))
                    {
                        ++mismatches;
                    }
                    const bool bFace0 = spec.Slice == sliceOf(0u, 0u) || spec.Slice == sliceOf(0u, 2u);
                    coveredFace0Mip0 += (bMip0) ? 1u : 0u;
                    coveredFace0Mip2 += spec.Slice == sliceOf(0u, 2u) ? 1u : 0u;
                    coveredNeighborMip2 += spec.Slice == sliceOf(neighborFace, 2u) ? 1u : 0u;
                    // 面の境（NDC が ±1 の端）から 2 texel 以内の、覆われた texel
                    const double edgeNdc = 2.0 * 2.0 * slice.info[1];
                    if (bFace0 && ndcX > 1.0 - edgeNdc)
                    {
                        ++seamCoveredFace0;
                    }
                    if (spec.Slice == sliceOf(neighborFace, 2u) && (std::abs(ndcX) > 1.0 - edgeNdc || std::abs(ndcY) > 1.0 - edgeNdc))
                    {
                        ++seamCoveredNeighbor;
                    }
                }
            }
        }
        std::cout << TestName << " ケース R: 灯 1・Range 20・ページ=" << pageSpecs.size() << " 塊=" << originalChunks.size() << "→" << groupedChunks.size()
                  << "（組ごと）比べた texel=" << compared << " 覆われた=" << covered << " 曖昧=" << ambiguous << " 不一致=" << mismatches
                  << " 最大誤差=" << maxError << " 面0の段0=" << coveredFace0Mip0 << " 面0の段2=" << coveredFace0Mip2 << " 隣の面(" << neighborFace
                  << ")の段2=" << coveredNeighborMip2 << " 境際=" << seamCoveredFace0 << "/" << seamCoveredNeighbor << std::endl;
        Expect(mismatches == 0u, "ケース R: 物理ページが、軸の距離 ÷ Range の期待値（多角形の外は 1.0）と一致しなければならない");
        Expect(coveredFace0Mip0 > 20000u && coveredFace0Mip2 > 3000u,
               "ケース R: 四角形が面 0 の段 0 と段 2 に描かれなければならない（面の軸に垂直・傾いた四角形）");
        Expect(coveredNeighborMip2 > 1000u && seamCoveredFace0 > 50u && seamCoveredNeighbor > 50u,
               "ケース R: 面の境をまたぐ四角形が、面 0 と隣の面の両方の境際まで切れ目なく描かれなければならない");
        Expect(belowNear == 0u, "ケース R: 光源の後ろ（近い平面より手前）の部分が深度として書かれてはならない");

        // ----- 展開の統計・インスタンス -----
        uint32_t drawnInstances = 0;
        for (uint32_t chunk = 0; chunk < static_cast<uint32_t>(groupedChunks.size()); ++chunk)
        {
            drawnInstances += readback.Draws[VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS + chunk * VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS + 1u];
        }
        Expect(readback.Stats[VirtualShadowMap::StatRasterInstances] == drawnInstances && drawnInstances > 0u &&
                   readback.Stats[VirtualShadowMap::StatRasterOverflow] == 0u,
               "ケース R: 書いたインスタンスの数が間接描画の数の合計と一致し、溢れてはならない");
        uint32_t badInstances = 0;
        uint32_t smallFaceMismatch = 0;
        uint32_t smallInstances = 0;
        for (uint32_t index = 0; index < drawnInstances && index < readback.Instances.size() / 4u; ++index)
        {
            const uint32_t chunkIndex = readback.Instances[index * 4u];
            const uint32_t slice = readback.Instances[index * 4u + 1u] & 255u;
            if (chunkIndex >= groupedChunks.size())
            {
                ++badInstances;
                continue;
            }
            const VsmShadowChunk& chunk = groupedChunks[chunkIndex];
            // インスタンスのスライスは、その塊の印の中にあり、倍精度の判定でも写るスライスでなければならない
            const bool bInMask = slice >= chunk.Reserved * 32u && slice < chunk.Reserved * 32u + 32u &&
                                 ((chunk.LevelMask >> (slice - chunk.Reserved * 32u)) & 1u) != 0u;
            VirtualShadowMap::CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = chunk.BoundsMin[axis];
                bounds.Max[axis] = chunk.BoundsMax[axis];
            }
            badInstances += (!bInMask || !VirtualShadowMap::PerspectiveSliceTouchesBounds(slices[slice], bounds)) ? 1u : 0u;
            if (std::strcmp(reference.Polygons[sourceOfGrouped[chunkIndex]].Name, "S") == 0)
            {
                ++smallInstances;
                // 小さな四角形は面 0 だけ（スライスは 面 × 段数 + 段）
                const uint32_t faceOfSlice = (slice - pointScene.Lights.FirstSlice) / pointScene.Lights.Settings.MipCount;
                smallFaceMismatch += faceOfSlice != 0u ? 1u : 0u;
            }
        }
        Expect(badInstances == 0u, "ケース R: インスタンスのスライスが、塊の印と倍精度の判定の両方で写るスライスでなければならない");
        Expect(smallInstances > 0u && smallFaceMismatch == 0u, "ケース R: 面 0 の中心の小さな四角形は、面 0 のページにだけインスタンスができなければならない");

        // ----- 太陽のクリップマップを無効にして渡した場合（点光源だけのフレーム）も同じ結果 -----
        VirtualShadowMapClipmap noSun = scene.Clipmap;
        noSun.bEnabled = false;
        RasterReadback sunless;
        if (!RunPointRasterFrame(device, raster, noSun, resources, rasterBuffers, sliceCount, slices, pageSpecs, frameSerial++, sunless))
        {
            std::cerr << TestName << " ケース R（太陽なし）を実行できませんでした" << std::endl;
            return false;
        }
        Expect(sunless.bRasterRecorded && sunless.Pool == readback.Pool &&
                   sunless.Stats[VirtualShadowMap::StatRasterInstances] == readback.Stats[VirtualShadowMap::StatRasterInstances],
               "ケース R: 太陽のクリップマップが無効でも、スライスの表を渡せば同じ物理ページと同じインスタンスの数でなければならない");

        // ----- ケース S: 同じ物理ページを、照明と同じ関数で受け手の点から読む -----
        return RunPointSampleCases(device, shaderManager, reference, pointScene, pageSpecs, readback, resources);
    }

    // ========================================
    // ケース T: 点光源の面のページの持ち越し（灯の移動・投影物の移動による無効化。太陽なし）
    // ========================================
    //
    // 2 灯（ケース P と同じ場面。灯 0 は Range 30、灯 1 は Range 6）の面のページを、深度から印を付けて割り当て、ワールドの多角形
    // （各灯の各面の軸に垂直な背景の四角形 12 枚と、灯 0 のある面のページの内側に置いた小さな動く四角形）を展開 → 描画する。
    // どのフレームも、同じ場面をキャッシュを使わない別の記録（別の VirtualShadowMapPages・別の資源）で描き直した物理ページと、全 texel で比べる。
    //   T1: 最初のフレームは全ページを描き、持ち越しは 0。
    //   T2（止まった灯と投影物）: 2 フレーム目は点光源のページが 1 枚も描かれず（描いたページ 0・無効にしたページ 0）、持ち越しが要求の数で、
    //       展開が描くインスタンスも 0、物理プールの中身が不変。
    //   T3（投影物が動く）: 動く四角形を横へ動かすと、前後の境界を覆う球が面の NDC で覆うページだけが描き直される。箱を面へ写した範囲のページは必ず
    //       dirty、dirty のページは球の範囲（倍精度の参照）の内側、描いた枚数は全体より少なく、動く四角形の中身が変わる。
    //   T4（灯が動く）: 灯 0 を動かすと、灯 0 のスライスの割り当て済みのページがすべて描き直され（無効にしたスライスは 36）、灯 1 のページは描き直されない。
    //   T5（灯の並びが変わる）: 灯 0 と灯 1 を入れ替える（識別子が同じでも番号が変わる）と、両方の灯のページがすべて描き直される。
    //   T6（灯が無くなる）: 灯 1 が無くなると、灯 1 のスライスのページは空きへ戻り、灯 0 のページは描き直されない。

    struct PointCachePage
    {
        uint32_t Slice = 0;
        uint32_t Light = 0;
        uint32_t PageX = 0;
        uint32_t PageY = 0;
        uint32_t Physical = 0;
        bool bDirty = false;
    };

    struct PointCacheFrame
    {
        RasterReadback Readback;
        Container::VariableArray<PointCachePage> Pages;
        bool bContinued = false;
        uint32_t InvalidatedSlices = 0;
        uint32_t SphereCount = 0;

        uint32_t Stat(VirtualShadowMap::StatWord word) const { return Readback.Stats[word]; }
        uint32_t DirtyCount() const
        {
            uint32_t count = 0;
            for (const PointCachePage& page : Pages)
            {
                count += page.bDirty ? 1u : 0u;
            }
            return count;
        }
        uint32_t PagesOf(uint32_t light) const
        {
            uint32_t count = 0;
            for (const PointCachePage& page : Pages)
            {
                count += page.Light == light ? 1u : 0u;
            }
            return count;
        }
        uint32_t DirtyOf(uint32_t light) const
        {
            uint32_t count = 0;
            for (const PointCachePage& page : Pages)
            {
                count += (page.Light == light && page.bDirty) ? 1u : 0u;
            }
            return count;
        }
    };

    // ページの表の割り当て済みの欄から、点光源のスライスのページを取り出す
    Container::VariableArray<PointCachePage> DecodePointPages(const PointScene& pointScene, const Container::VariableArray<uint32_t>& pageTable)
    {
        Container::VariableArray<PointCachePage> pages;
        const VirtualShadowMapPointLights& lights = pointScene.Lights;
        for (uint32_t index = 0; index < pageTable.size(); ++index)
        {
            const uint32_t entry = pageTable[index];
            if ((entry & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) == 0u)
            {
                continue;
            }
            const uint32_t slice = index / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            if (slice < lights.FirstSlice)
            {
                continue;
            }
            const uint32_t address = index % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const uint32_t dimension = static_cast<uint32_t>(pointScene.Slices[slice].origin[3]);
            PointCachePage page;
            page.Slice = slice;
            page.Light = (slice - lights.FirstSlice) / lights.SlicesPerLight;
            page.PageX = address % dimension;
            page.PageY = address / dimension;
            page.Physical = entry & VirtualShadowMap::PAGE_INDEX_MASK;
            page.bDirty = (entry & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u;
            pages.push_back(page);
        }
        return pages;
    }

    // 灯 light の面 face の基底（光源・軸・接線 2 本）。面の座標の行はスライスの行列のまま
    struct PointFaceBasis
    {
        double Light[3] = {};
        double Axis[3] = {};
        double Tangent[3] = {};
        double Bitangent[3] = {};
    };

    PointFaceBasis MakePointFaceBasis(const PointScene& pointScene, uint32_t light, uint32_t face)
    {
        const GPUVsmSlice& slice = pointScene.Slices[VirtualShadowMapPointSliceIndex(pointScene.Lights, light, face, 0u)];
        PointFaceBasis basis;
        basis.Light[0] = pointScene.Lights.Position[light].x;
        basis.Light[1] = pointScene.Lights.Position[light].y;
        basis.Light[2] = pointScene.Lights.Position[light].z;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            basis.Axis[axis] = slice.axisZ[axis];
            basis.Tangent[axis] = slice.axisX[axis];
            basis.Bitangent[axis] = slice.axisY[axis];
        }
        return basis;
    }

    // 面の軸の距離 axial の平面の、接線方向 [a0, a1] × [b0, b1] の四角形（光源からの距離。接線 a, b は軸の距離 1 あたりでなく長さ）
    PointPolygon MakePointQuad(const PointFaceBasis& basis, double axial, double a0, double a1, double b0, double b1, const char* name)
    {
        PointPolygon polygon;
        polygon.Count = 4u;
        polygon.Name = name;
        const double corners[4][2] = {{a0, b0}, {a1, b0}, {a1, b1}, {a0, b1}};
        for (uint32_t corner = 0; corner < 4u; ++corner)
        {
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                polygon.P[corner][axis] = basis.Light[axis] + axial * basis.Axis[axis] + corners[corner][0] * basis.Tangent[axis] +
                                          corners[corner][1] * basis.Bitangent[axis];
            }
        }
        return polygon;
    }

    void PolygonBounds(const PointPolygon& polygon, float (&outMin)[3], float (&outMax)[3])
    {
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            outMin[axis] = 1.0e30f;
            outMax[axis] = -1.0e30f;
            for (uint32_t corner = 0; corner < polygon.Count; ++corner)
            {
                outMin[axis] = std::min(outMin[axis], static_cast<float>(polygon.P[corner][axis]));
                outMax[axis] = std::max(outMax[axis], static_cast<float>(polygon.P[corner][axis]));
            }
        }
    }

    // 多角形の塊（組ごとの印は本番の関数）→ 点光源のページの印付け・割り当て・消去 → 展開 → 描画を 1 フレーム分走らせる。
    // tracker が null ならキャッシュを使わない。null でなければ、多角形の動きから無効化の球を作ってページの記録へ渡す
    bool RunPointCacheFrame(const DevicePtr& device,
                            VirtualShadowMapPages& pages,
                            VirtualShadowMapRaster& raster,
                            const Scene& scene,
                            const PointScene& pointScene,
                            const Container::VariableArray<PointPolygon>& polygons,
                            const Resources& resources,
                            const TexturePtr& depth,
                            VirtualShadowMap::CasterMotionTracker* tracker,
                            uint64_t frameSerial,
                            PointCacheFrame& out)
    {
        // 太陽の段のスライスは空にして、点光源の面の印だけを見る
        GPUVsmSlice pointOnlySlices[VirtualShadowMapMaxSlices];
        std::memcpy(pointOnlySlices, pointScene.Slices, sizeof(GPUVsmSlice) * pointScene.SliceCount);
        for (uint32_t level = 0; level < VirtualShadowMap::LEVEL_COUNT; ++level)
        {
            std::memset(&pointOnlySlices[level], 0, sizeof(GPUVsmSlice));
        }
        ChunkGeometry geometry = BuildPointPolygonChunks(polygons);
        const Container::VariableArray<VsmShadowChunk> originalChunks = geometry.Chunks;
        Container::VariableArray<VsmShadowChunk> groupedChunks;
        VirtualShadowMap::CasterStats casterStats;
        for (const VsmShadowChunk& source : originalChunks)
        {
            VirtualShadowMap::CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = source.BoundsMin[axis];
                bounds.Max[axis] = source.BoundsMax[axis];
            }
            const VirtualShadowMap::SliceMasks masks = VirtualShadowMap::SliceMasksForBounds(pointOnlySlices, pointScene.SliceCount, bounds);
            if (masks.IsAny())
            {
                VirtualShadowMap::PushChunkPerGroup(source, masks, groupedChunks, casterStats);
            }
        }
        geometry.Chunks = groupedChunks;
        RasterBuffers rasterBuffers;
        if (groupedChunks.empty() || !CreateRasterBuffers(device, geometry, 65536u, rasterBuffers))
        {
            return false;
        }

        // 投影物の動き（多角形ごとに 1 件。署名 = 頂点の座標、境界 = 多角形の箱）から、無効にする球を作る
        Container::VariableArray<float> spheres;
        bool bInvalidateAll = false;
        if (tracker != nullptr)
        {
            Container::VariableArray<VirtualShadowMap::CasterMotionEntry> entries;
            for (uint32_t index = 0; index < polygons.size(); ++index)
            {
                float vertices[12] = {};
                for (uint32_t corner = 0; corner < polygons[index].Count; ++corner)
                {
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        vertices[corner * 3u + axis] = static_cast<float>(polygons[index].P[corner][axis]);
                    }
                }
                VirtualShadowMap::CasterMotionEntry entry;
                entry.Key = static_cast<uint64_t>(index) + 1u;
                entry.Signature = VirtualShadowMap::CasterHashFloats(1469598103934665603ull, vertices, 12u);
                entry.bHasBounds = true;
                float boundsMin[3];
                float boundsMax[3];
                PolygonBounds(polygons[index], boundsMin, boundsMax);
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    entry.Bounds.Min[axis] = boundsMin[axis];
                    entry.Bounds.Max[axis] = boundsMax[axis];
                }
                entries.push_back(entry);
            }
            Container::VariableArray<VirtualShadowMap::CasterBounds> changed;
            tracker->Update(entries, changed, bInvalidateAll);
            if (!bInvalidateAll && !VirtualShadowMap::BuildInvalidationSpheres(changed, VirtualShadowMap::MAX_INVALIDATION_RECTS, spheres))
            {
                bInvalidateAll = true;
                spheres.clear();
            }
            out.SphereCount = static_cast<uint32_t>(spheres.size() / 4u);
        }

        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            return false;
        }
        const BufferPtr buffers[] = {resources.Pool,      resources.PageTable, resources.RequestBits, resources.FreeList,
                                     resources.Stats,     resources.DirtyList, rasterBuffers.Chunks,   rasterBuffers.Instances,
                                     rasterBuffers.Draws};
        pages.BeginFrame(0, frameSerial);
        raster.BeginFrame(0, frameSerial);
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
        // 太陽なし（夜）: クリップマップを渡さず、点光源だけで印付けをする
        pagesDispatch.Clipmap = nullptr;
        pagesDispatch.PointLights = &pointScene.Lights;
        pagesDispatch.SliceCount = pointScene.SliceCount;
        std::memcpy(pagesDispatch.InverseViewProjection, scene.InverseViewProjection, sizeof(pagesDispatch.InverseViewProjection));
        std::memcpy(pagesDispatch.CameraPosition, scene.CameraPosition, sizeof(pagesDispatch.CameraPosition));
        SetCameraForward(pagesDispatch, scene);
        pagesDispatch.FovYDegrees = scene.Camera.FieldOfView;
        pagesDispatch.bCacheEnabled = tracker != nullptr;
        pagesDispatch.InvalidationSpheres = spheres.empty() ? nullptr : spheres.data();
        pagesDispatch.InvalidationSphereCount = static_cast<uint32_t>(spheres.size() / 4u);
        pagesDispatch.bInvalidateAll = bInvalidateAll;
        out.Readback.bPagesRecorded = pages.Record(commandList.get(), pagesDispatch);

        VirtualShadowMapClipmap noSun = scene.Clipmap;
        noSun.bEnabled = false;
        VirtualShadowMapRasterDispatch rasterDispatch;
        rasterDispatch.Clipmap = &noSun;
        rasterDispatch.SliceCount = pointScene.SliceCount;
        rasterDispatch.Slices = pointScene.Slices;
        rasterDispatch.PoolPages = resources.PoolPages;
        rasterDispatch.Pool = resources.Pool;
        rasterDispatch.PageTable = resources.PageTable;
        rasterDispatch.Stats = resources.Stats;
        rasterDispatch.Chunks = rasterBuffers.Chunks;
        rasterDispatch.ChunkCount = rasterBuffers.ChunkCount;
        rasterDispatch.Instances = rasterBuffers.Instances;
        rasterDispatch.Draws = rasterBuffers.Draws;
        out.Readback.bRasterRecorded = raster.Record(commandList.get(), rasterDispatch);
        out.Readback.DrawCount = raster.GetLastDrawCount();
        for (const BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        if (!ReadAll(resources.Pool, out.Readback.Pool) || !ReadAll(resources.PageTable, out.Readback.PageTable) ||
            !ReadAll(resources.FreeList, out.Readback.FreeList) || !ReadAll(resources.Stats, out.Readback.Stats) ||
            !ReadAll(rasterBuffers.Draws, out.Readback.Draws) || !ReadAll(rasterBuffers.Instances, out.Readback.Instances))
        {
            return false;
        }
        out.Pages = DecodePointPages(pointScene, out.Readback.PageTable);
        out.bContinued = pages.WasCacheContinued();
        out.InvalidatedSlices = pages.GetPointInvalidatedSliceCount();
        return out.Readback.bPagesRecorded && out.Readback.bRasterRecorded;
    }

    const PointCachePage* FindPointPage(const Container::VariableArray<PointCachePage>& pages, uint32_t slice, uint32_t pageX, uint32_t pageY)
    {
        for (const PointCachePage& page : pages)
        {
            if (page.Slice == slice && page.PageX == pageX && page.PageY == pageY)
            {
                return &page;
            }
        }
        return nullptr;
    }

    // 2 つの結果が、同じ（スライス・ページ）の集合を持ち、全 texel の語が一致する（物理ページの番号は違ってよい）。違った語の数を返す（集合が違えば最大）
    uint32_t CountPointPoolDifferences(const PointCacheFrame& cached, const PointCacheFrame& fresh)
    {
        if (cached.Pages.size() != fresh.Pages.size())
        {
            return 0xFFFFFFFFu;
        }
        uint32_t different = 0;
        for (const PointCachePage& page : cached.Pages)
        {
            const PointCachePage* counterpart = FindPointPage(fresh.Pages, page.Slice, page.PageX, page.PageY);
            if (counterpart == nullptr)
            {
                return 0xFFFFFFFFu;
            }
            const size_t baseCached = static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS;
            const size_t baseFresh = static_cast<size_t>(counterpart->Physical) * VirtualShadowMap::PAGE_WORDS;
            for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
            {
                different += cached.Readback.Pool[baseCached + word] != fresh.Readback.Pool[baseFresh + word] ? 1u : 0u;
            }
        }
        return different;
    }

    // 同じ場面を、キャッシュを使わない別の記録（別の VirtualShadowMapPages・別の資源）で描き直し、キャッシュを使った結果と全 texel で比べる。違った語の数を返す
    uint32_t CompareWithFreshPointFrame(const DevicePtr& device,
                                        VirtualShadowMapPages& freshPages,
                                        VirtualShadowMapRaster& raster,
                                        const Scene& scene,
                                        const PointScene& pointScene,
                                        const Container::VariableArray<PointPolygon>& polygons,
                                        uint32_t poolPages,
                                        const TexturePtr& depth,
                                        const PointCacheFrame& cached,
                                        uint64_t frameSerial)
    {
        Resources resources;
        PointCacheFrame fresh;
        if (!CreateResources(device, poolPages, resources, pointScene.SliceCount) ||
            !RunPointCacheFrame(device, freshPages, raster, scene, pointScene, polygons, resources, depth, nullptr, frameSerial, fresh))
        {
            return 0xFFFFFFFFu;
        }
        return CountPointPoolDifferences(cached, fresh);
    }

    // 点光源のスライスの面で、球が覆うページの範囲（面の全体を [0, 一辺のページ数) としたページの座標。VsmPerspectivePageRange と同じ手順を倍精度で行う）
    struct PointPageRange
    {
        bool bValid = false;
        int32_t MinX = 0;
        int32_t MinY = 0;
        int32_t MaxX = -1;
        int32_t MaxY = -1;

        bool Contains(uint32_t pageX, uint32_t pageY) const
        {
            return bValid && static_cast<int32_t>(pageX) >= MinX && static_cast<int32_t>(pageX) <= MaxX && static_cast<int32_t>(pageY) >= MinY &&
                   static_cast<int32_t>(pageY) <= MaxY;
        }
    };

    void SliceCoordinates(const GPUVsmSlice& slice, const double (&point)[3], double (&out)[3])
    {
        const float* rows[3] = {slice.axisX, slice.axisY, slice.axisZ};
        for (uint32_t row = 0; row < 3u; ++row)
        {
            out[row] = static_cast<double>(rows[row][0]) * point[0] + static_cast<double>(rows[row][1]) * point[1] +
                       static_cast<double>(rows[row][2]) * point[2] + static_cast<double>(rows[row][3]);
        }
    }

    PointPageRange ReferenceSphereRange(const GPUVsmSlice& slice, const double (&center)[3], double radius, double ndcMargin)
    {
        PointPageRange range;
        const double rangeMeters = slice.info[2];
        const double nearPlane = slice.info[3];
        const int32_t pages = slice.origin[3];
        double c[3];
        SliceCoordinates(slice, center, c);
        if (!(rangeMeters > nearPlane) || c[0] * c[0] + c[1] * c[1] + c[2] * c[2] > (rangeMeters + radius) * (rangeMeters + radius) ||
            c[2] + radius < nearPlane || c[2] - radius > rangeMeters)
        {
            return range;
        }
        const double side = radius * 1.4142135623730951;
        if (c[0] - c[2] > side || -c[0] - c[2] > side || c[1] - c[2] > side || -c[1] - c[2] > side)
        {
            return range;
        }
        range.bValid = true;
        if (c[2] - radius <= nearPlane)
        {
            range.MinX = 0;
            range.MinY = 0;
            range.MaxX = pages - 1;
            range.MaxY = pages - 1;
            return range;
        }
        const double denominator = c[2] * c[2] - radius * radius;
        int32_t low[2];
        int32_t high[2];
        for (uint32_t axis = 0; axis < 2u; ++axis)
        {
            const double spread = radius * std::sqrt(std::max(c[axis] * c[axis] + denominator, 0.0));
            const double ndcLow = (c[axis] * c[2] - spread) / denominator - ndcMargin;
            const double ndcHigh = (c[axis] * c[2] + spread) / denominator + ndcMargin;
            low[axis] = std::max(static_cast<int32_t>(std::floor(ndcLow * 0.5 * pages + 0.5 * pages)), 0);
            high[axis] = std::min(static_cast<int32_t>(std::floor(ndcHigh * 0.5 * pages + 0.5 * pages)), pages - 1);
        }
        range.MinX = low[0];
        range.MinY = low[1];
        range.MaxX = high[0];
        range.MaxY = high[1];
        return range;
    }

    // 箱の 8 隅を面の NDC へ写した範囲。箱の全体が近い平面の手前・Range の内側にあるときだけ有効（箱の中のどの点もこの範囲のページに写る）
    PointPageRange ReferenceBoxRange(const GPUVsmSlice& slice, const float (&boxMin)[3], const float (&boxMax)[3])
    {
        PointPageRange range;
        const double rangeMeters = slice.info[2];
        const double nearPlane = slice.info[3];
        const int32_t pages = slice.origin[3];
        double ndcMin[2] = {1.0e30, 1.0e30};
        double ndcMax[2] = {-1.0e30, -1.0e30};
        for (uint32_t corner = 0; corner < 8u; ++corner)
        {
            const double point[3] = {static_cast<double>((corner & 1u) ? boxMax[0] : boxMin[0]), static_cast<double>((corner & 2u) ? boxMax[1] : boxMin[1]),
                                     static_cast<double>((corner & 4u) ? boxMax[2] : boxMin[2])};
            double c[3];
            SliceCoordinates(slice, point, c);
            if (!(c[2] > nearPlane + 0.05) || std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) > rangeMeters)
            {
                return range;
            }
            for (uint32_t axis = 0; axis < 2u; ++axis)
            {
                ndcMin[axis] = std::min(ndcMin[axis], c[axis] / c[2]);
                ndcMax[axis] = std::max(ndcMax[axis], c[axis] / c[2]);
            }
        }
        range.bValid = true;
        // 縁の近くのページは含めない（単精度の GPU との差で落ちうる）。箱の NDC を内側へ縮めた範囲のページは必ず含まれる
        const double shrink = 2.0e-3;
        range.MinX = static_cast<int32_t>(std::floor((ndcMin[0] + shrink) * 0.5 * pages + 0.5 * pages));
        range.MinY = static_cast<int32_t>(std::floor((ndcMin[1] + shrink) * 0.5 * pages + 0.5 * pages));
        range.MaxX = static_cast<int32_t>(std::floor((ndcMax[0] - shrink) * 0.5 * pages + 0.5 * pages));
        range.MaxY = static_cast<int32_t>(std::floor((ndcMax[1] - shrink) * 0.5 * pages + 0.5 * pages));
        range.MinX = std::max(range.MinX, 0);
        range.MinY = std::max(range.MinY, 0);
        range.MaxX = std::min(range.MaxX, pages - 1);
        range.MaxY = std::min(range.MaxY, pages - 1);
        return range;
    }

    bool RunPointCacheCases(const DevicePtr& device,
                            ShaderManager& shaderManager,
                            VirtualShadowMapRaster& raster,
                            const Scene& scene,
                            const Container::VariableArray<float>& sunImage,
                            uint64_t& frameSerial)
    {
        VirtualShadowMapPages cachedPages;
        VirtualShadowMapPages freshPages;
        if (!cachedPages.Initialize(device.get(), &shaderManager) || !freshPages.Initialize(device.get(), &shaderManager))
        {
            std::cerr << TestName << " ケース T: ページのパイプラインを初期化できませんでした" << std::endl;
            return false;
        }

        // ----- 灯の並びと、印付けに使う深度 -----
        const PointScene sceneA = BuildPointScene(scene);
        Container::VariableArray<float> image = sunImage;
        RemoveAmbiguousPointPixels(scene, sceneA, image);
        const TexturePtr depth = CreateDepthTexture(device, image);
        if (!depth)
        {
            std::cerr << TestName << " ケース T: 深度のテクスチャを作れませんでした" << std::endl;
            return false;
        }
        const auto buildLights = [&](const Math::Vector3& position0, bool bSwap, uint32_t lightCount) {
            PointShadowSnapshot snapshot;
            snapshot.LightCount = lightCount;
            snapshot.Lights[0].LightId = 101u;
            snapshot.Lights[0].Position = position0;
            snapshot.Lights[0].Range = 30.0f;
            snapshot.Lights[1].LightId = 102u;
            snapshot.Lights[1].Position = Math::Vector3(-6.0f, 1.0f, -2.0f);
            snapshot.Lights[1].Range = 6.0f;
            if (bSwap)
            {
                std::swap(snapshot.Lights[0], snapshot.Lights[1]);
            }
            return BuildPointSceneFrom(scene, snapshot);
        };
        const Math::Vector3 position0(1.5f, 2.0f, -12.0f);
        const Math::Vector3 movedPosition0(2.0f, 2.0f, -12.0f);
        const PointScene sceneMoved = buildLights(movedPosition0, false, 2u);
        const PointScene sceneSwapped = buildLights(position0, true, 2u);
        const PointScene sceneSingle = buildLights(position0, false, 1u);

        // 要求のあるページの参照（プールの大きさと、動く四角形の位置の選択に使う）
        const auto collectKeys = [&](const PointScene& pointScene) {
            Container::VariableArray<uint32_t> keys;
            for (uint32_t pixelY = 0; pixelY < ImageHeight; ++pixelY)
            {
                for (uint32_t pixelX = 0; pixelX < ImageWidth; ++pixelX)
                {
                    ClassifyPointPixel(scene, pointScene, pixelX, pixelY, image[pixelY * ImageWidth + pixelX], &keys, nullptr, nullptr);
                }
            }
            SortUnique(keys);
            return keys;
        };
        const Container::VariableArray<uint32_t> keysA = collectKeys(sceneA);
        const Container::VariableArray<uint32_t> keysMoved = collectKeys(sceneMoved);
        Expect(keysA.size() >= 20u && keysMoved.size() >= 20u, "ケース T: 点光源のページに要求がなければならない（場面が退化している）");
        const uint32_t poolPages = static_cast<uint32_t>(std::max(keysA.size(), keysMoved.size()) * 5u / 4u) + 64u;
        std::cout << TestName << " ケース T: 要求の参照=" << keysA.size() << "（灯を動かすと " << keysMoved.size() << "）プール=" << poolPages << std::endl;

        // ----- 多角形: 各灯の各面の背景の四角形と、灯 0 の要求のあるページの内側に置いた動く四角形 -----
        Container::VariableArray<PointPolygon> polygons;
        for (uint32_t light = 0; light < sceneA.Lights.LightCount; ++light)
        {
            const double depthMeters = 0.5 * sceneA.Lights.Range[light];
            for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
            {
                const double half = 1.3 * depthMeters;
                polygons.push_back(MakePointQuad(MakePointFaceBasis(sceneA, light, face), depthMeters, -half, half, -half, half, "Backdrop"));
            }
        }
        const uint32_t backdropCount = static_cast<uint32_t>(polygons.size());
        // 灯 0 の面のページのうち、要求のある最も細かい段（ページの数が最も多い段）の最初のページ
        uint32_t moverFace = 0;
        uint32_t moverPages = 0;
        double moverNdc[2] = {};
        bool bFound = false;
        for (const uint32_t key : keysA)
        {
            const uint32_t slice = key / VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            const uint32_t local = slice - sceneA.Lights.FirstSlice;
            const uint32_t pagesPerAxis = static_cast<uint32_t>(sceneA.Slices[slice].origin[3]);
            if (local / sceneA.Lights.SlicesPerLight != 0u || pagesPerAxis <= moverPages)
            {
                continue;
            }
            moverFace = (local % sceneA.Lights.SlicesPerLight) / sceneA.Lights.Settings.MipCount;
            moverPages = pagesPerAxis;
            const uint32_t address = key % VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            moverNdc[0] = ((address % moverPages) + 0.5) / moverPages * 2.0 - 1.0;
            moverNdc[1] = ((address / moverPages) + 0.5) / moverPages * 2.0 - 1.0;
            bFound = true;
        }
        Expect(bFound, "ケース T: 灯 0 の要求のあるページが見つからない（場面が退化している）");
        if (!bFound)
        {
            return false;
        }
        const double moverDepth = 0.25 * sceneA.Lights.Range[0];
        const double pageMeters = 2.0 / moverPages * moverDepth;
        const PointFaceBasis moverBasis = MakePointFaceBasis(sceneA, 0u, moverFace);
        const double moverCenter[2] = {moverNdc[0] * moverDepth, moverNdc[1] * moverDepth};
        const auto makeMover = [&](double shiftPages) {
            const double half = 0.15 * pageMeters;
            const double shift = shiftPages * pageMeters;
            return MakePointQuad(moverBasis, moverDepth, moverCenter[0] + shift - half, moverCenter[0] + shift + half, moverCenter[1] - half,
                                 moverCenter[1] + half, "Mover");
        };
        polygons.push_back(makeMover(0.0));
        const uint32_t moverIndex = backdropCount;

        Resources resources;
        VirtualShadowMap::CasterMotionTracker tracker;
        if (!CreateResources(device, poolPages, resources, sceneA.SliceCount))
        {
            std::cerr << TestName << " ケース T: 資源を作れませんでした" << std::endl;
            return false;
        }

        // ----- T1: 最初のフレームは全ページを描く -----
        PointCacheFrame frame1;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneA, polygons, resources, depth, &tracker, frameSerial++, frame1))
        {
            std::cerr << TestName << " ケース T1 を実行できませんでした" << std::endl;
            return false;
        }
        const uint32_t pointPages = static_cast<uint32_t>(frame1.Pages.size());
        Expect(!frame1.bContinued, "ケース T1: 最初のフレームは前フレームの表を引き継がない");
        Expect(frame1.Stat(VirtualShadowMap::StatOverflow) == 0u && frame1.Stat(VirtualShadowMap::StatRasterOverflow) == 0u,
               "ケース T1: ページの溢れも展開の溢れも出てはならない");
        Expect(pointPages >= 20u && frame1.PagesOf(0u) > 0u && frame1.PagesOf(1u) > 0u, "ケース T1: 2 灯どちらにも割り当て済みのページがなければならない");
        Expect(frame1.Stat(VirtualShadowMap::StatPointRequested) == pointPages && frame1.DirtyCount() == pointPages &&
                   frame1.Stat(VirtualShadowMap::StatPointRendered) == pointPages && frame1.Stat(VirtualShadowMap::StatPointCached) == 0u,
               "ケース T1: 最初のフレームは点光源のページをすべて描き、持ち越しは 0 でなければならない");
        {
            uint32_t covered = 0;
            for (const PointCachePage& page : frame1.Pages)
            {
                for (uint32_t word = 0; word < VirtualShadowMap::PAGE_WORDS; ++word)
                {
                    covered += frame1.Readback.Pool[static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS + word] != VirtualShadowMap::EMPTY_DEPTH_BITS ? 1u : 0u;
                }
            }
            Expect(covered > 20000u, "ケース T1: 背景の四角形が点光源のページに描かれていなければならない（場面が退化している）");
        }
        std::cout << TestName << " ケース T1: 点光源のページ=" << pointPages << "（灯 0 が " << frame1.PagesOf(0u) << "、灯 1 が " << frame1.PagesOf(1u)
                  << "）描いた=" << frame1.Stat(VirtualShadowMap::StatPointRendered) << std::endl;

        // ----- T2: 止まった灯と投影物の 2 フレーム目は、点光源のページが描かれない -----
        PointCacheFrame frame2;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneA, polygons, resources, depth, &tracker, frameSerial++, frame2))
        {
            std::cerr << TestName << " ケース T2 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame2.bContinued && frame2.InvalidatedSlices == 0u && frame2.SphereCount == 0u,
               "ケース T2: 止まった場面は前フレームの表を引き継ぎ、スライスも球も無効にしない");
        Expect(frame2.Stat(VirtualShadowMap::StatPointRendered) == 0u && frame2.Stat(VirtualShadowMap::StatPointCached) == pointPages &&
                   frame2.Stat(VirtualShadowMap::StatPointInvalidated) == 0u && frame2.Stat(VirtualShadowMap::StatPointReleased) == 0u &&
                   frame2.Stat(VirtualShadowMap::StatRendered) == 0u,
               "ケース T2: 止まった場面の 2 フレーム目に描かれる点光源のページが 0、持ち越しが要求の数でなければならない");
        Expect(frame2.Stat(VirtualShadowMap::StatRasterInstances) == 0u && frame2.DirtyCount() == 0u, "ケース T2: dirty のページが無いので、描くインスタンスが 0 でなければならない");
        Expect(frame2.Readback.Pool == frame1.Readback.Pool, "ケース T2: 持ち越したページの物理プールの中身が変わってはならない");
        for (const PointCachePage& page : frame1.Pages)
        {
            const PointCachePage* same = FindPointPage(frame2.Pages, page.Slice, page.PageX, page.PageY);
            Expect(same != nullptr && same->Physical == page.Physical, "ケース T2: 持ち越したページが同じ物理ページを保たなければならない");
        }

        // ----- T3: 投影物が動くと、前後の境界を覆う球が面の NDC で覆うページだけが描き直される -----
        const float shiftPages = 0.9f;
        Container::VariableArray<PointPolygon> movedPolygons = polygons;
        movedPolygons[moverIndex] = makeMover(shiftPages);
        PointCacheFrame frame3;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneA, movedPolygons, resources, depth, &tracker, frameSerial++, frame3))
        {
            std::cerr << TestName << " ケース T3 を実行できませんでした" << std::endl;
            return false;
        }
        const uint32_t dirty3 = frame3.DirtyCount();
        Expect(frame3.bContinued && frame3.InvalidatedSlices == 0u && frame3.SphereCount == 2u,
               "ケース T3: 動いた四角形の前後の境界 2 つが球になり、スライスは無効にならない");
        Expect(frame3.Pages.size() == pointPages && frame3.Stat(VirtualShadowMap::StatPointRendered) == dirty3 &&
                   frame3.Stat(VirtualShadowMap::StatPointInvalidated) == dirty3 && frame3.Stat(VirtualShadowMap::StatPointCached) == pointPages - dirty3,
               "ケース T3: 描いた・無効にした・持ち越したページの数が、ページの表の dirty と一致しなければならない");
        Expect(dirty3 > 0u && dirty3 * 2u < pointPages, "ケース T3: 動いた範囲の一部のページだけが描き直されなければならない");
        {
            float oldMin[3];
            float oldMax[3];
            float newMin[3];
            float newMax[3];
            PolygonBounds(polygons[moverIndex], oldMin, oldMax);
            PolygonBounds(movedPolygons[moverIndex], newMin, newMax);
            const float* boxes[2][2] = {{oldMin, oldMax}, {newMin, newMax}};
            uint32_t tightRequired = 0;
            uint32_t missing = 0;
            uint32_t outside = 0;
            for (const PointCachePage& page : frame3.Pages)
            {
                const GPUVsmSlice& slice = sceneA.Slices[page.Slice];
                bool bTight = false;
                bool bLoose = false;
                for (const auto& box : boxes)
                {
                    float boxMin[3] = {box[0][0], box[0][1], box[0][2]};
                    float boxMax[3] = {box[1][0], box[1][1], box[1][2]};
                    bTight = bTight || ReferenceBoxRange(slice, boxMin, boxMax).Contains(page.PageX, page.PageY);
                    const double center[3] = {0.5 * (static_cast<double>(boxMin[0]) + boxMax[0]), 0.5 * (static_cast<double>(boxMin[1]) + boxMax[1]),
                                              0.5 * (static_cast<double>(boxMin[2]) + boxMax[2])};
                    const double radius = 0.5 * std::sqrt((static_cast<double>(boxMax[0]) - boxMin[0]) * (static_cast<double>(boxMax[0]) - boxMin[0]) +
                                                          (static_cast<double>(boxMax[1]) - boxMin[1]) * (static_cast<double>(boxMax[1]) - boxMin[1]) +
                                                          (static_cast<double>(boxMax[2]) - boxMin[2]) * (static_cast<double>(boxMax[2]) - boxMin[2]));
                    bLoose = bLoose || ReferenceSphereRange(slice, center, radius + 0.01, 2.0e-3).Contains(page.PageX, page.PageY);
                }
                tightRequired += bTight ? 1u : 0u;
                missing += (bTight && !page.bDirty) ? 1u : 0u;
                outside += (page.bDirty && !bLoose) ? 1u : 0u;
            }
            Expect(tightRequired > 0u, "ケース T3: 動いた四角形を面へ写した範囲に、割り当て済みのページがなければならない（場面が退化している）");
            Expect(missing == 0u, "ケース T3: 動いた四角形の箱を面へ写した範囲の割り当て済みのページは、すべて描き直されなければならない");
            Expect(outside == 0u, "ケース T3: 描き直されたページは、前後の境界を覆う球が面の NDC で覆う範囲の内側でなければならない");
            std::cout << TestName << " ケース T3: 動かしたページ幅=" << shiftPages << " 描き直したページ=" << dirty3 << "/" << pointPages << "（箱の範囲 " << tightRequired
                      << "、球の範囲の外 " << outside << "、取りこぼし " << missing << "）球=" << frame3.SphereCount << std::endl;
        }
        {
            // 動いた四角形の中身が変わる（描き直さないと古い影が残る）
            uint32_t changed = 0;
            for (const PointCachePage& page : frame3.Pages)
            {
                const PointCachePage* before = FindPointPage(frame2.Pages, page.Slice, page.PageX, page.PageY);
                for (uint32_t word = 0; before != nullptr && word < VirtualShadowMap::PAGE_WORDS; ++word)
                {
                    changed += frame3.Readback.Pool[static_cast<size_t>(page.Physical) * VirtualShadowMap::PAGE_WORDS + word] !=
                                       frame2.Readback.Pool[static_cast<size_t>(before->Physical) * VirtualShadowMap::PAGE_WORDS + word]
                                   ? 1u
                                   : 0u;
                }
            }
            Expect(changed > 100u, "ケース T3: 動いた四角形のぶん、描き直したページの texel が変わらなければならない");
        }
        {
            const uint32_t different = CompareWithFreshPointFrame(device, freshPages, raster, scene, sceneA, movedPolygons, poolPages, depth, frame3, frameSerial++);
            Expect(different == 0u, "ケース T3: 物理ページが毎フレーム描き直したときと全 texel で一致しなければならない");
            std::cout << TestName << " ケース T3: 毎フレーム描き直した結果との texel の違い=" << different << std::endl;
        }

        // ----- T4: 灯 0 が動くと、灯 0 のスライスの全ページが描き直される -----
        PointCacheFrame frame4;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneMoved, movedPolygons, resources, depth, &tracker, frameSerial++, frame4))
        {
            std::cerr << TestName << " ケース T4 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame4.bContinued && frame4.InvalidatedSlices == sceneMoved.Lights.SlicesPerLight && frame4.SphereCount == 0u,
               "ケース T4: 動いた灯 0 のスライスだけ（6 面 × 6 段 = 36）が無効になる");
        Expect(frame4.PagesOf(0u) > 0u && frame4.DirtyOf(0u) == frame4.PagesOf(0u),
               "ケース T4: 動いた灯 0 の割り当て済みのページは、すべて描き直されなければならない");
        Expect(frame4.PagesOf(1u) > 0u && frame4.DirtyOf(1u) == 0u, "ケース T4: 動かない灯 1 のページは描き直されてはならない");
        Expect(frame4.Stat(VirtualShadowMap::StatPointRendered) == frame4.DirtyCount() && frame4.Stat(VirtualShadowMap::StatOverflow) == 0u,
               "ケース T4: 描いた点光源のページの数が dirty と一致し、溢れが出てはならない");
        {
            const uint32_t different = CompareWithFreshPointFrame(device, freshPages, raster, scene, sceneMoved, movedPolygons, poolPages, depth, frame4, frameSerial++);
            Expect(different == 0u, "ケース T4: 灯を動かした後の物理ページが毎フレーム描き直したときと全 texel で一致しなければならない");
            std::cout << TestName << " ケース T4: 灯 0 のページ=" << frame4.PagesOf(0u) << " 描き直した=" << frame4.DirtyOf(0u) << " 灯 1 のページ=" << frame4.PagesOf(1u)
                      << " 描き直した=" << frame4.DirtyOf(1u) << " 無効にしたスライス=" << frame4.InvalidatedSlices << " texel の違い=" << different << std::endl;
        }

        // ----- T5: 灯の並びが変わると（識別子が同じでも番号が変わる）、両方の灯のページが描き直される -----
        PointCacheFrame frame5;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneSwapped, movedPolygons, resources, depth, &tracker, frameSerial++, frame5))
        {
            std::cerr << TestName << " ケース T5 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame5.bContinued && frame5.InvalidatedSlices == 2u * sceneSwapped.Lights.SlicesPerLight,
               "ケース T5: 入れ替わった 2 灯のスライスがすべて無効になる");
        Expect(frame5.Pages.size() > 0u && frame5.DirtyCount() == frame5.Pages.size(), "ケース T5: 入れ替わった灯のページは、すべて描き直されなければならない");
        {
            const uint32_t different = CompareWithFreshPointFrame(device, freshPages, raster, scene, sceneSwapped, movedPolygons, poolPages, depth, frame5, frameSerial++);
            Expect(different == 0u, "ケース T5: 灯を入れ替えた後の物理ページが毎フレーム描き直したときと全 texel で一致しなければならない");
            std::cout << TestName << " ケース T5: ページ=" << frame5.Pages.size() << " 描き直した=" << frame5.DirtyCount() << " texel の違い=" << different << std::endl;
        }

        // ----- T6: 灯が無くなると、その灯のスライスのページは空きへ戻る -----
        // （T5 で入れ替えた並びから、灯 0 だけの並びへ戻す。スロット 0 の灯が変わるので、灯 0 も描き直されるのは T5 と同じ。灯 1 のスロットは空きへ戻る）
        PointCacheFrame frame6a;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneA, movedPolygons, resources, depth, &tracker, frameSerial++, frame6a))
        {
            std::cerr << TestName << " ケース T6（元の並びへ戻す）を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame6a.Pages.size() > 0u && frame6a.DirtyCount() == frame6a.Pages.size(), "ケース T6: 並びを戻した 1 フレームは、両方の灯のページが描き直される");
        PointCacheFrame frame6b;
        if (!RunPointCacheFrame(device, cachedPages, raster, scene, sceneSingle, movedPolygons, resources, depth, &tracker, frameSerial++, frame6b))
        {
            std::cerr << TestName << " ケース T6 を実行できませんでした" << std::endl;
            return false;
        }
        Expect(frame6b.bContinued && frame6b.InvalidatedSlices == sceneSingle.Lights.SlicesPerLight,
               "ケース T6: 無くなった灯 1 のスライス（36）だけが無効になる");
        Expect(frame6b.PagesOf(0u) == frame6a.PagesOf(0u) && frame6b.PagesOf(1u) == 0u && frame6b.DirtyCount() == 0u,
               "ケース T6: 無くなった灯 1 のページは空きへ戻り、残った灯 0 のページは描き直されない");
        Expect(frame6b.Stat(VirtualShadowMap::StatPointReleased) == frame6a.PagesOf(1u) && frame6b.Stat(VirtualShadowMap::StatPointRendered) == 0u,
               "ケース T6: 空きへ戻した点光源のページの数が、無くなった灯 1 のページの数と一致しなければならない");
        {
            const uint32_t different = CompareWithFreshPointFrame(device, freshPages, raster, scene, sceneSingle, movedPolygons, poolPages, depth, frame6b, frameSerial++);
            Expect(different == 0u, "ケース T6: 灯が減った後の物理ページが毎フレーム描き直したときと全 texel で一致しなければならない");
            std::cout << TestName << " ケース T6: 残ったページ=" << frame6b.Pages.size() << " 空きへ戻した=" << frame6b.Stat(VirtualShadowMap::StatPointReleased)
                      << " texel の違い=" << different << std::endl;
        }
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
                // ----- ケース W: スライスが 40 個（太陽の 10 段をスライス 30〜39 に置いた場面を、ケース F と同じ texel で描く） -----
                if (!RunWideSliceCases(device, pages, raster, scene, reference, depth, caseF, frameSerial))
                {
                    return 1;
                }
                // ----- ケース L: 照明が使う VSM の読み出し（ケース F の物理プール・ページの表を、照明と同じ関数で読む） -----
                if (!RunSampleCases(device, shaderManager, pages, raster, scene, image, caseF, frameSerial))
                {
                    return 1;
                }
                // ----- ケース C2: 隣のページへの印の範囲が、ページの何枚分にもなるとき -----
                if (!RunWideMarginMarkingCase(device, pages, frameSerial))
                {
                    return 1;
                }
                // ----- ケース P: 点光源の面のページへの印付けと、太陽と同じプールからの割り当て -----
                if (!RunPointMarkCases(device, pages, scene, image, frameSerial))
                {
                    return 1;
                }
                // ----- ケース R: 点光源の面のスライスへの展開・描画 -----
                if (!RunPointRasterCases(device, shaderManager, raster, scene, frameSerial))
                {
                    return 1;
                }
                // ----- ケース M: ページのキャッシュ（持ち越し・無効化・古い順の解放） -----
                if (!RunCacheCases(device, shaderManager, raster, scene, reference, image, depth, frameSerial))
                {
                    return 1;
                }
                // ----- ケース T: 点光源の面のページの持ち越し（灯の移動・投影物の移動による無効化） -----
                if (!RunPointCacheCases(device, shaderManager, raster, scene, image, frameSerial))
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
