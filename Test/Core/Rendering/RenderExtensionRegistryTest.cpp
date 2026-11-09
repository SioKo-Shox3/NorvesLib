#include "Rendering/RenderExtension/RenderExtension.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/ViewRenderContext.h"
#include "Thread/Atomic.h"
#include "Thread/Thread.h"
#include <cstring>
#include <iostream>
#include <thread>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Thread = NorvesLib::Thread;

namespace
{
    int g_Failures = 0;
    const char* g_TestName = nullptr;

    bool Expect(bool bCondition, const char* reason)
    {
        if (!bCondition)
        {
            std::cerr << g_TestName << ": " << reason << '\n';
            ++g_Failures;
        }
        return bCondition;
    }

    struct FakePassState
    {
        const char* Name;
        bool bInitialize = true;
        uint32_t InitializeCount = 0;
        uint32_t ExecuteCount = 0;
    };

    class NativeFakeViewPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit NativeFakeViewPass(FakePassState* state) : m_State(state)
        {
        }

        const char* GetName() const override
        {
            return m_State->Name;
        }

        bool Initialize(ViewRenderContext&) override
        {
            ++m_State->InitializeCount;
            m_bInitialized = m_State->bInitialize;
            return m_bInitialized;
        }

        void Shutdown() override
        {
            m_bInitialized = false;
        }

        void Setup(ViewRenderContext&) override
        {
        }

        void Execute(ViewRenderContext&) override
        {
            ++m_State->ExecuteCount;
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources&, ViewRenderContext& context) override
        {
            Execute(context);
        }

