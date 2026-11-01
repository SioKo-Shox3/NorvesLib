#include "Rendering/VirtualShadowMapPass.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Math/MatrixUtils.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/VirtualShadowMapCasters.h"
#include "Rendering/VirtualShadowMapFoliageCasters.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapRaster.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // ページの表・要求のビット列が持つスライスの数。太陽の段の後ろに、点光源の VSM のとき最大 4 灯 × 6 面 × 解像度の段を並べる
        // （点光源のスライスは UpdatePointLights が LEVEL_COUNT から並べる）
        uint32_t SliceCapacityFor(PointShadowMethod method)
        {
            return method == PointShadowMethod::Vsm
                       ? VirtualShadowMap::LEVEL_COUNT + PointShadowMaxLights * PointShadowFaceCount * VirtualShadowMapPointSettings{}.MipCount
                       : VirtualShadowMap::LEVEL_COUNT;
        }

#if NORVES_BUILD_DEVELOPMENT
        /** 無効化の照合の結果（VSM_INVALIDATE_EXPECTED の値） */
        struct InvalidationCheck
        {
            /** 動いた境界が覆う、割り当て済みのページの数（太陽の段） */
            uint32_t Expected = 0;
            /** 割り当て済みで dirty のページの数（太陽の段） */
            uint32_t SunDirty = 0;
            /** Expected のうち dirty の付かなかった数（描き直し漏れ） */
            uint32_t Missed = 0;
            /** dirty だが Expected の外の数 */
            uint32_t Extra = 0;
        };

        /**
         * 無効化の直後のページの表（太陽の段）と、同じフレームの動いた境界・クリップマップから、境界が覆う割り当て済みのページを数える。
         * 境界はライト空間の矩形（余白なし）にし、段ごとに floor(位置 / ページの幅) が範囲 [原点, 原点 + 一辺) に入るページを、
         * GPU と同じトーラスの番地（段 × 128 × 128 + (y mod 128) × 128 + (x mod 128)）で集合にする。
         */
        InvalidationCheck CountExpectedInvalidation(const VirtualShadowMapClipmap& clipmap,
                                                    const Container::VariableArray<float>& bounds,
                                                    const uint32_t* table)
        {
            InvalidationCheck check;
            const uint32_t levelCount = std::min(clipmap.LevelCount, VirtualShadowMap::LEVEL_COUNT);
            const int64_t dimension = static_cast<int64_t>(VirtualShadowMap::TABLE_DIMENSION);
            Container::VariableArray<uint32_t> covered;
            covered.resize(static_cast<size_t>(levelCount) * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL / 32u, 0u);
            for (size_t offset = 0; offset + 6u <= bounds.size(); offset += 6u)
            {
                VirtualShadowMap::CasterBounds box;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    box.Min[axis] = bounds[offset + axis];
                    box.Max[axis] = bounds[offset + 3u + axis];
                }
                double lightMin[2] = {};
                double lightMax[2] = {};
                if (!VirtualShadowMap::LightSpaceRect(clipmap, box, lightMin, lightMax))
                {
                    continue;
                }
                for (uint32_t level = 0; level < levelCount; ++level)
                {
                    const VirtualShadowMapClipmapLevel& data = clipmap.Levels[level];
                    const double pageMeters = static_cast<double>(data.PageMeters);
                    if (!(pageMeters > 0.0))
                    {
                        continue;
                    }
                    const int64_t lowX = std::max(static_cast<int64_t>(std::floor(lightMin[0] / pageMeters)), data.OriginPageX);
                    const int64_t lowY = std::max(static_cast<int64_t>(std::floor(lightMin[1] / pageMeters)), data.OriginPageY);
                    const int64_t highX = std::min(static_cast<int64_t>(std::floor(lightMax[0] / pageMeters)), data.OriginPageX + dimension - 1);
                    const int64_t highY = std::min(static_cast<int64_t>(std::floor(lightMax[1] / pageMeters)), data.OriginPageY + dimension - 1);
                    for (int64_t pageY = lowY; pageY <= highY; ++pageY)
                    {
                        for (int64_t pageX = lowX; pageX <= highX; ++pageX)
                        {
                            const uint32_t entry = level * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL +
                                                   static_cast<uint32_t>(pageY & (dimension - 1)) * VirtualShadowMap::TABLE_DIMENSION +
                                                   static_cast<uint32_t>(pageX & (dimension - 1));
                            covered[entry >> 5u] |= 1u << (entry & 31u);
                        }
                    }
                }
            }
            const uint32_t entryCount = levelCount * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL;
            for (uint32_t entry = 0; entry < entryCount; ++entry)
            {
                const uint32_t value = table[entry];
                if ((value & VirtualShadowMap::PAGE_ENTRY_ALLOCATED) == 0u)
                {
                    continue;
                }
                const bool bCovered = ((covered[entry >> 5u] >> (entry & 31u)) & 1u) != 0u;
                const bool bDirty = (value & VirtualShadowMap::PAGE_ENTRY_DIRTY) != 0u;
                check.Expected += bCovered ? 1u : 0u;
                check.SunDirty += bDirty ? 1u : 0u;
                check.Missed += bCovered && !bDirty ? 1u : 0u;
                check.Extra += !bCovered && bDirty ? 1u : 0u;
            }
            return check;
        }
