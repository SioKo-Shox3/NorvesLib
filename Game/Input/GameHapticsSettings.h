#pragma once

#include "Core/Public/Input/HapticsJson.h"
#include "Core/Public/Asset/AssetFileReader.h"

namespace Game::Input
{
    // 起動時の既定asset読込み。通常のI/O/validation失敗はfalse、旧設定/再生を維持。
    // 確保例外は伝播する。AssetFileReaderの全file読込み後、JSON解析前に1MiBを確認する。
    bool InitializeGameHaptics(NorvesLib::Core::Input::HapticsService& service,
        NorvesLib::Core::Container::String& error);
    bool InitializeGameHaptics(NorvesLib::Core::Input::HapticsService& service,
        const NorvesLib::Core::Asset::AssetFileReader& reader, NorvesLib::Core::Container::String& error);
    bool ApplyGameHapticsJson(NorvesLib::Core::Input::HapticsService& service,
        const NorvesLib::Core::Container::String& json, NorvesLib::Core::Container::String& error);
} // namespace Game::Input
