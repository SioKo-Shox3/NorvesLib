#include "Rendering/MegaGeometryPass.h"
#include "Container/UnorderedSet.h"
#include "Debug/DebugConfig.h"
#include "Debug/Stats.h"
#include "RHI/FrameResourceWriteTracker.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/ConstantMaterialTextureCache.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include "Rendering/MegaGeometryMirror.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ScopedGpuTimestamp.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SparseResidencyShading.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VirtualTextureFeedbackMaterial.h"
#include "Rendering/VisibilityMerge.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/VisibilityResolvePass.h"
#include "Text/IdentityPool.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iterator>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // VT の要求のバッファの binding（megageometry.frag の VT_FEEDBACK_BINDING と同じ。材質のテクスチャ 1-6 の次）
        constexpr uint32_t VirtualTextureFeedbackBindingIndex = 7;
        // メッシュの表の binding（Common/MegaInstanceTable.glsl の MEGA_MESH_BINDING。カリングと描画のセットで同じ番号）
        constexpr uint32_t MegaMeshTableBindingIndex = 22;

        // 値を持たない起動引数（flag）があるか。起動引数は ApplicationProcessor の外（描画の層）でも読めるよう、
        // プロセスのコマンドラインを直接見る（VirtualShadowMapPass の --vsm-cache と同じ）
        bool HasProcessCommandLineFlag(const wchar_t *flag)
        {
#if defined(_WIN32)
            const wchar_t *commandLine = GetCommandLineW();
            if (commandLine == nullptr)
            {
                return false;
            }
            const size_t flagLength = std::wcslen(flag);
            for (const wchar_t *found = std::wcsstr(commandLine, flag); found != nullptr;
                 found = std::wcsstr(found + 1, flag))
            {
                // 引数の先頭から末尾までが flag と一致するものだけ（--mega-flat-path-x などを取り違えない）
                const bool bStartsArgument = found == commandLine || found[-1] == L' ' || found[-1] == L'"';
                const wchar_t next = found[flagLength];
                if (bStartsArgument && (next == L'\0' || next == L' ' || next == L'"'))
                {
                    return true;
                }
            }
#else
            (void)flag;
#endif
            return false;
        }
    } // namespace

    using namespace Container;

    namespace
    {
        constexpr uint32_t DEBUG_PAYLOAD_MODE_NONE = 0u;
        constexpr uint32_t DEBUG_PAYLOAD_MODE_CLUSTER_INDEX = 1u;
        constexpr uint32_t DEBUG_PAYLOAD_MODE_LOD_LEVEL = 2u;

        // cluster_cull.comp の cullPass（CULL_PASS_* と同じ値）
        constexpr uint32_t CULL_PASS_SINGLE = 0u; // 従来の1回の判定（遮蔽の判定なし）
        constexpr uint32_t CULL_PASS_FIRST = 1u;  // 1パス目: 前のフレームで見えたクラスタだけ
        constexpr uint32_t CULL_PASS_SECOND = 2u; // 2パス目: HZB で判定して残りを描き、見えたビットを更新

        // 統計（cluster_cull.comp の stats[12]）のバイト数。0〜3 は pass1・pass2_tested・pass2_drawn・occluded、
        // 4〜7 はソフトウェアラスタの振り分け（pass1 の一覧・pass2 の一覧・ハードだけ・一覧が満杯で積めなかった数）、
        // 8 はハードのコマンドを空振りにした数、9・10 は visbuffer_sw_raster.comp が数える（矩形の上限を超えた三角形・走ったワークグループ）、
        // 11 は区間の描画コマンドの上限（MaxDrawCount）を超えて積めなかった可視クラスタの数（両パスの合計）、
        // 12 は BVH のたどりの列の容量を超えて落ちた要素の数（両パスの合計）、13 は 2 パス目の根の入力の容量を超えたか（1 なら超えた）、
        // 14・15 は BVH の節の列の 1 段・葉の列へ積もうとした数の最大（容量を超えた分も含む）
        constexpr uint32_t StatsBufferWords = 16u;
        constexpr uint32_t StatsBufferBytes = StatsBufferWords * sizeof(uint32_t);

        // ソフトウェアラスタの一覧（cluster_cull.comp の SwRasterBuffer）。先頭はパスごとの間接 dispatch の引数（x・y・z）と
        // 積もうとした数の 4 語ずつ（SW_RASTER_HEADER_WORDS = 8 語）で、その後にパスごとのコマンドの位置の列が続く。
        // 一覧の容量はパスごとのコマンド数まで（間接 dispatch の引数は 2 次元なので、x の上限 65535 を超えても y へ折り返す）
        constexpr uint32_t SwRasterHeaderWords = 8u;

        // グループの BVH のたどり（cluster_bvh_cull.comp）。BvhStageClusters は葉のクラスタの判定の段の印、
        // BvhLeafSlots は葉の列の1要素を受け持つスレッドの数（葉が持つクラスタの最大数）、
        // BvhCounterCount はカウンタのバッファの語数（添え字 s は段 s の入力の数（段 0 は範囲の段が積んだ範囲の数）、BvhLeafCounter は葉の列の数。
        // BvhArgsBaseWords からは段 s と葉の段の間接 dispatch の引数が BvhArgsStrideWords 語ずつ並ぶ。
        // Common/MegaGeometryCull.glsl の BVH_LEAF_COUNTER・BVH_ARGS_BASE・BVH_ARGS_STRIDE と一致）
        constexpr uint32_t BvhStageClusters = 0xFFFFFFFFu;
        constexpr uint32_t BvhLeafSlots = MegaGeometry::GROUP_BVH_MAX_LEAF_CLUSTERS;
        constexpr uint32_t BvhLeafCounter = 16;
        constexpr uint32_t BvhArgsBaseWords = 32;
        constexpr uint32_t BvhArgsStrideWords = 4;
        constexpr uint32_t BvhCounterCount = 128;
        constexpr uint32_t BvhCounterBytes = BvhCounterCount * sizeof(uint32_t);
        static_assert(BvhArgsBaseWords + (BvhLeafCounter + 1u) * BvhArgsStrideWords <= BvhCounterCount,
                      "BVH の間接 dispatch の引数がカウンタのバッファに収まりません");
        static_assert(MegaGeometry::GROUP_BVH_MAX_LEVELS <= BvhLeafCounter,
                      "BVH の段のカウンタが葉の列のカウンタと重なります");

        // BVH のたどりの列の容量（要素。1 要素 = uvec2）。本数やメッシュによらず固定で、節の列 2 本（段ごとに入力と出力を
        // 入れ替える）と葉の列を並べる。超えた要素は積まずに数える（MEGA_CAPACITY の bvh_overflow）。
        // 大きさの目安: mega-stress 5 万個の既定の視点で、節の列の 1 段へ積もうとした数の最大が約 349 万、葉の列が約 115 万
        // （MEGA_CAPACITY の bvh_node_peak・bvh_leaf_peak）
        constexpr uint32_t BvhNodeQueueCapacity = 1u << 22;
        constexpr uint32_t BvhLeafQueueCapacity = 1u << 22;
        constexpr uint64_t BvhQueueEntries = 2ull * BvhNodeQueueCapacity + BvhLeafQueueCapacity;
        static_assert(BvhQueueEntries <= 0x3FFFFFFFull, "BVH の列の位置が uint32 に収まりません");
        static_assert(static_cast<uint64_t>(BvhLeafQueueCapacity) * BvhLeafSlots <= 0xFFFFFFFFull,
                      "葉の段のスレッドの番号が uint32 に収まりません");

        // 2 パス目の根の入力（cluster_bvh_cull.comp の binding 14）。頭の 8 語（2 パス目の段 0 の間接 dispatch の引数・
        // 積もうとした数・容量を超えた数・予約）の後ろに、1 パス目の段 0 が視錐台と LOD を通った根のインスタンスの番号を積む。
        // 各パスの頭で 0 にするカウンタとは別に持ち、1 パス目の前に頭だけを 0 にする
        constexpr uint32_t BvhPass2HeaderWords = 8;
        constexpr uint32_t BvhPass2Capacity = 1u << 20;

        // インスタンスの判定の範囲（cluster_bvh_cull.comp の範囲の段・mega_scene_scatter.comp の段 2）。BVH を持つインスタンスを
        // 描く順に BvhRangeSeats 席ずつ区切り、範囲ごとの AABB（vec4 × 2 = RangeBoundsBytes）を常駐の表に持つ。範囲の段は
        // 通った範囲を節の列 0 へ積み、席の段は 1 範囲 = 1 ワークグループ（64 スレッド）で席を判定する。
        // BvhStageRanges は範囲の段の印、RangeBoundsBinding はカリングのディスクリプタセットの binding
        constexpr uint32_t BvhRangeSeats = 64;
        constexpr uint32_t BvhStageRanges = 0xFFFFFFFEu;
        constexpr uint32_t RangeBoundsBytes = 8u * sizeof(float);
        constexpr uint32_t RangeBoundsBinding = 15;

        // 画素の大きさの判定（席の段）を使える描く範囲か: 描く範囲が描画先の全体（上下の反転は可）のときだけ判定する
        // （画素の中心の位置を、カリングと描画で同じに数えられるときだけ）
        bool IsPixelCullViewport(const RHI::Viewport &viewport, uint32_t width, uint32_t height)
        {
            const float fullWidth = static_cast<float>(width);
            const float fullHeight = static_cast<float>(height);
            const bool bUpright = viewport.y == 0.0f && viewport.height == fullHeight;
            const bool bFlipped = viewport.y == fullHeight && viewport.height == -fullHeight;
            return width > 0 && height > 0 && viewport.x == 0.0f && viewport.width == fullWidth && (bUpright || bFlipped);
        }

        // 手放したバッファ（作り直した「見えた」ビット・IndirectDraw のバッファなど）を、パスが持ち続けるフレーム数。
        // 手放したフレームの記録（古い中身のコピーを含む）が終わった次のフレームで参照を落とす。GPU の完了までの保持は
        // RHI の遅延破棄（参照を落とした後の提出の完了で破棄する）が受け持つ。立ち上がりで列を倍々に伸ばすと古い物が
        // 数 GB になるので、フレーム数で長く持つと VRAM の山が上がる
        constexpr uint64_t RetiredBufferFrames = 0;

        bool IsFiniteSphere(const BoundingSphere &sphere)
        {
            return sphere.Radius > 0.0f && std::isfinite(sphere.Radius) && std::isfinite(sphere.CenterX) &&
                   std::isfinite(sphere.CenterY) && std::isfinite(sphere.CenterZ);
        }

        /**
         * @brief 影の判定に使うインスタンスの境界（ワールドの球）を求める
         *
         * プロキシの WorldBounds と、メッシュ全体のローカルの境界をワールド行列（行ベクトル規約の列優先の並びのまま = GLSL の mat4 と
         * 同じ読み方）で移した球の、両方を含む球。プロキシの WorldBounds は、メッシュの読み込みが済む前に決まった値のまま残ることが
         * あり（既定の小さな球）、それだけを信じると、実際の大きさより小さい境界で影のページの無効化・カリングが抜ける。
         * どちらか片方だけが使えるならそれ。どちらも無ければ false。
         */
        bool ResolveShadowBounds(const float *worldMatrix,
                                 const BoundingSphere &proxyBounds,
                                 const BoundingSphere &meshBounds,
                                 float (&outSphere)[4])
        {
            const bool bHasProxy = IsFiniteSphere(proxyBounds);
            float meshSphere[4] = {};
            bool bHasMesh = false;
            if (IsFiniteSphere(meshBounds))
            {
                const float local[3] = {meshBounds.CenterX, meshBounds.CenterY, meshBounds.CenterZ};
                float scale = 0.0f;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    // GLSL の world[axis].xyz（列）。拡大率はその長さの最大
                    const float *column = worldMatrix + axis * 4;
                    scale = std::max(scale, std::sqrt(column[0] * column[0] + column[1] * column[1] + column[2] * column[2]));
                    meshSphere[axis] = worldMatrix[12 + axis] + local[0] * worldMatrix[0 + axis] + local[1] * worldMatrix[4 + axis] +
                                       local[2] * worldMatrix[8 + axis];
                }
                meshSphere[3] = meshBounds.Radius * scale;
                bHasMesh = std::isfinite(meshSphere[0]) && std::isfinite(meshSphere[1]) && std::isfinite(meshSphere[2]) &&
                           std::isfinite(meshSphere[3]) && meshSphere[3] > 0.0f;
            }
            if (bHasProxy && !bHasMesh)
            {
                outSphere[0] = proxyBounds.CenterX;
                outSphere[1] = proxyBounds.CenterY;
                outSphere[2] = proxyBounds.CenterZ;
                outSphere[3] = proxyBounds.Radius;
                return true;
            }
            if (!bHasMesh)
            {
                return false;
            }
            if (!bHasProxy)
            {
                std::memcpy(outSphere, meshSphere, sizeof(meshSphere));
                return true;
            }
            // 2 つの球を含む最小の球（片方がもう片方を含むならその大きいほう）
            const float proxy[4] = {proxyBounds.CenterX, proxyBounds.CenterY, proxyBounds.CenterZ, proxyBounds.Radius};
            const float delta[3] = {meshSphere[0] - proxy[0], meshSphere[1] - proxy[1], meshSphere[2] - proxy[2]};
            const float distance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            if (distance + meshSphere[3] <= proxy[3])
            {
                std::memcpy(outSphere, proxy, sizeof(proxy));
            }
            else if (distance + proxy[3] <= meshSphere[3])
            {
                std::memcpy(outSphere, meshSphere, sizeof(meshSphere));
            }
            else
            {
                const float radius = 0.5f * (distance + proxy[3] + meshSphere[3]);
                const float along = (radius - proxy[3]) / distance;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    outSphere[axis] = proxy[axis] + delta[axis] * along;
                }
                outSphere[3] = radius;
            }
            return true;
        }

        // 影を落とすインスタンスの鍵（FNV-1a。値が違えば違う、というだけを期待する）。作り直した後の最初の表で、
        // 前に表を作ったときの並びを引くのにだけ使う
        uint64_t MixShadowInstanceKey(uint64_t key, uint64_t componentId)
        {
            constexpr uint64_t Prime = 1099511628211ull;
            uint64_t hash = 14695981039346656037ull;
            for (const uint64_t value : {key, componentId})
            {
                for (uint32_t shift = 0; shift < 64u; shift += 8u)
                {
                    hash = (hash ^ ((value >> shift) & 0xFFull)) * Prime;
                }
            }
            return hash;
        }

        // ページの範囲の常駐の区画の署名（ページを読み込むメッシュの常駐が変わったかを見分ける）
        uint64_t HashPageResidency(const Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> &entries,
                                   uint32_t base, uint32_t count)
        {
            constexpr uint64_t Prime = 1099511628211ull;
            uint64_t hash = 14695981039346656037ull ^ count;
            const uint64_t end = std::min<uint64_t>(static_cast<uint64_t>(base) + count, entries.size());
            for (uint64_t index = base; index < end; ++index)
            {
                hash = (hash ^ entries[index].Region) * Prime;
            }
            return hash;
        }

        // VSM の無効化へ積む箱の上限（超えたら全ページを描き直す。VSM は矩形が 256 を超えると全ページにする）
        constexpr size_t ShadowInvalidationMaxBoxes = 4096;
        // MEGA_SHADOW_DIRTY を出す間隔（表を作った回数）
        constexpr uint32_t ShadowDirtyLogInterval = 120;

        bool IsMegaGeometryDebugPayloadMode(DebugViewMode mode)
        {
            return mode == DebugViewMode::MegaGeometryClusters ||
                   mode == DebugViewMode::LODLevel;
        }

        uint32_t GetMegaGeometryDebugPayloadMode(DebugViewMode mode, bool bSupported)
        {
            if (!bSupported)
            {
                return DEBUG_PAYLOAD_MODE_NONE;
            }

            if (mode == DebugViewMode::MegaGeometryClusters)
            {
                return DEBUG_PAYLOAD_MODE_CLUSTER_INDEX;
            }

            if (mode == DebugViewMode::LODLevel)
            {
                return DEBUG_PAYLOAD_MODE_LOD_LEVEL;
            }

            return DEBUG_PAYLOAD_MODE_NONE;
        }
    } // namespace

    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    MegaGeometryPass::MegaGeometryPass(const MegaGeometryPassSettings &settings)
        : m_Settings(settings)
    {
        // 撮り比べ用に、LODの段を選ぶ誤差の閾値（画素）を環境変数で替えられるようにする。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *value = std::getenv("NORVES_MEGA_LOD_ERROR_PX");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value != nullptr && value[0] != '\0')
        {
            char *end = nullptr;
            const float threshold = std::strtof(value, &end);
            if (end != value && *end == '\0' && std::isfinite(threshold) && threshold > 0.0f)
            {
                m_Settings.LODBias = threshold;
            }
        }

        // グループの BVH のたどりを切って、平らなクラスタの列だけで判定する（撮り比べ用。既定はたどる）
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *bvhValue = std::getenv("NORVES_MEGA_BVH");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (bvhValue != nullptr && (std::strcmp(bvhValue, "0") == 0 || std::strcmp(bvhValue, "off") == 0))
        {
            m_Settings.bUseGroupBVH = false;
        }
        // 既定は全インスタンスを BVH の経路でたどる。--mega-flat-path で平らな経路（cluster_cull.comp・FirstGroup の接頭和）へ戻す
        if (HasProcessCommandLineFlag(L"--mega-flat-path"))
        {
            m_Settings.bUseGroupBVH = false;
        }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *statsEveryFrameValue = std::getenv("NORVES_MEGA_STATS_EVERY_FRAME");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (statsEveryFrameValue != nullptr && std::strcmp(statsEveryFrameValue, "1") == 0)
        {
            m_Settings.bStatsEveryFrame = true;
        }

        // インスタンスの判定の距離（m）を環境変数で替えられるようにする（既定は距離で落とさない）
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *cullDistanceValue = std::getenv("NORVES_MEGA_INSTANCE_CULL_DISTANCE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (cullDistanceValue != nullptr && cullDistanceValue[0] != '\0')
        {
            char *end = nullptr;
            const float distance = std::strtof(cullDistanceValue, &end);
            if (end != cullDistanceValue && *end == '\0' && std::isfinite(distance) && distance > 0.0f)
            {
                m_Settings.InstanceCullDistance = distance;
            }
        }
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *pixelCullValue = std::getenv("NORVES_MEGA_INSTANCE_PIXEL_CULL");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (pixelCullValue != nullptr && (std::strcmp(pixelCullValue, "0") == 0 || std::strcmp(pixelCullValue, "off") == 0))
        {
            m_Settings.bInstancePixelCull = false;
        }

        // 区間のコマンドの最大数を環境変数で替えられるようにする（本数を増やした測定で、描かれないクラスタを出さないため）
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *maxDrawValue = std::getenv("NORVES_MEGA_MAX_DRAW_COUNT");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (maxDrawValue != nullptr && maxDrawValue[0] != '\0')
        {
            char *end = nullptr;
            const unsigned long long maxDraw = std::strtoull(maxDrawValue, &end, 10);
            // コマンドは 2 パス分が 1 本のバッファに並び、バイト位置が 31 ビットに収まる範囲に限る
            if (end != maxDrawValue && *end == '\0' && maxDraw > 0 && maxDraw <= (0x7FFFFFFFull / sizeof(MegaGeometry::DrawIndexedIndirectCommand)) / 2u)
            {
                m_Settings.MaxDrawCount = static_cast<uint32_t>(maxDraw);
            }
        }
    }

    MegaGeometryPass::~MegaGeometryPass()
    {
        if (IsInitialized())
        {
            Shutdown();
        }
    }

    // ========================================
    // Initialize
    // ========================================

    bool MegaGeometryPass::Initialize(ViewRenderContext &context)
    {
        m_Device = context.Device;
        if (!m_Device)
        {
            return false;
        }

        // まとめたカリング・描画は、メッシュのクラスタ配列をデバイスアドレスで引き、コマンドの firstInstance で
        // 描画情報を引く。どちらかが無いデバイスではパスを無効にする
        {
            const auto &initCaps = m_Device->GetCapabilities();
            if (!initCaps.bBufferDeviceAddress || !initCaps.bDrawIndirectFirstInstance)
            {
                NORVES_LOG_ERROR("MegaGeometryPass",
                                 "MEGA_BATCH_UNSUPPORTED buffer_device_address=%d "
                                 "draw_indirect_first_instance=%d "
                                 "まとめたカリング・描画に対応しないため、パスは無効化されます",
                                 initCaps.bBufferDeviceAddress ? 1 : 0,
                                 initCaps.bDrawIndirectFirstInstance ? 1 : 0);
                m_bInitialized = true;
                return true;
            }
        }

        // カリングコンピュートシェーダーの読み込み
        if (context.ShaderMgr)
        {
            m_CullShader = context.ShaderMgr->LoadShader(
                "cluster_cull.comp", RHI::ShaderStage::Compute);
        }
        if (!m_CullShader)
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "カリングシェーダーの読み込みに失敗。パスは無効化されます");
            m_bInitialized = true;
            return true;
        }

        // カリング用GPUリソース作成
        if (!CreateCullResources(m_Device))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリング用GPUリソースの作成に失敗");
            return false;
        }

        // カリングコンピュートパイプライン作成
        RHI::ComputePipelineDesc cullPipelineDesc;
        cullPipelineDesc.computeShader = m_CullShader;
        cullPipelineDesc.descriptorSetLayouts.push_back(BuildCullDescriptorSetDesc());
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // 常駐の表へ差分を散在書き込みするパイプライン。作れなければ表を書けないので、パスを無効にする
        if (context.ShaderMgr)
        {
            m_SceneScatterShader = context.ShaderMgr->LoadShader("mega_scene_scatter.comp", RHI::ShaderStage::Compute);
        }
        if (m_SceneScatterShader)
        {
            RHI::ComputePipelineDesc scatterPipelineDesc;
            scatterPipelineDesc.computeShader = m_SceneScatterShader;
            scatterPipelineDesc.descriptorSetLayouts.push_back(BuildSceneScatterDescriptorSetDesc());
            m_SceneScatterPipeline = m_Device->CreateComputePipeline(scatterPipelineDesc);
        }
        if (!m_SceneScatterPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "常駐の表の散在書き込みのパイプラインを作れません。パスは無効化されます");
            m_CullPipeline.reset();
            m_bInitialized = true;
            return true;
        }

        // グループの BVH をたどるカリング（BVH を持つメッシュ用）。作れなければ BVH は使わず、平らなクラスタの列で判定する
        if (context.ShaderMgr)
        {
            m_BvhCullShader = context.ShaderMgr->LoadShader(
                "cluster_bvh_cull.comp", RHI::ShaderStage::Compute);
        }
        if (m_BvhCullShader)
        {
            RHI::ComputePipelineDesc bvhPipelineDesc;
            bvhPipelineDesc.computeShader = m_BvhCullShader;
            bvhPipelineDesc.descriptorSetLayouts.push_back(BuildCullDescriptorSetDesc());
            m_BvhCullPipeline = m_Device->CreateComputePipeline(bvhPipelineDesc);
        }
        if (!m_BvhCullPipeline)
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "MEGA_BVH_UNAVAILABLE BVH "
                                                   "をたどるカリングを作れないため、平らなクラスタの列で判定します");
        }

        // 遮蔽カリング（2パス）の HZB。作れなければ従来の1回の判定で描く
        m_bHiZReady = m_HiZ.Initialize(m_Device, context.ShaderMgr);

        // GBuffer描画用シェーダーの読み込み（MegaGeometry専用GBuffer互換シェーダー）
        if (context.ShaderMgr)
        {
            m_DrawVertexShader = context.ShaderMgr->LoadShader(
                "megageometry.vert", RHI::ShaderStage::Vertex);
            m_DrawFragmentShader = context.ShaderMgr->LoadShader(
                "megageometry.frag", RHI::ShaderStage::Pixel);
        }
        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer描画シェーダーの読み込みに失敗");
            return false;
        }

        // デフォルトPBRテクスチャの作成
        auto createDefault1x1 = [this](const char *debugName, uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> RHI::TexturePtr
        {
            RHI::TextureDesc texDesc;
            texDesc.Width = 1;
            texDesc.Height = 1;
            texDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            texDesc.Usage = RHI::ResourceUsage::ShaderRead;
            texDesc.DebugName = debugName;

            auto tex = m_Device->CreateTexture(texDesc);
            if (tex)
            {
                uint8_t pixel[4] = {r, g, b, a};
                tex->Update(pixel, 4, 4);
            }
            return tex;
        };

        m_DefaultWhiteTexture = createDefault1x1("MegaGeometry_DefaultWhite", 255, 255, 255, 255);
        m_DefaultFlatNormalTexture = createDefault1x1("MegaGeometry_DefaultFlatNormal", 128, 128, 255, 255);
        m_DefaultBlackTexture = createDefault1x1("MegaGeometry_DefaultBlack", 0, 0, 0, 255);

        if (!m_DefaultWhiteTexture || !m_DefaultFlatNormalTexture || !m_DefaultBlackTexture)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトPBRテクスチャの作成に失敗");
            return false;
        }

        // デフォルトLinearサンプラーの作成
        RHI::SamplerDesc sampDesc;
        sampDesc.filterMin = RHI::FilterMode::Linear;
        sampDesc.filterMag = RHI::FilterMode::Linear;
        sampDesc.filterMip = RHI::FilterMode::Linear;
        sampDesc.addressU = RHI::TextureAddressMode::Wrap;
        sampDesc.addressV = RHI::TextureAddressMode::Wrap;
        sampDesc.addressW = RHI::TextureAddressMode::Wrap;
        m_DefaultLinearSampler = m_Device->CreateSampler(sampDesc);
        if (!m_DefaultLinearSampler)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトサンプラーの作成に失敗");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("MegaGeometryPass", "初期化完了 (MaxDrawCount: %u)", m_Settings.MaxDrawCount);
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void MegaGeometryPass::Shutdown()
    {
        m_CullPipeline.reset();
        m_CullShader.reset();
        m_BvhCullPipeline.reset();
        m_BvhCullShader.reset();
        m_BvhQueueBuffer.reset();
        m_BvhCounterBuffer.reset();
        m_BvhPass2Buffer.reset();
        m_bBvhQueueAllocFailedLogged = false;
        m_LoggedBvhInstances = 0xFFFFFFFFu;
        m_LoggedBvhLevels = 0xFFFFFFFFu;
        m_LoggedFlatInstances = 0xFFFFFFFFu;
        m_IndirectDrawBuffer.reset();
        m_DrawCountBuffer.reset();
        m_DrawInfoBuffer.reset();
        m_SwRasterBuffer.reset();
        m_SwRasterBufferCapacity = 0;
        m_SwRasterCapacity = 0;
        m_bSwRasterBinned = false;
        m_CommandCapacity = 0;
        m_CounterCapacity = 0;
        m_IndirectDrawBufferHandle = {};
        m_DrawCountBufferHandle = {};
        m_MegaGeometryCompleteHandle = {};
        m_FrameSlots.Clear();
        m_bFrameSlotMeshOwnersHeld = false;
        m_PreparedSceneTables = PreparedSceneTables{};
        m_SceneScatterPipeline.reset();
        m_SceneScatterShader.reset();
        m_SlotTableBuffer.reset();
        m_SlotTableCapacity = 0;
        m_InstanceTableBuffer.reset();
        m_ShadowTableBuffer.reset();
        m_InstanceTableCapacity = 0;
        m_MeshTableBuffer.reset();
        m_MeshTableCapacity = 0;
        m_RangeBoundsBuffer.reset();
        m_RangeBoundsCapacity = 0;
        m_RangeBoundsRowCount = 0;
        m_SlotRowCopies.clear();
        m_SlotRowStates.clear();
        m_SlotRowChangedStamps.clear();
        m_SceneUpdateStamp = 0;
        m_EntryCopies.clear();
        m_EntryCopyCount = 0;
        m_ShadowRowCopies.clear();
        m_ShadowRowCopyCount = 0;
        m_MeshCopies.clear();
        m_bMeshCopiesValid = false;
        m_bSceneRowsAllDirty = true;
        m_SceneDirtyPositions.clear();
        m_bSceneSlotsDirty = true;
        m_SceneUploadStats = SceneUploadStats{};
        m_ShadowRecords.clear();
        m_ShadowDirtyMarks.clear();
        m_ShadowDirtyPositions.clear();
        m_bShadowRebuildPending = false;
        m_ShadowPreviousInstances.clear();
        m_ShadowPreviousRecords.clear();
        m_ShadowMeshes.clear();
        m_ShadowPageTableVersion = ~0ull;
        m_ShadowPageEntries.clear();
        m_ShadowInvalidation = MegaGeometryShadowInvalidation{};
        m_ShadowDirtyStats = ShadowDirtyStats{};
        m_SceneTables = SceneTables{};
        m_bSceneTablesValid = false;
        m_bSceneTablesBvhAvailable = false;
        m_SceneInstanceRows.clear();
        m_SceneInstanceHandles.clear();
        m_SceneMeshSources.clear();
        m_SceneSkippedMeshes.clear();
        m_SceneTableDirtyMarks.clear();
        m_SceneTableDirtyPositions.clear();
        m_SceneTablesCheck = SceneTables{};
        m_SceneTablesCheckRecords.clear();
        m_SceneTablesStats = SceneTablesStats{};
        m_RenderFrameCount = 0;
        m_LastRenderFrameSerial = 0;
        m_bUnsetFrameSerialWarned = false;
        m_DummyVisibilityBuffer.reset();
        m_DummyStatsBuffer.reset();
        m_VisibilityBuffer.reset();
        m_VisibilityCapacity = 0;
        m_VisibilityRegions.clear();
        m_VisibilityFreeRegions.clear();
        m_VisibilityAllocatedEnd = 0;
        m_VisibilityBuildStamp = 0;
        m_VisibilitySweepStamp = 0;
        m_RetiredBuffers.clear();
        m_OcclusionFrameCount = 0;
        // まだ渡っていない統計は受け取らない（読み戻し先は GPU の完了の後にフレームの持ち主へ戻る）
        m_StatsReadback.CancelPending();
        m_PendingStats.clear();
        m_bStatsLoggedOnce = false;
        m_bStatsEpochActive = false;
        m_bDropVisibilityContinuity = false;
        m_bOcclusionFallbackLogged = false;
        m_bBatchUnsupportedLogged = false;
        m_HiZ.Shutdown();
        m_bHiZReady = false;

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_SecondGBufferRenderPass.reset();
        m_SecondGBufferFramebuffer.reset();
        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();
        m_VelocityTexture.reset();
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferEmissiveHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferVelocityHandle = {};

        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_ConstantMaterialTextures.reset();
        m_DefaultLinearSampler.reset();

        m_Instances.clear();
        m_InstanceSourceRevision = 0;
        m_InstanceSourceProxies = nullptr;
        m_bPreferRenderGraphGBufferResources = false;
        m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;
        m_bMegaGeometryDebugPayloadUnsupportedWarned = false;

        m_bInitialized = false;
    }

    // ========================================
    // Setup
    // ========================================

    MegaGeometryPass::MegaMeshInstance MegaGeometryPass::MakeMegaMeshInstance(const MegaGeometryProxy &proxy)
    {
        MegaMeshInstance instance;
        instance.ObjectId = proxy.ObjectId;
        instance.ComponentId = proxy.ComponentId;
        instance.Handle = proxy.MegaMeshHandle;
        std::memcpy(instance.WorldMatrix, &proxy.WorldTransform, sizeof(float) * 16);
        std::memcpy(instance.PreviousWorldMatrix, &proxy.PreviousWorldTransform, sizeof(float) * 16);
        instance.bCastShadow = proxy.bCastShadow;
        instance.WorldBounds = proxy.WorldBounds;
        instance.SceneSlot = proxy.SceneSlot;
        return instance;
    }

    bool MegaGeometryPass::PatchInstancesFromChangedSlots(const ViewRenderContext &context)
    {
        const MegaGeometryMirror &mirror = *context.SnapshotMegaMirror;
        const auto &proxies = *context.SnapshotMegaGeometryProxies;

        // m_Instances は proxy の配列と同じ並び・同じ数（無効な proxy が無い）ときだけ、位置で対応する。
        // 先に全部の席を確かめてから書く（途中で諦めても m_Instances を壊さない）
        Container::VariableArray<uint32_t> positions;
        positions.reserve(context.SnapshotMegaChangedSlots->size());
        for (const uint32_t slot : *context.SnapshotMegaChangedSlots)
        {
            uint32_t position = 0;
            if (!mirror.TryGetPositionBySlot(slot, position) || position >= proxies.size() ||
                position >= m_Instances.size())
            {
                return false;
            }
            const MegaGeometryProxy &proxy = proxies[position];
            // ObjectId が替わる位置は、見えたビットの引き継ぎ（鍵の集合）が変わるので作り直す。
            // 行の席が替わる位置も、席の表の番号を決め直すので作り直す
            if (!proxy.IsValid() || m_Instances[position].ObjectId != proxy.ObjectId ||
                m_Instances[position].SceneSlot != proxy.SceneSlot)
            {
                return false;
            }
            positions.push_back(position);
        }
        for (const uint32_t position : positions)
        {
            // 席の表の番号は行の席と一緒に保つ（行の席が同じことは上で確かめた）
            const uint32_t tableSlot = m_Instances[position].TableSlot;
            const MegaMeshInstance before = m_Instances[position];
            m_Instances[position] = MakeMegaMeshInstance(proxies[position]);
            m_Instances[position].TableSlot = tableSlot;
            // VSM のキャッシュ: 影に効く中身が変わった位置だけを、次に表を作るときに旧・新の境界で無効にする
            NoteShadowPatched(position, before, m_Instances[position]);
            // 表の差分更新: 次に表を作るときに、この位置の行を直す
            MarkSceneTableDirty(position);
            // 席の表の行を確かめる位置（全部を確かめる更新が待っていれば要らない）
            if (!m_bSceneRowsAllDirty)
            {
                m_SceneDirtyPositions.push_back(position);
            }
        }

        // 検査 MEGA_SCENE_DELTA: 直した結果が、全部を作り直した結果と同じこと
        if (context.bSnapshotMegaVerify)
        {
            bool bSame = m_Instances.size() == proxies.size();
            for (size_t index = 0; bSame && index < proxies.size(); ++index)
            {
                const MegaMeshInstance expected = MakeMegaMeshInstance(proxies[index]);
                const MegaMeshInstance &actual = m_Instances[index];
                bSame = actual.ObjectId == expected.ObjectId && actual.ComponentId == expected.ComponentId &&
                        actual.Handle.Id == expected.Handle.Id && actual.bCastShadow == expected.bCastShadow &&
                        std::memcmp(actual.WorldMatrix, expected.WorldMatrix, sizeof(expected.WorldMatrix)) == 0 &&
                        std::memcmp(actual.PreviousWorldMatrix, expected.PreviousWorldMatrix,
                                    sizeof(expected.PreviousWorldMatrix)) == 0 &&
                        std::memcmp(&actual.WorldBounds, &expected.WorldBounds, sizeof(expected.WorldBounds)) == 0;
            }
            if (!bSame)
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "変わった席だけ直したインスタンスが、作り直した結果と一致しません");
            }
            mirror.NoteConsumerCheck(bSame);
        }
        return true;
    }

    void MegaGeometryPass::Setup(ViewRenderContext &context)
    {
        // VSM の投影物のカリングの入力は、今フレームの記録が書けたときだけ有効にする（前のフレームのものを残さない）
        m_ShadowCasterInputs = MegaGeometryShadowCasterInputs{};

        // 差分の同期では、RenderThread の写しが前回から変わっていない（版が同じ）フレームは、インスタンスを作り直さない。
        // 前に見た版から変わった席を写しが数えているフレーム（描画を見送った差分の席も含む）は、変わった席のインスタンスだけを直す。
        // 写しの版は、基準・差分を当てたときだけ進む。手で足したインスタンス（AddMegaMeshInstance）は版を 0 に戻す
        const bool bFromMirror = context.SnapshotMegaChangedSlots != nullptr && context.SnapshotMegaRevision != 0 &&
                                 context.SnapshotMegaGeometryProxies != nullptr;
        const bool bSameSource = bFromMirror && context.SnapshotMegaGeometryProxies == m_InstanceSourceProxies;
        const bool bReuseInstances = bSameSource && context.SnapshotMegaRevision == m_InstanceSourceRevision;
        bool bPatchedInstances = false;
        if (!bReuseInstances && bSameSource && context.SnapshotMegaMirror != nullptr &&
            context.SnapshotMegaMirror->CanPatchFrom(m_InstanceSourceRevision) &&
            m_Instances.size() == context.SnapshotMegaGeometryProxies->size())
        {
            bPatchedInstances = PatchInstancesFromChangedSlots(context);
            if (bPatchedInstances)
            {
                m_InstanceSourceRevision = context.SnapshotMegaRevision;
            }
        }
        if (!bReuseInstances && !bPatchedInstances)
        {
            // 作り直したインスタンスは、席の表の番号を決め直し、全部の席の行を確かめる
            m_bSceneRowsAllDirty = true;
            m_bSceneSlotsDirty = true;
            m_SceneDirtyPositions.clear();
            m_bSceneTablesValid = false;
            // VSM のキャッシュ: 並びが変わるので、次に表を作るときに前の並びと鍵で引いて比べる
            BeginShadowRebuild(false);
            m_Instances.clear();
            m_InstanceSourceRevision = bFromMirror ? context.SnapshotMegaRevision : 0;
            m_InstanceSourceProxies = bFromMirror ? static_cast<const void *>(context.SnapshotMegaGeometryProxies) : nullptr;

            if (context.SnapshotMegaGeometryProxies)
            {
                const auto &megaGeometryProxies = *context.SnapshotMegaGeometryProxies;
                m_Instances.reserve(megaGeometryProxies.size());

                for (const auto &proxy : megaGeometryProxies)
                {
                    if (!proxy.IsValid())
                    {
                        continue;
                    }

                    m_Instances.push_back(MakeMegaMeshInstance(proxy));
                }
            }

            // スナップショットから外れたインスタンスの見えたビットは、全インスタンスが消えて記録を省くフレームでも
            // 引き継げないものにする（記録の中だけで連続性を見ると、空のフレームを挟んだ再追加が引き継ぎに見える）。
            // LastUsedFrame を0にすると、次の記録の連続の条件（LastUsedFrame + 1 == 記録のフレーム数）を満たさない。
            // 鍵は 1 回だけ集めて引く（区画ごとに全インスタンスを探すと、区画数×インスタンス数の二重ループになる）。
            // インスタンスの集合が変わらないフレームは、前回の結果と同じなので省く
            Container::UnorderedSet<uint64_t> presentKeys;
            presentKeys.reserve(m_Instances.size());
            for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
            {
                const uint64_t objectId = m_Instances[instanceIndex].ObjectId;
                presentKeys.insert(objectId != 0 ? objectId : (0x8000000000000000ull | instanceIndex));
            }

            for (VisibilityRegion &region : m_VisibilityRegions)
            {
                if (region.ClusterCount != 0 && presentKeys.find(region.Key) == presentKeys.end())
                {
                    region.LastUsedFrame = 0;
                }
            }

            // インスタンスが無くなったフレームは表を作らないので、前に載せた投影物の境界をここで無効化へ積む
            FlushShadowRebuildWithoutInstances();
        }

        const bool bNeedsDrawPipeline = !m_Instances.empty() && m_CullPipeline;
        const bool bNeedsAttachmentTransitionPass = m_bPreferRenderGraphGBufferResources;
        if (!bNeedsDrawPipeline && !bNeedsAttachmentTransitionPass)
        {
            return;
        }

        if (!m_bPreferRenderGraphGBufferResources)
        {
            m_AlbedoTexture.reset();
            m_NormalTexture.reset();
            m_MaterialTexture.reset();
            m_EmissiveTexture.reset();
            m_DepthTexture.reset();
            m_VelocityTexture.reset();
        }

        // Legacy bridge fallback: named resource が不足している場合だけSharedResourcesから補完する。
        if (context.SharedResources)
        {
            if (!m_AlbedoTexture)
            {
                m_AlbedoTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Albedo"));
            }
            if (!m_NormalTexture)
            {
                m_NormalTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Normal"));
            }
            if (!m_MaterialTexture)
            {
                m_MaterialTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Material"));
            }
            if (!m_EmissiveTexture)
            {
                m_EmissiveTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Emissive"));
            }
            if (!m_DepthTexture)
            {
                m_DepthTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Depth"));
            }
            if (!m_VelocityTexture)
            {
                m_VelocityTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Velocity"));
            }
        }

        // GBufferテクスチャが利用可能か確認
        if (!m_AlbedoTexture ||
            !m_NormalTexture ||
            !m_MaterialTexture ||
            !m_EmissiveTexture ||
            !m_DepthTexture ||
            !m_VelocityTexture)
        {
            return;
        }

        // 画面サイズ変更に対応
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();
        const bool bUseRenderGraphAttachmentStates = m_bPreferRenderGraphGBufferResources;

        bool bFramebufferTargetsChanged = true;
        if (m_GBufferFramebuffer)
        {
            bFramebufferTargetsChanged =
                m_GBufferFramebuffer->GetColorAttachment(0).get() != m_AlbedoTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(1).get() != m_NormalTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(2).get() != m_MaterialTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(3).get() != m_EmissiveTexture.get() ||
                m_GBufferFramebuffer->GetColorAttachment(4).get() != m_VelocityTexture.get() ||
                m_GBufferFramebuffer->GetDepthStencilAttachment().get() != m_DepthTexture.get();
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            bFramebufferTargetsChanged ||
            m_bGBufferRenderPassUsesRenderGraphAttachmentStates != bUseRenderGraphAttachmentStates ||
            !m_GBufferRenderPass ||
            !m_GBufferFramebuffer ||
            (bNeedsDrawPipeline && !bDrawPipelinesReady))
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // GBuffer互換レンダーパス作成（Load既存内容）
            if (!CreateDrawPipeline(context,
                                    bNeedsDrawPipeline,
                                    bUseRenderGraphAttachmentStates))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "描画パイプラインの作成に失敗");
                return;
            }
        }
    }

    void MegaGeometryPass::Declare(RenderGraphBuilder &builder)
    {
        // 前のフレームで準備したまま積まなかった結果は使わない
        m_bFramePrepared = false;
        m_DeclaredDebugMode = builder.GetContext() ? builder.GetContext()->GetActiveDebugMode() : DebugViewMode::Normal;
        m_IndirectDrawBufferHandle = {};
        m_DrawCountBufferHandle = {};
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferEmissiveHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferVelocityHandle = {};

        if (m_IndirectDrawBuffer)
        {
            m_IndirectDrawBufferHandle = builder.ImportBuffer(m_IndirectDrawBuffer,
                                                              RHI::ResourceState::Common,
                                                              "MegaGeometry_IndirectDraw");
            if (m_IndirectDrawBufferHandle.IsValid())
            {
                builder.Write(m_IndirectDrawBufferHandle,
                              RHI::ResourceState::Common,
                              RHI::ResourceState::Common);
            }
        }

        if (m_DrawCountBuffer)
        {
            m_DrawCountBufferHandle = builder.ImportBuffer(m_DrawCountBuffer,
                                                           RHI::ResourceState::Common,
                                                           "MegaGeometry_DrawCount");
            if (m_DrawCountBufferHandle.IsValid())
            {
                builder.Write(m_DrawCountBufferHandle,
                              RHI::ResourceState::Common,
                              RHI::ResourceState::Common);
            }
        }

        RGTextureHandle albedoProbe;
        RGTextureHandle normalProbe;
        RGTextureHandle materialProbe;
        RGTextureHandle emissiveProbe;
        RGTextureHandle depthProbe;
        RGTextureHandle velocityProbe;
        const bool bHasAllNamedGBufferAttachments =
            builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedoProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normalProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferMaterial, materialProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferEmissive, emissiveProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depthProbe) &&
            builder.TryGetTexture(RenderGraphResourceNames::GBufferVelocity, velocityProbe);

        if (bHasAllNamedGBufferAttachments)
        {
            RGTextureHandle albedoHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferAlbedo,
                                                    albedoHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferAlbedoHandle = albedoHandle.ToResourceHandle();
            }

            RGTextureHandle normalHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferNormal,
                                                    normalHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferNormalHandle = normalHandle.ToResourceHandle();
            }

            RGTextureHandle materialHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferMaterial,
                                                    materialHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferMaterialHandle = materialHandle.ToResourceHandle();
            }

            RGTextureHandle emissiveHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferEmissive,
                                                    emissiveHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferEmissiveHandle = emissiveHandle.ToResourceHandle();
            }

            // velocity: MegaGeometry の画素の動き（GBufferPass と同じ currentUV - previousUV）を書く。
            RGTextureHandle velocityHandle;
            if (builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::GBufferVelocity,
                                                    velocityHandle,
                                                    RHI::AttachmentLoadOp::Load,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource))
            {
                m_GBufferVelocityHandle = velocityHandle.ToResourceHandle();
            }

            RGTextureHandle depthHandle;
            if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth,
                                         depthHandle,
                                         RGAttachmentKind::DepthStencil,
                                         RGAttachmentMutability::Write,
                                         RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::DepthWrite,
                                         RHI::ResourceState::ShaderResource))
            {
                m_GBufferDepthHandle = depthHandle.ToResourceHandle();
            }
        }

        m_MegaGeometryCompleteHandle = builder.CreateLogical("MegaGeometryComplete");
        builder.Write(m_MegaGeometryCompleteHandle,
                      RHI::ResourceState::Common,
                      RHI::ResourceState::Common);
        builder.PreserveInsertionOrder();
    }

    void MegaGeometryPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        // 準備段（並列の記録）を経たフレームは、添付の解決・Setup・表の準備が済んでいる。同じ並びで ID のラスタが
        // 記録を進めるので、ここではパスの状態を書き換えず、決めてあるものを積むだけにする
        const bool bPrepared = m_bFramePrepared;
        if (!bPrepared)
        {
            ResolveGraphGBufferTextures(resources);
            Setup(context);
        }
        Execute(context);
        if (!bPrepared)
        {
            m_bPreferRenderGraphGBufferResources = false;
        }
    }

    void MegaGeometryPass::ResolveGraphGBufferTextures(RenderGraphResources &resources)
    {
        RHI::TexturePtr graphAlbedoTexture = m_GBufferAlbedoHandle.IsValid()
                                                 ? resources.GetTexture(m_GBufferAlbedoHandle)
                                                 : nullptr;
        RHI::TexturePtr graphNormalTexture = m_GBufferNormalHandle.IsValid()
                                                 ? resources.GetTexture(m_GBufferNormalHandle)
                                                 : nullptr;
        RHI::TexturePtr graphMaterialTexture = m_GBufferMaterialHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferMaterialHandle)
                                                   : nullptr;
        RHI::TexturePtr graphEmissiveTexture = m_GBufferEmissiveHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferEmissiveHandle)
                                                   : nullptr;
        RHI::TexturePtr graphDepthTexture = m_GBufferDepthHandle.IsValid()
                                                ? resources.GetTexture(m_GBufferDepthHandle)
                                                : nullptr;
        RHI::TexturePtr graphVelocityTexture = m_GBufferVelocityHandle.IsValid()
                                                   ? resources.GetTexture(m_GBufferVelocityHandle)
                                                   : nullptr;

        const bool bHasAllRenderGraphGBufferResources =
            graphAlbedoTexture &&
            graphNormalTexture &&
            graphMaterialTexture &&
            graphEmissiveTexture &&
            graphDepthTexture &&
            graphVelocityTexture;

        m_bPreferRenderGraphGBufferResources = bHasAllRenderGraphGBufferResources;
        if (m_bPreferRenderGraphGBufferResources)
        {
            m_AlbedoTexture = graphAlbedoTexture;
            m_NormalTexture = graphNormalTexture;
            m_MaterialTexture = graphMaterialTexture;
            m_EmissiveTexture = graphEmissiveTexture;
            m_DepthTexture = graphDepthTexture;
            m_VelocityTexture = graphVelocityTexture;
        }
        else
        {
            m_AlbedoTexture.reset();
            m_NormalTexture.reset();
            m_MaterialTexture.reset();
            m_EmissiveTexture.reset();
            m_DepthTexture.reset();
            m_VelocityTexture.reset();
        }
    }

    bool MegaGeometryPass::DeclaresAllLayoutChanges() const
    {
        // グラフの Texture は GBuffer の添付だけで、添付の状態遷移の render pass の finalState が宣言（ShaderResource）と一致する。
        // 間接描画の引数・数のバッファは layout を持たない
        return m_bInitialized;
    }

    RGRecordPolicy MegaGeometryPass::GetRecordPolicy() const
    {
        // 記録を ID のラスタへ移すフレームだけ。表の準備と移すかの判定は準備段（RenderThread）で済ませ、Execute は
        // 添付の状態遷移を積むだけになる。移さないフレームは MegaGeometry の記録（中で自分でバリアを張る）を積むので直列
        const bool bDeferredRecord = m_bVisibilityPlanEnabled && m_VisibilityRaster &&
                                     m_VisibilityRaster->IsExecutionPlanned() &&
                                     ShouldSkipGBufferDraw(m_DeclaredDebugMode);
        return DeclaresAllLayoutChanges() && bDeferredRecord ? RGRecordPolicy::Parallel : RGRecordPolicy::Serial;
    }

    void MegaGeometryPass::PrepareParallelRecord(RenderGraphResources &resources, ViewRenderContext &context)
    {
        ResolveGraphGBufferTextures(resources);
        Setup(context);
        m_PreparedAction = PrepareFrame(context);
        m_bFramePrepared = true;
        // 記録（ID のラスタの Execute）と同時に読まれないよう、ここで戻す（Execute の添付の状態遷移は決めた結果で積む）
        m_bPreferRenderGraphGBufferResources = false;
        if (m_bFrameRecordDeferred)
        {
            PrepareConstantMaterialTextures();
        }
    }

    void MegaGeometryPass::PrepareConstantMaterialTextures()
    {
        if (!m_Device)
        {
            return;
        }
        if (!m_ConstantMaterialTextures)
        {
            m_ConstantMaterialTextures = Container::MakeUnique<ConstantMaterialTextureCache>();
        }
        // 記録の区間ごとの材質と同じ条件で引く（ORM を張る材質の分も作るが、1x1 で 8bit の値ごとに 1 枚だけ）
        for (const SceneSection &section : m_SceneTables.Sections)
        {
            if (!section.Representative)
            {
                continue;
            }
            const auto &mat = section.Representative->Material;
            if (!std::isfinite(mat.Metallic) || !std::isfinite(mat.Roughness) || !std::isfinite(mat.OcclusionStrength))
            {
                continue;
            }
            if (mat.Metallic >= 0)
            {
                (void)m_ConstantMaterialTextures->GetOrCreate(m_Device, mat.Metallic);
            }
            if (mat.Roughness >= 0)
            {
                (void)m_ConstantMaterialTextures->GetOrCreate(m_Device, mat.Roughness);
            }
            if (mat.OcclusionStrength != 1)
            {
                (void)m_ConstantMaterialTextures->GetOrCreate(m_Device, mat.OcclusionStrength);
            }
        }
    }

    // ========================================
    // Execute
    // ========================================

    bool MegaGeometryPass::ShouldSkipGBufferDraw(DebugViewMode mode) const
    {
        // 解決のパスが分かるときは、パイプラインの準備まで含めて判定する（使えないのに止めると画面が空になる）
        return m_bSkipGBufferDraw && m_Device &&
               (m_ResolvePass ? m_ResolvePass->CanResolve(m_Device, mode)
                              : VisibilityResolveGeometry::IsSupported(m_Device->GetCapabilities()));
    }

    void MegaGeometryPass::Execute(ViewRenderContext &context)
    {
        // 準備段が決めたフレームはその結果で積む（1 本の記録ではここで決める）
        const PreparedFrameAction action = m_bFramePrepared ? m_PreparedAction : PrepareFrame(context);
        m_bFramePrepared = false;
        if (action == PreparedFrameAction::EmptyTransition)
        {
            context.EnqueueFullscreenPass(m_GBufferRenderPass,
                                          m_GBufferFramebuffer,
                                          context.GetActiveLocalViewport(),
                                          context.GetActiveLocalScissor(),
                                          RHI::PipelinePtr{},
                                          RHI::DescriptorSetPtr{},
                                          0,
                                          0);
        }
        else if (action == PreparedFrameAction::Record)
        {
            context.EnqueueMegaGeometryPass(this);
        }
    }

    MegaGeometryPass::PreparedFrameAction MegaGeometryPass::PrepareFrame(ViewRenderContext &context)
    {
        m_bFrameRecordDeferred = false;

        const bool bCanEnqueueEmptyTransitionPass =
            m_bPreferRenderGraphGBufferResources &&
            m_GBufferRenderPass &&
            m_GBufferFramebuffer;
        const PreparedFrameAction emptyTransition =
            bCanEnqueueEmptyTransitionPass ? PreparedFrameAction::EmptyTransition : PreparedFrameAction::None;

        if (m_Instances.empty() || !m_CullPipeline || !context.Resources.MegaGeometry)
        {
            if (m_Instances.empty())
            {
                ReleaseIdleMeshRegionOwners(context.FrameIndex, context.ResolveRenderFrameSerial());
            }
            return emptyTransition;
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !bDrawPipelinesReady)
        {
            return emptyTransition;
        }

        if (!context.GetActiveCamera())
        {
            return emptyTransition;
        }

        // 表を書く共通の前段: インスタンスの表・メッシュの表・影の表を、読み手のどのパス（主のカリングと描画、
        // ID のラスタと解決、VSM の影）よりも前にここで書く。記録がどこで行われても（キューのコマンド・ID のラスタ）同じ表を使う。
        // 常駐の表への散在書き込みは、ここで直接コマンドリストへ記録する（RenderGraph はこのパスの前後でキューを流すので、
        // 読み手のパスの記録より前に並ぶ）
        PrepareSceneTables(context.BuildMegaGeometryPassCommand(this).MegaGeometry, context.CommandList);

        // 解決が GBuffer を書くフレームは GBuffer の描画を止めるので、2パスの遮蔽の HZB の元になる深度が GBuffer に入らない。
        // ID のラスタがその Execute の中で記録を駆動して（ID・深度の1パス目 → HZB → 2パス目）、HZB を ID の深度から作る。
        // GBuffer の添付の状態遷移（render pass の開始と終了）は、ここで済ませる
        if (m_bVisibilityPlanEnabled && m_VisibilityRaster && m_VisibilityRaster->IsExecutionPlanned() &&
            ShouldSkipGBufferDraw(context.GetActiveDebugMode()))
        {
            m_bFrameRecordDeferred = true;
            return emptyTransition;
        }

        return PreparedFrameAction::Record;
    }

    namespace
    {
        // 材質の区間にまとめられるか（同じ材質値・同じテクスチャのハンドルか）
        bool IsSameMaterial(const MegaGeometry::MegaMeshMaterial &a, const MegaGeometry::MegaMeshMaterial &b)
        {
            for (uint32_t index = 0; index < 4; ++index)
            {
                if (a.BaseColor[index] != b.BaseColor[index])
                {
                    return false;
                }
            }
            for (uint32_t index = 0; index < 3; ++index)
            {
                if (a.EmissiveColor[index] != b.EmissiveColor[index])
                {
                    return false;
                }
            }
            return a.Metallic == b.Metallic && a.Roughness == b.Roughness &&
                   a.OcclusionStrength == b.OcclusionStrength && a.EmissiveLuminanceNits == b.EmissiveLuminanceNits &&
                   a.AlbedoTexture == b.AlbedoTexture && a.NormalTexture == b.NormalTexture &&
                   a.MetallicTexture == b.MetallicTexture && a.RoughnessTexture == b.RoughnessTexture &&
                   a.AOTexture == b.AOTexture && a.ORMTexture == b.ORMTexture && a.HeightTexture == b.HeightTexture &&
                   a.bNormalTwoChannel == b.bNormalTwoChannel && a.HeightScale == b.HeightScale &&
                   a.bHasHeightMap == b.bHasHeightMap && a.DisplacementUVSpacing == b.DisplacementUVSpacing;
        }

        // 材質の区間ごとの定数（megageometry.vert/frag の MVPData と一致。ワールド変換はインスタンスの表にある）
        struct SectionUniformData
        {
            float View[16];
            float Projection[16];
            float CameraPosition[4];
            float ObjectColor[4];
            float EmissiveChromaticityAndLuminanceNits[4];
            float PomParams[4];
            float PreviousView[16];
            float PreviousProjection[16];
            float FrameParams[4]; // x=前のカメラがあるか（1/0）, y=発光に掛けるプリエクスポージャ, z=変位の頂点の間隔（UV）, w=描画の番号がLODの段か（1/0）
            float MaterialParams[4]; // x=ORMの1枚を metallic の枠に張ったか（1/0）, y=法線が2チャンネル（BC5）か（1/0）, z=材質のテクスチャが sparse（VT）か（1/0）, w=VT のフィードバックのパラメータ（アルベド。0 は書かない）
            float VtFeedbackParams[4]; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallic の枠）, z=高さ（0 は書かない）, w=未使用
        };
        static_assert(sizeof(SectionUniformData) <= 512u);

        constexpr uint32_t SectionUniformBufferBytes = 512;
        constexpr uint32_t IndirectCommandBytes = static_cast<uint32_t>(sizeof(MegaGeometry::DrawIndexedIndirectCommand));
    } // namespace

    namespace
    {
        // 表の差分更新: m_Instances の位置が表へ載っていない印
        constexpr uint32_t NoSceneRow = 0xFFFFFFFFu;
        // MEGA_TABLES の照合の間隔（表を作った回数）
        constexpr uint32_t SceneTablesCheckInterval = 120;

        // メッシュの区画をまとめた描画で引くためのアドレスと基点
        struct SceneMeshAddressing
        {
            uint64_t ClusterAddress = 0;
            uint64_t VertexBase = 0;
            uint64_t IndexBase = 0;
            bool bAddressable = false;
            bool bAligned = false;

            bool IsUsable() const
            {
                return bAddressable && bAligned && VertexBase <= 0x7FFFFFFFull && IndexBase <= 0xFFFFFFFFull;
            }
        };

        SceneMeshAddressing ResolveSceneMeshAddressing(const MegaGeometry::MegaMeshGPUData &gpuData)
        {
            // クラスタ配列はデバイスアドレスで、頂点・インデックスはプールの塊の先頭からの基点で引く。
            // 基点は頂点・インデックスの単位で表すので、領域の先頭が頂点・インデックスの大きさの倍数であること
            // （プールの区画は 256 バイト整列）
            SceneMeshAddressing addressing;
            addressing.ClusterAddress = gpuData.ClusterBuffer ? gpuData.ClusterBuffer->GetDeviceAddress() : 0ull;
            addressing.bAddressable = addressing.ClusterAddress != 0 && gpuData.VertexBuffer && gpuData.IndexBuffer;
            addressing.bAligned = gpuData.VertexBufferOffsetBytes % sizeof(Mesh3DVertex) == 0 &&
                                  gpuData.IndexBufferOffsetBytes % sizeof(uint32_t) == 0;
            addressing.VertexBase = gpuData.VertexBufferOffsetBytes / sizeof(Mesh3DVertex);
            addressing.IndexBase = gpuData.IndexBufferOffsetBytes / sizeof(uint32_t);
            return addressing;
        }

        bool IsSameSphere(const float (&a)[4], const BoundingSphere &b)
        {
            return a[0] == b.CenterX && a[1] == b.CenterY && a[2] == b.CenterZ && a[3] == b.Radius;
        }
    } // namespace

    bool MegaGeometryPass::IsSceneBvhAvailable() const
    {
        // 列（m_BvhQueueBuffer）は BVH のインスタンスが初めて現れた記録で作る。作れなければその記録は描画を見送る
        return m_Settings.bUseGroupBVH && m_BvhCullPipeline && m_BvhCounterBuffer;
    }

    bool MegaGeometryPass::CanTraverseSceneBvh(const MegaGeometry::MegaMeshGPUData *data) const
    {
        // グループの BVH を持つメッシュ（NVMESH v1.1 の焼き込み、または読み込み時に実行時に作ったもの）は BVH をたどって判定し、
        // BVH を作れなかったメッシュだけが平らなクラスタの列で判定する
        return IsSceneBvhAvailable() && data && data->GroupBVHNodeCount > 0 && data->GroupBVHBufferBytes > 0 &&
               !data->GroupBVHLevelNodeCounts.empty() &&
               data->GroupBVHLevelNodeCounts.size() <= MegaGeometry::GROUP_BVH_MAX_LEVELS;
    }

    MegaGeometryPass::GPUMegaMesh MegaGeometryPass::MakeSceneMeshRow(const MegaGeometry::MegaMeshGPUData &gpuData,
                                                                     uint64_t clusterAddress, uint64_t vertexBase,
                                                                     uint64_t indexBase, uint32_t sectionIndex,
                                                                     bool bTraverseBvh)
    {
        const uint64_t clusterAddressWithOffset = clusterAddress + gpuData.ClusterBufferOffsetBytes;
        GPUMegaMesh mesh{};
        if (gpuData.LODBounds.IsValid())
        {
            mesh.LODSphere[0] = gpuData.LODBounds.CenterX;
            mesh.LODSphere[1] = gpuData.LODBounds.CenterY;
            mesh.LODSphere[2] = gpuData.LODBounds.CenterZ;
            mesh.LODSphere[3] = gpuData.LODBounds.Radius;
        }
        mesh.ClusterAddressLow = static_cast<uint32_t>(clusterAddressWithOffset & 0xFFFFFFFFull);
        mesh.ClusterAddressHigh = static_cast<uint32_t>(clusterAddressWithOffset >> 32);
        mesh.ClusterCount = gpuData.ClusterCount;
        mesh.SectionIndex = sectionIndex;
        mesh.VertexBase = static_cast<uint32_t>(vertexBase);
        mesh.IndexBase = static_cast<uint32_t>(indexBase);
        mesh.PageTableBase = gpuData.PageTableBase;
        // 締まった境界球。有限で半径が正のときだけ載せ、無ければ判定は根の節の球を使う。実行時の LOD の TotalBounds は最も細かい段
        // だけから作るので、表へ写した後に mega_scene_scatter.comp の段 3 が、半径を全段のクラスタの球を包むまで広げる
        const BoundingSphere &bounds = gpuData.TotalBounds;
        if (std::isfinite(bounds.CenterX) && std::isfinite(bounds.CenterY) && std::isfinite(bounds.CenterZ) &&
            std::isfinite(bounds.Radius) && bounds.Radius > 0.0f)
        {
            mesh.BoundsSphere[0] = bounds.CenterX;
            mesh.BoundsSphere[1] = bounds.CenterY;
            mesh.BoundsSphere[2] = bounds.CenterZ;
            mesh.BoundsSphere[3] = bounds.Radius;
        }
        if (bTraverseBvh)
        {
            const uint64_t bvhAddress = clusterAddress + gpuData.GroupBVHBufferOffsetBytes;
            mesh.BvhAddressLow = static_cast<uint32_t>(bvhAddress & 0xFFFFFFFFull);
            mesh.BvhAddressHigh = static_cast<uint32_t>(bvhAddress >> 32);
            mesh.BvhNodeCount = gpuData.GroupBVHNodeCount;
        }
        return mesh;
    }

    MegaGeometryPass::VisibilityRequest MegaGeometryPass::MakeSceneVisibilityRequest(
        const MegaMeshInstance &instance, size_t instanceIndex, const MegaGeometry::MegaMeshGPUData &gpuData)
    {
        VisibilityRequest request;
        request.Key = instance.ObjectId != 0 ? instance.ObjectId : (0x8000000000000000ull | instanceIndex);
        request.MeshId = instance.Handle.Id;
        request.ComponentId = instance.ComponentId;
        request.ClusterBufferIdentity = gpuData.ClusterBuffer.get();
        request.ClusterBufferOffsetBytes = gpuData.ClusterBufferOffsetBytes;
        request.ClusterCount = gpuData.ClusterCount;
        request.TableSlot = instance.TableSlot;
        return request;
    }

    void MegaGeometryPass::FillSceneShadowRow(const MegaMeshInstance &instance, const MegaGeometry::MegaMeshGPUData &gpuData,
                                              MegaGeometryShadowInstance &ioEntry, ShadowInstanceRecord &outRecord) const
    {
        // 頂点・インデックスの塊のバッファのアドレス（表へ載るメッシュは、両方のバッファがあることを確かめてある）
        const uint64_t vertexAddress = gpuData.VertexBuffer->GetDeviceAddress();
        const uint64_t indexAddress = gpuData.IndexBuffer->GetDeviceAddress();
        ioEntry.VertexAddress[0] = static_cast<uint32_t>(vertexAddress & 0xFFFFFFFFull);
        ioEntry.VertexAddress[1] = static_cast<uint32_t>(vertexAddress >> 32);
        ioEntry.IndexAddress[0] = static_cast<uint32_t>(indexAddress & 0xFFFFFFFFull);
        ioEntry.IndexAddress[1] = static_cast<uint32_t>(indexAddress >> 32);
        ioEntry.Flags = 0u;
        std::memset(ioEntry.BoundsSphere, 0, sizeof(ioEntry.BoundsSphere));
        outRecord = ShadowInstanceRecord{};
        // 影を落とすインスタンスだけが、影の判定のワークグループ（64クラスタ）を持つ
        if (instance.bCastShadow)
        {
            ioEntry.Flags |= MegaGeometryShadowFlagCaster;
            const bool bHasShadowBounds =
                ResolveShadowBounds(instance.WorldMatrix, instance.WorldBounds, gpuData.TotalBounds, ioEntry.BoundsSphere);
            if (bHasShadowBounds)
            {
                ioEntry.Flags |= MegaGeometryShadowFlagBounds;
                std::memcpy(outRecord.Sphere, ioEntry.BoundsSphere, sizeof(outRecord.Sphere));
            }
            outRecord.bCaster = true;
            outRecord.bHasBounds = bHasShadowBounds;
        }
    }

    void MegaGeometryPass::ComposeSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables, bool bCommit,
                                              VariableArray<ShadowInstanceRecord> &outRecords)
    {
        tables.Sections.clear();
        tables.Drawables.clear();
        tables.Entries.clear();
        tables.Meshes.clear();
        tables.MeshOwners.clear();
        tables.VisibilityRequests.clear();
        tables.ShadowInstances.clear();
        tables.ShadowTotalGroups = 0;
        tables.ShadowCasterCount = 0;
        tables.TotalGroups = 0;
        tables.BvhInstanceCount = 0;
        tables.BvhLevelCapacity.clear();
        tables.BvhLeafCapacity = 0;
        tables.Drawables.reserve(m_Instances.size());
        tables.Entries.reserve(m_Instances.size());
        tables.VisibilityRequests.reserve(m_Instances.size());
        tables.ShadowInstances.reserve(m_Instances.size());
        outRecords.assign(m_Instances.size(), ShadowInstanceRecord{});
        if (bCommit)
        {
            m_SceneInstanceRows.assign(m_Instances.size(), NoSceneRow);
            m_SceneInstanceHandles.resize(m_Instances.size());
            m_SceneMeshSources.clear();
            m_SceneSkippedMeshes.clear();
            m_bSceneTablesBvhAvailable = IsSceneBvhAvailable();
        }

        // BVH のインスタンスをインスタンスの表の先頭に並べる（平らな判定のワークグループは、後ろのインスタンスだけが持つ）
        VariableArray<size_t> &instanceOrder = m_SceneInstanceOrder;
        instanceOrder.clear();
        instanceOrder.reserve(m_Instances.size());
        {
            VariableArray<size_t> flatInstances;
            for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
            {
                const auto *orderData = command.MegaGeometry->GetReadyMegaMeshGPUData(m_Instances[instanceIndex].Handle);
                (CanTraverseSceneBvh(orderData) ? instanceOrder : flatInstances).push_back(instanceIndex);
            }
            instanceOrder.insert(instanceOrder.end(), flatInstances.begin(), flatInstances.end());
        }

        // メッシュの表の番号（ロード済みのメッシュごとに 1 行。同じメッシュのインスタンスは同じ行を指す）
        Container::UnorderedMap<const MegaGeometry::MegaMeshGPUData *, uint32_t> meshIndices;
        // 表へ載らなかったメッシュ（ハンドルごとに 1 つ覚える）
        Container::UnorderedSet<uint64_t> skippedHandles;
        // 「見えた」ビットの区画は席ごとに決める（AssignVisibilityRegion）。この表の番号で、前の表に載った席を見分ける
        if (bCommit)
        {
            ++m_VisibilityBuildStamp;
        }
        for (const size_t instanceIndex : instanceOrder)
        {
            const auto &instance = m_Instances[instanceIndex];
            // 区画へのコピーが GPU で完了したメッシュだけを描く（書き込み中の区画は読まない）
            const auto *gpuData = command.MegaGeometry->GetReadyMegaMeshGPUData(instance.Handle);
            const auto noteSkipped = [&]() -> void
            {
                if (bCommit && skippedHandles.insert(instance.Handle.Id).second)
                {
                    SceneSkippedMesh skipped;
                    skipped.Handle = instance.Handle;
                    skipped.GpuData = gpuData;
                    skipped.ClusterCount = gpuData ? gpuData->ClusterCount : 0u;
                    m_SceneSkippedMeshes.push_back(skipped);
                }
            };
            if (bCommit)
            {
                m_SceneInstanceHandles[instanceIndex] = instance.Handle.Id;
            }
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                noteSkipped();
                continue;
            }

            const SceneMeshAddressing addressing = ResolveSceneMeshAddressing(*gpuData);
            if (!addressing.IsUsable())
            {
                if (bCommit && !m_bBatchUnsupportedLogged)
                {
                    NORVES_LOG_ERROR("MegaGeometryPass",
                                     "MEGA_BATCH_INSTANCE_SKIPPED mesh=\"%s\" addressable=%d "
                                     "aligned=%d "
                                     "メッシュの区画をまとめた描画で引けないため、このメッシュは描き"
                                     "ません",
                                     gpuData->DebugName.empty() ? "" : gpuData->DebugName.c_str(),
                                     addressing.bAddressable ? 1 : 0, addressing.bAligned ? 1 : 0);
                    m_bBatchUnsupportedLogged = true;
                }
                noteSkipped();
                continue;
            }

            // 区間を探す（無ければ作る）
            VariableArray<SceneSection> &sections = tables.Sections;
            uint32_t sectionIndex = 0;
            for (; sectionIndex < sections.size(); ++sectionIndex)
            {
                const auto *representative = sections[sectionIndex].Representative;
                if (representative->VertexBuffer.get() == gpuData->VertexBuffer.get() &&
                    representative->IndexBuffer.get() == gpuData->IndexBuffer.get() &&
                    IsSameMaterial(representative->Material, gpuData->Material))
                {
                    break;
                }
            }
            if (sectionIndex == sections.size())
            {
                SceneSection section;
                section.Representative = gpuData;
                sections.push_back(section);
            }
            sections[sectionIndex].ClusterTotal += gpuData->ClusterCount;

            // メッシュの表の行（このフレームで初めて出たメッシュなら足す）
            const bool bTraverseBvh = CanTraverseSceneBvh(gpuData);
            uint32_t meshIndex = 0;
            const auto foundMesh = meshIndices.find(gpuData);
            if (foundMesh != meshIndices.end())
            {
                meshIndex = foundMesh->second;
            }
            else
            {
                const GPUMegaMesh mesh = MakeSceneMeshRow(*gpuData, addressing.ClusterAddress, addressing.VertexBase,
                                                          addressing.IndexBase, sectionIndex, bTraverseBvh);
                meshIndex = static_cast<uint32_t>(tables.Meshes.size());
                tables.Meshes.push_back(mesh);
                tables.MeshOwners.push_back(gpuData->RegionOwner);
                meshIndices.emplace(gpuData, meshIndex);
                if (bCommit)
                {
                    // 差分更新は、次のフレームもこのメッシュが同じ中身のまま準備できていることを確かめてから行を使い続ける
                    SceneMeshSource source;
                    source.Handle = instance.Handle;
                    source.GpuData = gpuData;
                    source.VertexBuffer = gpuData->VertexBuffer.get();
                    source.IndexBuffer = gpuData->IndexBuffer.get();
                    source.ClusterBuffer = gpuData->ClusterBuffer.get();
                    source.RegionOwner = gpuData->RegionOwner.get();
                    source.Row = mesh;
                    source.TotalBounds[0] = gpuData->TotalBounds.CenterX;
                    source.TotalBounds[1] = gpuData->TotalBounds.CenterY;
                    source.TotalBounds[2] = gpuData->TotalBounds.CenterZ;
                    source.TotalBounds[3] = gpuData->TotalBounds.Radius;
                    source.bTraverseBvh = bTraverseBvh;
                    source.BvhLeafCount = gpuData->GroupBVHLeafCount;
                    source.BvhLevelNodeCounts = gpuData->GroupBVHLevelNodeCounts;
                    m_SceneMeshSources.push_back(std::move(source));
                }
            }

            VisibilityRequest request = MakeSceneVisibilityRequest(instance, instanceIndex, *gpuData);
            request.Offset = bCommit ? AssignVisibilityRegion(instance.TableSlot, request)
                                     : PeekVisibilityRegion(instance.TableSlot, request);

            // 変換は席の表にあり、GPU が描く順の表の行を組むときに席から引く
            GPUMegaEntry entry;
            entry.Slot = instance.TableSlot;
            entry.MeshIndex = meshIndex;
            entry.FirstGroup = static_cast<uint32_t>(tables.TotalGroups);
            entry.VisibleOffset = request.Offset;
            if (bTraverseBvh)
            {
                // BVH をたどるインスタンスは、平らな判定のワークグループを持たない
                ++tables.BvhInstanceCount;
                if (tables.BvhLevelCapacity.size() < gpuData->GroupBVHLevelNodeCounts.size())
                {
                    tables.BvhLevelCapacity.resize(gpuData->GroupBVHLevelNodeCounts.size(), 0ull);
                }
                for (size_t level = 0; level < gpuData->GroupBVHLevelNodeCounts.size(); ++level)
                {
                    tables.BvhLevelCapacity[level] += gpuData->GroupBVHLevelNodeCounts[level];
                }
                tables.BvhLeafCapacity += gpuData->GroupBVHLeafCount;
            }
            else
            {
                tables.TotalGroups += (static_cast<uint64_t>(gpuData->ClusterCount) + 63u) / 64u;
            }

            if (bCommit)
            {
                m_SceneInstanceRows[instanceIndex] = static_cast<uint32_t>(tables.Entries.size());
            }
            SceneDrawable drawable;
            drawable.InstanceIndex = instanceIndex;
            drawable.GpuData = gpuData;
            tables.Drawables.push_back(drawable);
            tables.Entries.push_back(entry);
            tables.VisibilityRequests.push_back(request);

            // 影の表: BVH のインスタンスも平らなクラスタの列として全部見る（影の判定は BVH をたどらない）
            MegaGeometryShadowInstance shadowEntry{};
            shadowEntry.FirstGroup = static_cast<uint32_t>(tables.ShadowTotalGroups);
            ShadowInstanceRecord &shadowRecord = outRecords[instanceIndex];
            FillSceneShadowRow(instance, *gpuData, shadowEntry, shadowRecord);
            if (shadowRecord.bCaster)
            {
                tables.ShadowTotalGroups += (static_cast<uint64_t>(gpuData->ClusterCount) + 63u) / 64u;
                ++tables.ShadowCasterCount;
            }
            tables.ShadowInstances.push_back(shadowEntry);
        }
    }

    void MegaGeometryPass::MarkSceneTableDirty(size_t position)
    {
        // 表が無ければ次の表は全部を作るので、覚えなくてよい
        if (!m_bSceneTablesValid)
        {
            return;
        }
        if (m_SceneTableDirtyMarks.size() < m_Instances.size())
        {
            m_SceneTableDirtyMarks.resize(m_Instances.size(), 0u);
        }
        if (position < m_SceneTableDirtyMarks.size() && m_SceneTableDirtyMarks[position] == 0u)
        {
            m_SceneTableDirtyMarks[position] = 1u;
            m_SceneTableDirtyPositions.push_back(static_cast<uint32_t>(position));
        }
    }

    bool MegaGeometryPass::PatchSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables, bool bResidencyFresh)
    {
        if (!m_bSceneTablesValid || &tables != &m_SceneTables || m_SceneInstanceRows.size() != m_Instances.size() ||
            m_SceneInstanceHandles.size() != m_Instances.size() || m_SceneMeshSources.size() != tables.Meshes.size() ||
            m_ShadowRecords.size() != m_Instances.size() || IsSceneBvhAvailable() != m_bSceneTablesBvhAvailable)
        {
            return false;
        }

        // ----- 確かめる（作り直しが要れば、何も変えずに戻る） -----
        // 表へ載せたメッシュが、同じ中身のまま準備できていること（材質は作った後に変わらず、区間はメッシュと同じ持ち主を指す）
        for (const SceneMeshSource &source : m_SceneMeshSources)
        {
            const auto *gpuData = command.MegaGeometry->GetReadyMegaMeshGPUData(source.Handle);
            if (gpuData != source.GpuData || gpuData->ClusterCount == 0 || gpuData->VertexBuffer.get() != source.VertexBuffer ||
                gpuData->IndexBuffer.get() != source.IndexBuffer || gpuData->ClusterBuffer.get() != source.ClusterBuffer ||
                gpuData->RegionOwner.get() != source.RegionOwner || !IsSameSphere(source.TotalBounds, gpuData->TotalBounds) ||
                gpuData->GroupBVHLeafCount != source.BvhLeafCount ||
                gpuData->GroupBVHLevelNodeCounts.size() != source.BvhLevelNodeCounts.size() ||
                !std::equal(source.BvhLevelNodeCounts.begin(), source.BvhLevelNodeCounts.end(),
                            gpuData->GroupBVHLevelNodeCounts.begin()))
            {
                return false;
            }
            const SceneMeshAddressing addressing = ResolveSceneMeshAddressing(*gpuData);
            const bool bTraverseBvh = CanTraverseSceneBvh(gpuData);
            if (!addressing.IsUsable() || bTraverseBvh != source.bTraverseBvh)
            {
                return false;
            }
            const GPUMegaMesh row = MakeSceneMeshRow(*gpuData, addressing.ClusterAddress, addressing.VertexBase,
                                                     addressing.IndexBase, source.Row.SectionIndex, bTraverseBvh);
            if (std::memcmp(&row, &source.Row, sizeof(row)) != 0)
            {
                return false;
            }
        }
        // 表へ載らなかったメッシュが、準備できていない（または同じ中身で引けない）ままであること
        for (const SceneSkippedMesh &skipped : m_SceneSkippedMeshes)
        {
            const auto *gpuData = command.MegaGeometry->GetReadyMegaMeshGPUData(skipped.Handle);
            if (gpuData != skipped.GpuData || (gpuData && gpuData->ClusterCount != skipped.ClusterCount))
            {
                return false;
            }
        }
        // 直したインスタンスのメッシュが替わっていないこと（替わると区間・メッシュの行・並びが変わる）
        for (const uint32_t position : m_SceneTableDirtyPositions)
        {
            if (position >= m_Instances.size() || m_Instances[position].Handle.Id != m_SceneInstanceHandles[position])
            {
                return false;
            }
        }

        // ----- 直す -----
        // VSM のキャッシュ: このフレームに載せたメッシュの中身を覚えたものと比べる（区画・境界は上で同じと確かめたので、
        // 変わるのはページの常駐だけ）
        m_SceneMeshShadowChanged.assign(m_SceneMeshSources.size(), 0u);
        bool bAnyMeshShadowChanged = false;
        for (size_t meshIndex = 0; meshIndex < m_SceneMeshSources.size(); ++meshIndex)
        {
            const SceneMeshSource &source = m_SceneMeshSources[meshIndex];
            const bool bMeshChanged = UpdateShadowMeshState(source.Handle, *source.GpuData, bResidencyFresh);
            m_SceneMeshShadowChanged[meshIndex] = bMeshChanged ? 1u : 0u;
            bAnyMeshShadowChanged = bAnyMeshShadowChanged || bMeshChanged;
            m_ShadowDirtyStats.MeshChanges += bMeshChanged ? 1u : 0u;
        }

        // 直したインスタンスの行: 「見えた」の区画の要求・描く順の行の区画の先頭・影の行と記録
        bool bCasterChanged = false;
        for (const uint32_t position : m_SceneTableDirtyPositions)
        {
            const MegaMeshInstance &instance = m_Instances[position];
            const uint32_t row = m_SceneInstanceRows[position];
            if (row == NoSceneRow)
            {
                NoteShadowRecord(position, ShadowInstanceRecord{}, false);
                continue;
            }
            const MegaGeometry::MegaMeshGPUData &gpuData = *tables.Drawables[row].GpuData;
            GPUMegaEntry &entry = tables.Entries[row];
            VisibilityRequest request = MakeSceneVisibilityRequest(instance, position, gpuData);
            request.Offset = AssignVisibilityRegion(instance.TableSlot, request);
            tables.VisibilityRequests[row] = request;
            entry.Slot = instance.TableSlot;
            entry.VisibleOffset = request.Offset;

            MegaGeometryShadowInstance &shadowEntry = tables.ShadowInstances[row];
            const bool bWasCaster = (shadowEntry.Flags & MegaGeometryShadowFlagCaster) != 0u;
            ShadowInstanceRecord record;
            FillSceneShadowRow(instance, gpuData, shadowEntry, record);
            bCasterChanged = bCasterChanged || bWasCaster != record.bCaster;
            NoteShadowRecord(position, record, m_SceneMeshShadowChanged[entry.MeshIndex] != 0u);
        }
        // ページの常駐が変わったメッシュを使う、直していない行は、記録はそのままで今の境界を描き直す
        if (bAnyMeshShadowChanged)
        {
            for (size_t row = 0; row < tables.Entries.size(); ++row)
            {
                const size_t instanceIndex = tables.Drawables[row].InstanceIndex;
                if (m_SceneMeshShadowChanged[tables.Entries[row].MeshIndex] != 0u &&
                    (instanceIndex >= m_SceneTableDirtyMarks.size() || m_SceneTableDirtyMarks[instanceIndex] == 0u))
                {
                    const ShadowInstanceRecord record = m_ShadowRecords[instanceIndex];
                    NoteShadowRecord(instanceIndex, record, true);
                }
            }
        }
        // 投影物かどうかが変わった: 影の判定のワークグループの先頭を数え直す（落とさない行は次の行と同じ値）
        if (bCasterChanged)
        {
            tables.ShadowTotalGroups = 0;
            tables.ShadowCasterCount = 0;
            for (size_t row = 0; row < tables.ShadowInstances.size(); ++row)
            {
                MegaGeometryShadowInstance &shadowEntry = tables.ShadowInstances[row];
                shadowEntry.FirstGroup = static_cast<uint32_t>(tables.ShadowTotalGroups);
                if ((shadowEntry.Flags & MegaGeometryShadowFlagCaster) != 0u)
                {
                    tables.ShadowTotalGroups += (static_cast<uint64_t>(tables.Drawables[row].GpuData->ClusterCount) + 63u) / 64u;
                    ++tables.ShadowCasterCount;
                }
            }
        }

        m_SceneTablesStats.DeltaRows += m_SceneTableDirtyPositions.size();
        for (const uint32_t position : m_SceneTableDirtyPositions)
        {
            m_SceneTableDirtyMarks[position] = 0u;
        }
        m_SceneTableDirtyPositions.clear();
        return true;
    }

    void MegaGeometryPass::NoteShadowRecord(size_t instanceIndex, const ShadowInstanceRecord &now, bool bMeshChanged)
    {
        ShadowInstanceRecord &record = m_ShadowRecords[instanceIndex];
        if (bMeshChanged || m_ShadowDirtyMarks[instanceIndex] != 0u || record.bCaster != now.bCaster)
        {
            PushShadowInvalidation(record, now);
        }
        record = now;
    }

    void MegaGeometryPass::BuildSceneTables(const MegaGeometryPassCommand &command, SceneTables &tables)
    {
        {
            NORVES_PROFILE_SCOPE("MegaGeometryPass.BuildSceneTables");
            if (!command.MegaGeometry)
            {
                tables.Sections.clear();
                tables.Drawables.clear();
                tables.Entries.clear();
                tables.Meshes.clear();
                tables.MeshOwners.clear();
                tables.VisibilityRequests.clear();
                tables.ShadowInstances.clear();
                tables.ShadowTotalGroups = 0;
                tables.ShadowCasterCount = 0;
                tables.TotalGroups = 0;
                tables.BvhInstanceCount = 0;
                tables.BvhLevelCapacity.clear();
                tables.BvhLeafCapacity = 0;
                m_bSceneTablesValid = false;
                return;
            }

            // ----- VSM のキャッシュの無効化の準備 -----
            // ページの表が変わったときだけ写しを取り、表へ載せるメッシュの常駐の署名を作り直す
            const bool bResidencyFresh =
                command.MegaGeometry->CopyPageTableIfChanged(m_ShadowPageTableVersion, m_ShadowPageEntries);
            ++m_ShadowMeshStamp;
            // 作り直した後の最初の表は、前に表を作ったときの並びを鍵で引いて全部を比べる。それ以外は、影に効く中身が変わった位置
            // （NoteShadowPatched）・メッシュの中身が変わったインスタンス・投影物かどうかが変わったインスタンスだけを見る
            const bool bShadowRebuild = m_bShadowRebuildPending;
            Container::UnorderedMap<uint64_t, uint32_t> previousShadowPositions;
            VariableArray<uint8_t> previousShadowSeen;
            if (bShadowRebuild)
            {
                previousShadowPositions.reserve(m_ShadowPreviousInstances.size());
                for (size_t index = 0; index < m_ShadowPreviousInstances.size(); ++index)
                {
                    const MegaMeshInstance &previous = m_ShadowPreviousInstances[index];
                    const uint64_t objectKey = previous.ObjectId != 0 ? previous.ObjectId : (0x8000000000000000ull | index);
                    previousShadowPositions.emplace(MixShadowInstanceKey(objectKey, previous.ComponentId),
                                                    static_cast<uint32_t>(index));
                }
                previousShadowSeen.assign(m_ShadowPreviousInstances.size(), 0u);
                m_ShadowRecords.assign(m_Instances.size(), ShadowInstanceRecord{});
                m_ShadowDirtyMarks.assign(m_Instances.size(), 0u);
                m_ShadowDirtyPositions.clear();
                ++m_ShadowDirtyStats.Rebuilds;
            }
            else if (m_ShadowRecords.size() != m_Instances.size() || m_ShadowDirtyMarks.size() != m_Instances.size())
            {
                // 並びの数は作り直しでしか変わらない（念のため、足りない位置は投影物でないものとして扱う）
                m_ShadowRecords.resize(m_Instances.size());
                m_ShadowDirtyMarks.resize(m_Instances.size(), 0u);
            }
            m_ShadowDirtyStats.DirtySlots += m_ShadowDirtyPositions.size();
            ++m_ShadowDirtyStats.Builds;

            // 並びが変わらなければ、前の表の直したインスタンスの行だけを書き換える。変われば全部を作る
            if (!bShadowRebuild && PatchSceneTables(command, tables, bResidencyFresh))
            {
                ++m_SceneTablesStats.DeltaBuilds;
            }
            else
            {
                ComposeSceneTables(command, tables, true, m_SceneComposeRecords);
                m_bSceneTablesValid = &tables == &m_SceneTables;
                m_SceneTableDirtyMarks.assign(m_Instances.size(), 0u);
                m_SceneTableDirtyPositions.clear();
                ++m_SceneTablesStats.FullBuilds;

                // VSM のキャッシュ: 区画・境界・ページの常駐が前に載せたときから変わったメッシュは、使う席を描き直す
                m_SceneMeshShadowChanged.assign(m_SceneMeshSources.size(), 0u);
                for (size_t meshIndex = 0; meshIndex < m_SceneMeshSources.size(); ++meshIndex)
                {
                    const SceneMeshSource &source = m_SceneMeshSources[meshIndex];
                    const bool bMeshChanged = UpdateShadowMeshState(source.Handle, *source.GpuData, bResidencyFresh);
                    m_SceneMeshShadowChanged[meshIndex] = bMeshChanged ? 1u : 0u;
                    m_ShadowDirtyStats.MeshChanges += bMeshChanged ? 1u : 0u;
                }
                for (const size_t instanceIndex : m_SceneInstanceOrder)
                {
                    const uint32_t row = m_SceneInstanceRows[instanceIndex];
                    const bool bMeshChanged =
                        row != NoSceneRow && m_SceneMeshShadowChanged[tables.Entries[row].MeshIndex] != 0u;
                    const ShadowInstanceRecord &now = m_SceneComposeRecords[instanceIndex];
                    if (!bShadowRebuild)
                    {
                        NoteShadowRecord(instanceIndex, now, bMeshChanged);
                        continue;
                    }
                    const MegaMeshInstance &instance = m_Instances[instanceIndex];
                    const uint64_t objectKey =
                        instance.ObjectId != 0 ? instance.ObjectId : (0x8000000000000000ull | instanceIndex);
                    const auto found = previousShadowPositions.find(MixShadowInstanceKey(objectKey, instance.ComponentId));
                    // 前の並びに無かった物は、投影物でなかったものとして比べる
                    ShadowInstanceRecord before;
                    bool bSame = !now.bCaster;
                    if (found != previousShadowPositions.end())
                    {
                        const uint32_t previousIndex = found->second;
                        previousShadowSeen[previousIndex] = 1u;
                        before = m_ShadowPreviousRecords[previousIndex];
                        const MegaMeshInstance &previous = m_ShadowPreviousInstances[previousIndex];
                        bSame = !bMeshChanged && before.bCaster == now.bCaster &&
                                (!now.bCaster ||
                                 (before.bHasBounds == now.bHasBounds &&
                                  std::memcmp(before.Sphere, now.Sphere, sizeof(now.Sphere)) == 0 &&
                                  previous.Handle.Id == instance.Handle.Id &&
                                  std::memcmp(previous.WorldMatrix, instance.WorldMatrix, sizeof(instance.WorldMatrix)) == 0));
                    }
                    if (!bSame)
                    {
                        PushShadowInvalidation(before, now);
                    }
                    m_ShadowRecords[instanceIndex] = now;
                }
            }

            // ----- VSM のキャッシュの無効化の後始末 -----
            if (bShadowRebuild)
            {
                // 前の並びにあって今の並びに無い投影物は、前の境界を描き直す
                for (size_t index = 0; index < m_ShadowPreviousRecords.size(); ++index)
                {
                    if (index >= previousShadowSeen.size() || previousShadowSeen[index] == 0u)
                    {
                        PushShadowInvalidation(m_ShadowPreviousRecords[index], ShadowInstanceRecord{});
                    }
                }
                m_ShadowPreviousInstances.clear();
                m_ShadowPreviousRecords.clear();
                m_bShadowRebuildPending = false;
            }
            else
            {
                for (const uint32_t position : m_ShadowDirtyPositions)
                {
                    if (position < m_ShadowDirtyMarks.size())
                    {
                        m_ShadowDirtyMarks[position] = 0u;
                    }
                }
            }
            m_ShadowDirtyPositions.clear();
            // このフレームに載らなかったメッシュは記録から外す（次に載ったときに変わったものとして扱う）
            for (auto iterator = m_ShadowMeshes.begin(); iterator != m_ShadowMeshes.end();)
            {
                iterator = iterator->second.Stamp != m_ShadowMeshStamp ? m_ShadowMeshes.erase(iterator) : std::next(iterator);
            }
            // 120 回ごとに、積んだ無効化の内訳を出す
            ShadowDirtyStats &stats = m_ShadowDirtyStats;
            if (!stats.bLoggedOnce || stats.Builds >= ShadowDirtyLogInterval)
            {
                NORVES_LOG_INFO("MegaGeometryPass",
                                "MEGA_SHADOW_DIRTY builds=%u rebuilds=%u dirty_slots=%llu mesh_changes=%llu residency_changes=%llu "
                                "boxes=%llu invalidate_all=%u instances=%u",
                                stats.Builds, stats.Rebuilds, static_cast<unsigned long long>(stats.DirtySlots),
                                static_cast<unsigned long long>(stats.MeshChanges),
                                static_cast<unsigned long long>(stats.ResidencyChanges), static_cast<unsigned long long>(stats.Boxes),
                                stats.InvalidateAll, static_cast<uint32_t>(m_Instances.size()));
                stats = ShadowDirtyStats{};
                stats.bLoggedOnce = true;
            }
        }
#if NORVES_BUILD_DEVELOPMENT
        CheckSceneTables(command, tables);
#endif
    }

    void MegaGeometryPass::CheckSceneTables(const MegaGeometryPassCommand &command, const SceneTables &tables)
    {
        SceneTablesStats &stats = m_SceneTablesStats;
        if (++stats.SinceCheck < SceneTablesCheckInterval || &tables != &m_SceneTables)
        {
            return;
        }
        NORVES_PROFILE_SCOPE("MegaGeometryPass.CheckSceneTables");
        stats.SinceCheck = 0;
        // 全部を作り直した表（何も変えずに作る）と、今の表・影の記録を行ごとに比べる。記録が決める区間の範囲（Capacity・CommandBase）は
        // 表の中身ではないので比べない
        ComposeSceneTables(command, m_SceneTablesCheck, false, m_SceneTablesCheckRecords);
        const SceneTables &expected = m_SceneTablesCheck;
        uint32_t mismatchedRows = 0;
        const char *firstTable = nullptr;
        size_t firstRow = 0;
        const auto note = [&](const char *table, size_t row) -> void
        {
            if (mismatchedRows++ == 0)
            {
                firstTable = table;
                firstRow = row;
            }
        };
        if (tables.TotalGroups != expected.TotalGroups || tables.ShadowTotalGroups != expected.ShadowTotalGroups ||
            tables.ShadowCasterCount != expected.ShadowCasterCount || tables.BvhInstanceCount != expected.BvhInstanceCount ||
            tables.BvhLeafCapacity != expected.BvhLeafCapacity || tables.BvhLevelCapacity.size() != expected.BvhLevelCapacity.size() ||
            !std::equal(tables.BvhLevelCapacity.begin(), tables.BvhLevelCapacity.end(), expected.BvhLevelCapacity.begin(),
                        expected.BvhLevelCapacity.end()))
        {
            note("Totals", 0);
        }
        if (tables.Sections.size() != expected.Sections.size())
        {
            note("Sections", tables.Sections.size());
        }
        for (size_t row = 0; row < std::min(tables.Sections.size(), expected.Sections.size()); ++row)
        {
            if (tables.Sections[row].Representative != expected.Sections[row].Representative ||
                tables.Sections[row].ClusterTotal != expected.Sections[row].ClusterTotal)
            {
                note("Sections", row);
            }
        }
        if (tables.Meshes.size() != expected.Meshes.size() || tables.MeshOwners.size() != expected.MeshOwners.size())
        {
            note("Meshes", tables.Meshes.size());
        }
        for (size_t row = 0; row < std::min(tables.Meshes.size(), expected.Meshes.size()); ++row)
        {
            if (std::memcmp(&tables.Meshes[row], &expected.Meshes[row], sizeof(GPUMegaMesh)) != 0)
            {
                note("Meshes", row);
            }
        }
        for (size_t row = 0; row < std::min(tables.MeshOwners.size(), expected.MeshOwners.size()); ++row)
        {
            if (tables.MeshOwners[row].get() != expected.MeshOwners[row].get())
            {
                note("MeshOwners", row);
            }
        }
        const size_t rowCount = tables.Entries.size();
        if (rowCount != expected.Entries.size() || tables.Drawables.size() != rowCount ||
            expected.Drawables.size() != rowCount || tables.VisibilityRequests.size() != rowCount ||
            expected.VisibilityRequests.size() != rowCount || tables.ShadowInstances.size() != rowCount ||
            expected.ShadowInstances.size() != rowCount)
        {
            note("Rows", rowCount);
        }
        else
        {
            for (size_t row = 0; row < rowCount; ++row)
            {
                const GPUMegaEntry &entry = tables.Entries[row];
                const GPUMegaEntry &expectedEntry = expected.Entries[row];
                const VisibilityRequest &request = tables.VisibilityRequests[row];
                const VisibilityRequest &expectedRequest = expected.VisibilityRequests[row];
                if (tables.Drawables[row].InstanceIndex != expected.Drawables[row].InstanceIndex ||
                    tables.Drawables[row].GpuData != expected.Drawables[row].GpuData)
                {
                    note("Drawables", row);
                }
                else if (entry.Slot != expectedEntry.Slot || entry.MeshIndex != expectedEntry.MeshIndex ||
                         entry.FirstGroup != expectedEntry.FirstGroup || entry.VisibleOffset != expectedEntry.VisibleOffset)
                {
                    note("Entries", row);
                }
                else if (request.Key != expectedRequest.Key || request.MeshId != expectedRequest.MeshId ||
                         request.ComponentId != expectedRequest.ComponentId ||
                         request.ClusterBufferIdentity != expectedRequest.ClusterBufferIdentity ||
                         request.ClusterBufferOffsetBytes != expectedRequest.ClusterBufferOffsetBytes ||
                         request.ClusterCount != expectedRequest.ClusterCount || request.TableSlot != expectedRequest.TableSlot ||
                         request.Offset != expectedRequest.Offset)
                {
                    note("VisibilityRequests", row);
                }
                else if (std::memcmp(&tables.ShadowInstances[row], &expected.ShadowInstances[row],
                                     sizeof(MegaGeometryShadowInstance)) != 0)
                {
                    note("ShadowInstances", row);
                }
            }
        }
        // 影の記録（VSM の無効化が前と比べる値）も、全部を作ったときと同じであること
        if (m_ShadowRecords.size() != m_SceneTablesCheckRecords.size())
        {
            note("ShadowRecords", m_ShadowRecords.size());
        }
        else
        {
            for (size_t index = 0; index < m_ShadowRecords.size(); ++index)
            {
                const ShadowInstanceRecord &record = m_ShadowRecords[index];
                const ShadowInstanceRecord &expectedRecord = m_SceneTablesCheckRecords[index];
                if (record.bCaster != expectedRecord.bCaster || record.bHasBounds != expectedRecord.bHasBounds ||
                    std::memcmp(record.Sphere, expectedRecord.Sphere, sizeof(record.Sphere)) != 0)
                {
                    note("ShadowRecords", index);
                }
            }
        }

        // 照合の表がメッシュの区画の持ち主を持ち続けないようにする
        m_SceneTablesCheck.MeshOwners.clear();
        ++stats.Checks;
        if (mismatchedRows > 0)
        {
            ++stats.Mismatches;
            // 食い違った表は使い続けない（次の表は全部を作る）
            m_bSceneTablesValid = false;
            NORVES_LOG_ERROR("MegaGeometryPass",
                             "MEGA_TABLES_MISMATCH table=%s row=%zu rows=%u 差分で直した表が、全部を作り直した表と一致しません",
                             firstTable, firstRow, mismatchedRows);
        }
        NORVES_LOG_INFO("MegaGeometryPass",
                        "MEGA_TABLES mismatch=%u checks=%u full=%u delta=%u delta_rows=%llu rows=%zu instances=%zu",
                        stats.Mismatches, stats.Checks, stats.FullBuilds, stats.DeltaBuilds,
                        static_cast<unsigned long long>(stats.DeltaRows), tables.Entries.size(), m_Instances.size());
        stats.FullBuilds = 0;
        stats.DeltaBuilds = 0;
        stats.DeltaRows = 0;
    }

    bool MegaGeometryPass::ShadowMeshState::operator==(const ShadowMeshState &other) const
    {
        return VertexBuffer == other.VertexBuffer && IndexBuffer == other.IndexBuffer && ClusterBuffer == other.ClusterBuffer &&
               VertexBufferOffsetBytes == other.VertexBufferOffsetBytes &&
               IndexBufferOffsetBytes == other.IndexBufferOffsetBytes &&
               ClusterBufferOffsetBytes == other.ClusterBufferOffsetBytes && ClusterCount == other.ClusterCount &&
               PageTableBase == other.PageTableBase && PageCount == other.PageCount &&
               std::memcmp(TotalBounds, other.TotalBounds, sizeof(TotalBounds)) == 0 && Residency == other.Residency;
    }

    bool MegaGeometryPass::UpdateShadowMeshState(MegaGeometry::MegaMeshHandle handle,
                                                 const MegaGeometry::MegaMeshGPUData &data,
                                                 bool bResidencyFresh)
    {
        ShadowMeshState state;
        state.VertexBuffer = data.VertexBuffer.get();
        state.IndexBuffer = data.IndexBuffer.get();
        state.ClusterBuffer = data.ClusterBuffer.get();
        state.VertexBufferOffsetBytes = data.VertexBufferOffsetBytes;
        state.IndexBufferOffsetBytes = data.IndexBufferOffsetBytes;
        state.ClusterBufferOffsetBytes = data.ClusterBufferOffsetBytes;
        state.ClusterCount = data.ClusterCount;
        state.PageTableBase = data.PageTableBase;
        state.PageCount = data.PageCount;
        state.TotalBounds[0] = data.TotalBounds.CenterX;
        state.TotalBounds[1] = data.TotalBounds.CenterY;
        state.TotalBounds[2] = data.TotalBounds.CenterZ;
        state.TotalBounds[3] = data.TotalBounds.Radius;
        state.Stamp = m_ShadowMeshStamp;

        const auto found = m_ShadowMeshes.find(handle.Id);
        const bool bKnown = found != m_ShadowMeshes.end();
        // 常駐の署名はページを読み込むメッシュだけが持つ（全部が常駐のままのメッシュは変わらない）。
        // ページの表の写しが変わったフレームと、初めて載せるメッシュだけ作り直す
        if (data.bPagesStreamed)
        {
            state.Residency = (bResidencyFresh || !bKnown)
                                  ? HashPageResidency(m_ShadowPageEntries, data.PageTableBase, data.PageCount)
                                  : found->second.Residency;
        }
        if (!bKnown)
        {
            m_ShadowMeshes.emplace(handle.Id, state);
            return true;
        }
        const bool bChanged = !(found->second == state);
        if (bChanged && found->second.Residency != state.Residency)
        {
            ShadowMeshState sameResidency = state;
            sameResidency.Residency = found->second.Residency;
            m_ShadowDirtyStats.ResidencyChanges += found->second == sameResidency ? 1u : 0u;
        }
        found->second = state;
        return bChanged;
    }

    void MegaGeometryPass::PushShadowInvalidation(const ShadowInstanceRecord &before, const ShadowInstanceRecord &after)
    {
        if (!before.bCaster && !after.bCaster)
        {
            return;
        }
        MegaGeometryShadowInvalidation &invalidation = m_ShadowInvalidation;
        if (invalidation.bInvalidateAll)
        {
            return;
        }
        // 境界の分からない投影物が変わった・積んだ数が上限に達した: 範囲を絞れないので全ページを描き直す
        if ((before.bCaster && !before.bHasBounds) || (after.bCaster && !after.bHasBounds) ||
            invalidation.Boxes.size() >= ShadowInvalidationMaxBoxes)
        {
            invalidation.Boxes.clear();
            invalidation.bInvalidateAll = true;
            ++m_ShadowDirtyStats.InvalidateAll;
            return;
        }
        // 前の境界と今の境界を合わせた箱を 1 つ積む（動いた物は元の場所と今の場所の両方の影を描き直す）
        MegaGeometryShadowBox box;
        bool bHasBox = false;
        for (const ShadowInstanceRecord *record : {&before, &after})
        {
            if (!record->bCaster)
            {
                continue;
            }
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                const float low = record->Sphere[axis] - record->Sphere[3];
                const float high = record->Sphere[axis] + record->Sphere[3];
                box.Min[axis] = bHasBox ? std::min(box.Min[axis], low) : low;
                box.Max[axis] = bHasBox ? std::max(box.Max[axis], high) : high;
            }
            bHasBox = true;
        }
        invalidation.Boxes.push_back(box);
        ++m_ShadowDirtyStats.Boxes;
    }

    void MegaGeometryPass::BeginShadowRebuild(bool bKeepInstances)
    {
        // 表を作る前に何度作り直しても、比べる元は最後に表を作ったときの並び
        if (m_bShadowRebuildPending)
        {
            return;
        }
        m_bShadowRebuildPending = true;
        if (bKeepInstances)
        {
            m_ShadowPreviousInstances = m_Instances;
        }
        else
        {
            m_ShadowPreviousInstances.clear();
            m_ShadowPreviousInstances.swap(m_Instances);
        }
        m_ShadowPreviousRecords.clear();
        m_ShadowPreviousRecords.swap(m_ShadowRecords);
        // 記録の無い位置（まだ表を作っていない並び）は投影物でないものとして比べる
        m_ShadowPreviousRecords.resize(m_ShadowPreviousInstances.size());
        m_ShadowDirtyPositions.clear();
    }

    void MegaGeometryPass::NoteShadowPatched(size_t position, const MegaMeshInstance &before, const MegaMeshInstance &after)
    {
        // 作り直しを待つ間は、次の表で全部を鍵で比べる
        if (m_bShadowRebuildPending)
        {
            return;
        }
        const bool bChanged = before.Handle.Id != after.Handle.Id || before.bCastShadow != after.bCastShadow ||
                              std::memcmp(before.WorldMatrix, after.WorldMatrix, sizeof(after.WorldMatrix)) != 0 ||
                              std::memcmp(&before.WorldBounds, &after.WorldBounds, sizeof(after.WorldBounds)) != 0;
        if (!bChanged)
        {
            return;
        }
        if (position >= m_ShadowDirtyMarks.size())
        {
            m_ShadowDirtyMarks.resize(m_Instances.size(), 0u);
        }
        if (position < m_ShadowDirtyMarks.size() && m_ShadowDirtyMarks[position] == 0u)
        {
            m_ShadowDirtyMarks[position] = 1u;
            m_ShadowDirtyPositions.push_back(static_cast<uint32_t>(position));
        }
    }

    void MegaGeometryPass::FlushShadowRebuildWithoutInstances()
    {
        if (!m_bShadowRebuildPending || !m_Instances.empty())
        {
            return;
        }
        for (const ShadowInstanceRecord &before : m_ShadowPreviousRecords)
        {
            PushShadowInvalidation(before, ShadowInstanceRecord{});
        }
        m_ShadowPreviousInstances.clear();
        m_ShadowPreviousRecords.clear();
        m_ShadowRecords.clear();
        m_ShadowDirtyMarks.clear();
        m_ShadowDirtyPositions.clear();
        m_bShadowRebuildPending = false;
    }

    void MegaGeometryPass::TakeShadowInvalidation(MegaGeometryShadowInvalidation &out)
    {
        out.Boxes.clear();
        out.Boxes.swap(m_ShadowInvalidation.Boxes);
        out.bInvalidateAll = m_ShadowInvalidation.bInvalidateAll;
        m_ShadowInvalidation.bInvalidateAll = false;
    }

    void MegaGeometryPass::PublishShadowCasterInputs(const FrameSlot &slot, SceneTables &tables)
    {
        // 常駐の表（描く順の表・影の表・メッシュの表）と、このフレームのページの表を VSM の投影物のカリングへ渡す。
        // 影を落とすインスタンスが無い・ワークグループの数が 32 ビットに収まらないときは入力を作らない
        if (m_ShadowTableBuffer && tables.ShadowCasterCount > 0 && tables.ShadowTotalGroups <= 0xFFFFFFFFull)
        {
            m_ShadowCasterInputs.bValid = true;
            m_ShadowCasterInputs.InstanceBuffer = m_InstanceTableBuffer;
            m_ShadowCasterInputs.ShadowInstanceBuffer = m_ShadowTableBuffer;
            m_ShadowCasterInputs.MeshBuffer = m_MeshTableBuffer;
            m_ShadowCasterInputs.PageTableBuffer = slot.PageTableBuffer;
            m_ShadowCasterInputs.InstanceCount = static_cast<uint32_t>(tables.Entries.size());
            m_ShadowCasterInputs.CasterCount = tables.ShadowCasterCount;
            m_ShadowCasterInputs.TotalGroups = static_cast<uint32_t>(tables.ShadowTotalGroups);
        }
    }

    void MegaGeometryPass::ReleaseIdleMeshRegionOwners(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        if (!m_bFrameSlotMeshOwnersHeld || frameSerial == 0 || inFlightIndex >= FrameUseRingMaxInFlightSlots)
        {
            return;
        }
        // 枠を選ぶと、同じ番号の前のフレーム（GPU の仕事が終わっている）が使った組だけが渡される。
        // インスタンスの無いフレームが飛行中のフレームの数だけ続けば、どの組もメッシュの区画を持たなくなる
        // 前のフレームの表の持ち主は CPU の一覧で、GPU が読むのは組の持ち主なので、すぐ手放してよい
        m_SceneTables.MeshOwners.clear();
        m_bSceneTablesValid = false;
        m_FrameSlots.BeginFrame(inFlightIndex, frameSerial);
        m_FrameSlots.ForEachIdleInActiveSlot([](FrameSlot &slot) { slot.MeshRegionOwners.clear(); });
    }

    void MegaGeometryPass::PrepareSceneTables(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList)
    {
        NORVES_PROFILE_SCOPE("MegaGeometryPass.PrepareSceneTables");
        m_PreparedSceneTables = PreparedSceneTables{};
        // 通し番号が無いコマンドは記録ごとに別のフレームとして扱うので、前段の表を記録へ渡せない（記録が自分で作る）
        if (m_Instances.empty() || !m_CullPipeline || !command.MegaGeometry || command.RenderFrameSerial == 0 ||
            !commandList)
        {
            return;
        }

        if (m_bSceneSlotsDirty)
        {
            AssignSceneTableSlots();
        }
        BuildSceneTables(command, m_SceneTables);
        if (m_SceneTables.Entries.empty() || m_SceneTables.TotalGroups > 0xFFFFFFFFull)
        {
            return;
        }

        // このフレームの資源の組を 1 つ取り、同じフレームの記録へ渡す（記録は同じ枠・同じ通し番号なら取り直さない）。
        // ページの表の版はここで決め、同じフレームの記録（主の描画・ID のラスタ）と VSM が同じ版を使う
        m_FrameSlots.BeginFrame(command.InFlightIndex, command.RenderFrameSerial);
        FrameSlot &slot = m_FrameSlots.Acquire();
        uint64_t pageTableBytes = 0;
        if (!SyncPageTable(slot, *command.MegaGeometry, &pageTableBytes) ||
            !UpdateResidentSceneTables(slot, m_SceneTables, commandList, pageTableBytes))
        {
            // 記録が自分で作り直し、同じ失敗ならそこで記録する
            return;
        }
        PublishShadowCasterInputs(slot, m_SceneTables);

        m_PreparedSceneTables.bPrepared = true;
        m_PreparedSceneTables.FrameSerial = command.RenderFrameSerial;
        m_PreparedSceneTables.InFlightIndex = command.InFlightIndex;
        m_PreparedSceneTables.Slot = &slot;
    }

    void MegaGeometryPass::RecordFrameCommand(const MegaGeometryPassCommand &command,
                                              RHI::ICommandList *commandList,
                                              IDrawSink *sink)
    {
        // CPU の記録時間（trace の Type=Scope 行）と、記録した区間の GPU 時間（Type=GPU 行 "MegaGeometry"）
        NORVES_PROFILE_SCOPE("MegaGeometryPass.RecordFrameCommand");
        ScopedGpuTimestamp gpuTimestamp(commandList, "MegaGeometry");

        // 前のフレームの描画の写しのために残したバッファが戻されていなければ戻し、写しも捨てる
        // （ビジビリティバッファのラスタが取り出さなかったフレームの後始末）
        m_VisibilityPlan = VisibilityDrawPlan{};
        ReleaseVisibilityDrawBuffers(commandList);
        m_bSwRasterBinned = false;
        m_SwRasterCapacity = 0;
        // 同じフレーム・同じ枠で前段が表を書いていれば、その表とフレームスロットを使う（1 回だけ。同じフレームの
        // 次のビューポートは自分で作る）。使わない記録は、VSM の入力を自分が表を書けたときだけ有効にする
        const bool bPreparedTables = m_PreparedSceneTables.bPrepared && command.RenderFrameSerial != 0 &&
                                     m_PreparedSceneTables.FrameSerial == command.RenderFrameSerial &&
                                     m_PreparedSceneTables.InFlightIndex == command.InFlightIndex &&
                                     m_PreparedSceneTables.Slot != nullptr;
        FrameSlot *preparedSlot = bPreparedTables ? m_PreparedSceneTables.Slot : nullptr;
        m_PreparedSceneTables = PreparedSceneTables{};
        if (!bPreparedTables)
        {
            m_ShadowCasterInputs = MegaGeometryShadowCasterInputs{};
        }

        if (m_Instances.empty() || !m_CullPipeline || !commandList || !command.MegaGeometry || !command.bHasMainCamera)
        {
            return;
        }

        bool bDrawPipelinesReady = m_DrawPipeline != nullptr;
#if NORVES_BUILD_DEVELOPMENT
        bDrawPipelinesReady = bDrawPipelinesReady && m_DrawWireframePipeline != nullptr;
#endif

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !bDrawPipelinesReady)
        {
            return;
        }

        auto *cmdList = commandList;

        auto recordEmptyRenderPass = [this, &command, cmdList, sink]() -> void
        {
            if (sink)
            {
                // GBuffer の添付の状態遷移は、呼び出し側が済ませてある
                return;
            }
            cmdList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);
            cmdList->SetViewport(command.Viewport);
            cmdList->SetScissor(command.Scissor);
            cmdList->EndRenderPass();
        };

        using namespace NorvesLib::Math;

        const auto &cam = command.MainCamera;
        const float aspectRatio = m_CurrentHeight > 0
                                      ? static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight)
                                      : 1.0f;
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(cam, aspectRatio, m_Device);
        // velocity は前のカメラが無ければ0（GBufferPass と同じ）。
        const CameraViewConstants previousCameraConstants = CameraViewConstants::BuildForDevice(
            command.bHasPreviousCamera ? command.PreviousCamera : cam, aspectRatio, m_Device);
        const auto &caps = m_Device->GetCapabilities();
        const bool bMegaGeometryDebugPayloadRequested =
            IsMegaGeometryDebugPayloadMode(command.DebugMode);
        const bool bMegaGeometryDebugPayloadSupported =
            bMegaGeometryDebugPayloadRequested &&
            caps.bDrawIndirectFirstInstance;
        // デバッグ表示でなければ、描いているクラスタのLODの段を渡す（変位した材質が段の頂点の間隔を知るため）。
        const bool bLODLevelPayload = !bMegaGeometryDebugPayloadRequested && caps.bDrawIndirectFirstInstance;
        const uint32_t debugPayloadMode =
            bLODLevelPayload ? DEBUG_PAYLOAD_MODE_LOD_LEVEL
                             : GetMegaGeometryDebugPayloadMode(command.DebugMode, bMegaGeometryDebugPayloadSupported);

        if (bMegaGeometryDebugPayloadRequested &&
            !caps.bDrawIndirectFirstInstance &&
            !m_bMegaGeometryDebugPayloadUnsupportedWarned)
        {
            NORVES_LOG_WARNING("MegaGeometryPass",
                               "%s のデバッグ表示は DrawIndirectFirstInstance が要るため、"
                               "通常の MegaGeometry の陰影で描きます",
                               DebugViewModeToString(command.DebugMode));
            m_bMegaGeometryDebugPayloadUnsupportedWarned = true;
        }

        const ClipSpaceFrustumPlanes frustumPlanes =
            MatrixUtils::ExtractClipSpaceFrustumPlanes(cameraConstants.ViewProjectionMatrix,
                                                       ClipSpaceDepthRange::ZeroToOne);

        // ========================================
        // 遮蔽カリングの経路を決める
        // ========================================
        // 2パス: 1パス目（前のフレームで見えたクラスタ）→ HZB（その時点の深度から）→ 2パス目（残りを遮蔽の判定で選ぶ）。
        // 使えないとき（--mega-occlusion=off・HZBが作れない・描く範囲が深度と一致しない）は、
        // 従来の1回の判定（遮蔽の判定なし）で描く。
        bool bTwoPass = CanUseTwoPassOcclusion(command);
        // 2パスでないフレームも数える（見えたビットは連続した2パスのフレームの間でだけ引き継ぐため）
        ++m_OcclusionFrameCount;
        // フレームごとの資源の組を選ぶ。1フレームの複数のビューポートは同じ通し番号なので、組が別々に渡る。
        // 通し番号 0（FrameCommand::CreateMegaGeometryPass で直接組んだコマンドなど）はフレームの境目が分からず、そのままだと
        // 組が記録ごとに増え続け、退避したバッファの寿命も進まず解放されない。記録の回数を代わりの番号にして、
        // 記録ごとに別のフレームとして扱う（1 フレームに複数回記録する呼び出し側は、通し番号を渡すこと）
        uint64_t frameSerial = command.RenderFrameSerial;
        if (frameSerial == 0)
        {
            if (!m_bUnsetFrameSerialWarned)
            {
                m_bUnsetFrameSerialWarned = true;
                NORVES_LOG_WARNING("MegaGeometryPass", "FRAME_SERIAL_UNSET "
                                                       "フレームの通し番号が無いコマンドです。記録ごとに"
                                                       "別のフレームとして扱います");
            }
            frameSerial = m_OcclusionFrameCount;
        }
        if (m_RenderFrameCount == 0 || frameSerial != m_LastRenderFrameSerial)
        {
            ++m_RenderFrameCount;
            m_LastRenderFrameSerial = frameSerial;
        }
        m_FrameSlots.BeginFrame(command.InFlightIndex, frameSerial);
        // 決定的な撮影のエポック（読み込み完了）の最初のフレームから、統計の行に相対フレームの番号を付ける
        // 見えたビットも時間的な状態なので、エポックの最初のフレームは引き継がずに0から始める（読み込み完了までの
        // フレーム数の違いが、エポックの最初のフレームの1パス目の数に出ないようにする）
        m_bDropVisibilityContinuity = command.bDeterministicCapture && command.bTemporalEpochStart;
        if (m_bDropVisibilityContinuity)
        {
            m_bStatsEpochActive = true;
        }
        const int64_t epochFrame =
            (command.bDeterministicCapture && m_bStatsEpochActive) ? static_cast<int64_t>(command.TemporalFrameIndex) : -1;

        // ========================================
        // インスタンスの表・メッシュの表・影の表（前段が書いたフレームはそれを使い、書いていなければここで作る）
        // ========================================
        using Section = SceneSection;
        using Drawable = SceneDrawable;
        SceneTables &tables = m_SceneTables;
        if (!bPreparedTables)
        {
            if (m_bSceneSlotsDirty)
            {
                AssignSceneTableSlots();
            }
            BuildSceneTables(command, tables);
        }
        VariableArray<Section> &sections = tables.Sections;
        const VariableArray<Drawable> &drawables = tables.Drawables;
        const VariableArray<GPUMegaEntry> &instanceTable = tables.Entries;
        VariableArray<VisibilityRequest> &visibilityRequests = tables.VisibilityRequests;
        const uint64_t totalGroups = tables.TotalGroups;
        const uint32_t bvhInstanceCount = tables.BvhInstanceCount;
        const VariableArray<uint64_t> &bvhLevelCapacity = tables.BvhLevelCapacity;
        const uint64_t bvhLeafCapacity = tables.BvhLeafCapacity;

        if (instanceTable.empty() || totalGroups > 0xFFFFFFFFull)
        {
            recordEmptyRenderPass();
            return;
        }

        // BVH のたどりの列の配置（容量は固定）: 節の列 0・節の列 1・葉の列。段 0（席）の入力は、範囲の段が節の列 0 へ積んだ
        // 範囲の一覧か 2 パス目の根の入力。段 s（1以上）は節の列 s & 1 を読み、次の段の列 (s + 1) & 1 へ書く
        const uint32_t bvhLevelCount = static_cast<uint32_t>(bvhLevelCapacity.size());
        const auto bvhNodeQueueBase = [](uint32_t stage) -> uint32_t { return (stage & 1u) * BvhNodeQueueCapacity; };
        constexpr uint32_t bvhLeafQueueBase = 2u * BvhNodeQueueCapacity;
        if (bvhInstanceCount != m_LoggedBvhInstances || bvhLevelCount != m_LoggedBvhLevels ||
            static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount != m_LoggedFlatInstances)
        {
            m_LoggedBvhInstances = bvhInstanceCount;
            m_LoggedBvhLevels = bvhLevelCount;
            m_LoggedFlatInstances = static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount;
            // scene_leaves は全インスタンスの葉の合計（列の容量ではない）。列の容量は本数によらず固定
            NORVES_LOG_INFO("MegaGeometryPass",
                            "MEGA_BVH bvh_instances=%u flat_instances=%u levels=%u "
                            "scene_leaves=%llu node_queue_capacity=%u leaf_queue_capacity=%u pass2_capacity=%u "
                            "flat_groups=%llu dispatches_per_pass=%u instance_ranges=%u",
                            bvhInstanceCount,
                            static_cast<uint32_t>(instanceTable.size()) - bvhInstanceCount,
                            bvhLevelCount,
                            static_cast<unsigned long long>(bvhLeafCapacity),
                            BvhNodeQueueCapacity,
                            BvhLeafQueueCapacity,
                            BvhPass2Capacity,
                            static_cast<unsigned long long>(totalGroups),
                            (totalGroups > 0 ? 1u : 0u) + (bvhInstanceCount > 0 ? bvhLevelCount + 2u : 0u),
                            (bvhInstanceCount + BvhRangeSeats - 1u) / BvhRangeSeats);
        }

        // 「前のフレームで見えた」ビットの配置（2パスのときだけ）。作れなければ従来の1回の判定で描く
        if (bTwoPass)
        {
            // 区画はインスタンスの並びで詰めるので、表を作るときに書いた先頭（GPUMegaInstance::VisibleOffset）と同じ
            if (!UpdateVisibilityLayout(cmdList, visibilityRequests))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "見えたビットのバッファを作れませんでした。遮蔽カリ"
                                                     "ングを使わずに描きます");
                bTwoPass = false;
            }
        }

        // ソフトウェアラスタの振り分けを使えるか。要求されたときだけ判定し、使えない理由は 1 回だけログへ出す。
        // 64bit のバッファ（深度 + ID）への書き込みが 64bit アトミックを使い、合流が ID のラスタ（sink）の後ろにあるので、
        // どちらかが無ければ振り分けない。振り分けた一覧はパスごとなので、2 パスの遮蔽の判定のときだけ使う
        bool bSwRasterBin = false;
        if (m_bSwRasterRequested)
        {
            const char *fallbackReason = nullptr;
            if (!sink)
            {
                // sink が無い理由: 材質の解決を使う On（GBuffer の描画を止める）なのに ID のラスタが使えないフレームは resolve_unavailable、
                // Debug（GBuffer が先に MegaGeometry を描く）は debug_mode、描画の写しを作らない構成は visibility_buffer_off
                fallbackReason = m_bSkipGBufferDraw ? "resolve_unavailable" : (m_bVisibilityPlanEnabled ? "debug_mode" : "visibility_buffer_off");
            }
            else if (!VisibilityMerge::IsSupported(caps))
            {
                fallbackReason = "int64_atomics_unsupported";
            }
            else if (!bTwoPass)
            {
                fallbackReason = "occlusion_off";
            }
            if (fallbackReason)
            {
                if (!m_bSwRasterFallbackLogged)
                {
                    NORVES_LOG_INFO("MegaGeometryPass", "SW_RASTER_FALLBACK reason=%s", fallbackReason);
                    m_bSwRasterFallbackLogged = true;
                }
            }
            else
            {
                bSwRasterBin = true;
            }
        }

        // 統計（MEGA_OCCLUSION）を取るか。読み戻しのあるビルド（開発）で、フレームの持ち主があるときだけ。
        // 読み戻し先は、描く直前にフレームの持ち主から借りる（提出の完了の後に OnFrameReadback で受け取ってログへ出す）
        bool bWantStats = false;
