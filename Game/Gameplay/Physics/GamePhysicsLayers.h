#pragma once

#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Scene/PhysicsLayerTable.h"

namespace Game::Gameplay
{
    // 起動時に既定のレイヤー表を読み込みます。失敗時は Default だけの表を保持します。
    // ファイル全体の読み込み後、JSON 解析前にサイズ上限を確認します。
    bool InitializeGamePhysicsLayers(NorvesLib::Core::Scene::PhysicsLayerTable& layers,
        NorvesLib::Core::Container::String& error);
    bool InitializeGamePhysicsLayers(NorvesLib::Core::Scene::PhysicsLayerTable& layers,
        const NorvesLib::Core::Asset::AssetFileReader& reader, NorvesLib::Core::Container::String& error);
} // namespace Game::Gameplay
