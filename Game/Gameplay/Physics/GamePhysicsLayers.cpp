#include "GamePhysicsLayers.h"

namespace Game::Gameplay
{
    namespace Asset = NorvesLib::Core::Asset;
    namespace Container = NorvesLib::Core::Container;
    namespace Scene = NorvesLib::Core::Scene;

    bool InitializeGamePhysicsLayers(Scene::PhysicsLayerTable& layers, Container::String& error)
    {
        Asset::AssetFileReader reader;
        return InitializeGamePhysicsLayers(layers, reader, error);
    }

    bool InitializeGamePhysicsLayers(Scene::PhysicsLayerTable& layers, const Asset::AssetFileReader& reader,
        Container::String& error)
    {
        layers.ResetToDefault();
        Asset::AssetReadRequest request;
        request.InputPath = "Config/CollisionLayers.json";
        request.bAllowAbsolutePath = false;
        const auto asset = reader.Read(request);
        if (!asset.Succeeded())
        {
            error = "既定のコリジョンレイヤーのファイルを読めません";
            return false;
        }
        if (asset.Blob.GetSize() > Scene::PhysicsLayerTable::MaximumTextBytes)
        {
            error = "コリジョンレイヤーのファイルがサイズ上限を超えています";
            return false;
        }
        Container::String text;
        if (!asset.Blob.IsEmpty())
        {
            text.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
        }
        return layers.LoadFromJson(text, &error);
    }
} // namespace Game::Gameplay
