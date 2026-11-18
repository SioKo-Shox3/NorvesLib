// BridgeAssetEditService の実装。logicalPath ごとに文書を開いて保持し、拒否のときは値を変えない。

#include "BridgeAssetEditProtocol.h"

#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <cmath>
#include <utility>

namespace Game::Bridge
{
    namespace Core = NorvesLib::Core;

    namespace
    {
        BridgeAssetEditReply Reject(const char* method, const char* reason, const Container::String* path = nullptr)
        {
            BridgeAssetEditReply reply;
            auto& rejection = reply.Rejection;
            rejection.Method = method;
            rejection.Reason = reason;
            rejection.bHasPath = path != nullptr;
            if (path != nullptr)
            {
                rejection.Path = *path;
            }
            if (rejection.Reason == "unknown_asset")
            {
                rejection.Message = "The asset is unknown.";
            }
            else if (rejection.Reason == "unknown_property")
            {
                rejection.Message = "The asset property is unknown.";
            }
            else if (rejection.Reason == "out_of_range")
            {
                rejection.Message = "The value is outside the permitted range.";
            }
            else if (rejection.Reason == "save_failed")
            {
                rejection.Message = "The asset could not be saved.";
            }
            else
            {
                rejection.Message = "The value does not match the property type.";
            }
            return reply;
        }

        BridgeAssetEditReply Success(Container::String resultJson)
        {
            BridgeAssetEditReply reply;
            reply.bSuccess = true;
            reply.ResultJson = std::move(resultJson);
            return reply;
        }

        const char* EditFailureReason(const Container::String& code)
        {
            if (code == "invalid_document")
            {
                return "unknown_asset";
            }
            if (code == "out_of_range")
            {
                return "out_of_range";
            }
            if (code == "unknown_property")
            {
                return "unknown_property";
            }
            return "type_mismatch";
        }

        bool ParseParams(const Container::String& text, Core::JsonDocument& document)
        {
            return Core::JsonDocument::TryParse(text, document) && document.GetRoot().IsObject();
        }

        bool HasLogicalPath(const Core::JsonValue& root)
        {
            const auto logicalPath = root.FindMember("logicalPath");
            return logicalPath.IsString() && !logicalPath.AsString().empty();
        }