#if NORVES_ENABLE_STATS
        bWantStats = bTwoPass && command.FrameResources != nullptr;
#endif

        // ========================================
        // 区間のコマンドの範囲と、GPUバッファ・フレームスロットの用意
        // ========================================
        const uint32_t sectionCount = static_cast<uint32_t>(sections.size());
        const uint32_t passCount = bTwoPass ? 2u : 1u;
        uint64_t commandsPerPass = 0;
        for (Section &section : sections)
        {
            section.Capacity = static_cast<uint32_t>(std::min<uint64_t>(section.ClusterTotal, m_Settings.MaxDrawCount));
            section.CommandBase = static_cast<uint32_t>(commandsPerPass);
            commandsPerPass += section.Capacity;
        }
        const uint64_t commandsTotal = commandsPerPass * passCount;
        if (commandsTotal == 0 || commandsTotal > 0x7FFFFFFFull / IndirectCommandBytes)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_BATCH_COMMAND_COUNT_INVALID commands=%llu", static_cast<unsigned long long>(commandsTotal));
            recordEmptyRenderPass();
            return;
        }

        FrameSlot &frameSlot = preparedSlot ? *preparedSlot : m_FrameSlots.Acquire();
        if (!EnsureBatchBuffers(static_cast<uint32_t>(commandsTotal), sectionCount * passCount) ||
            !EnsureFrameSlot(frameSlot, sectionCount * passCount, sectionCount) ||
            (bvhInstanceCount > 0 &&
             (!EnsureBvhStageResources(frameSlot, 0, bvhLevelCount + 2u) ||
              (bTwoPass && !EnsureBvhStageResources(frameSlot, 1, bvhLevelCount + 2u)))))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "まとめた描画の資源を用意できませんでした");
            recordEmptyRenderPass();
            return;
        }
        // BVH の固定容量の列。作れなければ（VRAM の不足など）このフレームの描画を見送る（ERROR は作れなくなった最初の 1 回だけ）
        if (bvhInstanceCount > 0 && !EnsureBvhQueueBuffers())
        {
            recordEmptyRenderPass();
            return;
        }

        // ソフトウェアラスタの一覧。作れなければ振り分けずにハードだけで描く
        const uint32_t swRasterCapacity = static_cast<uint32_t>(commandsPerPass);
        if (bSwRasterBin && !EnsureSwRasterBuffer(swRasterCapacity))
        {
            if (!m_bSwRasterFallbackLogged)
            {
                NORVES_LOG_INFO("MegaGeometryPass", "SW_RASTER_FALLBACK reason=buffer_unavailable");
                m_bSwRasterFallbackLogged = true;
            }
            bSwRasterBin = false;
        }
        m_bSwRasterBinned = bSwRasterBin;
        m_SwRasterCapacity = bSwRasterBin ? swRasterCapacity : 0u;
        // ソフトのラスタが走るフレームだけ、一覧へ積めたクラスタのハードのコマンドを空振りにする（走らないなら積むだけ）。
        // bSwRasterBin は sink と 2 パスの遮蔽の判定を前提にしている
        const bool bSwRasterSkipHardware = bSwRasterBin && sink != nullptr && sink->PrepareSwRaster();

        // ページの表（常駐の状態）をこのフレームのスロットへ写し、常駐の表へ差分を書く（前段が済ませたフレームは、前段の版の
        // ページの表と表をそのまま使う。前段の後に常駐が変わっても、VSM とこの記録が同じ版を読むよう写し直さない）。
        // フレームの間は変わらないので、2パスの判定が食い違わない
        if (!bPreparedTables)
        {
            uint64_t pageTableBytes = 0;
            if (!SyncPageTable(frameSlot, *command.MegaGeometry, &pageTableBytes))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "ページの表のバッファを用意できませんでした");
                recordEmptyRenderPass();
                return;
            }
            if (!UpdateResidentSceneTables(frameSlot, tables, cmdList, pageTableBytes))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "常駐の表へ差分を書けませんでした");
                recordEmptyRenderPass();
                return;
            }
            PublishShadowCasterInputs(frameSlot, tables);
        }

        // ページの要求を書くバッファ（このフレームのもの。獲得できなければ容量 0 で、統計用の代わりのバッファを束ねる）。
        // 要求が指す表の位置を後で引き直せるよう、このフレームのシェーダーが見る表の版を結び付ける
        command.MegaGeometry->SetCurrentPageTableVersion(frameSlot.PageTableVersion);
        RHI::BufferPtr pageRequestBuffer = command.MegaGeometry->GetCurrentPageRequestBuffer();
        const uint32_t pageRequestCapacity = pageRequestBuffer ? command.MegaGeometry->GetCurrentPageRequestCapacity() : 0u;
        if (!pageRequestBuffer || pageRequestCapacity == 0)
        {
            pageRequestBuffer = m_DummyStatsBuffer;
        }

        // 区間の表を書く（ホストが書き、カリングが読む）
        {
            VariableArray<uint32_t> sectionTable;
            sectionTable.reserve(static_cast<size_t>(sectionCount) * passCount * 2u);
            for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
            {
                for (const Section &section : sections)
                {
                    sectionTable.push_back(static_cast<uint32_t>(passIndex * commandsPerPass) + section.CommandBase);
                    sectionTable.push_back(section.Capacity);
                }
            }
            frameSlot.SectionBuffer->Update(sectionTable.data(), sectionTable.size() * sizeof(uint32_t));
        }

        // ========================================
        // 区間ごとの描画入力（定数・テクスチャ）を準備
        // ========================================
        for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
        {
            const auto *gpuData = sections[sectionIndex].Representative;
            const SectionDraw &sectionDraw = frameSlot.Sections[sectionIndex];

            SectionUniformData uniform{};
            cameraConstants.CopyShaderView(uniform.View);
            cameraConstants.CopyShaderProjection(uniform.Projection);
            cameraConstants.CopyCameraPosition(uniform.CameraPosition);
            previousCameraConstants.CopyShaderView(uniform.PreviousView);
            previousCameraConstants.CopyShaderProjection(uniform.PreviousProjection);
            uniform.FrameParams[0] = command.bHasPreviousCamera ? 1.0f : 0.0f;
            // 発光はプリエクスポージャ後の値で GBuffer_Emissive へ書く（GBufferPass・LightingPass と同じ値）。
            uniform.FrameParams[1] = ResolveSceneColorPreExposure(&cam);

            // マテリアル値を設定
            const auto &mat = gpuData->Material;
            uniform.ObjectColor[0] = mat.BaseColor[0];
            uniform.ObjectColor[1] = mat.BaseColor[1];
            uniform.ObjectColor[2] = mat.BaseColor[2];
            uniform.ObjectColor[3] = mat.BaseColor[3];
            uniform.EmissiveChromaticityAndLuminanceNits[0] = mat.EmissiveColor[0];
            uniform.EmissiveChromaticityAndLuminanceNits[1] = mat.EmissiveColor[1];
            uniform.EmissiveChromaticityAndLuminanceNits[2] = mat.EmissiveColor[2];
            uniform.EmissiveChromaticityAndLuminanceNits[3] = mat.EmissiveLuminanceNits;
            uniform.PomParams[0] = mat.HeightScale;
            uniform.PomParams[1] = mat.bHasHeightMap ? 1.0f : 0.0f;
            uniform.PomParams[2] = static_cast<float>(static_cast<uint8_t>(command.DebugMode));
            uniform.PomParams[3] = bMegaGeometryDebugPayloadSupported ? 1.0f : 0.0f;
            uniform.FrameParams[2] = mat.DisplacementUVSpacing > 0.0f ? mat.DisplacementUVSpacing : 0.0f;
            uniform.FrameParams[3] = bLODLevelPayload ? 1.0f : 0.0f;

            // PBRテクスチャ（マテリアルテクスチャまたはデフォルトにフォールバック）
            // 材質はハンドルしか持たないので、描画のたびに引き直す（解放済みならデフォルトに落ちる）。
            auto resolveTexture = [&command](TextureHandle handle, const RHI::TexturePtr &fallback) -> RHI::TexturePtr
            {
                if (command.Textures && handle.IsValid())
                {
                    if (RHI::TexturePtr texture = command.Textures->GetRHITexturePtr(handle))
                    {
                        return texture;
                    }
                }
                return fallback;
            };
            // ORM は metallic の枠に張り、シェーダーへフラグで伝える（descriptor の binding は増やさない）。
            // texture が解決できないときは別々の枠（既定値）の経路へ落とす。
            const RHI::TexturePtr orm = resolveTexture(mat.ORMTexture, nullptr);
            uniform.MaterialParams[0] = orm ? 1.0f : 0.0f;
            uniform.MaterialParams[1] = mat.bNormalTwoChannel ? 1.0f : 0.0f;

            if (!std::isfinite(mat.Metallic) || !std::isfinite(mat.Roughness) || !std::isfinite(mat.OcclusionStrength))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "材質の定数が有限値ではありません");
                continue;
            }
            const auto constant = [&](float value) -> RHI::TexturePtr
            {
                if (!m_ConstantMaterialTextures)
                {
                    m_ConstantMaterialTextures = Container::MakeUnique<ConstantMaterialTextureCache>();
                }
                return m_ConstantMaterialTextures->GetOrCreate(m_Device, value);
            };
            // PBRテクスチャ
            auto albedo = resolveTexture(mat.AlbedoTexture, m_DefaultWhiteTexture);
            auto normal = resolveTexture(mat.NormalTexture, m_DefaultFlatNormalTexture);
            auto metallic = orm ? orm
                                : resolveTexture(mat.MetallicTexture,
                                                 mat.Metallic >= 0 ? constant(mat.Metallic) : m_DefaultBlackTexture);
            auto roughness = orm ? orm
                                 : resolveTexture(mat.RoughnessTexture,
                                                  mat.Roughness >= 0 ? constant(mat.Roughness) : m_DefaultWhiteTexture);
            auto ao = orm ? orm
                          : resolveTexture(mat.AOTexture, mat.OcclusionStrength == 1 ? m_DefaultWhiteTexture
                                                                                     : constant(mat.OcclusionStrength));
            auto height = resolveTexture(mat.HeightTexture, m_DefaultBlackTexture);
            if (!metallic || !roughness || !ao)
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "材質の定数テクスチャを準備できません");
                continue;
            }

            // 張るテクスチャに sparse（VT）が1枚でもあれば、シェーダーは常駐しないタイルを読まず粗いミップへ逃げる。
            uniform.MaterialParams[2] = AnySparseTexture(albedo, normal, metallic, roughness, ao, height) ? 1.0f : 0.0f;
            // VT の要求を書く先（このフレームのバッファ。対応しないデバイスでは null で、シェーダーに binding は入らない）。
            // アルベドが VT のとき、シェーダーがこのフレームの要求を書く（24bit 以下の整数は float に正確に載る）
            const TextureResources::VirtualTextureFeedbackTarget feedbackTarget =
                command.Textures ? command.Textures->GetVirtualTextureFeedbackTarget()
                                 : TextureResources::VirtualTextureFeedbackTarget{};
            uniform.MaterialParams[3] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.AlbedoTexture, albedo.get(), feedbackTarget));
            // 法線・ORM・高さも VT のとき、それぞれの表の番号で要求を書く（ORM の枠は metallic に張ったテクスチャ）
            uniform.VtFeedbackParams[0] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.NormalTexture, normal.get(), feedbackTarget));
            uniform.VtFeedbackParams[1] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.ORMTexture, orm.get(), feedbackTarget));
            uniform.VtFeedbackParams[2] = static_cast<float>(
                ResolveVirtualTextureFeedbackParam(command.Textures, mat.HeightTexture, height.get(), feedbackTarget));

            sectionDraw.Uniform->Update(&uniform, sizeof(SectionUniformData));

            const RHI::DescriptorSetPtr &descriptorSet = sectionDraw.DescriptorSet;
            descriptorSet->BindConstantBuffer(0, sectionDraw.Uniform, 0,
                                              static_cast<uint32_t>(sizeof(SectionUniformData)));

            // PBRテクスチャバインド
            descriptorSet->BindTexture(1, albedo);
            descriptorSet->BindSampler(1, m_DefaultLinearSampler);
            descriptorSet->BindTexture(2, normal);
            descriptorSet->BindSampler(2, m_DefaultLinearSampler);
            descriptorSet->BindTexture(3, metallic);
            descriptorSet->BindSampler(3, m_DefaultLinearSampler);
            descriptorSet->BindTexture(4, roughness);
            descriptorSet->BindSampler(4, m_DefaultLinearSampler);
            descriptorSet->BindTexture(5, ao);
            descriptorSet->BindSampler(5, m_DefaultLinearSampler);
            descriptorSet->BindTexture(6, height);
            descriptorSet->BindSampler(6, m_DefaultLinearSampler);
            BindVirtualTextureFeedback(*descriptorSet, VirtualTextureFeedbackBindingIndex, feedbackTarget);

            // 頂点シェーダーが引くインスタンスの表・メッシュの表と描画情報
            descriptorSet->BindStorageBuffer(8, m_InstanceTableBuffer, 0,
                                             static_cast<uint32_t>(instanceTable.size() * sizeof(GPUMegaInstance)));
            descriptorSet->BindStorageBuffer(MegaMeshTableBindingIndex, m_MeshTableBuffer, 0,
                                             static_cast<uint32_t>(tables.Meshes.size() * sizeof(GPUMegaMesh)));
            descriptorSet->BindStorageBuffer(9, m_DrawInfoBuffer, 0,
                                             static_cast<uint32_t>(m_DrawInfoBuffer->GetSize()));

            descriptorSet->Update();
        }

        // ========================================
        // クラスタカリングの記録（パスごと）
        // ========================================
        // カリング用ユニフォームの、パスによらない部分
        CullUniformData baseUniform{};
        cameraConstants.CopyShaderView(baseUniform.ViewMatrix);
        cameraConstants.CopyShaderProjection(baseUniform.ProjectionMatrix);
        cameraConstants.CopyCameraPosition(baseUniform.CameraPosition);
        baseUniform.CameraPosition[3] = 0.0f;
        frustumPlanes.CopyToShaderData(baseUniform.FrustumPlanes);
        baseUniform.InstanceCount = static_cast<uint32_t>(instanceTable.size());
        baseUniform.TotalGroupCount = static_cast<uint32_t>(totalGroups);
        baseUniform.LODBias = m_Settings.LODBias;
        baseUniform.ScreenHeight = static_cast<float>(m_CurrentHeight);
        // projectionFactor = screenHeight / (2 * tan(fov/2))
        const float halfFovTan = std::tan(cameraConstants.FieldOfViewRadians * 0.5f);
        baseUniform.ProjectionFactor = (halfFovTan > 1e-6f)
                                           ? static_cast<float>(m_CurrentHeight) / (2.0f * halfFovTan)
                                           : 1.0f;
        baseUniform.DebugPayloadMode = debugPayloadMode;
        // 「見えた」印: 前のフレームの2パス目が書いた値（今のフレームの番号）と、今のフレームが書く値。
        // 0（バッファを0で埋めた直後・見えなかった）とは決して一致しない（番号は1から数える）
        baseUniform.VisibleReadStamp = static_cast<uint32_t>(m_OcclusionFrameCount);
        baseUniform.VisibleWriteStamp = static_cast<uint32_t>(m_OcclusionFrameCount) + 1u;
        baseUniform.BvhRootCount = bvhInstanceCount;
        baseUniform.BvhNodeCapacity = BvhNodeQueueCapacity;
        baseUniform.BvhLeafCapacity = BvhLeafQueueCapacity;
        baseUniform.BvhPass2Capacity = BvhPass2Capacity;
        // インスタンスの判定: 範囲の数・距離・画素の大きさ（描く範囲が描画先の全体のときだけ）
        const uint32_t bvhRangeCount = (bvhInstanceCount + BvhRangeSeats - 1u) / BvhRangeSeats;
        baseUniform.BvhRangeCount = bvhRangeCount;
        baseUniform.InstanceCullDistance = m_Settings.InstanceCullDistance;
        if (m_Settings.bInstancePixelCull && IsPixelCullViewport(command.Viewport, m_CurrentWidth, m_CurrentHeight))
        {
            baseUniform.PixelCullWidth = static_cast<float>(m_CurrentWidth);
            baseUniform.PixelCullHeight = static_cast<float>(m_CurrentHeight);
        }
        baseUniform.PageRequestCapacity = pageRequestCapacity;
        baseUniform.bSwRasterEnabled = bSwRasterSkipHardware ? 2u : (bSwRasterBin ? 1u : 0u);
        baseUniform.SwRasterCapacity = bSwRasterBin ? swRasterCapacity : 0u;
        baseUniform.SwRasterMaxPixels = m_SwRasterMaxPixels;
        baseUniform.SwRasterNearPlane = cam.NearPlane;

        // メッシュ共通のLOD球を持つメッシュの選ばれる段を、変わったときに記録する
        for (const Drawable &drawable : drawables)
        {
            if (drawable.GpuData->LODBounds.IsValid())
            {
                LogUniformLODSelection(m_Instances[drawable.InstanceIndex], *drawable.GpuData, baseUniform);
            }
        }

        RHI::BufferPtr statsBuffer = m_DummyStatsBuffer;
        // この記録の統計の読み戻し（借りられたときだけ。statsSlot が null なら統計を取らない）
        PendingStats statsRecord;
        PendingStats *statsSlot = nullptr;
        if (bWantStats)
        {
            FrameReadbackRequest readbackRequest;
            readbackRequest.Receiver = &m_StatsReadback;
            readbackRequest.Size = StatsBufferBytes;
            readbackRequest.ViewId = command.ViewId;
            readbackRequest.ViewportId = command.ViewportId;
            const FrameReadbackTarget statsTarget = command.FrameResources->RequestReadback(readbackRequest);
            // 束縛は先頭から（借りた読み戻し先の位置は常に 0）
            if (statsTarget.IsValid() && statsTarget.Offset == 0)
            {
                statsBuffer = statsTarget.Buffer;
                // 1フレームぶんの統計を GPU で 0 から数える（借りた読み戻し先は前の受け取りが終わった後）
                cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
                cmdList->FillBuffer(statsBuffer, 0, StatsBufferBytes, 0);
                cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
                statsRecord.RequestId = statsTarget.RequestId;
                statsRecord.Frame = m_OcclusionFrameCount;
                statsRecord.RenderFrame = command.FrameNumber;
                statsRecord.EpochFrame = epochFrame;
                statsRecord.bSwRasterBin = bSwRasterBin;
                statsRecord.SwDispatches = 0;
                statsSlot = &statsRecord;
            }
        }

        // 区間のカウンタ（全パス分）を0にし、コマンド・描画情報をカリングが書ける状態にする。
        // GPU側のカウントを参照できない環境（DrawIndexedIndirect）では、積まれなかったコマンドが
        // instanceCount=0 の空振りになるよう、コマンドも0で埋める
        cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
        cmdList->FillBuffer(m_DrawCountBuffer, 0, static_cast<uint64_t>(sectionCount) * passCount * sizeof(uint32_t), 0);
        cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        if (caps.bDrawIndirectCount)
        {
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }
        else
        {
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_IndirectDrawBuffer, 0, commandsTotal * IndirectCommandBytes, 0);
            cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        }
        cmdList->BufferBarrier(m_DrawInfoBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        if (bSwRasterBin)
        {
            // ソフトの一覧の頭（間接 dispatch の引数と積もうとした数）を 0 にする。一覧の中身は積んだ分だけが読まれるので消さない
            cmdList->BufferBarrier(m_SwRasterBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_SwRasterBuffer, 0, SwRasterHeaderWords * sizeof(uint32_t), 0);
            cmdList->BufferBarrier(m_SwRasterBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        }

        // BVH のたどりの列・カウンタを、このフレームで使い始めたか（最初のパスの前に Common から使える状態にする）
        bool bBvhBuffersUsed = false;
        // 2 パス目の根の入力を、このフレームの 1 パス目が書いたか（2 パス目の段 0 はそれを読む。書かなければ全部の根を判定する）
        bool bBvhPass2Used = false;

        // cullPass: 0=従来の1回の判定、1=1パス目、2=2パス目。hiZTexture は2パス目の遮蔽の判定が読む HZB（null なら判定しない）
        auto recordCull = [&](uint32_t cullPass, const RHI::TexturePtr &hiZTexture) -> void
        {
            const bool bSecond = cullPass == CULL_PASS_SECOND;
            const uint32_t passIndex = bSecond ? 1u : 0u;
            ScopedGpuTimestamp cullTimestamp(cmdList, bSecond ? "MegaGeometryCull2" : "MegaGeometryCull1");

            // ----------------------------------------
            // 1. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData = baseUniform;
            uniformData.CullPass = cullPass;
            uniformData.bStatsEnabled = (statsSlot && cullPass != CULL_PASS_SINGLE) ? 1u : 0u;
            uniformData.SectionBase = passIndex * sectionCount;

            // 遮蔽の判定（2パス目だけ）。HZB の元の深度の解像度を渡す（HZB のミップ0 はその半分）
            if (bSecond && hiZTexture && m_DepthTexture)
            {
                uniformData.HiZWidth = m_DepthTexture->GetWidth();
                uniformData.HiZHeight = m_DepthTexture->GetHeight();
                uniformData.HiZMipCount = m_HiZ.GetMipCount();
                uniformData.bHiZEnabled = 1;
            }
            frameSlot.CullUniform[passIndex]->Update(&uniformData, sizeof(CullUniformData));

            // ----------------------------------------
            // 2. カリングディスクリプタセット（平らな判定と BVH の各段で、UBO だけが違う）
            // ----------------------------------------
            const auto bindCullDescriptors = [&](const RHI::DescriptorSetPtr &descriptorSet,
                                                 const RHI::BufferPtr &cullUniformBuffer) -> void
            {
                descriptorSet->BindConstantBuffer(0, cullUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(CullUniformData)));
                descriptorSet->BindStorageBuffer(1, m_InstanceTableBuffer, 0,
                                                 static_cast<uint32_t>(instanceTable.size() * sizeof(GPUMegaInstance)));
                descriptorSet->BindStorageBuffer(MegaMeshTableBindingIndex, m_MeshTableBuffer, 0,
                                                 static_cast<uint32_t>(tables.Meshes.size() * sizeof(GPUMegaMesh)));
                descriptorSet->BindStorageBuffer(2, m_IndirectDrawBuffer, 0,
                                                 static_cast<uint32_t>(m_IndirectDrawBuffer->GetSize()));
                descriptorSet->BindStorageBuffer(3, m_DrawCountBuffer, 0,
                                                 static_cast<uint32_t>(m_DrawCountBuffer->GetSize()));

                // binding 4 の HZB は2パス目の遮蔽の判定（bHiZEnabled=1）だけが読む
                descriptorSet->BindTexture(4, (bSecond && hiZTexture) ? hiZTexture : m_DefaultBlackTexture);
                descriptorSet->BindSampler(4, m_DefaultLinearSampler);

                // binding 5・6: 「前のフレームで見えた」印と統計。従来の経路は触らないので代わりのバッファを結ぶ
                if (bTwoPass && cullPass != CULL_PASS_SINGLE)
                {
                    descriptorSet->BindStorageBuffer(5, m_VisibilityBuffer, 0,
                                                     static_cast<uint32_t>(m_VisibilityBuffer->GetSize()));
                }
                else
                {
                    descriptorSet->BindStorageBuffer(5, m_DummyVisibilityBuffer, 0,
                                                     static_cast<uint32_t>(m_DummyVisibilityBuffer->GetSize()));
                }
                descriptorSet->BindStorageBuffer(6, statsBuffer, 0, StatsBufferBytes);
                descriptorSet->BindStorageBuffer(7, frameSlot.SectionBuffer, 0,
                                                 static_cast<uint32_t>(sectionCount * passCount * 2u * sizeof(uint32_t)));
                descriptorSet->BindStorageBuffer(8, m_DrawInfoBuffer, 0,
                                                 static_cast<uint32_t>(m_DrawInfoBuffer->GetSize()));
                // binding 9・10・14: BVH のたどりの列・カウンタ・2 パス目の根の入力（BVH を使わない経路は触らない。
                // BVH のインスタンスがまだ現れず列を作っていなければ、代わりにカウンタを結ぶ）
                const RHI::BufferPtr &bvhQueue = m_BvhQueueBuffer ? m_BvhQueueBuffer : m_BvhCounterBuffer;
                const RHI::BufferPtr &bvhPass2 = m_BvhPass2Buffer ? m_BvhPass2Buffer : m_BvhCounterBuffer;
                descriptorSet->BindStorageBuffer(9, bvhQueue, 0, static_cast<uint32_t>(bvhQueue->GetSize()));
                descriptorSet->BindStorageBuffer(10, m_BvhCounterBuffer, 0, BvhCounterBytes);
                descriptorSet->BindStorageBuffer(14, bvhPass2, 0, static_cast<uint32_t>(bvhPass2->GetSize()));
                // binding 15: 範囲の境界（常駐の表を書けた記録だけが走るので在る。無ければ代わりにカウンタを結ぶ）
                const RHI::BufferPtr &rangeBounds = m_RangeBoundsBuffer ? m_RangeBoundsBuffer : m_BvhCounterBuffer;
                descriptorSet->BindStorageBuffer(RangeBoundsBinding, rangeBounds, 0,
                                                 static_cast<uint32_t>(rangeBounds->GetSize()));
                // binding 11・12: ページの表（このフレームの常駐）と、ページの要求の列
                descriptorSet->BindStorageBuffer(11, frameSlot.PageTableBuffer, 0,
                                                 static_cast<uint32_t>(frameSlot.PageTableBuffer->GetSize()));
                descriptorSet->BindStorageBuffer(12, pageRequestBuffer, 0,
                                                 static_cast<uint32_t>(pageRequestBuffer->GetSize()));
                // binding 13: ソフトウェアラスタの一覧（振り分けないフレームは触らないので代わりのバッファを結ぶ）
                if (bSwRasterBin)
                {
                    descriptorSet->BindStorageBuffer(13, m_SwRasterBuffer, 0,
                                                     static_cast<uint32_t>(m_SwRasterBuffer->GetSize()));
                }
                else
                {
                    descriptorSet->BindStorageBuffer(13, m_DummyStatsBuffer, 0, StatsBufferBytes);
                }
                descriptorSet->Update();
            };

            // 1次元のスレッド数を、x 方向の上限を避けて2次元のワークグループで出す
            const auto dispatchThreads = [&](uint64_t threads) -> void
            {
                const uint64_t groups = (threads + 63u) / 64u;
                const uint32_t dispatchX = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535u));
                const uint32_t dispatchY = static_cast<uint32_t>((groups + dispatchX - 1u) / dispatchX);
                cmdList->Dispatch(dispatchX, dispatchY, 1);
            };

            // ----------------------------------------
            // 3. 平らな判定（BVH を持たないインスタンス。全部を1回の dispatch で）
            // ----------------------------------------
            if (totalGroups > 0)
            {
                const RHI::DescriptorSetPtr &cullDescriptorSet = frameSlot.CullDescriptorSet[passIndex];
                bindCullDescriptors(cullDescriptorSet, frameSlot.CullUniform[passIndex]);
                cmdList->SetPipeline(m_CullPipeline);
                cmdList->SetDescriptorSet(cullDescriptorSet, 0);
                dispatchThreads(totalGroups * 64u);
            }

            // ----------------------------------------
            // 3b. BVH をたどる判定（BVH を持つインスタンス）。節を段ごとの dispatch で判定して枝を切り、
            //     残った葉のクラスタを平らな判定と同じ判定にかける
            // ----------------------------------------
            if (bvhInstanceCount > 0)
            {
                if (totalGroups > 0)
                {
                    // 平らな判定の書き込み（描画コマンド・カウンタ・描画情報・見えた印・統計）を、BVH の判定へ見せる
                    cmdList->BufferBarrier(m_IndirectDrawBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_DrawCountBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_DrawInfoBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    if (bSwRasterBin)
                    {
                        cmdList->BufferBarrier(m_SwRasterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                    if (bTwoPass && cullPass != CULL_PASS_SINGLE)
                    {
                        cmdList->BufferBarrier(m_VisibilityBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                    if (uniformData.bStatsEnabled != 0)
                    {
                        cmdList->BufferBarrier(statsBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                    if (pageRequestCapacity != 0)
                    {
                        // ページの要求の印と列に、BVH の葉のクラスタの判定が続けて書く
                        cmdList->BufferBarrier(frameSlot.PageTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                        cmdList->BufferBarrier(pageRequestBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    }
                }

                // 列とカウンタを使える状態にし、カウンタと間接 dispatch の引数をパスごとに0へ戻す
                cmdList->BufferBarrier(m_BvhCounterBuffer,
                                       bBvhBuffersUsed ? RHI::ResourceState::UnorderedAccess : RHI::ResourceState::Common,
                                       RHI::ResourceState::CopyDest);
                if (!bBvhBuffersUsed)
                {
                    cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
                    bBvhBuffersUsed = true;
                }
                cmdList->FillBuffer(m_BvhCounterBuffer, 0, BvhCounterBytes, 0);
                cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
                cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::IndirectArgument);

                // 2 パス目の根の入力: 1 パス目の前に頭（引数・数）だけを 0 にし、1 パス目の段 0 が積む。2 パス目はその書き込みを
                // 段 0 の入力と間接 dispatch の引数として読む（各パスの頭で 0 にするカウンタとは別のバッファ）
                if (cullPass == CULL_PASS_FIRST)
                {
                    cmdList->BufferBarrier(m_BvhPass2Buffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
                    cmdList->FillBuffer(m_BvhPass2Buffer, 0, BvhPass2HeaderWords * sizeof(uint32_t), 0);
                    cmdList->BufferBarrier(m_BvhPass2Buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
                    bBvhPass2Used = true;
                }
                const bool bPass2Roots = bSecond && bBvhPass2Used;
                if (bPass2Roots)
                {
                    cmdList->BufferBarrier(m_BvhPass2Buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhPass2Buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndirectArgument);
                }

                // 1 パス目・1 回の判定は、範囲の段が通った範囲を節の列 0 へ積み、段 0（席）はその範囲の数から書いた引数で
                // 間接 dispatch する（1 範囲 = 1 ワークグループ）。2 パス目の段 0 は根の入力の引数で dispatch する。
                // 段 1 以降と葉の段は、前の段が積んだ数（容量で頭打ち）から書いた引数で間接 dispatch する
                cmdList->SetPipeline(m_BvhCullPipeline);
                const bool bSeatsFromRanges = !bPass2Roots;
                if (bSeatsFromRanges)
                {
#if NORVES_ENABLE_STATS
                    char rangeTimestampName[32] = {};
                    std::snprintf(rangeTimestampName, sizeof(rangeTimestampName), "MegaBvhC%uRange", passIndex + 1u);
                    ScopedGpuTimestamp rangeTimestamp(cmdList, rangeTimestampName);
#endif
                    CullUniformData rangeUniform = uniformData;
                    rangeUniform.BvhStage = BvhStageRanges;
                    rangeUniform.BvhNextBase = bvhNodeQueueBase(0);
                    rangeUniform.BvhLeafBase = bvhLeafQueueBase;
                    const BvhStageDraw &rangeDraw = frameSlot.BvhStages[passIndex][bvhLevelCount + 1u];
                    rangeDraw.Uniform->Update(&rangeUniform, sizeof(CullUniformData));
                    bindCullDescriptors(rangeDraw.DescriptorSet, rangeDraw.Uniform);
                    cmdList->SetDescriptorSet(rangeDraw.DescriptorSet, 0);
                    dispatchThreads(bvhRangeCount);
                    // 席の段が、範囲の段が積んだ一覧・カウンタと引数を読む
                    cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndirectArgument);
                }
                for (uint32_t stage = 0; stage <= bvhLevelCount; ++stage)
                {
                    const bool bClusterStage = stage == bvhLevelCount;

#if NORVES_ENABLE_STATS
                    // 段ごとの GPU 時間（段 0 = インスタンスごとの根の判定。最後の段 = 葉のクラスタの判定）。
                    // 名前は 1 パス目が MegaBvhC1S<段>、2 パス目が MegaBvhC2S<段>、葉のクラスタの段は S<段> の代わりに Leaf
                    char stageTimestampName[32] = {};
                    if (bClusterStage)
                    {
                        std::snprintf(stageTimestampName, sizeof(stageTimestampName), "MegaBvhC%uLeaf", passIndex + 1u);
                    }
                    else
                    {
                        std::snprintf(stageTimestampName, sizeof(stageTimestampName), "MegaBvhC%uS%u", passIndex + 1u, stage);
                    }
                    ScopedGpuTimestamp stageTimestamp(cmdList, stageTimestampName);
#endif

                    CullUniformData stageUniform = uniformData;
                    stageUniform.BvhStage = bClusterStage ? BvhStageClusters : stage;
                    stageUniform.BvhInputBase = !bClusterStage ? bvhNodeQueueBase(stage) : 0u;
                    stageUniform.bBvhSeatsFromRanges = (stage == 0 && bSeatsFromRanges) ? 1u : 0u;
                    stageUniform.BvhNextBase = bvhNodeQueueBase(stage + 1u);
                    stageUniform.BvhLeafBase = bvhLeafQueueBase;
                    const BvhStageDraw &stageDraw = frameSlot.BvhStages[passIndex][stage];
                    stageDraw.Uniform->Update(&stageUniform, sizeof(CullUniformData));
                    bindCullDescriptors(stageDraw.DescriptorSet, stageDraw.Uniform);
                    cmdList->SetDescriptorSet(stageDraw.DescriptorSet, 0);
                    bool bDispatched = true;
                    if (stage == 0 && !bSeatsFromRanges)
                    {
                        bDispatched = cmdList->DispatchIndirect(m_BvhPass2Buffer, 0);
                    }
                    else
                    {
                        // 段 0 の引数（範囲の段が書く）は段 0 の位置にある
                        const uint32_t argsStage = bClusterStage ? BvhLeafCounter : stage;
                        const uint64_t argsOffset =
                            static_cast<uint64_t>(BvhArgsBaseWords + argsStage * BvhArgsStrideWords) * sizeof(uint32_t);
                        bDispatched = cmdList->DispatchIndirect(m_BvhCounterBuffer, argsOffset);
                    }
                    if (!bDispatched)
                    {
                        NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_BVH_INDIRECT_FAILED pass=%u stage=%u", passIndex + 1u, stage);
                    }

                    // 次の段が、この段が積んだ列・カウンタ（シェーダーの読み取り）と引数（間接 dispatch）を読む
                    cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                    cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndirectArgument);
                }
            }

            // ----------------------------------------
            // 4. バリア: Compute UAV → IndirectArgument（描画情報は頂点シェーダーが読む）
            // ----------------------------------------
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawInfoBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::GenericRead);
            if (bSwRasterBin)
            {
                // 次のパスのカリングが同じバッファの別の範囲へ続けて書く
                cmdList->BufferBarrier(m_SwRasterBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess);
            }
            if (cullPass == CULL_PASS_FIRST)
            {
                // 1パス目の読み取りを、2パス目の書き込みより前に済ませる
                cmdList->BufferBarrier(m_VisibilityBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess);
            }
            if (uniformData.bStatsEnabled != 0)
            {
                // 統計のカウンタへ、次のディスパッチが続けて足す
                cmdList->BufferBarrier(statsBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess);
            }
        };

        // ビジビリティバッファの解決が GBuffer を書くときは、描画の呼び出しだけを省く
        // 解決のパスが分かるときは、パイプラインの準備まで含めて判定する（使えないのに止めると画面が空になる）
        const bool bSkipGBufferDraw = ShouldSkipGBufferDraw(command.DebugMode);

        // ビジビリティバッファのラスタが引く、このフレームの描画の写し（カリングの結果のバッファと区間の表）
        const auto buildVisibilityPlan = [&](VisibilityDrawPlan &plan) -> void
        {
            plan.bValid = true;
            plan.PassCount = passCount;
            plan.SectionCount = sectionCount;
            plan.CommandsPerPass = static_cast<uint32_t>(commandsPerPass);
            plan.CommandsTotal = static_cast<uint32_t>(commandsTotal);
            plan.bUseIndirectCount = caps.bDrawIndirectCount;
            plan.IndirectBuffer = m_IndirectDrawBuffer;
            plan.CountBuffer = m_DrawCountBuffer;
            plan.DrawInfoBuffer = m_DrawInfoBuffer;
            plan.InstanceBuffer = m_InstanceTableBuffer;
            plan.InstanceBufferBytes = static_cast<uint64_t>(instanceTable.size()) * sizeof(GPUMegaInstance);
            plan.MeshBuffer = m_MeshTableBuffer;
            plan.MeshBufferBytes = static_cast<uint64_t>(tables.Meshes.size()) * sizeof(GPUMegaMesh);
            plan.SectionBuffer = frameSlot.SectionBuffer;
            plan.SectionBufferBytes = static_cast<uint64_t>(sectionCount) * passCount * 2u * sizeof(uint32_t);
            if (bSwRasterBin)
            {
                plan.SwRasterBuffer = m_SwRasterBuffer;
                plan.SwRasterCapacity = swRasterCapacity;
                plan.SwRasterMaxPixels = m_SwRasterMaxPixels;
            }
            plan.StatsBuffer = statsBuffer;
            plan.bStatsEnabled = statsSlot != nullptr;
            for (const Section &section : sections)
            {
                VisibilityDrawPlan::Section planSection;
                planSection.VertexBuffer = section.Representative->VertexBuffer;
                planSection.IndexBuffer = section.Representative->IndexBuffer;
                planSection.Capacity = section.Capacity;
                planSection.CommandBase = section.CommandBase;
                planSection.Material = section.Representative->Material;
                plan.Sections.push_back(planSection);
            }
        };

        // 描画（GBuffer render pass をパスごとに1回だけ開き、材質の区間ごとに1回の間接描画を発行する）
        auto recordDraws = [&](const RHI::RenderPassPtr &renderPass,
                               const RHI::FramebufferPtr &framebuffer,
                               uint32_t passIndex) -> void
        {
            if (sink)
            {
                // GBuffer の描画は止めている。添付の状態遷移は呼び出し側が済ませ、2パスの描画は sink が行う
                return;
            }
            ScopedGpuTimestamp drawTimestamp(cmdList, passIndex == 0 ? "MegaGeometryDraw1" : "MegaGeometryDraw2");

            cmdList->BeginRenderPass(renderPass, framebuffer);
            if (bSkipGBufferDraw)
            {
                // 添付の状態遷移（render pass の開始と終了）だけ行う
                cmdList->EndRenderPass();
                return;
            }
            cmdList->SetViewport(command.Viewport);
            cmdList->SetScissor(command.Scissor);
            cmdList->SetPipeline(SelectDrawPipeline(command.DebugMode));

            for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                const Section &section = sections[sectionIndex];
                if (section.Capacity == 0)
                {
                    continue;
                }

                cmdList->SetDescriptorSet(frameSlot.Sections[sectionIndex].DescriptorSet, 0);

                // 頂点/インデックスバッファ設定（プールの塊の先頭から。メッシュの位置はコマンドの基点が持つ）
                const auto *gpuData = section.Representative;
                cmdList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
                cmdList->SetIndexBuffer(gpuData->IndexBuffer, 0);

                // IndirectDraw発行
                // DrawIndirectCount対応の場合はGPU側カウントを参照し、
                // 実際に可視なクラスタ数だけドローコールを発行する。
                // 非対応の場合は区間のコマンドの最大数をそのまま使用（積まれなかった分は instanceCount=0 で空振り）。
                const uint64_t commandOffsetBytes =
                    (static_cast<uint64_t>(passIndex) * commandsPerPass + section.CommandBase) * IndirectCommandBytes;
            if (caps.bDrawIndirectCount)
            {
                    cmdList->DrawIndexedIndirectCount(m_IndirectDrawBuffer, commandOffsetBytes, m_DrawCountBuffer,
                                                      static_cast<uint64_t>(passIndex * sectionCount + sectionIndex) *
                                                          sizeof(uint32_t),
                                                      section.Capacity, IndirectCommandBytes);
            }
            else
            {
                    cmdList->DrawIndexedIndirect(m_IndirectDrawBuffer, commandOffsetBytes, section.Capacity,
                        IndirectCommandBytes);
            }
        }

            cmdList->EndRenderPass();
        };

        // sink に描かせるのは2パスのときだけ（1回の判定は、描画の写しを残してラスタが全部を1回で描く）
        const bool bStaged = sink != nullptr && bTwoPass;
        VisibilityDrawPlan stagedPlan;
        const uint32_t swDispatchesBefore = sink ? sink->GetSwRasterDispatchCount() : 0u;

        if (!bTwoPass)
        {
            // 従来の経路: 全インスタンスを1回の判定（遮蔽の判定なし）で選び、1回の render pass で描く
            recordCull(CULL_PASS_SINGLE, nullptr);
            recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, 0);
        }
        else
        {
            if (bStaged)
            {
                buildVisibilityPlan(stagedPlan);
            }

            // 1パス目: 前のフレームで見えたクラスタだけを描く。前のフレームの2パス目の書き込みを見せる
            cmdList->BufferBarrier(m_VisibilityBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::UnorderedAccess);
            recordCull(CULL_PASS_FIRST, nullptr);
            if (bStaged)
            {
                sink->RecordFirstPassDraws(cmdList, stagedPlan);
                // HZB の元の深度へ、ハードのラスタの外で書かれた深度（ソフトウェアラスタ）を合流させる
                sink->RecordMergeBeforeHiZ(cmdList, stagedPlan);
            }
            else
            {
                recordDraws(m_GBufferRenderPass, m_GBufferFramebuffer, 0);
            }

            // 深度（GBufferPass の不透明＋1パス目。sink があるときは ID のラスタが描いた塊＋1パス目＋合流した値）から HZB を作る
            // （深度は1パス目の終わりで ShaderResource）。
            // 作れなかったときは遮蔽の判定をしない（1パス目で描かなかったクラスタを全て描く）
            const bool bHiZBuilt = m_HiZ.Build(cmdList, m_DepthTexture);

            // 1パス目が描いたクラスタの使用の印（ページの表の要求の印と要求の列）を、2パス目の書き込みへ見せる。
            // 同じフレームの印での重複の省略と、列の件数の加算が、パスをまたいで続くため
            if (pageRequestCapacity != 0)
            {
                cmdList->BufferBarrier(frameSlot.PageTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
                cmdList->BufferBarrier(pageRequestBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            }

            // 2パス目: 判定を通った全クラスタを HZB で判定し、1パス目で描かなかった見えるものを描く
            recordCull(CULL_PASS_SECOND, bHiZBuilt ? m_HiZ.GetTexture() : RHI::TexturePtr{});
            if (bStaged)
            {
                sink->RecordSecondPassDraws(cmdList, stagedPlan);
            }
            else
            {
                recordDraws(m_SecondGBufferRenderPass, m_SecondGBufferFramebuffer, 1);
            }
            if (statsSlot && sink)
            {
                // このフレームで sink が記録したソフトの dispatch の数（ログの sw_dispatches）
                statsSlot->SwDispatches = sink->GetSwRasterDispatchCount() - swDispatchesBefore;
            }

            if (statsSlot)
            {
                // 統計のシェーダーの書き込みを、ホストの読み取りへ見せる。提出の完了の後に OnFrameReadback で受け取る
                cmdList->BufferBarrier(statsBuffer,
                                       RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::HostRead);
                m_PendingStats.push_back(*statsSlot);
            }
        }

        // ページの要求の列へのシェーダーの書き込みを、ホストの読み取りへ見せる（数フレーム後に読み戻す）
        if (pageRequestCapacity != 0)
        {
            command.MegaGeometry->RecordPageRequestHostBarrier(*cmdList);
        }

        // ソフトの一覧を次のフレーム用に戻す
        if (bSwRasterBin)
        {
            cmdList->BufferBarrier(m_SwRasterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }

        // BVH のたどりの列・カウンタを次のフレーム用に戻す
        if (bBvhBuffersUsed)
        {
            cmdList->BufferBarrier(m_BvhQueueBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_BvhCounterBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
        if (bBvhPass2Used)
        {
            cmdList->BufferBarrier(m_BvhPass2Buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }

        if (m_bVisibilityPlanEnabled || bStaged)
        {
            // ビジビリティバッファのラスタが同じコマンドをもう一度描くので、バッファは戻さず、描画の写しを渡す
            // （ラスタが使い終わった後に ReleaseVisibilityDrawBuffers で Common へ戻す）。
            // sink に描かせたフレームは、写しを sink へ渡し済みなので残さない
            if (!bStaged)
            {
                buildVisibilityPlan(m_VisibilityPlan);
            }
            m_bVisibilityBuffersHeld = true;
            m_HeldIndirectDrawBuffer = m_IndirectDrawBuffer;
            m_HeldDrawCountBuffer = m_DrawCountBuffer;
            m_HeldDrawInfoBuffer = m_DrawInfoBuffer;
        }
        else
        {
            // IndirectDrawバッファ・カウンタ・描画情報を次のフレーム用に戻す
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_DrawInfoBuffer,
                                   RHI::ResourceState::GenericRead,
                                   RHI::ResourceState::Common);
        }

        ReleaseStaleBuffers();
    }

    bool MegaGeometryPass::TakeVisibilityDrawPlan(VisibilityDrawPlan &outPlan)
    {
        const bool bValid = m_VisibilityPlan.bValid;
        outPlan = std::move(m_VisibilityPlan);
        m_VisibilityPlan = VisibilityDrawPlan{};
        return bValid;
    }

    void MegaGeometryPass::ReleaseVisibilityDrawBuffers(RHI::ICommandList *commandList,
                                                        RHI::ResourceState indirectState)
    {
        if (!m_bVisibilityBuffersHeld)
        {
            return;
        }
        m_bVisibilityBuffersHeld = false;
        if (commandList)
        {
            if (m_HeldIndirectDrawBuffer)
            {
                commandList->BufferBarrier(m_HeldIndirectDrawBuffer, indirectState, RHI::ResourceState::Common);
            }
            if (m_HeldDrawCountBuffer)
            {
                commandList->BufferBarrier(m_HeldDrawCountBuffer, indirectState, RHI::ResourceState::Common);
            }
            if (m_HeldDrawInfoBuffer)
            {
                commandList->BufferBarrier(m_HeldDrawInfoBuffer, RHI::ResourceState::GenericRead, RHI::ResourceState::Common);
            }
        }
        m_HeldIndirectDrawBuffer.reset();
        m_HeldDrawCountBuffer.reset();
        m_HeldDrawInfoBuffer.reset();
    }

    void MegaGeometryPass::LogUniformLODSelection(const MegaMeshInstance &instance,
                                                  const MegaGeometry::MegaMeshGPUData &gpuData,
                                                  const CullUniformData &uniformData)
    {
        if (gpuData.LevelRanges.empty())
        {
            return;
        }

        // ワールド行列は行ベクトル規約（並進は行3）。半径は最大の軸の伸びで広げる（cluster_cull.comp と同じ）。
        const float *world = instance.WorldMatrix;
        const BoundingSphere &local = gpuData.LODBounds;
        float center[3] = {};
        float maxScaleSquared = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            center[axis] = local.CenterX * world[0 + axis] + local.CenterY * world[4 + axis] +
                           local.CenterZ * world[8 + axis] + world[12 + axis];
            const float lengthSquared = world[axis * 4 + 0] * world[axis * 4 + 0] +
                                        world[axis * 4 + 1] * world[axis * 4 + 1] +
                                        world[axis * 4 + 2] * world[axis * 4 + 2];
            maxScaleSquared = std::max(maxScaleSquared, lengthSquared);
        }
        const float scale = std::sqrt(maxScaleSquared);
        const float dx = center[0] - uniformData.CameraPosition[0];
        const float dy = center[1] - uniformData.CameraPosition[1];
        const float dz = center[2] - uniformData.CameraPosition[2];
        const float centerDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float radius = local.Radius * scale;

        // 中心の深さと画角は、シェーダーへ渡す列優先の行列から cluster_cull.comp と同じく求める
        // （配列の [列 * 4 + 行]。深さはクリップ座標の w を projection[2][3] で割った値）。
        const float *view = uniformData.ViewMatrix;
        const float *projection = uniformData.ProjectionMatrix;
        float viewPosition[4] = {};
        for (uint32_t row = 0; row < 4; ++row)
        {
            viewPosition[row] =
                view[0 * 4 + row] * center[0] + view[1 * 4 + row] * center[1] + view[2 * 4 + row] * center[2] +
                view[3 * 4 + row];
        }
        float clipW = 0.0f;
        for (uint32_t column = 0; column < 4; ++column)
        {
            clipW += projection[column * 4 + 3] * viewPosition[column];
        }
        const float depthScale = std::abs(projection[2 * 4 + 3]);
        const float centerDepth = depthScale > 1.0e-6f ? clipW / depthScale : 0.0f;
        const float tanHalfFovX = 1.0f / std::max(std::abs(projection[0 * 4 + 0]), 1.0e-6f);
        const float tanHalfFovY = 1.0f / std::max(std::abs(projection[1 * 4 + 1]), 1.0e-6f);
        const float errorScale = MegaGeometry::ComputeLODSphereErrorPixelsPerMeter(centerDistance,
                                                                                    centerDepth,
                                                                                    radius,
                                                                                    uniformData.ProjectionFactor,
                                                                                    tanHalfFovX,
                                                                                    tanHalfFovY) *
                                 scale;
        const uint32_t level = MegaGeometry::SelectCoarsestLODWithinError(gpuData.LevelRanges,
                                                                           errorScale,
                                                                           uniformData.LODBias);

        LoggedUniformLOD *logged = nullptr;
        for (LoggedUniformLOD &entry : m_LoggedUniformLODs)
        {
            if (entry.MegaMeshId == instance.Handle.Id)
            {
                logged = &entry;
                break;
            }
        }
        if (logged && logged->Level == level)
        {
            return;
        }
        if (!logged)
        {
            m_LoggedUniformLODs.push_back(LoggedUniformLOD{instance.Handle.Id, level});
        }
        else
        {
            logged->Level = level;
        }

        const MegaGeometry::MegaMeshLevelRange &range = gpuData.LevelRanges[level];
        const bool bHasCoarser = level + 1u < gpuData.LevelRanges.size();
        NORVES_LOG_INFO("MegaGeometryPass",
                        "mega_lod_select mesh=\"%s\" level=%u level_triangles=%u "
                        "camera_to_lod_center_m=%.3f "
                        "lod_radius_m=%.3f center_depth_m=%.3f perspective_stretch=%.3f "
                        "threshold_px=%.2f "
                        "level_error_px=%.3f coarser_level_error_px=%.3f",
                        gpuData.DebugName.empty() ? "" : gpuData.DebugName.c_str(),
                        level,
                        range.IndexCount / 3u,
                        static_cast<double>(centerDistance),
                        static_cast<double>(radius),
                        static_cast<double>(centerDepth),
                        static_cast<double>(MegaGeometry::ComputeLODSpherePerspectiveStretch(
                            centerDistance, centerDepth, radius, tanHalfFovX, tanHalfFovY)),
                        static_cast<double>(uniformData.LODBias),
                        static_cast<double>(range.Error * errorScale),
                        bHasCoarser ? static_cast<double>(gpuData.LevelRanges[level + 1u].Error * errorScale) : -1.0);
    }

    // ========================================
    // MegaMeshインスタンス管理
    // ========================================

    void MegaGeometryPass::AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix)
    {
        MegaMeshInstance instance;
        instance.Handle = handle;
        if (worldMatrix)
        {
            std::memcpy(instance.WorldMatrix, worldMatrix, sizeof(float) * 16);
        }
        else
        {
            // 単位行列
            std::memset(instance.WorldMatrix, 0, sizeof(float) * 16);
            instance.WorldMatrix[0] = 1.0f;
            instance.WorldMatrix[5] = 1.0f;
            instance.WorldMatrix[10] = 1.0f;
            instance.WorldMatrix[15] = 1.0f;
        }
        // VSM のキャッシュ: 並びが変わるので、次に表を作るときに今の並びと鍵で引いて比べる
        BeginShadowRebuild(true);
        m_Instances.push_back(instance);
        m_InstanceSourceRevision = 0;
        m_InstanceSourceProxies = nullptr;
        m_bSceneSlotsDirty = true;
        m_bSceneRowsAllDirty = true;
        m_bSceneTablesValid = false;
    }

    void MegaGeometryPass::ClearMegaMeshInstances()
    {
        // VSM のキャッシュ: 前に表へ載せた投影物の境界を無効化へ積む
        BeginShadowRebuild(false);
        m_Instances.clear();
        FlushShadowRebuildWithoutInstances();
        m_InstanceSourceRevision = 0;
        m_InstanceSourceProxies = nullptr;
        m_bSceneSlotsDirty = true;
        m_bSceneRowsAllDirty = true;
        m_bSceneTablesValid = false;
    }

    namespace
    {
        uint32_t NextPowerOfTwo(uint32_t value)
        {
            if (value <= 1u)
            {
                return 1u;
            }
            --value;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            return value + 1u;
        }
    } // namespace

    // ========================================
    // カリング用GPUリソース作成
    // ========================================

    bool MegaGeometryPass::CreateCullResources(RHI::IDevice *device)
    {
        if (!device)
        {
            return false;
        }

        // IndirectDrawコマンド・区間のカウンタ・描画情報（足りなくなったら EnsureBatchBuffers が作り直す）
        constexpr uint32_t InitialCommandCapacity = 4096;
        constexpr uint32_t InitialCounterCapacity = 64;
        if (!EnsureBatchBuffers(InitialCommandCapacity, InitialCounterCapacity))
        {
            return false;
        }

        // 従来の経路が binding 5・6 に結ぶ代わりのバッファ（シェーダーは触らない）
        RHI::BufferDesc dummyVisibilityDesc(
            4u * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DummyVisibility");
        m_DummyVisibilityBuffer = device->CreateBuffer(dummyVisibilityDesc);
        RHI::BufferDesc dummyStatsDesc(
            StatsBufferBytes,
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DummyStats");
        m_DummyStatsBuffer = device->CreateBuffer(dummyStatsDesc);
        if (!m_DummyVisibilityBuffer || !m_DummyStatsBuffer)
        {
            return false;
        }

        // BVH のたどりのカウンタ（平らな判定のディスクリプタセットも binding 9・10・14 に結ぶので、常に作っておく。
        // 固定容量の列と 2 パス目の根の入力は、BVH のインスタンスが初めて現れた記録で作る）
        return EnsureBvhCounterBuffer();
    }

    bool MegaGeometryPass::EnsureBvhCounterBuffer()
    {
        if (!m_Device)
        {
            return false;
        }
        if (m_BvhCounterBuffer)
        {
            return true;
        }

        // 頭は段ごとの間接 dispatch の引数になるので IndirectBuffer も持つ。各パスの頭で FillBuffer で 0 にする
        RHI::BufferDesc counterDesc(BvhCounterBytes,
                                    RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer |
                                        RHI::ResourceUsage::TransferDst,
                                    false,
                                    "MegaGeometry_BvhCounters");
        m_BvhCounterBuffer = m_Device->CreateBuffer(counterDesc);
        return m_BvhCounterBuffer != nullptr;
    }

    bool MegaGeometryPass::EnsureBvhQueueBuffers()
    {
        if (!m_Device || !m_BvhCounterBuffer)
        {
            return false;
        }
        if (m_BvhQueueBuffer && m_BvhPass2Buffer)
        {
            return true;
        }

        // 容量は固定なので作り直さない。割り当ての失敗（VRAM の不足）は例外で届くので、ここで受けて描画を見送る側へ返す
        RHI::BufferPtr queueBuffer = m_BvhQueueBuffer;
        RHI::BufferPtr pass2Buffer = m_BvhPass2Buffer;
        const uint64_t queueBytes = BvhQueueEntries * 2u * sizeof(uint32_t);
        const uint64_t pass2Bytes = (static_cast<uint64_t>(BvhPass2HeaderWords) + BvhPass2Capacity) * sizeof(uint32_t);
        try
        {
            if (!queueBuffer)
            {
                RHI::BufferDesc queueDesc(queueBytes, RHI::ResourceUsage::StorageBuffer, false, "MegaGeometry_BvhQueue");
                queueBuffer = m_Device->CreateBuffer(queueDesc);
            }
            if (!pass2Buffer)
            {
                // 頭は 2 パス目の段 0 の間接 dispatch の引数になるので IndirectBuffer も持つ。1 パス目の前に頭を FillBuffer で 0 にする
                RHI::BufferDesc pass2Desc(pass2Bytes,
                                          RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer |
                                              RHI::ResourceUsage::TransferDst,
                                          false,
                                          "MegaGeometry_BvhPass2Roots");
                pass2Buffer = m_Device->CreateBuffer(pass2Desc);
            }
        }
        catch (const std::exception &exception)
        {
            if (!m_bBvhQueueAllocFailedLogged)
            {
                NORVES_LOG_ERROR("MegaGeometryPass",
                                 "MEGA_BVH_QUEUE_ALLOC_FAILED queue_bytes=%llu pass2_bytes=%llu reason=%s。描画を見送ります",
                                 static_cast<unsigned long long>(queueBytes),
                                 static_cast<unsigned long long>(pass2Bytes),
                                 exception.what());
                m_bBvhQueueAllocFailedLogged = true;
            }
            return false;
        }
        if (!queueBuffer || !pass2Buffer)
        {
            if (!m_bBvhQueueAllocFailedLogged)
            {
                NORVES_LOG_ERROR("MegaGeometryPass",
                                 "MEGA_BVH_QUEUE_ALLOC_FAILED queue_bytes=%llu pass2_bytes=%llu reason=null。描画を見送ります",
                                 static_cast<unsigned long long>(queueBytes),
                                 static_cast<unsigned long long>(pass2Bytes));
                m_bBvhQueueAllocFailedLogged = true;
            }
            // 作れた方は持っておき、次の記録で残りだけを作り直す
            m_BvhQueueBuffer = queueBuffer;
            m_BvhPass2Buffer = pass2Buffer;
            return false;
        }

        m_BvhQueueBuffer = queueBuffer;
        m_BvhPass2Buffer = pass2Buffer;
        m_bBvhQueueAllocFailedLogged = false;
        NORVES_LOG_INFO("MegaGeometryPass",
                        "VRAM_LEDGER bvh_queue mb=%.2f node_capacity=%u leaf_capacity=%u pass2_mb=%.2f pass2_capacity=%u",
                        static_cast<double>(queueBytes) / (1024.0 * 1024.0),
                        BvhNodeQueueCapacity,
                        BvhLeafQueueCapacity,
                        static_cast<double>(pass2Bytes) / (1024.0 * 1024.0),
                        BvhPass2Capacity);
        return true;
    }

    bool MegaGeometryPass::EnsureSwRasterBuffer(uint32_t capacity)
    {
        if (!m_Device || capacity == 0)
        {
            return false;
        }
        if (m_SwRasterBuffer && capacity <= m_SwRasterBufferCapacity)
        {
            return true;
        }

        const uint32_t newCapacity = NextPowerOfTwo(std::max(capacity, m_SwRasterBufferCapacity));
        const uint64_t words = static_cast<uint64_t>(SwRasterHeaderWords) + 2ull * newCapacity;
        // 頭は間接 dispatch の引数になるので IndirectBuffer も持つ。頭は FillBuffer で 0 にする
        RHI::BufferDesc desc(words * sizeof(uint32_t),
                             RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferDst,
                             false,
                             "MegaGeometry_SwRaster");
        RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
        if (!buffer)
        {
            return false;
        }

        // 古いバッファは、直前のフレームのGPUがまだ使っているかもしれないので、しばらく保持してから破棄する
        if (m_SwRasterBuffer)
        {
            m_RetiredBuffers.push_back(RetiredBuffer{m_SwRasterBuffer, m_RenderFrameCount});
        }
        m_SwRasterBuffer = buffer;
        m_SwRasterBufferCapacity = newCapacity;
        NORVES_LOG_INFO("MegaGeometryPass",
                        "VRAM_LEDGER sw_raster_list mb=%.2f capacity=%u",
                        static_cast<double>(desc.Size) / (1024.0 * 1024.0),
                        newCapacity);
        return true;
    }

    bool MegaGeometryPass::EnsureBvhStageResources(FrameSlot &slot, uint32_t passIndex, uint32_t stageCount)
    {
        // FrameUseRing の枠（FrameSlot）の資源。Acquire と作成が別の区間（準備の段と記録の段）になることがあるので、
        // 作る所で守られていない上書きの見張りへ「守られている」と登録する
        RHI::FrameResourceProtectedCreationScope protectedCreation;
        if (!m_Device)
        {
            return false;
        }

        const RHI::DescriptorSetDesc cullDsDesc = BuildCullDescriptorSetDesc();
        while (slot.BvhStages[passIndex].size() < stageCount)
        {
            BvhStageDraw stageDraw;
            RHI::BufferDesc desc(sizeof(CullUniformData),
                                 RHI::ResourceUsage::ConstantBuffer,
                                 true,
                                 "MegaGeometry_BvhCullUBO");
            stageDraw.Uniform = m_Device->CreateBuffer(desc);
            stageDraw.DescriptorSet = m_Device->CreateDescriptorSet(cullDsDesc);
            if (!stageDraw.Uniform || !stageDraw.DescriptorSet)
            {
                return false;
            }
            slot.BvhStages[passIndex].push_back(stageDraw);
        }
        return true;
    }

    bool MegaGeometryPass::EnsureBatchBuffers(uint32_t commandCapacity, uint32_t counterCapacity)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_IndirectDrawBuffer && m_DrawCountBuffer && m_DrawInfoBuffer &&
            commandCapacity <= m_CommandCapacity && counterCapacity <= m_CounterCapacity)
        {
            return true;
        }

        const uint32_t newCommandCapacity = NextPowerOfTwo(std::max(commandCapacity, m_CommandCapacity));
        const uint32_t newCounterCapacity = NextPowerOfTwo(std::max(counterCapacity, m_CounterCapacity));

        // IndirectDrawコマンドバッファ (SSBO + IndirectBuffer)
        RHI::BufferDesc indirectDesc(
            static_cast<uint64_t>(newCommandCapacity) * sizeof(MegaGeometry::DrawIndexedIndirectCommand),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferDst,
            false,
            "MegaGeometry_IndirectDraw");
        RHI::BufferPtr indirectBuffer = m_Device->CreateBuffer(indirectDesc);

        // 区間ごとのカウンタ（atomic counter用 SSBO。DrawIndexedIndirectCount のカウントバッファも兼ねる）
        RHI::BufferDesc countDesc(
            static_cast<uint64_t>(newCounterCapacity) * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferDst,
            false,
            "MegaGeometry_DrawCount");
        RHI::BufferPtr countBuffer = m_Device->CreateBuffer(countDesc);

        // コマンドごとの描画情報（インスタンスの番号・payload）。カリングが書き、頂点シェーダーが読む
        RHI::BufferDesc drawInfoDesc(
            static_cast<uint64_t>(newCommandCapacity) * 2u * sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer,
            false,
            "MegaGeometry_DrawInfo");
        RHI::BufferPtr drawInfoBuffer = m_Device->CreateBuffer(drawInfoDesc);

        if (!indirectBuffer || !countBuffer || !drawInfoBuffer)
        {
            return false;
        }

        // 古いバッファは、直前のフレームのGPUがまだ使っているかもしれないので、しばらく保持してから破棄する
        for (RHI::BufferPtr *old : {&m_IndirectDrawBuffer, &m_DrawCountBuffer, &m_DrawInfoBuffer})
        {
            if (*old)
            {
                m_RetiredBuffers.push_back(RetiredBuffer{*old, m_RenderFrameCount});
            }
        }
        m_IndirectDrawBuffer = indirectBuffer;
        m_DrawCountBuffer = countBuffer;
        m_DrawInfoBuffer = drawInfoBuffer;
        m_CommandCapacity = newCommandCapacity;
        m_CounterCapacity = newCounterCapacity;
        return true;
    }

    RHI::DescriptorSetDesc MegaGeometryPass::BuildCullDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;
        auto addBinding = [&desc](uint32_t binding, RHI::ResourceBindType type) -> void
        {
            RHI::DescriptorBinding descriptorBinding;
            descriptorBinding.binding = binding;
            descriptorBinding.type = type;
            descriptorBinding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(descriptorBinding);
        };
        addBinding(0, RHI::ResourceBindType::ConstantBuffer);        // カリング用ユニフォーム
        addBinding(1, RHI::ResourceBindType::StructuredBuffer);      // インスタンスの表
        addBinding(2, RHI::ResourceBindType::RWBuffer);              // IndirectDrawコマンド
        addBinding(3, RHI::ResourceBindType::RWBuffer);              // 区間ごとのカウンタ
        addBinding(4, RHI::ResourceBindType::CombinedImageSampler);  // Hi-Z
        addBinding(5, RHI::ResourceBindType::RWBuffer);              // 「前のフレームで見えた」ビット
        addBinding(6, RHI::ResourceBindType::RWBuffer);              // 統計
        addBinding(7, RHI::ResourceBindType::StructuredBuffer);      // 区間の表
        addBinding(8, RHI::ResourceBindType::RWBuffer);              // 描画情報
        addBinding(9, RHI::ResourceBindType::RWBuffer);              // BVH のたどりの列
        addBinding(10, RHI::ResourceBindType::RWBuffer);             // BVH のたどりのカウンタ
        addBinding(11, RHI::ResourceBindType::RWBuffer);             // ページの表（常駐 + 要求の印）
        addBinding(12, RHI::ResourceBindType::RWBuffer);             // ページの要求の列
        addBinding(13, RHI::ResourceBindType::RWBuffer);             // ソフトウェアラスタの一覧と間接 dispatch の引数
        addBinding(14, RHI::ResourceBindType::RWBuffer);             // BVH のたどりの 2 パス目の根の入力と間接 dispatch の引数
        addBinding(RangeBoundsBinding, RHI::ResourceBindType::StructuredBuffer); // 範囲の境界（インスタンスの判定の範囲の段）
        addBinding(MegaMeshTableBindingIndex, RHI::ResourceBindType::StructuredBuffer); // メッシュの表
        return desc;
    }

    RHI::DescriptorSetDesc MegaGeometryPass::BuildDrawDescriptorSetDesc() const
    {
        // GBufferPassと同じ（set=0, binding 0=UBO, 1-6=textures）に、頂点シェーダーが引く
        // インスタンスの表（8）・描画情報（9）・メッシュの表（22）を足した形
        RHI::DescriptorSetDesc desc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        desc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(texBinding);
        }
        if (UsesVirtualTextureFeedbackBinding(m_Device))
        {
            AddVirtualTextureFeedbackBinding(desc, VirtualTextureFeedbackBindingIndex);
        }

        for (uint32_t binding : {8u, 9u, MegaMeshTableBindingIndex})
        {
            RHI::DescriptorBinding bufferBinding;
            bufferBinding.binding = binding;
            bufferBinding.type = RHI::ResourceBindType::StructuredBuffer;
            bufferBinding.stages = RHI::ShaderStage::Vertex;
            desc.bindings.push_back(bufferBinding);
        }
        return desc;
    }

    RHI::DescriptorSetDesc MegaGeometryPass::BuildSceneScatterDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc desc;
        auto addBinding = [&desc](uint32_t binding, RHI::ResourceBindType type) -> void
        {
            RHI::DescriptorBinding descriptorBinding;
            descriptorBinding.binding = binding;
            descriptorBinding.type = type;
            descriptorBinding.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(descriptorBinding);
        };
        addBinding(0, RHI::ResourceBindType::StructuredBuffer); // 記録の列（staging）
        addBinding(1, RHI::ResourceBindType::RWBuffer);         // 席の表
        addBinding(2, RHI::ResourceBindType::RWBuffer);         // 描く順の表
        addBinding(3, RHI::ResourceBindType::RWBuffer);         // 影の表
        addBinding(4, RHI::ResourceBindType::RWBuffer);         // メッシュの表
        addBinding(5, RHI::ResourceBindType::RWBuffer);         // 範囲の境界
        return desc;
    }

    bool MegaGeometryPass::EnsureResidentTable(RHI::BufferPtr &buffer, uint32_t &capacity, uint32_t required,
                                               uint32_t minimumCapacity, uint64_t elementBytes, const char *debugName,
                                               RHI::ICommandList *commandList, bool &bOutGrown, bool &bOutCopied)
    {
        bOutGrown = false;
        bOutCopied = false;
        if (!m_Device)
        {
            return false;
        }
        if (buffer && required <= capacity)
        {
            return true;
        }

        const uint32_t newCapacity = std::max(minimumCapacity, NextPowerOfTwo(std::max(required, 1u)));
        // コピーの元と先になる（広げるときに古い表の中身を写す。開発ビルドの検査が読み戻す）
        RHI::BufferDesc desc(static_cast<uint64_t>(newCapacity) * elementBytes,
                             RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferSrc |
                                 RHI::ResourceUsage::TransferDst,
                             false,
                             debugName);
        RHI::BufferPtr newBuffer = m_Device->CreateBuffer(desc);
        if (!newBuffer)
        {
            return false;
        }
        uint64_t copiedBytes = 0;
        if (buffer)
        {
            // 古い表の中身を新しい表の先頭へ写す。古い表はどの更新の後も読み取りの状態にあり、前のフレームの散在書き込みは
            // 同じキューの前の記録なので、バリアで前の読み手・書き手の後に並ぶ。写した行は送り直さずに済む
            copiedBytes = std::min(buffer->GetSize(), desc.Size);
            if (commandList && copiedBytes > 0)
            {
                commandList->BufferBarrier(buffer, RHI::ResourceState::GenericRead, RHI::ResourceState::CopySource);
                commandList->CopyBuffer(buffer, newBuffer, copiedBytes, 0, 0);
                commandList->BufferBarrier(newBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::GenericRead);
                bOutCopied = true;
            }
            else
            {
                copiedBytes = 0;
            }
            // 古い表は、前のフレームの GPU がまだ読んでいるかもしれない（このフレームのコピーも読む）ので、しばらく保持してから破棄する
            m_RetiredBuffers.push_back(RetiredBuffer{buffer, m_RenderFrameCount});
        }
        buffer = newBuffer;
        capacity = newCapacity;
        bOutGrown = true;
        NORVES_LOG_INFO("MegaGeometryPass",
                        "VRAM_LEDGER %s mb=%.2f capacity=%u copied_bytes=%llu",
                        debugName,
                        static_cast<double>(desc.Size) / (1024.0 * 1024.0),
                        newCapacity,
                        static_cast<unsigned long long>(copiedBytes));
        return true;
    }

    void MegaGeometryPass::AssignSceneTableSlots()
    {
        m_bSceneSlotsDirty = false;
        // 席の表の番号が変わるので、次の表は全部を作る
        m_bSceneTablesValid = false;
        // 行の席（SceneSlot = 席の番号 + 1）をそのまま席の表の番号にする。席を持たないインスタンス（手で足した・行の外の
        // proxy）と、同じ席を持つ 2 つ目以降のインスタンスは、行の席の最大の次から順に割り当てる
        uint32_t slotLimit = 0;
        for (const MegaMeshInstance &instance : m_Instances)
        {
            slotLimit = std::max(slotLimit, instance.SceneSlot);
        }
        m_SceneSlotSeen.assign(slotLimit, static_cast<uint8_t>(0));
        uint32_t nextLocalSlot = slotLimit;
        for (MegaMeshInstance &instance : m_Instances)
        {
            if (instance.SceneSlot != 0 && m_SceneSlotSeen[instance.SceneSlot - 1u] == 0u)
            {
                m_SceneSlotSeen[instance.SceneSlot - 1u] = 1u;
                instance.TableSlot = instance.SceneSlot - 1u;
            }
            else
            {
                instance.TableSlot = nextLocalSlot++;
            }
        }
    }

    bool MegaGeometryPass::UpdateResidentSceneTables(FrameSlot &slot, const SceneTables &tables,
                                                     RHI::ICommandList *commandList, uint64_t pageTableBytes)
    {
        NORVES_PROFILE_SCOPE("MegaGeometryPass.UpdateResidentSceneTables");
        if (!m_Device || !commandList || !m_SceneScatterPipeline)
        {
            return false;
        }

        // この組を前に使った記録の GPU の仕事は終わっている。そのときの読み戻しを照合し、そのときのメッシュの区画の持ち主を
        // このフレームの記録が読むメッシュの持ち主に替える（替えた古い持ち主の区画は、GpuRetireQueue を通って空きへ戻る）
        ResolveSceneTableCheck(slot);
        slot.MeshRegionOwners.assign(tables.MeshOwners.begin(), tables.MeshOwners.end());
        m_bFrameSlotMeshOwnersHeld = m_bFrameSlotMeshOwnersHeld || !slot.MeshRegionOwners.empty();

        ++m_SceneUpdateStamp;
        if (m_SceneUpdateStamp == 0u)
        {
            // 番号が一周したら、古い番号を今の番号と取り違えないよう消してから 1 に戻す
            std::fill(m_SlotRowChangedStamps.begin(), m_SlotRowChangedStamps.end(), 0u);
            m_SceneUpdateStamp = 1u;
        }
        const uint32_t stamp = m_SceneUpdateStamp;
        const uint32_t entryCount = static_cast<uint32_t>(tables.Entries.size());
        const uint32_t meshCount = static_cast<uint32_t>(tables.Meshes.size());

        // ----------------------------------------
        // 1. 席の表: 確かめる行（Setup が作り直したなら全部、直したなら直した位置）の中身を写しと比べる
        // ----------------------------------------
        bool bAllRows = m_bSceneRowsAllDirty;
        uint32_t slotLimit = 0;
        if (bAllRows)
        {
            for (const MegaMeshInstance &instance : m_Instances)
            {
                slotLimit = std::max(slotLimit, instance.TableSlot + 1u);
            }
        }
        else
        {
            for (const uint32_t position : m_SceneDirtyPositions)
            {
                if (position < m_Instances.size())
                {
                    slotLimit = std::max(slotLimit, m_Instances[position].TableSlot + 1u);
                }
            }
        }
        // 描く順の行が指す席も収める（GPU が席の表の外を読まないように）
        for (const GPUMegaEntry &entry : tables.Entries)
        {
            slotLimit = std::max(slotLimit, entry.Slot + 1u);
        }
        bool bSlotTableGrown = false;
        bool bSlotTableCopied = false;
        if (!EnsureResidentTable(m_SlotTableBuffer, m_SlotTableCapacity, slotLimit, 1024u, sizeof(GPUMegaSlotRow),
                                 "MegaGeometry_SlotTable", commandList, bSlotTableGrown, bSlotTableCopied))
        {
            return false;
        }
        bool bCopiedAnyTable = bSlotTableCopied;
        if (bSlotTableGrown)
        {
            if (!bSlotTableCopied)
            {
                // 新しい席の表は中身を持たない。覚えている行は送り直すが、中身は変わっていない（描く順の表は組み直さない）
                for (uint8_t &state : m_SlotRowStates)
                {
                    if (state == 1u)
                    {
                        state = 2u;
                    }
                }
                bAllRows = true;
            }
            // コピーしたときは、古い表にあった行は新しい表の同じ位置にあるので、写しの状態をそのまま引き継ぐ
            m_SlotRowCopies.resize(m_SlotTableCapacity);
            m_SlotRowStates.resize(m_SlotTableCapacity, 0u);
            m_SlotRowChangedStamps.resize(m_SlotTableCapacity, 0u);
        }

        Container::VariableArray<uint32_t> rowSlots;
        const auto checkRow = [&](const MegaMeshInstance &instance) -> void
        {
            const uint32_t tableSlot = instance.TableSlot;
            GPUMegaSlotRow row;
            std::memcpy(row.WorldMatrix, instance.WorldMatrix, sizeof(row.WorldMatrix));
            std::memcpy(row.PreviousWorldMatrix, instance.PreviousWorldMatrix, sizeof(row.PreviousWorldMatrix));
            uint8_t &state = m_SlotRowStates[tableSlot];
            GPUMegaSlotRow &copy = m_SlotRowCopies[tableSlot];
            const bool bSame = state != 0u && std::memcmp(&copy, &row, sizeof(GPUMegaSlotRow)) == 0;
            if (bSame && state == 1u)
            {
                return;
            }
            if (!bSame)
            {
                copy = row;
                m_SlotRowChangedStamps[tableSlot] = stamp;
            }
            state = 1u;
            rowSlots.push_back(tableSlot);
        };
        if (bAllRows)
        {
            for (const MegaMeshInstance &instance : m_Instances)
            {
                checkRow(instance);
            }
        }
        else
        {
            for (const uint32_t position : m_SceneDirtyPositions)
            {
                if (position < m_Instances.size())
                {
                    checkRow(m_Instances[position]);
                }
            }
        }
        m_bSceneRowsAllDirty = false;
        m_SceneDirtyPositions.clear();

        // ここから先で失敗したら、写しを GPU の表に書いたものと見なせないので、次の更新が全部を送り直すようにする
        const auto invalidateCopies = [this]() -> void
        {
            for (uint8_t &state : m_SlotRowStates)
            {
                if (state == 1u)
                {
                    state = 2u;
                }
            }
            m_bSceneRowsAllDirty = true;
            m_EntryCopyCount = 0;
            m_ShadowRowCopyCount = 0;
            m_bMeshCopiesValid = false;
        };

        // ----------------------------------------
        // 2. 描く順の表・影の表: 並びの出所が変わった行と、席の行の中身が変わった行
        // ----------------------------------------
        bool bInstanceTableGrown = false;
        bool bInstanceTableCopied = false;
        if (!EnsureResidentTable(m_InstanceTableBuffer, m_InstanceTableCapacity, entryCount, 256u,
                                 sizeof(GPUMegaInstance), "MegaGeometry_InstanceTable", commandList, bInstanceTableGrown,
                                 bInstanceTableCopied))
        {
            invalidateCopies();
            return false;
        }
        uint32_t shadowCapacity = (m_ShadowTableBuffer && !bInstanceTableGrown) ? m_InstanceTableCapacity : 0u;
        bool bShadowTableGrown = false;
        bool bShadowTableCopied = false;
        if (!EnsureResidentTable(m_ShadowTableBuffer, shadowCapacity, m_InstanceTableCapacity, m_InstanceTableCapacity,
                                 sizeof(MegaGeometryShadowInstance), "MegaGeometry_ShadowInstanceTable", commandList,
                                 bShadowTableGrown, bShadowTableCopied))
        {
            invalidateCopies();
            return false;
        }
        bCopiedAnyTable = bCopiedAnyTable || bInstanceTableCopied || bShadowTableCopied;
        // コピーしたときは、古い表の行が新しい表の同じ位置にあるので、写しの行数を引き継ぐ
        if (bInstanceTableGrown)
        {
            if (!bInstanceTableCopied)
            {
                m_EntryCopyCount = 0;
            }
            m_EntryCopies.resize(m_InstanceTableCapacity);
        }
        if (bShadowTableGrown)
        {
            if (!bShadowTableCopied)
            {
                m_ShadowRowCopyCount = 0;
            }
            m_ShadowRowCopies.resize(m_InstanceTableCapacity);
        }

        Container::VariableArray<uint32_t> entryIndices;
        for (uint32_t index = 0; index < entryCount; ++index)
        {
            const GPUMegaEntry &entry = tables.Entries[index];
            GPUMegaEntry &copy = m_EntryCopies[index];
            const bool bSameEntry = index < m_EntryCopyCount && copy.Slot == entry.Slot && copy.MeshIndex == entry.MeshIndex &&
                                    copy.FirstGroup == entry.FirstGroup && copy.VisibleOffset == entry.VisibleOffset;
            if (bSameEntry && m_SlotRowChangedStamps[entry.Slot] != stamp)
            {
                continue;
            }
            copy = entry;
            entryIndices.push_back(index);
        }
        m_EntryCopyCount = entryCount;

        Container::VariableArray<uint32_t> shadowIndices;
        for (uint32_t index = 0; index < entryCount; ++index)
        {
            const MegaGeometryShadowInstance &shadow = tables.ShadowInstances[index];
            MegaGeometryShadowInstance &copy = m_ShadowRowCopies[index];
            if (index < m_ShadowRowCopyCount && std::memcmp(&copy, &shadow, sizeof(MegaGeometryShadowInstance)) == 0)
            {
                continue;
            }
            copy = shadow;
            shadowIndices.push_back(index);
        }
        m_ShadowRowCopyCount = entryCount;

        // ----------------------------------------
        // 3. メッシュの表: 中身が変わったら全部を書く（メッシュの数は少ない）
        // ----------------------------------------
        bool bMeshTableGrown = false;
        bool bMeshTableCopied = false;
        if (!EnsureResidentTable(m_MeshTableBuffer, m_MeshTableCapacity, meshCount, 16u, sizeof(GPUMegaMesh),
                                 "MegaGeometry_MeshTable", commandList, bMeshTableGrown, bMeshTableCopied))
        {
            invalidateCopies();
            return false;
        }
        bCopiedAnyTable = bCopiedAnyTable || bMeshTableCopied;
        const bool bMeshesChanged = (bMeshTableGrown && !bMeshTableCopied) || !m_bMeshCopiesValid || m_MeshCopies.size() != tables.Meshes.size() ||
                                    (meshCount > 0 &&
                                     std::memcmp(m_MeshCopies.data(), tables.Meshes.data(), meshCount * sizeof(GPUMegaMesh)) != 0);
        const uint32_t meshRecords = bMeshesChanged ? meshCount : 0u;
        if (bMeshesChanged)
        {
            m_MeshCopies.assign(tables.Meshes.begin(), tables.Meshes.end());
            m_bMeshCopiesValid = true;
        }

        // ----------------------------------------
        // 3b. 範囲の境界: 描く順の行が変わった範囲だけを作り直す（メッシュの表が変わったら、境界の球が変わりうるので全部）
        // ----------------------------------------
        const uint32_t rangeCount = (entryCount + BvhRangeSeats - 1u) / BvhRangeSeats;
        bool bRangeTableGrown = false;
        bool bRangeTableCopied = false;
        if (!EnsureResidentTable(m_RangeBoundsBuffer, m_RangeBoundsCapacity, rangeCount, 16u, RangeBoundsBytes,
                                 "MegaGeometry_RangeBounds", commandList, bRangeTableGrown, bRangeTableCopied))
        {
            invalidateCopies();
            return false;
        }
        Container::VariableArray<uint32_t> dirtyRanges;
        if ((bRangeTableGrown && !bRangeTableCopied) || meshRecords > 0)
        {
            dirtyRanges.reserve(rangeCount);
            for (uint32_t range = 0; range < rangeCount; ++range)
            {
                dirtyRanges.push_back(range);
            }
        }
        else
        {
            // entryIndices は昇順なので、同じ範囲は続けて並ぶ
            for (const uint32_t index : entryIndices)
            {
                const uint32_t range = index / BvhRangeSeats;
                if (dirtyRanges.empty() || dirtyRanges.back() != range)
                {
                    dirtyRanges.push_back(range);
                }
            }
            // 行が減ったら、末尾の範囲から消えた行の境界を外す（行の中身が変わっていなくても作り直す）
            if (entryCount > 0 && entryCount < m_RangeBoundsRowCount)
            {
                const uint32_t lastRange = (entryCount - 1u) / BvhRangeSeats;
                if (dirtyRanges.empty() || dirtyRanges.back() != lastRange)
                {
                    dirtyRanges.push_back(lastRange);
                }
            }
        }
        m_RangeBoundsRowCount = entryCount;

        // ----------------------------------------
        // 4. 記録の列を staging へ書き、散在書き込みを記録する（書くものが無ければ何もしない）
        // ----------------------------------------
        constexpr uint32_t HeaderWords = 4u;
        constexpr uint32_t RowRecordWords = 4u + 32u;    // 席の番号の uvec4 + 席の行（128 B）
        constexpr uint32_t EntryRecordWords = 8u;        // 2 語の uvec4
        constexpr uint32_t ShadowRecordWords = 4u + 12u; // 描く順の番号の uvec4 + 影の表の行（48 B）
        constexpr uint32_t MeshRecordWords = 4u + 20u;   // メッシュの表の番号の uvec4 + メッシュの表の行（80 B）
        constexpr uint32_t RegionAlignWords = 64u;       // 区画の先頭を 256 B にそろえる（storage のオフセットの整列）
        static_assert(sizeof(GPUMegaSlotRow) == 32u * sizeof(uint32_t));
        static_assert(sizeof(MegaGeometryShadowInstance) == 12u * sizeof(uint32_t));
        static_assert(sizeof(GPUMegaMesh) == 20u * sizeof(uint32_t));

        const uint32_t rowCount = static_cast<uint32_t>(rowSlots.size());
        const uint32_t entryRecords = static_cast<uint32_t>(entryIndices.size());
        const uint32_t shadowRecords = static_cast<uint32_t>(shadowIndices.size());
        const uint64_t rowRegionWords = rowCount > 0 ? HeaderWords + static_cast<uint64_t>(rowCount) * RowRecordWords : 0u;
        const uint64_t tableRecords = static_cast<uint64_t>(entryRecords) + shadowRecords + meshRecords;
        const uint64_t tableRegionWords =
            tableRecords > 0 ? HeaderWords + static_cast<uint64_t>(entryRecords) * EntryRecordWords +
                                   static_cast<uint64_t>(shadowRecords) * ShadowRecordWords +
                                   static_cast<uint64_t>(meshRecords) * MeshRecordWords
                             : 0u;
        const uint64_t tableRegionOffsetWords = (rowRegionWords + RegionAlignWords - 1u) / RegionAlignWords * RegionAlignWords;
        // 段 2 の記録: 作り直す範囲の番号を 1 語ずつ（uvec4 に 4 つ詰める）
        const uint32_t rangeRecords = static_cast<uint32_t>(dirtyRanges.size());
        const uint64_t rangeRegionWords =
            rangeRecords > 0 ? HeaderWords + (static_cast<uint64_t>(rangeRecords) + 3u) / 4u * 4u : 0u;
        const uint64_t rangeRegionOffsetWords =
            (tableRegionOffsetWords + tableRegionWords + RegionAlignWords - 1u) / RegionAlignWords * RegionAlignWords;
        // 段 3 の区画: ヘッダだけ（メッシュの表を書き直したときに、全メッシュの締まった球の半径を広げる。メッシュの表の番号は
        // ワークグループの通し番号）
        const uint64_t boundsRegionWords = meshRecords > 0 ? HeaderWords : 0u;
        const uint64_t boundsRegionOffsetWords =
            (rangeRegionOffsetWords + rangeRegionWords + RegionAlignWords - 1u) / RegionAlignWords * RegionAlignWords;
        const uint64_t uploadBytes = (rowRegionWords + tableRegionWords + rangeRegionWords + boundsRegionWords) * sizeof(uint32_t);
        if (bCopiedAnyTable)
        {
            ++m_SceneUploadStats.Grows;
        }
        if (rowRegionWords == 0 && tableRegionWords == 0 && rangeRegionWords == 0)
        {
            if (bCopiedAnyTable)
            {
                RecordSceneTableCheck(slot, commandList, entryCount, meshCount);
            }
            NoteSceneUpload(pageTableBytes, 0u, 0u, 0u, 0u, pageTableBytes);
            return true;
        }

        const uint64_t totalWords = boundsRegionWords > 0  ? boundsRegionOffsetWords + boundsRegionWords
                                    : rangeRegionWords > 0 ? rangeRegionOffsetWords + rangeRegionWords
                                                           : tableRegionOffsetWords + tableRegionWords;
        if (totalWords * sizeof(uint32_t) > 0xFFFFFFFFull)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_SCENE_UPLOAD_TOO_LARGE bytes=%llu",
                             static_cast<unsigned long long>(totalWords * sizeof(uint32_t)));
            invalidateCopies();
            return false;
        }
        m_SceneScatterWords.assign(static_cast<size_t>(totalWords), 0u);
        uint32_t *words = m_SceneScatterWords.data();
        if (rowRegionWords > 0)
        {
            words[0] = rowCount;
            words[1] = 0u; // 段 0
            uint32_t *record = words + HeaderWords;
            for (const uint32_t tableSlot : rowSlots)
            {
                record[0] = tableSlot;
                std::memcpy(record + 4u, &m_SlotRowCopies[tableSlot], sizeof(GPUMegaSlotRow));
                record += RowRecordWords;
            }
        }
        if (tableRegionWords > 0)
        {
            uint32_t *region = words + tableRegionOffsetWords;
            region[0] = entryRecords;
            region[1] = 1u; // 段 1
            region[2] = shadowRecords;
            region[3] = meshRecords;
            uint32_t *record = region + HeaderWords;
            for (const uint32_t index : entryIndices)
            {
                const GPUMegaEntry &entry = tables.Entries[index];
                record[0] = index;
                record[1] = entry.Slot;
                record[2] = entry.MeshIndex;
                record[3] = entry.FirstGroup;
                record[4] = entry.VisibleOffset;
                record += EntryRecordWords;
            }
            for (const uint32_t index : shadowIndices)
            {
                record[0] = index;
                std::memcpy(record + 4u, &tables.ShadowInstances[index], sizeof(MegaGeometryShadowInstance));
                record += ShadowRecordWords;
            }
            for (uint32_t meshIndex = 0; meshIndex < meshRecords; ++meshIndex)
            {
                record[0] = meshIndex;
                std::memcpy(record + 4u, &tables.Meshes[meshIndex], sizeof(GPUMegaMesh));
                record += MeshRecordWords;
            }
        }
        if (rangeRegionWords > 0)
        {
            uint32_t *region = words + rangeRegionOffsetWords;
            region[0] = rangeRecords;
            region[1] = 2u; // 段 2
            region[2] = entryCount;
            std::memcpy(region + HeaderWords, dirtyRanges.data(), static_cast<size_t>(rangeRecords) * sizeof(uint32_t));
        }
        if (boundsRegionWords > 0)
        {
            uint32_t *region = words + boundsRegionOffsetWords;
            region[0] = meshRecords;
            region[1] = 3u; // 段 3
        }

        // staging はフレームスロットのもの（この組を前に使ったフレームの GPU の仕事は終わっている）
        const uint64_t stagingBytes = totalWords * sizeof(uint32_t);
        if (!slot.SceneStaging || slot.SceneStagingCapacity < stagingBytes)
        {
            // 2 GiB を超える大きさは 2 の冪へ丸めると 32 ビットで溢れるので、そのときは必要な大きさのまま取る
            const uint64_t rounded = stagingBytes <= 0x80000000ull ? NextPowerOfTwo(static_cast<uint32_t>(stagingBytes))
                                                                   : stagingBytes;
            const uint64_t capacity = std::max<uint64_t>(64ull * 1024ull, rounded);
            RHI::BufferDesc desc(capacity, RHI::ResourceUsage::StorageBuffer, true, "MegaGeometry_SceneStaging");
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                invalidateCopies();
                return false;
            }
            slot.SceneStaging = buffer;
            slot.SceneStagingCapacity = capacity;
        }
        slot.SceneStaging->Update(m_SceneScatterWords.data(), stagingBytes);

        const RHI::DescriptorSetDesc scatterDesc = BuildSceneScatterDescriptorSetDesc();
        for (RHI::DescriptorSetPtr &descriptorSet : slot.SceneScatterDescriptorSet)
        {
            if (!descriptorSet)
            {
                RHI::FrameResourceProtectedCreationScope protectedCreation;
                descriptorSet = m_Device->CreateDescriptorSet(scatterDesc);
                if (!descriptorSet)
                {
                    invalidateCopies();
                    return false;
                }
            }
        }
        const auto bindTables = [this, &slot](const RHI::DescriptorSetPtr &descriptorSet, uint64_t offsetWords,
                                              uint64_t sizeWords) -> void
        {
            descriptorSet->BindStorageBuffer(0, slot.SceneStaging, static_cast<uint32_t>(offsetWords * sizeof(uint32_t)),
                                             static_cast<uint32_t>(sizeWords * sizeof(uint32_t)));
            descriptorSet->BindStorageBuffer(1, m_SlotTableBuffer, 0, static_cast<uint32_t>(m_SlotTableBuffer->GetSize()));
            descriptorSet->BindStorageBuffer(2, m_InstanceTableBuffer, 0,
                                             static_cast<uint32_t>(m_InstanceTableBuffer->GetSize()));
            descriptorSet->BindStorageBuffer(3, m_ShadowTableBuffer, 0, static_cast<uint32_t>(m_ShadowTableBuffer->GetSize()));
            descriptorSet->BindStorageBuffer(4, m_MeshTableBuffer, 0, static_cast<uint32_t>(m_MeshTableBuffer->GetSize()));
            descriptorSet->BindStorageBuffer(5, m_RangeBoundsBuffer, 0, static_cast<uint32_t>(m_RangeBoundsBuffer->GetSize()));
            descriptorSet->Update();
        };
        const auto dispatchRecords = [commandList](uint64_t records) -> void
        {
            const uint64_t groups = (records + 63u) / 64u;
            const uint32_t dispatchX = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535u));
            const uint32_t dispatchY = static_cast<uint32_t>((groups + dispatchX - 1u) / dispatchX);
            commandList->Dispatch(dispatchX, dispatchY, 1);
        };

        // 前のフレームの読み手（カリング・頂点・解決・VSM）が読み終えてから書き、書き終えてから読ませる。
        // 同じキューの記録の順なので、パイプラインのバリアで足りる
        const RHI::BufferPtr residentTables[] = {m_SlotTableBuffer, m_InstanceTableBuffer, m_ShadowTableBuffer, m_MeshTableBuffer,
                                                 m_RangeBoundsBuffer};
        for (const RHI::BufferPtr &table : residentTables)
        {
            commandList->BufferBarrier(table, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        }
        commandList->SetPipeline(m_SceneScatterPipeline);
        if (rowRegionWords > 0)
        {
            bindTables(slot.SceneScatterDescriptorSet[0], 0u, rowRegionWords);
            commandList->SetDescriptorSet(slot.SceneScatterDescriptorSet[0], 0);
            dispatchRecords(rowCount);
            // 段 1 が席の表から描く順の表を組むので、段 0 の書き込みを見せる
            commandList->BufferBarrier(m_SlotTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        }
        if (tableRegionWords > 0)
        {
            bindTables(slot.SceneScatterDescriptorSet[1], tableRegionOffsetWords, tableRegionWords);
            commandList->SetDescriptorSet(slot.SceneScatterDescriptorSet[1], 0);
            dispatchRecords(tableRecords);
        }
        if (boundsRegionWords > 0)
        {
            // 段 3 が段 1 の写したメッシュの行（クラスタ配列のアドレス・ホストの球）を読んで半径を広げるので、段 1 の書き込みを見せる。
            // 段 2 はこの後のバリアの後に、広げた半径を読む
            commandList->BufferBarrier(m_MeshTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            bindTables(slot.SceneScatterDescriptorSet[3], boundsRegionOffsetWords, boundsRegionWords);
            commandList->SetDescriptorSet(slot.SceneScatterDescriptorSet[3], 0);
            // 1 ワークグループ（64 スレッド）= 1 メッシュ
            dispatchRecords(static_cast<uint64_t>(meshRecords) * 64u);
        }
        if (rangeRegionWords > 0)
        {
            // 段 2 が段 1 の書いた描く順の表とメッシュの表から範囲の境界を作るので、段 1 の書き込みを見せる
            commandList->BufferBarrier(m_InstanceTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            commandList->BufferBarrier(m_MeshTableBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            bindTables(slot.SceneScatterDescriptorSet[2], rangeRegionOffsetWords, rangeRegionWords);
            commandList->SetDescriptorSet(slot.SceneScatterDescriptorSet[2], 0);
            // 1 ワークグループ = 1 範囲
            dispatchRecords(static_cast<uint64_t>(rangeRecords) * BvhRangeSeats);
        }
        for (const RHI::BufferPtr &table : residentTables)
        {
            commandList->BufferBarrier(table, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        }
        if (bCopiedAnyTable)
        {
            RecordSceneTableCheck(slot, commandList, entryCount, meshCount);
        }

        NoteSceneUpload(uploadBytes + pageTableBytes, rowCount, entryRecords, shadowRecords, meshRecords, pageTableBytes);
        return true;
    }

    void MegaGeometryPass::NoteSceneUpload(uint64_t bytes, uint32_t rows, uint32_t entries, uint32_t shadows,
                                           uint32_t meshes, uint64_t pageTableBytes)
    {
        SceneUploadStats &stats = m_SceneUploadStats;
        ++stats.Updates;
        if (bytes == 0)
        {
            ++stats.ZeroUpdates;
        }
        stats.WindowBytes += bytes;
        if (stats.bLoggedOnce && stats.Updates < 120u)
        {
            return;
        }

#if NORVES_BUILD_DEVELOPMENT
        // 変わった席の数え漏れの検査: 差分を書いた直後なので、全インスタンスの席の行の写しが m_Instances と一致し、
        // GPU の表にある（状態 1）こと。Setup が直した位置を記録し損ねると、ここで食い違う
        uint32_t mismatches = 0;
        for (const MegaMeshInstance &instance : m_Instances)
        {
            const uint32_t tableSlot = instance.TableSlot;
            if (tableSlot >= m_SlotRowStates.size() || m_SlotRowStates[tableSlot] != 1u ||
                std::memcmp(m_SlotRowCopies[tableSlot].WorldMatrix, instance.WorldMatrix, sizeof(instance.WorldMatrix)) != 0 ||
                std::memcmp(m_SlotRowCopies[tableSlot].PreviousWorldMatrix, instance.PreviousWorldMatrix,
                            sizeof(instance.PreviousWorldMatrix)) != 0)
            {
                ++mismatches;
            }
        }
        if (mismatches > 0)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "MEGA_SCENE_UPLOAD_MISMATCH mismatch=%u instances=%zu 席の表の写しが"
                                                 "インスタンスと一致しません（変わった席の数え漏れ）",
                             mismatches, m_Instances.size());
        }
        // 表を広げた後の GPU の表の読み戻しの照合（窓の間に食い違った行）も、数え漏れと同じ食い違いとして数える
        stats.Mismatches = mismatches + stats.GpuMismatches;
#endif

        // bytes は、この更新で常駐の表とページの表へ書いたバイト数。インスタンスも常駐も変わらない更新は 0。
        // grows は表を広げて古い表をコピーした更新の数、gpu_checks はその後の読み戻しを照合した数（窓の間）
        NORVES_LOG_INFO("MegaGeometryPass",
                        "MEGA_SCENE_UPLOAD bytes=%llu rows=%u entries=%u shadows=%u meshes=%u page_bytes=%llu "
                        "updates=%u zero_updates=%u window_bytes=%llu slot_capacity=%u instance_capacity=%u "
                        "grows=%u gpu_checks=%u gpu_mismatch=%u mismatch=%u",
                        static_cast<unsigned long long>(bytes),
                        rows,
                        entries,
                        shadows,
                        meshes,
                        static_cast<unsigned long long>(pageTableBytes),
                        stats.Updates,
                        stats.ZeroUpdates,
                        static_cast<unsigned long long>(stats.WindowBytes),
                        m_SlotTableCapacity,
                        m_InstanceTableCapacity,
                        stats.Grows,
                        stats.GpuChecks,
                        stats.GpuMismatches,
                        stats.Mismatches);
        stats.bLoggedOnce = true;
        stats.Updates = 0;
        stats.ZeroUpdates = 0;
        stats.WindowBytes = 0;
        stats.Grows = 0;
        stats.GpuChecks = 0;
        stats.GpuMismatches = 0;
    }

    void MegaGeometryPass::RecordSceneTableCheck(FrameSlot &slot, RHI::ICommandList *commandList, uint32_t entryCount,
                                                 uint32_t meshCount)
    {
#if NORVES_BUILD_DEVELOPMENT
        // 表ごとに先頭のこの行数までを見る（席が非常に多いシーンで、読み戻しと写しが大きくなりすぎないように）
        constexpr uint32_t SceneCheckMaxRows = 16384u;
        if (!m_Device || !commandList || !m_SlotTableBuffer || !m_InstanceTableBuffer || !m_ShadowTableBuffer ||
            !m_MeshTableBuffer)
        {
            return;
        }
        const uint32_t slotRows = std::min(m_SlotTableCapacity, SceneCheckMaxRows);
        const uint32_t entryRows = std::min(entryCount, SceneCheckMaxRows);
        const uint32_t meshRows = std::min(meshCount, SceneCheckMaxRows);
        const uint64_t slotBytes = static_cast<uint64_t>(slotRows) * sizeof(GPUMegaSlotRow);
        const uint64_t instanceBytes = static_cast<uint64_t>(entryRows) * sizeof(GPUMegaInstance);
        const uint64_t shadowBytes = static_cast<uint64_t>(entryRows) * sizeof(MegaGeometryShadowInstance);
        const uint64_t meshBytes = static_cast<uint64_t>(meshRows) * sizeof(GPUMegaMesh);
        const uint64_t totalBytes = slotBytes + instanceBytes + shadowBytes + meshBytes;
        if (totalBytes == 0)
        {
            return;
        }
        if (!slot.SceneCheckReadback || slot.SceneCheckCapacity < totalBytes)
        {
            RHI::BufferDesc desc(totalBytes, RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst, true,
                                 "MegaGeometry_SceneCheckReadback");
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            slot.SceneCheckReadback = m_Device->CreateBuffer(desc);
            slot.SceneCheckCapacity = slot.SceneCheckReadback ? totalBytes : 0u;
            if (!slot.SceneCheckReadback)
            {
                return;
            }
        }

        // 期待する中身: 席の表は写し（GPU の表にある行だけ照合する）、描く順の表は写しの出所と席の行から
        // mega_scene_scatter.comp と同じ並び（席の行 8 語 + (メッシュの表の番号, 最初のワークグループ, 「見えた」印の先頭, 0)）
        slot.SceneCheckExpected.assign(static_cast<size_t>(totalBytes), static_cast<uint8_t>(0));
        slot.SceneCheckSlotMask.assign(slotRows, static_cast<uint8_t>(0));
        uint8_t *expected = slot.SceneCheckExpected.data();
        for (uint32_t row = 0; row < slotRows && row < m_SlotRowStates.size(); ++row)
        {
            if (m_SlotRowStates[row] == 1u)
            {
                slot.SceneCheckSlotMask[row] = 1u;
                std::memcpy(expected + row * sizeof(GPUMegaSlotRow), &m_SlotRowCopies[row], sizeof(GPUMegaSlotRow));
            }
        }
        expected += slotBytes;
        for (uint32_t row = 0; row < entryRows; ++row)
        {
            const GPUMegaEntry &entry = m_EntryCopies[row];
            uint32_t words[sizeof(GPUMegaInstance) / sizeof(uint32_t)] = {};
            if (entry.Slot < m_SlotRowCopies.size())
            {
                std::memcpy(words, &m_SlotRowCopies[entry.Slot], sizeof(GPUMegaSlotRow));
            }
            words[32] = entry.MeshIndex;
            words[33] = entry.FirstGroup;
            words[34] = entry.VisibleOffset;
            std::memcpy(expected + row * sizeof(GPUMegaInstance), words, sizeof(GPUMegaInstance));
        }
        expected += instanceBytes;
        if (entryRows > 0)
        {
            std::memcpy(expected, m_ShadowRowCopies.data(), shadowBytes);
        }
        expected += shadowBytes;
        if (meshRows > 0)
        {
            std::memcpy(expected, m_MeshCopies.data(), meshBytes);
        }

        // 常駐の表は読み取りの状態（GenericRead はコピー元を含む）。読み戻しはホストが前に読んだ後
        commandList->BufferBarrier(slot.SceneCheckReadback, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
        uint64_t offset = 0;
        const auto copyTable = [&](const RHI::BufferPtr &table, uint64_t bytes) -> void
        {
            if (bytes > 0)
            {
                commandList->CopyBuffer(table, slot.SceneCheckReadback, bytes, 0, offset);
            }
            offset += bytes;
        };
        copyTable(m_SlotTableBuffer, slotBytes);
        copyTable(m_InstanceTableBuffer, instanceBytes);
        copyTable(m_ShadowTableBuffer, shadowBytes);
        copyTable(m_MeshTableBuffer, meshBytes);
        commandList->BufferBarrier(slot.SceneCheckReadback, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
        slot.SceneCheckSlotRows = slotRows;
        slot.SceneCheckEntryRows = entryRows;
        slot.SceneCheckMeshRows = meshRows;
        slot.bSceneCheckPending = true;
#else
        (void)slot;
        (void)commandList;
        (void)entryCount;
        (void)meshCount;
#endif
    }

    void MegaGeometryPass::ResolveSceneTableCheck(FrameSlot &slot)
    {
#if NORVES_BUILD_DEVELOPMENT
        if (!slot.bSceneCheckPending)
        {
            return;
        }
        slot.bSceneCheckPending = false;
        const uint64_t slotBytes = static_cast<uint64_t>(slot.SceneCheckSlotRows) * sizeof(GPUMegaSlotRow);
        const uint64_t instanceBytes = static_cast<uint64_t>(slot.SceneCheckEntryRows) * sizeof(GPUMegaInstance);
        const uint64_t shadowBytes = static_cast<uint64_t>(slot.SceneCheckEntryRows) * sizeof(MegaGeometryShadowInstance);
        const uint64_t meshBytes = static_cast<uint64_t>(slot.SceneCheckMeshRows) * sizeof(GPUMegaMesh);
        const uint64_t totalBytes = slotBytes + instanceBytes + shadowBytes + meshBytes;
        if (!slot.SceneCheckReadback || totalBytes == 0 || slot.SceneCheckExpected.size() < totalBytes)
        {
            return;
        }
        const uint8_t *mapped = static_cast<const uint8_t *>(slot.SceneCheckReadback->Map(0, totalBytes));
        if (!mapped)
        {
            return;
        }
        const uint8_t *expected = slot.SceneCheckExpected.data();
        uint32_t slotMismatches = 0;
        for (uint32_t row = 0; row < slot.SceneCheckSlotRows; ++row)
        {
            const uint64_t at = static_cast<uint64_t>(row) * sizeof(GPUMegaSlotRow);
            if (slot.SceneCheckSlotMask[row] != 0u && std::memcmp(mapped + at, expected + at, sizeof(GPUMegaSlotRow)) != 0)
            {
                ++slotMismatches;
            }
        }
        // compareBytes は行の先頭から比べるバイト数（行の大きさ以下）
        const auto countRows = [&](uint64_t base, uint32_t rows, uint64_t rowBytes, uint64_t compareBytes) -> uint32_t
        {
            uint32_t count = 0;
            for (uint32_t row = 0; row < rows; ++row)
            {
                const uint64_t at = base + static_cast<uint64_t>(row) * rowBytes;
                if (std::memcmp(mapped + at, expected + at, static_cast<size_t>(compareBytes)) != 0)
                {
                    ++count;
                }
            }
            return count;
        };
        const uint32_t entryMismatches =
            countRows(slotBytes, slot.SceneCheckEntryRows, sizeof(GPUMegaInstance), sizeof(GPUMegaInstance));
        const uint32_t shadowMismatches = countRows(slotBytes + instanceBytes, slot.SceneCheckEntryRows,
                                                    sizeof(MegaGeometryShadowInstance), sizeof(MegaGeometryShadowInstance));
        // メッシュの行の末尾の語（締まった球の半径）は mega_scene_scatter.comp の段 3 が GPU で広げるので、写しと比べない
        const uint32_t meshMismatches = countRows(slotBytes + instanceBytes + shadowBytes, slot.SceneCheckMeshRows,
                                                  sizeof(GPUMegaMesh), offsetof(GPUMegaMesh, BoundsSphere) + 3u * sizeof(float));
        slot.SceneCheckReadback->Unmap();

        const uint32_t mismatches = slotMismatches + entryMismatches + shadowMismatches + meshMismatches;
        ++m_SceneUploadStats.GpuChecks;
        m_SceneUploadStats.GpuMismatches += mismatches;
        if (mismatches > 0)
        {
            NORVES_LOG_ERROR("MegaGeometryPass",
                             "MEGA_SCENE_GROW_CHECK slot_rows=%u entry_rows=%u mesh_rows=%u slot_mismatch=%u "
                             "entry_mismatch=%u shadow_mismatch=%u mesh_mismatch=%u mismatch=%u "
                             "広げた常駐の表の中身が写しと一致しません",
                             slot.SceneCheckSlotRows, slot.SceneCheckEntryRows, slot.SceneCheckMeshRows, slotMismatches,
                             entryMismatches, shadowMismatches, meshMismatches, mismatches);
        }
        else
        {
            NORVES_LOG_INFO("MegaGeometryPass",
                            "MEGA_SCENE_GROW_CHECK slot_rows=%u entry_rows=%u mesh_rows=%u mismatch=0",
                            slot.SceneCheckSlotRows, slot.SceneCheckEntryRows, slot.SceneCheckMeshRows);
        }
#else
        (void)slot;
#endif
    }

    bool MegaGeometryPass::EnsureFrameSlot(FrameSlot &slot,
                                           uint32_t sectionTableEntries,
                                           uint32_t sectionCount)
    {
        // FrameUseRing の枠（FrameSlot）の資源。Acquire と作成が別の区間（準備の段と記録の段）になることがあるので、
        // 作る所で守られていない上書きの見張りへ「守られている」と登録する
        RHI::FrameResourceProtectedCreationScope protectedCreation;
        if (!m_Device)
        {
            return false;
        }

        if (!slot.SectionBuffer || slot.SectionCapacity < sectionTableEntries)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(sectionTableEntries));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * 2u * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "MegaGeometry_SectionTable");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.SectionBuffer = buffer;
            slot.SectionCapacity = capacity;
        }

        const RHI::DescriptorSetDesc cullDsDesc = BuildCullDescriptorSetDesc();
        for (uint32_t passIndex = 0; passIndex < 2; ++passIndex)
        {
            if (!slot.CullUniform[passIndex])
            {
                RHI::BufferDesc desc(sizeof(CullUniformData),
                                     RHI::ResourceUsage::ConstantBuffer,
                                     true,
                                     "MegaGeometry_CullUBO");
                slot.CullUniform[passIndex] = m_Device->CreateBuffer(desc);
            }
            if (!slot.CullDescriptorSet[passIndex])
            {
                slot.CullDescriptorSet[passIndex] = m_Device->CreateDescriptorSet(cullDsDesc);
            }
            if (!slot.CullUniform[passIndex] || !slot.CullDescriptorSet[passIndex])
            {
                return false;
            }
        }

        if (slot.Sections.size() < sectionCount)
        {
            const RHI::DescriptorSetDesc drawDsDesc = BuildDrawDescriptorSetDesc();
            while (slot.Sections.size() < sectionCount)
            {
                RHI::BufferDesc desc(SectionUniformBufferBytes,
                                     RHI::ResourceUsage::ConstantBuffer,
                                     true,
                                     "MegaGeometry_DrawUBO");
                SectionDraw sectionDraw;
                sectionDraw.Uniform = m_Device->CreateBuffer(desc);
                sectionDraw.DescriptorSet = m_Device->CreateDescriptorSet(drawDsDesc);
                if (!sectionDraw.Uniform || !sectionDraw.DescriptorSet)
                {
                    return false;
                }
                slot.Sections.push_back(sectionDraw);
            }
        }

        return true;
    }

    bool MegaGeometryPass::SyncPageTable(FrameSlot &slot, MegaGeometryResources &resources, uint64_t *outBytes)
    {
        if (outBytes)
        {
            *outBytes = 0;
        }
        if (!m_Device)
        {
            return false;
        }

        // バッファが無いスロットは、どの版とも一致しない版から始めて、必ず1回は書く
        uint64_t version = slot.PageTableBuffer ? slot.PageTableVersion : ~0ull;
        Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> entries;
        const bool bChanged = resources.CopyPageTableIfChanged(version, entries);
        if (slot.PageTableBuffer && !bChanged)
        {
            return true;
        }

        // この組を直前に使ったのは前のフレーム（飛行中のフレームの番号が同じ）で、そのGPUの仕事は終わっているので、作り直して置き換えてよい
        const uint32_t entryCount = static_cast<uint32_t>(entries.size());
        if (!slot.PageTableBuffer || slot.PageTableCapacity < entryCount)
        {
            const uint32_t capacity = std::max(64u, NextPowerOfTwo(entryCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * sizeof(MegaGeometry::GeometryPageTable::Entry),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "MegaGeometry_PageTable");
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.PageTableBuffer = buffer;
            slot.PageTableCapacity = capacity;
        }
        if (entryCount > 0)
        {
            slot.PageTableBuffer->Update(entries.data(),
                                         static_cast<size_t>(entryCount) * sizeof(MegaGeometry::GeometryPageTable::Entry));
            if (outBytes)
            {
                *outBytes = static_cast<uint64_t>(entryCount) * sizeof(MegaGeometry::GeometryPageTable::Entry);
            }
        }
        slot.PageTableVersion = version;
        return true;
    }

    // ========================================
    // GBuffer互換グラフィックスパイプライン作成
    // ========================================

    bool MegaGeometryPass::CreateDrawPipeline(ViewRenderContext &context,
                                              bool bRequireDrawPipeline,
                                              bool bUseRenderGraphAttachmentStates)
    {
        if (!m_Device)
        {
            return false;
        }

        // GBuffer互換レンダーパス作成（Load既存内容）
        RHI::RenderPassDesc rpDesc;
        const RHI::ResourceState colorInitialState = bUseRenderGraphAttachmentStates
                                                         ? RHI::ResourceState::RenderTarget
                                                         : RHI::ResourceState::ShaderResource;
        const RHI::ResourceState depthInitialState = bUseRenderGraphAttachmentStates
                                                         ? RHI::ResourceState::DepthWrite
                                                         : RHI::ResourceState::ShaderResource;

        // Albedo: Load（GBufferPassで書いた内容を保持）
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = RHI::Format::R8G8B8A8_UNORM;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = false;
        albedoAttach.loadOp = RHI::AttachmentLoadOp::Load;
        albedoAttach.storeOp = RHI::AttachmentStoreOp::Store;
        albedoAttach.initialState = colorInitialState;
        albedoAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal: Load
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = false;
        normalAttach.loadOp = RHI::AttachmentLoadOp::Load;
        normalAttach.storeOp = RHI::AttachmentStoreOp::Store;
        normalAttach.initialState = colorInitialState;
        normalAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material: Load
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = RHI::Format::R8G8B8A8_UNORM;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = false;
        materialAttach.loadOp = RHI::AttachmentLoadOp::Load;
        materialAttach.storeOp = RHI::AttachmentStoreOp::Store;
        materialAttach.initialState = colorInitialState;
        materialAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Emissive: Load
        RHI::AttachmentDesc emissiveAttach;
        emissiveAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        emissiveAttach.isDepthStencil = false;
        emissiveAttach.clear = false;
        emissiveAttach.loadOp = RHI::AttachmentLoadOp::Load;
        emissiveAttach.storeOp = RHI::AttachmentStoreOp::Store;
        emissiveAttach.initialState = colorInitialState;
        emissiveAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(emissiveAttach);

        // Velocity: Load
        RHI::AttachmentDesc velocityAttach;
        velocityAttach.format = m_VelocityTexture ? m_VelocityTexture->GetFormat() : RHI::Format::R16G16_FLOAT;
        velocityAttach.isDepthStencil = false;
        velocityAttach.clear = false;
        velocityAttach.loadOp = RHI::AttachmentLoadOp::Load;
        velocityAttach.storeOp = RHI::AttachmentStoreOp::Store;
        velocityAttach.initialState = colorInitialState;
        velocityAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(velocityAttach);

        // Depth: Load + DepthTest
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = false;
        rpDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        rpDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        rpDesc.depthStencilAttachment.initialState = depthInitialState;
        rpDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer互換レンダーパスの作成に失敗");
            return false;
        }

        // フレームバッファ作成（GBufferPassと同じテクスチャを参照）
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.colorTargets.push_back(m_EmissiveTexture);
        fbDesc.colorTargets.push_back(m_VelocityTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = m_CurrentWidth;
        fbDesc.height = m_CurrentHeight;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "フレームバッファの作成に失敗");
            return false;
        }

        // 2パス目のレンダーパス・フレームバッファ。1パス目の描画の後に続けて開くので、全てのアタッチメントが
        // 1パス目の終わりの状態（ShaderResource）から始まる。作れなければ2パスの遮蔽カリングは使わない。
        m_SecondGBufferRenderPass.reset();
        m_SecondGBufferFramebuffer.reset();
        {
            RHI::RenderPassDesc secondRenderPassDesc = rpDesc;
            for (RHI::AttachmentDesc &attachment : secondRenderPassDesc.colorAttachments)
            {
                attachment.initialState = RHI::ResourceState::ShaderResource;
            }
            secondRenderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::ShaderResource;

            RHI::RenderPassPtr secondRenderPass = m_Device->CreateRenderPass(secondRenderPassDesc);
            if (secondRenderPass)
            {
                RHI::FramebufferDesc secondFbDesc = fbDesc;
                secondFbDesc.renderPass = secondRenderPass;
                RHI::FramebufferPtr secondFramebuffer = m_Device->CreateFramebuffer(secondFbDesc);
                if (secondFramebuffer)
                {
                    m_SecondGBufferRenderPass = secondRenderPass;
                    m_SecondGBufferFramebuffer = secondFramebuffer;
                }
            }
            if (!m_SecondGBufferRenderPass || !m_SecondGBufferFramebuffer)
            {
                NORVES_LOG_WARNING("MegaGeometryPass", "2パス目のGBuffer互換レンダーパスの作成に失敗。"
                                                       "遮蔽カリングは使いません");
            }
        }

        m_bGBufferRenderPassUsesRenderGraphAttachmentStates = bUseRenderGraphAttachmentStates;

        if (!bRequireDrawPipeline)
        {
            m_DrawPipeline.reset();
            m_DrawWireframePipeline.reset();
            return true;
        }

        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            m_DrawPipeline.reset();
            m_DrawWireframePipeline.reset();
            return false;
        }

        m_DrawPipeline.reset();
        m_DrawWireframePipeline.reset();

        if (!CreateDrawPipelineVariant(RHI::PolygonMode::Fill, m_DrawPipeline))
        {
            return false;
        }

#if NORVES_BUILD_DEVELOPMENT
        if (!CreateDrawPipelineVariant(RHI::PolygonMode::Line, m_DrawWireframePipeline))
        {
            return false;
        }
#endif

        return true;
    }

    bool MegaGeometryPass::CreateDrawPipelineVariant(RHI::PolygonMode polygonMode, RHI::PipelinePtr &outPipeline)
    {
        if (!m_Device || !m_GBufferRenderPass || !m_DrawVertexShader || !m_DrawFragmentShader)
        {
            return false;
        }

        // グラフィックスパイプライン作成
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_DrawVertexShader;
        pipelineDesc.pixelShader = m_DrawFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Mesh3DVertex互換）
        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        // Position: location=0, vec3
        RHI::VertexAttributeDesc posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::Format::R32G32B32_FLOAT;
        posAttr.offset = 0;
        pipelineDesc.vertexAttributes.push_back(posAttr);

        // Normal: location=1, vec3
        RHI::VertexAttributeDesc normalAttr;
        normalAttr.location = 1;
        normalAttr.binding = 0;
        normalAttr.format = RHI::Format::R32G32B32_FLOAT;
        normalAttr.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttr);

        // TexCoord: location=2, vec2
        RHI::VertexAttributeDesc texCoordAttr;
        texCoordAttr.location = 2;
        texCoordAttr.binding = 0;
        texCoordAttr.format = RHI::Format::R32G32_FLOAT;
        texCoordAttr.offset = sizeof(float) * 6;
        pipelineDesc.vertexAttributes.push_back(texCoordAttr);

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = polygonMode;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // MRT用ブレンドステート（Albedo・Normal・Material・Emissive・Velocity の5カラーアタッチメント分）
        for (int i = 0; i < 5; ++i)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }

        pipelineDesc.renderPass = m_GBufferRenderPass;

        // ディスクリプタセットレイアウト（GBufferPassの set=0, binding 0=UBO, 1-6=textures に、
        // インスタンスの表・描画情報を足した形）
        pipelineDesc.descriptorSetLayouts.push_back(BuildDrawDescriptorSetDesc());

        outPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!outPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "グラフィックスパイプラインの作成に失敗");
            return false;
        }

        return true;
    }

    RHI::PipelinePtr MegaGeometryPass::SelectDrawPipeline(DebugViewMode mode) const
    {
#if NORVES_BUILD_DEVELOPMENT
        if (mode == DebugViewMode::Wireframe && m_DrawWireframePipeline)
        {
            return m_DrawWireframePipeline;
        }
#endif

        return m_DrawPipeline;
    }

    bool MegaGeometryPass::CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command)
    {
        auto logFallbackOnce = [this](const char *reason) -> void
        {
            if (!m_bOcclusionFallbackLogged)
            {
                NORVES_LOG_INFO("MegaGeometryPass", "MEGA_OCCLUSION_OFF reason=%s", reason);
                m_bOcclusionFallbackLogged = true;
            }
        };

        if (!command.MegaGeometry || !command.MegaGeometry->IsOcclusionCullingEnabled())
        {
            logFallbackOnce("disabled");
            return false;
        }
        if (!m_bHiZReady || !m_DepthTexture || !m_SecondGBufferRenderPass || !m_SecondGBufferFramebuffer ||
            !m_DummyVisibilityBuffer || !m_DummyStatsBuffer)
        {
            logFallbackOnce("unavailable");
            return false;
        }

        // 遮蔽の判定は深度の全体を画面（UV の 0〜1）と見るので、描く範囲が深度の全体と一致するときだけ使う
        const uint32_t depthWidth = m_DepthTexture->GetWidth();
        const uint32_t depthHeight = m_DepthTexture->GetHeight();
        const RHI::Viewport &viewport = command.Viewport;
        if (viewport.x != 0.0f ||
            std::fabs(viewport.width - static_cast<float>(depthWidth)) > 0.5f ||
            std::fabs(std::fabs(viewport.height) - static_cast<float>(depthHeight)) > 0.5f)
        {
            logFallbackOnce("viewport");
            return false;
        }

        // 判定は保存される深度がNDCの深度そのもの（範囲が0〜1）であることを前提にする
        if (viewport.minDepth != 0.0f || viewport.maxDepth != 1.0f)
        {
            logFallbackOnce("depth_range");
            return false;
        }

        if (!m_HiZ.Resize(depthWidth, depthHeight))
        {
            logFallbackOnce("hiz");
            return false;
        }
        return true;
    }

    uint32_t MegaGeometryPass::AssignVisibilityRegion(uint32_t tableSlot, const VisibilityRequest &request)
    {
        if (m_VisibilityRegions.size() <= tableSlot)
        {
            m_VisibilityRegions.resize(static_cast<size_t>(tableSlot) + 1u);
        }
        VisibilityRegion &region = m_VisibilityRegions[tableSlot];
        region.LastBuildStamp = m_VisibilityBuildStamp;

        // 鍵・コンポーネント・メッシュ・クラスタの塊が替わった席は、同じ区画でも中身を引き継がない（記録が0で埋める）
        if (region.Key != request.Key || region.ComponentId != request.ComponentId || region.MeshId != request.MeshId ||
            region.ClusterBufferIdentity != request.ClusterBufferIdentity ||
            region.ClusterBufferOffsetBytes != request.ClusterBufferOffsetBytes)
        {
            region.Key = request.Key;
            region.ComponentId = request.ComponentId;
            region.MeshId = request.MeshId;
            region.ClusterBufferIdentity = request.ClusterBufferIdentity;
            region.ClusterBufferOffsetBytes = request.ClusterBufferOffsetBytes;
            region.LastUsedFrame = 0;
        }
        if (region.ClusterCount == request.ClusterCount)
        {
            return region.Offset;
        }

        // クラスタ数が替わった: 区画を手放して取り直す
        const auto releaseRegion = [this](VisibilityRegion &released) -> void
        {
            if (released.ClusterCount != 0)
            {
                m_VisibilityFreeRegions[released.ClusterCount].push_back(released.Offset);
            }
            released.ClusterCount = 0;
            released.Offset = 0;
            released.LastUsedFrame = 0;
        };
        releaseRegion(region);
        if (request.ClusterCount == 0)
        {
            return 0;
        }

        const auto takeFreeRegion = [this](uint32_t clusterCount, uint32_t &outOffset) -> bool
        {
            const auto found = m_VisibilityFreeRegions.find(clusterCount);
            if (found == m_VisibilityFreeRegions.end() || found->second.empty())
            {
                return false;
            }
            outOffset = found->second.back();
            found->second.pop_back();
            return true;
        };
        uint32_t offset = 0;
        bool bTaken = takeFreeRegion(request.ClusterCount, offset);
        if (!bTaken && m_VisibilityAllocatedEnd + request.ClusterCount > m_VisibilityCapacity &&
            m_VisibilitySweepStamp != m_VisibilityBuildStamp)
        {
            // 末尾から切り出すとバッファを大きくすることになる。前の表にも載らなかった席（区画の外へ出た・消えたインスタンス）の
            // 区画を先に手放す（この表でまだ見ていない席は、前の表に載っていれば手放さない）
            m_VisibilitySweepStamp = m_VisibilityBuildStamp;
            for (VisibilityRegion &stale : m_VisibilityRegions)
            {
                if (stale.ClusterCount != 0 && stale.LastBuildStamp + 1u < m_VisibilityBuildStamp)
                {
                    releaseRegion(stale);
                }
            }
            bTaken = takeFreeRegion(request.ClusterCount, offset);
        }
        if (!bTaken)
        {
            // 末尾が 32bit を超える配置はバッファを作れない（UpdateVisibilityLayout が 2 パスを止める）ので、位置は丸めるだけ
            offset = static_cast<uint32_t>(std::min<uint64_t>(m_VisibilityAllocatedEnd, 0xFFFFFFFFull));
            m_VisibilityAllocatedEnd += request.ClusterCount;
        }
        region.ClusterCount = request.ClusterCount;
        region.Offset = offset;
        region.LastUsedFrame = 0;
        return offset;
    }

    uint32_t MegaGeometryPass::PeekVisibilityRegion(uint32_t tableSlot, const VisibilityRequest &request) const
    {
        if (tableSlot >= m_VisibilityRegions.size())
        {
            return ~0u;
        }
        const VisibilityRegion &region = m_VisibilityRegions[tableSlot];
        if (region.Key != request.Key || region.ComponentId != request.ComponentId || region.MeshId != request.MeshId ||
            region.ClusterBufferIdentity != request.ClusterBufferIdentity ||
            region.ClusterBufferOffsetBytes != request.ClusterBufferOffsetBytes || region.ClusterCount != request.ClusterCount)
        {
            return ~0u;
        }
        return request.ClusterCount == 0 ? 0u : region.Offset;
    }

    bool MegaGeometryPass::UpdateVisibilityLayout(RHI::ICommandList *commandList,
                                                  VariableArray<VisibilityRequest> &requests)
    {
        // 区画は表を作るときに席ごとに決めてある（AssignVisibilityRegion）。末尾が容量に収まらないときだけバッファを大きくする
        const uint64_t requiredElements = m_VisibilityAllocatedEnd;
        if (requiredElements == 0 || requiredElements > 0x3FFFFFFFull || !m_Device || !commandList)
        {
            return false;
        }

        // 決定的な撮影のエポックの最初のフレームは、全部の区画を0から始める
        const bool bClearAll = m_bDropVisibilityContinuity;
        bool bCopyDest = false;
        if (!m_VisibilityBuffer || requiredElements > m_VisibilityCapacity)
        {
            // 1/8 の余裕を足して作り直し、0で埋めてから古い中身を同じ位置へ写す（区画は動かないので、引き継ぐ区画もそのまま読める）
            const uint64_t newCapacity = std::min<uint64_t>(requiredElements + requiredElements / 8u, 0x3FFFFFFFull);
            RHI::BufferDesc desc(newCapacity * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst |
                                     RHI::ResourceUsage::TransferSrc,
                                 false,
                                 "MegaGeometry_VisibleLastFrame");
            RHI::BufferPtr newBuffer = m_Device->CreateBuffer(desc);
            if (!newBuffer)
            {
                return false;
            }

            commandList->BufferBarrier(newBuffer, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            commandList->FillBuffer(newBuffer, 0, newCapacity * sizeof(uint32_t), 0);
            if (m_VisibilityBuffer && !bClearAll)
            {
                // 前のフレームの2パス目の書き込みを、コピー元として見せる。0埋めの後にコピーが書く
                commandList->BufferBarrier(m_VisibilityBuffer,
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::CopySource);
                commandList->BufferBarrier(newBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::CopyDest);
                commandList->CopyBuffer(m_VisibilityBuffer, newBuffer, m_VisibilityCapacity * sizeof(uint32_t), 0, 0);
            }

            // 古いバッファは、GPUが使い終わるまで保持してから破棄する
            if (m_VisibilityBuffer)
            {
                m_RetiredBuffers.push_back(RetiredBuffer{m_VisibilityBuffer, m_RenderFrameCount});
            }
            m_VisibilityBuffer = newBuffer;
            m_VisibilityCapacity = newCapacity;
            bCopyDest = true;
        }

        const auto beginClear = [&]() -> void
        {
            if (bCopyDest)
            {
                // 作り直したときの0埋め・コピーの後に埋める
                commandList->BufferBarrier(m_VisibilityBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::CopyDest);
            }
            else
            {
                commandList->BufferBarrier(m_VisibilityBuffer,
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::CopyDest);
                bCopyDest = true;
            }
        };

        if (bClearAll && !bCopyDest)
        {
            beginClear();
            commandList->FillBuffer(m_VisibilityBuffer, 0, m_VisibilityCapacity * sizeof(uint32_t), 0);
        }

        // 直前のフレームに描かれていた区画だけが、見えたビットを引き継げる。1フレームでも描かれなかった（スナップショットから
        // 外れた・メッシュが無かった・2パスでなかった）区画と、取り直した・持ち主が替わった区画は0から始める。
        // 隣り合う区画はまとめて埋める
        bool bClearStarted = false;
        uint64_t pendingBegin = 0;
        uint64_t pendingEnd = 0;
        const auto flushClear = [&]() -> void
        {
            if (pendingEnd > pendingBegin)
            {
                if (!bClearStarted)
                {
                    beginClear();
                    bClearStarted = true;
                }
                commandList->FillBuffer(m_VisibilityBuffer,
                                        pendingBegin * sizeof(uint32_t),
                                        (pendingEnd - pendingBegin) * sizeof(uint32_t),
                                        0);
            }
            pendingBegin = 0;
            pendingEnd = 0;
        };
        for (const VisibilityRequest &request : requests)
        {
            if (request.TableSlot >= m_VisibilityRegions.size())
            {
                return false;
            }
            VisibilityRegion &region = m_VisibilityRegions[request.TableSlot];
            const bool bContinuing = !bClearAll && region.LastUsedFrame != 0 &&
                                     region.LastUsedFrame + 1 == m_OcclusionFrameCount &&
                                     region.Key == request.Key && region.Offset == request.Offset &&
                                     region.ClusterCount == request.ClusterCount;
            region.LastUsedFrame = m_OcclusionFrameCount;
            if (bContinuing || bClearAll || request.ClusterCount == 0)
            {
                continue;
            }
            const uint64_t begin = request.Offset;
            const uint64_t end = begin + request.ClusterCount;
            if (pendingEnd > pendingBegin && begin == pendingEnd)
            {
                pendingEnd = end;
                continue;
            }
            flushClear();
            pendingBegin = begin;
            pendingEnd = end;
        }
        flushClear();

        if (bCopyDest)
        {
            commandList->BufferBarrier(m_VisibilityBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess);
        }
        return true;
    }

    void MegaGeometryPass::ReleaseStaleBuffers()
    {
        // 手放したフレームより後のフレームなら参照を落とす（GPU が使い終わるまでの破棄の遅延は RHI が行う）
        for (size_t index = 0; index < m_RetiredBuffers.size();)
        {
            if (m_RenderFrameCount - m_RetiredBuffers[index].RetiredFrame > RetiredBufferFrames)
            {
                m_RetiredBuffers[index] = m_RetiredBuffers.back();
                m_RetiredBuffers.pop_back();
                continue;
            }
            ++index;
        }
    }

    void MegaGeometryPass::OnFrameReadback(const FrameReadbackResult &result)
    {
        const auto it = std::find_if(m_PendingStats.begin(), m_PendingStats.end(),
                                     [&result](const PendingStats &entry) { return entry.RequestId == result.RequestId; });
        if (it == m_PendingStats.end())
        {
            return;
        }
        const PendingStats pending = *it;
        m_PendingStats.erase(it);
        // 提出しなかったフレームの統計（取り消し）は出さない
        const uint32_t *const stats = result.Size >= StatsBufferBytes ? static_cast<const uint32_t *>(result.Data) : nullptr;
        if (result.Status != FrameReadbackStatus::Delivered || !stats)
        {
            return;
        }
        // 書いたフレームの提出は完了している
        // エポックが始まっていれば相対フレームが30の倍数のフレームで出す（撮影の間で同じ相対フレームを突き合わせられる）
        const bool bSampleFrame = pending.EpochFrame >= 0 ? (pending.EpochFrame % 30 == 0) : (pending.Frame % 30 == 0);
        if (!m_bStatsLoggedOnce || bSampleFrame || m_Settings.bStatsEveryFrame)
        {
            NORVES_LOG_INFO("MegaGeometryPass",
                            "MEGA_OCCLUSION frame=%llu epoch_frame=%lld pass1=%u "
                            "pass2_tested=%u pass2_drawn=%u occluded=%u",
                            static_cast<unsigned long long>(pending.RenderFrame),
                            static_cast<long long>(pending.EpochFrame),
                            stats[0],
                            stats[1],
                            stats[2],
                            stats[3]);
            if (pending.bSwRasterBin)
            {
                // 振り分けの結果。ソフトのラスタが無い間は、ハードがすべてのクラスタを描く（hw は一覧へ積まなかった数）
                NORVES_LOG_INFO("MegaGeometryPass",
                                "SW_RASTER_BIN pass1=%u pass2=%u hw=%u overflow=%u "
                                "zeroed=%u sw_groups=%u sw_dispatches=%u frame=%llu "
                                "epoch_frame=%lld",
                                stats[4], stats[5], stats[6], stats[7],
                                stats[8], stats[10], pending.SwDispatches,
                                static_cast<unsigned long long>(pending.RenderFrame),
                                static_cast<long long>(pending.EpochFrame));
                // ソフトが 1 スレッド 1 三角形で走査する矩形（一辺の上限は振り分けのしきい値から max(64, ceil(2 × しきい値) + 2) 画素で決める）を超えて描かなかった三角形の数。振り分けのしきい値が保守的なら 0
                NORVES_LOG_INFO("MegaGeometryPass",
                                "SW_RASTER_OVERSIZE=%u frame=%llu epoch_frame=%lld",
                                stats[9],
                                static_cast<unsigned long long>(pending.RenderFrame),
                                static_cast<long long>(pending.EpochFrame));
            }
            // 描かれなかったクラスタの数（0 が合格）と、インスタンス数に比例して膨らむバッファの大きさ。
            // bvh_overflow は BVH のたどりの固定容量の列を超えて判定から落ちた要素の数（0 が合格）、bvh_pass2_fallback は
            // 2 パス目の根の入力が溢れて全部の根を判定し直したか、bvh_node_peak・bvh_leaf_peak は節の列の 1 段・葉の列へ
            // 積もうとした数の最大（容量を超えた分も含む）。BVH の列（bvh_queue_bytes）は本数によらず一定
            const auto bufferBytes = [](const RHI::BufferPtr &buffer) -> unsigned long long
            { return buffer ? static_cast<unsigned long long>(buffer->GetSize()) : 0ull; };
            NORVES_LOG_INFO("MegaGeometryPass",
                            "MEGA_CAPACITY draw_dropped=%u max_draw_count=%u command_capacity=%u "
                            "visibility_bytes=%llu indirect_bytes=%llu draw_info_bytes=%llu "
                            "bvh_queue_bytes=%llu bvh_pass2_bytes=%llu bvh_overflow=%u bvh_pass2_fallback=%u "
                            "bvh_node_peak=%u bvh_leaf_peak=%u "
                            "sw_raster_bytes=%llu frame=%llu epoch_frame=%lld",
                            stats[11],
                            m_Settings.MaxDrawCount,
                            m_CommandCapacity,
                            bufferBytes(m_VisibilityBuffer),
                            bufferBytes(m_IndirectDrawBuffer),
                            bufferBytes(m_DrawInfoBuffer),
                            bufferBytes(m_BvhQueueBuffer),
                            bufferBytes(m_BvhPass2Buffer),
                            stats[12],
                            stats[13],
                            stats[14],
                            stats[15],
                            bufferBytes(m_SwRasterBuffer),
                            static_cast<unsigned long long>(pending.RenderFrame),
                            static_cast<long long>(pending.EpochFrame));
            m_bStatsLoggedOnce = true;
        }
    }

} // namespace NorvesLib::Core::Rendering
