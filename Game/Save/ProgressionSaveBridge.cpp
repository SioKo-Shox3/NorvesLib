#include "ProgressionSaveBridge.h"

#include "ProgressSaveData.h"
#include "Core/Public/Text/JsonDocument.h"
#include "Core/Public/Text/JsonWriter.h"
#include "Gameplay/Progression/ProgressionState.h"

#include <utility>

namespace Game::Save
{
    using NorvesLib::Core::JsonDocument;
    using NorvesLib::Core::JsonWriter;
    using NorvesLib::Modules::Gameplay::ProgressionState;

    namespace
    {
        bool ApplyIds(const Container::VariableArray<Container::String>& ids, ProgressionState& state)
        {
            JsonWriter writer;
            writer.BeginObject();
            writer.WriteInt64("version", 1);
            writer.BeginArray("defeatedEnemyIds");
            for (const auto& id : ids)
            {
                writer.WriteStringElement(id);
            }
            writer.EndArray();
            writer.EndObject();
            return writer.IsComplete() && state.LoadStateFromJson(writer.ToString());
        }
    } // namespace

    bool CaptureProgression(const ProgressionState& state, ProgressSaveData& out)
    {
        JsonDocument document;
        if (!JsonDocument::TryParse(state.SaveStateToJson(), document))
        {
            return false;
        }

        const auto root = document.GetRoot();
        const auto version = root.FindMember("version");
        const auto defeated = root.FindMember("defeatedEnemyIds");
        if (!version.IsIntegerLiteral() || version.AsNumber() != 1.0 || !defeated.IsArray())
        {
            return false;
        }

        Container::VariableArray<Container::String> ids;
        ids.reserve(defeated.GetArraySize());
        for (size_t index = 0; index < defeated.GetArraySize(); ++index)
        {
            const auto id = defeated.GetArrayElement(index);
            if (!id.IsString())
            {
                return false;
            }
            ids.push_back(id.AsString());
        }
        out.Progress.Defeated = std::move(ids);
        return true;
    }

    bool ApplyProgression(const ProgressSaveData& data, ProgressionState& state)
    {
        return ApplyIds(data.Progress.Defeated, state);
    }

    bool ClearProgression(ProgressionState& state)
    {
        return ApplyIds({}, state);
    }
} // namespace Game::Save
