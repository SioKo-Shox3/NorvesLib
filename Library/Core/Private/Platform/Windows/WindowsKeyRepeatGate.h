#pragma once
#include <cstdint>
namespace NorvesLib::Core::Platform
{
    /// focus獲得後に実際のkeydownを受けたVKだけrepeat/文字変換を許可する。
    /// エンジンのKeyCodeに未登録のIME/OEMキーもnative VKとして追跡する。
    class WindowsKeyRepeatGate
    {
    public:
        bool CanTranslate(uint32_t key,bool repeat) const noexcept
        {
            return key<256 && (!repeat || m_Down[key]);
        }
        bool AcceptPress(uint32_t key,bool repeat) noexcept
        {
            if(!CanTranslate(key,repeat)) return false;
            m_Down[key]=true;return true;
        }
        void Release(uint32_t key) noexcept { if(key<256) m_Down[key]=false; }
        void Clear() noexcept { for(bool& down:m_Down) down=false; }
    private:
        bool m_Down[256]{};
    };
}
