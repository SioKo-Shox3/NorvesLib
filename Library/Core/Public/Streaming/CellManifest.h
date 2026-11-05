// セルのマニフェストを扱う。
#pragma once

#include "Streaming/StreamingTypes.h"
#include "Container/Span.h"

namespace NorvesLib::Core::Streaming
{
    enum class ECellManifestError : uint8_t
    {
        None, ParseError, UnsupportedFormat, MissingField, InvalidFieldType,
        NonPositiveCellSize, NonFiniteValue, InvertedBounds, InvalidCellId,
        DuplicateCellId, UnknownPayloadKind, EmptyLogicalPath, ParentDirectoryInPath, AbsolutePath
    };

    const char* ToString(ECellManifestError error);

    struct CellManifestError
    {
        ECellManifestError Code = ECellManifestError::None;
        int32_t CellIndex = -1; // セルに属さない問題は -1。
        int32_t PayloadIndex = -1; // ペイロードに属さない問題は -1。
        Container::String Message;
    };

    class CellManifest
    {
    public:
        static constexpr const char* kFormat = "norves.cell-manifest.v1";

        // 未登録の種別を拒否し、失敗時は出力を空にする。成功時のエラーは None に戻す。
        static bool TryParse(const Container::String& text, Container::Span<const char* const> knownKinds,
                             CellManifest& outManifest, CellManifestError* pOutError = nullptr);

        const Math::Vector3& GetOrigin() const;
        float GetCellSize() const;
        // 返す参照とビューは、このマニフェストの再代入・破棄まで有効。
        Container::Span<const StreamingCellDesc> GetCells() const;
        // 見つからない場合は nullptr を返す。
        const StreamingCellDesc* FindCell(StreamingCellId id) const;

    private:
        Math::Vector3 m_Origin;
        float m_CellSize = 0.0f;
        Container::VariableArray<StreamingCellDesc> m_Cells;
    };
} // namespace NorvesLib::Core::Streaming
