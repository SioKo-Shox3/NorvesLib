// セルのマニフェストを扱う。
#include "Streaming/CellManifest.h"

#include "Container/UnorderedSet.h"
#include "Text/JsonDocument.h"

#include <cfloat>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Streaming
{
    namespace
    {
        struct ManifestReader
        {
            CellManifestError* Error;
            int32_t CellIndex = -1;
            int32_t PayloadIndex = -1;

            bool Fail(ECellManifestError code, const char* message) const
            {
                if (Error != nullptr)
                {
                    Error->Code = code;
                    Error->CellIndex = CellIndex;
                    Error->PayloadIndex = PayloadIndex;
                    Error->Message = message;
                }
                return false;
            }

            bool Require(JsonValue object, const char* name, JsonValue& outValue) const
            {
                outValue = object.FindMember(name);
                if (!outValue.IsValid())
                {
                    return Fail(ECellManifestError::MissingField, "必須のフィールドがありません。");
                }
                return true;
            }

            bool ReadFloat(JsonValue value, float& outValue) const
            {
                if (!value.IsNumber())
                {
                    return Fail(ECellManifestError::InvalidFieldType, "数値が必要です。");
                }
                const double number = value.AsNumber();
                // 元の数値と、格納先の float での有限性を保証する。
                if (!std::isfinite(number) || number < -FLT_MAX || number > FLT_MAX)
                {
                    return Fail(ECellManifestError::NonFiniteValue, "有限の float として表せない数値です。");
                }
                outValue = static_cast<float>(number);
                if (!std::isfinite(outValue))
                {
                    return Fail(ECellManifestError::NonFiniteValue, "数値が有限ではありません。");
                }
                return true;
            }

            bool ReadVector(JsonValue value, Math::Vector3& outValue) const
            {
                if (!value.IsArray() || value.GetArraySize() != 3)
                {
                    return Fail(ECellManifestError::InvalidFieldType, "ベクトルには3要素の数値配列が必要です。");
                }
                return ReadFloat(value.GetArrayElement(0), outValue.x) &&
                       ReadFloat(value.GetArrayElement(1), outValue.y) &&
                       ReadFloat(value.GetArrayElement(2), outValue.z);
            }

            bool ValidatePath(const Container::String& path) const
            {
                if (path.empty())
                {
                    return Fail(ECellManifestError::EmptyLogicalPath, "論理パスが空です。");
                }
                if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'))
                {
                    return Fail(ECellManifestError::AbsolutePath, "論理パスに絶対パスは指定できません。");
                }
                size_t segmentStart = 0;
                for (size_t index = 0; index <= path.size(); ++index)
                {
                    if (index == path.size() || path[index] == '/' || path[index] == '\\')
                    {
                        if (index - segmentStart == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
                        {
                            return Fail(ECellManifestError::ParentDirectoryInPath, "論理パスに親ディレクトリの要素は指定できません。");
                        }
                        segmentStart = index + 1;
                    }
                }
                return true;
            }
        };
    } // namespace

    const char* ToString(ECellManifestError error)
    {
        switch (error)
        {
        case ECellManifestError::None: return "None";
        case ECellManifestError::ParseError: return "ParseError";
        case ECellManifestError::UnsupportedFormat: return "UnsupportedFormat";
        case ECellManifestError::MissingField: return "MissingField";
        case ECellManifestError::InvalidFieldType: return "InvalidFieldType";
        case ECellManifestError::NonPositiveCellSize: return "NonPositiveCellSize";
        case ECellManifestError::NonFiniteValue: return "NonFiniteValue";
        case ECellManifestError::InvertedBounds: return "InvertedBounds";
        case ECellManifestError::InvalidCellId: return "InvalidCellId";
        case ECellManifestError::DuplicateCellId: return "DuplicateCellId";
        case ECellManifestError::UnknownPayloadKind: return "UnknownPayloadKind";
        case ECellManifestError::EmptyLogicalPath: return "EmptyLogicalPath";
        case ECellManifestError::ParentDirectoryInPath: return "ParentDirectoryInPath";
        case ECellManifestError::AbsolutePath: return "AbsolutePath";
        }
        return "Unknown";
    }

    bool CellManifest::TryParse(const Container::String& text, Container::Span<const char* const> knownKinds,
                                CellManifest& outManifest, CellManifestError* pOutError)
    {
        // 検証途中のデータを出力へ残さず、成功時だけまとめて渡す。
        outManifest = CellManifest{};
        if (pOutError != nullptr)
        {
            *pOutError = CellManifestError{};
        }
        ManifestReader reader{pOutError};
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return reader.Fail(ECellManifestError::ParseError, "JSONを解析できません。");
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return reader.Fail(ECellManifestError::MissingField, "マニフェストのルートオブジェクトがありません。");
        }
        JsonValue format, origin, cellSize, cells;
        if (!reader.Require(root, "format", format) || !reader.Require(root, "origin", origin) ||
            !reader.Require(root, "cellSize", cellSize) || !reader.Require(root, "cells", cells))
        {
            return false;
        }
        if (!format.IsString() || !cells.IsArray())
        {
            return reader.Fail(ECellManifestError::InvalidFieldType, "format は文字列、cells は配列である必要があります。");
        }
        if (format.AsString() != kFormat)
        {
            return reader.Fail(ECellManifestError::UnsupportedFormat, "対応していないマニフェスト形式です。");
        }
        CellManifest parsed;
        if (!reader.ReadVector(origin, parsed.m_Origin) || !reader.ReadFloat(cellSize, parsed.m_CellSize))
        {
            return false;
        }
        if (parsed.m_CellSize <= 0.0f)
        {
            return reader.Fail(ECellManifestError::NonPositiveCellSize, "セルの大きさは正の数である必要があります。");
        }
        Container::UnorderedSet<StreamingCellId> seenIds;
        for (size_t cellIndex = 0; cellIndex < cells.GetArraySize(); ++cellIndex)
        {
            reader.CellIndex = static_cast<int32_t>(cellIndex);
            reader.PayloadIndex = -1;
            const JsonValue cell = cells.GetArrayElement(cellIndex);
            if (!cell.IsObject())
            {
                return reader.Fail(ECellManifestError::InvalidFieldType, "セルはオブジェクトである必要があります。");
            }
            JsonValue id, bounds, payloads;
            if (!reader.Require(cell, "id", id) || !reader.Require(cell, "bounds", bounds) ||
                !reader.Require(cell, "payloads", payloads))
            {
                return false;
            }
            const double idNumber = id.AsNumber();
            if (!id.IsIntegerLiteral() || !std::isfinite(idNumber) || idNumber < 0.0 ||
                idNumber >= static_cast<double>(kInvalidStreamingCellId))
            {
                return reader.Fail(ECellManifestError::InvalidCellId, "セルIDには無効値未満の非負の整数字句が必要です。");
            }
            StreamingCellDesc cellDesc;
            cellDesc.Id = id.AsUInt32();
            if (!seenIds.insert(cellDesc.Id).second)
            {
                return reader.Fail(ECellManifestError::DuplicateCellId, "セルIDが重複しています。");
            }
            if (!bounds.IsObject() || !payloads.IsArray())
            {
                return reader.Fail(ECellManifestError::InvalidFieldType, "bounds はオブジェクト、payloads は配列である必要があります。");
            }
            JsonValue minimum, maximum;
            if (!reader.Require(bounds, "min", minimum) || !reader.Require(bounds, "max", maximum) ||
                !reader.ReadVector(minimum, cellDesc.Bounds.Min) || !reader.ReadVector(maximum, cellDesc.Bounds.Max))
            {
                return false;
            }
            // float への丸めで大小関係が失われる前の値で比較する。
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (minimum.GetArrayElement(axis).AsNumber() > maximum.GetArrayElement(axis).AsNumber())
                {
                    return reader.Fail(ECellManifestError::InvertedBounds, "境界の最小値が最大値を超えています。");
                }
            }
            for (size_t payloadIndex = 0; payloadIndex < payloads.GetArraySize(); ++payloadIndex)
            {
                reader.PayloadIndex = static_cast<int32_t>(payloadIndex);
                const JsonValue payload = payloads.GetArrayElement(payloadIndex);
                if (!payload.IsObject())
                {
                    return reader.Fail(ECellManifestError::InvalidFieldType, "ペイロードはオブジェクトである必要があります。");
                }
                JsonValue kind, logicalPath;
                if (!reader.Require(payload, "kind", kind) || !reader.Require(payload, "logicalPath", logicalPath))
                {
                    return false;
                }
                if (!kind.IsString() || !logicalPath.IsString())
                {
                    return reader.Fail(ECellManifestError::InvalidFieldType, "kind と logicalPath は文字列である必要があります。");
                }
                bool bKnownKind = false;
                for (size_t kindIndex = 0; kindIndex < knownKinds.size(); ++kindIndex)
                {
                    if (knownKinds[kindIndex] != nullptr && kind.AsString() == knownKinds[kindIndex])
                    {
                        bKnownKind = true;
                        break;
                    }
                }
                if (!bKnownKind)
                {
                    return reader.Fail(ECellManifestError::UnknownPayloadKind, "登録されていないペイロード種別です。");
                }
                if (!reader.ValidatePath(logicalPath.AsString()))
                {
                    return false;
                }
                cellDesc.Payloads.push_back(StreamingPayloadDesc{kind.AsString(), logicalPath.AsString()});
            }
            parsed.m_Cells.push_back(std::move(cellDesc));
        }
        outManifest = std::move(parsed);
        return true;
    }

    const Math::Vector3& CellManifest::GetOrigin() const
    {
        return m_Origin;
    }

    float CellManifest::GetCellSize() const
    {
        return m_CellSize;
    }

    Container::Span<const StreamingCellDesc> CellManifest::GetCells() const
    {
        return Container::Span<const StreamingCellDesc>(m_Cells.data(), m_Cells.size());
    }

    const StreamingCellDesc* CellManifest::FindCell(StreamingCellId id) const
    {
        for (const StreamingCellDesc& cell : m_Cells)
        {
            if (cell.Id == id)
            {
                return &cell;
            }
        }
        return nullptr;
    }
} // namespace NorvesLib::Core::Streaming