    private:
        FakePassState* m_State;
    };

    Container::TUniquePtr<IViewPass> CreateFakePass(void* context)
    {
        return Container::MakeUnique<NativeFakeViewPass>(static_cast<FakePassState*>(context));
    }

    bool RegisterFake(RenderExtensionRegistry& registry, FakePassState& state,
                      RenderExtensionStage stage, int32_t order = 0)
    {
        RenderExtensionDesc desc;
        desc.Name = state.Name;
        desc.Stage = stage;
        desc.Order = order;
        return registry.Register(desc, {CreateFakePass, &state});
    }

    const RenderExtensionStage Stages[] = {
        RenderExtensionStage::GBuffer, RenderExtensionStage::AfterLighting,
        RenderExtensionStage::Transparent, RenderExtensionStage::PostTemporalAA,
        RenderExtensionStage::PostTonemap};
    const char* Anchors[] = {
        "FoliagePass", "LightingPass", "ForwardPass", "TemporalAAPass", "VignettePass"};

    struct StagePasses
    {
        FakePassState States[5] = {
            {"Ext.Test.GBuffer"}, {"Ext.Test.AfterLighting"}, {"Ext.Test.Transparent"},
            {"Ext.Test.PostTemporalAA"}, {"Ext.Test.PostTonemap"}};

        void RegisterAll(SceneView& view)
        {
            for (uint32_t index = 0; index < 5; ++index)
            {
                Expect(RegisterFake(view.GetRenderExtensionRegistry(), States[index], Stages[index]),
                       "各段の拡張を登録できない");
            }
            view.GetRenderExtensionRegistry().ApplyPending();
        }
    };

    using PassNames = Container::VariableArray<Container::String>;
    using PassPointers = Container::VariableArray<IViewPass*>;

    PassPointers CapturePointers(const View& view, bool bPost)
    {
        PassPointers result;
        if (bPost)
        {
            if (const auto* stack = view.GetPostProcessStack())
            {
                for (const auto& pass : stack->GetPasses())
                {
                    result.push_back(pass.get());
                }
            }
        }
        else
        {
            for (uint32_t index = 0; index < view.GetPassCount(); ++index)
            {
                result.push_back(view.GetPassAt(index));
            }
        }
        return result;
    }

    PassNames CaptureNames(const View& view, bool bPost)
    {
        PassNames names;
        for (const auto* pass : CapturePointers(view, bPost))
        {
            names.push_back(pass ? pass->GetName() : "<null>");
        }
        return names;
    }

    template <typename T>
    bool EqualSequence(const Container::VariableArray<T>& left, const Container::VariableArray<T>& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (size_t index = 0; index < left.size(); ++index)
        {
            if (left[index] != right[index])
            {
                return false;
            }
        }
        return true;
    }

    int FindName(const PassNames& names, const char* name)
    {
        for (size_t index = 0; index < names.size(); ++index)
        {
            if (names[index] == name)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    uint32_t CountName(const PassNames& names, const char* name)
    {
        uint32_t count = 0;
        for (const auto& candidate : names)
        {
            if (candidate == name)
            {
                ++count;
            }
        }
        return count;
    }

    void CheckStagePositions(const SceneView& view, const StagePasses& extensions)
    {
        const auto mainNames = CaptureNames(view, false);
        const auto postNames = CaptureNames(view, true);
        for (uint32_t index = 0; index < 5; ++index)
        {
            const auto& names = index < 3 ? mainNames : postNames;
            const char* name = extensions.States[index].Name;
            const int anchor = FindName(names, Anchors[index]);
            Expect(anchor >= 0 && FindName(names, name) == anchor + 1,
                   "拡張が指定した錨の直後にない");
            Expect(CountName(mainNames, name) + CountName(postNames, name) == 1,
                   "拡張の出現回数が1回ではない");
        }
        const int gbuffer = FindName(mainNames, extensions.States[0].Name);
        Expect(gbuffer >= 0 && FindName(mainNames, "SSAOPass") > gbuffer,
               "GBuffer段の拡張がSSAOより前にない");
        const int transparent = FindName(mainNames, extensions.States[2].Name);
        Expect(transparent >= 0 && FindName(mainNames, "DistortionPass") == transparent + 1,
               "半透明段の拡張がDistortionPassの直前にない");
        Expect(mainNames.size() >= 2 && mainNames[mainNames.size() - 2] == "DepthOfFieldPass" && mainNames.back() == "MotionBlurPass", "末尾の被写界深度と動きぼけが失われたか順序が異なる");
    }

    void TestStageOrdering()
    {
        for (const auto mode : {VisibilityBufferMode::On, VisibilityBufferMode::Off})
        {
            StagePasses extensions;
            SceneRenderer renderer;
            SceneView view;
            view.SetupDeferredPipeline(&renderer, RasterDirectBrdf::Analytic, mode);
            extensions.RegisterAll(view);
            CheckStagePositions(view, extensions);
        }
    }

    void TestOrderAndRegistrationSequence()
    {
        FakePassState first{"Ext.Test.First"};
        FakePassState second{"Ext.Test.Second"};
        FakePassState earlier{"Ext.Test.Earlier"};
        SceneRenderer renderer;
        SceneView view;
        view.SetupDeferredPipeline(&renderer);
        auto& registry = view.GetRenderExtensionRegistry();
        registry.ApplyPending();
        Expect(RegisterFake(registry, first, RenderExtensionStage::AfterLighting, 10), "1件目を登録できない");
        Expect(RegisterFake(registry, second, RenderExtensionStage::AfterLighting, 10), "2件目を登録できない");
        Expect(RegisterFake(registry, earlier, RenderExtensionStage::AfterLighting, -10), "小さいOrderを登録できない");
        registry.ApplyPending();
        const auto names = CaptureNames(view, false);
        const int anchor = FindName(names, "LightingPass");
        Expect(anchor >= 0 && FindName(names, earlier.Name) == anchor + 1 &&
                   FindName(names, first.Name) == anchor + 2 && FindName(names, second.Name) == anchor + 3,
               "Orderの昇順または同Orderの登録順が守られない");
    }

    void TestDuplicateNameRejected()
    {
        FakePassState state{"Ext.Test.Duplicate"};
        SceneRenderer renderer;
        SceneView view;
        view.SetupDeferredPipeline(&renderer);
        auto& registry = view.GetRenderExtensionRegistry();
        registry.ApplyPending();
        Expect(RegisterFake(registry, state, RenderExtensionStage::AfterLighting), "初回登録が拒否された");
        Expect(!RegisterFake(registry, state, RenderExtensionStage::AfterLighting), "同名の2回目が拒否されない");
        registry.ApplyPending();
        Expect(CountName(CaptureNames(view, false), state.Name) + CountName(CaptureNames(view, true), state.Name) == 1,
               "同名拒否後のパスが1件ではない");
    }

    void TestUnregisterRestoresOrder()
    {
        StagePasses extensions;
        SceneRenderer renderer;
        SceneView view;
        view.SetupDeferredPipeline(&renderer);
        auto& registry = view.GetRenderExtensionRegistry();
        registry.ApplyPending();
        const auto mainBefore = CaptureNames(view, false);
        const auto postBefore = CaptureNames(view, true);
        extensions.RegisterAll(view);
        CheckStagePositions(view, extensions);
        for (auto& state : extensions.States)
        {
            Expect(registry.Unregister(state.Name), "登録済み拡張を解除できない");
        }
        registry.ApplyPending();
        Expect(EqualSequence(mainBefore, CaptureNames(view, false)), "解除後のメインパスの並びが元に戻らない");
        Expect(EqualSequence(postBefore, CaptureNames(view, true)), "解除後の後段パスの並びが元に戻らない");
    }

    void TestRebuildDoesNotDuplicate()
    {
        StagePasses extensions;
        SceneRenderer renderer;
        SceneView view;
        view.SetupDeferredPipeline(&renderer);
        extensions.RegisterAll(view);
        const auto mainBefore = CaptureNames(view, false);
        const auto postBefore = CaptureNames(view, true);
        view.SetupDeferredPipeline(&renderer);
        CheckStagePositions(view, extensions);
        Expect(EqualSequence(mainBefore, CaptureNames(view, false)), "組み直し後のメインパスの位置が変わった");
        Expect(EqualSequence(postBefore, CaptureNames(view, true)), "組み直し後の後段パスの位置が変わった");
    }

    void CheckUniqueNames(const SceneView& view)
    {
        auto names = CaptureNames(view, false);
        for (const auto& name : CaptureNames(view, true))
        {
            names.push_back(name);
        }
        for (const auto& name : names)
        {
            Expect(CountName(names, name.c_str()) == 1, "最後の反映後にパス名が重複している");
        }
    }

    void TestCrossThreadChangesAreDeferred()
    {
        FakePassState mainStates[2] = {{"Ext.Test.ThreadMainA"}, {"Ext.Test.ThreadMainB"}};
        FakePassState postStates[2] = {{"Ext.Test.ThreadPostA"}, {"Ext.Test.ThreadPostB"}};
        SceneRenderer renderer;
        SceneView view;
        view.SetupDeferredPipeline(&renderer);
        auto& registry = view.GetRenderExtensionRegistry();
        registry.ApplyPending();

        constexpr uint32_t RoundCount = 64;
        Thread::Atomic<uint32_t> requestedRound{0};
        Thread::Atomic<uint32_t> completedRound{0};
        bool bWorkerSucceeded = true;
        // 控えを取った後に必ず操作し、各反映後には次の組へ名前を交替する。
        Thread::Thread worker([&]()
        {
            for (uint32_t round = 1; round <= RoundCount; ++round)
            {
                while (requestedRound.Load() < round)
                {
                    std::this_thread::yield();
                }
                if (round > 1)
                {
                    const uint32_t previous = (round - 2) % 2;
                    bWorkerSucceeded &= registry.Unregister(mainStates[previous].Name);
                    bWorkerSucceeded &= registry.Unregister(postStates[previous].Name);
                }
                const uint32_t current = (round - 1) % 2;
                bWorkerSucceeded &= RegisterFake(registry, mainStates[current], RenderExtensionStage::GBuffer);
                bWorkerSucceeded &= RegisterFake(registry, postStates[current], RenderExtensionStage::PostTonemap);
                completedRound.Store(round);
            }
        });

        for (uint32_t round = 1; round <= RoundCount; ++round)
        {
            const auto mainBefore = CapturePointers(view, false);
            const auto postBefore = CapturePointers(view, true);
            requestedRound.Store(round);
            do
            {
                std::this_thread::yield();
            } while (completedRound.Load() < round);
            Expect(EqualSequence(mainBefore, CapturePointers(view, false)),
                   "ApplyPending前にメインパスの件数またはポインタが変わった");
            Expect(EqualSequence(postBefore, CapturePointers(view, true)),
                   "ApplyPending前に後段パスの件数またはポインタが変わった");
            registry.ApplyPending();
        }
        worker.Join();
        Expect(bWorkerSucceeded, "別スレッドの登録または解除が失敗した");
        registry.ApplyPending();
        const uint32_t finalIndex = (RoundCount - 1) % 2;
        Expect(CountName(CaptureNames(view, false), mainStates[finalIndex].Name) == 1 &&
                   CountName(CaptureNames(view, true), postStates[finalIndex].Name) == 1,
               "最後の登録が反映されていない");
        CheckUniqueNames(view);
    }

    struct ExecutionCounts
    {
        uint32_t Anchor = 0;
        uint32_t Following = 0;
        uint32_t FailedExtension = 0;
        uint32_t FailedInitialize = 0;
    };

    ExecutionCounts RenderTwice(bool bFailingExtension)
    {
        FakePassState anchor{"LightingPass"};
        FakePassState following{"Ext.Test.Following"};
        FakePassState failing{"Ext.Test.Failing", false};
        View view;
        if (!Expect(view.Initialize(ViewSettings{}), "デバイスなしのViewを初期化できない"))
        {
            return {};
        }
        view.AddPass(Container::MakeUnique<NativeFakeViewPass>(&anchor));
        view.AddPass(Container::MakeUnique<NativeFakeViewPass>(&following));
        RenderExtensionRegistry registry(&view);
        if (bFailingExtension)
        {
            Expect(RegisterFake(registry, failing, RenderExtensionStage::AfterLighting), "失敗する拡張を登録できない");
            registry.ApplyPending();
        }
        RenderGraph graph;
        if (!Expect(graph.Initialize(nullptr), "デバイスなしのRenderGraphを初期化できない"))
        {
            return {};
        }
        ViewRenderContext context;
        context.Graph = &graph;
        view.Render(context);
        view.Render(context);
        view.Shutdown();
        graph.Shutdown();
        return {anchor.ExecuteCount, following.ExecuteCount, failing.ExecuteCount, failing.InitializeCount};
    }

    void TestInitializeFailurePreservesOtherPasses()
    {
        const auto baseline = RenderTwice(false);
        const auto failing = RenderTwice(true);
        Expect(baseline.Anchor > 0 && baseline.Following > 0, "比較元の偽パスが実行されていない");
        Expect(failing.Anchor == baseline.Anchor && failing.Following == baseline.Following,
               "拡張の初期化失敗で他のパスの実行回数が変わった");
        Expect(failing.FailedInitialize > 0, "拡張のInitialize失敗を通っていない");
        Expect(failing.FailedExtension == 0, "初期化に失敗した拡張の中身が実行された");
    }

    void RunTest(const char* name, void (*test)())
    {
        g_TestName = name;
        const int before = g_Failures;
        test();
        std::cout << name << (g_Failures == before ? ": 成功\n" : ": 失敗\n");
    }
} // namespace

int main()
{
    RunTest("TestStageOrdering", TestStageOrdering);
    RunTest("TestOrderAndRegistrationSequence", TestOrderAndRegistrationSequence);
    RunTest("TestDuplicateNameRejected", TestDuplicateNameRejected);
    RunTest("TestUnregisterRestoresOrder", TestUnregisterRestoresOrder);
    RunTest("TestRebuildDoesNotDuplicate", TestRebuildDoesNotDuplicate);
    RunTest("TestCrossThreadChangesAreDeferred", TestCrossThreadChangesAreDeferred);
    RunTest("TestInitializeFailurePreservesOtherPasses", TestInitializeFailurePreservesOtherPasses);
    std::cout << "RenderExtensionRegistryTest: 失敗数=" << g_Failures << '\n';
    return g_Failures == 0 ? 0 : 1;
}
