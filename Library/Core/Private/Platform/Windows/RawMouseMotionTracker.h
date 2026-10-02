#pragma once
#include <cstdint>
#include <cstddef>
namespace NorvesLib::Core::Platform
{
    struct RawMouseDelta { float X=0,Y=0; };
    struct RawMouseDesktop
    {
        int32_t Left=0,Top=0,Width=0,Height=0;
        bool Virtual=false;
        bool operator==(const RawMouseDesktop&) const = default;
    };
    /// native handleを非所有の識別値として保持する。満杯の履歴は再seedしjumpさせない。
    class RawMouseMotionTracker
    {
    public:
        static constexpr size_t Capacity=16;
        void Clear() noexcept
        {
            for(auto& entry:m_Entries) entry={};
            m_NextEviction=0;
        }
        void Forget(uintptr_t device) noexcept
        {
            for(auto& entry:m_Entries) if(entry.Used && entry.Device==device) entry={};
        }
        RawMouseDelta Relative(uintptr_t device,int32_t x,int32_t y) noexcept
        {
            // absolute→relative→absoluteの切替で以前の絶対座標を再利用しない。
            Forget(device);
            return {static_cast<float>(x),static_cast<float>(y)};
        }
        bool Absolute(uintptr_t device,int32_t x,int32_t y,const RawMouseDesktop& desktop,RawMouseDelta& out) noexcept
        {
            out={};
            if(x<0 || x>65535 || y<0 || y>65535 || desktop.Width<=0 || desktop.Height<=0) return false;
            Entry* found=nullptr;
            for(auto& entry:m_Entries) if(entry.Used && entry.Device==device) { found=&entry;break; }
            if(!found)
            {
                for(auto& entry:m_Entries) if(!entry.Used) { found=&entry;break; }
                if(!found) { found=&m_Entries[m_NextEviction];m_NextEviction=(m_NextEviction+1)%Capacity; }
                *found={true,device,x,y,desktop};return true;
            }
            if(found->Desktop==desktop)
            {
                // doubleで差分/換算し、pixelの端点0..(size-1)へ対応させる。
                out.X=static_cast<float>((static_cast<double>(x)-found->X)*(desktop.Width-1)/65535.0);
                out.Y=static_cast<float>((static_cast<double>(y)-found->Y)*(desktop.Height-1)/65535.0);
            }
            found->X=x;found->Y=y;found->Desktop=desktop;
            return true;
        }
    private:
        struct Entry
        {
            bool Used=false;
            uintptr_t Device=0;
            int32_t X=0,Y=0;
            RawMouseDesktop Desktop;
        };
        Entry m_Entries[Capacity]{};
        size_t m_NextEviction=0;
    };
}