#endif
    } // namespace

    /**
     * 投影物の塊の記録を作る作業の状態。展開の出力（インスタンス・間接描画の引数）は GPU が書くので全 Execute で 1 つを使い回し、
     * 塊の記録はホストが書くので、フレームの枠（FrameUseRing）ごとに Execute のたびに別のバッファを使う
     * （同じフレームの複数のビューポートが、提出前の記録を上書きしない）。
     */
    struct VirtualShadowMapCasterState
    {
        struct Use
        {
            RHI::BufferPtr Chunks;
            uint32_t ChunkCapacity = 0;
        };

        FrameUseRing<Use> Uses;
        RHI::BufferPtr Instances;
        RHI::BufferPtr Draws;
        /** @brief 今回の Execute が集めた塊の記録と内訳 */
        Container::VariableArray<VsmShadowChunk> Chunks;
        VirtualShadowMap::CasterStats Stats;
        /** @brief 作業配列（毎フレーム容量を使い回す） */
        VirtualShadowMap::ProceduralPlanScratch PlanScratch;
        Container::VariableArray<VirtualShadowMap::ProceduralChunkPlan> Plan;
        Container::VariableArray<MeshIndexChunk> SkinnedChunks;
        /** @brief 最後に出した内訳（変わったときだけ出す）と、出してからの回数 */
        VirtualShadowMap::CasterStats LoggedStats;
        bool bLogged = false;
        uint32_t ReportsSinceLog = 0;
        /** @brief キャッシュの無効化: 今フレームの投影物の動きの入力・前フレームの記録・無効にするライト空間の矩形（太陽の段）と球（点光源の面） */
        Container::VariableArray<VirtualShadowMap::CasterMotionEntry> Motion;
        VirtualShadowMap::CasterMotionTracker MotionTracker;
        Container::VariableArray<VirtualShadowMap::CasterBounds> ChangedBounds;
        /** @brief MegaGeometryPass から取り出した無効化（変わった席の旧・新の境界と、常駐・メッシュが変わった席の境界） */
        MegaGeometryShadowInvalidation MegaInvalidation;
        Container::VariableArray<float> InvalidationRects;
        Container::VariableArray<float> InvalidationSpheres;
        bool bInvalidateAll = false;
        /**
         * @brief 点光源の VSM のとき（灯が 1 つ以上）に、投影物の集め方と展開が使うスライスの表（太陽の段の後ろに点光源の面を並べたもの）。
         *        使わないフレーム（bUseSliceTable が false）は、太陽のクリップマップの段だけを見る従来の集め方
         */
        GPUVsmSlice SliceTable[VirtualShadowMapMaxSlices];
        uint32_t SliceTableCount = 0;
        bool bUseSliceTable = false;

        /**
         * @brief 草・低木の株のメッシュを VSM の描画が読むバッファ（BDA）とブロックの境界。束のメッシュごとに初めて影に描くときに作り、
         *        書き換えない。使われなくなってから FoliageMeshReleaseDelay 回の集めを待って捨てる（飛行中のフレームの記録が参照する）
         */
        struct FoliageMeshEntry
        {
            Container::TSharedPtr<const FoliageMeshData> Source;
            RHI::BufferPtr Vertex;
            RHI::BufferPtr Index;
            Container::VariableArray<BoundingBox> Blocks;
            uint64_t LastUsedCollect = 0;
        };
        Container::VariableArray<FoliageMeshEntry> FoliageMeshes;
        /**
         * @brief 束の番号 × MAX_FOLIAGE_SHADOW_LODS ＋ 影の段 → FoliageMeshes の番号（今回の集めの作業。-1 は未解決、-2 は作れない）
         */
        Container::VariableArray<int32_t> FoliageBatchMesh;
        Container::VariableArray<VirtualShadowMap::FoliageShadowCandidate> FoliageCandidates;
        uint64_t FoliageCollectCount = 0;
        /** @brief 株を置く風（描画の定数へ渡す）。x, y = 風の吹いていく向き（XZ）、z = 風の時計[s] / x = 突風の速さ、y = 突風の大きさ */
        float FoliageWind[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        float FoliageWindParams[4] = {};
        /** @brief 影に描いた株のうち最も遠い株の根元までの距離[m]（ログ用） */
        float FoliageFarthest = 0.0f;
        /** @brief 影に描いた ShadowPriority 0 の株のうち最も遠い株の根元までの距離[m]（ログ用） */
        float FoliagePriority0Farthest = 0.0f;
        /** @brief ShadowPriority 0 の株のうち、予算の外で影に描かなかった株の数（ログ用） */
        uint32_t FoliagePriority0OverBudget = 0;
        /** @brief 影に描いた株の数を影の段ごとに（ログ用） */
        uint32_t FoliageLodPlants[VirtualShadowMap::MAX_FOLIAGE_SHADOW_LODS] = {};
    };

    namespace
    {
        constexpr double BytesPerMegabyte = 1024.0 * 1024.0;
        /** @brief 投影物の内訳が変わらなくても VSM_CASTERS を出す間隔（Execute の回数） */
        constexpr uint32_t CasterLogIntervalExecutions = 60;

        /** @brief 使われなくなった株のメッシュのバッファを捨てるまでに待つ集めの回数（飛行中のフレームとビューの数より十分に多い） */
        constexpr uint64_t FoliageMeshReleaseDelay = 64;

        // 株のメッシュの VSM 用のバッファとブロックの境界を返す（無ければ作る）。作れなければ -1
        int32_t AcquireFoliageShadowMesh(RHI::IDevice* device,
                                         VirtualShadowMapCasterState& state,
                                         const Container::TSharedPtr<const FoliageMeshData>& mesh)
        {
            for (uint32_t index = 0; index < static_cast<uint32_t>(state.FoliageMeshes.size()); ++index)
            {
                VirtualShadowMapCasterState::FoliageMeshEntry& entry = state.FoliageMeshes[index];
                if (entry.Source.get() == mesh.get())
                {
                    entry.LastUsedCollect = state.FoliageCollectCount;
                    return static_cast<int32_t>(index);
                }
            }
            if (!device || !mesh)
            {
                return -1;
            }
            VirtualShadowMapCasterState::FoliageMeshEntry entry;
            entry.Source = mesh;
            if (!VirtualShadowMap::BuildFoliageBlockBounds(*mesh, entry.Blocks))
            {
                return -1;
            }
            // VSM の描画は頂点・インデックスをアドレス（BDA）で読む。作った後は書き換えない
            const RHI::ResourceUsage usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::BufferDeviceAddress;
            const uint64_t vertexBytes = static_cast<uint64_t>(mesh->Vertices.size()) * sizeof(FoliageVertex);
            const uint64_t indexBytes = static_cast<uint64_t>(mesh->Indices.size()) * sizeof(uint32_t);
            entry.Vertex = device->CreateBuffer(RHI::BufferDesc(vertexBytes, usage, true, "VsmFoliage_Vertices"));
            entry.Index = device->CreateBuffer(RHI::BufferDesc(indexBytes, usage, true, "VsmFoliage_Indices"));
            if (!entry.Vertex || !entry.Index || entry.Vertex->GetDeviceAddress() == 0u || entry.Index->GetDeviceAddress() == 0u)
            {
                NORVES_LOG_ERROR("VirtualShadowMapPass", "VSM_FOLIAGE_BUFFER_FAILED vertex_bytes=%llu index_bytes=%llu",
                                 static_cast<unsigned long long>(vertexBytes),
                                 static_cast<unsigned long long>(indexBytes));
                return -1;
            }
            entry.Vertex->Update(mesh->Vertices.data(), static_cast<size_t>(vertexBytes));
            entry.Index->Update(mesh->Indices.data(), static_cast<size_t>(indexBytes));
            entry.LastUsedCollect = state.FoliageCollectCount;
            state.FoliageMeshes.push_back(std::move(entry));
            return static_cast<int32_t>(state.FoliageMeshes.size() - 1u);
        }

        // 草・低木の株: カメラに近い株を近い順に、株の塊の予算に収まるだけ記録にする（置き方は vsm_draw.vert が foliage.vert と同じ式で決める）
        void CollectFoliageCasters(RHI::IDevice* device,
                                   VirtualShadowMapCasterState& state,
                                   const FoliageScene& scene,
                                   const CameraProxy& camera,
                                   double totalTime,
                                   const VirtualShadowMapClipmap& clipmap,
                                   const VirtualShadowMap::CasterSliceTable* slices)
        {
            ++state.FoliageCollectCount;
            state.FoliageFarthest = 0.0f;
            state.FoliagePriority0Farthest = 0.0f;
            state.FoliagePriority0OverBudget = 0;
            std::fill(std::begin(state.FoliageLodPlants), std::end(state.FoliageLodPlants), 0u);
            // GBuffer の描画（FoliagePass）と同じ風の向きと時計
            FoliageWindDirection(scene.Wind, state.FoliageWind[0], state.FoliageWind[1]);
            state.FoliageWind[2] = FoliageWindClock(totalTime, scene.Wind);
            state.FoliageWind[3] = 0.0f;
            state.FoliageWindParams[0] = scene.Wind.GustSpeed;
            state.FoliageWindParams[1] = scene.Wind.GustScale;
            state.FoliageWindParams[2] = 0.0f;
            state.FoliageWindParams[3] = 0.0f;

            const float cameraPosition[3] = {camera.PositionX, camera.PositionY, camera.PositionZ};
            VirtualShadowMap::CollectFoliageShadowCandidates(scene, cameraPosition, state.FoliageCandidates);
            state.FoliageBatchMesh.clear();
            state.FoliageBatchMesh.resize(scene.Batches.size() * VirtualShadowMap::MAX_FOLIAGE_SHADOW_LODS, -1);
            // 株の塊は、ほかの投影物の後ろに、予算と記録の上限の残りの小さい方まで足す
            const size_t budgetEnd = std::min<size_t>(VirtualShadowMap::MAX_CASTER_CHUNKS,
                                                      state.Chunks.size() + VirtualShadowMap::FOLIAGE_SHADOW_CHUNK_BUDGET);
            for (const VirtualShadowMap::FoliageShadowCandidate& candidate : state.FoliageCandidates)
            {
                const FoliageBatch& batch = scene.Batches[candidate.Batch];
                const float distance = std::sqrt(candidate.DistanceSquared);
                // 遠い株は簡略のメッシュの段で描く（1 株の塊の数を減らして、予算で遠くまで届かせる）
                const uint32_t lod = VirtualShadowMap::SelectFoliageShadowLod(batch, distance);
                const Container::TSharedPtr<const FoliageMeshData>& lodMesh = VirtualShadowMap::FoliageShadowLodMesh(batch, lod);
                const bool bStill = lod != 0u && batch.ShadowLods[lod - 1u].bStill;
                int32_t& meshIndex = state.FoliageBatchMesh[candidate.Batch * VirtualShadowMap::MAX_FOLIAGE_SHADOW_LODS + lod];
                if (meshIndex == -1)
                {
                    meshIndex = AcquireFoliageShadowMesh(device, state, lodMesh);
                    meshIndex = meshIndex < 0 ? -2 : meshIndex;
                }
                if (meshIndex < 0)
                {
                    ++state.Stats.SkippedDraws;
                    continue;
                }
                const VirtualShadowMapCasterState::FoliageMeshEntry& entry = state.FoliageMeshes[static_cast<size_t>(meshIndex)];
                // 予算は省く前の塊の数で見る（遠い大きな株が収まらなくても、後ろの小さな株は足す）
                if (state.Chunks.size() + entry.Blocks.size() > budgetEnd)
                {
                    ++state.Stats.FoliageOverBudget;
                    state.FoliagePriority0OverBudget += candidate.Priority == 0u ? 1u : 0u;
                    continue;
                }
                const FoliageInstance& instance = (*batch.Instances)[candidate.Instance];
                const float fadeScale = VirtualShadowMap::FoliageFadeScale(batch, distance, instance.FadeThreshold);
                VirtualShadowMap::FoliageShadowMesh mesh;
                mesh.VertexAddress = entry.Vertex->GetDeviceAddress();
                mesh.IndexAddress = entry.Index->GetDeviceAddress();
                mesh.BlockBounds = entry.Blocks.data();
                mesh.BlockCount = static_cast<uint32_t>(entry.Blocks.size());
                mesh.IndexCount = static_cast<uint32_t>(lodMesh->Indices.size());
                VirtualShadowMap::CasterBounds bounds;
                bool bHasBounds = false;
                const uint32_t added = VirtualShadowMap::AppendFoliageInstance(
                    batch, mesh, instance, fadeScale, clipmap, state.Chunks, state.Stats, bounds, bHasBounds, slices, bStill);
                if (added == 0u || !bHasBounds)
                {
                    continue;
                }
                state.FoliageFarthest = std::max(state.FoliageFarthest, distance);
                if (candidate.Priority == 0u)
                {
                    state.FoliagePriority0Farthest = std::max(state.FoliagePriority0Farthest, distance);
                }
                ++state.FoliageLodPlants[lod];

                // キャッシュの無効化の入力: 風で揺れる株は毎フレーム動いた物として扱う（鍵は束のインスタンスの配列と番号）。
                // 揺らさない段の株は、置き方か段が変わったときだけ動いた物にする
                VirtualShadowMap::CasterMotionEntry motion;
                motion.Key = VirtualShadowMap::CasterHashCombine(
                    VirtualShadowMap::CasterHashCombine(0x464F4C49ull, reinterpret_cast<uintptr_t>(batch.Instances.get())), candidate.Instance);
                const float placement[5] = {instance.Position[0], instance.Position[1], instance.Position[2], instance.Yaw, fadeScale};
                motion.Signature = VirtualShadowMap::CasterHashCombine(VirtualShadowMap::CasterHashFloats(1469598103934665603ull, placement, 5u),
                                                                       reinterpret_cast<uintptr_t>(lodMesh.get()));
                motion.bAlwaysChanged = batch.WindStrength != 0.0f && !bStill;
                motion.bHasBounds = true;
                motion.Bounds = bounds;
                state.Motion.push_back(motion);
            }

            // 一覧から外れた株のメッシュのバッファを捨てる
            for (auto it = state.FoliageMeshes.begin(); it != state.FoliageMeshes.end();)
            {
                if (state.FoliageCollectCount - it->LastUsedCollect > FoliageMeshReleaseDelay)
                {
                    it = state.FoliageMeshes.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // MegaGeometryPass が積んだキャッシュの無効化を取り出す（MegaGeometry が無ければ空）
        void TakeMegaInvalidationFrom(MegaGeometryPass* megaPass, VirtualShadowMapCasterState& state)
        {
            if (megaPass)
            {
                megaPass->TakeShadowInvalidation(state.MegaInvalidation);
            }
            else
            {
                state.MegaInvalidation.Boxes.clear();
                state.MegaInvalidation.bInvalidateAll = false;
            }
        }

        // スキニングの描画の持ち主（元の描画の SourceMeshComponentId と同じ ComponentId を持つプロキシ）。無ければ null
        const SkinnedMeshProxy* FindSkinnedProxy(const Container::VariableArray<SkinnedMeshProxy>& proxies, uint64_t componentId)
        {
            if (componentId == 0u)
            {
                return nullptr;
            }
            for (const SkinnedMeshProxy& proxy : proxies)
            {
                if (proxy.ComponentId == componentId)
                {
                    return &proxy;
                }
            }
            return nullptr;
        }

        // アニメーション後の境界（ローカル）にワールド行列をかけた、描画のワールドの境界。境界か行列が有限でなければ false
        bool BuildSkinnedWorldBounds(const SkinnedMeshProxy& proxy, VirtualShadowMap::CasterBounds& outBounds)
        {
            if (!proxy.bHasAnimatedBounds)
            {
                return false;
            }
            const Math::Vector3 localMin = proxy.AnimatedBounds.Min;
            const Math::Vector3 localMax = proxy.AnimatedBounds.Max;
            if (!std::isfinite(localMin.x) || !std::isfinite(localMin.y) || !std::isfinite(localMin.z) || !std::isfinite(localMax.x) ||
                !std::isfinite(localMax.y) || !std::isfinite(localMax.z) || localMin.x > localMax.x || localMin.y > localMax.y ||
                localMin.z > localMax.z)
            {
                return false;
            }
            for (const float value : proxy.WorldTransform.values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            const Math::Vector3 center = Math::MatrixUtils::TransformPointRowVector(proxy.WorldTransform, proxy.AnimatedBounds.Center());
            const Math::Vector3 half = Math::MatrixUtils::AbsUpper3x3TransformExtentsRowVector(proxy.WorldTransform, proxy.AnimatedBounds.HalfExtents());
            outBounds.Min[0] = center.x - half.x;
            outBounds.Min[1] = center.y - half.y;
            outBounds.Min[2] = center.z - half.z;
            outBounds.Max[0] = center.x + half.x;
            outBounds.Max[1] = center.y + half.y;
            outBounds.Max[2] = center.z + half.z;
            return true;
        }

        // --vsm-cache=off（または環境変数 NORVES_VSM_CACHE=off）のときだけ false。ページのキャッシュは既定で有効。
        // 起動引数は ApplicationProcessor の外（描画の層）でも読めるよう、プロセスのコマンドラインを直接見る
        bool IsCacheEnabledByProcess()
        {
            const auto IsOffValue = [](const wchar_t* value) {
                return value != nullptr && (std::wcscmp(value, L"off") == 0 || std::wcscmp(value, L"0") == 0 || std::wcscmp(value, L"false") == 0);
            };
            wchar_t environmentValue[16] = {};
            const DWORD length = GetEnvironmentVariableW(L"NORVES_VSM_CACHE", environmentValue, 16);
            if (length > 0 && length < 16 && IsOffValue(environmentValue))
            {
                return false;
            }
            const wchar_t* commandLine = GetCommandLineW();
            if (commandLine == nullptr)
            {
                return true;
            }
            constexpr wchar_t Prefix[] = L"--vsm-cache=";
            const wchar_t* found = std::wcsstr(commandLine, Prefix);
            if (found == nullptr)
            {
                return true;
            }
            wchar_t argumentValue[16] = {};
            const wchar_t* begin = found + (sizeof(Prefix) / sizeof(wchar_t) - 1);
            size_t count = 0;
            while (begin[count] != L'\0' && begin[count] != L' ' && begin[count] != L'"' && count + 1 < 16)
            {
                argumentValue[count] = begin[count];
                ++count;
            }
            return !IsOffValue(argumentValue);
        }

        // 資源 1 つ分: 名前・バッファ・グラフに取り込んだ資源の控え
        struct DeclaredBuffer
        {
            Identity Name;
            const RHI::BufferPtr* Buffer = nullptr;
            RGResourceHandle* Handle = nullptr;
            const char* DebugName = nullptr;
        };
    } // namespace

    VirtualShadowMapPass::VirtualShadowMapPass(uint32_t requestedPoolPages)
        : m_RequestedPoolPages(requestedPoolPages)
        , m_bCacheEnabled(IsCacheEnabledByProcess())
    {
    }

    VirtualShadowMapPass::~VirtualShadowMapPass()
    {
        Shutdown();
    }

    void VirtualShadowMapPass::Fallback(VirtualShadowMap::FallbackReason reason)
    {
        ReleaseResources();
        m_bActive = false;
        m_FallbackReason = reason;
        // 照明は CSM のまま描く。理由は 1 回だけ出す（Initialize が 1 回しか呼ばない）
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_FALLBACK reason=%s",
                        VirtualShadowMap::FallbackReasonName(reason));
        if (m_PointShadowMethod == PointShadowMethod::Vsm)
        {
            // 点光源の VSM は太陽の VSM の資源の上に乗るので、キューブのまま描く
            NORVES_LOG_INFO("VirtualShadowMapPass",
                            "VSM_FALLBACK reason=%s",
                            VirtualShadowMap::PointRequiresVsmReasonName);
        }
    }

    void VirtualShadowMapPass::UpdatePointLights(const ViewRenderContext& context)
    {
        PointShadowSnapshot emptySnapshot;
        const PointShadowSnapshot& snapshot = context.SnapshotPointShadows != nullptr ? *context.SnapshotPointShadows : emptySnapshot;
        // 点光源のスライスは太陽の段（LEVEL_COUNT 件）の後ろに並べる
        const VirtualShadowMapPointLights lights =
            BuildVirtualShadowMapPointLights(snapshot, VirtualShadowMapPointSettings{}, VirtualShadowMap::LEVEL_COUNT);
        if (!m_bPointLightsLogged || VirtualShadowMapPointLightsDiffer(lights, m_PointLights))
        {
            m_bPointLightsLogged = true;
            m_PointLights = lights;
            NORVES_LOG_INFO("VirtualShadowMapPass",
                            "VSM_POINT lights=%u slices=%u face_res=%u mips=%u",
                            lights.LightCount,
                            lights.SliceCount(),
                            lights.Settings.FaceResolution,
                            lights.Settings.MipCount);
        }
        else
        {
            m_PointLights = lights;
        }
    }

    void VirtualShadowMapPass::ReleaseResources()
    {
        m_Pool.reset();
        m_PageTable.reset();
        m_RequestBits.reset();
        m_FreeList.reset();
        m_Stats.reset();
        m_DirtyList.reset();
        m_Pages.reset();
        m_RasterReporter.reset();
        if (m_Raster)
        {
            m_Raster->Shutdown();
        }
        m_Raster.reset();
        m_Casters.reset();
        m_MegaList.reset();
        m_MegaDirtyBits.reset();
        m_MegaChunks.reset();
        if (m_MegaCull)
        {
            m_MegaCull->Shutdown();
        }
        m_MegaCull.reset();
        m_MegaReporter.reset();
        for (StatsSlot& slot : m_StatsSlots)
        {
            slot = StatsSlot{};
        }
        m_PoolPages = 0;
        if (m_Gpu)
        {
            m_Gpu->SetShadowMapPoolBytes(0);
        }
    }

    bool VirtualShadowMapPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_Gpu = context.Resources.Gpu;
        m_bActive = false;
        m_FallbackReason = VirtualShadowMap::FallbackReason::None;
        m_bInitialFilled = false;
        if (!m_Device)
        {
            // デバイスが無いときは資源を作れない。描画全体は止めない
            Fallback(VirtualShadowMap::FallbackReason::PoolSize);
            m_bInitialized = true;
            return true;
        }

        const VirtualShadowMap::PoolPlan plan = VirtualShadowMap::PlanPool(m_Device->GetCapabilities(), m_RequestedPoolPages);
        if (!plan.IsSupported())
        {
            Fallback(plan.Reason);
            m_bInitialized = true;
            return true;
        }

        // 印付け・割り当て・消去の計算パイプライン。作れなければ VSM を使わず CSM で描く
        m_Pages = Container::MakeUnique<VirtualShadowMapPages>();
        if (!m_Pages->Initialize(m_Device, context.ShaderMgr))
        {
            Fallback(VirtualShadowMap::FallbackReason::Pipeline);
            m_bInitialized = true;
            return true;
        }

        // 展開・描画のパイプライン。作れなければ（装置が firstInstance の間接描画を使えないときを含む）VSM を使わず CSM で描く
        m_Raster = Container::MakeUnique<VirtualShadowMapRaster>();
        if (!m_Raster->Initialize(m_Device, context.ShaderMgr))
        {
            Fallback(VirtualShadowMap::FallbackReason::Pipeline);
            m_bInitialized = true;
            return true;
        }
        m_Casters = Container::MakeUnique<VirtualShadowMapCasterState>();

        // MegaGeometry の投影物のカリングと、クラスタの記録（影の塊）を描く仕組み。用意できないときは MegaGeometry の影だけを欠いた VSM を
        // 公開せず、VSM 全体を使わず CSM で描く（照明が CSM へ戻らないと、CSM が描く MegaGeometry の太陽の影が消える）。
        // クラスタの記録は件数を GPU から読む間接描画で描くので、それが使えない装置でも戻す
        if (!m_Raster->SupportsMegaCasters())
        {
            NORVES_LOG_WARNING("VirtualShadowMapPass", "MegaGeometry の影の描画に要る DrawIndexedIndirectCount が無いので、VSM を使わない");
            Fallback(VirtualShadowMap::FallbackReason::MegaGeometry);
            m_bInitialized = true;
            return true;
        }
        m_MegaCull = Container::MakeUnique<VirtualShadowMapMegaCull>();
        if (!m_MegaCull->Initialize(m_Device, context.ShaderMgr))
        {
            NORVES_LOG_WARNING("VirtualShadowMapPass", "MegaGeometry の投影物のカリングのパイプラインを作れないので、VSM を使わない");
            Fallback(VirtualShadowMap::FallbackReason::MegaGeometry);
            m_bInitialized = true;
            return true;
        }
        m_MegaReporter = Container::MakeUnique<VirtualShadowMapMegaCullStatsReporter>();

        m_RasterReporter = Container::MakeUnique<VirtualShadowMapRasterStatsReporter>();

        const uint64_t poolBytes = VirtualShadowMap::PoolBytes(plan.Pages);
        const uint64_t freeListBytes = VirtualShadowMap::FreeListBytes(plan.Pages);
        const RHI::ResourceUsage storageUsage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        const RHI::ResourceUsage statsUsage = VirtualShadowMap::StatsBufferUsage();

        // 大きな確保は失敗しうる（装置のメモリ不足・上限）。作れなければ VSM を使わず CSM で描く
        bool bCreated = false;
        bool bMegaCreated = false;
        try
        {
            m_Pool = m_Device->CreateBuffer(RHI::BufferDesc(
                poolBytes, storageUsage | RHI::ResourceUsage::BufferDeviceAddress, false, "VSM_PhysicalPool"));
            m_PageTable = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::PageTableBytes(SliceCapacityFor(m_PointShadowMethod)),
                                storageUsage | RHI::ResourceUsage::TransferSrc, false, "VSM_PageTable"));
            m_RequestBits = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::RequestBitsBytes(SliceCapacityFor(m_PointShadowMethod)), storageUsage, false, "VSM_RequestBits"));
            m_FreeList = m_Device->CreateBuffer(RHI::BufferDesc(freeListBytes, storageUsage, false, "VSM_FreeList"));
            m_Stats = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::STATS_BYTES, statsUsage, false, "VSM_Stats"));
            // 消去するページの一覧は、間接 dispatch の引数としても読まれる
            m_DirtyList = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::DirtyListBytes(plan.Pages),
                                                                 storageUsage | RHI::ResourceUsage::IndirectBuffer,
                                                                 false,
                                                                 "VSM_DirtyList"));
            // 展開の出力（GPU が書く）。投影物の塊の記録は、描くフレームで必要な大きさに合わせて作る
            m_Casters->Instances = m_Device->CreateBuffer(RHI::BufferDesc(
                VirtualShadowMap::RasterInstanceBytes(VirtualShadowMap::RASTER_INSTANCE_CAPACITY), VirtualShadowMap::RasterInstanceUsage(), false, "VsmRaster_Instances"));
            // 間接描画の引数は、ホストが書いた塊と、続く MegaGeometry のクラスタの記録（カリングの一覧の容量ぶん）の両方を持つ
            m_Casters->Draws = m_Device->CreateBuffer(RHI::BufferDesc(
                VirtualShadowMap::RasterDrawBytes(VirtualShadowMap::MAX_CASTER_CHUNKS +
                                                  VirtualShadowMap::MEGA_CULL_LIST_CAPACITY),
                VirtualShadowMap::RasterDrawUsage(),
                false,
                "VsmRaster_Draws"));
            bCreated = m_Pool && m_PageTable && m_RequestBits && m_FreeList && m_Stats && m_DirtyList && m_Casters->Instances && m_Casters->Draws;
            // MegaGeometry の投影物のカリングの出力の一覧と、dirty のページの階層（GPU が書く）。作れなければ MegaGeometry の影の経路を
            // 用意できないので、VSM を使わない（下の bMegaCreated の失敗の戻り）
            {
                m_MegaList = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::MegaCullListBytes(VirtualShadowMap::MEGA_CULL_LIST_CAPACITY),
                                                                    VirtualShadowMap::MegaCullListUsage(),
                                                                    false,
                                                                    "VsmMega_List"));
                m_MegaDirtyBits = m_Device->CreateBuffer(
                    RHI::BufferDesc(VirtualShadowMap::MegaDirtyBitsBytes(SliceCapacityFor(m_PointShadowMethod)),
                                    VirtualShadowMap::MegaDirtyBitsUsage(),
                                    false,
                                    "VsmMega_DirtyBits"));
                m_MegaChunks = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::RasterChunkBytes(VirtualShadowMap::MEGA_CULL_LIST_CAPACITY),
                                                                      VirtualShadowMap::MegaChunkUsage(),
                                                                      false,
                                                                      "VsmMega_Chunks"));
                bMegaCreated = m_MegaList && m_MegaDirtyBits && m_MegaChunks;
            }
        }
        catch (...)
        {
            bCreated = false;
        }
        if (!bCreated)
        {
            Fallback(VirtualShadowMap::FallbackReason::PoolSize);
            m_bInitialized = true;
            return true;
        }
        if (!bMegaCreated)
        {
            NORVES_LOG_WARNING("VirtualShadowMapPass", "MegaGeometry の投影物のカリングの資源を作れないので、VSM を使わない");
            Fallback(VirtualShadowMap::FallbackReason::MegaGeometry);
            m_bInitialized = true;
            return true;
        }

        // 空きページの一覧: 先頭が数（全ページ）、続いて空きページの番号（0 〜 pages-1）。最初は全ページが空き。
        // 後ろの作業の領域（使用中の印・年齢）は 0 のまま（毎フレームの割り当てが作り直す）
        {
            Container::VariableArray<uint32_t> freeList;
            freeList.resize(static_cast<size_t>(plan.Pages) * 3u + 1u, 0u);
            freeList[0] = plan.Pages;
            for (uint32_t page = 0; page < plan.Pages; ++page)
            {
                freeList[static_cast<size_t>(page) + 1u] = page;
            }
            m_FreeList->Update(freeList.data(), freeListBytes);
        }

        // 統計の読み戻し先（host-visible）。作れない・写像できない装置では読み戻さない（記録は続く）
        for (StatsSlot& slot : m_StatsSlots)
        {
            slot.Buffer = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::STATS_BYTES, VirtualShadowMap::StatsReadbackUsage(), true, "VSM_StatsReadback"));
            slot.Mapped = slot.Buffer ? static_cast<const uint32_t*>(slot.Buffer->Map(0, 0)) : nullptr;
        }

        m_PoolPages = plan.Pages;
        m_bActive = true;
        const uint64_t rasterBytes = VirtualShadowMap::RasterInstanceBytes(VirtualShadowMap::RASTER_INSTANCE_CAPACITY) +
                                     m_Casters->Draws->GetSize();
        const uint64_t megaBytes = VirtualShadowMap::MegaCullListBytes(VirtualShadowMap::MEGA_CULL_LIST_CAPACITY) +
                                   VirtualShadowMap::MegaDirtyBitsBytes(SliceCapacityFor(m_PointShadowMethod)) +
                                   VirtualShadowMap::RasterChunkBytes(VirtualShadowMap::MEGA_CULL_LIST_CAPACITY);
        if (m_Gpu)
        {
            m_Gpu->SetShadowMapPoolBytes(poolBytes + rasterBytes + megaBytes);
        }

        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_pool pages=%u mb=%.3f",
                        plan.Pages,
                        static_cast<double>(poolBytes) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_page_table mb=%.3f",
                        static_cast<double>(VirtualShadowMap::PageTableBytes(SliceCapacityFor(m_PointShadowMethod))) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_raster mb=%.3f",
                        static_cast<double>(rasterBytes) / BytesPerMegabyte);
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VRAM_LEDGER vsm_mega_cull mb=%.3f",
                        static_cast<double>(megaBytes) / BytesPerMegabyte);
