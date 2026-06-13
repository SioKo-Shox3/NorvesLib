#include "Rendering/RenderGraph/LegacyViewPassAdapter.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/ViewRenderContext.h"
#include "Container/PointerTypes.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    const char* PhaseInitialize = "Initialize";
    const char* PhaseSetup = "Setup";
    const char* PhaseExecute = "Execute";

    struct PassEvent
    {
        const char* Name = nullptr;
        const char* Phase = nullptr;
    };

    class FakeViewPass final : public IViewPass
    {
    public:
        FakeViewPass(const char* name, Container::VariableArray<PassEvent>* events)
            : m_Name(name), m_Events(events)
        {
        }

        const char* GetName() const override
        {
            return m_Name;
        }

        bool Initialize(ViewRenderContext& context) override
        {
            (void)context;
            Record(PhaseInitialize);
            m_bInitialized = true;
            return true;
        }

        void Shutdown() override
        {
            m_bInitialized = false;
        }

        void Setup(ViewRenderContext& context) override
        {
            (void)context;
            assert(m_bInitialized);
            Record(PhaseSetup);
        }

        void Execute(ViewRenderContext& context) override
        {
            (void)context;
            assert(m_bInitialized);
            Record(PhaseExecute);
        }

    private:
        void Record(const char* phase)
        {
            if (!m_Events)
            {
                return;
            }

            PassEvent event;
            event.Name = m_Name;
            event.Phase = phase;
            m_Events->push_back(event);
        }

        const char* m_Name = nullptr;
        Container::VariableArray<PassEvent>* m_Events = nullptr;
    };

    void AssertEvent(const Container::VariableArray<PassEvent>& events,
                     uint32_t index,
                     const char* name,
                     const char* phase)
    {
        assert(index < events.size());
        assert(std::strcmp(events[index].Name, name) == 0);
        assert(std::strcmp(events[index].Phase, phase) == 0);
    }

    void TestLazyInitSetupExecute()
    {
        Container::VariableArray<PassEvent> events;
        FakeViewPass pass("A", &events);
        LegacyViewPassAdapter adapter(&pass);

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&adapter);

        assert(graph.Compile());
        assert(graph.GetCompiledPassOrder().size() == 1);
        assert(graph.GetCompiledPassOrder()[0] == 0);
        assert(graph.GetCompiledBarriers().empty());

        ViewRenderContext context;
        assert(graph.Execute(context));
        assert(pass.IsInitialized());
        assert(events.size() == 3);
        AssertEvent(events, 0, "A", PhaseInitialize);
        AssertEvent(events, 1, "A", PhaseSetup);
        AssertEvent(events, 2, "A", PhaseExecute);
    }

    void TestInsertionOrderAndLogicalOnlyDeclarations()
    {
        Container::VariableArray<PassEvent> events;
        FakeViewPass passA("A", &events);
        FakeViewPass passB("B", &events);
        FakeViewPass passC("C", &events);
        LegacyViewPassAdapter adapterA(&passA);
        LegacyViewPassAdapter adapterB(&passB);
        LegacyViewPassAdapter adapterC(&passC);

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&adapterA);
        graph.AddPass(&adapterB);
        graph.AddPass(&adapterC);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == 0);
        assert(order[1] == 1);
        assert(order[2] == 2);
        assert(graph.GetCompiledBarriers().empty());

        ViewRenderContext context;
        assert(graph.Execute(context));
        assert(events.size() == 9);
        AssertEvent(events, 0, "A", PhaseInitialize);
        AssertEvent(events, 1, "A", PhaseSetup);
        AssertEvent(events, 2, "A", PhaseExecute);
        AssertEvent(events, 3, "B", PhaseInitialize);
        AssertEvent(events, 4, "B", PhaseSetup);
        AssertEvent(events, 5, "B", PhaseExecute);
        AssertEvent(events, 6, "C", PhaseInitialize);
        AssertEvent(events, 7, "C", PhaseSetup);
        AssertEvent(events, 8, "C", PhaseExecute);
    }

    void TestPostProcessStackExecutesAsOneGraphBlock()
    {
        Container::VariableArray<PassEvent> events;
        FakeViewPass before("Before", &events);
        FakeViewPass after("After", &events);
        auto postProcessStack = Container::MakeUnique<PostProcessStack>();
        postProcessStack->AddPass(Container::MakeUnique<FakeViewPass>("StackA", &events));
        postProcessStack->AddPass(Container::MakeUnique<FakeViewPass>("StackB", &events));

        LegacyViewPassAdapter beforeAdapter(&before);
        LegacyViewPassAdapter stackAdapter(postProcessStack.get());
        LegacyViewPassAdapter afterAdapter(&after);

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&beforeAdapter);
        graph.AddPass(&stackAdapter);
        graph.AddPass(&afterAdapter);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == 0);
        assert(order[1] == 1);
        assert(order[2] == 2);
        assert(graph.GetCompiledBarriers().empty());

        ViewRenderContext context;
        assert(graph.Execute(context));
        assert(events.size() == 12);
        AssertEvent(events, 0, "Before", PhaseInitialize);
        AssertEvent(events, 1, "Before", PhaseSetup);
        AssertEvent(events, 2, "Before", PhaseExecute);
        AssertEvent(events, 3, "StackA", PhaseInitialize);
        AssertEvent(events, 4, "StackB", PhaseInitialize);
        AssertEvent(events, 5, "StackA", PhaseSetup);
        AssertEvent(events, 6, "StackB", PhaseSetup);
        AssertEvent(events, 7, "StackA", PhaseExecute);
        AssertEvent(events, 8, "StackB", PhaseExecute);
        AssertEvent(events, 9, "After", PhaseInitialize);
        AssertEvent(events, 10, "After", PhaseSetup);
        AssertEvent(events, 11, "After", PhaseExecute);
    }
} // namespace

int main()
{
    std::cout << "LegacyViewPassAdapterTest start" << std::endl;

    TestLazyInitSetupExecute();
    std::cout << "  lazy init/setup/execute passed" << std::endl;
    TestInsertionOrderAndLogicalOnlyDeclarations();
    std::cout << "  insertion order/logical declarations passed" << std::endl;
    TestPostProcessStackExecutesAsOneGraphBlock();
    std::cout << "  post process stack block passed" << std::endl;

    std::cout << "LegacyViewPassAdapterTest passed" << std::endl;
    return 0;
}

