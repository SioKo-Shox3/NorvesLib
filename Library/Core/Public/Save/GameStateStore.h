#pragma once

#include "Container/Containers.h"
#include "Delegate/MulticastDelegate.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::Core::Save
{
    enum class GameStateScope : uint8_t { Persistent, Session };
    enum class GameStateValueType : uint8_t { Bool, Int64, Double };

    struct GameStateEntry
    {
        Container::String Name;
        GameStateValueType Type = GameStateValueType::Bool;
        bool BoolValue = false;
        int64_t IntValue = 0;
        double DoubleValue = 0.0;
    };

    // 呼び出し元で操作を直列化する。読み込み成功時は台帳と値をまとめて置き換える。
    class GameStateStore
    {
    public:
        bool LoadKeyLedgerFromText(const Container::String& jsonText, Container::String* pOutError = nullptr);
        bool LoadKeyLedgerFile(const Container::String& utf8Path);

        bool TryGetBool(Identity key, bool& out) const;
        bool TryGetInt64(Identity key, int64_t& out) const;
        bool TryGetDouble(Identity key, double& out) const;
        bool SetBool(Identity key, bool value);
        bool SetInt64(Identity key, int64_t value);
        bool SetDouble(Identity key, double value);

        MulticastDelegate<Identity>& OnValueChanged() { return m_OnValueChanged; }

        // 出力を置き換える。名前はストアから独立して所有される。
        void CollectPersistent(Container::VariableArray<GameStateEntry>& out) const;
        bool ApplyPersistentBool(const Container::String& name, bool value);
        bool ApplyPersistentInt64(const Container::String& name, int64_t value);
        bool ApplyPersistentDouble(const Container::String& name, double value);
        // 値が変わったキーを通知する。通知中に台帳が置き換わった場合は処理を終える。
        void ResetScope(GameStateScope scope);

    private:
        struct Record
        {
            Container::String Name;
            GameStateScope Scope = GameStateScope::Persistent;
            GameStateValueType Type = GameStateValueType::Bool;
            bool BoolValue = false;
            int64_t IntValue = 0;
            double DoubleValue = 0.0;
            bool DefaultBool = false;
            int64_t DefaultInt = 0;
            double DefaultDouble = 0.0;
        };

        const Record* FindTyped(Identity key, GameStateValueType type) const;
        Record* FindTyped(Identity key, GameStateValueType type);
        bool CheckPersistent(const Container::String& name, GameStateValueType type) const;
        void NotifyChanged(const Container::String& name);

        Container::VariableArray<Record> m_Records;
        Container::UnorderedMap<uint64_t, size_t> m_Indices;
        MulticastDelegate<Identity> m_OnValueChanged;
        uint64_t m_LedgerRevision = 0;
    };
}
