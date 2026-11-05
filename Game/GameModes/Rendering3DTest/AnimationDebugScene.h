#pragma once
#include "Core/Public/Animation/AnimGraphResource.h"
#include "Core/Public/Animation/GroundQuery.h"
#include "Core/Public/Animation/PoseModifier.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Particle/SwordTrailRibbon.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#if defined(NORVES_ENABLE_IMGUI)
#include "Debug/AnimatorDebugView.h"
#include "Debug/SocketDebugView.h"
#endif
namespace NorvesLib::Core
{
    class World;
}
namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}
namespace Game::GameModes
{
    // --animation-debugだけが生成する合成scene。既定起動画面へは配置しない。
    class AnimationDebugScene
    {
      public:
        ~AnimationDebugScene();
        bool Prepare(NorvesLib::Core::GameMode::GameModeContext&, NorvesLib::Core::Rendering::MaterialHandle,
                     bool bPoseModifiersEnabled = true);
        void ActivateViews(NorvesLib::Core::World&);
        void TickTrail(NorvesLib::Core::World&, float deltaTime);
        void Stop();

        uint64_t GetQuadrupedObjectId() const
        {
            return m_Quadruped;
        }
        const NorvesLib::Core::Container::String& GetPoseModifiersJson() const
        {
            return m_PoseModifiersJson;
        }
        const NorvesLib::Core::Container::String& GetPoseModifiersPath() const
        {
            return m_PoseModifiersPath;
        }
        NorvesLib::Core::Animation::IGroundQuery* GetGroundQuery() const
        {
            return m_GroundQuery.get();
        }
        const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& GetWeightParameterNames() const
        {
            return m_WeightParameterNames;
        }

      private:
        bool PrepareQuadruped(NorvesLib::Core::GameMode::GameModeContext&,
                              NorvesLib::Core::Rendering::MaterialHandle, bool bPoseModifiersEnabled);
        void TickQuadruped(NorvesLib::Core::World&, float deltaTime);
        uint64_t m_Quadruped = 0;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> m_QuadrupedAsset, m_FloorAsset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimGraphResource> m_QuadrupedGraph;
        NorvesLib::Core::Container::TUniquePtr<NorvesLib::Core::Animation::IGroundQuery> m_GroundQuery;
        // 設定から作った補正を保持し、場面の問い合わせより先に参照を切る。
        NorvesLib::Core::Container::VariableArray<
            NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Animation::IPoseModifier>> m_QuadrupedModifiers;
        NorvesLib::Core::Container::String m_PoseModifiersJson, m_PoseModifiersPath;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> m_WeightParameterNames;
        double m_QuadrupedTime = 0.0;
        float m_QuadrupedHeight = 0, m_QuadrupedBank = 0;
        uint64_t m_Character = 0, m_Item = 0;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> m_CharacterAsset, m_ItemAsset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimGraphResource> m_Graph;
        NorvesLib::Core::Particle::SwordTrailRibbon m_SwordTrail;
        NorvesLib::Core::Rendering::RibbonTrailScene m_TrailScene;
        double m_TrailTime = 0.0;
        bool m_bFirstTrailSample = true;
#if defined(NORVES_ENABLE_IMGUI)
        Game::Debug::AnimatorDebugView m_AnimatorView;
        Game::Debug::SocketDebugView m_SocketView;
#endif
    };
} // namespace Game::GameModes
