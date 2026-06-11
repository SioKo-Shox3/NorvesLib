#include "Rendering/TextureAssetLoader.h"

#include "Asset/AssetFileReader.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetResolveResult.h"
#include "Asset/AssetSystem.h"
#include "FileStream/FileStream.h"
#include "FileStream/Package.h"
#include "Logging/LogMacros.h"

#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;

        struct DecodedTextureMemory
        {
            TextureCreateInfo CreateInfo;
            Container::VariableArray<uint8_t> Pixels;
            int Width = 0;
            int Height = 0;
            int Channels = 0;
            double DecodeMs = 0.0;
            double CopyMs = 0.0;
            bool bSuccess = false;
        };

        struct TextureFileReadMemory
        {
            Container::VariableArray<uint8_t> Bytes;
            double ReadMs = 0.0;
            size_t FileBytes = 0;
            bool bSuccess = false;
        };

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }

        Container::String ToString(const Container::AnsiString &value)
        {
            return Container::String(value.c_str());
        }

        uint32_t CalculateFullMipCount(uint32_t width, uint32_t height)
        {
            uint32_t mipLevels = 1;
            while (width > 1 || height > 1)
            {
                width = std::max(1u, width / 2);
                height = std::max(1u, height / 2);
                ++mipLevels;
            }
            return mipLevels;
        }

        const char *GetTextureLoadSourceName(TextureLoadSource source)
        {
            switch (source)
            {
            case TextureLoadSource::CookedNvtex:
                return "cooked_nvtex";
            case TextureLoadSource::LooseStbi:
                return "loose_stbi";
            case TextureLoadSource::LegacyFile:
            default:
                return "legacy_file";
            }
        }

        const char *GetAssetResolveStatusName(Asset::AssetResolveStatus status)
        {
            switch (status)
            {
            case Asset::AssetResolveStatus::SuccessCooked:
                return "SuccessCooked";
            case Asset::AssetResolveStatus::SuccessLoose:
                return "SuccessLoose";
            case Asset::AssetResolveStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetResolveStatus::InvalidManifest:
                return "InvalidManifest";
            case Asset::AssetResolveStatus::LooseReadFailed:
                return "LooseReadFailed";
            case Asset::AssetResolveStatus::CookedPackageReadFailed:
                return "CookedPackageReadFailed";
            case Asset::AssetResolveStatus::CookedPackageParseFailed:
                return "CookedPackageParseFailed";
            case Asset::AssetResolveStatus::CookedEntryMissing:
                return "CookedEntryMissing";
            case Asset::AssetResolveStatus::CookedEntryHashMismatch:
                return "CookedEntryHashMismatch";
            default:
                return "Unknown";
            }
        }

        const char *GetAssetManifestResolveStatusName(Asset::AssetManifestResolveStatus status)
        {
            switch (status)
            {
            case Asset::AssetManifestResolveStatus::CookedReferenceFound:
                return "CookedReferenceFound";
            case Asset::AssetManifestResolveStatus::LooseFallbackManifestMissing:
                return "LooseFallbackManifestMissing";
            case Asset::AssetManifestResolveStatus::LooseFallbackVariantMissing:
                return "LooseFallbackVariantMissing";
            case Asset::AssetManifestResolveStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetManifestResolveStatus::InvalidManifest:
                return "InvalidManifest";
            default:
                return "Unknown";
            }
        }

        const char *GetAssetReadStatusName(Asset::AssetReadStatus status)
        {
            switch (status)
            {
            case Asset::AssetReadStatus::Success:
                return "Success";
            case Asset::AssetReadStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetReadStatus::InvalidAssetRoot:
                return "InvalidAssetRoot";
            case Asset::AssetReadStatus::InvalidPath:
                return "InvalidPath";
            case Asset::AssetReadStatus::FileNotFound:
                return "FileNotFound";
            case Asset::AssetReadStatus::OpenFailed:
                return "OpenFailed";
            case Asset::AssetReadStatus::SizeQueryFailed:
                return "SizeQueryFailed";
            case Asset::AssetReadStatus::SizeTooLarge:
                return "SizeTooLarge";
            case Asset::AssetReadStatus::ReadFailed:
                return "ReadFailed";
            default:
                return "Unknown";
            }
        }

        const char *GetPreparedTextureAssetStatusName(PreparedTextureAssetStatus status)
        {
            switch (status)
            {
            case PreparedTextureAssetStatus::InvalidRequest:
                return "InvalidRequest";
            case PreparedTextureAssetStatus::InvalidPath:
                return "InvalidPath";
            case PreparedTextureAssetStatus::AbsolutePathUnsupported:
                return "AbsolutePathUnsupported";
            case PreparedTextureAssetStatus::ManifestInvalid:
                return "ManifestInvalid";
            case PreparedTextureAssetStatus::ManifestMissingLooseFallback:
                return "ManifestMissingLooseFallback";
            case PreparedTextureAssetStatus::VariantMissingLooseFallback:
                return "VariantMissingLooseFallback";
            case PreparedTextureAssetStatus::CookedPackageReadFailed:
                return "CookedPackageReadFailed";
            case PreparedTextureAssetStatus::CookedPackageParseFailed:
                return "CookedPackageParseFailed";
            case PreparedTextureAssetStatus::CookedEntryMissing:
                return "CookedEntryMissing";
            case PreparedTextureAssetStatus::CookedEntryHashMismatch:
                return "CookedEntryHashMismatch";
            case PreparedTextureAssetStatus::CookedTextureParseFailed:
                return "CookedTextureParseFailed";
            case PreparedTextureAssetStatus::DebugLooseFallback:
                return "DebugLooseFallback";
            case PreparedTextureAssetStatus::CookedReady:
                return "CookedReady";
            default:
                return "Unknown";
            }
        }

        const char *GetCookedTextureParseStatusName(Asset::CookedTextureParseStatus status)
        {
            switch (status)
            {
            case Asset::CookedTextureParseStatus::Success:
                return "Success";
            case Asset::CookedTextureParseStatus::BadMagic:
                return "BadMagic";
            case Asset::CookedTextureParseStatus::PayloadHashMismatch:
                return "PayloadHashMismatch";
            case Asset::CookedTextureParseStatus::InvalidBlob:
                return "InvalidBlob";
            default:
                return "Failure";
            }
        }

        bool IsPreparedTextureAssetLooseFallbackStatus(PreparedTextureAssetStatus status)
        {
            return status == PreparedTextureAssetStatus::ManifestMissingLooseFallback ||
                   status == PreparedTextureAssetStatus::VariantMissingLooseFallback ||
                   status == PreparedTextureAssetStatus::DebugLooseFallback;
        }

        PreparedTextureAssetStatus ApplyPreparedCookedFailureFallback(
            PreparedTextureAssetStatus cookedFailureStatus,
            TextureAssetFallbackMode fallbackMode)
        {
            if (TextureAssetResolver::AllowsDebugLooseFallback(fallbackMode))
            {
                return PreparedTextureAssetStatus::DebugLooseFallback;
            }

            return cookedFailureStatus;
        }

        void SetPreparedTextureAssetStatus(PreparedTextureAsset &prepared,
                                           PreparedTextureAssetStatus status,
                                           const char *reason)
        {
            prepared.Status = status;
            prepared.Reason = reason != nullptr ? Container::String(reason) : Container::String();
            if (IsPreparedTextureAssetLooseFallbackStatus(status))
            {
                prepared.Source = TextureLoadSource::LooseStbi;
            }
            else if (status == PreparedTextureAssetStatus::CookedReady ||
                     status == PreparedTextureAssetStatus::CookedPackageReadFailed ||
                     status == PreparedTextureAssetStatus::CookedPackageParseFailed ||
                     status == PreparedTextureAssetStatus::CookedEntryMissing ||
                     status == PreparedTextureAssetStatus::CookedEntryHashMismatch ||
                     status == PreparedTextureAssetStatus::CookedTextureParseFailed)
            {
                prepared.Source = TextureLoadSource::CookedNvtex;
            }
        }

        const char *NormalizeProfileRole(const char *role, const char *fallback)
        {
            return (role != nullptr && role[0] != '\0') ? role : fallback;
        }

        TextureAssetFallbackMode ToTextureAssetFallbackMode(Asset::AssetFallbackMode mode)
        {
            return TextureAssetResolver::ToTextureAssetFallbackMode(mode);
        }

        TextureAssetCpuLoadResult MakeBaseResult(const TextureAssetLoadPlan &plan)
        {
            TextureAssetCpuLoadResult result;
            result.Path = plan.RequestPath;
            result.ResolvedPath = plan.ResolvedPath;
            result.CacheKey = plan.CacheKey;
            result.LogicalPath = plan.LogicalPath;
            result.AssetGeneration = plan.Generation;
            result.FallbackMode = ToTextureAssetFallbackMode(plan.FallbackMode);
            return result;
        }

        bool DecodeStbiBytes(const uint8_t *bytes,
                             size_t byteCount,
                             const Container::String &debugName,
                             bool bFullMipChain,
                             DecodedTextureMemory &outDecoded)
        {
            outDecoded = {};
            if (bytes == nullptr || byteCount == 0 || byteCount > static_cast<size_t>(INT_MAX))
            {
                return false;
            }

            auto decodeStartTime = LoadProfileNow();
            unsigned char *pixels = stbi_load_from_memory(
                bytes,
                static_cast<int>(byteCount),
                &outDecoded.Width,
                &outDecoded.Height,
                &outDecoded.Channels,
                4);
            outDecoded.DecodeMs = LoadProfileElapsedMs(decodeStartTime);
            if (!pixels || outDecoded.Width <= 0 || outDecoded.Height <= 0)
            {
                if (pixels)
                {
                    stbi_image_free(pixels);
                }
                return false;
            }

            const size_t pixelBytes = static_cast<size_t>(outDecoded.Width) *
                                      static_cast<size_t>(outDecoded.Height) *
                                      4u;
            auto copyStartTime = LoadProfileNow();
            outDecoded.Pixels.resize(pixelBytes);
            std::memcpy(outDecoded.Pixels.data(), pixels, pixelBytes);
            outDecoded.CopyMs = LoadProfileElapsedMs(copyStartTime);
            stbi_image_free(pixels);

            outDecoded.CreateInfo.Width = static_cast<uint32_t>(outDecoded.Width);
            outDecoded.CreateInfo.Height = static_cast<uint32_t>(outDecoded.Height);
            outDecoded.CreateInfo.MipLevels = bFullMipChain
                                                  ? CalculateFullMipCount(outDecoded.CreateInfo.Width, outDecoded.CreateInfo.Height)
                                                  : 1;
            outDecoded.CreateInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            outDecoded.CreateInfo.DebugName = debugName;
            outDecoded.bSuccess = true;
            return true;
        }

        bool DecodeStbiFromMemory(Asset::AssetBlob blob,
                                  const Container::String &debugName,
                                  bool bFullMipChain,
                                  DecodedTextureMemory &outDecoded)
        {
            if (!blob.IsValid())
            {
                outDecoded = {};
                return false;
            }

            return DecodeStbiBytes(blob.GetData(), blob.GetSize(), debugName, bFullMipChain, outDecoded);
        }

        TextureFileReadMemory ReadTextureFileBytes(const Container::String &resolvedPath)
        {
            TextureFileReadMemory result;
            auto readStartTime = LoadProfileNow();
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                resolvedPath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);

            if (!fileStream || !fileStream->IsOpen())
            {
                result.ReadMs = LoadProfileElapsedMs(readStartTime);
                return result;
            }

            const int64_t fileSize = fileStream->GetSize();
            if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(INT_MAX))
            {
                fileStream->Close();
                result.ReadMs = LoadProfileElapsedMs(readStartTime);
                return result;
            }

            result.Bytes.resize(static_cast<size_t>(fileSize));
            const size_t bytesRead = fileStream->Read(result.Bytes.data(), static_cast<size_t>(fileSize));
            fileStream->Close();
            result.ReadMs = LoadProfileElapsedMs(readStartTime);
            result.FileBytes = bytesRead;

            if (bytesRead != static_cast<size_t>(fileSize))
            {
                result.Bytes.clear();
                return result;
            }

            result.bSuccess = true;
            return result;
        }

        TextureAssetCpuLoadResult LoadStbiBlobForCaller(const TextureAssetLoadPlan &plan,
                                                        const Asset::AssetBlob &blob,
                                                        TextureLoadSource source,
                                                        bool bExplicitDebugFallback)
        {
            TextureAssetCpuLoadResult result = MakeBaseResult(plan);
            result.Source = source;

            DecodedTextureMemory decoded;
            const bool bDecoded = DecodeStbiFromMemory(blob, plan.RequestPath, true, decoded);
            const size_t pixelDataSize = decoded.Pixels.size();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_sync_stbi_memory role=caller source=%s path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d debug_fallback=%d success=%d",
                            GetTextureLoadSourceName(source),
                            plan.RequestPath.c_str(),
                            plan.LogicalPath.c_str(),
                            plan.ResolvedPath.c_str(),
                            blob.GetSize(),
                            decoded.DecodeMs,
                            decoded.CopyMs,
                            pixelDataSize,
                            decoded.Width,
                            decoded.Height,
                            decoded.Channels,
                            bExplicitDebugFallback ? 1 : 0,
                            bDecoded ? 1 : 0);
            if (!bDecoded)
            {
                return result;
            }

            result.CreateInfo = std::move(decoded.CreateInfo);
            result.PixelData = std::move(decoded.Pixels);
            result.PixelDataSize = result.PixelData.size();
            result.bSuccess = true;
            return result;
        }

        void LogAsyncTextureProfile(const TextureAssetCpuLoadResult &result,
                                    uint32_t requestId,
                                    double readMs,
                                    double resolveMs,
                                    double parseMs,
                                    size_t fileBytes,
                                    double decodeMs,
                                    double copyMs,
                                    size_t pixelBytes,
                                    int width,
                                    int height,
                                    int channels,
                                    uint32_t mipLevels,
                                    uint32_t layers)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_async_worker role=worker source=%s request_id=%u path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" read_ms=%.3f resolve_ms=%.3f parse_ms=%.3f file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d mip_levels=%u layers=%u success=%d",
                            GetTextureLoadSourceName(result.Source),
                            static_cast<unsigned int>(requestId),
                            result.Path.c_str(),
                            result.LogicalPath.c_str(),
                            result.ResolvedPath.c_str(),
                            readMs,
                            resolveMs,
                            parseMs,
                            fileBytes,
                            decodeMs,
                            copyMs,
                            pixelBytes,
                            width,
                            height,
                            channels,
                            mipLevels,
                            layers,
                            result.bSuccess ? 1 : 0);
        }

        void DecodeFileWithStbiForWorker(TextureAssetCpuLoadResult &result,
                                         uint32_t requestId,
                                         TextureLoadSource source,
                                         bool bExplicitDebugFallback,
                                         double resolveMs,
                                         double parseMs)
        {
            result.Source = source;
            double readMs = 0.0;
            double decodeMs = 0.0;
            double copyMs = 0.0;
            size_t fileBytes = 0;
            size_t pixelBytes = 0;
            int width = 0;
            int height = 0;
            int channels = 0;
            uint32_t mipLevels = 0;
            uint32_t layers = 0;

            const TextureFileReadMemory fileRead = ReadTextureFileBytes(result.ResolvedPath);
            readMs = fileRead.ReadMs;
            fileBytes = fileRead.FileBytes;
            if (!fileRead.bSuccess)
            {
                result.bSuccess = false;
                LogAsyncTextureProfile(result, requestId, readMs, resolveMs, parseMs, fileBytes, decodeMs, copyMs, pixelBytes, width, height, channels, mipLevels, layers);
                return;
            }

            DecodedTextureMemory decoded;
            const bool bDecoded = DecodeStbiBytes(
                fileRead.Bytes.data(),
                fileRead.Bytes.size(),
                result.Path,
                false,
                decoded);
            decodeMs = decoded.DecodeMs;
            copyMs = decoded.CopyMs;
            pixelBytes = decoded.Pixels.size();
            width = decoded.Width;
            height = decoded.Height;
            channels = decoded.Channels;
            mipLevels = decoded.CreateInfo.MipLevels;
            layers = decoded.CreateInfo.ArraySize;

            if (!bDecoded)
            {
                result.bSuccess = false;
                LogAsyncTextureProfile(result, requestId, readMs, resolveMs, parseMs, fileBytes, decodeMs, copyMs, pixelBytes, width, height, channels, mipLevels, layers);
                return;
            }

            if (bExplicitDebugFallback)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"worker cooked failure fallback\"",
                                result.Path.c_str(),
                                result.LogicalPath.c_str());
            }

            result.CreateInfo = std::move(decoded.CreateInfo);
            result.PixelData = std::move(decoded.Pixels);
            result.PixelDataSize = result.PixelData.size();
            result.bSuccess = true;
            LogAsyncTextureProfile(result, requestId, readMs, resolveMs, parseMs, fileBytes, decodeMs, copyMs, pixelBytes, width, height, channels, mipLevels, layers);
        }

        void DecodeLooseBlobWithStbiForWorker(TextureAssetCpuLoadResult &result,
                                              const Asset::AssetBlob &blob,
                                              uint32_t requestId,
                                              double resolveMs,
                                              TextureLoadSource source,
                                              bool bExplicitDebugFallback)
        {
            result.Source = source;
            const size_t fileBytes = blob.GetSize();

            DecodedTextureMemory decoded;
            const bool bDecoded = DecodeStbiFromMemory(blob, result.Path, false, decoded);
            const double decodeMs = decoded.DecodeMs;
            const double copyMs = decoded.CopyMs;
            const size_t pixelBytes = decoded.Pixels.size();
            const int width = decoded.Width;
            const int height = decoded.Height;
            const int channels = decoded.Channels;
            const uint32_t mipLevels = decoded.CreateInfo.MipLevels;
            const uint32_t layers = decoded.CreateInfo.ArraySize;

            if (bExplicitDebugFallback)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"asset system cooked failure fallback\"",
                                result.Path.c_str(),
                                result.LogicalPath.c_str());
            }

            if (!bDecoded)
            {
                result.bSuccess = false;
                LogAsyncTextureProfile(result, requestId, 0.0, resolveMs, 0.0, fileBytes, decodeMs, copyMs, pixelBytes, width, height, channels, mipLevels, layers);
                return;
            }

            result.CreateInfo = std::move(decoded.CreateInfo);
            result.PixelData = std::move(decoded.Pixels);
            result.PixelDataSize = result.PixelData.size();
            result.bSuccess = true;
            LogAsyncTextureProfile(result, requestId, 0.0, resolveMs, 0.0, fileBytes, decodeMs, copyMs, pixelBytes, width, height, channels, mipLevels, layers);
        }
    }

    TextureAssetStbiPixels::TextureAssetStbiPixels(unsigned char *pixels) noexcept
        : m_Pixels(pixels)
    {
    }

    TextureAssetStbiPixels::~TextureAssetStbiPixels()
    {
        Reset();
    }

    TextureAssetStbiPixels::TextureAssetStbiPixels(TextureAssetStbiPixels &&other) noexcept
        : m_Pixels(std::exchange(other.m_Pixels, nullptr))
    {
    }

    TextureAssetStbiPixels &TextureAssetStbiPixels::operator=(TextureAssetStbiPixels &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Pixels = std::exchange(other.m_Pixels, nullptr);
        }
        return *this;
    }

    void TextureAssetStbiPixels::Reset() noexcept
    {
        if (m_Pixels)
        {
            stbi_image_free(m_Pixels);
            m_Pixels = nullptr;
        }
    }

    TextureAssetCpuLoadResult TextureAssetLoader::LoadLooseFileForCaller(
        const TextureAssetLoadPlan &plan,
        TextureLoadSource source)
    {
        TextureAssetCpuLoadResult result = MakeBaseResult(plan);
        result.Source = source;

        int width = 0;
        int height = 0;
        int channels = 0;
        auto stbiStartTime = LoadProfileNow();
        unsigned char *pixels = stbi_load(plan.ResolvedPath.c_str(), &width, &height, &channels, 4);
        double stbiFileMs = LoadProfileElapsedMs(stbiStartTime);
        size_t pixelDataSize = pixels && width > 0 && height > 0
                                   ? static_cast<size_t>(width) * static_cast<size_t>(height) * 4u
                                   : 0;
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_sync_stbi_file role=caller source=%s path=\"%s\" resolved_path=\"%s\" width=%d height=%d channels=%d pixel_bytes=%zu ms=%.3f success=%d",
                        GetTextureLoadSourceName(source),
                        plan.RequestPath.c_str(),
                        plan.ResolvedPath.c_str(),
                        width,
                        height,
                        channels,
                        pixelDataSize,
                        stbiFileMs,
                        pixels ? 1 : 0);
        if (!pixels)
        {
            NORVES_LOG_ERROR("TextureResources", "Failed to load texture file: %s", plan.ResolvedPath.c_str());
            return result;
        }

        result.CreateInfo.Width = static_cast<uint32_t>(width);
        result.CreateInfo.Height = static_cast<uint32_t>(height);
        result.CreateInfo.MipLevels = CalculateFullMipCount(result.CreateInfo.Width, result.CreateInfo.Height);
        result.CreateInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        result.CreateInfo.DebugName = plan.RequestPath;
        result.DirectPixels = TextureAssetStbiPixels(pixels);
        result.PixelDataSize = pixelDataSize;
        result.bSuccess = true;
        return result;
    }

    TextureAssetCpuLoadResult TextureAssetLoader::LoadForCaller(const TextureAssetLoadPlan &plan)
    {
        if (!plan.bUseAssetSystem || !plan.AssetSystem)
        {
            return LoadLooseFileForCaller(plan, TextureLoadSource::LegacyFile);
        }

        TextureAssetCpuLoadResult result = MakeBaseResult(plan);
        auto resolveStartTime = LoadProfileNow();
        Asset::AssetResolveResult resolveResult = plan.AssetSystem->ResolveAsset(
            plan.LogicalPath,
            Asset::AssetKind::Texture,
            Asset::AssetManifest::DefaultVariant,
            plan.FallbackMode);
        const double resolveMs = LoadProfileElapsedMs(resolveStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_asset_resolve role=caller path=\"%s\" logical_path=\"%s\" source=%s resolve_ms=%.3f status=%s manifest_status=%u success=%d explicit_fallback=%d",
                        plan.RequestPath.c_str(),
                        plan.LogicalPath.c_str(),
                        resolveResult.UsedCooked() ? "cooked_nvtex" : (resolveResult.UsedLoose() ? "loose_stbi" : "none"),
                        resolveMs,
                        GetAssetResolveStatusName(resolveResult.Status),
                        static_cast<unsigned int>(resolveResult.ManifestStatus),
                        resolveResult.Succeeded() ? 1 : 0,
                        resolveResult.RequiresExplicitLog() ? 1 : 0);

        if (resolveResult.RequiresExplicitLog())
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_asset_debug_fallback role=caller source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"%s\"",
                            plan.RequestPath.c_str(),
                            plan.LogicalPath.c_str(),
                            resolveResult.FallbackDecision.Reason.c_str());
        }

        if (!resolveResult.Succeeded())
        {
            NORVES_LOG_ERROR("TextureResources", "Texture asset resolve failed: %s", resolveResult.Reason.c_str());
            return result;
        }

        if (resolveResult.UsedLoose())
        {
            return LoadStbiBlobForCaller(
                plan,
                resolveResult.Blob,
                TextureLoadSource::LooseStbi,
                resolveResult.Source == Asset::AssetResolveSource::DebugLooseFallback);
        }

        result.Source = TextureLoadSource::CookedNvtex;
        auto parseStartTime = LoadProfileNow();
        Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(resolveResult.Blob);
        const double parseMs = LoadProfileElapsedMs(parseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_cooked_parse role=caller source=cooked_nvtex path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                        plan.RequestPath.c_str(),
                        plan.LogicalPath.c_str(),
                        resolveResult.Blob.GetSize(),
                        parseMs,
                        GetCookedTextureParseStatusName(parseResult.Status),
                        parseResult.Succeeded() ? 1 : 0);

        if (!parseResult.Succeeded())
        {
            if (plan.FallbackMode == Asset::AssetFallbackMode::DebugAllowLooseFallback)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=caller source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture parse failed\"",
                                plan.RequestPath.c_str(),
                                plan.LogicalPath.c_str());
                return LoadLooseFileForCaller(plan, TextureLoadSource::LooseStbi);
            }

            return result;
        }

        result.CookedTexture = Container::MakeShared<CookedTextureAsyncPayload>();
        result.CookedTexture->Texture = std::move(parseResult.Texture);
        result.bSuccess = true;
        return result;
    }

    TextureAssetCpuLoadResult TextureAssetLoader::LoadForWorker(
        const TextureAssetLoadPlan &plan,
        uint32_t requestId)
    {
        TextureAssetCpuLoadResult result = MakeBaseResult(plan);

        if (!plan.bUseAssetSystem || !plan.AssetSystem)
        {
            DecodeFileWithStbiForWorker(result, requestId, TextureLoadSource::LegacyFile, false, 0.0, 0.0);
            return result;
        }

        auto resolveStartTime = LoadProfileNow();
        Asset::AssetResolveResult resolveResult = plan.AssetSystem->ResolveAsset(
            plan.LogicalPath,
            Asset::AssetKind::Texture,
            Asset::AssetManifest::DefaultVariant,
            plan.FallbackMode);
        const double resolveMs = LoadProfileElapsedMs(resolveStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_asset_resolve role=worker path=\"%s\" logical_path=\"%s\" source=%s resolve_ms=%.3f status=%s manifest_status=%u success=%d explicit_fallback=%d",
                        result.Path.c_str(),
                        result.LogicalPath.c_str(),
                        resolveResult.UsedCooked() ? "cooked_nvtex" : (resolveResult.UsedLoose() ? "loose_stbi" : "none"),
                        resolveMs,
                        GetAssetResolveStatusName(resolveResult.Status),
                        static_cast<unsigned int>(resolveResult.ManifestStatus),
                        resolveResult.Succeeded() ? 1 : 0,
                        resolveResult.RequiresExplicitLog() ? 1 : 0);

        if (!resolveResult.Succeeded())
        {
            if (resolveResult.RequiresExplicitLog())
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"%s\"",
                                result.Path.c_str(),
                                result.LogicalPath.c_str(),
                                resolveResult.FallbackDecision.Reason.c_str());
            }

            result.bSuccess = false;
            LogAsyncTextureProfile(result, requestId, 0.0, resolveMs, 0.0, 0, 0.0, 0.0, 0, 0, 0, 0, 0, 0);
            return result;
        }

        if (resolveResult.UsedLoose())
        {
            DecodeLooseBlobWithStbiForWorker(
                result,
                resolveResult.Blob,
                requestId,
                resolveMs,
                TextureLoadSource::LooseStbi,
                resolveResult.Source == Asset::AssetResolveSource::DebugLooseFallback);
            return result;
        }

        result.Source = TextureLoadSource::CookedNvtex;
        const size_t fileBytes = resolveResult.Blob.GetSize();
        auto parseStartTime = LoadProfileNow();
        Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(resolveResult.Blob);
        const double parseMs = LoadProfileElapsedMs(parseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_cooked_parse role=worker source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                        static_cast<unsigned int>(requestId),
                        result.Path.c_str(),
                        result.LogicalPath.c_str(),
                        resolveResult.Blob.GetSize(),
                        parseMs,
                        GetCookedTextureParseStatusName(parseResult.Status),
                        parseResult.Succeeded() ? 1 : 0);

        if (!parseResult.Succeeded())
        {
            if (TextureAssetResolver::AllowsDebugLooseFallback(result.FallbackMode))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture parse failed\"",
                                result.Path.c_str(),
                                result.LogicalPath.c_str());
                DecodeFileWithStbiForWorker(result, requestId, TextureLoadSource::LooseStbi, false, resolveMs, parseMs);
                return result;
            }

            result.bSuccess = false;
            LogAsyncTextureProfile(result, requestId, 0.0, resolveMs, parseMs, fileBytes, 0.0, 0.0, 0, 0, 0, 0, 0, 0);
            return result;
        }

        result.CookedTexture = Container::MakeShared<CookedTextureAsyncPayload>();
        result.CookedTexture->Texture = std::move(parseResult.Texture);
        const int width = static_cast<int>(result.CookedTexture->Texture.Width);
        const int height = static_cast<int>(result.CookedTexture->Texture.Height);
        const int channels = static_cast<int>(Asset::GetCookedTextureBytesPerPixel(result.CookedTexture->Texture.PixelFormat));
        const uint32_t mipLevels = result.CookedTexture->Texture.MipCount;
        const uint32_t layers = result.CookedTexture->Texture.LayerCount;
        result.bSuccess = true;
        LogAsyncTextureProfile(result, requestId, 0.0, resolveMs, parseMs, fileBytes, 0.0, 0.0, 0, width, height, channels, mipLevels, layers);
        return result;
    }

    TextureAssetLooseDecodeResult TextureAssetLoader::DecodeLooseFallbackForMainRender(
        const Container::String &path,
        const Container::AnsiString &logicalPath,
        const Container::String &resolvedPath)
    {
        TextureAssetLooseDecodeResult result;
        const TextureFileReadMemory fileRead = ReadTextureFileBytes(resolvedPath);
        DecodedTextureMemory decoded;
        bool bDecoded = false;
        if (fileRead.bSuccess)
        {
            bDecoded = DecodeStbiBytes(
                fileRead.Bytes.data(),
                fileRead.Bytes.size(),
                path,
                false,
                decoded);
        }

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_async_upload_fallback_decode role=main_render source=loose_stbi path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" read_ms=%.3f file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d success=%d",
                        path.c_str(),
                        logicalPath.c_str(),
                        resolvedPath.c_str(),
                        fileRead.ReadMs,
                        fileRead.FileBytes,
                        decoded.DecodeMs,
                        decoded.CopyMs,
                        decoded.Pixels.size(),
                        decoded.Width,
                        decoded.Height,
                        decoded.Channels,
                        bDecoded ? 1 : 0);

        if (!bDecoded)
        {
            return result;
        }

        result.CreateInfo = std::move(decoded.CreateInfo);
        result.PixelData = std::move(decoded.Pixels);
        result.bSuccess = true;
        return result;
    }

    PreparedTextureAsset TextureAssetLoader::PrepareForWorker(
        const PreparedTextureAssetPlan &plan,
        const char *role,
        uint32_t requestId)
    {
        PreparedTextureAssetPlan workingPlan = plan;
        const char *profileRole = NormalizeProfileRole(role, "worker");

        auto finish = [&](PreparedTextureAssetStatus status, const char *reason)
        {
            SetPreparedTextureAssetStatus(workingPlan.Prepared, status, reason);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_prepare_asset role=%s request_id=%u path=\"%s\" logical_path=\"%s\" cache_key=\"%s\" generation=%llu status=%s source=%s reason=\"%s\"",
                            profileRole,
                            static_cast<unsigned int>(requestId),
                            workingPlan.Prepared.RequestPath.c_str(),
                            workingPlan.Prepared.LogicalPath.c_str(),
                            workingPlan.Prepared.CacheKey.c_str(),
                            static_cast<unsigned long long>(workingPlan.Prepared.Generation),
                            GetPreparedTextureAssetStatusName(workingPlan.Prepared.Status),
                            GetTextureLoadSourceName(workingPlan.Prepared.Source),
                            workingPlan.Prepared.Reason.c_str());
            return workingPlan.Prepared;
        };

        if (!workingPlan.bReadyForManifest)
        {
            return finish(workingPlan.BlockedStatus, workingPlan.BlockedReason);
        }

        auto resolveStartTime = LoadProfileNow();
        const Asset::AssetManifestResolveResult manifestResult =
            TextureAssetResolver::FindPreparedCookedVariant(workingPlan);
        const double resolveMs = LoadProfileElapsedMs(resolveStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_manifest role=%s request_id=%u path=\"%s\" logical_path=\"%s\" resolve_ms=%.3f manifest_status=%s",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        workingPlan.Prepared.RequestPath.c_str(),
                        workingPlan.Prepared.LogicalPath.c_str(),
                        resolveMs,
                        GetAssetManifestResolveStatusName(manifestResult.Status));

        switch (manifestResult.Status)
        {
        case Asset::AssetManifestResolveStatus::CookedReferenceFound:
            break;
        case Asset::AssetManifestResolveStatus::LooseFallbackManifestMissing:
            return finish(PreparedTextureAssetStatus::ManifestMissingLooseFallback, "asset manifest is not loaded");
        case Asset::AssetManifestResolveStatus::LooseFallbackVariantMissing:
            return finish(PreparedTextureAssetStatus::VariantMissingLooseFallback, "asset manifest variant is missing");
        case Asset::AssetManifestResolveStatus::InvalidManifest:
            return finish(PreparedTextureAssetStatus::ManifestInvalid, "asset manifest is invalid");
        case Asset::AssetManifestResolveStatus::InvalidRequest:
        default:
            return finish(PreparedTextureAssetStatus::InvalidPath, "asset manifest request is invalid");
        }

        workingPlan.Prepared.Source = TextureLoadSource::CookedNvtex;

        Asset::AssetFileReader packageReader(workingPlan.AssetRoot);
        Asset::AssetReadRequest packageReadRequest;
        packageReadRequest.InputPath = manifestResult.Reference.CookedPackage;
        packageReadRequest.AssetRoot = {};
        packageReadRequest.bAllowAbsolutePath = false;

        auto packageReadStartTime = LoadProfileNow();
        const Asset::AssetReadResult packageRead = packageReader.Read(packageReadRequest);
        const double packageReadMs = LoadProfileElapsedMs(packageReadStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_package_read role=%s request_id=%u path=\"%s\" logical_path=\"%s\" package=\"%s\" bytes=%zu read_ms=%.3f status=%s success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        workingPlan.Prepared.RequestPath.c_str(),
                        workingPlan.Prepared.LogicalPath.c_str(),
                        manifestResult.Reference.CookedPackage.c_str(),
                        packageRead.BytesRead,
                        packageReadMs,
                        GetAssetReadStatusName(packageRead.Status),
                        packageRead.Succeeded() ? 1 : 0);

        if (!packageRead.Succeeded())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedPackageReadFailed,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked package read failed");
        }

        NorvesLib::FileStream::Package package;
        auto packageParseStartTime = LoadProfileNow();
        const bool bPackageParsed = package.LoadFromMemory(
            packageRead.Blob.GetSpan(),
            ToString(packageRead.Blob.GetSourcePath()));
        const double packageParseMs = LoadProfileElapsedMs(packageParseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_package_parse role=%s request_id=%u path=\"%s\" logical_path=\"%s\" package=\"%s\" parse_ms=%.3f success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        workingPlan.Prepared.RequestPath.c_str(),
                        workingPlan.Prepared.LogicalPath.c_str(),
                        manifestResult.Reference.CookedPackage.c_str(),
                        packageParseMs,
                        bPackageParsed ? 1 : 0);

        if (!bPackageParsed)
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedPackageParseFailed,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked package parse failed");
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(manifestResult.Reference.EntryName, manifestResult.Reference.EntryType, entry))
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryMissing,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked entry is missing");
        }

        Asset::AssetBlob cookedBlob = package.OpenEntry(entry);
        if (!cookedBlob.IsValid())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryMissing,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked entry open failed");
        }

        const uint64_t cookedHash = Asset::ComputeAssetPackagePayloadHash(cookedBlob.GetData(), cookedBlob.GetSize());
        if (cookedHash != manifestResult.Reference.CookedHash)
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryHashMismatch,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked entry hash mismatch");
        }

        const size_t cookedBlobBytes = cookedBlob.GetSize();
        auto parseStartTime = LoadProfileNow();
        Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(std::move(cookedBlob));
        const double parseMs = LoadProfileElapsedMs(parseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_cooked_parse role=%s source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        workingPlan.Prepared.RequestPath.c_str(),
                        workingPlan.Prepared.LogicalPath.c_str(),
                        cookedBlobBytes,
                        parseMs,
                        GetCookedTextureParseStatusName(parseResult.Status),
                        parseResult.Succeeded() ? 1 : 0);

        if (!parseResult.Succeeded())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedTextureParseFailed,
                workingPlan.Prepared.FallbackMode);
            return finish(status, "cooked texture parse failed");
        }

        workingPlan.Prepared.Payload = Container::MakeShared<CookedTextureAsyncPayload>();
        workingPlan.Prepared.Payload->Texture = std::move(parseResult.Texture);
        return finish(PreparedTextureAssetStatus::CookedReady, "");
    }

    bool TextureAssetLoader::TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
        const PreparedTextureAsset &prepared,
        PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
        Container::String *pOutReason,
        const char *role,
        uint32_t requestId)
    {
        const char *profileRole = NormalizeProfileRole(role, "worker");

        auto fail = [&](const char *reason)
        {
            outSplit = PreparedCookedTextureMip0RGBA8UNormLinearSplit();
            if (pOutReason != nullptr)
            {
                *pOutReason = reason != nullptr ? Container::String(reason) : Container::String();
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_prepared_split role=%s request_id=%u path=\"%s\" logical_path=\"%s\" success=0 reason=\"%s\"",
                            profileRole,
                            static_cast<unsigned int>(requestId),
                            prepared.RequestPath.c_str(),
                            prepared.LogicalPath.c_str(),
                            reason != nullptr ? reason : "");
            return false;
        };

        if (prepared.Status != PreparedTextureAssetStatus::CookedReady)
        {
            return fail("not cooked ready");
        }

        if (!prepared.Payload)
        {
            return fail("payload missing");
        }

        const Asset::CookedTextureData &texture = prepared.Payload->Texture;
        if (texture.LayerCount != 1)
        {
            return fail("unsupported layer count");
        }

        if (texture.PixelFormat != Asset::CookedTexturePixelFormat::RGBA8UNorm)
        {
            return fail("unsupported pixel format");
        }

        if (texture.ColorSpace != Asset::CookedTextureColorSpace::Linear)
        {
            return fail("unsupported color space");
        }

        if (texture.Width == 0 || texture.Height == 0 || texture.Mips.empty())
        {
            return fail("mip0 missing");
        }

        uint64_t pixelCount64 = 0;
        if (texture.Width > std::numeric_limits<uint64_t>::max() / texture.Height)
        {
            return fail("mip0 size overflow");
        }
        pixelCount64 = static_cast<uint64_t>(texture.Width) * static_cast<uint64_t>(texture.Height);
        if (pixelCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            pixelCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4u))
        {
            return fail("mip0 size overflow");
        }

        const size_t pixelCount = static_cast<size_t>(pixelCount64);
        const size_t expectedMipBytes = pixelCount * 4u;
        const Container::Span<const uint8_t> mip0 = texture.GetMipBytes(0);
        if (mip0.size() != expectedMipBytes)
        {
            return fail("mip0 size mismatch");
        }

        PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
        split.Width = texture.Width;
        split.Height = texture.Height;
        split.R.resize(pixelCount);
        split.G.resize(pixelCount);
        split.B.resize(pixelCount);
        split.A.resize(pixelCount);
        for (size_t index = 0; index < pixelCount; ++index)
        {
            const size_t baseIndex = index * 4u;
            split.R[index] = mip0[baseIndex + 0];
            split.G[index] = mip0[baseIndex + 1];
            split.B[index] = mip0[baseIndex + 2];
            split.A[index] = mip0[baseIndex + 3];
        }

        outSplit = std::move(split);
        if (pOutReason != nullptr)
        {
            pOutReason->clear();
        }

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepared_split role=%s request_id=%u path=\"%s\" logical_path=\"%s\" width=%u height=%u pixels=%zu success=1",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        prepared.RequestPath.c_str(),
                        prepared.LogicalPath.c_str(),
                        outSplit.Width,
                        outSplit.Height,
                        pixelCount);
        return true;
    }
}
