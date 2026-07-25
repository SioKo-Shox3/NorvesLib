#pragma once

#include "Component/Component.h"
#include "Scripting/ScriptRuntime.h"

namespace NorvesLib::Core
{
    class ScriptRuntime;

    namespace Component
    {
        class ScriptComponent final : public Component
        {
            REFLECTION_CLASS(ScriptComponent, Component)

        public:
            ScriptComponent();
            ~ScriptComponent() override;

            void BeginPlay() override;
            void EndPlay() override;
            void Tick(float deltaTime) override;
            void Finalize() override;

        protected:
            PROPERTY(Container::String, ScriptPath)
            PROPERTY(Container::String, ScriptClassName)

        private:
            void ReleaseBinding();

            ScriptBindingHandle m_BindingHandle;

            friend class ::NorvesLib::Core::ScriptRuntime;
        };
    } // namespace Component
} // namespace NorvesLib::Core

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::ScriptComponent, NorvesLib::Core::EClassCastFlags::Component)
