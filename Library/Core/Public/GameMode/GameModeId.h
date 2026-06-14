#pragma once

#include <cstdint>

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモード識別子
     *
     * GameModeRegistryへの登録・検索キーとして使用する列挙型。
     * 新しいゲームモードを追加する場合はここにエントリを追加する。
     */
    enum class GameModeId : uint32_t
    {
        Rendering3DTest,
        MemoryAgingTest,
    };

} // namespace NorvesLib::Core::GameMode
