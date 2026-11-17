#include "Component/MegaGeometryComponent.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MegaGeometryComponent, MeshComponent)

    MegaGeometryComponent::MegaGeometryComponent()
        : MeshComponent()
    {
    }

    MegaGeometryComponent::MegaGeometryComponent(const FieldInitializer *initializer)
        : MeshComponent(initializer)
    {
    }

    MegaGeometryComponent::MegaGeometryComponent(const IUnknown *sourceObject)
        : MeshComponent(sourceObject)
    {
    }

    MegaGeometryComponent::~MegaGeometryComponent()
    {
    }

    void MegaGeometryComponent::SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle)
    {
        StoreMegaMeshHandle(handle);
        MarkRenderStateDirty();
        m_ModelBinding.Reset();
        ModelAsset = ModelAssetRef{};
    }

    void MegaGeometryComponent::SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle,
                                                  Container::StringView sourcePath)
    {
        StoreMegaMeshHandle(handle);
        ModelAssetRef reference;
        (void)reference.TrySet(sourcePath);
        ModelAsset = reference;
        MarkRenderStateDirty();
        m_ModelBinding.MarkExternal(Asset::AssetKind::Model, GetModelAsset().GetLogicalPath());
    }

    void MegaGeometryComponent::SetModelAsset(const ModelAssetRef& reference)
    {
        ModelAsset = reference;
        OnPropertyApplied(*s_PropertyPtr_ModelAsset);
    }

    void MegaGeometryComponent::BeginPlay()
    {
        MeshComponent::BeginPlay();
        if (!GetModelAsset().IsEmpty() && !HasMegaMesh())
        {
            RequestModelBinding();
        }
    }

    void MegaGeometryComponent::Finalize()
    {
        StoreMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle::Invalid());
        m_ModelBinding.Reset();
        MeshComponent::Finalize();
    }

    void MegaGeometryComponent::OnPropertyApplied(const ClassProperty& property)
    {
        MeshComponent::OnPropertyApplied(property);
        if (&property != s_PropertyPtr_ModelAsset)
        {
            return;
        }

        if (!m_ModelBinding.Matches(Asset::AssetKind::Model, GetModelAsset().GetLogicalPath()))
        {
            StoreMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle::Invalid());
            MarkRenderStateDirty();
            m_ModelBinding.Reset();
        }
        if (!GetModelAsset().IsEmpty() && bBegunPlay)
        {
            RequestModelBinding();
        }
    }

    void MegaGeometryComponent::RequestModelBinding()
    {
        m_ModelBinding.RequestModel(GetModelAsset());
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (world)
        {
            const ComponentTickRegistration* registration = world->FindComponentTick(StaticClass());
            if (!registration || !registration->HasBody())
            {
                ComponentTickRegistration tick;
                tick.Name = "MegaGeometry.AssetBinding";
                tick.Run = [](Component& component, float)
                {
                    static_cast<MegaGeometryComponent&>(component).PollAssetBinding();
                };
                tick.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsMegaMesh>().Type);
                world->RegisterComponentTick(StaticClass(), tick);
            }
        }
        PollAssetBinding();
    }

    void MegaGeometryComponent::PollAssetBinding()
    {
        Rendering::ModelHandle model = Rendering::ModelHandle::Invalid();
        if (!m_ModelBinding.TakeReadyModel(model))
        {
            return;
        }

        const Rendering::MegaGeometry::MegaMeshHandle handle = m_ModelBinding.ResolveModelMegaMesh(model);
        StoreMegaMeshHandle(handle);
        MarkRenderStateDirty();
        if (!handle.IsValid())
        {
            const Container::StringView path = m_ModelBinding.GetLogicalPath();
            NORVES_LOG_WARNING("MegaGeometryComponent", "MODEL_MEGAMESH_INVALID path=%.*s",
                               static_cast<int>(path.size()), path.data());
        }
    }

    Rendering::MegaGeometry::MegaMeshHandle MegaGeometryComponent::LoadMegaMeshHandle() const
    {
        Rendering::MegaGeometry::MegaMeshHandle handle = m_MegaMeshHandleFallback;
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsMegaMesh>().Type,
                                   static_cast<uint32_t>(offsetof(Ecs::EcsMegaMesh, Handle)),
                                   static_cast<uint32_t>(sizeof(handle)), &handle);
        return handle;
    }

    void MegaGeometryComponent::StoreMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle)
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsMegaMesh>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsWriteColumnPart(row, type, static_cast<uint32_t>(offsetof(Ecs::EcsMegaMesh, Handle)),
                                   static_cast<uint32_t>(sizeof(handle)), &handle);
            return;
        }
        m_MegaMeshHandleFallback = handle;
    }

    void MegaGeometryComponent::OnEcsRowAttached()
    {
        MeshComponent::OnEcsRowAttached();
        StoreMegaMeshHandle(m_MegaMeshHandleFallback);
    }

    void MegaGeometryComponent::OnEcsRowDetaching()
    {
        MeshComponent::OnEcsRowDetaching();
        m_MegaMeshHandleFallback = LoadMegaMeshHandle();
    }

    bool MegaGeometryComponent::BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const
    {
        const Rendering::MegaGeometry::MegaMeshHandle megaMeshHandle = LoadMegaMeshHandle();
        if (!megaMeshHandle.IsValid())
        {
            return false;
        }

        if (!IsVisible())
        {
            return false;
        }

        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.MegaMeshHandle = megaMeshHandle;
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.WorldBounds = m_WorldBounds;
        outProxy.bVisible = getbVisible().Get();
        outProxy.bCastShadow = getbCastShadow().Get();
        outProxy.LayerMask = getRenderLayerProp().Get();
        return true;
    }

} // namespace NorvesLib::Core::Component
