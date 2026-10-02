#pragma once
#include "Core/Public/Text/IdentityPool.h"

namespace Game::InputActions
{
    using namespace NorvesLib::Core::literals;
    // Game側の暫定参照ID。型・binding・感度の定義はDefaultInputBindings.jsonに置く。
    inline constexpr auto DebugContext="Debug"_id;
    inline constexpr auto GameplayContext="Gameplay"_id;
    inline constexpr auto MenuContext="Menu"_id;
    inline constexpr auto CutsceneContext="Cutscene"_id;
    inline constexpr auto Move="Move"_id;
    inline constexpr auto Look="Look"_id;
    inline constexpr auto Sprint="Sprint"_id;
    inline constexpr auto Bite="Bite"_id;
    inline constexpr auto Swing="Swing"_id;
    inline constexpr auto Jump="Jump"_id;
    inline constexpr auto Sniff="Sniff"_id;
    inline constexpr auto Confirm="Confirm"_id;
    inline constexpr auto Cancel="Cancel"_id;
}
