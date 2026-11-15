// シ者の撃破時に実行する応答 ID と処理の対応を公開する。
#pragma once

#include "Container/String.h"

namespace Game::Gameplay
{
    class ShishaComponent;

    struct ShishaDefeatResponse
    {
        const char* Id;
        bool (*Run)(ShishaComponent& shisha);
    };

    inline constexpr const char* WitherRecoverResponseId = "wither.recover";

    const ShishaDefeatResponse* FindShishaDefeatResponse(const NorvesLib::Core::Container::String& id);
    bool RunWitherRecover(ShishaComponent& shisha);
} // namespace Game::Gameplay
