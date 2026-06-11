#include "Platform/PlatformProcess.h"

#include <Windows.h>
#include <tchar.h>

namespace NorvesLib::Core::Platform
{

    Container::String GetExecutablePath()
    {
        TCHAR buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileName(NULL, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return Container::String{};
        }
        return Container::String(buffer);
    }

    bool SetWorkingDirectory(const Container::String& path)
    {
        return SetCurrentDirectory(path.c_str()) != 0;
    }

    Container::String GetExecutableDirectory()
    {
        Container::String execPath = GetExecutablePath();
        if (execPath.empty())
        {
            return Container::String{};
        }

        // バックスラッシュを優先して検索し、なければスラッシュで試みる
        Container::String::size_type pos = execPath.FindLast(TEXT('\\'));
        if (pos == Container::String::npos)
        {
            pos = execPath.FindLast(TEXT('/'));
        }
        if (pos == Container::String::npos)
        {
            return Container::String{};
        }
        return execPath.Substring(0, pos);
    }

} // namespace NorvesLib::Core::Platform
