#pragma once

#include "Scene/PhysicsQueryTypes.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Text/IdentityPool.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    /** @brief コリジョンレイヤーの名前とビット位置です。 */
    struct PhysicsLayerEntry
    {
        Identity Name;
        uint8_t Bit;
    };

    /**
     * @brief 名前からコリジョンマスクを解決するデータ表です。
     *
     * 名前は Assets/Config/CollisionLayers.json が決め、エンジンは Default だけを知ります。
     * 同ファイルのゲーム固有の名前は仮であり、使う側の書き換えは後の段で行います。
     * 未知の名前を 0 や全ビットへ黙って置き換えず、解決失敗として通知します。
     */
    class PhysicsLayerTable
    {
    public:
        static constexpr const char* SchemaName = "collision-layers.v1";
        static constexpr size_t MaximumTextBytes = 1024 * 1024;
        static constexpr const char* DefaultLayerName = "Default";

        /** @brief Default = bit 0 だけの表を作ります。 */
        PhysicsLayerTable();

        /** @brief 表を Default = bit 0 だけに戻します。 */
        void ResetToDefault();

        /**
         * @brief JSON 全体を検査し、成功した場合だけ表を置き換えます。
         * @return 失敗時は false を返し、表を保持して error に日本語の理由を設定します。
         * 例外は呼び出し元へ伝播しません。診断用メモリも確保できない場合は error を保持します。
         */
        bool LoadFromJson(const Container::String& json, Container::String* error = nullptr);

        /** @brief 名前の 1 ビットを取得します。未知・空の名前では false を返し、出力を保持します。 */
        bool TryGetLayerMask(const Identity& name, PhysicsCollisionMask& outMask) const;

        /**
         * @brief 名前の列を論理和します。未知の名前があれば false を返し、出力を保持します。
         * 空の列は有効なマスク 0 として成功します。
         */
        bool TryBuildMask(Container::Span<const Identity> names, PhysicsCollisionMask& outMask) const;

        /** @brief 表の借用ビューです。表の再設定または破棄で無効になります。 */
        Container::Span<const PhysicsLayerEntry> GetLayers() const;

        /** @brief 表のレイヤー数を取得します。 */
        size_t GetLayerCount() const;

    private:
        Container::VariableArray<PhysicsLayerEntry> m_Layers;
    };
} // namespace NorvesLib::Core::Scene
