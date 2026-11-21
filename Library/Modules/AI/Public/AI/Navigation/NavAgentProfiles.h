#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::AI
{
    /// 経路照会用のプロファイルを定義順に保持する。
    class NavAgentProfileSet
    {
    public:
        /// Small・Medium・Large の組み込み値で初期化する。
        NavAgentProfileSet();
        static NavAgentProfileSet MakeDefaults();

        /// 全件が有効なら置き換える。失敗時は現在の定義を保つ。
        bool LoadFromJson(const Core::Container::String& text);

        /// 大文字小文字を区別して検索する。該当する名前がなければ nullptr を返す。
        /// 返すポインタと GetAt の参照は、読み込みに成功すると失効する。
        const NavAgentProfile* Find(const char* name) const;
        uint32_t GetCount() const;

        /// index は GetCount() 未満であること。
        const NavAgentProfile& GetAt(uint32_t index) const;

    private:
        Core::Container::VariableArray<NavAgentProfile> m_Profiles;
    };
} // namespace NorvesLib::Modules::AI
