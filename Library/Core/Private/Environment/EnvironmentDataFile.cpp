// 環境データのファイルサイズと完全読み込みを確認し、UTF-8 BOM を取り除く。
#include "EnvironmentDataFile.h"

#include <filesystem>
#include <fstream>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        bool FileError(Container::String* pOutError, const TCHAR* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            return false;
        }
    } // 無名名前空間

    bool ReadEnvironmentDataFile(const Container::String& rootDirectory, const Container::String& relativePath,
                                 Container::VariableArray<uint8_t>& outBytes, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        try
        {
            const std::filesystem::path filePath = rootDirectory.empty() ? std::filesystem::path(relativePath.c_str()) :
                std::filesystem::path(rootDirectory.c_str()) / std::filesystem::path(relativePath.c_str());
            std::error_code error;
            if (!std::filesystem::is_regular_file(filePath, error) || error)
            {
                return FileError(pOutError, _T("環境データのファイルが見つからないか、通常のファイルではありません"));
            }
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return FileError(pOutError, _T("環境データのファイルを開けません"));
            }
            const auto length = file.tellg();
            if (length <= 0 || length > 1048576)
            {
                return FileError(pOutError, _T("環境データのファイルの大きさは 1～1048576 バイトが必要です"));
            }
            Container::VariableArray<uint8_t> bytes(static_cast<size_t>(length), 0);
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
                file.peek() != std::char_traits<char>::eof())
            {
                return FileError(pOutError, _T("環境データのファイルを最後まで読めません"));
            }
            if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
            {
                bytes.erase(bytes.begin(), bytes.begin() + 3);
            }
            outBytes.swap(bytes);
            return true;
        }
        catch (...)
        {
            return FileError(pOutError, _T("環境データのファイルの読み込み中に例外が発生しました"));
        }
    }
} // 名前空間 NorvesLib::Core::Environment
