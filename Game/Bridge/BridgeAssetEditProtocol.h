// Bridge の資産の編集(asset.getProperties・asset.setProperty・asset.save)の params と結果の JSON を、SDK の型を使わずに組み立てる部品。

#pragma once

#include "BridgeAssetPropertyEditor.h"

namespace Game::Bridge
{
    // 資産編集を拒否した理由と、通信側へ返す説明を保持する。
    struct BridgeAssetEditRejection
    {
        Container::String Method;
        Container::String Reason;
        Container::String Path;
        bool bHasPath = false;
        Container::String Message;
    };

    Container::String BuildRejectionDataJson(const BridgeAssetEditRejection& rejection);

    struct BridgeAssetEditReply
    {
        bool bSuccess = false;
        Container::String ResultJson;
        BridgeAssetEditRejection Rejection;
    };

    // 同じ論理パスの文書と未保存の編集を保持する。各操作を同時に呼び出さない。
    class BridgeAssetEditService
    {
    public:
        explicit BridgeAssetEditService(const Container::String& assetRoot);

        BridgeAssetEditService(const BridgeAssetEditService&) = delete;
        BridgeAssetEditService& operator=(const BridgeAssetEditService&) = delete;

        BridgeAssetEditReply GetProperties(const Container::String& paramsJson);
        BridgeAssetEditReply SetProperty(const Container::String& paramsJson);
        BridgeAssetEditReply Save(const Container::String& paramsJson);

    private:
        struct OpenDocument
        {
            Container::String LogicalPath;
            BridgeAssetDocumentId Id = 0;
        };

        bool GetOrOpenDocument(const Container::String& logicalPath, BridgeAssetDocumentId& outDocument);

        BridgeAssetPropertyEditor m_Editor;
        Container::VariableArray<OpenDocument> m_Documents;
    };
} // namespace Game::Bridge
