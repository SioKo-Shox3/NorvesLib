// クック済み草原を読み、所有者の World で再生中だけ草の場を登録する。
// 保存するのはパスと地面の設定だけで、実行時の場と地面は EndPlay で返す。
#pragma once

#include "Component.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Vegetation
{
    class GrassField;
    class TerrainGroundHeightSource;
}

namespace NorvesLib::Core::Component
{
    class GrassFieldComponent : public Component
    {
        REFLECTION_CLASS(GrassFieldComponent, Component)

    public:
        GrassFieldComponent();
        explicit GrassFieldComponent(const FieldInitializer* initializer);
        explicit GrassFieldComponent(const IUnknown* sourceObject);
        ~GrassFieldComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        void SetCookedFieldPath(const Container::String& path) { getCookedFieldPath() = path; }
        Container::String GetCookedFieldPath() { return getCookedFieldPath(); }
        void SetSpeciesTablePath(const Container::String& path) { getSpeciesTablePath() = path; }
        Container::String GetSpeciesTablePath() { return getSpeciesTablePath(); }
        void SetUseTerrainGround(bool bUse) { getbUseTerrainGround() = bUse; }
        bool GetUseTerrainGround() { return getbUseTerrainGround(); }

        PROPERTY(Container::String, CookedFieldPath)
        PROPERTY(Container::String, SpeciesTablePath)
        PROPERTY(bool, bUseTerrainGround)

    private:
        void SetDefaults();

        // 場が借用する地面を先に宣言し、場より後に破棄する。
        Container::TUniquePtr<Vegetation::TerrainGroundHeightSource> m_GroundSource;
        Container::TUniquePtr<Vegetation::GrassField> m_Field;
        Engine::Engine* m_RegisteredEngine = nullptr;
    };
} // namespace NorvesLib::Core::Component
