#pragma once
#include "Core/Public/Animation/AnimGraphResource.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
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
        bool Prepare(NorvesLib::Core::GameMode::GameModeContext&, NorvesLib::Core::Rendering::MaterialHandle);
        void ActivateViews(NorvesLib::Core::World&);
        void Stop();

      private:
        uint64_t m_Character = 0, m_Item = 0;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> m_CharacterAsset, m_ItemAsset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimGraphResource> m_Graph;
#if defined(NORVES_ENABLE_IMGUI)
        Game::Debug::AnimatorDebugView m_AnimatorView;
        Game::Debug::SocketDebugView m_SocketView;
#endif
    };
} // namespace Game::GameModes
