#pragma once

#include "Core/Public/Container/String.h"

namespace NorvesLib::Core::Rendering
{
    struct CapturedFrame;
} // namespace NorvesLib::Core::Rendering

namespace Game::Debug
{
    /**
     * @brief BackBuffer の取得結果（表示用に符号化済みの 8bit）を不透明の RGBA8 PNG として保存する
     *
     * --capture-png の保存と同じ形式。--capture-sequence と --capture-views が使う。
     * @return 取得が成功していて、対応する形式で、書き込めたら true
     */
    bool SaveCapturedFramePng(const NorvesLib::Core::Rendering::CapturedFrame& frame,
                              const NorvesLib::Core::Container::String& path);
} // namespace Game::Debug
