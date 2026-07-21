#include "CoreTypes.h"
#include "Text/JsonDocument.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <Windows.h>
#include <bcrypt.h>

namespace
{
    using NorvesLib::Core::Container::AnsiString;
    using NorvesLib::Core::Container::Set;
    using NorvesLib::Core::Container::String;
    using NorvesLib::Core::Container::VariableArray;
    using NorvesLib::Core::JsonDocument;
    using NorvesLib::Core::JsonValue;

    bool Check(bool bCondition, const char* expression, int line)
    {
        if (!bCondition)
        {
            std::cout << "CHECK failed at line " << line << ": " << expression << "\n";
        }
        return bCondition;
    }

#define CHECK_EXPRESSION(expression) \
    do \
    { \
        if (!Check((expression), #expression, __LINE__)) \
        { \
            return false; \
        } \
    } while (false)

    bool ReadFileBytes(const std::filesystem::path& path, VariableArray<uint8_t>& outBytes)
    {
        outBytes.clear();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
            static_cast<uint64_t>(size.QuadPart) > static_cast<uint64_t>(outBytes.max_size()))
        {
            CloseHandle(file);
            return false;
        }

        outBytes.resize(static_cast<size_t>(size.QuadPart));
        size_t offset = 0;
        while (offset < outBytes.size())
        {
            const size_t remaining = outBytes.size() - offset;
            const DWORD requested = remaining > static_cast<size_t>(MAXDWORD)
                ? MAXDWORD
                : static_cast<DWORD>(remaining);
            DWORD bytesRead = 0;
            if (!ReadFile(file, outBytes.data() + offset, requested, &bytesRead, nullptr) || bytesRead == 0)
            {
                CloseHandle(file);
                outBytes.clear();
                return false;
            }
            offset += bytesRead;
        }

        CloseHandle(file);
        return true;
    }

    bool ReadTextFile(const std::filesystem::path& path, AnsiString& outText)
    {
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(path, bytes))
        {
            return false;
        }

        outText.clear();
        outText.reserve(bytes.size());
        for (uint8_t byte : bytes)
        {
            outText.push_back(static_cast<char>(byte));
        }
        return true;
    }

    String ToString(const AnsiString& text)
    {
        String result;
        result.reserve(text.size());
        for (char character : text)
        {
            result.push_back(static_cast<TCHAR>(static_cast<unsigned char>(character)));
        }
        return result;
    }

    String ToManifestPath(const std::filesystem::path& path)
    {
        const auto native = path.native();
        String result;
        result.reserve(native.size());
        for (const auto character : native)
        {
            result.push_back(character == '\\' ? _T('/') : static_cast<TCHAR>(character));
        }
        return result;
    }

