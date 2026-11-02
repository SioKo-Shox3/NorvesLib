#include "VfxEffectCookCli.h"

#include "CookManagedTransactionController.h"
#include "NativeCookArguments.h"
#include "Particle/VfxEffectSerialization.h"
#include "Text/JsonUnicodeScalar.h"
#include "Text/UnicodeText.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <Windows.h>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace Container = Core::Container;
        namespace Particle = Core::Particle;
        namespace Text = Core::TextDetail;

        struct TemporaryFile
        {
            std::filesystem::path Path;
            HANDLE Handle = INVALID_HANDLE_VALUE;
            bool bCreated = false;

            ~TemporaryFile()
            {
                if (Handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(Handle);
                }
                if (bCreated)
                {
                    std::error_code error;
                    std::filesystem::remove(Path, error);
                }
            }
        };

        std::filesystem::path Utf8Path(const Container::AnsiString& value)
        {
            Container::VariableArray<char8_t> units;
            units.reserve(value.size());
            for (const char byte : value)
            {
                units.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
            }
            if (units.empty())
            {
                return {};
            }
            return std::filesystem::path(units.begin(), units.end());
        }

        bool ReadUtf8File(const std::filesystem::path& path, Container::String& outText)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                std::fputs("VFX の入力ファイルを開けません\n", stderr);
                return false;
            }
            input.seekg(0, std::ios::end);
            const std::streamoff length = input.tellg();
            if (length < 0 || static_cast<uint64_t>(length) > std::numeric_limits<size_t>::max() ||
                static_cast<uint64_t>(length) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
            {
                std::fputs("VFX の入力ファイルのサイズを取得できません\n", stderr);
                return false;
            }
            Container::VariableArray<uint8_t> bytes;
            bytes.resize(static_cast<size_t>(length));
            input.seekg(0, std::ios::beg);
            if (!input || (!bytes.empty() &&
                (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
                 input.gcount() != static_cast<std::streamsize>(bytes.size()))))
            {
                std::fputs("VFX の入力ファイルを読み取れません\n", stderr);
                return false;
            }

            size_t offset = 0;
            if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
            {
                offset = 3;
            }
            outText.reserve(bytes.size() - offset);
            const uint8_t* data = bytes.empty() ? nullptr : bytes.data() + offset;
            if (!Text::ForEachUnicodeScalar<uint8_t>({data, bytes.size() - offset}, [&](uint32_t scalar)
                {
                    const auto encoded = Text::EncodeJsonUnicodeScalar<Container::String::value_type>(scalar);
                    outText.append(encoded.Units, encoded.Count);
                }))
            {
                std::fputs("VFX の入力ファイルの UTF-8 が不正です\n", stderr);
                return false;
            }
            return true;
        }

        void PrintVfxError(const Container::String& error)
        {
            Container::AnsiString utf8;
            const bool bValid = Text::ForEachUnicodeScalar<Container::String::value_type>(
                {error.data(), error.size()}, [&](uint32_t scalar)
                {
                    const auto encoded = Text::EncodeJsonUnicodeScalar<char>(scalar);
                    utf8.append(encoded.Units, encoded.Count);
                });
            if (bValid)
            {
                std::fwrite(utf8.data(), 1, utf8.size(), stderr);
                std::fputc('\n', stderr);
            }
            else
            {
                std::fputs("VFX のエラーを表示できません\n", stderr);
            }
        }

        bool WriteCookedFile(const std::filesystem::path& destination, const Container::VariableArray<uint8_t>& bytes)
        {
            const std::filesystem::path parent = destination.parent_path();
            if (!parent.empty())
            {
                std::error_code error;
                std::filesystem::create_directories(parent, error);
                if (error)
                {
                    std::fputs("VFX の出力先ディレクトリを作成できません\n", stderr);
                    return false;
                }
            }

            Container::AnsiString token;
            Container::AnsiString tokenError;
            if (!Detail::ManagedTransaction::Token(token, tokenError))
            {
                std::fputs("VFX の一時ファイル名を作成できません\n", stderr);
                return false;
            }
            TemporaryFile temporary;
            temporary.Path = destination;
            temporary.Path += ".tmp-";
            temporary.Path += token.c_str();
            temporary.Handle = CreateFileW(temporary.Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            if (temporary.Handle == INVALID_HANDLE_VALUE)
            {
                std::fputs("VFX の一時ファイルを開けません\n", stderr);
                return false;
            }
            temporary.bCreated = true;
            size_t offset = 0;
            while (offset < bytes.size())
            {
                const size_t remaining = bytes.size() - offset;
                const DWORD request = static_cast<DWORD>(remaining > 0x40000000u ? 0x40000000u : remaining);
                DWORD written = 0;
                if (!WriteFile(temporary.Handle, bytes.data() + offset, request, &written, nullptr) || written == 0)
                {
                    std::fputs("VFX の一時ファイルを書き込めません\n", stderr);
                    return false;
                }
                offset += written;
            }
            if (!FlushFileBuffers(temporary.Handle))
            {
                std::fputs("VFX の一時ファイルの書き込みを完了できません\n", stderr);
                return false;
            }
            const bool bClosed = CloseHandle(temporary.Handle) != 0;
            temporary.Handle = INVALID_HANDLE_VALUE;
            if (!bClosed || !MoveFileExW(temporary.Path.c_str(), destination.c_str(),
                                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::fputs("VFX の出力ファイルを置き換えられません\n", stderr);
                return false;
            }
            temporary.bCreated = false;
            return true;
        }
    } // namespace

    bool RunVfxEffectCookCommand(int argc, char** argv, int& outExitCode)
    {
        if (argc < 2 || argv == nullptr || argv[1] == nullptr || std::strcmp(argv[1], "--cook-vfx") != 0)
        {
            return false;
        }
        if (argc != 4 || argv[2] == nullptr || argv[3] == nullptr)
        {
            std::fputs("使い方: AssetCook --cook-vfx <入力.vfx.json> <出力.nvfx>\n", stderr);
            outExitCode = 2;
            return true;
        }
        outExitCode = 1;
        try
        {
            Container::VariableArray<Container::AnsiString> arguments;
            Container::AnsiString argumentError;
            if (!Detail::CollectNativeCookArguments(argc, argv, arguments, argumentError) ||
                arguments.size() != 4 || arguments[1] != "--cook-vfx")
            {
                std::fprintf(stderr, "VFX のコマンド引数を読み取れません: %s\n", argumentError.c_str());
                return true;
            }
            const auto input = Utf8Path(arguments[2]);
            const auto output = Utf8Path(arguments[3]);
            Container::String json;
            if (!ReadUtf8File(input, json))
            {
                return true;
            }
            Particle::VfxEffectAsset asset;
            Container::String error;
            Container::VariableArray<uint8_t> bytes;
            if (!Particle::LoadVfxEffectFromJson(json, asset, error) || !Particle::CookVfxEffect(asset, bytes, error))
            {
                PrintVfxError(error);
                return true;
            }
            if (!WriteCookedFile(output, bytes))
            {
                return true;
            }
            std::printf("vfx_cooked bytes=%zu emitters=%zu\n", bytes.size(), asset.Emitters.size());
            outExitCode = 0;
        }
        catch (...)
        {
            std::fputs("VFX のクックを完了できません\n", stderr);
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
