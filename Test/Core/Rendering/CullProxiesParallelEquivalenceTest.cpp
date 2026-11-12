// メッシュの可視順・要素・SortDepth と板による抑制集合が直列と並列で一致する契約を確かめる。
// 同じ入力で並列カリングを繰り返しても、全メッシュの SortDepth を含む結果が変わらないことを確かめる。
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "Rendering/SceneView.h"
#include "Rendering/Viewport.h"
#include "Thread/JobSystem.h"

#include <bit>
#include <cstdint>
#include <iostream>
#include <random>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    struct MeshResult
    {
        uint64_t ComponentId;
        uint32_t SortDepthBits;
    };

    struct CullResult
    {
        Container::VariableArray<MeshResult> Visible;
        Container::VariableArray<MeshResult> All;
        Container::UnorderedSet<uint64_t> Suppressed;
    };

    bool Fail(size_t count, const char* version, size_t chunkSize, const char* phase, const char* detail)
    {
        std::cout << "失敗: " << detail << "（メッシュ数=" << count << "、版=" << version
                  << "、チャンク=" << chunkSize << "、比較=" << phase << "）\n";
        return false;
    }

    ViewportSnapshot MakeSnapshot()
    {
        ViewportSnapshot snapshot;
        snapshot.ViewId = 1;
        snapshot.ViewportId = 2;
        snapshot.RenderWidth = 1280;
        snapshot.RenderHeight = 720;
        snapshot.PixelRect.Width = 1280.0f;
        snapshot.PixelRect.Height = 720.0f;
        snapshot.Camera.PositionZ = 5.0f;
        snapshot.Camera.ForwardZ = -1.0f;
        snapshot.Camera.UpY = 1.0f;
        snapshot.Camera.FieldOfView = 60.0f;
        snapshot.Camera.NearPlane = 0.1f;
        snapshot.Camera.FarPlane = 1000.0f;
        snapshot.Camera.Viewport.Width = 1280.0f;
        snapshot.Camera.Viewport.Height = 720.0f;
        snapshot.Camera.CullingMask = RenderLayer::Default;
        snapshot.bHasCamera = snapshot.Camera.IsValid();
        return snapshot;
    }

    void PopulateScene(SceneView& serial, SceneView& parallel, size_t count)
    {
        std::mt19937 random(0xC0111DEu);
        std::uniform_real_distribution<float> lateral(-45.0f, 45.0f);
        std::uniform_real_distribution<float> depth(-80.0f, 30.0f);

        for (size_t index = 0; index < count; ++index)
        {
            MeshProxy mesh{};
            mesh.ObjectId = static_cast<uint64_t>(index + 1);
            mesh.ComponentId = mesh.ObjectId;
            mesh.MeshHandle.Id = mesh.ObjectId;
            mesh.WorldBounds.CenterX = lateral(random);
            mesh.WorldBounds.CenterY = lateral(random);
            mesh.WorldBounds.CenterZ = depth(random);
            mesh.WorldBounds.Radius = 0.5f;
            mesh.SortDepth = -static_cast<float>(index % 23 + 1);

            if (index < 4)
            {
                mesh.WorldBounds.CenterX = static_cast<float>(index) * 0.5f - 0.75f;
                mesh.WorldBounds.CenterY = 0.0f;
                mesh.WorldBounds.CenterZ = 0.0f;
            }

            serial.AddMeshProxy(mesh);
            parallel.AddMeshProxy(mesh);

            if (index < 4 || index % 256 == 0)
            {
                const size_t kind = index < 4 ? index : (index / 256) % 4;
                BoardProxy board{};
                board.ObjectId = mesh.ObjectId;
                board.ComponentId = 1000000 + mesh.ComponentId;
                board.SourceMeshComponentId = mesh.ComponentId;
                board.Space = BoardSpace::WorldSpace;
                board.LayerMask = RenderLayer::Default;
                board.RenderSubtype = BoardRenderSubtype::Impostor;
                board.WorldBounds = mesh.WorldBounds;
                board.LODSwitchDistance = 3.0f;

                if (kind == 1)
                {
                    board.LODSwitchDistance = 200.0f;
                }
                else if (kind == 2)
                {
                    board.WorldBounds.CenterX = 30.0f;
                    board.WorldBounds.CenterY = 0.0f;
                    board.WorldBounds.CenterZ = 0.0f;
                }
                else if (kind == 3)
                {
                    board.RenderSubtype = BoardRenderSubtype::Standard;
                }

                serial.UpdateBoardProxy(board);
                parallel.UpdateBoardProxy(board);
            }
        }
    }

    bool CaptureResult(const SceneView& view, CullResult& result, size_t count,
                       const char* version, size_t chunkSize, const char* phase)
    {
        const auto& visible = view.GetVisibleMeshProxies();
        const auto& all = view.GetMeshProxies();
        const auto& suppressed = view.GetSuppressedMeshComponentIds();
        if (all.size() != count)
        {
            return Fail(count, version, chunkSize, phase, "登録されたメッシュの数が異なる");
        }

        result.Visible.reserve(visible.size());
        result.All.reserve(all.size());
        result.Suppressed.insert(suppressed.begin(), suppressed.end());
        for (const MeshProxy* mesh : visible)
        {
            if (!mesh)
            {
                return Fail(count, version, chunkSize, phase, "可視メッシュが空のポインタを含む");
            }
            if (suppressed.find(mesh->ComponentId) != suppressed.end())
            {
                return Fail(count, version, chunkSize, phase, "抑制されたメッシュが可視一覧にある");
            }
            result.Visible.push_back({mesh->ComponentId, std::bit_cast<uint32_t>(mesh->SortDepth)});
        }
        for (const MeshProxy& mesh : all)
        {
            result.All.push_back({mesh.ComponentId, std::bit_cast<uint32_t>(mesh.SortDepth)});
        }
        return true;
    }

    bool CompareResults(const CullResult& expected, const CullResult& actual, size_t count,
                        const char* version, size_t chunkSize, const char* phase)
    {
        if (expected.Visible.size() != actual.Visible.size())
        {
            return Fail(count, version, chunkSize, phase, "可視メッシュ数が異なる");
        }
        for (size_t index = 0; index < expected.Visible.size(); ++index)
        {
            if (expected.Visible[index].ComponentId != actual.Visible[index].ComponentId)
            {
                return Fail(count, version, chunkSize, phase, "可視メッシュの要素または順序が異なる");
            }
            if (expected.Visible[index].SortDepthBits != actual.Visible[index].SortDepthBits)
            {
                return Fail(count, version, chunkSize, phase, "可視メッシュの SortDepth が異なる");
            }
        }
        if (expected.All.size() != actual.All.size())
        {
            return Fail(count, version, chunkSize, phase, "全メッシュ数が異なる");
        }
        for (size_t index = 0; index < expected.All.size(); ++index)
        {
            if (expected.All[index].ComponentId != actual.All[index].ComponentId ||
                expected.All[index].SortDepthBits != actual.All[index].SortDepthBits)
            {
                return Fail(count, version, chunkSize, phase, "全メッシュの順序または SortDepth が異なる");
            }
        }
        if (expected.Suppressed.size() != actual.Suppressed.size())
        {
            return Fail(count, version, chunkSize, phase, "抑制されたメッシュの数が異なる");
        }
        for (uint64_t id : expected.Suppressed)
        {
            if (actual.Suppressed.find(id) == actual.Suppressed.end())
            {
                return Fail(count, version, chunkSize, phase, "抑制されたメッシュの集合が異なる");
            }
        }
        return true;
    }

    bool HasVisible(const CullResult& result, uint64_t id)
    {
        for (const MeshResult& mesh : result.Visible)
        {
            if (mesh.ComponentId == id)
            {
                return true;
            }
        }
        return false;
    }

    bool CheckScene(const CullResult& result, size_t count, const char* version, size_t chunkSize)
    {
        if (count > 0 && result.Suppressed.find(1) == result.Suppressed.end())
        {
            return Fail(count, version, chunkSize, "場面", "可視の Impostor がメッシュを抑制していない");
        }
        for (uint64_t id = 2; id <= 4 && id <= count; ++id)
        {
            if (result.Suppressed.find(id) != result.Suppressed.end() || !HasVisible(result, id))
            {
                return Fail(count, version, chunkSize, "場面", "不可視または通常の板がメッシュを抑制した");
            }
        }
        if (count == 100000 &&
            (result.Visible.empty() || result.Visible.size() == count || result.Suppressed.empty()))
        {
            return Fail(count, version, chunkSize, "場面", "可視数または抑制集合が退化している");
        }
        return true;
    }

    void Cull(SceneView& view, Viewport& viewport, const ViewportSnapshot& snapshot, bool bSnapshot)
    {
        if (bSnapshot)
        {
            view.CullProxies(snapshot);
        }
        else
        {
            view.CullProxies(&viewport);
        }
    }

    bool RunVariant(SceneView& serial, SceneView& parallel, Viewport& viewport,
                    const ViewportSnapshot& snapshot, size_t count, size_t chunkSize, bool bSnapshot)
    {
        const char* version = bSnapshot ? "ViewportSnapshot" : "Viewport";
        Cull(serial, viewport, snapshot, bSnapshot);
        Cull(parallel, viewport, snapshot, bSnapshot);

        CullResult serialResult;
        CullResult firstResult;
        if (!CaptureResult(serial, serialResult, count, version, chunkSize, "直列") ||
            !CaptureResult(parallel, firstResult, count, version, chunkSize, "並列1回目") ||
            !CompareResults(serialResult, firstResult, count, version, chunkSize, "直列と並列") ||
            !CheckScene(serialResult, count, version, chunkSize))
        {
            return false;
        }

        Cull(parallel, viewport, snapshot, bSnapshot);
        CullResult secondResult;
        if (!CaptureResult(parallel, secondResult, count, version, chunkSize, "並列2回目") ||
            !CompareResults(firstResult, secondResult, count, version, chunkSize, "並列の繰り返し"))
        {
            return false;
        }

        std::cout << "メッシュ数=" << count << "、版=" << version << "、チャンク=" << chunkSize
                  << "、可視=" << firstResult.Visible.size() << "、抑制=" << firstResult.Suppressed.size() << "\n";
        return true;
    }

    bool RunCase(size_t count, Viewport& viewport, const ViewportSnapshot& snapshot)
    {
        SceneView serial;
        SceneView parallel;
        SceneViewSettings settings;
        settings.MaxDrawDistance = 50.0f;
        if (!serial.Initialize(settings))
        {
            return Fail(count, "共通", 64, "準備", "直列の SceneView を初期化できない");
        }
        if (!parallel.Initialize(settings))
        {
            serial.Shutdown();
            return Fail(count, "共通", 64, "準備", "並列の SceneView を初期化できない");
        }

        PopulateScene(serial, parallel, count);
        bool bPassed = true;
        const size_t chunkSizes[] = {64, 1024};
        for (size_t chunkSize : chunkSizes)
        {
            if (chunkSize == 1024 && count != 100000)
            {
                continue;
            }
            parallel.SetCullParallelSettings({.MinProxyCount = 0, .ChunkSize = chunkSize});
            if (!RunVariant(serial, parallel, viewport, snapshot, count, chunkSize, false) ||
                !RunVariant(serial, parallel, viewport, snapshot, count, chunkSize, true))
            {
                bPassed = false;
                break;
            }
        }
        parallel.Shutdown();
        serial.Shutdown();
        return bPassed;
    }

    bool RunTests()
    {
        if (NorvesLib::Thread::JobSystem::Get().GetWorkerThreadCount() != 4)
        {
            return Fail(0, "共通", 64, "準備", "ワーカースレッドを4本初期化できない");
        }

        const ViewportSnapshot snapshot = MakeSnapshot();
        if (!snapshot.bHasCamera)
        {
            return Fail(0, "共通", 64, "準備", "カメラが無効");
        }
        Viewport viewport;
        ViewportSettings viewportSettings;
        viewportSettings.Width = 1.0f;
        viewportSettings.Height = 0.5625f;
        if (!viewport.Initialize(viewportSettings))
        {
            return Fail(0, "共通", 64, "準備", "Viewport を初期化できない");
        }
        viewport.SetCamera(snapshot.Camera);

        bool bPassed = true;
        const size_t counts[] = {0, 1, 63, 64, 65, 129, 100000};
        for (size_t count : counts)
        {
            if (!RunCase(count, viewport, snapshot))
            {
                bPassed = false;
                break;
            }
        }
        viewport.Shutdown();
        return bPassed;
    }
} // namespace

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(4);
    const bool bPassed = RunTests();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    if (!bPassed)
    {
        return 1;
    }
    std::cout << "CullProxiesParallelEquivalenceTest 合格\n";
    return 0;
}