    bool GetSha256(const std::filesystem::path& path, String& outHash)
    {
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(path, bytes))
        {
            return false;
        }

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hashHandle = nullptr;
        DWORD objectLength = 0;
        DWORD hashLength = 0;
        DWORD resultLength = 0;
        bool bSucceeded = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
        if (bSucceeded)
        {
            bSucceeded = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) >= 0;
        }
        if (bSucceeded)
        {
            bSucceeded = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) >= 0;
        }

        VariableArray<UCHAR> hashObject(objectLength);
        VariableArray<UCHAR> hash(hashLength);
        if (bSucceeded)
        {
            bSucceeded = BCryptCreateHash(algorithm, &hashHandle, hashObject.data(), objectLength,
                nullptr, 0, 0) >= 0;
        }
        if (bSucceeded && !bytes.empty())
        {
            bSucceeded = BCryptHashData(hashHandle, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0;
        }
        if (bSucceeded)
        {
            bSucceeded = BCryptFinishHash(hashHandle, hash.data(), hashLength, 0) >= 0;
        }

        if (hashHandle != nullptr)
        {
            BCryptDestroyHash(hashHandle);
        }
        if (algorithm != nullptr)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        if (!bSucceeded)
        {
            return false;
        }

        static constexpr TCHAR HexDigits[] = _T("0123456789ABCDEF");
        outHash.clear();
        outHash.reserve(hash.size() * 2);
        for (UCHAR value : hash)
        {
            outHash.push_back(HexDigits[value >> 4]);
            outHash.push_back(HexDigits[value & 0x0F]);
        }
        return true;
    }

    bool ContainsDefine(const AnsiString& text, const char* name, const char* value)
    {
        AnsiString expected("#define ");
        expected += name;
        const size_t definePosition = text.find(expected);
        if (definePosition == AnsiString::npos)
        {
            return false;
        }

        size_t position = definePosition + expected.size();
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
        {
            ++position;
        }
        const size_t valueLength = AnsiString(value).size();
        const size_t valueEnd = position + valueLength;
        return valueEnd <= text.size() &&
            text.substr(position, valueLength) == value &&
            (valueEnd == text.size() || std::isspace(static_cast<unsigned char>(text[valueEnd])) != 0);
    }

    bool IsIdentifierCharacter(char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    }

    bool ContainsAngelScriptInterfaceToken(const AnsiString& text)
    {
        for (size_t index = 0; index + 3 < text.size(); ++index)
        {
            if (text[index] != 'a' || text[index + 1] != 's' || text[index + 2] != 'I')
            {
                continue;
            }
            if (index > 0 && IsIdentifierCharacter(text[index - 1]))
            {
                continue;
            }
            if (IsIdentifierCharacter(text[index + 3]))
            {
                return true;
            }
        }
        return false;
    }

    bool VerifyPublicIsolation(const std::filesystem::path& sourceRoot)
    {
        const std::filesystem::path publicRoot = sourceRoot / "Library/Core/Public";
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(publicRoot))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            AnsiString contents;
            CHECK_EXPRESSION(ReadTextFile(entry.path(), contents));
            CHECK_EXPRESSION(contents.find("angelscript") == AnsiString::npos);
            CHECK_EXPRESSION(contents.find("asOBJ_") == AnsiString::npos);
            CHECK_EXPRESSION(contents.find("asCALL_") == AnsiString::npos);
            CHECK_EXPRESSION(!ContainsAngelScriptInterfaceToken(contents));
        }
        return true;
    }

    bool IsSelectedVendorPath(const String& path)
    {
        return path == _T("LICENSE") ||
            path.find(_T("upstream/sdk/angelscript/include/")) == 0 ||
            path.find(_T("upstream/sdk/angelscript/source/")) == 0;
    }

    bool VerifyManifest(const std::filesystem::path& vendorRoot, const JsonValue& root)
    {
        CHECK_EXPRESSION(root.IsObject());
        CHECK_EXPRESSION(root.FindMember("name").IsString());
        CHECK_EXPRESSION(root.FindMember("name").AsString() == _T("AngelScript"));
        CHECK_EXPRESSION(root.FindMember("version").IsString());
        CHECK_EXPRESSION(root.FindMember("version").AsString() == _T("2.38.0"));
        CHECK_EXPRESSION(root.FindMember("headerVersion").IsNumber());
        CHECK_EXPRESSION(root.FindMember("headerVersion").AsUInt32() == 23800);

        const JsonValue officialZip = root.FindMember("officialZip");
        CHECK_EXPRESSION(officialZip.IsObject());
        CHECK_EXPRESSION(officialZip.FindMember("url").AsString() ==
            _T("https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip"));
        CHECK_EXPRESSION(officialZip.FindMember("size").AsInt64() == 2060096);
        CHECK_EXPRESSION(officialZip.FindMember("sha256").AsString() ==
            _T("B33B5DBCDA10317EF67D628353D83246984CE6FCAC102D4DC2AED121EBA52E6F"));

        const JsonValue upstream = root.FindMember("upstream");
        CHECK_EXPRESSION(upstream.IsObject());
        CHECK_EXPRESSION(upstream.FindMember("tag").AsString() == _T("v2.38.0"));
        CHECK_EXPRESSION(upstream.FindMember("commit").AsString() ==
            _T("0601da029d846a658bf23f2888e953a45a94450a"));
        CHECK_EXPRESSION(upstream.FindMember("commitArchiveSha256").AsString() ==
            _T("0c2ed8bfa0bb3ace32efa842ac96ed605d6ad96bb35d8952a4e4c3acae8004bc"));

        const JsonValue license = root.FindMember("license");
        CHECK_EXPRESSION(license.IsObject());
        CHECK_EXPRESSION(license.FindMember("spdx").AsString() == _T("Zlib"));
        CHECK_EXPRESSION(license.FindMember("source").AsString() ==
            _T("sdk/angelscript/include/angelscript.h license preamble"));

        const JsonValue files = root.FindMember("files");
        CHECK_EXPRESSION(files.IsArray());
        Set<String> manifestPaths;
        for (size_t index = 0; index < files.GetArraySize(); ++index)
        {
            const JsonValue file = files.GetArrayElement(index);
            CHECK_EXPRESSION(file.IsObject());
            CHECK_EXPRESSION(file.FindMember("path").IsString());
            CHECK_EXPRESSION(file.FindMember("size").IsNumber());
            CHECK_EXPRESSION(file.FindMember("sha256").IsString());

            const String& relativePath = file.FindMember("path").AsString();
            CHECK_EXPRESSION(IsSelectedVendorPath(relativePath));
            CHECK_EXPRESSION(manifestPaths.insert(relativePath).second);
            const std::filesystem::path diskPath = vendorRoot / relativePath.data();
            CHECK_EXPRESSION(std::filesystem::is_regular_file(diskPath));
            CHECK_EXPRESSION(static_cast<uint64_t>(file.FindMember("size").AsInt64(-1)) ==
                static_cast<uint64_t>(std::filesystem::file_size(diskPath)));
            String hash;
            CHECK_EXPRESSION(GetSha256(diskPath, hash));
            CHECK_EXPRESSION(hash == file.FindMember("sha256").AsString());
        }

        Set<String> diskPaths;
        const std::filesystem::path upstreamRoot = vendorRoot / "upstream";
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(upstreamRoot))
        {
            if (entry.is_regular_file())
            {
                diskPaths.insert(ToManifestPath(entry.path().lexically_relative(vendorRoot)));
            }
        }
        CHECK_EXPRESSION(std::filesystem::is_regular_file(vendorRoot / "LICENSE"));
        diskPaths.insert(_T("LICENSE"));

        CHECK_EXPRESSION(manifestPaths.size() == diskPaths.size());
        for (const String& path : manifestPaths)
        {
            CHECK_EXPRESSION(diskPaths.find(path) != diskPaths.end());
        }
        for (const String& path : diskPaths)
        {
            CHECK_EXPRESSION(manifestPaths.find(path) != manifestPaths.end());
        }
        CHECK_EXPRESSION(manifestPaths.size() == 73);
        return true;
    }

    bool RunTest()
    {
        const std::filesystem::path sourceRoot = std::filesystem::path(NORVES_SOURCE_ROOT);
        const std::filesystem::path vendorRoot = sourceRoot / "Library/ThirdParty/angelscript";

        AnsiString manifestText;
        CHECK_EXPRESSION(ReadTextFile(vendorRoot / "UPSTREAM.json", manifestText));
        JsonDocument manifest;
        String parseError;
        CHECK_EXPRESSION(JsonDocument::TryParse(ToString(manifestText), manifest, &parseError));
        CHECK_EXPRESSION(VerifyManifest(vendorRoot, manifest.GetRoot()));

        AnsiString header;
        CHECK_EXPRESSION(ReadTextFile(vendorRoot / "upstream/sdk/angelscript/include/angelscript.h", header));
        CHECK_EXPRESSION(ContainsDefine(header, "ANGELSCRIPT_VERSION", "23800"));
        CHECK_EXPRESSION(ContainsDefine(header, "ANGELSCRIPT_VERSION_STRING", "\"2.38.0\""));

        AnsiString license;
        CHECK_EXPRESSION(ReadTextFile(vendorRoot / "LICENSE", license));
        CHECK_EXPRESSION(license.find("Permission is granted") != AnsiString::npos);

        AnsiString wrapper;
        CHECK_EXPRESSION(ReadTextFile(vendorRoot / "CMakeLists.txt", wrapper));
        CHECK_EXPRESSION(wrapper.find("FetchContent") == AnsiString::npos);
        CHECK_EXPRESSION(wrapper.find("ExternalProject_Add") == AnsiString::npos);
        CHECK_EXPRESSION(wrapper.find("add_on") == AnsiString::npos);
        CHECK_EXPRESSION(VerifyPublicIsolation(sourceRoot));
        return true;
    }
}

int main()
{
    std::cout << "AngelScriptVendorContractTest start\n";
    const bool bPassed = RunTest();
    std::cout << (bPassed ? "AngelScriptVendorContractTest passed\n" :
        "AngelScriptVendorContractTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