#if NORVES_BUILD_DEVELOPMENT
        if (m_MegaPass)
        {
            // Initialize より後に主パスが結ばれても最初の宣言で登録する。登録済みの所有権は主パスが除外する。
            // 作成時と同じ用途・名前・属性の記述子を渡す（容量は現在のバッファから取る）。
            const RHI::BufferDesc listDesc(m_MegaList ? m_MegaList->GetSize() : 0u,
                                          VirtualShadowMap::MegaCullListUsage(), false, "VsmMega_List");
            const RHI::BufferDesc dirtyDesc(m_MegaDirtyBits ? m_MegaDirtyBits->GetSize() : 0u,
                                           VirtualShadowMap::MegaDirtyBitsUsage(), false, "VsmMega_DirtyBits");
            const RHI::BufferDesc chunksDesc(m_MegaChunks ? m_MegaChunks->GetSize() : 0u,
                                            VirtualShadowMap::MegaChunkUsage(), false, "VsmMega_Chunks");
            m_MegaPass->NoteGpuSceneBuffer(m_MegaList, listDesc, false);
            m_MegaPass->NoteGpuSceneBuffer(m_MegaDirtyBits, dirtyDesc, false);
            m_MegaPass->NoteGpuSceneBuffer(m_MegaChunks, chunksDesc, false);
        }
