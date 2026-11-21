// ポストプロセスの JSON の読み書きと、最後の正常値を保つファイル追跡を実装する。
#include "Rendering/PostProcessProfile.h"

#include "Asset/TextAssetReloadTracker.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <initializer_list>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct FieldName
        {
            const char* Name;
            uint32_t Bit;
        };

        // 上書きする欄の名前と順序は、読み書きの両方でこの表を使う。
        constexpr FieldName FieldNames[] = {
            {"exposureBias", PostProcessFields::ExposureBias},
            {"whiteBalance", PostProcessFields::WhiteBalance},
            {"contrast", PostProcessFields::Contrast},
            {"pivot", PostProcessFields::ContrastPivot},
            {"saturation", PostProcessFields::Saturation},
            {"shadowLift", PostProcessFields::ShadowLift},
            {"highlightRolloff", PostProcessFields::HighlightRolloff},
            {"shadowTint", PostProcessFields::ShadowTint},
            {"highlightTint", PostProcessFields::HighlightTint},
            {"protectedHue", PostProcessFields::ProtectedHue},
            {"vignette", PostProcessFields::Vignette},
            {"grain", PostProcessFields::Grain},
            {"chromaticAberration", PostProcessFields::ChromaticAberration},
            {"lensDirt", PostProcessFields::LensDirt},
            {"look", PostProcessFields::Look},
            {"dither", PostProcessFields::Dither}};

        class ProfileJsonReader
        {
        public:
            ProfileJsonReader(Container::String& error, Container::VariableArray<Container::String>* unknownKeys)
                : m_Error(error), m_UnknownKeys(unknownKeys)
            {
            }

            bool Fail(const Container::String& path, const Container::String& reason)
            {
                m_Error = path + TEXT(": ") + reason;
                return false;
            }

            bool Object(JsonValue value, const char* path, std::initializer_list<const char*> keys)
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (!value.IsObject())
                {
                    return Fail(path, TEXT("オブジェクトが必要です"));
                }
                for (size_t index = 0; index < value.GetObjectSize(); ++index)
                {
                    const auto& name = value.GetMemberName(index);
                    bool known = false;
                    for (const char* key : keys)
                    {
                        if (name == key)
                        {
                            known = true;
                            break;
                        }
                    }
                    if (!known)
                    {
                        Container::String unknownPath(path);
                        if (!unknownPath.empty())
                        {
                            unknownPath += TEXT(".");
                        }
                        unknownPath += name;
                        NORVES_LOG_WARNING_F("PostProcess", "未知のポストプロセス設定を無視します: %s", unknownPath.c_str());
                        if (m_UnknownKeys)
                        {
                            m_UnknownKeys->push_back(unknownPath);
                        }
                    }
                }
                return true;
            }

            bool Number(JsonValue value, const char* path, float& out)
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (!value.IsNumber())
                {
                    return Fail(path, TEXT("数値が必要です"));
                }
                const double number = value.AsNumber();
                // float の範囲外を直接キャストせず、Sanitize が既定値へ戻す非有限値にする。
                constexpr double maximum = static_cast<double>(std::numeric_limits<float>::max());
                out = number > maximum ? std::numeric_limits<float>::infinity()
                    : number < -maximum ? -std::numeric_limits<float>::infinity()
                    : static_cast<float>(number);
                return true;
            }

            bool Boolean(JsonValue value, const char* path, bool& out)
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (!value.IsBoolean())
                {
                    return Fail(path, TEXT("真偽値が必要です"));
                }
                out = value.AsBool();
                return true;
            }

            bool Color(JsonValue value, const char* path, float (&out)[3])
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (!value.IsArray() || value.GetArraySize() != 3)
                {
                    return Fail(path, TEXT("長さ 3 の数値の配列が必要です"));
                }
                for (size_t index = 0; index < 3; ++index)
                {
                    if (!Number(value.GetArrayElement(index), path, out[index]))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool Lut(JsonValue value, const char* path, const char*& out)
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (value.IsNull())
                {
                    out = nullptr;
                    return true;
                }
                if (!value.IsString())
                {
                    return Fail(path, TEXT("文字列または null が必要です"));
                }
                const auto& text = value.AsString();
                // IdentityPool は文字列を消さず、unordered_map のノードは再ハッシュでも移動しない。
                // ビューは保存した String 全体を指して NUL 終端され、プロセス終了まで有効なので、
                // GameThread の JSON 破棄後も RenderThread から借用できる（CameraLookLut と同じ前提）。
                out = text.empty() ? nullptr : Identity(Container::StringView(text)).GetView().data();
                return true;
            }

            bool Mask(JsonValue value, uint32_t& out)
            {
                if (!value.IsValid())
                {
                    return true;
                }
                if (!value.IsArray())
                {
                    return Fail("overrideMask", TEXT("文字列の配列が必要です"));
                }
                out = PostProcessFields::None;
                for (size_t index = 0; index < value.GetArraySize(); ++index)
                {
                    const auto name = value.GetArrayElement(index);
                    if (!name.IsString())
                    {
                        return Fail("overrideMask", TEXT("各要素は文字列が必要です"));
                    }
                    bool known = false;
                    for (const auto& field : FieldNames)
                    {
                        if (name.AsString() == field.Name)
                        {
                            out |= field.Bit;
                            known = true;
                            break;
                        }
                    }
                    if (!known)
                    {
                        return Fail("overrideMask", Container::String(TEXT("未知の欄の名前です: ")) + name.AsString());
                    }
                }
                return true;
            }

        private:
            Container::String& m_Error;
            Container::VariableArray<Container::String>* m_UnknownKeys;
        };

        void WriteColor(JsonWriter& writer, const char* key, const float (&color)[3])
        {
            writer.BeginArray(key);
            for (float component : color)
            {
                writer.WriteNumberElement(component);
            }
            writer.EndArray();
        }

        void WriteLut(JsonWriter& writer, const char* key, const char* path)
        {
            if (path)
            {
                writer.WriteString(key, path);
            }
            else
            {
                writer.WriteNull(key);
            }
        }
    }

    bool ParsePostProcessProfileJson(Container::Span<const uint8_t> bytes, PostProcessProfile& outProfile,
        Container::String& outError, Container::VariableArray<Container::String>* outUnknownKeys)
    {
        outError.clear();
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            bytes = {bytes.data() + 3, bytes.size() - 3};
        }
        ProfileJsonReader reader(outError, outUnknownKeys);
        JsonDocument document;
        Container::String parseError;
        if (!JsonDocument::TryParseUtf8(bytes, document, &parseError))
        {
            return reader.Fail("$", Container::String(TEXT("JSON を解析できません: ")) + parseError);
        }
        const auto root = document.GetRoot();
        if (!root.IsObject())
        {
            return reader.Fail("$", TEXT("根はオブジェクトが必要です"));
        }
        const auto schema = root.FindMember("schema");
        if (!schema.IsString() || schema.AsString() != "postprocess.v1")
        {
            return reader.Fail("schema", TEXT("文字列 postprocess.v1 が必要です"));
        }

        PostProcessProfile profile;
        const auto name = root.FindMember("name");
        if (name.IsValid())
        {
            if (!name.IsString())
            {
                return reader.Fail("name", TEXT("文字列が必要です"));
            }
            profile.Name = Identity(Container::StringView(name.AsString()));
        }
        if (!reader.Mask(root.FindMember("overrideMask"), profile.OverrideMask) ||
            !reader.Object(root, "", {"schema", "name", "overrideMask", "grade", "lens", "look", "dither"}))
        {
            return false;
        }

        const auto grade = root.FindMember("grade");
        const auto whiteBalance = grade.FindMember("whiteBalance");
        const auto shadowLift = grade.FindMember("shadowLift");
        const auto highlightRolloff = grade.FindMember("highlightRolloff");
        const auto protectedHue = grade.FindMember("protectedHue");
        const auto lens = root.FindMember("lens");
        const auto vignette = lens.FindMember("vignette");
        const auto look = root.FindMember("look");
        auto& settings = profile.Settings;
        if (!reader.Object(grade, "grade", {"exposureBias", "whiteBalance", "contrast", "pivot", "saturation",
                "shadowLift", "highlightRolloff", "shadowTint", "highlightTint", "protectedHue"}) ||
            !reader.Object(whiteBalance, "grade.whiteBalance", {"temp", "tint"}) ||
            !reader.Object(shadowLift, "grade.shadowLift", {"amount", "rangeEV"}) ||
            !reader.Object(highlightRolloff, "grade.highlightRolloff", {"start", "strength"}) ||
            !reader.Object(protectedHue, "grade.protectedHue", {"enabled", "center", "width", "keep"}) ||
            !reader.Object(lens, "lens", {"vignette", "grain", "chromaticAberration", "lensDirt"}) ||
            !reader.Object(vignette, "lens.vignette", {"intensity", "radius", "softness"}) ||
            !reader.Object(look, "look", {"lutA", "lutB", "blend", "intensity"}) ||
            !reader.Number(grade.FindMember("exposureBias"), "grade.exposureBias", settings.Grade.ExposureBias) ||
            !reader.Number(whiteBalance.FindMember("temp"), "grade.whiteBalance.temp", settings.Grade.WhiteBalance.Temperature) ||
            !reader.Number(whiteBalance.FindMember("tint"), "grade.whiteBalance.tint", settings.Grade.WhiteBalance.Tint) ||
            !reader.Number(grade.FindMember("contrast"), "grade.contrast", settings.Grade.Contrast) ||
            !reader.Number(grade.FindMember("pivot"), "grade.pivot", settings.Grade.ContrastPivot) ||
            !reader.Number(grade.FindMember("saturation"), "grade.saturation", settings.Grade.Saturation) ||
            !reader.Number(shadowLift.FindMember("amount"), "grade.shadowLift.amount", settings.Grade.ShadowLift.Amount) ||
            !reader.Number(shadowLift.FindMember("rangeEV"), "grade.shadowLift.rangeEV", settings.Grade.ShadowLift.RangeEV) ||
            !reader.Number(highlightRolloff.FindMember("start"), "grade.highlightRolloff.start", settings.Grade.HighlightRolloff.Start) ||
            !reader.Number(highlightRolloff.FindMember("strength"), "grade.highlightRolloff.strength", settings.Grade.HighlightRolloff.Strength) ||
            !reader.Color(grade.FindMember("shadowTint"), "grade.shadowTint", settings.Grade.ShadowTint) ||
            !reader.Color(grade.FindMember("highlightTint"), "grade.highlightTint", settings.Grade.HighlightTint) ||
            !reader.Boolean(protectedHue.FindMember("enabled"), "grade.protectedHue.enabled", settings.Grade.ProtectedHue.bEnabled) ||
            !reader.Number(protectedHue.FindMember("center"), "grade.protectedHue.center", settings.Grade.ProtectedHue.CenterDeg) ||
            !reader.Number(protectedHue.FindMember("width"), "grade.protectedHue.width", settings.Grade.ProtectedHue.WidthDeg) ||
            !reader.Number(protectedHue.FindMember("keep"), "grade.protectedHue.keep", settings.Grade.ProtectedHue.Keep) ||
            !reader.Number(vignette.FindMember("intensity"), "lens.vignette.intensity", settings.Lens.Vignette.Intensity) ||
            !reader.Number(vignette.FindMember("radius"), "lens.vignette.radius", settings.Lens.Vignette.Radius) ||
            !reader.Number(vignette.FindMember("softness"), "lens.vignette.softness", settings.Lens.Vignette.Softness) ||
            !reader.Number(lens.FindMember("grain"), "lens.grain", settings.Lens.Grain) ||
            !reader.Number(lens.FindMember("chromaticAberration"), "lens.chromaticAberration", settings.Lens.ChromaticAberrationPixels) ||
            !reader.Number(lens.FindMember("lensDirt"), "lens.lensDirt", settings.Lens.LensDirtIntensity) ||
            !reader.Lut(look.FindMember("lutA"), "look.lutA", settings.Look.LutA) ||
            !reader.Lut(look.FindMember("lutB"), "look.lutB", settings.Look.LutB) ||
            !reader.Number(look.FindMember("blend"), "look.blend", settings.Look.Blend) ||
            !reader.Number(look.FindMember("intensity"), "look.intensity", settings.Look.Intensity) ||
            !reader.Number(root.FindMember("dither"), "dither", settings.DitherStrength))
        {
            return false;
        }

        const uint32_t restored = settings.Sanitize();
        for (const auto& field : FieldNames)
        {
            if ((restored & field.Bit) != 0)
            {
                NORVES_LOG_WARNING_F("PostProcess", "範囲外のポストプロセス設定を既定値へ戻しました: %s", field.Name);
            }
        }
        settings.bValid = true;
        outProfile = profile;
        return true;
    }

    Container::String WritePostProcessProfileJson(const PostProcessProfile& profile)
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("schema", "postprocess.v1");
        if (profile.Name.IsValid())
        {
            writer.WriteString("name", profile.Name.ToString());
        }
        writer.BeginArray("overrideMask");
        for (const auto& field : FieldNames)
        {
            if ((profile.OverrideMask & field.Bit) != 0)
            {
                writer.WriteStringElement(Container::String(field.Name));
            }
        }
        writer.EndArray();

        const auto& settings = profile.Settings;
        const auto& grade = settings.Grade;
        writer.BeginObject("grade");
        writer.WriteNumber("exposureBias", grade.ExposureBias);
        writer.BeginObject("whiteBalance");
        writer.WriteNumber("temp", grade.WhiteBalance.Temperature);
        writer.WriteNumber("tint", grade.WhiteBalance.Tint);
        writer.EndObject();
        writer.WriteNumber("contrast", grade.Contrast);
        writer.WriteNumber("pivot", grade.ContrastPivot);
        writer.WriteNumber("saturation", grade.Saturation);
        writer.BeginObject("shadowLift");
        writer.WriteNumber("amount", grade.ShadowLift.Amount);
        writer.WriteNumber("rangeEV", grade.ShadowLift.RangeEV);
        writer.EndObject();
        writer.BeginObject("highlightRolloff");
        writer.WriteNumber("start", grade.HighlightRolloff.Start);
        writer.WriteNumber("strength", grade.HighlightRolloff.Strength);
        writer.EndObject();
        WriteColor(writer, "shadowTint", grade.ShadowTint);
        WriteColor(writer, "highlightTint", grade.HighlightTint);
        writer.BeginObject("protectedHue");
        writer.WriteBool("enabled", grade.ProtectedHue.bEnabled);
        writer.WriteNumber("center", grade.ProtectedHue.CenterDeg);
        writer.WriteNumber("width", grade.ProtectedHue.WidthDeg);
        writer.WriteNumber("keep", grade.ProtectedHue.Keep);
        writer.EndObject();
        writer.EndObject();

        writer.BeginObject("lens");
        writer.BeginObject("vignette");
        writer.WriteNumber("intensity", settings.Lens.Vignette.Intensity);
        writer.WriteNumber("radius", settings.Lens.Vignette.Radius);
        writer.WriteNumber("softness", settings.Lens.Vignette.Softness);
        writer.EndObject();
        writer.WriteNumber("grain", settings.Lens.Grain);
        writer.WriteNumber("chromaticAberration", settings.Lens.ChromaticAberrationPixels);
        writer.WriteNumber("lensDirt", settings.Lens.LensDirtIntensity);
        writer.EndObject();
        writer.BeginObject("look");
        WriteLut(writer, "lutA", settings.Look.LutA);
        WriteLut(writer, "lutB", settings.Look.LutB);
        writer.WriteNumber("blend", settings.Look.Blend);
        writer.WriteNumber("intensity", settings.Look.Intensity);
        writer.EndObject();
        writer.WriteNumber("dither", settings.DitherStrength);
        writer.EndObject();
        return writer.ToString();
    }

    PostProcessProfileSource::PostProcessProfileSource() = default;
    PostProcessProfileSource::~PostProcessProfileSource() = default;
    PostProcessProfileSource::PostProcessProfileSource(PostProcessProfileSource&&) noexcept = default;
    PostProcessProfileSource& PostProcessProfileSource::operator=(PostProcessProfileSource&&) noexcept = default;

    bool PostProcessProfileSource::SetSource(const Container::AnsiString& assetRoot, Container::AnsiStringView path)
    {
        m_Tracker.reset();
        m_Profile = PostProcessProfile{};
        m_LastError.clear();
        m_bHasProfile = false;
        m_bReadFailed = false;
        auto tracker = Container::MakeUnique<Asset::TextAssetReloadTracker>(assetRoot);
        if (!tracker->SetPath(path))
        {
            m_LastError = TEXT("プロファイルのパスが不正です。Assets からの相対パスが必要です");
            return false;
        }
        m_Tracker = std::move(tracker);
        return true;
    }

    bool PostProcessProfileSource::Poll(double deltaSeconds)
    {
        if (!m_Tracker)
        {
            return false;
        }
        Asset::AssetBlob blob;
        const auto result = m_Tracker->Poll(deltaSeconds, blob);
        if (result == Asset::TextAssetPollResult::Changed)
        {
            m_bReadFailed = false;
            PostProcessProfile profile;
            if (!ParsePostProcessProfileJson(blob.GetSpan(), profile, m_LastError))
            {
                NORVES_LOG_WARNING_F("PostProcess", "ポストプロセスのプロファイルが不正なため以前の値を保持します: %s %s",
                    m_Tracker->GetPath().c_str(), m_LastError.c_str());
                return false;
            }
            m_Profile = profile;
            m_bHasProfile = true;
            return true;
        }
        if (result == Asset::TextAssetPollResult::ReadFailed)
        {
            m_LastError = TEXT("ポストプロセスのプロファイルを読み込めません: ");
            m_LastError += m_Tracker->GetPath().c_str();
            if (!m_bReadFailed)
            {
                NORVES_LOG_WARNING_F("PostProcess", "ポストプロセスのプロファイルを読めないため以前の値を保持します: %s",
                    m_Tracker->GetPath().c_str());
            }
            m_bReadFailed = true;
        }
        else if (result == Asset::TextAssetPollResult::Unchanged)
        {
            if (m_bReadFailed)
            {
                m_LastError.clear();
            }
            m_bReadFailed = false;
        }
        return false;
    }

    bool PostProcessProfileSource::HasProfile() const
    {
        return m_bHasProfile;
    }

    const PostProcessProfile& PostProcessProfileSource::GetProfile() const
    {
        return m_Profile;
    }

    const Container::String& PostProcessProfileSource::GetLastError() const
    {
        return m_LastError;
    }
} // namespace NorvesLib::Core::Rendering