        void SkipWhitespace(const Container::String& text, size_t& position)
        {
            while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
                text[position] == '\r' || text[position] == '\n'))
            {
                ++position;
            }
        }

        bool SkipString(const Container::String& text, size_t& position)
        {
            if (position == text.size() || text[position++] != '"')
            {
                return false;
            }
            while (position < text.size())
            {
                const char c = text[position++];
                if (c == '"')
                {
                    return true;
                }
                if (c == '\\')
                {
                    if (position == text.size())
                    {
                        return false;
                    }
                    ++position;
                }
            }
            return false;
        }

        // 文法は解析済み。文字列と入れ子を飛ばし、最上位の値だけを原文から借りる。
        bool ExtractValueJson(const Container::String& text, const Core::JsonValue& root, Container::String& out)
        {
            size_t position = 0;
            SkipWhitespace(text, position);
            ++position; // 最上位オブジェクトの開始括弧。
            for (size_t member = 0; member < root.GetObjectSize(); ++member)
            {
                SkipWhitespace(text, position);
                if (!SkipString(text, position))
                {
                    return false;
                }
                SkipWhitespace(text, position);
                if (position == text.size() || text[position++] != ':')
                {
                    return false;
                }
                SkipWhitespace(text, position);
                const size_t start = position;
                size_t depth = 0;
                while (position < text.size())
                {
                    const char c = text[position];
                    if (c == '"')
                    {
                        if (!SkipString(text, position))
                        {
                            return false;
                        }
                        continue;
                    }
                    if (depth == 0 && (c == ',' || c == '}'))
                    {
                        break;
                    }
                    if (c == '{' || c == '[')
                    {
                        ++depth;
                    }
                    else if (c == '}' || c == ']')
                    {
                        if (depth == 0)
                        {
                            return false;
                        }
                        --depth;
                    }
                    ++position;
                }
                // 復号済みの名前で照合し、エスケープされたキーと重複キーも FindMember と揃える。
                if (root.GetMemberName(member) == "value")
                {
                    size_t end = position;
                    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                        text[end - 1] == '\r' || text[end - 1] == '\n'))
                    {
                        --end;
                    }
                    out = text.substr(start, end - start);
                    return true;
                }
                if (position == text.size() || text[position++] != ',')
                {
                    return false;
                }
            }
            return false;
        }

        void AppendString(Container::String& json, const Container::String& value)
        {
            Core::JsonWriter writer;
            writer.WriteStringElement(value);
            json += writer.ToString();
        }

        const char* ValueType(const Container::String& type)
        {
            if (type == "Float")
            {
                return "number";
            }
            if (type == "UInt")
            {
                return "integer";
            }
            if (type == "Bool")
            {
                return "boolean";
            }
            if (type == "Vector2")
            {
                return "vector2";
            }
            if (type == "Vector3")
            {
                return "vector3";
            }
            if (type == "Color")
            {
                return "color";
            }
            if (type == "Enum")
            {
                return "enum";
            }
            return "string";
        }

        bool IsNumericType(const Container::String& type)
        {
            return type == "Float" || type == "UInt" || type == "Vector2" || type == "Vector3" || type == "Color";
        }

        const BridgeAssetProperty* FindProperty(const Container::VariableArray<BridgeAssetProperty>& properties,
            const Container::String& path)
        {
            for (const auto& property : properties)
            {
                if (property.Path == path)
                {
                    return &property;
                }
            }
            return nullptr;
        }

        void AppendPropertyJson(Container::String& json, const BridgeAssetProperty& property)
        {
            Core::JsonWriter writer;
            writer.BeginObject();
            writer.WriteString("path", property.Path);
            writer.WriteString("valueType", ValueType(property.TypeName));
            if (!property.Unit.empty())
            {
                writer.WriteString("unit", property.Unit);
            }
            if (IsNumericType(property.TypeName) && property.Min < property.Max &&
                std::isfinite(property.Min) && std::isfinite(property.Max))
            {
                writer.WriteNumber("minimum", property.Min);
                writer.WriteNumber("maximum", property.Max);
            }
            if (!property.Description.empty())
            {
                writer.WriteString("description", property.Description);
            }
            writer.EndObject();
            Container::String metadata = writer.ToString();
            metadata.pop_back();
            json += metadata;
            json += ",\"value\":";
            json += property.ValueJson;
            json += '}';
        }
    } // namespace

    Container::String BuildRejectionDataJson(const BridgeAssetEditRejection& rejection)
    {
        Core::JsonWriter writer;
        writer.BeginObject();
        writer.WriteString("method", rejection.Method);
        writer.WriteString("reason", rejection.Reason);
        if (rejection.bHasPath)
        {
            writer.WriteString("path", rejection.Path);
        }
        writer.EndObject();
        return writer.ToString();
    }

    BridgeAssetEditService::BridgeAssetEditService(const Container::String& assetRoot)
        : m_Editor(assetRoot)
    {
    }

    bool BridgeAssetEditService::GetOrOpenDocument(const Container::String& logicalPath,
        BridgeAssetDocumentId& outDocument)
    {
        for (const auto& document : m_Documents)
        {
            if (document.LogicalPath == logicalPath)
            {
                outDocument = document.Id;
                return true;
            }
        }
        if (!m_Editor.Open(logicalPath, outDocument).Success)
        {
            return false;
        }
        OpenDocument document;
        document.LogicalPath = logicalPath;
        document.Id = outDocument;
        m_Documents.push_back(std::move(document));
        return true;
    }

    BridgeAssetEditReply BridgeAssetEditService::GetProperties(const Container::String& paramsJson)
    {
        constexpr const char* method = "asset.getProperties";
        Core::JsonDocument params;
        if (!ParseParams(paramsJson, params) || !HasLogicalPath(params.GetRoot()))
        {
            return Reject(method, "unknown_asset");
        }
        const auto& logicalPath = params.GetRoot().FindMember("logicalPath").AsString();
        BridgeAssetDocumentId document = 0;
        Container::VariableArray<BridgeAssetProperty> properties;
        if (!GetOrOpenDocument(logicalPath, document) || !m_Editor.ListProperties(document, properties).Success)
        {
            return Reject(method, "unknown_asset");
        }
        Container::String json = "{\"logicalPath\":";
        AppendString(json, logicalPath);
        json += ",\"properties\":[";
        bool bFirst = true;
        for (const auto& property : properties)
        {
            if (!bFirst)
            {
                json += ',';
            }
            bFirst = false;
            AppendPropertyJson(json, property);
        }
        json += "]}";
        return Success(std::move(json));
    }

    BridgeAssetEditReply BridgeAssetEditService::SetProperty(const Container::String& paramsJson)
    {
        constexpr const char* method = "asset.setProperty";
        Core::JsonDocument params;
        if (!ParseParams(paramsJson, params))
        {
            return Reject(method, "unknown_asset");
        }
        const auto root = params.GetRoot();
        const auto pathValue = root.FindMember("path");
        const Container::String* path = pathValue.IsString() ? &pathValue.AsString() : nullptr;
        if (!HasLogicalPath(root))
        {
            return Reject(method, "unknown_asset", path);
        }
        if (path == nullptr || path->empty())
        {
            return Reject(method, "unknown_property", path);
        }
        Container::String valueJson;
        if (!root.HasMember("value") || !ExtractValueJson(paramsJson, root, valueJson))
        {
            return Reject(method, "type_mismatch", path);
        }
        BridgeAssetDocumentId document = 0;
        if (!GetOrOpenDocument(root.FindMember("logicalPath").AsString(), document))
        {
            return Reject(method, "unknown_asset", path);
        }
        Container::VariableArray<BridgeAssetProperty> properties;
        const auto listed = m_Editor.ListProperties(document, properties);
        if (!listed.Success)
        {
            return Reject(method, EditFailureReason(listed.Code), path);
        }
        const auto* previous = FindProperty(properties, *path);
        if (previous == nullptr)
        {
            return Reject(method, "unknown_property", path);
        }
        const Container::String previousValue = previous->ValueJson;
        const auto applied = m_Editor.SetProperty(document, *path, valueJson);
        if (!applied.Success)
        {
            return Reject(method, EditFailureReason(applied.Code), path);
        }
        const auto relisted = m_Editor.ListProperties(document, properties);
        const auto* current = FindProperty(properties, *path);
        if (!relisted.Success || current == nullptr)
        {
            // 設定前の値は検証済みなので、結果の取得に失敗した場合も編集を残さない。
            m_Editor.SetProperty(document, *path, previousValue);
            return Reject(method, relisted.Success ? "unknown_property" : EditFailureReason(relisted.Code), path);
        }
        Container::String json = "{\"previousValue\":";
        json += previousValue;
        json += ",\"appliedValue\":";
        json += current->ValueJson;
        json += '}';
        return Success(std::move(json));
    }

    BridgeAssetEditReply BridgeAssetEditService::Save(const Container::String& paramsJson)
    {
        constexpr const char* method = "asset.save";
        Core::JsonDocument params;
        if (!ParseParams(paramsJson, params) || !HasLogicalPath(params.GetRoot()))
        {
            return Reject(method, "unknown_asset");
        }
        BridgeAssetDocumentId document = 0;
        if (!GetOrOpenDocument(params.GetRoot().FindMember("logicalPath").AsString(), document))
        {
            return Reject(method, "unknown_asset");
        }
        if (!m_Editor.Save(document).Success)
        {
            return Reject(method, "save_failed");
        }
        return Success("{\"saved\":true}");
    }
} // namespace Game::Bridge
