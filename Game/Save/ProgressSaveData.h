#pragma once

#include "Core/Public/Container/Map.h"
#include "Core/Public/Container/Span.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Save/SaveMigration.h"

namespace NorvesLib::Core::Save
{
    class GameStateStore;
}

namespace Game::Save
{
    namespace Container = NorvesLib::Core::Container;

    enum class WaypointKind : uint8_t { Bed, Checkpoint, Poi };

    struct WaypointRef
    {
        Container::String Id;
        WaypointKind Kind = WaypointKind::Checkpoint;
    };

    // ゲームプレイの実行時オブジェクトから独立した、保存用の値の集合。
    struct ProgressSaveData
    {
        uint32_t FormatVersion = NorvesLib::Core::Save::SavePayloadFormatVersion;
        Container::String AppVersion;
        int64_t SavedAtUnix = 0;
        double PlaySeconds = 0.0;

        struct PlayerData
        {
            double Pos[3] = {};
            double Yaw = 0.0;
            WaypointRef Waypoint;
        } Player;

        struct WorldData
        {
            double TimeOfDay = 0.0;
            int64_t Day = 0;
            Container::String Weather;
        } World;

        struct ProgressData
        {
            Container::VariableArray<Container::String> Defeated;
            Container::VariableArray<WaypointRef> UnlockedWaypoints;
            Container::VariableArray<uint8_t> MapMask;
            Container::Map<Container::String, bool> Flags;
            Container::Map<Container::String, int64_t> Ints;
            Container::Map<Container::String, double> Reals;
        } Progress;
    };

    // BOM の無い UTF-8 JSON を返す。非有限数などを書けない場合は空文字列を返す。
    Container::String ToJson(const ProgressSaveData& data);
    // 移行と検証がすべて成功した場合だけ out を置き換える。
    bool FromJson(Container::Span<const uint8_t> payloadUtf8, ProgressSaveData& out,
                  Container::String* pOutError = nullptr);
    void CaptureStateStore(const NorvesLib::Core::Save::GameStateStore& store, ProgressSaveData& out);
    void ApplyToStateStore(const ProgressSaveData& data, NorvesLib::Core::Save::GameStateStore& store);
} // namespace Game::Save
