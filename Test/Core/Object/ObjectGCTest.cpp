#include "Object/ObjectHeap.h"
#include "Object/ObjectReference.h"
#include "Object/Reflection.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    class GCNode : public Object
    {
        REFLECTION_CLASS(GCNode, Object)

    public:
        GCNode() = default;

        void AddReferencedObjects(ReferenceCollector &collector) const override
        {
            StrongChild.AddReferencedObjects(collector);
        }

        void OnDestroying() override
        {
            bDestroyed = true;
        }

        ObjectRef<GCNode> StrongChild;
        WeakObjectRef<GCNode> WeakChild;
        bool bDestroyed = false;
    };

    IMPLEMENT_CLASS(GCNode, Object)
}

int main()
{
    std::cout << "ObjectGCTest start\n";

    ObjectHeap heap;

    ObjectHandle rootHandle = heap.Create<GCNode>();
    ObjectHandle strongHandle = heap.Create<GCNode>();
    ObjectHandle weakHandle = heap.Create<GCNode>();
    auto *root = heap.Resolve<GCNode>(rootHandle);
    assert(root != nullptr);

    root->StrongChild = ObjectRef<GCNode>(&heap, strongHandle);
    root->WeakChild = WeakObjectRef<GCNode>(&heap, weakHandle);
    assert(heap.AddRoot(rootHandle));

    ObjectHeap::GCStats stats = heap.CollectGarbage();
    assert(stats.MarkedObjects == 2);
    assert(stats.SweptObjects == 1);
    assert(heap.Resolve<GCNode>(rootHandle) != nullptr);
    assert(heap.Resolve<GCNode>(strongHandle) != nullptr);
    assert(heap.Resolve<GCNode>(weakHandle) == nullptr);

    ObjectHandle parentHandle = heap.Create<GCNode>();
    ObjectHandle innerHandle = heap.Create<GCNode>();
    GCNode *parent = heap.Resolve<GCNode>(parentHandle);
    GCNode *inner = heap.Resolve<GCNode>(innerHandle);
    assert(parent != nullptr);
    assert(inner != nullptr);
    assert(parent->AddInner(inner));
    assert(heap.AddRoot(parentHandle));

    stats = heap.CollectGarbage();
    assert(stats.MarkedObjects == 4);
    assert(heap.Resolve<GCNode>(innerHandle) != nullptr);

    assert(heap.RemoveRoot(parentHandle));
    stats = heap.CollectGarbage();
    assert(stats.SweptObjects == 2);
    assert(heap.Resolve<GCNode>(parentHandle) == nullptr);
    assert(heap.Resolve<GCNode>(innerHandle) == nullptr);

    ObjectHandle pendingHandle = heap.Create<GCNode>();
    GCNode *pending = heap.Resolve<GCNode>(pendingHandle);
    assert(pending != nullptr);
    assert(heap.AddRoot(pendingHandle));
    pending->SetFlag(OF_PendingDestroy, true);

    stats = heap.CollectGarbage();
    assert(stats.SweptObjects == 1);
    assert(heap.Resolve<GCNode>(pendingHandle) == nullptr);

    assert(heap.RemoveRoot(rootHandle));
    stats = heap.CollectGarbage();
    assert(stats.SweptObjects == 2);
    assert(heap.GetLiveObjectCount() == 0);

    std::cout << "ObjectGCTest passed\n";
    return 0;
}
