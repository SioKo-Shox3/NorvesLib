#include "Object/ObjectHeap.h"
#include "Object/ObjectReference.h"
#include "Object/Reflection.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    class IncrementalNode : public Object
    {
        REFLECTION_CLASS(IncrementalNode, Object)

    public:
        IncrementalNode() = default;

        void AddReferencedObjects(ReferenceCollector &collector) const override
        {
            Next.AddReferencedObjects(collector);
        }

        ObjectRef<IncrementalNode> Next;
    };

    IMPLEMENT_CLASS(IncrementalNode, Object)
}

int main()
{
    std::cout << "IncrementalGCTest start\n";

    ObjectHeap heap;

    ObjectHandle rootHandle = heap.Create<IncrementalNode>();
    ObjectHandle childHandle = heap.Create<IncrementalNode>();
    ObjectHandle garbageA = heap.Create<IncrementalNode>();
    ObjectHandle garbageB = heap.Create<IncrementalNode>();

    IncrementalNode *root = heap.Resolve<IncrementalNode>(rootHandle);
    assert(root != nullptr);
    root->Next = ObjectRef<IncrementalNode>(&heap, childHandle);
    assert(heap.AddRoot(rootHandle));

    ObjectHeap::GCBudget budget;
    budget.MaxObjectsPerFrame = 1;
    budget.MaxTimeMs = 100.0;

    size_t steps = 0;
    ObjectHeap::GCStats stats;
    do
    {
        stats = heap.CollectIncremental(budget);
        ++steps;
        assert(steps < 32);
    } while (heap.IsIncrementalGCActive());

    assert(steps > 1);
    assert(stats.MarkedObjects == 2);
    assert(stats.SweptObjects == 2);
    assert(heap.Resolve<IncrementalNode>(rootHandle) != nullptr);
    assert(heap.Resolve<IncrementalNode>(childHandle) != nullptr);
    assert(heap.Resolve<IncrementalNode>(garbageA) == nullptr);
    assert(heap.Resolve<IncrementalNode>(garbageB) == nullptr);
    assert(!heap.DumpGCState().empty());

    assert(heap.RemoveRoot(rootHandle));
    stats = {};
    steps = 0;
    do
    {
        stats = heap.CollectIncremental(budget);
        ++steps;
        assert(steps < 32);
    } while (heap.IsIncrementalGCActive());

    assert(stats.SweptObjects == 2);
    assert(heap.GetLiveObjectCount() == 0);

    std::cout << "IncrementalGCTest passed\n";
    return 0;
}
