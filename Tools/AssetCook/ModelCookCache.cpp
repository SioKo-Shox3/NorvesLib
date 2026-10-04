#include "ModelCookCache.h"
#include "Asset/AssetManifest.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "FileStream/Package.h"
#include <charconv>
#include <fstream>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Core::Asset;
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        using Core::Container::VariableArray;

        bool ReadCacheFile(const std::filesystem::path& path, VariableArray<uint8_t>& bytes)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path,error) || error)
            {
                return false;
            }
            std::ifstream input(path,std::ios::binary|std::ios::ate);
            const auto length=input.tellg();
            if (!input || length<0 || static_cast<uintmax_t>(length)>std::numeric_limits<size_t>::max() ||
                static_cast<uintmax_t>(length)>static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                return false;
            }
            bytes.resize(static_cast<size_t>(length));
            input.seekg(0,std::ios::beg);
            if (!bytes.empty())
            {
                input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
                if (input.gcount()!=static_cast<std::streamsize>(bytes.size()))
                {
                    return false;
                }
            }
            return input.peek()==std::char_traits<char>::eof() && !input.bad();
        }

        bool MatchesCacheEntry(const AssetManifest& manifest, const std::filesystem::path& manifestPath,
            const std::filesystem::path& expectedPackage, AnsiStringView logicalPath, AssetKind kind,
            AnsiStringView variant, AnsiStringView format, AnsiStringView entryName,
            AssetPackageFourCC entryType, uint64_t sourceHash)
        {
            const auto resolved=manifest.Resolve(logicalPath,kind,variant);
            if (!resolved.ShouldUseCooked())
            {
                return false;
            }
            const auto& reference=resolved.Reference;
            if (reference.SourceHash!=sourceHash || AnsiStringView(reference.Format)!=format ||
                AnsiStringView(reference.EntryName)!=entryName || reference.EntryType!=entryType || reference.CookedVersion!=0)
            {
                return false;
            }
            const auto recordedPath=manifestPath.parent_path()/
                std::filesystem::path(reference.CookedPackage.begin(),reference.CookedPackage.end());
            // 新しい出力先を要求された場合は、内容が同じでもmanifestを更新する必要がある。
            if (recordedPath.lexically_normal()!=expectedPackage.lexically_normal())
            {
                return false;
            }
            VariableArray<uint8_t> bytes;
            NorvesLib::FileStream::Package package;
            NorvesLib::FileStream::PackageEntry entry;
            if (!ReadCacheFile(expectedPackage,bytes) || !package.LoadFromMemory({bytes.data(),bytes.size()}) ||
                package.GetFormat()!=NorvesLib::FileStream::PackageFormat::V1 ||
                !package.FindEntry(AnsiString(entryName),entryType,entry) || entry.PayloadHash!=reference.CookedHash)
            {
                return false;
            }
            const auto payload=package.OpenEntry(entry);
            if (!payload.IsValid() || ComputeAssetPackagePayloadHash(payload.GetData(),payload.GetSize())!=reference.CookedHash)
            {
                return false;
            }
            if (entryType==MakeAssetPackageFourCC('M','s','h','0'))
            {
                return ParseCookedMesh(payload).Succeeded();
            }
            if (entryType==CookedSkeletalFormatV0::EntryType)
            {
                const auto skeletal = ParseCookedSkeletal(payload);
                return skeletal.Succeeded() && skeletal.Data.VersionMinor == CookedSkeletalFormatV02::VersionMinor;
            }
            const auto texture=ParseCookedTexture(payload);
            if (!texture.Succeeded())
            {
                return false;
            }
            const auto color=format==AnsiStringView("nvtex.v0.rgba8.srgb") ?
                CookedTextureColorSpace::SRGB : CookedTextureColorSpace::Linear;
            return texture.Texture.PixelFormat==CookedTexturePixelFormat::RGBA8UNorm && texture.Texture.ColorSpace==color;
        }
    } // namespace

    bool IsModelCookCacheCurrent(const std::filesystem::path& manifestPath,
        const std::filesystem::path& packagePath, Core::Container::AnsiStringView logicalPath,
        Core::Container::AnsiStringView variant, Core::Container::AnsiStringView format,
        Core::Container::AnsiStringView entryName, const ModelCookFingerprint& fingerprint)
    {
        if (!manifestPath.is_absolute() || !packagePath.is_absolute() ||
            (!IsSupportedMeshCookFormat(format) && !IsSupportedSkeletalCookFormat(format)))
        {
            return false;
        }
        VariableArray<uint8_t> bytes;
        if (!ReadCacheFile(manifestPath,bytes))
        {
            return false;
        }
        Core::Container::String text;
        text.reserve(bytes.size());
        for (const uint8_t value : bytes)
        {
            text.push_back(static_cast<Core::Container::String::value_type>(value));
        }
        AssetManifest manifest;
        if (!manifest.LoadFromJsonText(text))
        {
            return false;
        }
        const auto type=IsSupportedMeshCookFormat(format) ? MakeAssetPackageFourCC('M','s','h','0') : CookedSkeletalFormatV0::EntryType;
        if (!MatchesCacheEntry(manifest,manifestPath,packagePath,logicalPath,AssetKind::Model,
            variant,format,entryName,type,fingerprint.SourceHash))
        {
            return false;
        }
        for (const auto& image : fingerprint.EmbeddedImages)
        {
            char number[32]={};
            const auto converted=std::to_chars(number,number+sizeof(number),image.ImageIndex);
            if (converted.ec!=std::errc{})
            {
                return false;
            }
            AnsiString filename(packagePath.filename().generic_string().c_str());
            filename+=".img";
            filename.append(number,static_cast<size_t>(converted.ptr-number));
            filename+=".nvpkg";
            const auto imagePackage=packagePath.parent_path()/std::filesystem::path(filename.begin(),filename.end());
            if (!MatchesCacheEntry(manifest,manifestPath,imagePackage,image.LogicalPath,AssetKind::Texture,
                "default",image.Format,"__texture__",MakeAssetPackageFourCC('T','e','x','0'),image.SourceHash))
            {
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
