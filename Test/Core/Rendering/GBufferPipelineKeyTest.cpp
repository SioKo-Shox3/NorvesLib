#include "Rendering/GBufferProgramRegistry.h"
#include "Rendering/RenderMaterialStore.h"
#include "Thread/Atomic.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace RHI = NorvesLib::RHI;

namespace
{
    void MaterialKeys()
    {
        NorvesLib::Thread::Atomic<uint64_t> nextHandleId(1);
        RenderMaterialStore store(nextHandleId);
        MaterialCreateData create;
        const auto emptyHandle = store.CreateMaterial(create);
        create.Program = "Terrain";
        create.bTwoSided = true;
        create.Blend = BlendMode::Masked;
        const auto firstHandle = store.CreateMaterial(create);
        const auto sameHandle = store.CreateMaterial(create);
        create.Program = "Water";
        const auto otherHandle = store.CreateMaterial(create);

        const auto* empty = store.GetMaterialData(emptyHandle);
        const auto* first = store.GetMaterialData(firstHandle);
        const auto* same = store.GetMaterialData(sameHandle);
        const auto* other = store.GetMaterialData(otherHandle);
        assert(empty && first && same && other);
        assert(MakeGBufferProgramId(nullptr) == kStandardGBufferProgramId);
        assert(MakeGBufferProgramId("") == kStandardGBufferProgramId);
        assert(first->Program == "Terrain");
        assert(first->ProgramId == MakeGBufferProgramId("Terrain"));
        assert(first->ProgramId != kStandardGBufferProgramId);

        const auto nullKey = MakeGBufferPipelineKey(nullptr, false, false);
        const auto emptyKey = MakeGBufferPipelineKey(empty, false, false);
        const auto firstKey = MakeGBufferPipelineKey(first, false, false);
        const auto sameKey = MakeGBufferPipelineKey(same, false, false);
        const auto otherKey = MakeGBufferPipelineKey(other, false, false);
        const auto skinnedKey = MakeGBufferPipelineKey(first, true, false);
        const auto wireframeKey = MakeGBufferPipelineKey(first, false, true);
        assert(nullKey.Program == kStandardGBufferProgramId);
        assert(emptyKey.Program == kStandardGBufferProgramId);
        assert(nullKey == emptyKey);
        assert(firstKey == sameKey);
        assert(firstKey != otherKey);
        assert(firstKey != skinnedKey && skinnedKey.bSkinned);
        assert(firstKey != wireframeKey && wireframeKey.Polygon == RHI::PolygonMode::Line);
        assert(firstKey.Polygon == RHI::PolygonMode::Fill);
        assert(firstKey.Cull == RHI::CullMode::Back && !firstKey.bAlphaClip);

        create.Program = "";
        const bool bUpdated = store.UpdateMaterial(firstHandle, create);
        assert(bUpdated);
        first = store.GetMaterialData(firstHandle);
        assert(first && first->Program.empty());
        assert(first->ProgramId == kStandardGBufferProgramId);
    }

    void PipelineTable()
    {
        using TestPipeline = Container::TSharedPtr<int>;
        TGBufferPipelineTable<TestPipeline> table;
        uint32_t calls = 0;
        bool bFailWireframe = true;
        auto creator = [&](const GBufferPipelineKey& key) -> TestPipeline
        {
            ++calls;
            if (bFailWireframe && key.Polygon == RHI::PolygonMode::Line)
            {
                return {};
            }
            return Container::MakeShared<int>(static_cast<int>(calls));
        };

        const auto key = MakeGBufferPipelineKey(nullptr, false, false);
        const auto otherKey = MakeGBufferPipelineKey(nullptr, true, false);
        const auto failedKey = MakeGBufferPipelineKey(nullptr, false, true);
        const auto first = table.GetOrCreate(key, creator);
        const auto repeated = table.GetOrCreate(key, creator);
        assert(first && first == repeated && calls == 1 && table.GetCreatedCount() == 1);
        const auto other = table.GetOrCreate(otherKey, creator);
        assert(other && other != first && calls == 2 && table.GetCreatedCount() == 2);
        const auto failed = table.GetOrCreate(failedKey, creator);
        bFailWireframe = false;
        const auto repeatedFailure = table.GetOrCreate(failedKey, creator);
        assert(!failed && !repeatedFailure && calls == 3 && table.GetCreatedCount() == 2);
        table.Clear();
        assert(table.GetCreatedCount() == 0);
        const auto retried = table.GetOrCreate(failedKey, creator);
        assert(retried && calls == 4 && table.GetCreatedCount() == 1);
    }

    void ProgramRegistry()
    {
        GBufferProgramRegistry registry;
        GBufferProgramDesc initial;
        initial.Name = "Initial";
        initial.FragmentShader = "initial.frag";
        const uint64_t initialRevision = registry.GetRevision();
        const bool bInitialRegistered = registry.Register(initial);
        assert(bInitialRegistered);
        assert(registry.Find(MakeGBufferProgramId("Initial")));
        assert(registry.GetRevision() > initialRevision);
        const bool bInitialRemoved = registry.Unregister("Initial");
        assert(bInitialRemoved);
        assert(!registry.Find(MakeGBufferProgramId("Initial")));

        registry.CloseInitializationWindow();
        GBufferProgramDesc program;
        program.Name = "Terrain";
        program.VertexShader = "terrain.vert";
        program.FragmentShader = "terrain.frag";
        program.SkinnedVertexShader = "skinned_terrain.vert";
        const auto id = MakeGBufferProgramId("Terrain");
        const uint64_t beforeRegister = registry.GetRevision();
        const bool bRegistered = registry.Register(program);
        const bool bDuplicatePending = registry.Register(program);
        GBufferProgramDesc missingFragment;
        missingFragment.Name = "MissingFragment";
        const bool bMissingFragment = registry.Register(missingFragment);
        GBufferProgramDesc missingName;
        missingName.FragmentShader = "required.frag";
        const bool bMissingName = registry.Register(missingName);
        assert(bRegistered && !bDuplicatePending && !bMissingFragment && !bMissingName);
        assert(!registry.Find(id) && registry.GetRevision() == beforeRegister);
        registry.ApplyPending();
        const auto* found = registry.Find(id);
        assert(found && found->Name == "Terrain" && found->VertexShader == "terrain.vert");
        assert(found->FragmentShader == "terrain.frag" && found->SkinnedVertexShader == "skinned_terrain.vert");
        const uint64_t afterRegister = registry.GetRevision();
        assert(afterRegister > beforeRegister);
        const bool bDuplicateApplied = registry.Register(program);
        assert(!bDuplicateApplied);
        const bool bRemoved = registry.Unregister("Terrain");
        assert(bRemoved && registry.Find(id) && registry.GetRevision() == afterRegister);
        registry.ApplyPending();
        assert(!registry.Find(id) && registry.GetRevision() > afterRegister);

        GBufferProgramRegistry autoClosed;
        autoClosed.ApplyPending();
        const bool bAutoClosedRegistered = autoClosed.Register(program);
        assert(bAutoClosedRegistered && !autoClosed.Find(id));
        autoClosed.ApplyPending();
        assert(autoClosed.Find(id));
    }
} // namespace

int main()
{
    MaterialKeys();
    PipelineTable();
    ProgramRegistry();
    std::cout << "GBufferPipelineKeyTest: 合格" << std::endl;
    return 0;
}
