#include "Text/IdentityPool.h"
#include <functional>

namespace NorvesLib::Core
{
    // Identity クラスの実装
    
    Identity::Identity(const char* str)
    {
        if (str)
        {
            *this = IdentityPool::Get().CreateIdentity(str);
        }
    }

    Identity::Identity(const Container::String& str)
    {
        *this = IdentityPool::Get().CreateIdentity(str);
    }

    Identity::Identity(Container::StringView view)
    {
        *this = IdentityPool::Get().CreateIdentity(view);
    }

    Container::String Identity::ToString() const
    {
        return Container::String(m_StringView);
    }

    // IdentityPool クラスの実装

    IdentityPool& IdentityPool::Get()
    {
        static IdentityPool instance;
        return instance;
    }

    Identity IdentityPool::CreateIdentity(const char* str)
    {
        if (!str)
        {
            return Identity();
        }
        return CreateIdentity(Container::StringView(str));
    }

    Identity IdentityPool::CreateIdentity(const Container::String& str)
    {
        return CreateIdentity(Container::StringView(str));
    }

    Identity IdentityPool::CreateIdentity(Container::StringView view)
    {
        if (view.empty())
        {
            return Identity();
        }

        uint64_t hash = CalculateHash(view);
        if (hash == 0)
        {
            // 0はInvalidとして予約されているため、ハッシュ値が0になる場合は1を加算
            hash = 1;
        }

        Container::StringView storedView;
        {
            Thread::ScopedLock lock(m_Mutex);
            auto it = m_StringMap.find(hash);
            if (it == m_StringMap.end())
            {
                // 新しい文字列を登録
                auto result = m_StringMap.emplace(hash, Container::String(view));
                storedView = Container::StringView(result.first->second);
            }
            else
            {
                // 既存の文字列を参照
                storedView = Container::StringView(it->second);
            }
        }

        return Identity(hash, storedView);
    }

    Identity IdentityPool::GetIdentity(uint64_t hash) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_StringMap.find(hash);
        if (it != m_StringMap.end())
        {
            return Identity(hash, Container::StringView(it->second));
        }
        return Identity();
    }

    Container::StringView IdentityPool::GetStringFromHash(uint64_t hash) const
    {
        if (hash == 0)
        {
            return Container::StringView();
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_StringMap.find(hash);
        if (it != m_StringMap.end())
        {
            return Container::StringView(it->second);
        }
        return Container::StringView();
    }

    size_t IdentityPool::GetIdentityCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        return m_StringMap.size();
    }

    Container::Vector<Container::StringView> IdentityPool::GetAllStrings() const
    {
        Container::Vector<Container::StringView> result;
        
        Thread::ScopedLock lock(m_Mutex);
        result.reserve(m_StringMap.size());
        for (const auto& pair : m_StringMap)
        {
            result.emplace_back(Container::StringView(pair.second));
        }
        
        return result;
    }

    uint64_t IdentityPool::CalculateHash(Container::StringView view) const
    {
        // FNV-1a ハッシュアルゴリズムを使用
        constexpr uint64_t FNV_PRIME = 1099511628211ULL;
        constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

        uint64_t hash = FNV_OFFSET_BASIS;
        for (char c : view)
        {
            hash ^= static_cast<uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }
} // namespace NorvesLib::Core
