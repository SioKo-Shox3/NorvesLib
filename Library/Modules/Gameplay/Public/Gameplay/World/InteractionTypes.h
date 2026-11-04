#pragma once

#include "Container/String.h"
#include "Math/Vector3.h"
#include "Object/EntityHandle.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::World
{
    /// 選択された操作の表示と実行に必要な値の写し。
    struct InteractionPrompt
    {
        Core::EntityHandle Interactable;
        uint64_t ObjectId = 0;
        Core::Container::String ActionId;
        Core::Container::String PromptTextKey;
        Math::Vector3 WorldPosition;

        bool IsValid() const { return Interactable.IsValid(); }
    };
} // namespace NorvesLib::Modules::Gameplay::World
