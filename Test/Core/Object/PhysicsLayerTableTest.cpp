// 名前とビットの対応、読み込み失敗時の表の保持、既定データの契約を反証する。
#include "Asset/AssetFileReader.h"
#include "Scene/PhysicsLayerTable.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Scene;

static_assert(DefaultPhysicsLayer == 1u);

namespace
{
    struct ExpectedLayer
    {
        const char* Name;
        PhysicsCollisionMask Mask;
    };

    constexpr ExpectedLayer expectedLayers[] = {
        {"Default", DefaultPhysicsLayer},
        {"Alpha", PhysicsCollisionMask{1u} << 3},
        {"Beta", PhysicsCollisionMask{1u} << 12},
        {"Omega", PhysicsCollisionMask{1u} << 31},
    };

    void ExpectLayer(const PhysicsLayerTable& table, const char* name, PhysicsCollisionMask expected)
    {
        PhysicsCollisionMask mask = 0;
        assert(table.TryGetLayerMask(Identity(name), mask));
        assert(mask == expected);
    }

    void ExpectAcceptedTable(const PhysicsLayerTable& table)
    {
        assert(table.GetLayerCount() == sizeof(expectedLayers) / sizeof(expectedLayers[0]));
        for (const auto& layer : expectedLayers)
        {
            ExpectLayer(table, layer.Name, layer.Mask);
        }
    }

    struct InvalidTable
    {
        const char* Label;
        const char* Json;
        const char* OtherRejectedName;
    };
}

int main()
{
    PhysicsLayerTable table;
    assert(table.GetLayerCount() == 1);
    ExpectLayer(table, "Default", DefaultPhysicsLayer);
    std::cout << "initial_table passed\n";

    const Container::String validJson = R"({
        "schema": "collision-layers.v1",
        "layers": [
            {"name": "Default", "bit": 0},
            {"name": "Alpha", "bit": 3},
            {"name": "Beta", "bit": 12},
            {"name": "Omega", "bit": 31}
        ]
    })";
    assert(table.LoadFromJson(validJson));
    ExpectAcceptedTable(table);

    const Identity names[] = {Identity("Alpha"), Identity("Omega"), Identity("Default")};
    PhysicsCollisionMask mask = 0;
    assert(table.TryBuildMask(names, mask));
    assert(mask == ((PhysicsCollisionMask{1u} << 3) | (PhysicsCollisionMask{1u} << 31) | DefaultPhysicsLayer));
    mask = AllPhysicsLayers;
    assert(table.TryBuildMask(Container::Span<const Identity>{}, mask));
    assert(mask == 0);
    std::cout << "valid_table_and_masks passed\n";

    constexpr PhysicsCollisionMask sentinel = 0xA5A55A5Au;
    mask = sentinel;
    assert(!table.TryGetLayerMask(Identity("Unknown"), mask));
    assert(mask == sentinel);
    const Identity namesWithUnknown[] = {Identity("Alpha"), Identity("Unknown"), Identity("Omega")};
    mask = sentinel;
    assert(!table.TryBuildMask(namesWithUnknown, mask));
    assert(mask == sentinel);
    std::cout << "unknown_names_preserve_output passed\n";

    const InvalidTable invalidTables[] = {
        {"duplicate_names",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":3},{"name":"Rejected","bit":4}]})", nullptr},
        {"duplicate_bits",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":3},{"name":"RejectedSecond","bit":3}]})", "RejectedSecond"},
        {"bit_32",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":32}]})", nullptr},
        {"negative_bit",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":-1}]})", nullptr},
        {"empty_name",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":3},{"name":"","bit":4}]})", ""},
        {"missing_default",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Rejected","bit":3}]})", nullptr},
        {"default_not_zero",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Rejected","bit":3},{"name":"Default","bit":1}]})", nullptr},
        {"malformed_json",
            R"({"schema":"collision-layers.v1","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":3})", nullptr},
        {"wrong_schema",
            R"({"schema":"collision-layers.v2","layers":[{"name":"Default","bit":0},{"name":"Rejected","bit":3}]})", nullptr},
    };
    for (const auto& invalid : invalidTables)
    {
        std::cout << "checking_" << invalid.Label << std::endl;
        assert(!table.LoadFromJson(Container::String(invalid.Json)));
        ExpectAcceptedTable(table);
        mask = sentinel;
        assert(!table.TryGetLayerMask(Identity("Rejected"), mask));
        assert(mask == sentinel);
        if (invalid.OtherRejectedName != nullptr)
        {
            mask = sentinel;
            assert(!table.TryGetLayerMask(Identity(invalid.OtherRejectedName), mask));
            assert(mask == sentinel);
        }
    }
    std::cout << "invalid_tables_preserve_previous_table passed\n";

    const Container::String extraFieldsJson = R"({
        "schema": "collision-layers.v1",
        "unknownTopLevel": {"value": true},
        "layers": [
            {"name": "Default", "bit": 0, "unknownLayerField": 42},
            {"name": "Additional", "bit": 17, "unknownLayerField": "ignored"}
        ]
    })";
    assert(table.LoadFromJson(extraFieldsJson));
    assert(table.GetLayerCount() == 2);
    ExpectLayer(table, "Default", DefaultPhysicsLayer);
    ExpectLayer(table, "Additional", PhysicsCollisionMask{1u} << 17);
    std::cout << "unknown_fields passed\n";

    Asset::AssetFileReader reader;
    Asset::AssetReadRequest request;
    request.InputPath = "Config/CollisionLayers.json";
    request.bAllowAbsolutePath = false;
    const auto result = reader.Read(request);
    assert(result.Succeeded() && !result.Blob.IsEmpty());
    Container::String defaultJson;
    defaultJson.append(reinterpret_cast<const char*>(result.Blob.GetData()), result.Blob.GetSize());
    assert(table.LoadFromJson(defaultJson));
    const char* defaultNames[] = {
        "Default", "Terrain", "Platform", "Player", "Hurtbox", "Hitbox",
        "Prop", "Animal", "GrassInteract", "CameraBlock", "Trigger", "Perception",
    };
    PhysicsCollisionMask usedBits = 0;
    for (const char* name : defaultNames)
    {
        mask = 0;
        assert(table.TryGetLayerMask(Identity(name), mask));
        assert(mask != 0 && (mask & (mask - 1u)) == 0);
        assert((usedBits & mask) == 0);
        usedBits |= mask;
    }
    ExpectLayer(table, "Default", DefaultPhysicsLayer);
    std::cout << "default_asset passed\nPhysicsLayerTableTest passed\n";
    return 0;
}
