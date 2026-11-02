#pragma once

#include "Container/Containers.h"

#include <cstdint>

namespace Game::Bridge
{
    namespace Container = NorvesLib::Core::Container;

    struct BridgeAssetPropertyResult
    {
        bool Success = false;
        Container::String Code;
        Container::String Message;
    };

    struct BridgeAssetProperty
    {
        Container::String Path;
        Container::String TypeName;
        Container::String Unit;
        double Min = 0.0;
        double Max = 0.0;
        Container::String ValueJson;
    };

    using BridgeAssetDocumentId = uint64_t;

    class IBridgeAssetPropertyKind;
    struct BridgeAssetPropertyDocument;

    // 文書 ID はこのエディタの生存中だけ有効。各操作を同時に呼び出さない。
    class BridgeAssetPropertyEditor
    {
    public:
        explicit BridgeAssetPropertyEditor(const Container::String& assetRoot);
        ~BridgeAssetPropertyEditor();

        BridgeAssetPropertyEditor(const BridgeAssetPropertyEditor&) = delete;
        BridgeAssetPropertyEditor& operator=(const BridgeAssetPropertyEditor&) = delete;

        BridgeAssetPropertyResult Open(const Container::String& relativePath, BridgeAssetDocumentId& outDocument);
        BridgeAssetPropertyResult ListProperties(BridgeAssetDocumentId document,
            Container::VariableArray<BridgeAssetProperty>& outProperties) const;
        BridgeAssetPropertyResult SetProperty(BridgeAssetDocumentId document, const Container::String& path,
            const Container::String& valueJsonText);
        BridgeAssetPropertyResult Save(BridgeAssetDocumentId document);
        BridgeAssetPropertyResult Close(BridgeAssetDocumentId document);

    private:
        BridgeAssetPropertyDocument* FindDocument(BridgeAssetDocumentId document) const;

        Container::WideString m_RootPath;
        Container::VariableArray<Container::TUniquePtr<IBridgeAssetPropertyKind>> m_Kinds;
        Container::VariableArray<Container::TUniquePtr<BridgeAssetPropertyDocument>> m_Documents;
        BridgeAssetDocumentId m_NextDocumentId = 1;
        uint64_t m_SaveSequence = 0;
    };
} // namespace Game::Bridge
