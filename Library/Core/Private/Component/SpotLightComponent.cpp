#include "Component/SpotLightComponent.h"
#include "Object/Entity.h"
#include "Math/MathTypes.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <limits>

namespace
{
    double DegreesToCos(double degrees)
    {
        return std::cos(
            degrees * static_cast<double>(NorvesLib::Math::Constants::PI) / 180.0);
    }
}

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(SpotLightComponent, LightComponent)

    SpotLightComponent::SpotLightComponent()
        : LightComponent()
    {
        LightTypeProp = Rendering::LightType::Spot;
        IntensityUnit = LightIntensityUnit::Candela;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::SpotLightComponent(const FieldInitializer* initializer)
        : LightComponent(initializer)
    {
        LightTypeProp = Rendering::LightType::Spot;
        IntensityUnit = LightIntensityUnit::Candela;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::SpotLightComponent(const IUnknown* sourceObject)
        : LightComponent(sourceObject)
    {
        LightTypeProp = Rendering::LightType::Spot;
        IntensityUnit = LightIntensityUnit::Candela;
        Range = 10.0f;
        AttenuationConstant = 1.0f;
        AttenuationLinear = 0.09f;
        AttenuationQuadratic = 0.032f;
        InnerConeAngle = 12.5f;
        OuterConeAngle = 17.5f;
    }

    SpotLightComponent::~SpotLightComponent()
    {
    }

    void SpotLightComponent::Initialize()
    {
        LightComponent::Initialize();
    }

    void SpotLightComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
        // スポットライトは位置をオーナーのEntityからSyncToSceneView時に取得するため
        // Tick内での特別な処理は不要
    }

    // ========================================
    // LightProxy構築
    // ========================================

    bool SpotLightComponent::BuildLightProxy(Rendering::LightProxy& outProxy) const
    {
        Rendering::LightProxy snapshot = outProxy;

        // 基底クラスの共通チェックとフィールド設定
        if (!LightComponent::BuildLightProxy(snapshot))
        {
            return false;
        }

        // スポットライト固有
        snapshot.Type = Rendering::LightType::Spot;
        snapshot.Range = Range;
        snapshot.AttenuationConstant = AttenuationConstant;
        snapshot.AttenuationLinear = AttenuationLinear;
        snapshot.AttenuationQuadratic = AttenuationQuadratic;

        const float innerConeAngle = InnerConeAngle.Get();
        const float outerConeAngle = OuterConeAngle.Get();
        if (!std::isfinite(innerConeAngle) || !std::isfinite(outerConeAngle) ||
            innerConeAngle < 0.0f || outerConeAngle <= innerConeAngle ||
            outerConeAngle > 179.0f)
        {
            NORVES_LOG_WARNING("Light", "Rejected invalid spot light cone angles");
            return false;
        }

        const double innerCosine = DegreesToCos(static_cast<double>(innerConeAngle));
        const double outerCosine = DegreesToCos(static_cast<double>(outerConeAngle));
        const double omega = 2.0 * static_cast<double>(Math::Constants::PI) *
                             (1.0 - (innerCosine + outerCosine) / 2.0);
        if (!std::isfinite(omega) || omega <= 0.0f)
        {
            NORVES_LOG_WARNING("Light", "Rejected degenerate spot light cone solid angle");
            return false;
        }
        if (IntensityUnit.Get() == LightIntensityUnit::Lumen)
        {
            const double canonicalIntensity = static_cast<double>(Intensity.Get()) / omega;
            if (!std::isfinite(canonicalIntensity) || canonicalIntensity < 0.0 ||
                canonicalIntensity > static_cast<double>(std::numeric_limits<float>::max()))
            {
                NORVES_LOG_WARNING("Light", "Rejected non-representable spot light canonical intensity");
                return false;
            }
            snapshot.CanonicalIntensity = static_cast<float>(canonicalIntensity);
        }

        // オーナーのEntityから位置を取得
        const Entity* owner = GetOwner();
        if (owner)
        {
            const auto& pos = owner->GetPosition();
            snapshot.PositionX = pos.x;
            snapshot.PositionY = pos.y;
            snapshot.PositionZ = pos.z;
        }

        snapshot.InnerConeAngle = static_cast<float>(innerCosine);
        snapshot.OuterConeAngle = static_cast<float>(outerCosine);

        outProxy = snapshot;
        return true;
    }

} // namespace NorvesLib::Core::Component

