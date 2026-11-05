// クック済み草原の読み込みと、再生中の登録・公開・解除を行う。
#include "Component/GrassFieldComponent.h"

#include "Asset/AssetFileReader.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedGrassFieldFormat.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Vegetation/GrassField.h"
#include "Vegetation/GrassFieldAsset.h"
#include "Vegetation/GrassSpecies.h"
#include "Vegetation/TerrainGroundHeightSource.h"
#include "Vegetation/Vegetation.h"

#include <utility>

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool LoadSpeciesTable(const Asset::AssetSystem& assetSystem, const Container::String& path,
                              Vegetation::GrassSpeciesTable& outTable)
        {
            const auto resolved = assetSystem.ResolveAsset(path, Asset::AssetKind::Raw);
            Asset::AssetBlob blob = resolved.Blob;
            if (!resolved.Succeeded())
            {
                // Raw の解決が失敗した場合も、同じ loose ルートから直接読む。
                const auto& root = assetSystem.GetLooseAssetRoot();
                const Asset::AssetFileReader reader = root.empty() ? Asset::AssetFileReader() : Asset::AssetFileReader(root);
                Asset::AssetReadRequest request;
                request.InputPath = path;
                request.bAllowAbsolutePath = false;
                const auto loose = reader.Read(request);
                if (!loose.Succeeded())
                {
                    LOG_WARNING("GRASS_FIELD_SPECIES_TABLE path=%s status=ReadFailed code=%u 種別表を読めないため既定値を使います",
                                path.c_str(), static_cast<uint32_t>(loose.Status));
                    return false;
                }
                blob = loose.Blob;
            }

            const auto bytes = blob.GetSpan();
            size_t offset = 0;
            if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            {
                offset = 3;
            }
            Container::String json;
            if (bytes.size() > offset)
            {
                json.append(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
            }
            Container::String error;
            if (!Vegetation::TryParseGrassSpeciesTable(json, outTable, &error))
            {
                LOG_WARNING("GRASS_FIELD_SPECIES_TABLE path=%s status=ParseFailed 種別表を読めないため既定値を使います: %s",
                            path.c_str(), error.c_str());
                return false;
            }
            return true;
        }
    } // namespace

    IMPLEMENT_CLASS(GrassFieldComponent, Component)

    GrassFieldComponent::GrassFieldComponent()
        : Component()
    {
        SetDefaults();
    }

    GrassFieldComponent::GrassFieldComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    GrassFieldComponent::GrassFieldComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    GrassFieldComponent::~GrassFieldComponent() = default;

    void GrassFieldComponent::SetDefaults()
    {
        SetCookedFieldPath({});
        SetSpeciesTablePath("Assets/Vegetation/GrassSpecies.json");
        SetUseTerrainGround(true);
        SetTickEnabled(false);
    }

    void GrassFieldComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();

        const Container::String fieldPath = GetCookedFieldPath();
        Engine::Engine* engine = Engine::GEngine;
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!engine || !world)
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=MissingEngineOrWorld エンジンまたは所有者の World がありません",
                        fieldPath.c_str());
            return;
        }
        if (fieldPath.empty())
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=EmptyPath 草原のパスがありません", fieldPath.c_str());
            return;
        }
        const auto assetSystem = engine->GetTerrainSystem().GetAssetSystem();
        if (!assetSystem)
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=MissingAssetSystem 資産システムがありません",
                        fieldPath.c_str());
            return;
        }
        const auto resolved = assetSystem->ResolveAsset(fieldPath, Asset::AssetKind::World);
        if (!resolved.UsedCooked())
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=NotCooked code=%u reason=%s クック済み草原を読めません",
                        fieldPath.c_str(), static_cast<uint32_t>(resolved.Status), resolved.Reason.c_str());
            return;
        }
        const auto parsed = Asset::ParseCookedGrassField(resolved.Blob);
        if (!parsed.Succeeded())
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=%s 草原を解析できません",
                        fieldPath.c_str(), Asset::GetCookedGrassFieldParseStatusName(parsed.Status));
            return;
        }

        Vegetation::GrassSpeciesTable loadedTable;
        const Container::String speciesPath = GetSpeciesTablePath();
        (void)LoadSpeciesTable(*assetSystem, speciesPath, loadedTable);
        Container::String missingNames;
        m_Field = Vegetation::BuildGrassFieldFromCooked(parsed.Field, loadedTable, missingNames);
        if (!m_Field)
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=BuildFailed 草原を構築できません", fieldPath.c_str());
            return;
        }
        if (!missingNames.empty())
        {
            LOG_WARNING("GRASS_FIELD_SPECIES_TABLE path=%s status=MissingSpecies names=%s 表にない種別を既定値で補います",
                        speciesPath.c_str(), missingNames.c_str());
        }
        if (GetUseTerrainGround())
        {
            m_GroundSource = Container::MakeUnique<Vegetation::TerrainGroundHeightSource>(&engine->GetTerrainSystem());
            m_Field->SetGroundHeightSource(m_GroundSource.get());
        }
        auto& vegetation = engine->GetVegetation();
        if (!vegetation.RegisterGrassField(m_Field.get()))
        {
            LOG_WARNING("GRASS_FIELD_COMPONENT_SKIPPED path=%s status=AlreadyRegistered 別の草原が登録されています",
                        fieldPath.c_str());
            m_Field.reset();
            m_GroundSource.reset();
            return;
        }
        m_RegisteredEngine = engine;
        vegetation.PublishRenderData(engine->GetRenderWorld());
    }

    void GrassFieldComponent::EndPlay()
    {
        if (m_RegisteredEngine && m_Field)
        {
            auto& vegetation = m_RegisteredEngine->GetVegetation();
            vegetation.UnregisterGrassField(m_Field.get());
            vegetation.PublishRenderData(m_RegisteredEngine->GetRenderWorld());
        }
        m_RegisteredEngine = nullptr;
        m_Field.reset();
        m_GroundSource.reset();
        Component::EndPlay();
    }
} // namespace NorvesLib::Core::Component
