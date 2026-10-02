#include "GameHapticsSettings.h"
#include <utility>

namespace Game::Input
{
    namespace CoreInput = NorvesLib::Core::Input;
    namespace Container = NorvesLib::Core::Container;
    namespace Asset = NorvesLib::Core::Asset;
    bool InitializeGameHaptics(CoreInput::HapticsService& service, Container::String& error)
    {
        Asset::AssetFileReader reader;
        return InitializeGameHaptics(service, reader, error);
    }
    bool InitializeGameHaptics(CoreInput::HapticsService& service, const Asset::AssetFileReader& reader,
        Container::String& error)
    {
        Asset::AssetReadRequest request;
        request.InputPath = "Config/HapticsEffects.json";
        request.bAllowAbsolutePath = false;
        const auto asset = reader.Read(request);
        if (!asset.Succeeded())
        {
            error = "既定の振動効果assetを読めません";
            return false;
        }
        if (asset.Blob.GetSize() > CoreInput::HapticsJson::MaximumTextBytes)
        {
            error = "振動効果assetがサイズ上限を超えています";
            return false;
        }
        Container::String text;
        if (!asset.Blob.IsEmpty())
        {
            text.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
        }
        return ApplyGameHapticsJson(service, text, error);
    }
    bool ApplyGameHapticsJson(CoreInput::HapticsService& service, const Container::String& json,
        Container::String& error)
    {
        CoreInput::HapticsConfiguration candidate;
        CoreInput::HapticsJsonReport report;
        if (!CoreInput::HapticsJson::Parse(json, candidate, &report))
        {
            error = std::move(report.Error);
            return false;
        }
        if (!service.Configure({candidate.Effects.data(), candidate.Effects.size()}, candidate.Settings))
        {
            error = "振動効果と設定をserviceへ適用できません";
            return false;
        }
        error.clear();
        return true;
    }
} // namespace Game::Input