#endif
        m_bInitialized = true;
        return true;
    }

    void VirtualShadowMapPass::Shutdown()
    {
        ReleaseResources();
        m_PoolHandle = {};
        m_PageTableHandle = {};
        m_RequestBitsHandle = {};
        m_FreeListHandle = {};
        m_StatsHandle = {};
        m_DirtyListHandle = {};
        m_SkinnedVerticesHandle = {};
        m_MegaCompleteHandle = {};
        m_DepthHandle = {};
        m_bDeclared = false;
        m_bActive = false;
        m_bInitialFilled = false;
        m_bMarked = false;
        m_bRasterRecorded = false;
        m_bMegaCullRecorded = false;
        m_LastCasterChunkCount = 0;
        m_bStatsLogged = false;
        m_FramesSinceStatsLog = 0;
        m_StatsWriteIndex = StatsSlotCount - 1;
        m_StatsWriteSerial = 0;
        m_Device = nullptr;
        m_Gpu = nullptr;
        m_bInitialized = false;
    }

    void VirtualShadowMapPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VirtualShadowMapPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void VirtualShadowMapPass::Declare(RenderGraphBuilder& builder)
    {
#if NORVES_BUILD_DEVELOPMENT
        if (m_MegaPass)
        {
            // Initialize より後に主パスが結ばれても最初の宣言で登録する。登録済みの所有権は主パスが除外する。
            // 作成時と同じ用途・名前・属性の記述子を渡す（容量は現在のバッファから取る）。
            const RHI::BufferDesc listDesc(m_MegaList ? m_MegaList->GetSize() : 0u,
                                          VirtualShadowMap::MegaCullListUsage(), false, "VsmMega_List");
            const RHI::BufferDesc dirtyDesc(m_MegaDirtyBits ? m_MegaDirtyBits->GetSize() : 0u,
                                           VirtualShadowMap::MegaDirtyBitsUsage(), false, "VsmMega_DirtyBits");
            const RHI::BufferDesc chunksDesc(m_MegaChunks ? m_MegaChunks->GetSize() : 0u,
                                            VirtualShadowMap::MegaChunkUsage(), false, "VsmMega_Chunks");
            m_MegaPass->NoteGpuSceneBuffer(m_MegaList, listDesc, false);
            m_MegaPass->NoteGpuSceneBuffer(m_MegaDirtyBits, dirtyDesc, false);
            m_MegaPass->NoteGpuSceneBuffer(m_MegaChunks, chunksDesc, false);
        }
#endif
        m_PoolHandle = {};
        m_PageTableHandle = {};
        m_RequestBitsHandle = {};
        m_FreeListHandle = {};
        m_StatsHandle = {};
        m_DirtyListHandle = {};
        m_SkinnedVerticesHandle = {};
        m_MegaCompleteHandle = {};
        m_DepthHandle = {};
        m_bDeclared = false;

        // 作れなかった構成（装置の非対応・確保の失敗）では何も宣言しない
        if (!m_bActive)
        {
            return;
        }

        // 深度が確定した後に並べるため、GBuffer の深度を読む（印付けの入力）・法線を読む（並びのための依存。中身は読まない）。
        // 無い構成（GBuffer を作らない）では読まず、資源の公開だけをする（印付けはしない）
        RGTextureHandle depth;
        RGTextureHandle normal;
        if (builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, depth) &&
            builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth, depth, RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depth;
        }
        if (builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal))
        {
            builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal, normal, RHI::ResourceState::ShaderResource);
        }

        // 永続のバッファを取り込み、名前で公開する。状態の遷移は、実行の中で明示的に（Common ↔ CopyDest）行う
        const DeclaredBuffer buffers[] = {
            {RenderGraphResourceNames::VsmPhysicalPool, &m_Pool, &m_PoolHandle, "VSM_PhysicalPool"},
            {RenderGraphResourceNames::VsmPageTable, &m_PageTable, &m_PageTableHandle, "VSM_PageTable"},
            {RenderGraphResourceNames::VsmRequestBits, &m_RequestBits, &m_RequestBitsHandle, "VSM_RequestBits"},
            {RenderGraphResourceNames::VsmFreeList, &m_FreeList, &m_FreeListHandle, "VSM_FreeList"},
            {RenderGraphResourceNames::VsmStats, &m_Stats, &m_StatsHandle, "VSM_Stats"},
            {RenderGraphResourceNames::VsmDirtyList, &m_DirtyList, &m_DirtyListHandle, "VSM_DirtyList"},
        };
        bool bAllPublished = true;
        for (const DeclaredBuffer& declared : buffers)
        {
            *declared.Handle = builder.ImportBuffer(*declared.Buffer, RHI::ResourceState::Common, declared.DebugName);
            if (!declared.Handle->IsValid())
            {
                bAllPublished = false;
                continue;
            }
            builder.Write(*declared.Handle, RHI::ResourceState::Common, RHI::ResourceState::Common);
            bAllPublished = builder.PublishBuffer(declared.Name, *declared.Handle) && bAllPublished;
        }
        m_bDeclared = bAllPublished;

        // スキニングの変形した頂点は、投影物の描画が（記録の頂点のアドレスから）読む。SkinningComputePass が、このフレームに
        // 変形するインスタンスを持つときだけ宣言する（名前を持たないフレームに読むと、グラフのエラーになる）。変形の後に並べ、読める状態にする
        if (m_SkinningPass)
        {
            const RGResourceHandle skinnedVertices = m_SkinningPass->GetCurrentVerticesHandle();
            if (skinnedVertices.IsValid())
            {
                builder.Read(skinnedVertices, RHI::ResourceState::GenericRead);
                m_SkinnedVerticesHandle = skinnedVertices;
            }
        }
        // MegaGeometry の投影物のカリングは、主の経路のインスタンスの表・ページの表を読む。主のカリング（2 パス目まで）の記録が
        // 済んだ後に並べるため、MegaGeometryPass の完了（論理資源）を読む。MegaGeometryPass が宣言していない構成（無効）では読まない
        if (m_MegaPass)
        {
            const RGResourceHandle megaComplete = m_MegaPass->GetMegaGeometryCompleteHandle();
            if (megaComplete.IsValid())
            {
                builder.Read(megaComplete, RHI::ResourceState::Common);
                m_MegaCompleteHandle = megaComplete;
            }
        }
        builder.PreserveInsertionOrder();
    }

    void VirtualShadowMapPass::CollectCasters(ViewRenderContext& context)
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        state.Chunks.clear();
        state.Motion.clear();
        state.Stats = {};
        state.bUseSliceTable = false;
        state.SliceTableCount = 0;
        const VirtualShadowMapClipmap& clipmap = context.PhysicalLighting.SunClipmap;
        // 点光源の VSM で灯があるときは、太陽の段（クリップマップが無効なら空の段）の後ろに点光源の面を並べたスライスの表で、
        // 投影物が写るスライスを決める（光源の Range の内側で面の錐台と交わる物が記録になる）
        if (m_PointShadowMethod == PointShadowMethod::Vsm && m_PointLights.LightCount != 0u)
        {
            state.SliceTableCount = SliceCapacityFor(m_PointShadowMethod);
            BuildVirtualShadowMapSlices(clipmap.bEnabled ? &clipmap : nullptr, nullptr, state.SliceTableCount, state.SliceTable);
            BuildVirtualShadowMapPointSlices(m_PointLights, state.SliceTable);
            state.bUseSliceTable = true;
        }
        if (!clipmap.bEnabled && !state.bUseSliceTable)
        {
            return;
        }
        const VirtualShadowMap::CasterSliceTable sliceTable{state.SliceTable, state.SliceTableCount};
        const VirtualShadowMap::CasterSliceTable* slices = state.bUseSliceTable ? &sliceTable : nullptr;

        // ----- 手続きメッシュ: 影を落とすメッシュのプロキシ -----
        // 描画コマンドは主カメラの錐台で省かれた後の一覧なので、錐台の外でも VSM の段の範囲に入る投影物を落とす。
        // カリング前の全プロキシ（FramePacket::Scene.MeshProxies）から、サブメッシュ（無ければメッシュ全体）ごとに集める
        MeshResources* meshes = context.Resources.Meshes;
        if (meshes && context.SnapshotMeshProxies)
        {
            for (const MeshProxy& proxy : *context.SnapshotMeshProxies)
            {
                if (!proxy.IsValid() || !proxy.bCastShadow)
                {
                    continue;
                }
                const auto* gpuData = meshes->GetGPUData(proxy.MeshHandle);
                if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer)
                {
                    continue;
                }

                float world[16] = {};
                Math::MatrixUtils::CopyToShaderData(proxy.WorldTransform, world);

                // サブメッシュが無いプロキシはメッシュ全体を 1 つの範囲にする（GBuffer・ビジビリティバッファの経路と同じ範囲の選び方）
                const uint32_t rangeCount = proxy.SubMeshCount == 0u ? 1u : std::min(proxy.SubMeshCount, MAX_MATERIAL_SLOTS);
                for (uint32_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
                {
                    // サブメッシュがあっても IndexCount が 0 なら、CSM と同じくメッシュ全体（先頭 0・頂点の基点 0）として扱う
                    const bool bHasRange = proxy.SubMeshCount != 0u && proxy.SubMeshes[rangeIndex].IndexCount > 0u;
                    VirtualShadowMap::ProceduralDrawInput input;
                    input.VertexAddress = gpuData->VertexBuffer->GetDeviceAddress();
                    input.IndexAddress = gpuData->IndexBuffer->GetDeviceAddress();
                    input.FirstIndex = bHasRange ? proxy.SubMeshes[rangeIndex].IndexStart : 0u;
                    input.IndexCount = bHasRange ? proxy.SubMeshes[rangeIndex].IndexCount : gpuData->IndexCount;
                    input.VertexOffset = bHasRange ? proxy.SubMeshes[rangeIndex].VertexStart : 0u;
                    input.MeshBounds = gpuData->bHasLocalBounds ? &gpuData->LocalBounds : nullptr;
                    input.BlockBounds = gpuData->BlockBounds.empty() ? nullptr : gpuData->BlockBounds.data();
                    input.BlockBoundsCount = static_cast<uint32_t>(gpuData->BlockBounds.size());
                    if (!VirtualShadowMap::PlanProceduralChunks(input, state.PlanScratch, state.Plan))
                    {
                        ++state.Stats.SkippedDraws;
                        continue;
                    }
                    VirtualShadowMap::AppendProceduralInstance(input, state.Plan, world, clipmap, state.Chunks, state.Stats, slices);

                    // キャッシュの無効化の入力: 同じ描画が同じ変換・範囲なら動いていない
                    VirtualShadowMap::CasterMotionEntry motion;
                    motion.Key = VirtualShadowMap::CasterHashCombine(VirtualShadowMap::CasterHashCombine(proxy.ComponentId, proxy.ObjectId), rangeIndex);
                    uint64_t signature = VirtualShadowMap::CasterHashFloats(1469598103934665603ull, world, 16u);
                    signature = VirtualShadowMap::CasterHashCombine(signature, input.VertexAddress);
                    signature = VirtualShadowMap::CasterHashCombine(signature, input.IndexAddress);
                    signature = VirtualShadowMap::CasterHashCombine(signature, input.FirstIndex);
                    signature = VirtualShadowMap::CasterHashCombine(signature, input.IndexCount);
                    signature = VirtualShadowMap::CasterHashCombine(signature, input.VertexOffset);
                    motion.Signature = signature;
                    motion.bHasBounds = true;
                    motion.Bounds = VirtualShadowMap::TransformBoundsByWorld(world, *input.MeshBounds);
                    state.Motion.push_back(motion);
                }
            }
        }

        // ----- スキニング: SkinningComputePass が変形した頂点（ワールド空間）。境界は描画の境界 -----
        if (m_SkinningPass && context.SnapshotSkinnedMeshProxies)
        {
            const Container::VariableArray<SkinningComputeInstance>& instances = m_SkinningPass->GetInstances();
            for (const SkinningComputeInstance& instance : instances)
            {
                if (!instance.IndexBuffer || instance.IndexCount < 3u)
                {
                    continue;
                }
                const uint64_t indexAddress = instance.IndexBuffer->GetDeviceAddress();
                const SkinnedMeshProxy* proxy = FindSkinnedProxy(*context.SnapshotSkinnedMeshProxies, instance.SourceMeshComponentId);
                // 影を落とすかは元の描画ごとに見る（サブメッシュに分けた描画は、サブメッシュの影なしの印を含む）
                if (!instance.bCastShadow || (proxy && !proxy->bCastShadow))
                {
                    continue;
                }
                VirtualShadowMap::CasterBounds bounds;
                if (instance.CurrentVertexAddress == 0u || indexAddress == 0u || !proxy || !BuildSkinnedWorldBounds(*proxy, bounds))
                {
                    ++state.Stats.SkippedDraws;
                    continue;
                }
                // 元の描画のインデックスの範囲。数が 0 か、メッシュ全体を覆う範囲なら全体として扱う
                const bool bWholeMesh = instance.SourceIndexCount == 0u ||
                                        (instance.SourceFirstIndex == 0u && instance.SourceIndexCount >= instance.IndexCount);
                Container::VariableArray<MeshIndexChunk>& chunks = state.SkinnedChunks;
                if (bWholeMesh)
                {
                    // 登録時に分けた塊を使い、無ければインデックスの全体を分ける
                    if (!context.SkinnedMeshes || !context.SkinnedMeshes->TryGetChunks(instance.MeshHandle, chunks) || chunks.empty())
                    {
                        if (!BuildMeshIndexChunks(instance.IndexCount, nullptr, 0, chunks))
                        {
                            ++state.Stats.SkippedDraws;
                            continue;
                        }
                    }
                }
                else
                {
                    // サブメッシュの範囲だけを塊に分け、塊の先頭をインデックスバッファの中の位置へずらす
                    if (instance.SourceFirstIndex % 3u != 0u || instance.SourceIndexCount % 3u != 0u ||
                        instance.SourceFirstIndex > instance.IndexCount ||
                        instance.SourceIndexCount > instance.IndexCount - instance.SourceFirstIndex ||
                        !BuildMeshIndexChunks(instance.SourceIndexCount, nullptr, 0, chunks))
                    {
                        ++state.Stats.SkippedDraws;
                        continue;
                    }
                    for (MeshIndexChunk& chunk : chunks)
                    {
                        chunk.FirstIndex += instance.SourceFirstIndex;
                    }
                }
                VirtualShadowMap::AppendSkinnedInstance(
                    instance.CurrentVertexAddress, indexAddress, bounds, chunks, clipmap, state.Chunks, state.Stats, slices);

                // スキニングは毎フレーム変形するので、毎フレーム動いた物として扱う（鍵はコンポーネントとサブメッシュの範囲ごと）
                VirtualShadowMap::CasterMotionEntry motion;
                motion.Key = VirtualShadowMap::CasterHashCombine(VirtualShadowMap::CasterHashCombine(0x534B494Eull, instance.SourceMeshComponentId),
                                                                 instance.SourceFirstIndex);
                motion.bAlwaysChanged = true;
                motion.bHasBounds = true;
                motion.Bounds = bounds;
                state.Motion.push_back(motion);
            }
        }

        // ----- 草・低木の株: カメラに近い株（束の ShadowDistance 以内）を近い順に、株の塊の予算まで -----
        const CameraProxy* camera = context.GetActiveCamera();
        if (context.SnapshotFoliage && !context.SnapshotFoliage->IsEmpty() && camera)
        {
            CollectFoliageCasters(m_Device, state, *context.SnapshotFoliage, *camera, context.TotalTime, clipmap, slices);
        }

        // MegaGeometry の動きは前フレームと全部を比べず、MegaGeometryPass が変わった席・メッシュの変化から積んだ箱を
        // PlanInvalidation が取り出す（本数ではなく変わった席の数に比例する）
    }

    void VirtualShadowMapPass::PlanInvalidation(ViewRenderContext& context)
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        state.ChangedBounds.clear();
        state.InvalidationRects.clear();
        state.InvalidationSpheres.clear();
        state.bInvalidateAll = false;
        // MegaGeometry: 前に取り出してから積まれた、変わった席の旧・新の境界と、常駐・メッシュが変わった席の境界
        TakeMegaInvalidationFrom(m_MegaPass, state);
        // 太陽の段（矩形）も点光源の面（球）も無いフレームは、無効にする範囲が無い
        const bool bSun = context.PhysicalLighting.SunClipmap.bEnabled;
        const bool bPoint = state.bUseSliceTable;
        if (!m_bCacheEnabled || (!bSun && !bPoint))
        {
            state.MotionTracker.Reset();
            return;
        }
        state.MotionTracker.Update(state.Motion, state.ChangedBounds, state.bInvalidateAll);
        // MegaGeometry の箱は取り出した分をそのまま無効にする（多く描き直すぶんには影は崩れない）
        state.bInvalidateAll = state.bInvalidateAll || state.MegaInvalidation.bInvalidateAll;
        for (const MegaGeometryShadowBox& box : state.MegaInvalidation.Boxes)
        {
            VirtualShadowMap::CasterBounds bounds;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                bounds.Min[axis] = box.Min[axis];
                bounds.Max[axis] = box.Max[axis];
            }
            state.ChangedBounds.push_back(bounds);
        }
        if (state.bInvalidateAll)
        {
            return;
        }
        // 太陽の段は動いた境界のライト空間の矩形、点光源の面は境界を覆う球（面の NDC で覆うページ）を無効にする。
        // 数が多すぎる・境界が有限でない: 範囲を絞れないので全ページを無効にする
        if ((bSun && !VirtualShadowMap::BuildInvalidationRects(context.PhysicalLighting.SunClipmap,
                                                               state.ChangedBounds,
                                                               VirtualShadowMap::MAX_INVALIDATION_RECTS,
                                                               state.InvalidationRects)) ||
            (bPoint && !VirtualShadowMap::BuildInvalidationSpheres(state.ChangedBounds,
                                                                   VirtualShadowMap::MAX_INVALIDATION_RECTS,
                                                                   state.InvalidationSpheres)))
        {
            state.InvalidationRects.clear();
            state.InvalidationSpheres.clear();
            state.bInvalidateAll = true;
        }
    }

    void VirtualShadowMapPass::ReportCasters()
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        const VirtualShadowMap::CasterStats& stats = state.Stats;
        // 何も集めていない間は出さない（投影物の無い構成を毎回ログで埋めない）。一度出したら、0 に戻ったときも出す
        const bool bAnything = stats.ProceduralChunks != 0u || stats.SkinnedChunks != 0u || stats.CulledChunks != 0u ||
                               stats.DroppedChunks != 0u || stats.SkippedDraws != 0u || stats.FoliageChunks != 0u ||
                               stats.FoliageOverBudget != 0u;
        if (!state.bLogged && !bAnything)
        {
            return;
        }
        ++state.ReportsSinceLog;
        if (state.bLogged && stats == state.LoggedStats && state.ReportsSinceLog < CasterLogIntervalExecutions)
        {
            return;
        }
        state.LoggedStats = stats;
        state.bLogged = true;
        state.ReportsSinceLog = 0;
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_CASTERS procedural_chunks=%u skinned_chunks=%u culled=%u dropped=%u skipped=%u "
                        "foliage_plants=%u foliage_chunks=%u foliage_over_budget=%u foliage_farthest_m=%.1f "
                        "foliage_priority0_farthest_m=%.2f foliage_priority0_over_budget=%u foliage_lod_plants=%u/%u/%u/%u",
                        stats.ProceduralChunks,
                        stats.SkinnedChunks,
                        stats.CulledChunks,
                        stats.DroppedChunks,
                        stats.SkippedDraws,
                        stats.FoliageDraws,
                        stats.FoliageChunks,
                        stats.FoliageOverBudget,
                        static_cast<double>(state.FoliageFarthest),
                        static_cast<double>(state.FoliagePriority0Farthest),
                        state.FoliagePriority0OverBudget,
                        state.FoliageLodPlants[0],
                        state.FoliageLodPlants[1],
                        state.FoliageLodPlants[2],
                        state.FoliageLodPlants[3]);
    }

    bool VirtualShadowMapPass::RecordRaster(ViewRenderContext& context, uint64_t /*frameSerial*/)
    {
        VirtualShadowMapCasterState& state = *m_Casters;
        const uint32_t chunkCount = static_cast<uint32_t>(state.Chunks.size());
        // MegaGeometry のクラスタの記録（直前のカリングが作ったもの）は、ホストが書いた塊が 0 件でも描く
        const bool bMega = m_bMegaCullRecorded && m_MegaChunks && m_MegaList && m_Raster->SupportsMegaCasters();
        if ((chunkCount == 0u && !bMega) || chunkCount > VirtualShadowMap::MAX_CASTER_CHUNKS || !state.Instances || !state.Draws)
        {
            return false;
        }

        // 塊の記録は、この Execute 専用のバッファへ書く（ホストが書く。足りなければ 2 倍ずつ広げる）
        VirtualShadowMapCasterState::Use& use = state.Uses.Acquire();
        if (!use.Chunks || use.ChunkCapacity < chunkCount)
        {
            const uint32_t wanted = std::max(chunkCount, std::min(VirtualShadowMap::MAX_CASTER_CHUNKS, std::max(256u, use.ChunkCapacity * 2u)));
            RHI::FrameResourceProtectedCreationScope protectedCreation;
            use.Chunks = m_Device->CreateBuffer(
                RHI::BufferDesc(VirtualShadowMap::RasterChunkBytes(wanted), VirtualShadowMap::RasterChunkUsage(), true, "VsmRaster_Chunks"));
            use.ChunkCapacity = use.Chunks ? wanted : 0u;
            if (!use.Chunks)
            {
                return false;
            }
        }
        if (chunkCount != 0u)
        {
            use.Chunks->Update(state.Chunks.data(), static_cast<uint64_t>(chunkCount) * sizeof(VsmShadowChunk));
        }

        RHI::ICommandList* commandList = context.CommandList;
        RHI::BufferPtr rasterBuffers[5] = {use.Chunks, state.Instances, state.Draws};
        uint32_t rasterBufferCount = 3;
        if (bMega)
        {
            // カリングの記録の後に Common へ戻した一覧とクラスタの記録を、展開・描画が読めるように UnorderedAccess へ遷移する
            rasterBuffers[rasterBufferCount++] = m_MegaChunks;
            rasterBuffers[rasterBufferCount++] = m_MegaList;
        }
        for (uint32_t index = 0; index < rasterBufferCount; ++index)
        {
            commandList->BufferBarrier(rasterBuffers[index], RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }

        VirtualShadowMapRasterDispatch rasterDispatch;
        rasterDispatch.Clipmap = &context.PhysicalLighting.SunClipmap;
        if (state.bUseSliceTable)
        {
            // 点光源の面を含むスライスの表（CollectCasters が作ったもの）で展開・描画する
            rasterDispatch.SliceCount = state.SliceTableCount;
            rasterDispatch.Slices = state.SliceTable;
        }
        rasterDispatch.PoolPages = m_PoolPages;
        rasterDispatch.Pool = m_Pool;
        rasterDispatch.PageTable = m_PageTable;
        rasterDispatch.Stats = m_Stats;
        rasterDispatch.Chunks = use.Chunks;
        rasterDispatch.ChunkCount = chunkCount;
        rasterDispatch.Instances = state.Instances;
        rasterDispatch.Draws = state.Draws;
        std::copy(state.FoliageWind, state.FoliageWind + 4, rasterDispatch.FoliageWind);
        std::copy(state.FoliageWindParams, state.FoliageWindParams + 4, rasterDispatch.FoliageWindParams);
        if (bMega)
        {
            rasterDispatch.MegaChunks = m_MegaChunks;
            rasterDispatch.MegaList = m_MegaList;
            rasterDispatch.MegaCapacity = VirtualShadowMap::MEGA_CULL_LIST_CAPACITY;
        }
        const bool bRecorded = m_Raster->Record(commandList, rasterDispatch);
        m_bMegaDrawRecorded = bRecorded && m_Raster->WasMegaDrawRecorded();

        for (uint32_t index = 0; index < rasterBufferCount; ++index)
        {
            commandList->BufferBarrier(rasterBuffers[index], RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
        return bRecorded;
    }

    bool VirtualShadowMapPass::RecordMegaCull(ViewRenderContext& context, uint64_t /*frameSerial*/)
    {
        if (!m_MegaCull || !m_MegaCull->IsReady() || !m_MegaPass || !m_MegaList || !m_MegaDirtyBits || !m_MegaChunks)
        {
            return false;
        }
        const MegaGeometryShadowCasterInputs& inputs = m_MegaPass->GetShadowCasterInputs();
        if (!inputs.bValid || inputs.CasterCount == 0u || inputs.TotalGroups == 0u)
        {
            return false;
        }

        RHI::ICommandList* commandList = context.CommandList;
        const RHI::BufferPtr megaBuffers[] = {m_MegaList, m_MegaDirtyBits, m_MegaChunks};
        for (const RHI::BufferPtr& buffer : megaBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }

        VirtualShadowMapMegaCullDispatch megaDispatch;
        megaDispatch.Clipmap = &context.PhysicalLighting.SunClipmap;
        if (m_Casters->bUseSliceTable)
        {
            // 点光源の面を含むスライスの表（CollectCasters が作ったもの）で、太陽の段と点光源の面を同じ流れでカリングする
            megaDispatch.SliceCount = m_Casters->SliceTableCount;
            megaDispatch.Slices = m_Casters->SliceTable;
        }
        megaDispatch.PageTable = m_PageTable;
        megaDispatch.Stats = m_Stats;
        megaDispatch.DirtyBits = m_MegaDirtyBits;
        megaDispatch.List = m_MegaList;
        megaDispatch.Chunks = m_MegaChunks;
        megaDispatch.Instances = inputs.InstanceBuffer;
        megaDispatch.Meshes = inputs.MeshBuffer;
        megaDispatch.ShadowInstances = inputs.ShadowInstanceBuffer;
        megaDispatch.MegaPageTable = inputs.PageTableBuffer;
        megaDispatch.InstanceCount = inputs.InstanceCount;
        megaDispatch.TotalGroups = inputs.TotalGroups;
        const bool bRecorded = m_MegaCull->Record(commandList, megaDispatch);

        for (const RHI::BufferPtr& buffer : megaBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
        return bRecorded;
    }

    void VirtualShadowMapPass::HarvestStats(StatsSlot& slot)
    {
        if (!slot.bPending || !slot.Mapped)
        {
            return;
        }
        slot.bPending = false;
        // MegaGeometry の投影物のカリングの統計（記録したフレームだけ。60 回ごとに出す）
        if (slot.bMegaCull && m_MegaReporter)
        {
            m_MegaReporter->Report(slot.Mapped[VirtualShadowMap::StatMegaInstances],
                                   slot.Mapped[VirtualShadowMap::StatMegaClusters],
                                   slot.Mapped[VirtualShadowMap::StatMegaOverflow],
                                   slot.Mapped[VirtualShadowMap::StatMegaPointInstances],
                                   slot.Mapped[VirtualShadowMap::StatMegaPointClusters]);
        }
        // 展開の統計（投影物を描かない間は 0 のままで、何も出さない）
        if (m_RasterReporter)
        {
            m_RasterReporter->Report(slot.Mapped[VirtualShadowMap::StatRasterChunks],
                                     slot.Mapped[VirtualShadowMap::StatRasterInstances],
                                     slot.Mapped[VirtualShadowMap::StatRasterOverflow]);
        }
        // キャッシュの内訳（60 フレームごと）
        if (++m_FramesSinceCacheLog >= StatsLogIntervalFrames)
        {
            m_FramesSinceCacheLog = 0;
            NORVES_LOG_INFO("VirtualShadowMapPass",
                            "VSM_CACHE cached=%u rendered=%u invalidated=%u released=%u point_cached=%u point_rendered=%u point_invalidated=%u point_released=%u",
                            slot.Mapped[VirtualShadowMap::StatCached],
                            slot.Mapped[VirtualShadowMap::StatRendered],
                            slot.Mapped[VirtualShadowMap::StatInvalidated],
                            slot.Mapped[VirtualShadowMap::StatReleased],
                            slot.Mapped[VirtualShadowMap::StatPointCached],
                            slot.Mapped[VirtualShadowMap::StatPointRendered],
                            slot.Mapped[VirtualShadowMap::StatPointInvalidated],
                            slot.Mapped[VirtualShadowMap::StatPointReleased]);
        }
#if NORVES_BUILD_DEVELOPMENT
        // 無効化の照合: 同じフレームの統計の無効にしたページの数と、動いた境界が覆う割り当て済みのページの数（CPU で数えた）を並べる
        if (slot.bSnapshot && slot.SnapshotMapped)
        {
            slot.bSnapshot = false;
            const InvalidationCheck check = CountExpectedInvalidation(slot.SnapshotClipmap, slot.SnapshotBounds, slot.SnapshotMapped);
            NORVES_LOG_INFO("VirtualShadowMapPass",
                            "VSM_INVALIDATE_EXPECTED pages=%u invalidated=%u point_invalidated=%u sun_dirty=%u missed=%u extra=%u bounds=%u",
                            check.Expected,
                            slot.Mapped[VirtualShadowMap::StatInvalidated],
                            slot.Mapped[VirtualShadowMap::StatPointInvalidated],
                            check.SunDirty,
                            check.Missed,
                            check.Extra,
                            static_cast<uint32_t>(slot.SnapshotBounds.size() / 6u));
        }
#endif
        const uint32_t stats[6] = {slot.Mapped[VirtualShadowMap::StatRequested],
                                   slot.Mapped[VirtualShadowMap::StatAllocated],
                                   slot.Mapped[VirtualShadowMap::StatOverflow],
                                   slot.Mapped[VirtualShadowMap::StatLevelsUsed],
                                   slot.Mapped[VirtualShadowMap::StatPointRequested],
                                   slot.Mapped[VirtualShadowMap::StatPointAllocated]};
        ++m_FramesSinceStatsLog;
        bool bChanged = !m_bStatsLogged;
        for (uint32_t index = 0; index < 6u; ++index)
        {
            bChanged = bChanged || stats[index] != m_LoggedStats[index];
        }
        if (!bChanged && m_FramesSinceStatsLog < StatsLogIntervalFrames)
        {
            return;
        }
        for (uint32_t index = 0; index < 6u; ++index)
        {
            m_LoggedStats[index] = stats[index];
        }
        m_bStatsLogged = true;
        m_FramesSinceStatsLog = 0;
        // requested・allocated は太陽と点光源の合計。point_* は点光源の面のページだけ
        NORVES_LOG_INFO("VirtualShadowMapPass",
                        "VSM_PAGES requested=%u allocated=%u overflow=%u levels_used=0x%x point_requested=%u point_allocated=%u",
                        stats[0],
                        stats[1],
                        stats[2],
                        stats[3],
                        stats[4],
                        stats[5]);
    }

    void VirtualShadowMapPass::HarvestReadyStats(uint64_t frameSerial, uint64_t completedFrameSerial)
    {
        // 書いた順（最後に書いた枠の次が最も古い）に、読める枠を読む。通し番号は書いた順に増えるので、読めない枠に当たったら
        // それより新しい枠も読めない
        for (uint32_t offset = 1; offset <= StatsSlotCount; ++offset)
        {
            StatsSlot& slot = m_StatsSlots[(m_StatsWriteIndex + offset) % StatsSlotCount];
            if (!slot.bPending)
            {
                continue;
            }
            const bool bDelayed = slot.FrameSerial + StatsReadbackMinFrameDelay <= frameSerial;
            const bool bGpuCompleted = slot.FrameSerial <= completedFrameSerial;
            if (!bDelayed || !bGpuCompleted)
            {
                break;
            }
            HarvestStats(slot);
        }
    }

    void VirtualShadowMapPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_bMarked = false;
        m_bRasterRecorded = false;
        m_bMegaCullRecorded = false;
        m_bMegaDrawRecorded = false;
        m_LastCasterChunkCount = 0;
        const bool bPointLightsPrepared = m_bPointLightsPrepared;
        m_bPointLightsPrepared = false;
        if (!m_bActive || !m_bDeclared || !m_Pages || !m_Raster || !m_Casters || !context.CommandList)
        {
            return;
        }

        RHI::ICommandList* commandList = context.CommandList;

        if (m_PointShadowMethod == PointShadowMethod::Vsm)
        {
            if (!bPointLightsPrepared)
            {
                UpdatePointLights(context);
            }
            // 照明・影の測定が同じ並びで点光源の面のページを読む（LightCount が 0 の間はキューブのまま）。
            // 並列の記録では写した context へ書くので、後のパスへは準備段の公開が届く
            context.PhysicalLighting.PublishPointVsmLights(m_PointLights);
        }

        // 統計の読み戻しの枠は、飛行中のフレームの数とは別に、書いたフレームの順に使う。読むのは、通し番号の差が
        // StatsReadbackMinFrameDelay 以上で、そのフレームの提出の完了が確かめられた枠だけ（コーディネーターが渡す
        // CompletedRenderFrameSerial）。飛行中のフレームが 1 枠でも 3 枠以上でも、書いた GPU の仕事が終わる前には読まない。
        // 同じフレームの Execute（複数のビューポート）は通し番号が同じなので、提出前の枠を読まない（Execute の回数では数えない）
        const uint64_t frameSerial = context.ResolveRenderFrameSerial();
        HarvestReadyStats(frameSerial, context.CompletedRenderFrameSerial);

        // 最初の実行で、何も無い texel の深度（1.0）でプールを埋める（以後は dirty のページだけを消去する）。
        // 表・要求・統計・空きページ・消去の一覧は、毎フレームの記録が作り直す
        if (!m_bInitialFilled)
        {
            commandList->BufferBarrier(m_Pool, RHI::ResourceState::Common, RHI::ResourceState::CopyDest);
            commandList->FillBuffer(m_Pool, 0, VirtualShadowMap::PoolBytes(m_PoolPages), VirtualShadowMap::EMPTY_DEPTH_BITS);
            commandList->BufferBarrier(m_Pool, RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
            m_bInitialFilled = true;
        }

        VirtualShadowMapPagesDispatch dispatch;
        dispatch.PoolPages = m_PoolPages;
        dispatch.Pool = m_Pool;
        dispatch.PageTable = m_PageTable;
        dispatch.RequestBits = m_RequestBits;
        dispatch.FreeList = m_FreeList;
        dispatch.Stats = m_Stats;
        dispatch.DirtyList = m_DirtyList;
        // 点光源の VSM のときは、太陽の段の後ろに点光源の面を並べたスライスの数で記録する（印付けが点光源の面にも印を付ける）
        dispatch.SliceCount = SliceCapacityFor(m_PointShadowMethod);
        if (m_PointShadowMethod == PointShadowMethod::Vsm)
        {
            dispatch.PointLights = &m_PointLights;
        }
        const CameraProxy* camera = context.GetActiveCamera();
        const RHI::TexturePtr depth = m_DepthHandle.IsValid() ? resources.GetTexture(m_DepthHandle) : RHI::TexturePtr{};
        if (depth && camera && camera->Projection == ProjectionType::Perspective)
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyShaderCameraRelativeInverseViewProjection(dispatch.InverseViewProjection);
            float depthLinearization[2] = {};
            cameraConstants.CopyDepthLinearization(depthLinearization);
            dispatch.DepthNearOverFar = depthLinearization[1];
            dispatch.CameraPosition[0] = camera->PositionX;
            dispatch.CameraPosition[1] = camera->PositionY;
            dispatch.CameraPosition[2] = camera->PositionZ;
            dispatch.CameraForward[0] = camera->ForwardX;
            dispatch.CameraForward[1] = camera->ForwardY;
            dispatch.CameraForward[2] = camera->ForwardZ;
            // 影の範囲は照明の読み出しと同じ値（CSM の分割）で測る
            float shadowFade = 0.0f;
            ResolveVirtualShadowMapViewRange(context.PhysicalLighting.SunClipmap.Settings,
                                             context.PhysicalLighting.CascadedShadow.SplitDistances,
                                             dispatch.ShadowNearMeters,
                                             dispatch.ShadowFarMeters,
                                             shadowFade);
            dispatch.FovYDegrees = camera->FieldOfView;
            dispatch.Depth = depth;
            dispatch.Clipmap = &context.PhysicalLighting.SunClipmap;
        }

        const RHI::BufferPtr buffers[] = {m_Pool, m_PageTable, m_RequestBits, m_FreeList, m_Stats, m_DirtyList};
        for (const RHI::BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::Common, RHI::ResourceState::UnorderedAccess);
        }

        m_Pages->BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_Raster->BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_Casters->Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        if (m_MegaCull)
        {
            m_MegaCull->BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        }
        // 投影物を集め、前フレームからの動き（無効にするページの範囲）を決める。印付けをするフレームだけ（しなければ記録も要らない）
        bool bCollected = false;
        // 点光源の VSM で灯があれば、太陽が無くても集める・描く（点光源の面にだけ描く）
        const bool bPointCasters = m_PointShadowMethod == PointShadowMethod::Vsm && m_PointLights.LightCount != 0u;
        if (dispatch.Depth && dispatch.Clipmap && (dispatch.Clipmap->bEnabled || bPointCasters) && m_Raster->IsReady())
        {
            CollectCasters(context);
            PlanInvalidation(context);
            bCollected = true;
            dispatch.bCacheEnabled = m_bCacheEnabled;
            dispatch.InvalidationRects = m_Casters->InvalidationRects.empty() ? nullptr : m_Casters->InvalidationRects.data();
            dispatch.InvalidationRectCount = static_cast<uint32_t>(m_Casters->InvalidationRects.size() / 4u);
            dispatch.InvalidationSpheres = m_Casters->InvalidationSpheres.empty() ? nullptr : m_Casters->InvalidationSpheres.data();
            dispatch.InvalidationSphereCount = static_cast<uint32_t>(m_Casters->InvalidationSpheres.size() / 4u);
            dispatch.bInvalidateAll = m_Casters->bInvalidateAll;
        }
        else
        {
            dispatch.bCacheEnabled = m_bCacheEnabled;
            // 集めなかったフレームの動きは見ていないので、次に集めるとき前の記録との差が大きい。記録を捨てて新しく始める
            // （MegaGeometry の積まれた無効化も同じく捨てる）
            m_Casters->MotionTracker.Reset();
            TakeMegaInvalidationFrom(m_MegaPass, *m_Casters);
        }

        // 統計を写す読み戻しの枠。同じフレームの 2 回目以降の Execute は、同じ枠を新しい統計で上書きする。
        // 新しいフレームは次の枠を使い、その枠がまだ読まれていない（GPU の完了が確かめられていない）ときは、このフレームの統計は取らない
        StatsSlot* slot = nullptr;
        if (m_StatsWriteSerial == frameSerial)
        {
            slot = &m_StatsSlots[m_StatsWriteIndex];
        }
        else
        {
            const uint32_t nextIndex = (m_StatsWriteIndex + 1u) % StatsSlotCount;
            if (!m_StatsSlots[nextIndex].bPending)
            {
                m_StatsWriteIndex = nextIndex;
                m_StatsWriteSerial = frameSerial;
                slot = &m_StatsSlots[nextIndex];
            }
        }
        const bool bStatsSlot = slot && slot->Buffer && slot->Mapped;
#if NORVES_BUILD_DEVELOPMENT
        // 無効化の照合: 太陽の段を動いた境界の矩形で無効にするフレームを 60 フレームごとに選び、無効化の直後のページの表を写す
        bool bSnapshotWanted = false;
        if (bStatsSlot && bCollected && m_bCacheEnabled && !m_Casters->bInvalidateAll && dispatch.Clipmap->bEnabled &&
            ++m_FramesSinceSnapshot >= StatsLogIntervalFrames)
        {
            if (!slot->Snapshot)
            {
                slot->Snapshot = m_Device->CreateBuffer(RHI::BufferDesc(VirtualShadowMap::PageTableBytes(VirtualShadowMap::LEVEL_COUNT),
                                                                        VirtualShadowMap::StatsReadbackUsage(),
                                                                        true,
                                                                        "VSM_InvalidationSnapshot"));
                slot->SnapshotMapped = slot->Snapshot ? static_cast<const uint32_t*>(slot->Snapshot->Map(0, 0)) : nullptr;
            }
            if (slot->SnapshotMapped)
            {
                dispatch.PageTableSnapshot = slot->Snapshot;
                bSnapshotWanted = true;
            }
        }
#endif
        m_Pages->Record(commandList, dispatch);
        m_bMarked = m_Pages->WasMarked();

        // 影を落とす投影物の塊を物理ページへ描く。印付けをしなかった（深度かクリップマップが無い）フレームは、割り当て済みのページが無いので何も描かない
        m_bRasterRecorded = false;
        m_LastCasterChunkCount = 0;
        if (m_bMarked && bCollected)
        {
            ReportCasters();
            // MegaGeometry の投影物のカリング（展開の前。出力は VsmMega_List。主の経路のバッファには書かない）
            // （太陽が無効でも、点光源の面のスライスの表があれば点光源の面を対象に記録する）
            m_bMegaCullRecorded = (dispatch.Clipmap->bEnabled || m_Casters->bUseSliceTable) && RecordMegaCull(context, frameSerial);
            // ホストが書いた塊がある、または MegaGeometry のクラスタの記録を作ったフレームは、展開・描画を 1 回の流れで記録する
            if (!m_Casters->Chunks.empty() || m_bMegaCullRecorded)
            {
                m_bRasterRecorded = RecordRaster(context, frameSerial);
                m_LastCasterChunkCount = m_bRasterRecorded ? static_cast<uint32_t>(m_Casters->Chunks.size()) : 0u;
            }
        }

        // 統計を読み戻しの枠へ写す
        if (bStatsSlot)
        {
            VirtualShadowMap::RecordStatsReadback(*commandList, m_Stats, slot->Buffer);
            slot->bPending = true;
            slot->FrameSerial = frameSerial;
            slot->bMegaCull = m_bMegaCullRecorded;
            slot->bSnapshot = false;
#if NORVES_BUILD_DEVELOPMENT
            if (bSnapshotWanted && m_Pages->WasSnapshotRecorded())
            {
                // 照合に使う、このフレームの動いた境界とクリップマップ
                slot->bSnapshot = true;
                slot->SnapshotClipmap = *dispatch.Clipmap;
                slot->SnapshotBounds.clear();
                for (const VirtualShadowMap::CasterBounds& bounds : m_Casters->ChangedBounds)
                {
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        slot->SnapshotBounds.push_back(bounds.Min[axis]);
                    }
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        slot->SnapshotBounds.push_back(bounds.Max[axis]);
                    }
                }
                m_FramesSinceSnapshot = 0;
            }
#endif
        }

        // 宣言した状態（Common）へ戻す
        for (const RHI::BufferPtr& buffer : buffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::Common);
        }
    }

    bool VirtualShadowMapPass::DeclaresAllLayoutChanges() const
    {
        // グラフの Texture は深度を読むだけ（ShaderResource）で、ページの描画の render pass は添付を持たない。
        // パスの中のバリアはすべてバッファ（宣言の Common へ戻す）なので layout を変えない
        return true;
    }

    RGRecordPolicy VirtualShadowMapPass::GetRecordPolicy() const
    {
        // Execute が書くのは自分のメンバ・リング・バッファと MegaGeometry の影の無効化の取り出しだけ。後のパスが読む
        // 点光源の並びの公開は準備段で済ませる。MegaGeometry を記録するパスとは ReadsRecordingOf で並びを分ける
        return RGRecordPolicy::Parallel;
    }

    void VirtualShadowMapPass::PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context)
    {
        (void)resources;
        m_bPointLightsPrepared = false;
        if (!m_bActive || !m_bDeclared || m_PointShadowMethod != PointShadowMethod::Vsm)
        {
            return;
        }

        // LightingPass・ShadowProbePass が読む点光源の並びを、写す前の context へ公開する（Execute はこの並びで記録する）
        UpdatePointLights(context);
        context.PhysicalLighting.PublishPointVsmLights(m_PointLights);
        m_bPointLightsPrepared = true;
    }

    bool VirtualShadowMapPass::ReadsRecordingOf(const IRenderGraphPass& earlierPass) const
    {
        // MegaGeometry の投影物の入力・影の無効化（MegaGeometryPass と、その記録を担う VisibilityRasterPass が Execute で書く）と、
        // スキニングの instance の並び（SkinningComputePass の Execute が作る）を読む
        if (m_MegaPass && &earlierPass == static_cast<const IRenderGraphPass*>(m_MegaPass))
        {
            return true;
        }
        if (m_SkinningPass && &earlierPass == static_cast<const IRenderGraphPass*>(m_SkinningPass))
        {
            return true;
        }
        return dynamic_cast<const VisibilityRasterPass*>(&earlierPass) != nullptr;
    }

} // namespace NorvesLib::Core::Rendering
