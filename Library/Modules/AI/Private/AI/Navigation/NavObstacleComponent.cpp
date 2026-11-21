#include "AI/Navigation/NavObstacleComponent.h"

#include "AI/AIModule.h"
#include "Module/ModuleRegistry.h"

#include <bit>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        AIModule* Module()
        {
            return dynamic_cast<AIModule*>(FindAIModule(Core::Module::GetModuleRegistry()));
        }
    }

    IMPLEMENT_CLASS(NavObstacleComponent, Core::Component::Component)

    NavObstacleComponent::NavObstacleComponent()
    {
        SetDefaults();
    }

    NavObstacleComponent::NavObstacleComponent(const Core::FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    NavObstacleComponent::NavObstacleComponent(const Core::IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    void NavObstacleComponent::SetDefaults()
    {
        Shape = ENavObstacleShape::Obb;
        Radius = 0.5f;
        HalfLength = 0.5f;
        HalfExtents = Math::Vector2(0.5f, 0.5f);
        MinHeight = 0.0f;
        MaxHeight = 2.0f;
        Mode = ENavObstacleMode::Block;
        CostMultiplier = 4.0f;
    }

    NavObstacleComponent::~NavObstacleComponent()
    {
        Unregister();
    }

    void NavObstacleComponent::Initialize()
    {
        Component::Initialize();
        if (auto* module = Module())
        {
            module->RegisterObstacle(*this);
        }
    }

    void NavObstacleComponent::Unregister()
    {
        if (m_bRegistered)
        {
            if (auto* module = Module())
            {
                module->UnregisterObstacle(*this);
            }
        }
    }

    void NavObstacleComponent::EndPlay()
    {
        Unregister();
        Component::EndPlay();
    }

    void NavObstacleComponent::Finalize()
    {
        Unregister();
        Component::Finalize();
    }

    void NavObstacleComponent::OnPropertyApplied(const Core::ClassProperty& property)
    {
        Component::OnPropertyApplied(property);
        if (&property == s_PropertyPtr_Shape || &property == s_PropertyPtr_Radius ||
            &property == s_PropertyPtr_HalfLength || &property == s_PropertyPtr_HalfExtents ||
            &property == s_PropertyPtr_MinHeight || &property == s_PropertyPtr_MaxHeight ||
            &property == s_PropertyPtr_Mode || &property == s_PropertyPtr_CostMultiplier)
        {
            m_bNeedsReevaluate = true;
        }
    }

    NavObstacleComponent::StampProperties NavObstacleComponent::GetStampProperties() const
    {
        const auto& half = HalfExtents.Get();
        return {Shape.Get(), std::bit_cast<uint32_t>(Radius.Get()), std::bit_cast<uint32_t>(HalfLength.Get()),
                std::bit_cast<uint32_t>(half.x), std::bit_cast<uint32_t>(half.y),
                std::bit_cast<uint32_t>(MinHeight.Get()), std::bit_cast<uint32_t>(MaxHeight.Get()),
                Mode.Get(), std::bit_cast<uint32_t>(CostMultiplier.Get())};
    }

    bool NavObstacleComponent::IsNavStamped() const
    {
        return m_Stamp.bApplied && !m_Stamp.Cells.empty() && m_Overlay &&
               m_Stamp.OverlayGeneration == m_Overlay->GetGeneration();
    }

    uint32_t NavObstacleComponent::GetStampedCellCount() const
    {
        return IsNavStamped() ? static_cast<uint32_t>(m_Stamp.Cells.size()) : 0u;
    }
}
