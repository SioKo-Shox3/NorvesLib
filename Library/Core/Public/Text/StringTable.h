// 表示用の文字列とフォントに必要な文字集合を言語ごとに管理する。
#pragma once

#include "Container/Span.h"
#include "Debug/DebugConfig.h"
#include "Delegate/MulticastDelegate.h"
#include "Text/IdentityPool.h"
#if NORVES_BUILD_DEVELOPMENT
#include "Container/UnorderedSet.h"
#include "Thread/Mutex.h"
#endif

namespace NorvesLib::Core::Asset
{
    class AssetFileReader;
}

namespace NorvesLib::Core
{
    /// 名前が空の引数には、位置引数だけを数えて 0 から番号を割り当てる。
    struct TextFormatArg
    {
        Container::AnsiStringView Name;
        Container::AnsiStringView Value;
    };

    /**
     * @brief GameThread から利用する UTF-8 の文字列表。
     * 読み込みと使用言語の選択は独立している。警告済みキーの集合だけをロックする。
     */
    class StringTable
    {
    public:
        /// 指定言語の表を読み込む。再読み込みに失敗した場合は以前の表を保つ。
        bool LoadLanguage(Container::AnsiStringView language, const Asset::AssetFileReader& reader);
        bool SetActiveLanguage(Container::AnsiStringView language);
        Container::AnsiStringView GetActiveLanguage() const { return m_ActiveLanguage; }
        MulticastDelegate<Container::AnsiStringView> OnLanguageChanged;

        /// 返す翻訳は、その言語の再読み込みまたは表の破棄まで有効。
        /// 未翻訳なら開発時はキーを返し、製品では指定された代替言語も調べる。
        Container::AnsiStringView Get(Identity key) const;
        static Container::AnsiString Format(Container::AnsiStringView pattern, Container::Span<const TextFormatArg> args);
        static Container::AnsiString Format(const char* pattern, Container::Span<const TextFormatArg> args)
        {
            return Format(Container::AnsiStringView(pattern), args);
        }
        Container::AnsiString Format(Identity key, Container::Span<const TextFormatArg> args) const;

        /// 全言語の対象キーの訳文と印字可能な ASCII から、昇順で重複のない集合を作る。
        void CollectCodepoints(Container::VariableArray<uint32_t>& out, Container::AnsiStringView keyPrefix = {}) const;

    private:
        struct Translation
        {
            Container::AnsiString KeyName;
            Container::AnsiString Value;
        };

        struct LanguageTable
        {
            Container::AnsiString Fallback;
            Container::UnorderedMap<uint64_t, Translation> Strings;
        };

        struct LanguageHash
        {
            size_t operator()(const Container::AnsiString& language) const noexcept
            {
                return static_cast<size_t>(Fnv1aRaw(language.data(), language.size()));
            }
        };

        Container::UnorderedMap<Container::AnsiString, LanguageTable, LanguageHash> m_Languages;
        Container::AnsiString m_ActiveLanguage;
#if NORVES_BUILD_DEVELOPMENT
        mutable Container::UnorderedSet<uint64_t> m_WarnedKeys;
        mutable Thread::Mutex m_WarningMutex;
#endif
    };
}
