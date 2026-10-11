#pragma once

#include "Component/Component.h"
#include "Scripting/ScriptRuntime.h"

namespace NorvesLib::Core
{
    class ScriptRuntime;
    class World;

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
            void Finalize() override;

            /// 毎フレームの本体（非仮想）。World の順序付きの系が主群・優先度・深さ優先の順で呼ぶ
            void TickScript(float deltaTime);

            /// World へ Tick の本体を登録する（ScriptRuntime が World に束ねられたときに呼ぶ）
            static void RegisterTick(World& world);

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
