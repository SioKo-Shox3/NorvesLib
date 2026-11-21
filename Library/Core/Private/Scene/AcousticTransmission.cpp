// 音響設定の検査、距離減衰の換算、借用した寄与源の評価と合成を行う。
#include "Scene/AcousticTransmission.h"
#include "Asset/AssetFileReader.h"
#include "Debug/Stats.h"
#include "Scene/AcousticDistance.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        constexpr size_t kMaximumTextBytes = 256 * 1024;
        constexpr double kMaxFloat = std::numeric_limits<float>::max();
        constexpr const char* kClassNames[] = {"Cue", "Ambient", "Footstep", "Voice", "Rain", "Creature"};
        static_assert(sizeof(kClassNames) / sizeof(kClassNames[0]) == static_cast<size_t>(ESoundClass::Count));

        AcousticSettingsLoadResult Fail(const char* message, Container::StringView key = {}) noexcept
        {
            AcousticSettingsLoadResult result;
            try
            {
                result.Detail.append(message);
                if (!key.empty())
                {
                    result.Detail.append("（");
                    result.Detail.append(key.data(), key.size());
                    result.Detail.append("）");
                }
            }
            catch (...)
            {
                // 診断文の確保にも失敗したときは、読み込みの失敗だけを返す。
            }
            return result;
        }

        bool IsFloatInRange(JsonValue value, double minimum, double maximum)
        {
            const double number = value.AsNumber();
            return value.IsNumber() && std::isfinite(number) && minimum <= number && number <= maximum;
        }

        // JSON の値と診断用のキーパス。存在するメンバだけを訪ね、省略された設定は既定値を保つ。
        struct SettingsNode
        {
            JsonValue Value;
            Container::String Path;

            AcousticSettingsLoadResult UnknownKey() const
            {
                return Fail("未知の音響設定キーです", Path);
            }

            template<typename ReadMember>
            AcousticSettingsLoadResult ReadObject(ReadMember readMember) const
            {
                if (!Value.IsObject())
                {
                    return Fail("オブジェクトを指定してください", Path);
                }
                for (size_t i = 0; i < Value.GetObjectSize(); ++i)
                {
                    const auto& key = Value.GetMemberName(i);
                    SettingsNode field{Value.GetMemberValue(i), Path + "." + key};
                    for (size_t j = 0; j < i; ++j)
                    {
                        if (key == Value.GetMemberName(j))
                        {
                            return Fail("設定キーが重複しています", field.Path);
                        }
                    }
                    auto result = readMember(key, field);
                    if (!result.Succeeded())
                    {
                        return result;
                    }
                }
                return {true, {}};
            }

            AcousticSettingsLoadResult ReadFloat(float& target, double minimum = 0.0, double maximum = kMaxFloat) const
            {
                if (!IsFloatInRange(Value, minimum, maximum))
                {
                    return Fail("指定範囲の有限な数値が必要です", Path);
                }
                target = static_cast<float>(Value.AsNumber());
                return {true, {}};
            }

            AcousticSettingsLoadResult ReadPositive(float& target) const
            {
                auto result = ReadFloat(target);
                if (result.Succeeded() && target <= 0.0f)
                {
                    return Fail("正の数値が必要です", Path);
                }
                return result;
            }

            AcousticSettingsLoadResult ReadBool(bool& target) const
            {
                if (!Value.IsBoolean())
                {
                    return Fail("真偽値が必要です", Path);
                }
                target = Value.AsBool();
                return {true, {}};
            }

            AcousticSettingsLoadResult ReadCurve(Container::VariableArray<AcousticCurvePoint>& target, double maxX) const
            {
                const size_t count = Value.GetArraySize();
                if (!Value.IsArray() || count < 1 || count > 64)
                {
                    return Fail("曲線には 1 個以上 64 個以下の点が必要です", Path);
                }
                target.clear();
                target.reserve(count);
                for (size_t i = 0; i < count; ++i)
                {
                    const auto pair = Value.GetArrayElement(i);
                    if (!pair.IsArray() || pair.GetArraySize() != 2)
                    {
                        return Fail("曲線の点には座標と騒音床の 2 要素が必要です", Path);
                    }
                    const auto x = pair.GetArrayElement(0);
                    const auto db = pair.GetArrayElement(1);
                    if (!IsFloatInRange(x, 0.0, maxX) || !IsFloatInRange(db, -kMaxFloat, kMaxFloat))
                    {
                        return Fail("曲線の座標または騒音床が数値の範囲外です", Path);
                    }
                    // 格納時の丸めで重なった点も退け、補間の分母を正に保つ。
                    const float coordinate = static_cast<float>(x.AsNumber());
                    if (!target.empty() && coordinate <= target.back().X)
                    {
                        return Fail("曲線の座標は狭義の昇順で指定してください", Path);
                    }
                    target.push_back({coordinate, static_cast<float>(db.AsNumber())});
                }
                return {true, {}};
            }
        };

        AcousticSettingsLoadResult ReadTerrain(const SettingsNode& node, AcousticSettings& settings)
        {
            return node.ReadObject([&](const Container::String& key, const SettingsNode& field)
            {
                if (key == "baseLossDb")
                {
                    return field.ReadFloat(settings.TerrainBaseLossDb);
                }
                if (key == "lossPerMeterDb")
                {
                    return field.ReadFloat(settings.TerrainLossPerMeterDb);
                }
                if (key == "maxLossDb")
                {
                    return field.ReadFloat(settings.TerrainMaxLossDb);
                }
                if (key == "minDepthM")
                {
                    return field.ReadFloat(settings.TerrainMinDepthM);
                }
                return field.UnknownKey();
            });
        }

        AcousticSettingsLoadResult ReadCutoff(const SettingsNode& node, AcousticSettings& settings)
        {
            return node.ReadObject([&](const Container::String& key, const SettingsNode& field)
            {
                if (key == "minHz")
                {
                    return field.ReadPositive(settings.HfCutoffMinHz);
                }
                if (key == "maxHz")
                {
                    return field.ReadPositive(settings.HfCutoffMaxHz);
                }
                if (key == "maxLossDb")
                {
                    return field.ReadPositive(settings.HfCutoffMaxLossDb);
                }
                return field.UnknownKey();
            });
        }

        AcousticSettingsLoadResult ReadClass(const SettingsNode& node, AcousticClassSettings& settings)
        {
            return node.ReadObject([&](const Container::String& key, const SettingsNode& field)
            {
                if (key == "referenceDistanceM")
                {
                    return field.ReadPositive(settings.ReferenceDistanceM);
                }
                if (key == "highFrequencyLevelOffsetDb")
                {
                    return field.ReadFloat(settings.HighFrequencyLevelOffsetDb, -kMaxFloat);
                }
                if (key == "exemptFromOcclusion")
                {
                    return field.ReadBool(settings.bExemptFromOcclusion);
                }
                if (key == "exemptFromMasking")
                {
                    return field.ReadBool(settings.bExemptFromMasking);
                }
                return field.UnknownKey();
            });
        }

        AcousticSettingsLoadResult ReadClasses(const SettingsNode& node, AcousticSettings& settings)
        {
            return node.ReadObject([&](const Container::String& key, const SettingsNode& field)
            {
                for (size_t i = 0; i < static_cast<size_t>(ESoundClass::Count); ++i)
                {
                    if (key == kClassNames[i])
                    {
                        return ReadClass(field, settings.Classes[i]);
                    }
                }
                return field.UnknownKey();
            });
        }

        AcousticSettingsLoadResult ReadNoiseFloor(const SettingsNode& node, AcousticSettings& settings, bool bRain)
        {
            return node.ReadObject([&](const Container::String& key, const SettingsNode& field)
            {
                if (key == "curve")
                {
                    return bRain ? field.ReadCurve(settings.RainNoiseFloorCurve, 1.0)
                                 : field.ReadCurve(settings.WindNoiseFloorCurve, kMaxFloat);
                }
                if (bRain && key == "shelteredReductionDb")
                {
                    return field.ReadFloat(settings.RainShelteredReductionDb);
                }
                return field.UnknownKey();
            });
        }

        AcousticSettingsLoadResult ReadSettings(const SettingsNode& root, AcousticSettings& settings)
        {
            return root.ReadObject([&](const Container::String& key, const SettingsNode& field) -> AcousticSettingsLoadResult
            {
                if (key == "version")
                {
                    if (!field.Value.IsIntegerLiteral() || field.Value.AsNumber() != 1.0)
                    {
                        return Fail("対応する版は整数の 1 です", field.Path);
                    }
                    return {true, {}};
                }
                if (key == "vegetationAbsorptionDbPerMeter")
                {
                    return field.ReadFloat(settings.VegetationAbsorptionDbPerMeter);
                }
                if (key == "wallClassFallbackLossDb")
                {
                    return field.ReadFloat(settings.WallClassFallbackLossDb);
                }
                if (key == "occlusionHighFrequencyRatio")
                {
                    return field.ReadFloat(settings.OcclusionHighFrequencyRatio, 0.0, 4.0);
                }
                if (key == "terrainOcclusion")
                {
                    return ReadTerrain(field, settings);
                }
                if (key == "hfLossToCutoff")
                {
                    return ReadCutoff(field, settings);
                }
                if (key == "rainNoiseFloor")
                {
                    return ReadNoiseFloor(field, settings, true);
                }
                if (key == "windNoiseFloor")
                {
                    return ReadNoiseFloor(field, settings, false);
                }
                if (key == "maskingCoefficient")
                {
                    return field.ReadFloat(settings.MaskingCoefficient);
                }
                if (key == "updateHz")
                {
                    return field.ReadPositive(settings.UpdateHz);
                }
                if (key == "classes")
                {
                    return ReadClasses(field, settings);
                }
                if (key == "voiceBudgetPerFrame")
                {
                    if (!field.Value.IsIntegerLiteral() || !IsFloatInRange(field.Value, 1.0, 1024.0))
                    {
                        return Fail("音声数は 1 以上 1024 以下の整数で指定してください", field.Path);
                    }
                    settings.VoiceBudgetPerFrame = field.Value.AsUInt32();
                    return {true, {}};
                }
                return field.UnknownKey();
            });
        }

        bool IsFinitePosition(const Math::Vector3& position)
        {
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        }

        float AddLoss(float accumulated, float loss)
        {
            if (!std::isfinite(loss) || loss <= 0.0f)
            {
                return accumulated;
            }
            return static_cast<float>(std::min(static_cast<double>(accumulated) + loss, kMaxFloat));
        }
    }

    const char* GetSoundClassName(ESoundClass soundClass) noexcept
    {
        return soundClass < ESoundClass::Count ? kClassNames[static_cast<size_t>(soundClass)] : "Unknown";
    }

    AcousticSettings AcousticSettings::MakeDefault()
    {
        // 値は仮。根拠なし。草の吸収はゲームとして効く強さの仮の値。
        AcousticSettings settings;
        settings.RainNoiseFloorCurve = {{0.0f, -120.0f}, {0.1f, 30.0f}, {0.5f, 45.0f}, {1.0f, 55.0f}};
        settings.WindNoiseFloorCurve = {{0.0f, -120.0f}, {5.0f, 25.0f}, {20.0f, 45.0f}};
        settings.Classes[static_cast<size_t>(ESoundClass::Cue)].HighFrequencyLevelOffsetDb = -6.0f;
        settings.Classes[static_cast<size_t>(ESoundClass::Ambient)].HighFrequencyLevelOffsetDb = -10.0f;
        settings.Classes[static_cast<size_t>(ESoundClass::Footstep)].HighFrequencyLevelOffsetDb = -3.0f;
        settings.Classes[static_cast<size_t>(ESoundClass::Voice)].HighFrequencyLevelOffsetDb = -8.0f;
        settings.Classes[static_cast<size_t>(ESoundClass::Creature)].HighFrequencyLevelOffsetDb = -6.0f;
        return settings;
    }

    const AcousticClassSettings& AcousticSettings::GetClass(ESoundClass soundClass) const
    {
        return Classes[static_cast<size_t>(soundClass < ESoundClass::Count ? soundClass : ESoundClass::Voice)];
    }

    AcousticSettingsLoadResult ParseAcousticSettingsJson(const Container::String& json, AcousticSettings& outSettings)
    {
        try
        {
            if (json.size() > kMaximumTextBytes)
            {
                return Fail("音響設定の JSON が 256 KiB を超えています");
            }
            const bool bHasBom = json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xef &&
                static_cast<unsigned char>(json[1]) == 0xbb && static_cast<unsigned char>(json[2]) == 0xbf;
            const Container::String withoutBom = bHasBom ? json.substr(3) : Container::String{};
            JsonDocument document;
            Container::String parseError;
            if (!JsonDocument::TryParse(bHasBom ? withoutBom : json, document, &parseError))
            {
                return Fail("音響設定の JSON を解析できません", parseError);
            }

            auto settings = AcousticSettings::MakeDefault();
            auto result = ReadSettings({document.GetRoot(), "$"}, settings);
            if (!result.Succeeded())
            {
                return result;
            }
            if (settings.HfCutoffMaxHz <= settings.HfCutoffMinHz)
            {
                return Fail("上限周波数は下限周波数より大きくしてください", "hfLossToCutoff.maxHz / hfLossToCutoff.minHz");
            }
            outSettings = std::move(settings);
            return result;
        }
        catch (...)
        {
            return Fail("音響設定の読み込み中に例外が発生しました");
        }
    }

    float ComputeAcousticDistanceGainDb(float referenceDistanceM, float distanceM) noexcept
    {
        AcousticDistanceParams params;
        params.MinDistance = std::isfinite(referenceDistanceM) && referenceDistanceM > 0.0f ? referenceDistanceM : 1.0f;
        params.MaxDistance = kAcousticDistanceMaxM;
        params.Rolloff = AcousticRolloff::InverseClamped;
        const float gain = EvaluateAcousticDistanceGain(params, distanceM);
        return gain <= 1.0e-6f ? kAcousticSilenceDb : std::clamp(20.0f * std::log10(gain), kAcousticSilenceDb, 0.0f);
    }

    float ComputeHearingRadiusThresholdDb(float referenceDistanceM, float radiusM) noexcept
    {
        return ComputeAcousticDistanceGainDb(referenceDistanceM, radiusM);
    }

    float ComputeHighFrequencyCutoffHz(const AcousticSettings& settings, float highFrequencyLossDb) noexcept
    {
        if (std::isnan(highFrequencyLossDb) || highFrequencyLossDb <= 0.0f)
        {
            return settings.HfCutoffMaxHz;
        }
        if (highFrequencyLossDb >= settings.HfCutoffMaxLossDb)
        {
            return settings.HfCutoffMinHz;
        }
        const double ratio = static_cast<double>(settings.HfCutoffMinHz) / settings.HfCutoffMaxHz;
        const double fraction = static_cast<double>(highFrequencyLossDb) / settings.HfCutoffMaxLossDb;
        return static_cast<float>(settings.HfCutoffMaxHz * std::pow(ratio, fraction));
    }

    float EvaluateAcousticCurveDb(Container::Span<const AcousticCurvePoint> curve, float x) noexcept
    {
        if (curve.empty())
        {
            return kAcousticSilenceDb;
        }
        if (std::isnan(x) || x <= curve.front().X)
        {
            return curve.front().Db;
        }
        if (x >= curve.back().X)
        {
            return curve.back().Db;
        }
        const auto* right = std::lower_bound(curve.begin(), curve.end(), x,
            [](const AcousticCurvePoint& point, float coordinate) { return point.X < coordinate; });
        const auto& left = *(right - 1);
        const double fraction = (static_cast<double>(x) - left.X) / (static_cast<double>(right->X) - left.X);
        return static_cast<float>(std::lerp(static_cast<double>(left.Db), static_cast<double>(right->Db), fraction));
    }

    AcousticTransmissionEvaluator::AcousticTransmissionEvaluator()
        : m_Settings(AcousticSettings::MakeDefault()), m_OwnerThreadId(Thread::Thread::GetCurrentThreadId())
    {
    }

    AcousticTransmissionEvaluator::~AcousticTransmissionEvaluator() = default;

    bool AcousticTransmissionEvaluator::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    EPhysicsSceneQueryResult AcousticTransmissionEvaluator::Evaluate(const AcousticPathQuery& query, AcousticPathResult& outResult) const
    {
        outResult = AcousticPathResult{};
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        if (!IsFinitePosition(query.Source) || !IsFinitePosition(query.Listener) ||
            !std::isfinite(query.SourceLevelDb) || query.SoundClass >= ESoundClass::Count)
        {
            return EPhysicsSceneQueryResult::InvalidArgument;
        }
        NORVES_STAT_SCOPE("Acoustics.Evaluate");

        // 座標差と長さを倍精度で求め、離れた有限座標やごく短い経路でも方向を保つ。
        const double dx = static_cast<double>(query.Listener.x) - query.Source.x;
        const double dy = static_cast<double>(query.Listener.y) - query.Source.y;
        const double dz = static_cast<double>(query.Listener.z) - query.Source.z;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        outResult.DistanceM = static_cast<float>(std::min(distance, kMaxFloat));
        Math::Vector3 direction;
        if (distance > 0.0)
        {
            direction = {static_cast<float>(dx / distance), static_cast<float>(dy / distance), static_cast<float>(dz / distance)};
        }
        const auto& cls = m_Settings.GetClass(query.SoundClass);
        const float distanceDb = ComputeAcousticDistanceGainDb(cls.ReferenceDistanceM, outResult.DistanceM);
        auto filteredQuery = query;
        filteredQuery.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Exclude;
        const AcousticContributionContext context{filteredQuery, m_Settings, cls, direction, outResult.DistanceM, outResult};

        try
        {
            float occlusionHighFrequencyDb = 0.0f;
            for (const auto& entry : m_Contributors)
            {
                const auto kind = entry.Contributor->GetKind();
                if (kind == EAcousticMediumKind::Masking)
                {
                    continue;
                }
                AcousticContribution contribution{};
                entry.Contributor->Contribute(context, contribution);
                switch (kind)
                {
                case EAcousticMediumKind::StaticGeometry:
                    outResult.StaticOcclusionDb = AddLoss(outResult.StaticOcclusionDb, contribution.BroadbandLossDb);
                    break;
                case EAcousticMediumKind::Vegetation:
                    outResult.VegetationLossDb = AddLoss(outResult.VegetationLossDb, contribution.BroadbandLossDb);
                    break;
                case EAcousticMediumKind::Terrain:
                    outResult.TerrainOcclusionDb = AddLoss(outResult.TerrainOcclusionDb, contribution.BroadbandLossDb);
                    break;
                case EAcousticMediumKind::Masking:
                    break;
                }
                occlusionHighFrequencyDb = AddLoss(occlusionHighFrequencyDb, contribution.HighFrequencyLossDb);
                if (contribution.bBlocksLineOfSight)
                {
                    outResult.bLineOfSight = false;
                }
            }

            const float occlusion = outResult.StaticOcclusionDb + outResult.VegetationLossDb + outResult.TerrainOcclusionDb;
            outResult.GainDb = std::clamp(distanceDb - (cls.bExemptFromOcclusion ? 0.0f : occlusion), kAcousticSilenceDb, 0.0f);
            outResult.HighFrequencyLossDb = cls.bExemptFromOcclusion ? 0.0f : occlusionHighFrequencyDb;

            // どのマスキング寄与源にも、遮蔽までの同じ途中結果を渡す。
            float noiseFloorDb = outResult.NoiseFloorDb;
            float maskingHighFrequencyDb = 0.0f;
            for (const auto& entry : m_Contributors)
            {
                if (entry.Contributor->GetKind() == EAcousticMediumKind::Masking)
                {
                    AcousticContribution contribution{};
                    entry.Contributor->Contribute(context, contribution);
                    if (std::isfinite(contribution.NoiseFloorDb))
                    {
                        noiseFloorDb = std::max(noiseFloorDb, contribution.NoiseFloorDb);
                    }
                    maskingHighFrequencyDb = AddLoss(maskingHighFrequencyDb, contribution.HighFrequencyLossDb);
                }
            }
            outResult.NoiseFloorDb = noiseFloorDb;
            if (!cls.bExemptFromMasking)
            {
                outResult.HighFrequencyLossDb = AddLoss(outResult.HighFrequencyLossDb, maskingHighFrequencyDb);
            }
            return EPhysicsSceneQueryResult::Success;
        }
        catch (...)
        {
            outResult = AcousticPathResult{};
            throw;
        }
    }

    EPhysicsSceneQueryResult AcousticTransmissionEvaluator::BindContributor(IAcousticMediumContributor& contributor, int32_t order)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        for (const auto& entry : m_Contributors)
        {
            if (entry.Contributor == &contributor)
            {
                return EPhysicsSceneQueryResult::AlreadyBound;
            }
        }
        const auto position = std::upper_bound(m_Contributors.begin(), m_Contributors.end(), order,
            [](int32_t value, const ContributorEntry& entry) { return value < entry.Order; });
        m_Contributors.insert(position, ContributorEntry{&contributor, order});
        return EPhysicsSceneQueryResult::Success;
    }

    EPhysicsSceneQueryResult AcousticTransmissionEvaluator::UnbindContributor(IAcousticMediumContributor& contributor)
    {
        if (!IsOwnerThread())
        {
            return EPhysicsSceneQueryResult::WrongThread;
        }
        if (m_Contributors.empty())
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }
        for (auto it = m_Contributors.begin(); it != m_Contributors.end(); ++it)
        {
            if (it->Contributor == &contributor)
            {
                m_Contributors.erase(it);
                return EPhysicsSceneQueryResult::Success;
            }
        }
        return EPhysicsSceneQueryResult::ProviderMismatch;
    }

    const AcousticSettings& AcousticTransmissionEvaluator::GetSettings() const
    {
        return m_Settings;
    }

    void AcousticTransmissionEvaluator::SetSettings(const AcousticSettings& settings)
    {
        AcousticSettings replacement = settings;
        m_Settings = std::move(replacement);
    }

    AcousticSettingsLoadResult AcousticTransmissionEvaluator::LoadSettingsFromJson(const Container::String& json)
    {
        return ParseAcousticSettingsJson(json, m_Settings);
    }

    AcousticSettingsLoadResult AcousticTransmissionEvaluator::LoadSettingsFromAssetFile(const Container::AnsiString& inputPath)
    {
        try
        {
            Asset::AssetFileReader reader;
            Asset::AssetReadRequest request;
            request.InputPath = inputPath;
            request.bAllowAbsolutePath = false;
            request.MaxReadBytes = kMaximumTextBytes;
            const auto asset = reader.Read(request);
            if (!asset.Succeeded())
            {
                return Fail("音響設定ファイルを読めません", inputPath);
            }
            Container::String text;
            if (!asset.Blob.IsEmpty())
            {
                text.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
            }
            return LoadSettingsFromJson(text);
        }
        catch (...)
        {
            return Fail("音響設定ファイルの読み込み中に例外が発生しました", inputPath);
        }
    }

    float AcousticTransmissionEvaluator::GetDistanceGainDb(ESoundClass soundClass, float distanceM) const
    {
        return ComputeAcousticDistanceGainDb(m_Settings.GetClass(soundClass).ReferenceDistanceM, distanceM);
    }

    float AcousticTransmissionEvaluator::HearingRadiusToThresholdDb(ESoundClass soundClass, float radiusM) const
    {
        return ComputeHearingRadiusThresholdDb(m_Settings.GetClass(soundClass).ReferenceDistanceM, radiusM);
    }

    float AcousticTransmissionEvaluator::HighFrequencyLossToCutoffHz(float highFrequencyLossDb) const
    {
        return ComputeHighFrequencyCutoffHz(m_Settings, highFrequencyLossDb);
    }
}
